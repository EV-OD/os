#include "lexer.h"
#include "error.h"
#include "stdio.h"
#include "string.h"

/* -----------------------------------------------------------------------
 * Character classification helpers
 * --------------------------------------------------------------------- */
static int is_digit(char c)  { return (c >= '0' && c <= '9'); }
static int is_xdigit(char c) { return is_digit(c) || (c>='a'&&c<='f') || (c>='A'&&c<='F'); }
static int is_alpha(char c)  { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static int is_alnum(char c)  { return is_alpha(c) || is_digit(c); }
static int is_hspace(char c) { return c==' '||c=='\t'||c=='\r'; }

/* -----------------------------------------------------------------------
 * Low-level source advancement
 * --------------------------------------------------------------------- */
static char peek_char(Lexer *l)              { return l->src[l->pos]; }
static char peek2_char(Lexer *l)             { return (l->src[l->pos]) ? l->src[l->pos+1] : '\0'; }
static char advance_char(Lexer *l) {
    char c = l->src[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; }
    else           { l->col++;             }
    return c;
}

static Token *new_tok(Lexer *l, TokenType type, int line, int col) {
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
 * Number scanning  (decimal and 0x hex)
 * --------------------------------------------------------------------- */
static void scan_number(Lexer *l)
{
    int line = l->line, col = l->col;
    u32 val  = 0;

    if (peek_char(l) == '0' && (peek2_char(l) == 'x' || peek2_char(l) == 'X')) {
        advance_char(l); advance_char(l);   /* consume 0x */
        while (is_xdigit(peek_char(l))) {
            char c = advance_char(l);
            u32 d;
            if      (c >= '0' && c <= '9') d = (u32)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (u32)(c - 'a' + 10);
            else                            d = (u32)(c - 'A' + 10);
            val = val * 16 + d;
        }
    } else {
        while (is_digit(peek_char(l)))
            val = val * 10 + (u32)(advance_char(l) - '0');
    }

    Token *t = new_tok(l, TOK_NUMBER, line, col);
    if (t) t->int_val = (i32)val;
}

/* -----------------------------------------------------------------------
 * Keyword / identifier  table
 * --------------------------------------------------------------------- */
typedef struct { const char *kw; TokenType type; } KwEntry;

static const KwEntry KEYWORDS[] = {
    /* keywords */
    { "let",        TOK_LET       },
    { "mut",        TOK_MUT       },
    { "fn",         TOK_FN        },
    { "if",         TOK_IF        },
    { "else",       TOK_ELSE      },
    { "while",      TOK_WHILE     },
    { "for",        TOK_FOR       },
    { "in",         TOK_IN        },
    { "return",     TOK_RETURN    },
    { "break",      TOK_BREAK     },
    { "continue",   TOK_CONTINUE  },
    { "as",         TOK_AS        },
    { "struct",     TOK_STRUCT    },
    { "true",       TOK_TRUE      },
    { "false",      TOK_FALSE     },
    /* built-in functions */
    { "print",      TOK_PRINT     },
    { "println",    TOK_PRINTLN   },
    /* GUI builtins */
    { "gui_window", TOK_GUI_WINDOW},
    { "gui_fill",   TOK_GUI_FILL  },
    { "gui_rect",   TOK_GUI_RECT  },
    { "gui_text",   TOK_GUI_TEXT  },
    { "gui_line",   TOK_GUI_LINE  },
    { "gui_circle", TOK_GUI_CIRCLE},
    { "gui_flush",  TOK_GUI_FLUSH },
    { "gui_close",  TOK_GUI_CLOSE },
    { "import",          TOK_IMPORT         },
    { "from",            TOK_FROM           },
    { "gui_wait",   TOK_GUI_WAIT  },
    { "gui_poll",   TOK_GUI_POLL  },
    { "gui_pen",         TOK_GUI_PEN        },
    { "gui_fill_rect",   TOK_GUI_FILL_RECT  },
    { "gui_fill_circle", TOK_GUI_FILL_CIRCLE},
    { "gui_fill_round",  TOK_GUI_FILL_ROUND },
    { "gui_mouse",        TOK_GUI_MOUSE      },
    /* text buffer builtins */
    { "tbuf_open",      TOK_TBUF_OPEN      },
    { "tbuf_close",     TOK_TBUF_CLOSE     },
    { "tbuf_save",      TOK_TBUF_SAVE      },
    { "tbuf_getline",   TOK_TBUF_GETLINE   },
    { "tbuf_input",     TOK_TBUF_INPUT     },
    { "tbuf_linecount", TOK_TBUF_LINECOUNT },
    { "tbuf_cursor",    TOK_TBUF_CURSOR    },
    { "tbuf_numstr",    TOK_TBUF_NUMSTR    },
    { "getarg",         TOK_GETARG         },
    { "io_saveas",      TOK_TBUF_SAVEAS    },
    { "spawn_term",     TOK_SPAWN_TERM     },
    /* primitive types */
    { "i8",         TOK_TYPE_I8   },
    { "i16",        TOK_TYPE_I16  },
    { "i32",        TOK_TYPE_I32  },
    { "u8",         TOK_TYPE_U8   },
    { "u16",        TOK_TYPE_U16  },
    { "u32",        TOK_TYPE_U32  },
    { "bool",       TOK_TYPE_BOOL },
    { "str",        TOK_TYPE_STR  },
    { "void",       TOK_TYPE_VOID },
    { NULL,         TOK_IDENT     }
};

static void scan_ident(Lexer *l)
{
    int  line = l->line, col = l->col;
    char buf[MAX_IDENT_LEN];
    int  i = 0;

    while (is_alnum(peek_char(l)) && i < MAX_IDENT_LEN - 1)
        buf[i++] = advance_char(l);
    buf[i] = '\0';

    /* Look up in keyword table */
    TokenType type = TOK_IDENT;
    for (int k = 0; KEYWORDS[k].kw != NULL; k++) {
        if (strcmp(buf, KEYWORDS[k].kw) == 0) {
            type = KEYWORDS[k].type;
            break;
        }
    }

    Token *t = new_tok(l, type, line, col);
    if (t) strncpy(t->text, buf, MAX_IDENT_LEN - 1);
}

/* -----------------------------------------------------------------------
 * String literal scanning  (handles all escape sequences)
 * Strings can span up to MAX_STR_LEN characters.
 * --------------------------------------------------------------------- */
static void scan_string(Lexer *l)
{
    int line = l->line, col = l->col;
    char sbuf[MAX_STR_LEN];
    int  si = 0;

    while (peek_char(l) != '"' && peek_char(l) != '\0' && peek_char(l) != '\n') {
        char ch = advance_char(l);
        if (ch == '\\') {
            char esc = peek_char(l);
            switch (esc) {
                case 'n':  advance_char(l); ch = '\n'; break;
                case 't':  advance_char(l); ch = '\t'; break;
                case 'r':  advance_char(l); ch = '\r'; break;
                case '\\': advance_char(l); ch = '\\'; break;
                case '"':  advance_char(l); ch = '"';  break;
                case '0':  advance_char(l); ch = '\0'; break;
                default:   break;
            }
        }
        if (si < MAX_STR_LEN - 1) sbuf[si++] = ch;
    }
    if (peek_char(l) == '"') advance_char(l);   /* consume closing " */
    sbuf[si] = '\0';

    Token *t = new_tok(l, TOK_STRING, line, col);
    if (t) {
        /* Copy string content - may contain embedded NULs from \0, so
         * we copy up to si bytes to handle them properly. */
        int copy_len = (si < MAX_IDENT_LEN - 1) ? si : MAX_IDENT_LEN - 1;
        memcpy(t->text, sbuf, copy_len);
        t->text[copy_len] = '\0';
        t->str_len = si;
        t->int_val = si;
    }
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
        while (is_hspace(peek_char(l))) advance_char(l);

        int  line = l->line, col = l->col;
        char c    = peek_char(l);

        if (c == '\0') { new_tok(l, TOK_EOF, line, col); break; }

        /* Newlines */
        if (c == '\n') {
            advance_char(l);
            new_tok(l, TOK_NEWLINE, line, col);
            continue;
        }

        /* Block comments *C-style*  */
        if (c == '/' && peek2_char(l) == '*') {
            advance_char(l); advance_char(l);
            while (peek_char(l) != '\0') {
                if (peek_char(l) == '*' && peek2_char(l) == '/') {
                    advance_char(l); advance_char(l); break;
                }
                advance_char(l);
            }
            continue;
        }

        /* Line comments  //  */
        if (c == '/' && peek2_char(l) == '/') {
            while (peek_char(l) != '\n' && peek_char(l) != '\0') advance_char(l);
            continue;
        }

        /* Numbers */
        if (is_digit(c)) { scan_number(l); continue; }

        /* String literals */
        if (c == '"') { advance_char(l); scan_string(l); continue; }

        /* Identifiers / keywords */
        if (is_alpha(c)) { scan_ident(l); continue; }

        /* Multi-char or single-char tokens */
        advance_char(l);
        char c2 = peek_char(l);

        switch (c) {
            /* Arithmetic + compound assignment */
            case '+':
                if (c2 == '=') { advance_char(l); new_tok(l, TOK_PLUS_EQ,    line, col); }
                else            { new_tok(l, TOK_PLUS,    line, col); }
                break;
            case '-':
                if (c2 == '>') { advance_char(l); new_tok(l, TOK_ARROW,      line, col); }
                else if (c2 == '=') { advance_char(l); new_tok(l, TOK_MINUS_EQ, line, col); }
                else            { new_tok(l, TOK_MINUS,   line, col); }
                break;
            case '*':
                if (c2 == '=') { advance_char(l); new_tok(l, TOK_STAR_EQ,    line, col); }
                else            { new_tok(l, TOK_STAR,    line, col); }
                break;
            case '/':
                if (c2 == '=') { advance_char(l); new_tok(l, TOK_SLASH_EQ,   line, col); }
                else            { new_tok(l, TOK_SLASH,   line, col); }
                break;
            case '%':
                if (c2 == '=') { advance_char(l); new_tok(l, TOK_PERCENT_EQ, line, col); }
                else            { new_tok(l, TOK_PERCENT,  line, col); }
                break;

            /* Comparison / equality */
            case '=':
                if (c2 == '=') { advance_char(l); new_tok(l, TOK_EQEQ, line, col); }
                else            { new_tok(l, TOK_EQ,   line, col); }
                break;
            case '!':
                if (c2 == '=') { advance_char(l); new_tok(l, TOK_NEQ,  line, col); }
                else            { new_tok(l, TOK_NOT,  line, col); }
                break;
            case '<':
                if (c2 == '=') { advance_char(l); new_tok(l, TOK_LTE,  line, col); }
                else if (c2 == '<') { advance_char(l); new_tok(l, TOK_SHL, line, col); }
                else            { new_tok(l, TOK_LT,   line, col); }
                break;
            case '>':
                if (c2 == '=') { advance_char(l); new_tok(l, TOK_GTE,  line, col); }
                else if (c2 == '>') { advance_char(l); new_tok(l, TOK_SHR, line, col); }
                else            { new_tok(l, TOK_GT,   line, col); }
                break;

            /* Logical */
            case '&':
                if (c2 == '&') { advance_char(l); new_tok(l, TOK_AND,    line, col); }
                else            { new_tok(l, TOK_BITAND, line, col); }
                break;
            case '|':
                if (c2 == '|') { advance_char(l); new_tok(l, TOK_OR,     line, col); }
                else            { new_tok(l, TOK_BITOR,  line, col); }
                break;
            case '^': new_tok(l, TOK_BITXOR,   line, col); break;
            case '~': new_tok(l, TOK_BITNOT,   line, col); break;

            /* Range .. */
            case '.':
                if (c2 == '.') { advance_char(l); new_tok(l, TOK_DOTDOT, line, col); }
                else            { new_tok(l, TOK_DOT, line, col); }
                break;

            /* Delimiters */
            case '(': new_tok(l, TOK_LPAREN,    line, col); break;
            case ')': new_tok(l, TOK_RPAREN,    line, col); break;
            case '[': new_tok(l, TOK_LBRACKET,  line, col); break;
            case ']': new_tok(l, TOK_RBRACKET,  line, col); break;
            case '{': new_tok(l, TOK_LBRACE,    line, col); break;
            case '}': new_tok(l, TOK_RBRACE,    line, col); break;
            case ':': new_tok(l, TOK_COLON,     line, col); break;
            case ';': new_tok(l, TOK_SEMICOLON, line, col); break;
            case ',': new_tok(l, TOK_COMMA,     line, col); break;

            default: {
                char msg[64];
                msg[0]='u'; msg[1]='n'; msg[2]='k'; msg[3]=' ';
                msg[4]='\''; msg[5]=c; msg[6]='\''; msg[7]='\0';
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
        case TOK_NUMBER:      return "NUM";
        case TOK_STRING:      return "STR";
        case TOK_TRUE:        return "true";
        case TOK_FALSE:       return "false";
        case TOK_PLUS:        return "+";
        case TOK_MINUS:       return "-";
        case TOK_STAR:        return "*";
        case TOK_SLASH:       return "/";
        case TOK_PERCENT:     return "%";
        case TOK_PLUS_EQ:     return "+=";
        case TOK_MINUS_EQ:    return "-=";
        case TOK_STAR_EQ:     return "*=";
        case TOK_SLASH_EQ:    return "/=";
        case TOK_PERCENT_EQ:  return "%=";
        case TOK_EQEQ:        return "==";
        case TOK_NEQ:         return "!=";
        case TOK_LT:          return "<";
        case TOK_GT:          return ">";
        case TOK_LTE:         return "<=";
        case TOK_GTE:         return ">=";
        case TOK_AND:         return "&&";
        case TOK_OR:          return "||";
        case TOK_NOT:         return "!";
        case TOK_BITAND:      return "&";
        case TOK_BITOR:       return "|";
        case TOK_BITXOR:      return "^";
        case TOK_BITNOT:      return "~";
        case TOK_SHL:         return "<<";
        case TOK_SHR:         return ">>";
        case TOK_LPAREN:      return "(";
        case TOK_RPAREN:      return ")";
        case TOK_LBRACKET:    return "[";
        case TOK_RBRACKET:    return "]";
        case TOK_LBRACE:      return "{";
        case TOK_RBRACE:      return "}";
        case TOK_COLON:       return ":";
        case TOK_SEMICOLON:   return ";";
        case TOK_COMMA:       return ",";
        case TOK_EQ:          return "=";
        case TOK_ARROW:       return "->";
        case TOK_DOTDOT:      return "..";
        case TOK_DOT:         return ".";
        case TOK_LET:         return "let";
        case TOK_MUT:         return "mut";
        case TOK_FN:          return "fn";
        case TOK_IF:          return "if";
        case TOK_ELSE:        return "else";
        case TOK_WHILE:       return "while";
        case TOK_FOR:         return "for";
        case TOK_IN:          return "in";
        case TOK_RETURN:      return "return";
        case TOK_BREAK:       return "break";
        case TOK_CONTINUE:    return "continue";
        case TOK_AS:          return "as";
        case TOK_STRUCT:      return "struct";
        case TOK_PRINT:       return "print";
        case TOK_PRINTLN:     return "println";
        case TOK_GUI_WINDOW:  return "gui_window";
        case TOK_GUI_FILL:    return "gui_fill";
        case TOK_GUI_RECT:    return "gui_rect";
        case TOK_GUI_TEXT:    return "gui_text";
        case TOK_GUI_LINE:    return "gui_line";
        case TOK_GUI_CIRCLE:  return "gui_circle";
        case TOK_GUI_FLUSH:   return "gui_flush";
        case TOK_GUI_CLOSE:   return "gui_close";
        case TOK_GUI_WAIT:         return "gui_wait";
        case TOK_GUI_POLL:         return "gui_poll";
        case TOK_IMPORT:           return "import";
        case TOK_FROM:             return "from";
        case TOK_GUI_PEN:          return "gui_pen";
        case TOK_GUI_FILL_RECT:    return "gui_fill_rect";
        case TOK_GUI_FILL_CIRCLE:  return "gui_fill_circle";
        case TOK_GUI_FILL_ROUND:   return "gui_fill_round";
        case TOK_GUI_MOUSE:        return "gui_mouse";
        case TOK_TBUF_OPEN:       return "tbuf_open";
        case TOK_TBUF_CLOSE:      return "tbuf_close";
        case TOK_TBUF_SAVE:       return "tbuf_save";
        case TOK_TBUF_GETLINE:    return "tbuf_getline";
        case TOK_TBUF_INPUT:      return "tbuf_input";
        case TOK_TBUF_LINECOUNT:  return "tbuf_linecount";
        case TOK_TBUF_CURSOR:     return "tbuf_cursor";
        case TOK_TBUF_NUMSTR:      return "tbuf_numstr";
        case TOK_GETARG:          return "getarg";
        case TOK_TBUF_SAVEAS:     return "io_saveas";
        case TOK_SPAWN_TERM:      return "spawn_term";
        case TOK_TYPE_I8:     return "i8";
        case TOK_TYPE_I16:    return "i16";
        case TOK_TYPE_I32:    return "i32";
        case TOK_TYPE_U8:     return "u8";
        case TOK_TYPE_U16:    return "u16";
        case TOK_TYPE_U32:    return "u32";
        case TOK_TYPE_BOOL:   return "bool";
        case TOK_TYPE_STR:    return "str";
        case TOK_TYPE_VOID:   return "void";
        case TOK_IDENT:       return "IDENT";
        case TOK_NEWLINE:     return "NL";
        case TOK_EOF:         return "EOF";
        default:              return "ERR";
    }
}

void lexer_dump(Lexer *l)
{
    char buf[128];
    puts("Tokens:\n  ");
    for (int i = 0; i < l->count; i++) {
        Token *t = &l->tokens[i];
        if (t->type == TOK_NEWLINE) { puts("\n  "); continue; }
        if (t->type == TOK_EOF)     { puts("[EOF]"); break; }
        if (t->type == TOK_NUMBER)  { sprintf(buf, "[%d]", t->int_val); }
        else if (t->type == TOK_STRING) { sprintf(buf, "[\"%s\"]", t->text); }
        else if (t->type == TOK_IDENT)  { sprintf(buf, "[%s]", t->text); }
        else                            { sprintf(buf, "[%s]", token_type_name(t->type)); }
        puts(buf);
        puts(" ");
    }
    puts("\n");
}
