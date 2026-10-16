#include "bebisc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  VALUE_NUMBER,
  VALUE_FLOAT,
  VALUE_BOOL,
  VALUE_STRING,
  VALUE_MEMBER,
} ValueType;

typedef struct {
  ValueType type;

  int number;
  float float_value;
  int bool_value;

  const char *string;

  const char *member;
} Value;

typedef struct Binding {
  const char *name;
  Value value;
  struct Binding *next;
} Binding;

typedef struct Handler {
  ASTNode *node;
  struct Handler *next;
} Handler;

typedef struct Struct {
  ASTNode *node;
  struct Struct *next;
} Struct;

static int emit_event(ASTNode *emit, Handler *handlers, Struct *structs);
static int exec_body(ASTNode *stmt, Binding *bindings, Handler *handlers,
                     Struct *structs);

static Value value_from_node(ASTNode *node) {
  Value value = {VALUE_STRING, 0, 0.0f, 0, "", NULL};

  if (node == NULL)
    return value;

  switch (node->type) {
  case NODE_NUMBER_LITERAL:
    value.type = VALUE_NUMBER;
    value.number = node->decl.number_value;
    break;
  case NODE_FLOAT_LITERAL:
    value.type = VALUE_FLOAT;
    value.float_value = node->decl.float_value;
    break;
  case NODE_BOOL_LITERAL:
    value.type = VALUE_BOOL;
    value.bool_value = node->decl.bool_value;
    break;
  case NODE_STRING_LITERAL:
    value.type = VALUE_STRING;
    value.string = node->decl.string_value;
    break;
  case NODE_IDENTIFIER:
    value.type = VALUE_STRING;
    value.string = node->decl.identifier.name;
    break;
  case NODE_MEMBER_ACCESS:
    value.type = VALUE_MEMBER;
    value.string = node->decl.member_access.object->decl.identifier.name;
    value.member = node->decl.member_access.member;
    break;
  default:
    break;
  }

  return value;
}

static void value_print(Value value) {
  switch (value.type) {
  case VALUE_NUMBER:
    printf("%d", value.number);
    break;
  case VALUE_FLOAT:
    printf("%g", (double)value.float_value);
    break;
  case VALUE_BOOL:
    printf("%s", value.bool_value ? "true" : "false");
    break;
  case VALUE_STRING:
    printf("%s", value.string);
    break;
  case VALUE_MEMBER:
    printf("%s.%s", value.string, value.member);
    break;
  }
}

static Binding *bind_add(Binding *list, const char *name, Value value) {
  Binding *binding = malloc(sizeof(Binding));
  binding->name = name;
  binding->value = value;
  binding->next = list;
  return binding;
}

static void bindings_free(Binding *list) {
  while (list != NULL) {
    Binding *next = list->next;
    free(list);
    list = next;
  }
}

static Binding *bindings_find(Binding *list, const char *name, size_t length) {
  for (Binding *binding = list; binding != NULL; binding = binding->next) {
    if (strlen(binding->name) == length &&
        strncmp(binding->name, name, length) == 0)
      return binding;
  }
  return NULL;
}

static ASTNode *find_emit_arg(ASTNode *args, const char *name) {
  for (ASTNode *arg = args; arg != NULL; arg = arg->next) {
    if (strcmp(arg->decl.emit_arg.name, name) == 0)
      return arg;
  }
  return NULL;
}

static ASTNode *find_field(ASTNode *fields, const char *name) {
  for (ASTNode *field = fields; field != NULL; field = field->next) {
    if (strcmp(field->decl.struct_field.name, name) == 0)
      return field;
  }
  return NULL;
}

static ASTNode *find_field_len(ASTNode *fields, const char *name,
                               size_t length) {
  for (ASTNode *field = fields; field != NULL; field = field->next) {
    if (strlen(field->decl.struct_field.name) == length &&
        strncmp(field->decl.struct_field.name, name, length) == 0)
      return field;
  }
  return NULL;
}

static ASTNode *find_struct_by_name(Struct *structs, const char *name) {
  for (Struct *s = structs; s != NULL; s = s->next) {
    if (strcmp(s->node->decl.struct_decl.name, name) == 0)
      return s->node;
  }
  return NULL;
}

static int type_accepts(const char *type_name, Value value) {
  if (strcmp(type_name, "bonibon") == 0)
    return value.type == VALUE_NUMBER;
  if (strcmp(type_name, "pamukseker") == 0)
    return value.type == VALUE_FLOAT;
  if (strcmp(type_name, "falim") == 0)
    return value.type == VALUE_BOOL;
  if (strcmp(type_name, "ciklet") == 0)
    return value.type == VALUE_STRING;
  return value.type == VALUE_MEMBER && strcmp(value.string, type_name) == 0;
}

static const char *value_type_name(Value value) {
  switch (value.type) {
  case VALUE_NUMBER:
    return "bonibon";
  case VALUE_FLOAT:
    return "pamukseker";
  case VALUE_BOOL:
    return "falim";
  case VALUE_STRING:
    return "ciklet";
  case VALUE_MEMBER:
    return value.string;
  }
  return "?";
}

static int make_default(const char *type_name, Value *out) {
  if (strcmp(type_name, "bonibon") == 0) {
    *out = (Value){VALUE_NUMBER, 0, 0.0f, 0, "", NULL};
    return 1;
  }
  if (strcmp(type_name, "pamukseker") == 0) {
    *out = (Value){VALUE_FLOAT, 0, 0.0f, 0, "", NULL};
    return 1;
  }
  if (strcmp(type_name, "falim") == 0) {
    *out = (Value){VALUE_BOOL, 0, 0.0f, 0, "", NULL};
    return 1;
  }
  if (strcmp(type_name, "ciklet") == 0) {
    *out = (Value){VALUE_STRING, 0, 0.0f, 0, "", NULL};
    return 1;
  }
  return 0;
}

static int check_field(ASTNode *schema, const char *name, ASTNode *value_node,
                       int line) {
  ASTNode *field = find_field(schema->decl.struct_decl.fields, name);
  if (field == NULL) {
    fprintf(stderr, "error: line %d: struct '%s' has no field '%s'\n", line,
            schema->decl.struct_decl.name, name);
    return 1;
  }

  if (value_node == NULL)
    return 0;

  Value value = value_from_node(value_node);
  if (!type_accepts(field->decl.struct_field.type_name, value)) {
    fprintf(stderr, "error: line %d: field '%s' expects %s, got %s\n", line,
            name, field->decl.struct_field.type_name, value_type_name(value));
    return 1;
  }
  return 0;
}

static int check_handler_body(ASTNode *handler, ASTNode *schema) {
  int errors = 0;

  for (ASTNode *stmt = handler->decl.event_handler.body; stmt != NULL;
       stmt = stmt->next) {
    if (stmt->type != NODE_PRINT_STMT)
      continue;

    ASTNode *value = stmt->decl.print_stmt.value;
    if (value == NULL || value->type != NODE_STRING_LITERAL)
      continue;

    const char *text = value->decl.string_value;
    for (int i = 0; text[i] != '\0'; i++) {
      if (text[i] != '{')
        continue;

      int end = i + 1;
      while (text[end] != '\0' && text[end] != '}')
        end++;

      if (text[end] != '}')
        continue;

      size_t len = (size_t)(end - i - 1);
      if (find_field_len(schema->decl.struct_decl.fields, &text[i + 1], len) ==
          NULL) {
        fprintf(stderr, "error: line %d: unknown binding '{%.*s}'\n",
                stmt->line, (int)len, &text[i + 1]);
        errors++;
      }
      i = end;
    }
  }

  return errors;
}

static int check_program(ASTNode *program, Struct *structs) {
  int errors = 0;

  for (ASTNode *node = program; node != NULL; node = node->next) {
    if (node->type == NODE_EMIT) {
      const char *type_name = node->decl.emit.type_name;
      ASTNode *schema = find_struct_by_name(structs, type_name);

      if (schema == NULL) {
        fprintf(stderr, "error: line %d: unknown event type '%s'\n", node->line,
                type_name);
        errors++;
        continue;
      }

      for (ASTNode *arg = node->decl.emit.args; arg != NULL; arg = arg->next) {
        const char *name = arg->decl.emit_arg.name;
        if (strcmp(name, "id") == 0)
          continue;
        errors +=
            check_field(schema, name, arg->decl.emit_arg.value, node->line);
      }
    } else if (node->type == NODE_EVENT_HANDLER) {
      const char *type_name = node->decl.event_handler.type_name;
      ASTNode *schema = find_struct_by_name(structs, type_name);

      if (schema == NULL) {
        fprintf(stderr, "error: line %d: unknown event type '%s'\n", node->line,
                type_name);
        errors++;
        continue;
      }

      errors += check_handler_body(node, schema);
    }
  }

  return errors;
}

static const char *event_id(ASTNode *args) {
  ASTNode *arg = find_emit_arg(args, "id");
  if (arg == NULL)
    return NULL;

  Value value = value_from_node(arg->decl.emit_arg.value);
  if (value.type != VALUE_STRING)
    return NULL;
  return value.string;
}

static int handler_matches(ASTNode *handler, const char *id) {
  const char *expected = handler->decl.event_handler.id;

  if (expected == NULL)
    return 1;
  if (id == NULL)
    return 0;
  return strcmp(expected, id) == 0;
}

static int print_template(const char *text, Binding *bindings, int line) {
  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] != '{')
      continue;

    int end = i + 1;
    while (text[end] != '\0' && text[end] != '}')
      end++;

    if (text[end] != '}')
      continue;

    size_t len = (size_t)(end - i - 1);
    if (bindings_find(bindings, &text[i + 1], len) == NULL) {
      fprintf(stderr, "error: line %d: no value for '{%.*s}'\n", line, (int)len,
              &text[i + 1]);
      return 1;
    }
    i = end;
  }

  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] != '{') {
      putchar(text[i]);
      continue;
    }

    int end = i + 1;
    while (text[end] != '\0' && text[end] != '}')
      end++;

    if (text[end] == '}') {
      Binding *binding =
          bindings_find(bindings, &text[i + 1], (size_t)(end - i - 1));
      if (binding != NULL)
        value_print(binding->value);
      i = end;
    } else {
      putchar(text[i]);
    }
  }
  putchar('\n');
  return 0;
}

static Binding *bind_handler_fields(ASTNode *schema, ASTNode *args) {
  Binding *bindings = NULL;

  for (ASTNode *field = schema->decl.struct_decl.fields; field != NULL;
       field = field->next) {
    const char *name = field->decl.struct_field.name;
    const char *type = field->decl.struct_field.type_name;

    ASTNode *arg = find_emit_arg(args, name);
    Value value;

    if (arg != NULL)
      value = value_from_node(arg->decl.emit_arg.value);
    else if (!make_default(type, &value))
      continue;

    bindings = bind_add(bindings, name, value);
  }

  return bindings;
}

static int emit_event(ASTNode *emit, Handler *handlers, Struct *structs) {
  const char *type_name = emit->decl.emit.type_name;
  ASTNode *args = emit->decl.emit.args;
  const char *id = event_id(args);

  ASTNode *schema = find_struct_by_name(structs, type_name);
  if (schema == NULL)
    return 0;

  int errors = 0;
  for (Handler *h = handlers; h != NULL; h = h->next) {
    ASTNode *handler = h->node;

    if (strcmp(handler->decl.event_handler.type_name, type_name) != 0)
      continue;
    if (!handler_matches(handler, id))
      continue;

    Binding *bindings = bind_handler_fields(schema, args);
    errors += exec_body(handler->decl.event_handler.body, bindings, handlers,
                        structs);
    bindings_free(bindings);
  }

  return errors;
}

static int exec_body(ASTNode *stmt, Binding *bindings, Handler *handlers,
                     Struct *structs) {
  int errors = 0;

  for (; stmt != NULL; stmt = stmt->next) {
    if (stmt->type == NODE_PRINT_STMT)
      errors += print_template(stmt->decl.print_stmt.value->decl.string_value,
                               bindings, stmt->line);
    else if (stmt->type == NODE_EMIT)
      errors += emit_event(stmt, handlers, structs);
  }

  return errors;
}

static void free_structs(Struct *list) {
  while (list != NULL) {
    Struct *next = list->next;
    free(list);
    list = next;
  }
}

static void free_handlers(Handler *list) {
  while (list != NULL) {
    Handler *next = list->next;
    free(list);
    list = next;
  }
}

int run_program(ASTNode *program) {
  Struct *structs = NULL;
  Struct *structs_tail = NULL;

  Handler *handlers = NULL;
  Handler *handlers_tail = NULL;

  for (ASTNode *node = program; node != NULL; node = node->next) {
    if (node->type == NODE_STRUCT_DECL) {
      Struct *s = malloc(sizeof(Struct));
      s->node = node;
      s->next = NULL;

      if (structs == NULL)
        structs = s;
      else
        structs_tail->next = s;
      structs_tail = s;
    } else if (node->type == NODE_EVENT_HANDLER) {
      Handler *handler = malloc(sizeof(Handler));
      handler->node = node;
      handler->next = NULL;

      if (handlers == NULL)
        handlers = handler;
      else
        handlers_tail->next = handler;
      handlers_tail = handler;
    }
  }

  int errors = check_program(program, structs);
  if (errors > 0) {
    free_structs(structs);
    free_handlers(handlers);
    return errors;
  }

  int run_errors = 0;
  for (ASTNode *node = program; node != NULL; node = node->next) {
    if (node->type == NODE_EMIT)
      run_errors += emit_event(node, handlers, structs);
  }

  free_structs(structs);
  free_handlers(handlers);

  return run_errors;
}
