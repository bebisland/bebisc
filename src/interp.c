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

static void emit_event(ASTNode *emit, Handler *handlers);
static void exec_body(ASTNode *stmt, Binding *bindings, Handler *handlers);

static Value value_from_node(ASTNode *node) {
  Value value = {VALUE_STRING, 0, 0.0f, 0, "", NULL};

  if (node == NULL)
    return value;

  switch (node->type) {
  case NODE_NUMBER_LITERAL: // 3
    value.type = VALUE_NUMBER;
    value.number = node->decl.number_value;
    break;
  case NODE_FLOAT_LITERAL: // 2.5
    value.type = VALUE_FLOAT;
    value.float_value = node->decl.float_value;
    break;
  case NODE_BOOL_LITERAL: // true / false
    value.type = VALUE_BOOL;
    value.bool_value = node->decl.bool_value;
    break;
  case NODE_STRING_LITERAL: // "..."
    value.type = VALUE_STRING;
    value.string = node->decl.string_value;
    break;
  case NODE_IDENTIFIER: // name
    value.type = VALUE_STRING;
    value.string = node->decl.identifier.name;
    break;
  case NODE_MEMBER_ACCESS: // enum.member
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

static ASTNode *find_handler_param(ASTNode *params, const char *name) {
  for (ASTNode *param = params; param != NULL; param = param->next) {
    if (strcmp(param->decl.handler_param.name, name) == 0)
      return param;
  }
  return NULL;
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
  ASTNode *param = find_handler_param(handler->decl.event_handler.params, "id");

  if (param == NULL || param->decl.handler_param.value == NULL)
    return 1;

  Value expected = value_from_node(param->decl.handler_param.value);
  if (expected.type != VALUE_STRING || id == NULL)
    return 0;
  return strcmp(expected.string, id) == 0;
}

static void print_template(const char *text, Binding *bindings) {
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
}

static void emit_event(ASTNode *emit, Handler *handlers) {
  const char *type_name = emit->decl.emit.type_name;
  ASTNode *args = emit->decl.emit.args;
  const char *id = event_id(args);

  for (Handler *h = handlers; h != NULL; h = h->next) {
    ASTNode *handler = h->node;

    if (strcmp(handler->decl.event_handler.type_name, type_name) != 0)
      continue;
    if (!handler_matches(handler, id))
      continue;

    Binding *bindings = NULL;
    for (ASTNode *param = handler->decl.event_handler.params; param != NULL;
         param = param->next) {
      const char *name = param->decl.handler_param.name;
      ASTNode *arg = find_emit_arg(args, name);

      if (arg != NULL)
        bindings =
            bind_add(bindings, name, value_from_node(arg->decl.emit_arg.value));
      else if (param->decl.handler_param.value != NULL)
        bindings = bind_add(bindings, name,
                            value_from_node(param->decl.handler_param.value));
    }

    exec_body(handler->decl.event_handler.body, bindings, handlers);

    bindings_free(bindings);
  }
}

static void exec_body(ASTNode *stmt, Binding *bindings, Handler *handlers) {
  for (; stmt != NULL; stmt = stmt->next) {
    if (stmt->type == NODE_PRINT_STMT)
      print_template(stmt->decl.print_stmt.value->decl.string_value, bindings);
    else if (stmt->type == NODE_EMIT)
      emit_event(stmt, handlers);
  }
}

void run_program(ASTNode *program) {
  Handler *handlers = NULL;
  Handler *handlers_tail = NULL;

  for (ASTNode *node = program; node != NULL; node = node->next) {
    if (node->type != NODE_EVENT_HANDLER)
      continue;

    Handler *handler = malloc(sizeof(Handler));
    handler->node = node;
    handler->next = NULL;

    if (handlers == NULL)
      handlers = handler;
    else
      handlers_tail->next = handler;
    handlers_tail = handler;
  }

  for (ASTNode *node = program; node != NULL; node = node->next) {
    if (node->type == NODE_EMIT)
      emit_event(node, handlers);
  }

  while (handlers != NULL) {
    Handler *next = handlers->next;
    free(handlers);
    handlers = next;
  }
}
