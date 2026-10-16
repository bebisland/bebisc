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

#endif // BEBISC_H
