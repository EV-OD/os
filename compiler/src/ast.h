#ifndef COMPILER_AST_H
#define COMPILER_AST_H

/* -----------------------------------------------------------------------
 * ast.h  --  Abstract Syntax Tree node definitions (full language).
 *
 * All nodes are drawn from a static pool (no heap).  The pool is reset
 * via ast_init() before each compilation run.
 *
 * Node types:
 *
 *  Expressions:
 *   AST_NUMBER   -- integer literal
 *   AST_BOOL     -- boolean literal  (true / false)
 *   AST_STRING   -- string literal
 *   AST_IDENT    -- variable reference
 *   AST_BINOP    -- binary operation  (+ - * / % == != < > <= >= && || & | ^ << >>)
 *   AST_UNARY    -- unary operation   (- ! ~)
 *   AST_CAST     -- type cast  (expr as type)
 *   AST_CALL     -- function call     foo(a, b)
 *   AST_INDEX    -- array index       arr[i]
 *   AST_RANGE    -- integer range     lo..hi  (used in for..in)
 *
 *  Statements:
 *   AST_LET      -- immutable binding  let x: type = expr
 *   AST_MUT      -- mutable variable   mut x: type = expr
 *   AST_ASSIGN   -- assignment         x = expr  or  x op= expr
 *   AST_PRINT    -- print(expr)
 *   AST_PRINTLN  -- println(expr)
 *   AST_RETURN   -- return expr
 *   AST_BREAK    -- break
 *   AST_CONTINUE -- continue
 *   AST_IF       -- if / else
 *   AST_WHILE    -- while loop
 *   AST_FOR      -- for var in range / iterable
 *   AST_BLOCK    -- { stmt; stmt; ... }
 *   AST_EXPR_STMT-- expression used as statement (e.g. function call)
 *
 *  Top-level:
 *   AST_FUNC_DEF -- fn name(params) -> ret { body }
 *   AST_PROGRAM  -- root: list of top-level items (functions + stmts)
 *
 *  GUI built-ins:
 *   AST_GUI_CALL -- gui_xxx(...) mapped to a specific GUI syscall
 * --------------------------------------------------------------------- */

#include "common.h"

/* -----------------------------------------------------------------------
 * AstType  -- discriminant tag for each AST node kind
 * --------------------------------------------------------------------- */
typedef enum {
    /* Expressions */
    AST_NUMBER,
    AST_BOOL,
    AST_STRING,
    AST_IDENT,
    AST_BINOP,
    AST_UNARY,
    AST_CAST,
    AST_CALL,
    AST_INDEX,
    AST_RANGE,

    /* Statements */
    AST_LET,
    AST_MUT,
    AST_ASSIGN,
    AST_PRINT,
    AST_PRINTLN,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_BLOCK,
    AST_EXPR_STMT,

    /* Top-level */
    AST_FUNC_DEF,
    AST_PROGRAM,

    /* GUI built-in */
    AST_GUI_CALL
} AstType;

/* -----------------------------------------------------------------------
 * Resolved type tag used by the type-checker / codegen
 * --------------------------------------------------------------------- */
typedef enum {
    TY_UNKNOWN = 0,
    TY_VOID,
    TY_BOOL,
    TY_I8,
    TY_I16,
    TY_I32,
    TY_U8,
    TY_U16,
    TY_U32,
    TY_STR,       /* pointer to null-terminated string in data section */
    TY_PTR        /* generic raw pointer (u32 value)                     */
} RosType;

/* Parameter descriptor (used in AST_FUNC_DEF) */
typedef struct {
    char    name[MAX_IDENT_LEN];
    char    type_name[MAX_IDENT_LEN];
    RosType resolved;
} Param;

/* -----------------------------------------------------------------------
 * AstNode  --  One node in the AST (union-style flat struct)
 * --------------------------------------------------------------------- */
typedef struct AstNode {
    AstType  type;
    RosType  resolved_type;   /* set by semantic pass                     */

    /* ----- Literals -------------------------------------------------- */
    i32  number;              /* AST_NUMBER                                */
    int  bool_val;            /* AST_BOOL  (0=false, 1=true)               */

    /* ----- Name / type annotation ------------------------------------- */
    char name     [MAX_IDENT_LEN];   /* variable / function / ident name  */
    char type_name[MAX_IDENT_LEN];   /* declared type string e.g. "i32"   */

    /* ----- Operator -------------------------------------------------- */
    char  op;                 /* AST_BINOP / AST_UNARY: operator char      */
    char  op2;                /* second char for two-char ops (e.g. '=')   */

    /* ----- Children (shared across node types) ----------------------- */
    struct AstNode *left;    /* AST_BINOP left; AST_UNARY operand          */
    struct AstNode *right;   /* AST_BINOP right                            */
    struct AstNode *expr;    /* AST_LET/MUT/RETURN/PRINT expr              */
    struct AstNode *cond;    /* AST_IF/WHILE condition                     */
    struct AstNode *then_b;  /* AST_IF then-block                          */
    struct AstNode *else_b;  /* AST_IF else-block (may be NULL)            */
    struct AstNode *body;    /* AST_WHILE/FOR/FUNC_DEF body block          */
    struct AstNode *range;   /* AST_FOR range expression (AST_RANGE)       */

    /* ----- Arguments (function call / GUI call) ---------------------- */
    struct AstNode *args[MAX_ARGS];
    int             arg_count;

    /* ----- Function definition parameters ---------------------------- */
    Param  params[MAX_PARAMS];
    int    param_count;
    char   ret_type[MAX_IDENT_LEN];
    RosType ret_resolved;

    /* ----- Block statement list -------------------------------------- */
    struct AstNode *stmts[MAX_STMTS];
    int             stmt_count;

    /* ----- GUI call subtype ----------------------------------------- */
    int  gui_op;             /* one of the SYS_GUI_* constants              */

    /* ----- Source location ------------------------------------------ */
    int  line;
    int  col;
} AstNode;

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

/** Reset the static node pool.  Must be called before parsing. */
void ast_init(void);

/** Allocate a zeroed node from the pool; returns NULL on pool exhaustion. */
AstNode *ast_alloc(AstType type);

/** Pretty-print the AST tree (--emit-ast diagnostic). */
void ast_dump(AstNode *node, int indent);

/** Return a human-readable name for a RosType. */
const char *rostype_name(RosType t);

/** Convert a type-name string to RosType. */
RosType rostype_from_str(const char *s);

#endif /* COMPILER_AST_H */
