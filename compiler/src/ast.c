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
    n->type = type;
    return n;
}

/* -----------------------------------------------------------------------
 * Pretty-printer
 * --------------------------------------------------------------------- */

/* Print 'indent * 2' spaces. */
static void print_indent(int indent)
{
    char buf[65];
    int  n = indent * 2;
    if (n > 64) n = 64;
    int i;
    for (i = 0; i < n; i++) buf[i] = ' ';
    buf[i] = '\0';
    puts(buf);
}

void ast_dump(AstNode *node, int indent)
{
    char buf[128];
    if (!node) return;

    print_indent(indent);

    switch (node->type) {
        case AST_NUMBER:
            sprintf(buf, "Number(%d)\n", node->number);
            puts(buf);
            break;

        case AST_IDENT:
            sprintf(buf, "Ident(%s)\n", node->name);
            puts(buf);
            break;

        case AST_BINOP:
            sprintf(buf, "BinOp('%c')\n", node->op);
            puts(buf);
            ast_dump(node->left,  indent + 1);
            ast_dump(node->right, indent + 1);
            break;

        case AST_LET:
            sprintf(buf, "Let(%s: %s)\n", node->name, node->type_name);
            puts(buf);
            ast_dump(node->expr, indent + 1);
            break;

        case AST_PROGRAM: {
            int i;
            puts("Program\n");
            for (i = 0; i < node->stmt_count; i++)
                ast_dump(node->stmts[i], indent + 1);
            break;
        }
    }
}
