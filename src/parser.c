#include "bebisc.h"
#include <stdlib.h>
#include <string.h>

Token advance(Parser *parser) { return parser->tokens[parser->current++]; }
static Token peek(Parser *parser) { return parser->tokens[parser->current]; }
static int is_at_end(Parser *parser) { return peek(parser).type == TOKEN_EOF; }
static int check(Parser *parser, TokenType type) {
  return peek(parser).type == type;
}
static int match(Parser *parser, TokenType type) {
  if (!check(parser, type))
    return 0;
  advance(parser);
  return 1;
}
static void append(ASTNode **head, ASTNode **tail, ASTNode *node) {
  if (*head == NULL) {
    *head = node;
    *tail = node;
  } else {
    (*tail)->next = node;
    *tail = node;
  }
}

static ASTNode *parse_declaration(Parser *parser);
static ASTNode *parse_statement(Parser *parser);
static ASTNode *parse_value(Parser *parser);

static ASTNode *identifier(const char *name) {
  ASTNode *node = ast_node_new(NODE_IDENTIFIER);
  node->decl.identifier.name = strdup(name);
  return node;
}

static const char *id_string(ASTNode *value) {
  if (value == NULL)
    return NULL;
  if (value->type == NODE_STRING_LITERAL)
    return value->decl.string_value;
  if (value->type == NODE_IDENTIFIER)
    return value->decl.identifier.name;
  return NULL;
}

static ASTNode *parse_value(Parser *parser) {
  Token token = peek(parser);

  switch (token.type) {
  case TOKEN_STRING: {
    advance(parser);
    ASTNode *node = ast_node_new(NODE_STRING_LITERAL);
    node->decl.string_value = strdup(token.lex);
    return node;
  }
  case TOKEN_NUMBER: {
    advance(parser);
    ASTNode *node = ast_node_new(NODE_NUMBER_LITERAL);
    node->decl.number_value = atoi(token.lex);
    return node;
  }
  case TOKEN_FLOAT: {
    advance(parser);
    ASTNode *node = ast_node_new(NODE_FLOAT_LITERAL);
    node->decl.float_value = (float)atof(token.lex);
    return node;
  }
  case TOKEN_BOOL: {
    advance(parser);
    ASTNode *node = ast_node_new(NODE_BOOL_LITERAL);
    node->decl.bool_value = (strcmp(token.lex, "true") == 0) ? 1 : 0;
    return node;
  }
  case TOKEN_IDENTIFIER: {
    advance(parser);
    if (match(parser, TOKEN_DOT)) {
      Token member = advance(parser);
      ASTNode *node = ast_node_new(NODE_MEMBER_ACCESS);
      node->decl.member_access.object = identifier(token.lex);
      node->decl.member_access.member = strdup(member.lex);
      return node;
    }
    return identifier(token.lex);
  }
  default:
    return NULL;
  }
}

static ASTNode *parse_enum(Parser *parser) {
  advance(parser);

  Token name = advance(parser);

  if (!match(parser, TOKEN_LBRACE))
    return NULL;

  ASTNode *node = ast_node_new(NODE_ENUM_DECL);
  node->decl.enum_decl.name = strdup(name.lex);
  node->decl.enum_decl.members = NULL;

  ASTNode *tail = NULL;
  while (!is_at_end(parser) && !check(parser, TOKEN_RBRACE)) {
    Token member_name = advance(parser);

    ASTNode *member = ast_node_new(NODE_ENUM_MEMBER);
    member->decl.enum_member.name = strdup(member_name.lex);
    append(&node->decl.enum_decl.members, &tail, member);

    if (!match(parser, TOKEN_COMMA))
      break;
  }

  if (!match(parser, TOKEN_RBRACE))
    return NULL;

  return node;
}

static ASTNode *parse_struct(Parser *parser) {
  advance(parser);

  Token name = advance(parser);

  if (!match(parser, TOKEN_LBRACE))
    return NULL;

  ASTNode *node = ast_node_new(NODE_STRUCT_DECL);
  node->decl.struct_decl.name = strdup(name.lex);
  node->decl.struct_decl.fields = NULL;

  ASTNode *tail = NULL;
  while (!is_at_end(parser) && !check(parser, TOKEN_RBRACE)) {
    Token field_name = advance(parser);
    if (!match(parser, TOKEN_COLON))
      return NULL;
    Token type_name = advance(parser);

    ASTNode *field = ast_node_new(NODE_STRUCT_FIELD);
    field->decl.struct_field.name = strdup(field_name.lex);
    field->decl.struct_field.type_name = strdup(type_name.lex);
    append(&node->decl.struct_decl.fields, &tail, field);

    match(parser, TOKEN_COMMA);
  }

  if (!match(parser, TOKEN_RBRACE))
    return NULL;

  return node;
}

static ASTNode *parse_handler(Parser *parser) {
  Token keyword = advance(parser);

  if (!match(parser, TOKEN_LPAREN))
    return NULL;

  Token type_name = advance(parser);

  ASTNode *node = ast_node_new(NODE_EVENT_HANDLER);
  node->line = keyword.line;
  node->decl.event_handler.type_name = strdup(type_name.lex);
  node->decl.event_handler.id = NULL;
  node->decl.event_handler.body = NULL;

  // optional: id="..."
  if (match(parser, TOKEN_COMMA)) {
    Token name = advance(parser);
    if (strcmp(name.lex, "id") != 0)
      return NULL;
    if (!match(parser, TOKEN_ASSIGN))
      return NULL;
    const char *id = id_string(parse_value(parser));
    if (id == NULL)
      return NULL;
    node->decl.event_handler.id = strdup(id);
  }

  if (!match(parser, TOKEN_RPAREN))
    return NULL;
  if (!match(parser, TOKEN_LBRACE))
    return NULL;

  ASTNode *body_head = NULL;
  ASTNode *body_tail = NULL;
  while (!is_at_end(parser) && !check(parser, TOKEN_RBRACE)) {
    ASTNode *stmt = parse_statement(parser);
    if (stmt == NULL)
      break;
    append(&body_head, &body_tail, stmt);
  }
  node->decl.event_handler.body = body_head;

  if (!match(parser, TOKEN_RBRACE))
    return NULL;

  return node;
}

static ASTNode *parse_emit(Parser *parser) {
  Token keyword = advance(parser);

  if (!match(parser, TOKEN_LPAREN))
    return NULL;

  Token type_name = advance(parser);

  ASTNode *node = ast_node_new(NODE_EMIT);
  node->line = keyword.line;
  node->decl.emit.type_name = strdup(type_name.lex);
  node->decl.emit.args = NULL;

  ASTNode *tail = NULL;

  //   pirt(Type, {field=value, ...} [, id="..."])
  while (match(parser, TOKEN_COMMA)) {
    if (match(parser, TOKEN_LBRACE)) {
      while (!check(parser, TOKEN_RBRACE) && !is_at_end(parser)) {
        Token name = advance(parser);
        if (!match(parser, TOKEN_ASSIGN))
          return NULL;

        ASTNode *value = parse_value(parser);
        if (value == NULL)
          return NULL;

        ASTNode *arg = ast_node_new(NODE_EMIT_ARG);
        arg->decl.emit_arg.name = strdup(name.lex);
        arg->decl.emit_arg.value = value;
        append(&node->decl.emit.args, &tail, arg);

        if (!match(parser, TOKEN_COMMA))
          break;
      }
      if (!match(parser, TOKEN_RBRACE))
        return NULL;
    } else if (check(parser, TOKEN_IDENTIFIER)) {
      // named arg
      Token name = advance(parser);
      if (!match(parser, TOKEN_ASSIGN))
        return NULL;

      ASTNode *value = parse_value(parser);
      if (value == NULL)
        return NULL;

      ASTNode *arg = ast_node_new(NODE_EMIT_ARG);
      arg->decl.emit_arg.name = strdup(name.lex);
      arg->decl.emit_arg.value = value;
      append(&node->decl.emit.args, &tail, arg);
    } else {
      return NULL;
    }
  }

  if (!match(parser, TOKEN_RPAREN))
    return NULL;

  return node;
}

static ASTNode *parse_print(Parser *parser) {
  Token keyword = advance(parser);

  if (!match(parser, TOKEN_LPAREN))
    return NULL;

  ASTNode *value = parse_value(parser);
  if (value == NULL)
    return NULL;

  if (!match(parser, TOKEN_RPAREN))
    return NULL;

  ASTNode *node = ast_node_new(NODE_PRINT_STMT);
  node->line = keyword.line;
  node->decl.print_stmt.value = value;

  return node;
}

static ASTNode *parse_statement(Parser *parser) {
  switch (peek(parser).type) {
  case TOKEN_CIKLE:
    return parse_print(parser);
  case TOKEN_PIRT:
    return parse_emit(parser);
  default:
    advance(parser);
    return NULL;
  }
}

static ASTNode *parse_declaration(Parser *parser) {
  switch (peek(parser).type) {
  case TOKEN_PUDRA:
    return parse_enum(parser);
  case TOKEN_PETIBOR:
    return parse_struct(parser);
  case TOKEN_KOKLA:
    return parse_handler(parser);
  case TOKEN_PIRT:
    return parse_emit(parser);
  default:
    advance(parser);
    return NULL;
  }
}

ASTNode *parse_program(Parser *parser) {
  ASTNode *head = NULL;
  ASTNode *tail = NULL;

  while (!is_at_end(parser)) {
    ASTNode *node = parse_declaration(parser);
    if (node == NULL) {
      // TODO: error info
      break;
    }
    append(&head, &tail, node);
  }

  return head;
}
