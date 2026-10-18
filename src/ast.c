#include "bebisc.h"
#include <stdio.h>
#include <stdlib.h>

ASTNode *ast_node_new(NodeType type) {
  ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
  node->type = type;
  node->line = 0;
  node->next = NULL;
  return node;
}

static void print_value(ASTNode *node) {
  if (node == NULL) {
    printf("<none>\n");
    return;
  }

  switch (node->type) {
  case NODE_IDENTIFIER:
    printf("%s\n", node->decl.identifier.name);
    break;
  case NODE_MEMBER_ACCESS:
    printf("%s.%s\n", node->decl.member_access.object->decl.identifier.name,
           node->decl.member_access.member);
    break;
  case NODE_VAR_REF:
    printf("$%s\n", node->decl.var_ref.name);
    break;
  case NODE_INDEX:
    printf("$%s[%d]\n",
           node->decl.index.object->type == NODE_VAR_REF
               ? node->decl.index.object->decl.var_ref.name
               : "?",
           node->decl.index.index);
    break;
  case NODE_STRING_LITERAL:
    printf("\"%s\"\n", node->decl.string_value);
    break;
  case NODE_NUMBER_LITERAL:
    printf("%d\n", node->decl.number_value);
    break;
  case NODE_FLOAT_LITERAL:
    printf("%f\n", node->decl.float_value);
    break;
  case NODE_BOOL_LITERAL:
    printf("%s\n", node->decl.bool_value ? "true" : "false");
    break;
  default:
    printf("unknown value\n");
    break;
  }
}

void print_ast(ASTNode *node, int indent) {
  while (node != NULL) {
    for (int i = 0; i < indent; i++) {
      printf(" ");
    }

    switch (node->type) {
    case NODE_ENUM_DECL:
      printf("enum decl: %s\n", node->decl.enum_decl.name);
      print_ast(node->decl.enum_decl.members, indent + 2);
      break;
    case NODE_ENUM_MEMBER:
      printf("member: %s\n", node->decl.enum_member.name);
      break;
    case NODE_STRUCT_DECL:
      printf("struct decl: %s\n", node->decl.struct_decl.name);
      print_ast(node->decl.struct_decl.fields, indent + 2);
      break;
    case NODE_STRUCT_FIELD:
      printf("field: %s (type: %s)\n", node->decl.struct_field.name,
             node->decl.struct_field.type_name);
      break;
    case NODE_EVENT_HANDLER:
      printf("event handler: %s", node->decl.event_handler.type_name);
      if (node->decl.event_handler.id != NULL)
        printf(" (id: %s)", node->decl.event_handler.id);
      printf("\n");
      print_ast(node->decl.event_handler.body, indent + 2);
      break;
    case NODE_PRINT_STMT:
      printf("print: ");
      print_value(node->decl.print_stmt.value);
      break;
    case NODE_EMIT:
      printf("emit: %s\n", node->decl.emit.type_name);
      print_ast(node->decl.emit.args, indent + 2);
      break;
    case NODE_EMIT_ARG:
      printf("arg: %s = ", node->decl.emit_arg.name);
      print_value(node->decl.emit_arg.value);
      break;
    case NODE_VAR_REF:
      printf("ref: $%s\n", node->decl.var_ref.name);
      break;
    case NODE_INDEX:
      printf("index: [%d]\n", node->decl.index.index);
      print_ast(node->decl.index.object, indent + 2);
      break;
    case NODE_FIELD_SET:
      printf("set field: %s = ", node->decl.field_set.name);
      print_value(node->decl.field_set.value);
      break;
    case NODE_INDEX_SET:
      printf("set index: $%s[%d] = ", node->decl.index_set.name,
             node->decl.index_set.index);
      print_value(node->decl.index_set.value);
      break;
    case NODE_FLOW:
      printf("flow: ye ~> $%s\n", node->decl.flow.name);
      print_ast(node->decl.flow.args, indent + 2);
      break;
    default:
      printf("unknown ast node\n");
      break;
    }

    node = node->next;
  }
}

void free_ast(ASTNode *node) {
  while (node != NULL) {
    ASTNode *next = node->next;

    switch (node->type) {
    case NODE_ENUM_DECL:
      free(node->decl.enum_decl.name);
      free_ast(node->decl.enum_decl.members);
      break;
    case NODE_ENUM_MEMBER:
      free(node->decl.enum_member.name);
      break;
    case NODE_STRUCT_DECL:
      free(node->decl.struct_decl.name);
      free_ast(node->decl.struct_decl.fields);
      break;
    case NODE_STRUCT_FIELD:
      free(node->decl.struct_field.name);
      free(node->decl.struct_field.type_name);
      break;
    case NODE_EVENT_HANDLER:
      free(node->decl.event_handler.type_name);
      free(node->decl.event_handler.id);
      free_ast(node->decl.event_handler.body);
      break;
    case NODE_PRINT_STMT:
      free_ast(node->decl.print_stmt.value);
      break;
    case NODE_EMIT:
      free(node->decl.emit.type_name);
      free_ast(node->decl.emit.args);
      break;
    case NODE_EMIT_ARG:
      free(node->decl.emit_arg.name);
      free_ast(node->decl.emit_arg.value);
      break;
    case NODE_IDENTIFIER:
      free(node->decl.identifier.name);
      break;
    case NODE_MEMBER_ACCESS:
      free_ast(node->decl.member_access.object);
      free(node->decl.member_access.member);
      break;
    case NODE_VAR_REF:
      free(node->decl.var_ref.name);
      break;
    case NODE_INDEX:
      free_ast(node->decl.index.object);
      break;
    case NODE_FIELD_SET:
      free(node->decl.field_set.name);
      free_ast(node->decl.field_set.value);
      break;
    case NODE_INDEX_SET:
      free(node->decl.index_set.name);
      free_ast(node->decl.index_set.value);
      break;
    case NODE_FLOW:
      free(node->decl.flow.name);
      free_ast(node->decl.flow.args);
      break;
    case NODE_STRING_LITERAL:
      free(node->decl.string_value);
      break;
    default:
      break;
    }

    free(node);
    node = next;
  }
}
