#ifndef COMPILER_AST_H
#define COMPILER_AST_H

/* -----------------------------------------------------------------------
 * ast.h  --  Abstract Syntax Tree node definitions (Phase 1).
 *
 * All nodes are drawn from a static pool (no heap).  The pool is reset
 * via ast_init() before each compilation unit.
 *
 * Phase 1 node types:
 *   AST_NUMBER   -- integer literal
 *   AST_IDENT    -- variable reference
 *   AST_BINOP    -- binary operation  (+  -  *  /)
 *   AST_LET      -- let binding  (let <name>: <type> = <expr>)
 *   AST_PROGRAM  -- root: ordered list of statements
 * --------------------------------------------------------------------- */

#include "common.h"

typedef enum {
    AST_NUMBER,
    AST_IDENT,
    AST_BINOP,
    AST_LET,
    AST_PRINT,
    AST_STRING,
    AST_PROGRAM
} AstType;

/* All AST variants share a single struct to keep allocation trivial. */
typedef struct AstNode {
    AstType type;

    /* AST_NUMBER */
    i32 number;

    /* AST_IDENT, AST_LET: variable name */
    char name[MAX_IDENT_LEN];

    /* AST_LET: declared type, e.g. "i32" */
    char type_name[MAX_IDENT_LEN];

    /* AST_BINOP: operator character (+, -, *, /) */
    char op;

    /* AST_BINOP: operands; AST_LET: expr */
    struct AstNode *left;
    struct AstNode *right;

    /* AST_LET: initialiser expression */
    struct AstNode *expr;

    /* AST_PROGRAM: list of top-level statements */
    struct AstNode *stmts[MAX_BINDINGS];
    int stmt_count;
} AstNode;

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

/* Reset the static node pool. Must be called before parsing. */
void ast_init(void);

/* Allocate a zeroed node from the pool; returns NULL on pool exhaustion. */
AstNode *ast_alloc(AstType type);

/* Pretty-print the AST tree to the display (--emit-ast). */
void ast_dump(AstNode *node, int indent);

#endif /* COMPILER_AST_H */
