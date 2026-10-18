#include "bebisc.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  VALUE_NUMBER,
  VALUE_FLOAT,
  VALUE_BOOL,
  VALUE_STRING,
  VALUE_MEMBER,
  VALUE_TUPLE, // result of ye(...)
} ValueType;

typedef struct Value {
  ValueType type;

  int number;
  float float_value;
  int bool_value;

  const char *string;

  const char *member;

  struct Tuple *tuple;
} Value;

typedef struct Tuple {
  Value *items;
  const char **types;
  int count;
} Tuple;

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} TextBuffer;

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

typedef struct EnumDecl {
  ASTNode *node;
  struct EnumDecl *next;
} EnumDecl;

typedef struct {
  Binding *fields;
  Binding *vars;
} Scope;

typedef struct {
  Handler *handlers;
  Struct *structs;
  EnumDecl *enums;
  Binding *globals;
} Runtime;

// guards against handlers that keep emitting their own event
#define MAX_EVENT_DEPTH 256

static int eval_value(ASTNode *node, Scope *scope, Runtime *runtime,
                      Value *out);
static int emit_event(ASTNode *emit, Scope *scope, Runtime *runtime, int depth);
static int exec_statement(ASTNode *stmt, Scope *scope, Runtime *runtime,
                          int depth, int in_handler);

static Value value_nil(void) {
  Value value = {.type = VALUE_STRING, .string = ""};
  return value;
}

static Value value_from_node(ASTNode *node) {
  Value value = value_nil();

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

// values whose meaning depends on runtime state
static int is_dynamic_expr(ASTNode *node) {
  if (node == NULL)
    return 0;

  switch (node->type) {
  case NODE_VAR_REF:
  case NODE_INDEX:
  case NODE_IDENTIFIER:
    return 1;
  default:
    return 0;
  }
}

static Value value_clone(Value value) {
  if (value.type == VALUE_TUPLE) {
    Tuple *copy = malloc(sizeof(Tuple));
    copy->count = value.tuple->count;
    copy->items = malloc(sizeof(Value) * (size_t)copy->count);
    copy->types = malloc(sizeof(char *) * (size_t)copy->count);
    for (int i = 0; i < copy->count; i++) {
      copy->items[i] = value_clone(value.tuple->items[i]);
      copy->types[i] = value.tuple->types[i];
    }
    value.tuple = copy;
  }

  // strings and members are borrowed from the AST and stay shared
  return value;
}

// only tuples own memory everything else is borrowed
static void value_free(Value value) {
  if (value.type == VALUE_TUPLE) {
    for (int i = 0; i < value.tuple->count; i++)
      value_free(value.tuple->items[i]);
    free(value.tuple->items);
    free(value.tuple->types);
    free(value.tuple);
  }
}

static int buffer_put(TextBuffer *buffer, const char *text, size_t length) {
  if (buffer->len + length + 1 > buffer->cap) {
    size_t cap = buffer->cap == 0 ? 64 : buffer->cap;
    while (cap < buffer->len + length + 1)
      cap *= 2;

    char *data = realloc(buffer->data, cap);
    if (data == NULL)
      return 0;

    buffer->data = data;
    buffer->cap = cap;
  }

  memcpy(buffer->data + buffer->len, text, length);
  buffer->len += length;
  buffer->data[buffer->len] = '\0';
  return 1;
}

static int buffer_puts(TextBuffer *buffer, const char *text) {
  return buffer_put(buffer, text, strlen(text));
}

static int value_append(TextBuffer *buffer, Value value) {
  char scratch[64];

  switch (value.type) {
  case VALUE_NUMBER:
    snprintf(scratch, sizeof(scratch), "%d", value.number);
    return buffer_puts(buffer, scratch);
  case VALUE_FLOAT:
    snprintf(scratch, sizeof(scratch), "%g", (double)value.float_value);
    return buffer_puts(buffer, scratch);
  case VALUE_BOOL:
    return buffer_puts(buffer, value.bool_value ? "true" : "false");
  case VALUE_STRING:
    return buffer_puts(buffer, value.string);
  case VALUE_MEMBER:
    if (!buffer_puts(buffer, value.string))
      return 0;
    if (value.member == NULL || value.member[0] == '\0')
      return 1;
    return buffer_puts(buffer, ".") && buffer_puts(buffer, value.member);
  case VALUE_TUPLE:
    if (!buffer_puts(buffer, "("))
      return 0;
    for (int i = 0; i < value.tuple->count; i++) {
      if (i > 0 && !buffer_puts(buffer, ", "))
        return 0;
      if (!value_append(buffer, value.tuple->items[i]))
        return 0;
    }
    return buffer_puts(buffer, ")");
  }
  return 1;
}

static Binding *bind_add(Binding *list, const char *name, Value value) {
  Binding *binding = malloc(sizeof(Binding));
  binding->name = name;
  binding->value = value;
  binding->next = list;
  return binding;
}

static void vars_free(Binding *list) {
  while (list != NULL) {
    Binding *next = list->next;
    value_free(list->value);
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

static Binding *find_var(const char *name, Scope *scope, Binding *globals) {
  Binding *var = bindings_find(scope->vars, name, strlen(name));
  if (var != NULL)
    return var;
  return bindings_find(globals, name, strlen(name));
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

static ASTNode *find_enum_by_name(EnumDecl *enums, const char *name) {
  for (EnumDecl *e = enums; e != NULL; e = e->next) {
    if (strcmp(e->node->decl.enum_decl.name, name) == 0)
      return e->node;
  }
  return NULL;
}

static int is_primitive_type(const char *type_name) {
  return strcmp(type_name, "bonibon") == 0 ||
         strcmp(type_name, "pamukseker") == 0 ||
         strcmp(type_name, "falim") == 0 || strcmp(type_name, "ciklet") == 0;
}

static int type_is_known(const char *type_name, EnumDecl *enums) {
  return is_primitive_type(type_name) ||
         (enums != NULL && find_enum_by_name(enums, type_name) != NULL);
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
  case VALUE_TUPLE:
    return "ye";
  }
  return "?";
}

static int make_default(const char *type_name, EnumDecl *enums, Value *out) {
  if (strcmp(type_name, "bonibon") == 0) {
    *out = (Value){.type = VALUE_NUMBER};
    return 1;
  }
  if (strcmp(type_name, "pamukseker") == 0) {
    *out = (Value){.type = VALUE_FLOAT};
    return 1;
  }
  if (strcmp(type_name, "falim") == 0) {
    *out = (Value){.type = VALUE_BOOL};
    return 1;
  }
  if (strcmp(type_name, "ciklet") == 0) {
    *out = (Value){.type = VALUE_STRING, .string = ""};
    return 1;
  }

  if (!type_is_known(type_name, enums))
    return 0;

  *out = (Value){.type = VALUE_MEMBER, .string = type_name, .member = ""};
  return 1;
}

static Value make_default_or_report(const char *type_name, EnumDecl *enums,
                                    int line, int *errors) {
  Value value;
  if (!make_default(type_name, enums, &value)) {
    fprintf(stderr, "error: line %d: unknown type '%s'\n", line, type_name);
    (*errors)++;
    return value_nil();
  }
  return value;
}

static int eval_value(ASTNode *node, Scope *scope, Runtime *runtime,
                      Value *out) {
  *out = value_nil();

  if (node == NULL)
    return 0;

  switch (node->type) {
  case NODE_VAR_REF: {
    Binding *var = find_var(node->decl.var_ref.name, scope, runtime->globals);
    if (var == NULL) {
      fprintf(stderr, "error: line %d: undefined variable '$%s'\n", node->line,
              node->decl.var_ref.name);
      return 1;
    }
    *out = value_clone(var->value);
    return 0;
  }
  case NODE_INDEX: {
    Value object;
    if (eval_value(node->decl.index.object, scope, runtime, &object))
      return 1;

    if (object.type != VALUE_TUPLE) {
      fprintf(stderr, "error: line %d: only ye() values can be indexed\n",
              node->line);
      value_free(object);
      return 1;
    }

    int index = node->decl.index.index;
    if (index < 0 || index >= object.tuple->count) {
      fprintf(stderr, "error: line %d: index %d out of range (0..%d)\n",
              node->line, index, object.tuple->count - 1);
      value_free(object);
      return 1;
    }

    *out = value_clone(object.tuple->items[index]);
    value_free(object);
    return 0;
  }
  case NODE_IDENTIFIER: {
    const char *name = node->decl.identifier.name;
    Binding *field = bindings_find(scope->fields, name, strlen(name));
    if (field != NULL) {
      *out = value_clone(field->value);
      return 0;
    }

    if (find_enum_by_name(runtime->enums, name) != NULL) {
      *out = (Value){.type = VALUE_MEMBER, .string = name, .member = ""};
      return 0;
    }

    fprintf(stderr, "error: line %d: unknown identifier '%s'\n", node->line,
            name);
    return 1;
  }
  default:
    *out = value_from_node(node);
    return 0;
  }
}

static void var_store(const char *name, Value value, Scope *scope,
                      Runtime *runtime, int in_handler) {
  Binding *var = find_var(name, scope, runtime->globals);
  if (var != NULL) {
    value_free(var->value);
    var->value = value;
    return;
  }

  if (in_handler)
    scope->vars = bind_add(scope->vars, name, value);
  else
    runtime->globals = bind_add(runtime->globals, name, value);
}

static int check_field(ASTNode *schema, const char *name, ASTNode *value_node,
                       int line) {
  ASTNode *field = find_field(schema->decl.struct_decl.fields, name);
  if (field == NULL) {
    fprintf(stderr, "error: line %d: struct '%s' has no field '%s'\n", line,
            schema->decl.struct_decl.name, name);
    return 1;
  }

  if (value_node == NULL || is_dynamic_expr(value_node))
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
      if (text[i + 1] != '$' && find_field_len(schema->decl.struct_decl.fields,
                                               &text[i + 1], len) == NULL) {
        fprintf(stderr, "error: line %d: unknown binding '{%.*s}'\n",
                stmt->line, (int)len, &text[i + 1]);
        errors++;
      }
      i = end;
    }
  }

  return errors;
}

static int check_emit_node(ASTNode *node, Struct *structs) {
  const char *type_name = node->decl.emit.type_name;
  ASTNode *schema = find_struct_by_name(structs, type_name);

  if (schema == NULL) {
    fprintf(stderr, "error: line %d: unknown event type '%s'\n", node->line,
            type_name);
    return 1;
  }

  int errors = 0;
  for (ASTNode *arg = node->decl.emit.args; arg != NULL; arg = arg->next) {
    const char *name = arg->decl.emit_arg.name;
    if (strcmp(name, "id") == 0)
      continue;
    errors += check_field(schema, name, arg->decl.emit_arg.value, node->line);
  }
  return errors;
}

static int check_field_set(ASTNode *stmt, ASTNode *schema) {
  const char *name = stmt->decl.field_set.name;
  ASTNode *field = find_field(schema->decl.struct_decl.fields, name);

  if (field == NULL) {
    fprintf(stderr, "error: line %d: struct '%s' has no field '%s'\n",
            stmt->line, schema->decl.struct_decl.name, name);
    return 1;
  }

  ASTNode *value = stmt->decl.field_set.value;
  if (value != NULL && !is_dynamic_expr(value)) {
    Value static_value = value_from_node(value);
    if (!type_accepts(field->decl.struct_field.type_name, static_value)) {
      fprintf(stderr, "error: line %d: field '%s' expects %s, got %s\n",
              stmt->line, name, field->decl.struct_field.type_name,
              value_type_name(static_value));
      return 1;
    }
  }

  return 0;
}

static int check_statement(ASTNode *stmt, Struct *structs) {
  if (stmt->type == NODE_EMIT)
    return check_emit_node(stmt, structs);

  if (stmt->type == NODE_PRINT_STMT) {
    ASTNode *value = stmt->decl.print_stmt.value;
    if (value == NULL || value->type != NODE_STRING_LITERAL) {
      fprintf(stderr, "error: line %d: cikle expects a string literal\n",
              stmt->line);
      return 1;
    }
    return 0;
  }

  return 0;
}

static int check_program(ASTNode *program, Struct *structs, EnumDecl *enums) {
  int errors = 0;

  for (ASTNode *node = program; node != NULL; node = node->next) {
    if (node->type == NODE_STRUCT_DECL) {
      for (ASTNode *field = node->decl.struct_decl.fields; field != NULL;
           field = field->next) {
        const char *type_name = field->decl.struct_field.type_name;
        if (!type_is_known(type_name, enums)) {
          fprintf(stderr, "error: line %d: unknown type '%s' for field '%s'\n",
                  field->line, type_name, field->decl.struct_field.name);
          errors++;
        }
      }
    } else if (node->type == NODE_EMIT || node->type == NODE_PRINT_STMT) {
      errors += check_statement(node, structs);
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

      for (ASTNode *stmt = node->decl.event_handler.body; stmt != NULL;
           stmt = stmt->next) {
        if (stmt->type == NODE_FIELD_SET)
          errors += check_field_set(stmt, schema);
        else
          errors += check_statement(stmt, structs);
      }
    }
  }

  return errors;
}

static int event_id(ASTNode *args, Scope *scope, Runtime *runtime,
                    const char **out) {
  *out = NULL;

  ASTNode *arg = find_emit_arg(args, "id");
  if (arg == NULL)
    return 0;

  Value value;
  if (eval_value(arg->decl.emit_arg.value, scope, runtime, &value))
    return 1;

  if (value.type == VALUE_STRING)
    *out = value.string; // borrowed from the AST

  value_free(value);
  return 0;
}

static int handler_matches(ASTNode *handler, const char *id) {
  const char *expected = handler->decl.event_handler.id;

  if (expected == NULL)
    return 1;
  if (id == NULL)
    return 0;
  return strcmp(expected, id) == 0;
}

static ASTNode *parse_ref_text(const char *text, size_t len, int line) {
  if (len < 2 || text[0] != '$')
    return NULL;

  size_t i = 1;
  while (i < len && (isalnum((unsigned char)text[i]) || text[i] == '_' ||
                     ((unsigned char)text[i] & 0x80)))
    i++;

  if (i == 1)
    return NULL;

  ASTNode *node = ast_node_new(NODE_VAR_REF);
  node->line = line;
  node->decl.var_ref.name = strndup(&text[1], i - 1);

  while (i < len) {
    if (text[i] != '[')
      break;

    i++;
    size_t start = i;
    int index = 0;
    while (i < len && isdigit((unsigned char)text[i])) {
      index = index * 10 + (text[i] - '0');
      i++;
    }
    if (i == start || i >= len || text[i] != ']') {
      free_ast(node);
      return NULL;
    }
    i++; // ']'

    ASTNode *index_node = ast_node_new(NODE_INDEX);
    index_node->line = line;
    index_node->decl.index.object = node;
    index_node->decl.index.index = index;
    node = index_node;
  }

  if (i != len) {
    free_ast(node);
    return NULL;
  }

  return node;
}

static int template_ref(const char *text, size_t len, Scope *scope,
                        Runtime *runtime, int line, Value *out) {
  ASTNode *ref = parse_ref_text(text, len, line);
  if (ref == NULL) {
    fprintf(stderr, "error: line %d: bad reference '{%.*s}'\n", line, (int)len,
            text);
    return 1;
  }

  int errors = eval_value(ref, scope, runtime, out);
  free_ast(ref);
  return errors;
}

static int print_template(const char *text, Scope *scope, Runtime *runtime,
                          int line) {
  TextBuffer buffer = {0};

  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] != '{') {
      if (!buffer_put(&buffer, &text[i], 1))
        goto oom;
      continue;
    }

    int end = i + 1;
    while (text[end] != '\0' && text[end] != '}')
      end++;

    if (text[end] != '}') {
      if (!buffer_put(&buffer, &text[i], 1))
        goto oom;
      continue;
    }

    size_t len = (size_t)(end - i - 1);

    if (text[i + 1] == '$') {
      Value value;
      if (template_ref(&text[i + 1], len, scope, runtime, line, &value)) {
        free(buffer.data);
        return 1;
      }
      if (!value_append(&buffer, value)) {
        value_free(value);
        goto oom;
      }
      value_free(value);
    } else {
      Binding *field = bindings_find(scope->fields, &text[i + 1], len);
      if (field == NULL) {
        fprintf(stderr, "error: line %d: no value for '{%.*s}'\n", line,
                (int)len, &text[i + 1]);
        free(buffer.data);
        return 1;
      }
      if (!value_append(&buffer, field->value)) {
        free(buffer.data);
        goto oom;
      }
    }
    i = end;
  }

  if (buffer.data != NULL)
    fwrite(buffer.data, 1, buffer.len, stdout);
  putchar('\n');
  free(buffer.data);
  return 0;

oom:
  fprintf(stderr, "error: line %d: out of memory\n", line);
  free(buffer.data);
  return 1;
}

static Binding *bind_handler_fields(ASTNode *schema, ASTNode *args,
                                    Scope *scope, Runtime *runtime, int line,
                                    int *errors) {
  Binding *bindings = NULL;

  for (ASTNode *field = schema->decl.struct_decl.fields; field != NULL;
       field = field->next) {
    const char *name = field->decl.struct_field.name;
    const char *type = field->decl.struct_field.type_name;

    ASTNode *arg = find_emit_arg(args, name);
    Value value;

    if (arg != NULL) {
      if (eval_value(arg->decl.emit_arg.value, scope, runtime, &value)) {
        (*errors)++;
        value = make_default_or_report(type, runtime->enums, line, errors);
      } else if (!type_accepts(type, value)) {
        fprintf(stderr, "error: line %d: field '%s' expects %s, got %s\n", line,
                name, type, value_type_name(value));
        value_free(value);
        (*errors)++;
        value = make_default_or_report(type, runtime->enums, line, errors);
      }
    } else {
      value = make_default_or_report(type, runtime->enums, line, errors);
    }

    bindings = bind_add(bindings, name, value);
  }

  return bindings;
}

static int emit_event(ASTNode *emit, Scope *scope, Runtime *runtime,
                      int depth) {
  const char *type_name = emit->decl.emit.type_name;
  ASTNode *args = emit->decl.emit.args;

  if (depth > MAX_EVENT_DEPTH) {
    fprintf(stderr,
            "error: line %d: event depth limit reached (recursive pirt?)\n",
            emit->line);
    return 1;
  }

  ASTNode *schema = find_struct_by_name(runtime->structs, type_name);
  if (schema == NULL) {
    fprintf(stderr, "error: line %d: unknown event type '%s'\n", emit->line,
            type_name);
    return 1;
  }

  const char *id = NULL;
  int errors = event_id(args, scope, runtime, &id);

  for (Handler *h = runtime->handlers; h != NULL; h = h->next) {
    ASTNode *handler = h->node;

    if (strcmp(handler->decl.event_handler.type_name, type_name) != 0)
      continue;
    if (!handler_matches(handler, id))
      continue;

    int bind_errors = 0;
    Binding *fields = bind_handler_fields(schema, args, scope, runtime,
                                          emit->line, &bind_errors);
    errors += bind_errors;

    Scope handler_scope = {.fields = fields, .vars = NULL};
    for (ASTNode *stmt = handler->decl.event_handler.body; stmt != NULL;
         stmt = stmt->next)
      errors += exec_statement(stmt, &handler_scope, runtime, depth, 1);

    vars_free(handler_scope.vars);
    vars_free(fields);
  }

  return errors;
}

static int exec_statement(ASTNode *stmt, Scope *scope, Runtime *runtime,
                          int depth, int in_handler) {
  if (stmt->type == NODE_PRINT_STMT) {
    ASTNode *value = stmt->decl.print_stmt.value;
    if (value == NULL || value->type != NODE_STRING_LITERAL) {
      fprintf(stderr, "error: line %d: cikle expects a string literal\n",
              stmt->line);
      return 1;
    }
    return print_template(value->decl.string_value, scope, runtime, stmt->line);
  }

  if (stmt->type == NODE_EMIT)
    return emit_event(stmt, scope, runtime, depth + 1);

  if (stmt->type == NODE_FLOW) {
    int count = 0;
    for (ASTNode *arg = stmt->decl.flow.args; arg != NULL; arg = arg->next)
      count++;

    Tuple *tuple = malloc(sizeof(Tuple));
    tuple->count = count;
    tuple->items = malloc(sizeof(Value) * (size_t)count);
    tuple->types = malloc(sizeof(char *) * (size_t)count);

    int errors = 0;
    int i = 0;
    for (ASTNode *arg = stmt->decl.flow.args; arg != NULL;
         arg = arg->next, i++) {
      if (eval_value(arg, scope, runtime, &tuple->items[i])) {
        tuple->items[i] = value_nil();
        errors++;
      }
      tuple->types[i] = value_type_name(tuple->items[i]);
    }

    if (errors > 0) {
      value_free((Value){.type = VALUE_TUPLE, .tuple = tuple});
      return errors;
    }

    var_store(stmt->decl.flow.name,
              (Value){.type = VALUE_TUPLE, .tuple = tuple}, scope, runtime,
              in_handler);
    return 0;
  }

  if (stmt->type == NODE_INDEX_SET) {
    const char *name = stmt->decl.index_set.name;

    Binding *var = find_var(name, scope, runtime->globals);
    if (var == NULL) {
      fprintf(stderr, "error: line %d: undefined variable '$%s'\n", stmt->line,
              name);
      return 1;
    }
    if (var->value.type != VALUE_TUPLE) {
      fprintf(stderr, "error: line %d: '$%s' is not a ye() value\n", stmt->line,
              name);
      return 1;
    }

    int index = stmt->decl.index_set.index;
    if (index < 0 || index >= var->value.tuple->count) {
      fprintf(stderr, "error: line %d: index %d out of range (0..%d)\n",
              stmt->line, index, var->value.tuple->count - 1);
      return 1;
    }

    Value value;
    if (eval_value(stmt->decl.index_set.value, scope, runtime, &value))
      return 1;

    const char *expected =
        var->value.tuple->types != NULL ? var->value.tuple->types[index] : NULL;
    if (expected != NULL && strcmp(expected, value_type_name(value)) != 0) {
      fprintf(stderr,
              "error: line %d: element %d of '$%s' expects %s, got %s\n",
              stmt->line, index, name, expected, value_type_name(value));
      value_free(value);
      return 1;
    }

    value_free(var->value.tuple->items[index]);
    var->value.tuple->items[index] = value;
    return 0;
  }

  if (stmt->type == NODE_FIELD_SET) {
    const char *name = stmt->decl.field_set.name;

    Binding *field = bindings_find(scope->fields, name, strlen(name));
    if (field == NULL) {
      fprintf(stderr, "error: line %d: unknown identifier '%s'\n", stmt->line,
              name);
      return 1;
    }

    Value value;
    if (eval_value(stmt->decl.field_set.value, scope, runtime, &value))
      return 1;

    value_free(field->value);
    field->value = value;
    return 0;
  }

  return 0;
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

static void free_enums(EnumDecl *list) {
  while (list != NULL) {
    EnumDecl *next = list->next;
    free(list);
    list = next;
  }
}

int run_program(ASTNode *program) {
  Struct *structs = NULL;
  Struct *structs_tail = NULL;

  Handler *handlers = NULL;
  Handler *handlers_tail = NULL;

  EnumDecl *enums = NULL;
  EnumDecl *enums_tail = NULL;

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
    } else if (node->type == NODE_ENUM_DECL) {
      EnumDecl *e = malloc(sizeof(EnumDecl));
      e->node = node;
      e->next = NULL;

      if (enums == NULL)
        enums = e;
      else
        enums_tail->next = e;
      enums_tail = e;
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

  Runtime runtime = {.handlers = handlers,
                     .structs = structs,
                     .enums = enums,
                     .globals = NULL};
  Scope top_scope = {.fields = NULL, .vars = NULL};

  int errors = check_program(program, structs, enums);
  if (errors > 0) {
    free_structs(structs);
    free_handlers(handlers);
    free_enums(enums);
    return errors;
  }

  int run_errors = 0;
  for (ASTNode *node = program; node != NULL; node = node->next) {
    switch (node->type) {
    case NODE_EMIT:
      run_errors += emit_event(node, &top_scope, &runtime, 0);
      break;
    case NODE_PRINT_STMT:
    case NODE_FLOW:
    case NODE_INDEX_SET:
      run_errors += exec_statement(node, &top_scope, &runtime, 0, 0);
      break;
    default:
      break;
    }
  }

  vars_free(runtime.globals);
  free_structs(structs);
  free_handlers(handlers);
  free_enums(enums);

  return run_errors;
}
