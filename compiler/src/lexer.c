#include "lexer.h"
#include "error.h"
#include "stdio.h"
#include "string.h"

/* -----------------------------------------------------------------------
 * Character classification helpers
 * --------------------------------------------------------------------- */
static int is_digit(char c) { return (c >= '0' && c <= '9'); }
static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int is_alnum(char c) { return is_alpha(c) || is_digit(c); }
/* Horizontal whitespace only – newlines are kept as tokens. */
static int is_hspace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

/* -----------------------------------------------------------------------
 * Low-level source advancement
 * --------------------------------------------------------------------- */
static char peek_char(Lexer *l)
{
    return l->src[l->pos];
}

static char advance_char(Lexer *l)
{
    char c = l->src[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; }
    else           { l->col++;              }
    return c;
}

/* Allocate the next token slot; returns NULL on overflow. */
static Token *new_tok(Lexer *l, TokenType type, int line, int col)
{
    if (l->count >= MAX_TOKENS) {
        error_report("lexer", "token buffer overflow", line, col);
        l->had_error = 1;
        return NULL;
    }
    Token *t = &l->tokens[l->count++];
    memset(t, 0, sizeof(Token));
    t->type = type;
    t->line = line;
    t->col  = col;
    return t;
}

/* -----------------------------------------------------------------------
 * Number and identifier scanning
 * --------------------------------------------------------------------- */
static void scan_number(Lexer *l)
{
    int line = l->line, col = l->col;
    i32 val  = 0;
    while (is_digit(peek_char(l)))
        val = val * 10 + (advance_char(l) - '0');
    Token *t = new_tok(l, TOK_NUMBER, line, col);
    if (t) t->int_val = val;
}

static void scan_ident(Lexer *l)
{
    int  line = l->line, col = l->col;
    char buf[MAX_IDENT_LEN];
    int  i = 0;

    while (is_alnum(peek_char(l)) && i < MAX_IDENT_LEN - 1)
        buf[i++] = advance_char(l);
    buf[i] = '\0';

    /* Keyword / type classification */
    TokenType type;
    if      (strcmp(buf, "let")  == 0) type = TOK_LET;
    else if (strcmp(buf, "i32")  == 0) type = TOK_TYPE_I32;
    else if (strcmp(buf, "u32")  == 0) type = TOK_TYPE_U32;
    else if (strcmp(buf, "bool") == 0) type = TOK_TYPE_BOOL;
    else                               type = TOK_IDENT;

    Token *t = new_tok(l, type, line, col);
    if (t) strncpy(t->text, buf, MAX_IDENT_LEN - 1);
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */
void lexer_init(Lexer *l, const char *src)
{
    l->src       = src;
    l->pos       = 0;
    l->line      = 1;
    l->col       = 1;
    l->count     = 0;
    l->had_error = 0;
    memset(l->tokens, 0, sizeof(l->tokens));
}

int lexer_tokenize(Lexer *l)
{
    while (1) {
        /* Skip horizontal whitespace */
        while (is_hspace(peek_char(l)))
            advance_char(l);

        int  line = l->line, col = l->col;
        char c    = peek_char(l);

        /* End of source */
        if (c == '\0') {
            new_tok(l, TOK_EOF, line, col);
            break;
        }

        /* Newline – preserved as token for parser recovery */
        if (c == '\n') {
            advance_char(l);
            new_tok(l, TOK_NEWLINE, line, col);
            continue;
        }

        /* Line comments  //  */
        if (c == '/' && l->src[l->pos + 1] == '/') {
            while (peek_char(l) != '\n' && peek_char(l) != '\0')
                advance_char(l);
            continue;
        }

        /* Numbers */
        if (is_digit(c)) { scan_number(l); continue; }

        /* Identifiers / keywords */
        if (is_alpha(c)) { scan_ident(l);  continue; }

        /* Single-character tokens */
        advance_char(l);
        switch (c) {
            case '+': new_tok(l, TOK_PLUS,   line, col); break;
            case '-': new_tok(l, TOK_MINUS,  line, col); break;
            case '*': new_tok(l, TOK_STAR,   line, col); break;
            case '/': new_tok(l, TOK_SLASH,  line, col); break;
            case '(': new_tok(l, TOK_LPAREN, line, col); break;
            case ')': new_tok(l, TOK_RPAREN, line, col); break;
            case ':': new_tok(l, TOK_COLON,  line, col); break;
            case '=': new_tok(l, TOK_EQ,     line, col); break;
            default: {
                char msg[64];
                /* Build message without %c (unknown if supported) */
                msg[0] = 'u'; msg[1] = 'n'; msg[2] = 'k'; msg[3] = 'n';
                msg[4] = 'o'; msg[5] = 'w'; msg[6] = 'n'; msg[7] = ' ';
                msg[8] = '\''; msg[9] = c; msg[10] = '\''; msg[11] = '\0';
                error_report("lexer", msg, line, col);
                new_tok(l, TOK_ERROR, line, col);
                l->had_error = 1;
            }
        }
    }
    return l->had_error ? -1 : 0;
}

const char *token_type_name(TokenType t)
{
    switch (t) {
        case TOK_NUMBER:    return "NUM";
        case TOK_PLUS:      return "+";
        case TOK_MINUS:     return "-";
        case TOK_STAR:      return "*";
        case TOK_SLASH:     return "/";
        case TOK_LPAREN:    return "(";
        case TOK_RPAREN:    return ")";
        case TOK_LET:       return "let";
        case TOK_COLON:     return ":";
        case TOK_EQ:        return "=";
        case TOK_IDENT:     return "IDENT";
        case TOK_TYPE_I32:  return "i32";
        case TOK_TYPE_U32:  return "u32";
        case TOK_TYPE_BOOL: return "bool";
        case TOK_NEWLINE:   return "NL";
        case TOK_EOF:       return "EOF";
        default:            return "ERR";
    }
}

void lexer_dump(Lexer *l)
{
    char buf[128];
    puts("Tokens:\n  ");
    for (int i = 0; i < l->count; i++) {
        Token *t = &l->tokens[i];
        if (t->type == TOK_NEWLINE) {
            puts("\n  ");
            continue;
        }
        if (t->type == TOK_EOF) {
            puts("[EOF]");
            break;
        }
        if (t->type == TOK_NUMBER) {
            sprintf(buf, "[NUM:%d]", t->int_val);
        } else if (t->type == TOK_IDENT) {
            sprintf(buf, "[IDENT:%s]", t->text);
        } else if (t->type == TOK_TYPE_I32 || t->type == TOK_TYPE_U32 ||
                   t->type == TOK_TYPE_BOOL) {
            sprintf(buf, "[TYPE:%s]", t->text);
        } else {
            sprintf(buf, "[%s]", token_type_name(t->type));
        }
        puts(buf);
        puts(" ");
    }
    puts("\n");
}
