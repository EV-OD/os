#include "parser.h"
#include "error.h"
#include "stdio.h"
#include "string.h"

/* ======================================================================
 * Token helpers
 * ==================================================================== */
void parser_init(Parser *p, Lexer *l)
{
    p->lexer      = l;
    p->cur        = 0;
    p->had_error  = 0;
    p->loop_depth = 0;
}

static Token *tok_peek(Parser *p)
{
    return &p->lexer->tokens[p->cur];
}

static Token *tok_peek2(Parser *p)
{
    int next = p->cur + 1;
    /* skip a single newline for lookahead */
    if (p->lexer->tokens[next].type == TOK_NEWLINE) next++;
    if (next >= p->lexer->count) next = p->lexer->count - 1;
    return &p->lexer->tokens[next];
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

static int tok_match(Parser *p, TokenType type)
{
    if (tok_check(p, type)) { tok_advance(p); return 1; }
    return 0;
}

static Token *tok_expect(Parser *p, TokenType type, const char *msg)
{
    if (tok_check(p, type)) return tok_advance(p);
    Token *t = tok_peek(p);
    char   buf[128];
    sprintf(buf, "%s (got '%s')", msg, token_type_name(t->type));
    error_report("parser", buf, t->line, t->col);
    p->had_error = 1;
    return NULL;
}

static void skip_newlines(Parser *p)
{
    while (tok_check(p, TOK_NEWLINE) || tok_check(p, TOK_SEMICOLON))
        tok_advance(p);
}

/* Consume an optional statement-end newline or semicolon */
static void stmt_end(Parser *p)
{
    while (tok_check(p, TOK_NEWLINE) || tok_check(p, TOK_SEMICOLON))
        tok_advance(p);
}

/* Check whether the current token is a type keyword */
static int tok_is_type(Parser *p)
{
    TokenType t = tok_peek(p)->type;
    return (t == TOK_TYPE_I8  || t == TOK_TYPE_I16 ||
            t == TOK_TYPE_I32 || t == TOK_TYPE_U8  ||
            t == TOK_TYPE_U16 || t == TOK_TYPE_U32 ||
            t == TOK_TYPE_BOOL|| t == TOK_TYPE_STR ||
            t == TOK_TYPE_VOID);
}

/* Check whether the current token is a GUI keyword */
static int tok_is_gui(Parser *p)
{
    TokenType t = tok_peek(p)->type;
    return (t == TOK_GUI_WINDOW       || t == TOK_GUI_FILL         ||
            t == TOK_GUI_RECT         || t == TOK_GUI_TEXT         ||
            t == TOK_GUI_LINE         || t == TOK_GUI_CIRCLE       ||
            t == TOK_GUI_FLUSH        || t == TOK_GUI_CLOSE        ||
            t == TOK_GUI_WAIT         || t == TOK_GUI_POLL         ||
            t == TOK_GUI_PEN          || t == TOK_GUI_FILL_RECT    ||
            t == TOK_GUI_FILL_CIRCLE  || t == TOK_GUI_FILL_ROUND);
}

/* Parse and return a type name string into buf (MAX_IDENT_LEN). */
static int parse_type(Parser *p, char *buf)
{
    if (tok_is_type(p)) {
        Token *tt = tok_advance(p);
        strncpy(buf, token_type_name(tt->type), MAX_IDENT_LEN - 1);
        buf[MAX_IDENT_LEN - 1] = '\0';
        return 1;
    }
    if (tok_check(p, TOK_IDENT)) {
        Token *tt = tok_advance(p);
        strncpy(buf, tt->text, MAX_IDENT_LEN - 1);
        buf[MAX_IDENT_LEN - 1] = '\0';
        return 1;
    }
    Token *t = tok_peek(p);
    error_report("parser", "expected type name", t->line, t->col);
    p->had_error = 1;
    return 0;
}

/* ======================================================================
 * Forward declarations
 * ==================================================================== */
static AstNode *parse_expr(Parser *p);
static AstNode *parse_block(Parser *p);
static AstNode *parse_stmt(Parser *p);

/* ======================================================================
 * Argument list  "(" [ expr { "," expr } ] ")"
 * Returns number of arguments parsed into args[]; -1 on error.
 * ==================================================================== */
static int parse_arg_list(Parser *p, AstNode **args, int max_args)
{
    int count = 0;
    if (!tok_expect(p, TOK_LPAREN, "expected '(' in call")) return -1;
    skip_newlines(p);
    if (!tok_check(p, TOK_RPAREN)) {
        while (1) {
            if (count >= max_args) {
                error_report("parser", "too many arguments", tok_peek(p)->line, tok_peek(p)->col);
                p->had_error = 1;
                return -1;
            }
            args[count] = parse_expr(p);
            if (!args[count] || p->had_error) return -1;
            count++;
            skip_newlines(p);
            if (!tok_match(p, TOK_COMMA)) break;
            skip_newlines(p);
        }
    }
    if (!tok_expect(p, TOK_RPAREN, "expected ')' after arguments")) return -1;
    return count;
}

/* ======================================================================
 * primary ::= NUMBER | STRING | true | false
 *           | IDENT [ "(" arg_list ")" ]
 *           | gui_call
 *           | "(" expr ")"
 * ==================================================================== */
static AstNode *parse_primary(Parser *p)
{
    Token *t = tok_peek(p);

    /* Integer literal */
    if (t->type == TOK_NUMBER) {
        tok_advance(p);
        AstNode *n = ast_alloc(AST_NUMBER);
        if (!n) return NULL;
        n->number = t->int_val;
        n->line = t->line; n->col = t->col;
        return n;
    }

    /* Boolean literals */
    if (t->type == TOK_TRUE || t->type == TOK_FALSE) {
        tok_advance(p);
        AstNode *n = ast_alloc(AST_BOOL);
        if (!n) return NULL;
        n->bool_val = (t->type == TOK_TRUE) ? 1 : 0;
        n->line = t->line; n->col = t->col;
        return n;
    }

    /* String literal */
    if (t->type == TOK_STRING) {
        tok_advance(p);
        AstNode *n = ast_alloc(AST_STRING);
        if (!n) return NULL;
        strncpy(n->name, t->text, MAX_IDENT_LEN - 1);
        n->number = t->str_len;
        n->line = t->line; n->col = t->col;
        return n;
    }

    /* GUI built-in call */
    if (tok_is_gui(p)) {
        tok_advance(p);
        AstNode *n = ast_alloc(AST_GUI_CALL);
        if (!n) return NULL;
        strncpy(n->name, token_type_name(t->type), MAX_IDENT_LEN - 1);
        n->line = t->line; n->col = t->col;

        /* Map to syscall number (SYS_GUI_* defined in syscall.h) */
        switch (t->type) {
            case TOK_GUI_WINDOW: n->gui_op = 6;  break;
            case TOK_GUI_FILL:   n->gui_op = 8;  break;
            case TOK_GUI_RECT:   n->gui_op = 11; break;
            case TOK_GUI_TEXT:   n->gui_op = 9;  break;
            case TOK_GUI_LINE:   n->gui_op = 10; break;
            case TOK_GUI_CIRCLE: n->gui_op = 12; break;
            case TOK_GUI_FLUSH:  n->gui_op = 13; break;
            case TOK_GUI_CLOSE:  n->gui_op = 7;  break;
            case TOK_GUI_WAIT:         n->gui_op = 15; break;
            case TOK_GUI_POLL:         n->gui_op = 14; break;
            case TOK_GUI_PEN:          n->gui_op = 16; break;
            case TOK_GUI_FILL_RECT:    n->gui_op = 17; break;
            case TOK_GUI_FILL_CIRCLE:  n->gui_op = 18; break;
            case TOK_GUI_FILL_ROUND:   n->gui_op = 19; break;
            default:                   n->gui_op = 0;  break;
        }

        int nc = parse_arg_list(p, n->args, MAX_ARGS);
        if (nc < 0) return NULL;
        n->arg_count = nc;
        return n;
    }

    /* Identifier or function call */
    if (t->type == TOK_IDENT) {
        tok_advance(p);
        if (tok_check(p, TOK_LPAREN)) {
            /* function call: IDENT "(" args ")" */
            AstNode *n = ast_alloc(AST_CALL);
            if (!n) return NULL;
            strncpy(n->name, t->text, MAX_IDENT_LEN - 1);
            n->line = t->line; n->col = t->col;
            int nc = parse_arg_list(p, n->args, MAX_ARGS);
            if (nc < 0) return NULL;
            n->arg_count = nc;
            return n;
        }
        /* Simple variable reference */
        AstNode *n = ast_alloc(AST_IDENT);
        if (!n) return NULL;
        strncpy(n->name, t->text, MAX_IDENT_LEN - 1);
        n->line = t->line; n->col = t->col;
        return n;
    }

    /* Parenthesised expression */
    if (t->type == TOK_LPAREN) {
        tok_advance(p);
        AstNode *n = parse_expr(p);
        if (!tok_expect(p, TOK_RPAREN, "expected ')'")) return NULL;
        return n;
    }

    /* Error */
    char buf[80];
    sprintf(buf, "unexpected token '%s' in expression", token_type_name(t->type));
    error_report("parser", buf, t->line, t->col);
    p->had_error = 1;
    return NULL;
}

/* ======================================================================
 * postfix ::= primary { "[" expr "]" | "as" type }
 * ==================================================================== */
static AstNode *parse_postfix(Parser *p)
{
    AstNode *n = parse_primary(p);
    if (!n || p->had_error) return NULL;

    while (1) {
        if (tok_check(p, TOK_LBRACKET)) {
            tok_advance(p);
            AstNode *idx = ast_alloc(AST_INDEX);
            if (!idx) return NULL;
            if (n->type == AST_IDENT) strncpy(idx->name, n->name, MAX_IDENT_LEN - 1);
            idx->left = n;                    /* array base */
            idx->expr = parse_expr(p);        /* index      */
            if (!idx->expr || p->had_error) return NULL;
            if (!tok_expect(p, TOK_RBRACKET, "expected ']'")) return NULL;
            n = idx;
        } else if (tok_check(p, TOK_AS)) {
            tok_advance(p);
            AstNode *cast = ast_alloc(AST_CAST);
            if (!cast) return NULL;
            cast->expr = n;
            if (!parse_type(p, cast->type_name)) return NULL;
            cast->resolved_type = rostype_from_str(cast->type_name);
            n = cast;
        } else {
            break;
        }
    }
    return n;
}

/* ======================================================================
 * unary ::= ("!" | "-" | "~") unary | postfix
 * ==================================================================== */
static AstNode *parse_unary(Parser *p)
{
    TokenType t = tok_peek(p)->type;
    if (t == TOK_NOT || t == TOK_MINUS || t == TOK_BITNOT) {
        Token *op = tok_advance(p);
        AstNode *n = ast_alloc(AST_UNARY);
        if (!n) return NULL;
        n->op   = (t == TOK_NOT) ? '!' : (t == TOK_MINUS) ? '-' : '~';
        n->left = parse_unary(p);
        n->line = op->line; n->col = op->col;
        if (!n->left || p->had_error) return NULL;
        return n;
    }
    return parse_postfix(p);
}

/* ======================================================================
 * multiplicative ::= unary { ("*"|"/"|"%") unary }
 * ==================================================================== */
static AstNode *parse_multiplicative(Parser *p)
{
    AstNode *left = parse_unary(p);
    if (!left || p->had_error) return NULL;

    while (tok_check(p, TOK_STAR) || tok_check(p, TOK_SLASH) || tok_check(p, TOK_PERCENT)) {
        Token   *op    = tok_advance(p);
        AstNode *right = parse_unary(p);
        if (!right || p->had_error) return NULL;
        AstNode *node = ast_alloc(AST_BINOP);
        if (!node) return NULL;
        node->op    = (op->type == TOK_STAR) ? '*' : (op->type == TOK_SLASH) ? '/' : '%';
        node->left  = left;
        node->right = right;
        node->line  = op->line; node->col = op->col;
        left = node;
    }
    return left;
}

/* ======================================================================
 * additive ::= multiplicative { ("+"|"-") multiplicative }
 * ==================================================================== */
static AstNode *parse_additive(Parser *p)
{
    AstNode *left = parse_multiplicative(p);
    if (!left || p->had_error) return NULL;

    while (tok_check(p, TOK_PLUS) || tok_check(p, TOK_MINUS)) {
        Token   *op    = tok_advance(p);
        AstNode *right = parse_multiplicative(p);
        if (!right || p->had_error) return NULL;
        AstNode *node = ast_alloc(AST_BINOP);
        if (!node) return NULL;
        node->op    = (op->type == TOK_PLUS) ? '+' : '-';
        node->left  = left;
        node->right = right;
        node->line  = op->line; node->col = op->col;
        left = node;
    }
    return left;
}

/* ======================================================================
 * shift ::= additive { ("<<"|">>") additive }
 * ==================================================================== */
static AstNode *parse_shift(Parser *p)
{
    AstNode *left = parse_additive(p);
    if (!left || p->had_error) return NULL;

    while (tok_check(p, TOK_SHL) || tok_check(p, TOK_SHR)) {
        Token   *op    = tok_advance(p);
        AstNode *right = parse_additive(p);
        if (!right || p->had_error) return NULL;
        AstNode *node = ast_alloc(AST_BINOP);
        if (!node) return NULL;
        node->op    = (op->type == TOK_SHL) ? '<' : '>';
        node->op2   = (op->type == TOK_SHL) ? '<' : '>';
        node->left  = left;
        node->right = right;
        left = node;
    }
    return left;
}

/* ======================================================================
 * bitwise_and ::= shift { "&" shift }
 * ==================================================================== */
static AstNode *parse_bitwise_and(Parser *p)
{
    AstNode *left = parse_shift(p);
    if (!left || p->had_error) return NULL;

    while (tok_check(p, TOK_BITAND)) {
        Token   *op    = tok_advance(p);
        AstNode *right = parse_shift(p);
        if (!right || p->had_error) return NULL;
        AstNode *node = ast_alloc(AST_BINOP);
        if (!node) return NULL;
        node->op = '&'; node->left = left; node->right = right;
        node->line = op->line; left = node;
    }
    return left;
}

/* ======================================================================
 * bitwise_xor ::= bitwise_and { "^" bitwise_and }
 * ==================================================================== */
static AstNode *parse_bitwise_xor(Parser *p)
{
    AstNode *left = parse_bitwise_and(p);
    if (!left || p->had_error) return NULL;

    while (tok_check(p, TOK_BITXOR)) {
        Token   *op    = tok_advance(p);
        AstNode *right = parse_bitwise_and(p);
        if (!right || p->had_error) return NULL;
        AstNode *node = ast_alloc(AST_BINOP);
        if (!node) return NULL;
        node->op = '^'; node->left = left; node->right = right;
        node->line = op->line; left = node;
    }
    return left;
}

/* ======================================================================
 * bitwise_or ::= bitwise_xor { "|" bitwise_xor }
 * ==================================================================== */
static AstNode *parse_bitwise_or(Parser *p)
{
    AstNode *left = parse_bitwise_xor(p);
    if (!left || p->had_error) return NULL;

    while (tok_check(p, TOK_BITOR)) {
        Token   *op    = tok_advance(p);
        AstNode *right = parse_bitwise_xor(p);
        if (!right || p->had_error) return NULL;
        AstNode *node = ast_alloc(AST_BINOP);
        if (!node) return NULL;
        node->op = '|'; node->left = left; node->right = right;
        node->line = op->line; left = node;
    }
    return left;
}

/* ======================================================================
 * comparison ::= bitwise_or { ("=="|"!="|"<"|">"|"<="|">=") bitwise_or }
 * ==================================================================== */
static AstNode *parse_comparison(Parser *p)
{
    AstNode *left = parse_bitwise_or(p);
    if (!left || p->had_error) return NULL;

    for (;;) {
        TokenType tt = tok_peek(p)->type;
        if (tt != TOK_EQEQ && tt != TOK_NEQ  && tt != TOK_LT &&
            tt != TOK_GT   && tt != TOK_LTE   && tt != TOK_GTE)
            break;

        Token *op = tok_advance(p);
        AstNode *right = parse_bitwise_or(p);
        if (!right || p->had_error) return NULL;

        AstNode *node = ast_alloc(AST_BINOP);
        if (!node) return NULL;
        /* encode op in node->op + op2 */
        switch (tt) {
            case TOK_EQEQ: node->op = '='; node->op2 = '='; break;
            case TOK_NEQ:  node->op = '!'; node->op2 = '='; break;
            case TOK_LT:   node->op = '<'; node->op2 = 0;   break;
            case TOK_GT:   node->op = '>'; node->op2 = 0;   break;
            case TOK_LTE:  node->op = '<'; node->op2 = '='; break;
            case TOK_GTE:  node->op = '>'; node->op2 = '='; break;
            default: break;
        }
        node->left  = left;
        node->right = right;
        node->line  = op->line;
        left = node;
    }
    return left;
}

/* ======================================================================
 * logical_and ::= comparison { "&&" comparison }
 * ==================================================================== */
static AstNode *parse_logical_and(Parser *p)
{
    AstNode *left = parse_comparison(p);
    if (!left || p->had_error) return NULL;

    while (tok_check(p, TOK_AND)) {
        Token   *op    = tok_advance(p);
        AstNode *right = parse_comparison(p);
        if (!right || p->had_error) return NULL;
        AstNode *node = ast_alloc(AST_BINOP);
        if (!node) return NULL;
        node->op = 'A'; /* 'A' = && */
        node->left = left; node->right = right;
        node->line = op->line;
        left = node;
    }
    return left;
}

/* ======================================================================
 * logical_or ::= logical_and { "||" logical_and }
 * ==================================================================== */
static AstNode *parse_logical_or(Parser *p)
{
    AstNode *left = parse_logical_and(p);
    if (!left || p->had_error) return NULL;

    while (tok_check(p, TOK_OR)) {
        Token   *op    = tok_advance(p);
        AstNode *right = parse_logical_and(p);
        if (!right || p->had_error) return NULL;
        AstNode *node = ast_alloc(AST_BINOP);
        if (!node) return NULL;
        node->op = 'O'; /* 'O' = || */
        node->left = left; node->right = right;
        node->line = op->line;
        left = node;
    }
    return left;
}

static AstNode *parse_expr(Parser *p)
{
    return parse_logical_or(p);
}

/* ======================================================================
 * block ::= "{" stmt* "}"
 * ==================================================================== */
static AstNode *parse_block(Parser *p)
{
    Token *lb = tok_peek(p);
    if (!tok_expect(p, TOK_LBRACE, "expected '{'")) return NULL;

    AstNode *block = ast_alloc(AST_BLOCK);
    if (!block) return NULL;
    block->line = lb->line;

    skip_newlines(p);
    while (!tok_check(p, TOK_RBRACE) && !tok_check(p, TOK_EOF)) {
        AstNode *s = parse_stmt(p);
        if (!s || p->had_error) return NULL;
        if (block->stmt_count < MAX_STMTS)
            block->stmts[block->stmt_count++] = s;
        skip_newlines(p);
    }
    if (!tok_expect(p, TOK_RBRACE, "expected '}'")) return NULL;
    return block;
}

/* ======================================================================
 * Statements
 * ==================================================================== */

/* let_stmt ::= ("let" ["mut"] | "mut") IDENT ":" type "=" expr */
static AstNode *parse_let_stmt(Parser *p, int is_mut)
{
    tok_advance(p);   /* consume 'let' or 'mut' */

    /* Accept 'let mut x' as well as standalone 'mut x' */
    if (!is_mut && tok_check(p, TOK_MUT)) {
        tok_advance(p);   /* consume 'mut' */
        is_mut = 1;
    }

    Token *name_tok = tok_expect(p, TOK_IDENT, "expected variable name");
    if (!name_tok) return NULL;

    if (!tok_expect(p, TOK_COLON, "expected ':' after name")) return NULL;

    AstNode *node = ast_alloc(is_mut ? AST_MUT : AST_LET);
    if (!node) return NULL;
    strncpy(node->name, name_tok->text, MAX_IDENT_LEN - 1);
    node->line = name_tok->line;

    if (!parse_type(p, node->type_name)) return NULL;

    if (!tok_expect(p, TOK_EQ, "expected '=' after type")) return NULL;

    node->expr = parse_expr(p);
    if (!node->expr || p->had_error) return NULL;

    stmt_end(p);
    return node;
}

/* assign_stmt ::= IDENT ( "=" | "+=" | "-=" | "*=" | "/=" | "%=" ) expr */
static AstNode *parse_assign_stmt(Parser *p)
{
    Token *name_tok = tok_advance(p);   /* IDENT already checked */
    Token *op_tok   = tok_advance(p);   /* consume the assignment op */

    AstNode *node = ast_alloc(AST_ASSIGN);
    if (!node) return NULL;
    strncpy(node->name, name_tok->text, MAX_IDENT_LEN - 1);
    node->line = name_tok->line;

    /* Encode compound operator: store base operator or 0 for plain = */
    switch (op_tok->type) {
        case TOK_EQ:         node->op = 0;   break;
        case TOK_PLUS_EQ:    node->op = '+'; break;
        case TOK_MINUS_EQ:   node->op = '-'; break;
        case TOK_STAR_EQ:    node->op = '*'; break;
        case TOK_SLASH_EQ:   node->op = '/'; break;
        case TOK_PERCENT_EQ: node->op = '%'; break;
        default:             node->op = 0;   break;
    }

    node->expr = parse_expr(p);
    if (!node->expr || p->had_error) return NULL;

    stmt_end(p);
    return node;
}

/* if_stmt ::= "if" expr block [ "else" ( if_stmt | block ) ] */
static AstNode *parse_if_stmt(Parser *p)
{
    Token *if_tok = tok_advance(p);   /* consume 'if' */
    AstNode *node = ast_alloc(AST_IF);
    if (!node) return NULL;
    node->line = if_tok->line;

    node->cond = parse_expr(p);
    if (!node->cond || p->had_error) return NULL;

    skip_newlines(p);
    node->then_b = parse_block(p);
    if (!node->then_b || p->had_error) return NULL;

    /* optional else */
    skip_newlines(p);
    if (tok_check(p, TOK_ELSE)) {
        tok_advance(p);
        skip_newlines(p);
        if (tok_check(p, TOK_IF)) {
            node->else_b = parse_if_stmt(p);
        } else {
            node->else_b = parse_block(p);
        }
        if (!node->else_b || p->had_error) return NULL;
    }
    return node;
}

/* while_stmt ::= "while" expr block */
static AstNode *parse_while_stmt(Parser *p)
{
    Token *wt = tok_advance(p);   /* consume 'while' */
    AstNode *node = ast_alloc(AST_WHILE);
    if (!node) return NULL;
    node->line = wt->line;

    node->cond = parse_expr(p);
    if (!node->cond || p->had_error) return NULL;

    skip_newlines(p);
    p->loop_depth++;
    node->body = parse_block(p);
    p->loop_depth--;
    if (!node->body || p->had_error) return NULL;
    return node;
}

/* for_stmt ::= "for" IDENT ":" type "in" expr block */
static AstNode *parse_for_stmt(Parser *p)
{
    Token *ft = tok_advance(p);   /* consume 'for' */
    AstNode *node = ast_alloc(AST_FOR);
    if (!node) return NULL;
    node->line = ft->line;

    Token *var_tok = tok_expect(p, TOK_IDENT, "expected loop variable name");
    if (!var_tok) return NULL;
    strncpy(node->name, var_tok->text, MAX_IDENT_LEN - 1);

    /* Optional type annotation */
    if (tok_check(p, TOK_COLON)) {
        tok_advance(p);
        if (!parse_type(p, node->type_name)) return NULL;
    } else {
        strncpy(node->type_name, "i32", MAX_IDENT_LEN - 1);
    }

    if (!tok_expect(p, TOK_IN, "expected 'in'")) return NULL;

    /* The range expression: either  lo..hi  or any iterable expr */
    AstNode *lo = parse_additive(p);
    if (!lo || p->had_error) return NULL;

    if (tok_check(p, TOK_DOTDOT)) {
        tok_advance(p);
        AstNode *hi = parse_additive(p);
        if (!hi || p->had_error) return NULL;
        AstNode *range = ast_alloc(AST_RANGE);
        if (!range) return NULL;
        range->left  = lo;
        range->right = hi;
        node->range  = range;
    } else {
        /* treat the expression as the range (must be iterable – future) */
        node->range = lo;
    }

    skip_newlines(p);
    p->loop_depth++;
    node->body = parse_block(p);
    p->loop_depth--;
    if (!node->body || p->had_error) return NULL;
    return node;
}

/* return_stmt ::= "return" [ expr ] */
static AstNode *parse_return_stmt(Parser *p)
{
    Token   *rt   = tok_advance(p);
    AstNode *node = ast_alloc(AST_RETURN);
    if (!node) return NULL;
    node->line = rt->line;

    /* Optional expression (void return has no expression) */
    if (!tok_check(p, TOK_NEWLINE) && !tok_check(p, TOK_SEMICOLON) &&
        !tok_check(p, TOK_RBRACE)  && !tok_check(p, TOK_EOF)) {
        node->expr = parse_expr(p);
        if (!node->expr || p->had_error) return NULL;
    }
    stmt_end(p);
    return node;
}

/* print_stmt ::= ("print"|"println") "(" expr ")" */
static AstNode *parse_print_stmt(Parser *p, int is_println)
{
    tok_advance(p);

    if (!tok_expect(p, TOK_LPAREN, "expected '(' after print")) return NULL;

    AstNode *node = ast_alloc(is_println ? AST_PRINTLN : AST_PRINT);
    if (!node) return NULL;

    node->expr = parse_expr(p);
    if (!node->expr || p->had_error) return NULL;

    if (!tok_expect(p, TOK_RPAREN, "expected ')' after print arg")) return NULL;

    stmt_end(p);
    return node;
}

/* ======================================================================
 * Generic statement dispatcher
 * ==================================================================== */
static AstNode *parse_stmt(Parser *p)
{
    skip_newlines(p);

    TokenType tt  = tok_peek(p)->type;
    TokenType tt2 = tok_peek2(p)->type;

    if (tt == TOK_LET)          return parse_let_stmt(p, 0);
    if (tt == TOK_MUT)          return parse_let_stmt(p, 1);
    if (tt == TOK_IF)           return parse_if_stmt(p);
    if (tt == TOK_WHILE)        return parse_while_stmt(p);
    if (tt == TOK_FOR)          return parse_for_stmt(p);
    if (tt == TOK_RETURN)       return parse_return_stmt(p);
    if (tt == TOK_PRINT)        return parse_print_stmt(p, 0);
    if (tt == TOK_PRINTLN)      return parse_print_stmt(p, 1);

    if (tt == TOK_BREAK) {
        Token *bt = tok_advance(p);
        if (p->loop_depth == 0) {
            error_report("parser", "break outside loop", bt->line, bt->col);
            p->had_error = 1; return NULL;
        }
        AstNode *n = ast_alloc(AST_BREAK);
        if (n) n->line = bt->line;
        stmt_end(p);
        return n;
    }

    if (tt == TOK_CONTINUE) {
        Token *ct = tok_advance(p);
        if (p->loop_depth == 0) {
            error_report("parser", "continue outside loop", ct->line, ct->col);
            p->had_error = 1; return NULL;
        }
        AstNode *n = ast_alloc(AST_CONTINUE);
        if (n) n->line = ct->line;
        stmt_end(p);
        return n;
    }

    /* Assignment:  IDENT ( = | += | -= | *= | /= | %= ) expr */
    if (tt == TOK_IDENT &&
        (tt2 == TOK_EQ       || tt2 == TOK_PLUS_EQ  || tt2 == TOK_MINUS_EQ ||
         tt2 == TOK_STAR_EQ  || tt2 == TOK_SLASH_EQ || tt2 == TOK_PERCENT_EQ)) {
        return parse_assign_stmt(p);
    }

    /* Expression statement (function call, GUI call, etc.) */
    AstNode *expr = parse_expr(p);
    if (!expr || p->had_error) return NULL;

    AstNode *n = ast_alloc(AST_EXPR_STMT);
    if (!n) return NULL;
    n->expr = expr;
    n->line = expr->line;
    stmt_end(p);
    return n;
}

/* ======================================================================
 * fn_def ::= "fn" IDENT "(" param_list ")" [ "->" type ] block
 * ==================================================================== */
static AstNode *parse_fn_def(Parser *p)
{
    tok_advance(p);   /* consume 'fn' */

    Token *name_tok = tok_expect(p, TOK_IDENT, "expected function name");
    if (!name_tok) return NULL;

    AstNode *node = ast_alloc(AST_FUNC_DEF);
    if (!node) return NULL;
    strncpy(node->name, name_tok->text, MAX_IDENT_LEN - 1);
    node->line = name_tok->line;

    /* Parameters */
    if (!tok_expect(p, TOK_LPAREN, "expected '(' in fn")) return NULL;
    skip_newlines(p);

    while (!tok_check(p, TOK_RPAREN) && !tok_check(p, TOK_EOF)) {
        if (node->param_count >= MAX_PARAMS) {
            error_report("parser", "too many parameters",
                         tok_peek(p)->line, tok_peek(p)->col);
            p->had_error = 1; return NULL;
        }
        Token *pn = tok_expect(p, TOK_IDENT, "expected parameter name");
        if (!pn) return NULL;
        if (!tok_expect(p, TOK_COLON, "expected ':' after param name")) return NULL;

        Param *par = &node->params[node->param_count++];
        strncpy(par->name, pn->text, MAX_IDENT_LEN - 1);
        if (!parse_type(p, par->type_name)) return NULL;
        par->resolved = rostype_from_str(par->type_name);

        skip_newlines(p);
        if (!tok_match(p, TOK_COMMA)) break;
        skip_newlines(p);
    }
    if (!tok_expect(p, TOK_RPAREN, "expected ')' after parameters")) return NULL;

    /* Optional return type */
    strncpy(node->ret_type, "void", MAX_IDENT_LEN - 1);
    node->ret_resolved = TY_VOID;
    if (tok_check(p, TOK_ARROW)) {
        tok_advance(p);
        if (!parse_type(p, node->ret_type)) return NULL;
        node->ret_resolved = rostype_from_str(node->ret_type);
    }

    skip_newlines(p);
    node->body = parse_block(p);
    if (!node->body || p->had_error) return NULL;
    return node;
}

/* ======================================================================
 * program ::= top_item*
 * top_item ::= fn_def | stmt
 * ==================================================================== */
AstNode *parser_parse(Parser *p)
{
    AstNode *prog = ast_alloc(AST_PROGRAM);
    if (!prog) return NULL;

    skip_newlines(p);
    while (!tok_check(p, TOK_EOF)) {
        skip_newlines(p);
        if (tok_check(p, TOK_EOF)) break;

        AstNode *item = NULL;
        if (tok_check(p, TOK_FN)) {
            item = parse_fn_def(p);
        } else {
            item = parse_stmt(p);
        }

        if (!item || p->had_error) break;

        if (prog->stmt_count < MAX_STMTS)
            prog->stmts[prog->stmt_count++] = item;

        skip_newlines(p);
    }

    return prog;
}
