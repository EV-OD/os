#include "parser.h"
#include "error.h"
#include "stdio.h"
#include "string.h"

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */
void parser_init(Parser *p, Lexer *l)
{
    p->lexer     = l;
    p->cur       = 0;
    p->had_error = 0;
}

static Token *tok_peek(Parser *p)
{
    return &p->lexer->tokens[p->cur];
}

static Token *tok_advance(Parser *p)
{
    Token *t = &p->lexer->tokens[p->cur];
    if (t->type != TOK_EOF) p->cur++;
    return t;
}

static int tok_check(Parser *p, TokenType type)
{
    return tok_peek(p)->type == type;
}

/* Consume the expected token type or emit an error. */
static Token *tok_expect(Parser *p, TokenType type, const char *msg)
{
    if (tok_check(p, type)) return tok_advance(p);
    Token *t = tok_peek(p);
    char   buf[128];
    sprintf(buf, "%s (got [%s])", msg, token_type_name(t->type));
    error_report("parser", buf, t->line, t->col);
    p->had_error = 1;
    return NULL;
}

static void skip_newlines(Parser *p)
{
    while (tok_check(p, TOK_NEWLINE)) tok_advance(p);
}

/* -----------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------- */
static AstNode *parse_expr(Parser *p);

/* -----------------------------------------------------------------------
 * primary  ::= NUMBER | IDENT | "(" expr ")"
 * --------------------------------------------------------------------- */
static AstNode *parse_primary(Parser *p)
{
    Token *t = tok_peek(p);

    if (t->type == TOK_NUMBER) {
        tok_advance(p);
        AstNode *n = ast_alloc(AST_NUMBER);
        if (!n) return NULL;
        n->number = t->int_val;
        return n;
    }

    if (t->type == TOK_IDENT) {
        tok_advance(p);
        AstNode *n = ast_alloc(AST_IDENT);
        if (!n) return NULL;
        strncpy(n->name, t->text, MAX_IDENT_LEN - 1);
        return n;
    }

    if (t->type == TOK_LPAREN) {
        tok_advance(p);              /* consume '(' */
        AstNode *n = parse_expr(p);
        if (!tok_expect(p, TOK_RPAREN, "expected ')'")) return NULL;
        return n;
    }

    /* Error: unexpected token */
    char buf[64];
    sprintf(buf, "unexpected token [%s] in expression", token_type_name(t->type));
    error_report("parser", buf, t->line, t->col);
    p->had_error = 1;
    return NULL;
}

/* -----------------------------------------------------------------------
 * multiplicative  ::= primary { ("*" | "/") primary }
 * --------------------------------------------------------------------- */
static AstNode *parse_multiplicative(Parser *p)
{
    AstNode *left = parse_primary(p);
    if (!left || p->had_error) return NULL;

    while (tok_check(p, TOK_STAR) || tok_check(p, TOK_SLASH)) {
        Token  *op    = tok_advance(p);
        AstNode *right = parse_primary(p);
        if (!right || p->had_error) return NULL;

        AstNode *node = ast_alloc(AST_BINOP);
        if (!node) return NULL;
        node->op    = (op->type == TOK_STAR) ? '*' : '/';
        node->left  = left;
        node->right = right;
        left = node;
    }
    return left;
}

/* -----------------------------------------------------------------------
 * additive  ::= multiplicative { ("+" | "-") multiplicative }
 * --------------------------------------------------------------------- */
static AstNode *parse_additive(Parser *p)
{
    AstNode *left = parse_multiplicative(p);
    if (!left || p->had_error) return NULL;

    while (tok_check(p, TOK_PLUS) || tok_check(p, TOK_MINUS)) {
        Token  *op    = tok_advance(p);
        AstNode *right = parse_multiplicative(p);
        if (!right || p->had_error) return NULL;

        AstNode *node = ast_alloc(AST_BINOP);
        if (!node) return NULL;
        node->op    = (op->type == TOK_PLUS) ? '+' : '-';
        node->left  = left;
        node->right = right;
        left = node;
    }
    return left;
}

/* expr is currently the same as additive (future: comparisons, logic). */
static AstNode *parse_expr(Parser *p)
{
    return parse_additive(p);
}

/* -----------------------------------------------------------------------
 * let_stmt  ::= "let" IDENT ":" type_name "=" expr
 * --------------------------------------------------------------------- */
static AstNode *parse_let_stmt(Parser *p)
{
    /* "let" already confirmed by caller */
    tok_advance(p);

    Token *name_tok = tok_expect(p, TOK_IDENT, "expected variable name after 'let'");
    if (!name_tok) return NULL;

    if (!tok_expect(p, TOK_COLON, "expected ':' after variable name")) return NULL;

    /* Type annotation */
    Token *type_tok = tok_peek(p);
    if (type_tok->type != TOK_TYPE_I32 &&
        type_tok->type != TOK_TYPE_U32 &&
        type_tok->type != TOK_TYPE_BOOL)
    {
        error_report("parser", "expected type (i32, u32, bool)",
                     type_tok->line, type_tok->col);
        p->had_error = 1;
        return NULL;
    }
    /* Consume the type token */
    tok_advance(p);

    if (!tok_expect(p, TOK_EQ, "expected '=' after type")) return NULL;

    AstNode *expr = parse_expr(p);
    if (!expr || p->had_error) return NULL;

    AstNode *node = ast_alloc(AST_LET);
    if (!node) return NULL;
    strncpy(node->name,      name_tok->text,  MAX_IDENT_LEN - 1);
    strncpy(node->type_name, type_tok->text,  MAX_IDENT_LEN - 1);
    node->expr = expr;
    return node;
}

/* -----------------------------------------------------------------------
 * program  ::= { newline } { stmt { newline } }
 * --------------------------------------------------------------------- */
AstNode *parser_parse(Parser *p)
{
    AstNode *prog = ast_alloc(AST_PROGRAM);
    if (!prog) return NULL;

    skip_newlines(p);

    while (!tok_check(p, TOK_EOF)) {
        skip_newlines(p);
        if (tok_check(p, TOK_EOF)) break;

        /* Only let-statements in Phase 1 */
        if (!tok_check(p, TOK_LET)) {
            Token *t = tok_peek(p);
            char   buf[64];
            sprintf(buf, "expected 'let', got [%s]", token_type_name(t->type));
            error_report("parser", buf, t->line, t->col);
            p->had_error = 1;
            break;
        }

        AstNode *stmt = parse_let_stmt(p);
        if (!stmt || p->had_error) break;

        if (prog->stmt_count < MAX_BINDINGS)
            prog->stmts[prog->stmt_count++] = stmt;

        skip_newlines(p);
    }

    return prog;
}
