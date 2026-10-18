#include "bebisc.h"
#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path) {
  FILE *fptr = fopen(path, "r");
  if (!fptr)
    return NULL;

  // file size
  fseek(fptr, 0, SEEK_END);
  long size = ftell(fptr);
  rewind(fptr);

  if (size < 0) {
    fclose(fptr);
    return NULL;
  }

  char *buffer = malloc((unsigned)size + 1);
  if (!buffer) {
    fclose(fptr);
    return NULL;
  }

  size_t read = fread(buffer, 1, (long unsigned int)size, fptr);
  buffer[read] = '\0';

  fclose(fptr);
  return buffer;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <dosya.bc>\n", argv[0]);
    return 1;
  }

  char *source = read_file(argv[1]);
  if (!source) {
    fprintf(stderr, "err: '%s' file could not be read.\n", argv[1]);
    return 1;
  }

  int token_count = 0;
  Token *tokens = tokenize(source, &token_count);

  Parser parser = {tokens, 0, 0};
  ASTNode *root = parse_program(&parser);

  // the AST keeps its own copies of every lexeme
  tokens_free(tokens, token_count);

  if (parser.had_error) {
    free_ast(root);
    free(source);
    return 1;
  }

#ifdef DEBUG
  print_ast(root, 0);
#endif

  int status = run_program(root);

  free_ast(root);
  free(source);
  return status > 0 ? 1 : 0;
}
