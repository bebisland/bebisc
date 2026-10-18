#include "bebisc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Token advance(Parser *parser) {
  return parser->tokens[parser->current++];
}
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

static int is_type_keyword(TokenType type) {
  return type == TOKEN_BONIBON || type == TOKEN_FALIM ||
         type == TOKEN_PAMUKSEKER || type == TOKEN_CIKLET;
}

static int parse_named_args(Parser *parser, ASTNode **head, ASTNode **tail) {
  if (!match(parser, TOKEN_LBRACE))
    return 0;

  while (!check(parser, TOKEN_RBRACE) && !is_at_end(parser)) {
    Token name = advance(parser);
    if (!match(parser, TOKEN_ASSIGN))
      return 0;

    ASTNode *value = parse_value(parser);
    if (value == NULL)
      return 0;

    ASTNode *arg = ast_node_new(NODE_EMIT_ARG);
    arg->line = name.line;
    arg->decl.emit_arg.name = strdup(name.lex);
    arg->decl.emit_arg.value = value;
    append(head, tail, arg);

    if (!match(parser, TOKEN_COMMA))
      break;
  }

  if (!match(parser, TOKEN_RBRACE))
    return 0;

  return 1;
}

static ASTNode *parse_value(Parser *parser) {
  Token token = peek(parser);

  switch (token.type) {
  case TOKEN_STRING: {
    advance(parser);
    ASTNode *node = ast_node_new(NODE_STRING_LITERAL);
    node->line = token.line;
    node->decl.string_value = strdup(token.lex);
    return node;
  }
  case TOKEN_NUMBER: {
    advance(parser);
    ASTNode *node = ast_node_new(NODE_NUMBER_LITERAL);
    node->line = token.line;
    node->decl.number_value = atoi(token.lex);
    return node;
  }
  case TOKEN_FLOAT: {
    advance(parser);
    ASTNode *node = ast_node_new(NODE_FLOAT_LITERAL);
    node->line = token.line;
    node->decl.float_value = (float)atof(token.lex);
    return node;
  }
  case TOKEN_BOOL: {
    advance(parser);
    ASTNode *node = ast_node_new(NODE_BOOL_LITERAL);
    node->line = token.line;
    // pipi/true are the truthy spellings, popo/false the falsy ones
    node->decl.bool_value =
        (strcmp(token.lex, "pipi") == 0 || strcmp(token.lex, "true") == 0) ? 1
                                                                           : 0;
    return node;
  }
  case TOKEN_DOLLAR: {
    advance(parser);
    Token name = advance(parser);
    if (name.type != TOKEN_IDENTIFIER)
      return NULL;

    ASTNode *node = ast_node_new(NODE_VAR_REF);
    node->line = name.line;
    node->decl.var_ref.name = strdup(name.lex);

    // $name[i][j]...
    while (match(parser, TOKEN_LBRACKET)) {
      Token index = advance(parser);
      if (index.type != TOKEN_NUMBER) {
        free_ast(node);
        return NULL;
      }
      if (!match(parser, TOKEN_RBRACKET)) {
        free_ast(node);
        return NULL;
      }

      ASTNode *index_node = ast_node_new(NODE_INDEX);
      index_node->line = index.line;
      index_node->decl.index.object = node;
      index_node->decl.index.index = atoi(index.lex);
      node = index_node;
    }

    return node;
  }
  case TOKEN_IDENTIFIER: {
    advance(parser);

    ASTNode *node = identifier(token.lex);
    node->line = token.line;

    // Enum.Member
    if (match(parser, TOKEN_DOT)) {
      Token member = advance(parser);

      ASTNode *member_node = ast_node_new(NODE_MEMBER_ACCESS);
      member_node->line = member.line;
      member_node->decl.member_access.object = node;
      member_node->decl.member_access.member = strdup(member.lex);
      return member_node;
    }

    return node;
  }
  default:
    return NULL;
  }
}

static ASTNode *parse_enum(Parser *parser) {
  advance(parser);

  Token name = advance(parser);
  if (name.type != TOKEN_IDENTIFIER)
    return NULL;

  if (!match(parser, TOKEN_LBRACE))
    return NULL;

  ASTNode *node = ast_node_new(NODE_ENUM_DECL);
  node->line = name.line;
  node->decl.enum_decl.name = strdup(name.lex);
  node->decl.enum_decl.members = NULL;

  ASTNode *tail = NULL;
  while (!is_at_end(parser) && !check(parser, TOKEN_RBRACE)) {
    Token member_name = advance(parser);
    if (member_name.type != TOKEN_IDENTIFIER)
      goto fail;

    ASTNode *member = ast_node_new(NODE_ENUM_MEMBER);
    member->line = member_name.line;
    member->decl.enum_member.name = strdup(member_name.lex);
    append(&node->decl.enum_decl.members, &tail, member);

    if (!match(parser, TOKEN_COMMA))
      break;
  }

  if (!match(parser, TOKEN_RBRACE))
    goto fail;

  return node;

fail:
  free_ast(node);
  return NULL;
}

static ASTNode *parse_struct(Parser *parser) {
  advance(parser);

  Token name = advance(parser);
  if (name.type != TOKEN_IDENTIFIER)
    return NULL;

  if (!match(parser, TOKEN_LBRACE))
    return NULL;

  ASTNode *node = ast_node_new(NODE_STRUCT_DECL);
  node->line = name.line;
  node->decl.struct_decl.name = strdup(name.lex);
  node->decl.struct_decl.fields = NULL;

  ASTNode *tail = NULL;
  while (!is_at_end(parser) && !check(parser, TOKEN_RBRACE)) {
    Token field_name = advance(parser);
    if (field_name.type != TOKEN_IDENTIFIER)
      goto fail;
    if (!match(parser, TOKEN_COLON))
      goto fail;
    Token type_name = advance(parser);
    if (type_name.type != TOKEN_IDENTIFIER && !is_type_keyword(type_name.type))
      goto fail;

    ASTNode *field = ast_node_new(NODE_STRUCT_FIELD);
    field->line = field_name.line;
    field->decl.struct_field.name = strdup(field_name.lex);
    field->decl.struct_field.type_name = strdup(type_name.lex);
    append(&node->decl.struct_decl.fields, &tail, field);

    match(parser, TOKEN_COMMA);
  }

  if (!match(parser, TOKEN_RBRACE))
    goto fail;

  return node;

fail:
  free_ast(node);
  return NULL;
}

static ASTNode *parse_handler(Parser *parser) {
  Token keyword = advance(parser);

  if (!match(parser, TOKEN_LPAREN))
    return NULL;

  Token type_name = advance(parser);
  if (type_name.type != TOKEN_IDENTIFIER)
    return NULL;

  ASTNode *node = ast_node_new(NODE_EVENT_HANDLER);
  node->line = keyword.line;
  node->decl.event_handler.type_name = strdup(type_name.lex);
  node->decl.event_handler.id = NULL;
  node->decl.event_handler.body = NULL;

  // optional: id="..."
  if (match(parser, TOKEN_COMMA)) {
    Token name = advance(parser);
    if (strcmp(name.lex, "id") != 0)
      goto fail;
    if (!match(parser, TOKEN_ASSIGN))
      goto fail;

    ASTNode *id_value = parse_value(parser);
    const char *id = id_string(id_value);
    if (id == NULL) {
      free_ast(id_value);
      goto fail;
    }
    node->decl.event_handler.id = strdup(id);
    free_ast(id_value);
  }

  if (!match(parser, TOKEN_RPAREN))
    goto fail;
  if (!match(parser, TOKEN_LBRACE))
    goto fail;

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
    goto fail;

  return node;

fail:
  free_ast(node);
  return NULL;
}

static ASTNode *parse_emit(Parser *parser) {
  Token keyword = advance(parser);

  if (!match(parser, TOKEN_LPAREN))
    return NULL;

  Token type_name = advance(parser);
  if (type_name.type != TOKEN_IDENTIFIER)
    return NULL;

  ASTNode *node = ast_node_new(NODE_EMIT);
  node->line = keyword.line;
  node->decl.emit.type_name = strdup(type_name.lex);
  node->decl.emit.args = NULL;

  ASTNode *tail = NULL;

  //   pirt(Type, {field=value, ...} [, id="..."])
  while (match(parser, TOKEN_COMMA)) {
    if (check(parser, TOKEN_LBRACE)) {
      if (!parse_named_args(parser, &node->decl.emit.args, &tail))
        goto fail;
    } else if (check(parser, TOKEN_IDENTIFIER)) {
      Token name = advance(parser);
      if (!match(parser, TOKEN_ASSIGN))
        goto fail;

      ASTNode *value = parse_value(parser);
      if (value == NULL)
        goto fail;

      ASTNode *arg = ast_node_new(NODE_EMIT_ARG);
      arg->decl.emit_arg.name = strdup(name.lex);
      arg->decl.emit_arg.value = value;
      append(&node->decl.emit.args, &tail, arg);
    } else {
      goto fail;
    }
  }

  if (!match(parser, TOKEN_RPAREN))
    goto fail;

  return node;

fail:
  free_ast(node);
  return NULL;
}

static ASTNode *parse_print(Parser *parser) {
  Token keyword = advance(parser);

  if (!match(parser, TOKEN_LPAREN))
    return NULL;

  ASTNode *value = parse_value(parser);
  if (value == NULL)
    return NULL;

  if (!match(parser, TOKEN_RPAREN)) {
    free_ast(value);
    return NULL;
  }

  ASTNode *node = ast_node_new(NODE_PRINT_STMT);
  node->line = keyword.line;
  node->decl.print_stmt.value = value;

  return node;
}

// $name[i] = value
static ASTNode *parse_dollar_statement(Parser *parser) {
  Token dollar = advance(parser);

  Token name = advance(parser);
  if (name.type != TOKEN_IDENTIFIER)
    return NULL;

  if (!match(parser, TOKEN_LBRACKET))
    return NULL;

  Token index = advance(parser);
  if (index.type != TOKEN_NUMBER)
    return NULL;
  if (!match(parser, TOKEN_RBRACKET))
    return NULL;
  if (!match(parser, TOKEN_ASSIGN))
    return NULL;

  ASTNode *value = parse_value(parser);
  if (value == NULL)
    return NULL;

  ASTNode *node = ast_node_new(NODE_INDEX_SET);
  node->line = dollar.line;
  node->decl.index_set.name = strdup(name.lex);
  node->decl.index_set.index = atoi(index.lex);
  node->decl.index_set.value = value;
  return node;
}

static ASTNode *parse_field_statement(Parser *parser) {
  Token name = advance(parser);

  if (!match(parser, TOKEN_ASSIGN))
    return NULL;

  ASTNode *value = parse_value(parser);
  if (value == NULL)
    return NULL;

  ASTNode *node = ast_node_new(NODE_FIELD_SET);
  node->line = name.line;
  node->decl.field_set.name = strdup(name.lex);
  node->decl.field_set.value = value;
  return node;
}

// ye(v1, v2, ...) :o $name
static ASTNode *parse_flow(Parser *parser) {
  Token keyword = advance(parser);

  if (!match(parser, TOKEN_LPAREN))
    return NULL;

  ASTNode *node = ast_node_new(NODE_FLOW);
  node->line = keyword.line;
  node->decl.flow.name = NULL;
  node->decl.flow.args = NULL;

  ASTNode *tail = NULL;
  if (!check(parser, TOKEN_RPAREN)) {
    do {
      ASTNode *value = parse_value(parser);
      if (value == NULL)
        goto fail;
      append(&node->decl.flow.args, &tail, value);
    } while (match(parser, TOKEN_COMMA));
  }

  if (!match(parser, TOKEN_RPAREN))
    goto fail;
  if (!match(parser, TOKEN_WOW))
    goto fail;

  if (!match(parser, TOKEN_DOLLAR))
    goto fail;
  Token name = advance(parser);
  if (name.type != TOKEN_IDENTIFIER)
    goto fail;
  node->decl.flow.name = strdup(name.lex);

  return node;

fail:
  free_ast(node);
  return NULL;
}

static ASTNode *parse_statement(Parser *parser) {
  switch (peek(parser).type) {
  case TOKEN_CIKLE:
    return parse_print(parser);
  case TOKEN_PIRT:
    return parse_emit(parser);
  case TOKEN_YE:
    return parse_flow(parser);
  case TOKEN_DOLLAR:
    return parse_dollar_statement(parser);
  case TOKEN_IDENTIFIER:
    return parse_field_statement(parser);
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
  case TOKEN_CIKLE:
    return parse_print(parser);
  case TOKEN_YE:
    return parse_flow(parser);
  case TOKEN_DOLLAR:
    return parse_dollar_statement(parser);
  default:
    return NULL;
  }
}

ASTNode *parse_program(Parser *parser) {
  ASTNode *head = NULL;
  ASTNode *tail = NULL;

  while (!is_at_end(parser)) {
    int start = parser->current;
    ASTNode *node = parse_declaration(parser);
    if (node == NULL) {
      int already_reported = 0;
      int at = start;
      for (int i = start; i <= parser->current; i++) {
        if (parser->tokens[i].type == TOKEN_EOF) {
          at = start;
          break;
        }
        if (parser->tokens[i].type == TOKEN_UNKNOWN) {
          already_reported = 1;
          break;
        }
        at = i;
        if (i == parser->current)
          break;
      }

      if (!already_reported) {
        Token token = parser->tokens[at];
        fprintf(stderr, "error: line %d: unexpected '%s'\n", token.line,
                token.lex);
      }
      parser->had_error = 1;
      break;
    }
    append(&head, &tail, node);
  }

  return head;
}
