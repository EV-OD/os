#include "ast.h"
#include "stdio.h"
#include "string.h"

/* -----------------------------------------------------------------------
 * Static node pool
 * --------------------------------------------------------------------- */
static AstNode g_pool[MAX_AST_NODES];
static int     g_pool_used = 0;

void ast_init(void)
{
    g_pool_used = 0;
    memset(g_pool, 0, sizeof(g_pool));
}

AstNode *ast_alloc(AstType type)
{
    if (g_pool_used >= MAX_AST_NODES) return NULL;
    AstNode *n = &g_pool[g_pool_used++];
    memset(n, 0, sizeof(AstNode));
    n->type          = type;
    n->resolved_type = TY_UNKNOWN;
    return n;
}

/* -----------------------------------------------------------------------
 * Type helpers
 * --------------------------------------------------------------------- */
const char *rostype_name(RosType t)
{
    switch (t) {
        case TY_VOID:    return "void";
        case TY_BOOL:    return "bool";
        case TY_I8:      return "i8";
        case TY_I16:     return "i16";
        case TY_I32:     return "i32";
        case TY_U8:      return "u8";
        case TY_U16:     return "u16";
        case TY_U32:     return "u32";
        case TY_STR:     return "str";
        case TY_PTR:     return "*ptr";
        default:         return "?";
    }
}

RosType rostype_from_str(const char *s)
{
    if (strcmp(s, "void") == 0) return TY_VOID;
    if (strcmp(s, "bool") == 0) return TY_BOOL;
    if (strcmp(s, "i8")   == 0) return TY_I8;
    if (strcmp(s, "i16")  == 0) return TY_I16;
    if (strcmp(s, "i32")  == 0) return TY_I32;
    if (strcmp(s, "u8")   == 0) return TY_U8;
    if (strcmp(s, "u16")  == 0) return TY_U16;
    if (strcmp(s, "u32")  == 0) return TY_U32;
    if (strcmp(s, "str")  == 0) return TY_STR;
    return TY_UNKNOWN;
}

/* -----------------------------------------------------------------------
 * Pretty-printer
 * --------------------------------------------------------------------- */
static void print_indent(int indent)
{
    char buf[65];
    int n = indent * 2;
    if (n > 64) n = 64;
    int i;
    for (i = 0; i < n; i++) buf[i] = ' ';
    buf[i] = '\0';
    puts(buf);
}

void ast_dump(AstNode *node, int indent)
{
    char buf[256];
    int  i;
    if (!node) return;

    print_indent(indent);

    switch (node->type) {
        case AST_NUMBER:
            sprintf(buf, "Number(%d)\n", node->number);
            puts(buf);
            break;

        case AST_BOOL:
            sprintf(buf, "Bool(%s)\n", node->bool_val ? "true" : "false");
            puts(buf);
            break;

        case AST_STRING:
            sprintf(buf, "Str(\"%s\")\n", node->name);
            puts(buf);
            break;

        case AST_IDENT:
            sprintf(buf, "Ident(%s)\n", node->name);
            puts(buf);
            break;

        case AST_BINOP:
            if (node->op2)
                sprintf(buf, "BinOp('%c%c')\n", node->op, node->op2);
            else
                sprintf(buf, "BinOp('%c')\n", node->op);
            puts(buf);
            ast_dump(node->left,  indent + 1);
            ast_dump(node->right, indent + 1);
            break;

        case AST_UNARY:
            sprintf(buf, "Unary('%c')\n", node->op);
            puts(buf);
            ast_dump(node->left, indent + 1);
            break;

        case AST_CAST:
            sprintf(buf, "Cast(-> %s)\n", node->type_name);
            puts(buf);
            ast_dump(node->expr, indent + 1);
            break;

        case AST_CALL:
            sprintf(buf, "Call(%s, %d args)\n", node->name, node->arg_count);
            puts(buf);
            for (i = 0; i < node->arg_count; i++)
                ast_dump(node->args[i], indent + 1);
            break;

        case AST_INDEX:
            sprintf(buf, "Index(%s)\n", node->name);
            puts(buf);
            ast_dump(node->expr, indent + 1);
            break;

        case AST_RANGE:
            puts("Range\n");
            ast_dump(node->left,  indent + 1);
            ast_dump(node->right, indent + 1);
            break;

        case AST_LET:
            sprintf(buf, "Let(%s: %s)\n", node->name, node->type_name);
            puts(buf);
            ast_dump(node->expr, indent + 1);
            break;

        case AST_MUT:
            sprintf(buf, "Mut(%s: %s)\n", node->name, node->type_name);
            puts(buf);
            ast_dump(node->expr, indent + 1);
            break;

        case AST_ASSIGN:
            if (node->op2)
                sprintf(buf, "Assign(%s %c%c=)\n", node->name, node->op, node->op2);
            else
                sprintf(buf, "Assign(%s =)\n", node->name);
            puts(buf);
            ast_dump(node->expr, indent + 1);
            break;

        case AST_PRINT:
            puts("Print\n");
            ast_dump(node->expr, indent + 1);
            break;

        case AST_PRINTLN:
            puts("Println\n");
            ast_dump(node->expr, indent + 1);
            break;

        case AST_RETURN:
            puts("Return\n");
            if (node->expr) ast_dump(node->expr, indent + 1);
            break;

        case AST_BREAK:
            puts("Break\n");
            break;

        case AST_CONTINUE:
            puts("Continue\n");
            break;

        case AST_IF:
            puts("If\n");
            print_indent(indent + 1); puts("cond:\n");
            ast_dump(node->cond,   indent + 2);
            print_indent(indent + 1); puts("then:\n");
            ast_dump(node->then_b, indent + 2);
            if (node->else_b) {
                print_indent(indent + 1); puts("else:\n");
                ast_dump(node->else_b, indent + 2);
            }
            break;

        case AST_WHILE:
            puts("While\n");
            print_indent(indent + 1); puts("cond:\n");
            ast_dump(node->cond, indent + 2);
            print_indent(indent + 1); puts("body:\n");
            ast_dump(node->body, indent + 2);
            break;

        case AST_FOR:
            sprintf(buf, "For(%s in)\n", node->name);
            puts(buf);
            print_indent(indent + 1); puts("range:\n");
            ast_dump(node->range, indent + 2);
            print_indent(indent + 1); puts("body:\n");
            ast_dump(node->body, indent + 2);
            break;

        case AST_BLOCK:
            sprintf(buf, "Block(%d stmts)\n", node->stmt_count);
            puts(buf);
            for (i = 0; i < node->stmt_count; i++)
                ast_dump(node->stmts[i], indent + 1);
            break;

        case AST_EXPR_STMT:
            puts("ExprStmt\n");
            ast_dump(node->expr, indent + 1);
            break;

        case AST_FUNC_DEF: {
            sprintf(buf, "FnDef(%s -> %s, %d params)\n",
                    node->name, node->ret_type, node->param_count);
            puts(buf);
            for (i = 0; i < node->param_count; i++) {
                print_indent(indent + 1);
                sprintf(buf, "Param(%s: %s)\n",
                        node->params[i].name, node->params[i].type_name);
                puts(buf);
            }
            ast_dump(node->body, indent + 1);
            break;
        }

        case AST_GUI_CALL:
            sprintf(buf, "GuiCall(%s, %d args)\n", node->name, node->arg_count);
            puts(buf);
            for (i = 0; i < node->arg_count; i++)
                ast_dump(node->args[i], indent + 1);
            break;

        case AST_PROGRAM:
            sprintf(buf, "Program(%d items)\n", node->stmt_count);
            puts(buf);
            for (i = 0; i < node->stmt_count; i++)
                ast_dump(node->stmts[i], indent + 1);
            break;
    }
}
