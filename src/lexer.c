#include "bebisc.h"
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static TokenType check_keyword(const char *text) {
  if (strcmp(text, "pudra") == 0)
    return TOKEN_PUDRA;
  if (strcmp(text, "petibor") == 0)
    return TOKEN_PETIBOR;
  if (strcmp(text, "kokla") == 0)
    return TOKEN_KOKLA;
  if (strcmp(text, "pirt") == 0)
    return TOKEN_PIRT;
  if (strcmp(text, "cikle") == 0)
    return TOKEN_CIKLE;
  if (strcmp(text, "bonibon") == 0)
    return TOKEN_BONIBON;
  if (strcmp(text, "falim") == 0)
    return TOKEN_FALIM;
  if (strcmp(text, "pamukseker") == 0)
    return TOKEN_PAMUKSEKER;
  if (strcmp(text, "ciklet") == 0)
    return TOKEN_CIKLET;
  if (strcmp(text, "true") == 0 || strcmp(text, "false") == 0)
    return TOKEN_BOOL;
  return TOKEN_IDENTIFIER;
}

typedef struct {
  Token *tokens;
  int count;
  int capacity;
} TokenList;

static void init_token_list(TokenList *list) {
  list->capacity = 64;
  list->count = 0;
  list->tokens = malloc(sizeof(Token) * (size_t)list->capacity);
}

static void add_token(TokenList *list, TokenType type, const char *lex,
                      int line) {
  if (list->count >= list->capacity) {
    list->capacity *= 2;
    list->tokens =
        realloc(list->tokens, sizeof(Token) * (size_t)list->capacity);
  }
  list->tokens[list->count++] = (Token){type, lex, line};
}

static char *slice(const char *source, int start, int end) {
  int length = end - start;
  char *lex = malloc((size_t)length + 1);
  memcpy(lex, &source[start], (size_t)length);
  lex[length] = '\0';
  return lex;
}

Token *tokenize(const char *source, int *out_count) {
  TokenList list;
  init_token_list(&list);

  int line = 1;
  int cursor = 0;
  int len = (int)strlen(source);

  while (cursor < len) {
    char c = source[cursor];

    if (c == '\n') {
      line++;
      cursor++;
      continue;
    }
    if (isspace((unsigned char)c)) {
      cursor++;
      continue;
    }

    if (c == '/' && cursor + 1 < len && source[cursor + 1] == '/') {
      while (cursor < len && source[cursor] != '\n') {
        cursor++;
      }
      continue;
    }

    switch (c) {
    case ':':
      add_token(&list, TOKEN_COLON, ":", line);
      cursor++;
      continue;
    case '=':
      add_token(&list, TOKEN_ASSIGN, "=", line);
      cursor++;
      continue;
    case ',':
      add_token(&list, TOKEN_COMMA, ",", line);
      cursor++;
      continue;
    case '.':
      add_token(&list, TOKEN_DOT, ".", line);
      cursor++;
      continue;
    case '(':
      add_token(&list, TOKEN_LPAREN, "(", line);
      cursor++;
      continue;
    case ')':
      add_token(&list, TOKEN_RPAREN, ")", line);
      cursor++;
      continue;
    case '{':
      add_token(&list, TOKEN_LBRACE, "{", line);
      cursor++;
      continue;
    case '}':
      add_token(&list, TOKEN_RBRACE, "}", line);
      cursor++;
      continue;
    case ';':
      line++;
      cursor++;
      continue;
    }

    if (c == '"') {
      cursor++;
      int start = cursor;
      while (cursor < len && source[cursor] != '"') {
        cursor++;
      }

      add_token(&list, TOKEN_STRING, slice(source, start, cursor), line);

      if (cursor < len)
        cursor++; // closing quote
      continue;
    }

    if (isdigit((unsigned char)c)) {
      int start = cursor;
      int is_float = 0;
      while (cursor < len && (isdigit((unsigned char)source[cursor]) ||
                              source[cursor] == '.')) {
        if (source[cursor] == '.') {
          is_float = 1;
        }
        cursor++;
      }

      add_token(&list, is_float ? TOKEN_FLOAT : TOKEN_NUMBER,
                slice(source, start, cursor), line);
      continue;
    }

    if (isalpha((unsigned char)c) || c == '_') {
      int start = cursor;
      while (cursor < len && (isalnum((unsigned char)source[cursor]) ||
                              source[cursor] == '_')) {
        cursor++;
      }
      char *lex = slice(source, start, cursor);

      TokenType type = check_keyword(lex);
      add_token(&list, type, lex, line);
      continue;
    }

    cursor++;
  }

  add_token(&list, TOKEN_EOF, "", line);

  *out_count = list.count;
  return list.tokens;
}
