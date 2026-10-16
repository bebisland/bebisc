#ifndef BEBISC_H
#define BEBISC_H

// lexer.c
typedef enum {
  TOKEN_PUDRA = 0, // enum
  TOKEN_PETIBOR,   // struct
  TOKEN_KOKLA,     // event listener
  TOKEN_PIRT,      // event emitter
  TOKEN_CIKLE,     // print

  TOKEN_BONIBON,    // int
  TOKEN_FALIM,      // bool
  TOKEN_PAMUKSEKER, // float

  TOKEN_IDENTIFIER, // name for enum/struct/field vb.
  TOKEN_NUMBER,
  TOKEN_FLOAT,
  TOKEN_BOOL,
  TOKEN_STRING,

  TOKEN_COLON,  // :
  TOKEN_ASSIGN, // =
  TOKEN_COMMA,  // ,
  TOKEN_DOT,    // .
  TOKEN_LPAREN, // (
  TOKEN_RPAREN, // )
  TOKEN_LBRACE, // {
  TOKEN_RBRACE, // }

  TOKEN_EOF,
  TOKEN_UNKNOWN
} TokenType;

typedef struct {
  TokenType type;
  const char *lex;
  int line;
} Token;

Token *tokenize(const char *source, int *out_count);

// ast.c
typedef enum {
  NODE_ENUM_DECL,   // pudra
  NODE_ENUM_MEMBER, // enum case

  NODE_STRUCT_DECL,  // petibor
  NODE_STRUCT_FIELD, // field: type

  NODE_EVENT_HANDLER, // kokla
  NODE_HANDLER_PARAM, // bound field / id

  NODE_PRINT_STMT, // cikle

  NODE_EMIT,     // cikle
  NODE_EMIT_ARG, // name = value

  NODE_IDENTIFIER,
  NODE_MEMBER_ACCESS, // enum.member

  NODE_STRING_LITERAL,
  NODE_NUMBER_LITERAL,
  NODE_FLOAT_LITERAL,
  NODE_BOOL_LITERAL,
} NodeType;

typedef struct ASTNode {
  NodeType type;

  union {
    struct {
      char *name;
      struct ASTNode *members;
    } enum_decl;

    struct {
      char *name;
    } enum_member;

    struct {
      char *name;
      struct ASTNode *fields;
    } struct_decl;

    struct {
      char *name;
      char *type_name;
    } struct_field;

    struct {
      char *type_name;
      struct ASTNode *params;
      struct ASTNode *body;
    } event_handler;

    struct {
      char *name;
      struct ASTNode *value;
    } handler_param;

    struct {
      struct ASTNode *value;
    } print_stmt;

    struct {
      char *type_name;
      struct ASTNode *args;
    } emit;

    struct {
      char *name;
      struct ASTNode *value;
    } emit_arg;

    struct {
      char *name;
    } identifier;

    struct {
      struct ASTNode *object;
      char *member;
    } member_access;

    char *string_value;
    int number_value;
    float float_value;
    int bool_value;
  } decl;

  struct ASTNode *next;
} ASTNode;

ASTNode *ast_node_new(NodeType type);
void print_ast(ASTNode *node, int indent);

// parser.c
typedef struct {
  Token *tokens;
  int current;
} Parser;

Token advance(Parser *parser);
ASTNode *parse_program(Parser *parser);

#endif // BEBISC_H
