#include "bebisc.h"
#include <stdio.h>
#include <stdlib.h>

ASTNode *ast_node_new(NodeType type) {
  ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
  node->type = type;
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
      printf("event handler: %s\n", node->decl.event_handler.type_name);
      print_ast(node->decl.event_handler.params, indent + 2);
      print_ast(node->decl.event_handler.body, indent + 2);
      break;
    case NODE_HANDLER_PARAM:
      printf("bind: %s", node->decl.handler_param.name);
      if (node->decl.handler_param.value != NULL) {
        printf(" = ");
        print_value(node->decl.handler_param.value);
      } else {
        printf("\n");
      }
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
    default:
      printf("unknown ast node\n");
      break;
    }

    node = node->next;
  }
}
