#ifndef COMPILER_LEXER_H
#define COMPILER_LEXER_H

/* -----------------------------------------------------------------------
 * lexer.h  --  Full tokeniser for the RandomOS compiler.
 *
 * Input : a NUL-terminated source string.
 * Output: a flat array of Token values stored inside the Lexer struct
 *         (static – no heap allocation).
 *
 * Supported token set:
 *   Literals  : integer numbers, string literals, true/false
 *   Operators : + - * / % == != < > <= >= && || ! & | ^ ~ << >>
 *               = += -= *= /= %= -> .. ,
 *   Delimiters: ( ) [ ] { } : ;
 *   Keywords  : let mut fn if else while for in return break continue
 *               as struct
 *   Types     : i8 i16 i32 u8 u16 u32 bool str void
 *   GUI builtins: gui_window gui_fill gui_rect gui_text gui_line
 *                 gui_circle gui_flush gui_close gui_wait gui_poll
 *   Built-in functions: print println input len
 *   Other     : identifiers, end-of-file
 * --------------------------------------------------------------------- */

#include "common.h"

/* ------------------------------------------------------------------ */
/*  Token types                                                         */
/* ------------------------------------------------------------------ */
typedef enum {
    /* Literals */
    TOK_NUMBER,       /* integer literal, e.g. 42, 0xFF                  */
    TOK_STRING,       /* "..." string literal                             */
    TOK_TRUE,         /* true                                             */
    TOK_FALSE,        /* false                                            */

    /* Arithmetic operators */
    TOK_PLUS,         /* +                                                */
    TOK_MINUS,        /* -                                                */
    TOK_STAR,         /* *                                                */
    TOK_SLASH,        /* /                                                */
    TOK_PERCENT,      /* %                                                */

    /* Compound assignment */
    TOK_PLUS_EQ,      /* +=                                               */
    TOK_MINUS_EQ,     /* -=                                               */
    TOK_STAR_EQ,      /* *=                                               */
    TOK_SLASH_EQ,     /* /=                                               */
    TOK_PERCENT_EQ,   /* %=                                               */

    /* Comparison operators */
    TOK_EQEQ,         /* ==                                               */
    TOK_NEQ,          /* !=                                               */
    TOK_LT,           /* <                                                */
    TOK_GT,           /* >                                                */
    TOK_LTE,          /* <=                                               */
    TOK_GTE,          /* >=                                               */

    /* Logical operators */
    TOK_AND,          /* &&                                               */
    TOK_OR,           /* ||                                               */
    TOK_NOT,          /* !                                                */

    /* Bitwise operators */
    TOK_BITAND,       /* &                                                */
    TOK_BITOR,        /* |                                                */
    TOK_BITXOR,       /* ^                                                */
    TOK_BITNOT,       /* ~                                                */
    TOK_SHL,          /* <<                                               */
    TOK_SHR,          /* >>                                               */

    /* Punctuation */
    TOK_LPAREN,       /* (                                                */
    TOK_RPAREN,       /* )                                                */
    TOK_LBRACKET,     /* [                                                */
    TOK_RBRACKET,     /* ]                                                */
    TOK_LBRACE,       /* {                                                */
    TOK_RBRACE,       /* }                                                */
    TOK_COLON,        /* :                                                */
    TOK_SEMICOLON,    /* ;                                                */
    TOK_COMMA,        /* ,                                                */
    TOK_EQ,           /* =                                                */
    TOK_ARROW,        /* ->                                               */
    TOK_DOTDOT,       /* ..  (range)                                      */
    TOK_DOT,          /* .   (member access)                              */

    /* Keywords */
    TOK_LET,          /* let                                              */
    TOK_MUT,          /* mut                                              */
    TOK_FN,           /* fn                                               */
    TOK_IF,           /* if                                               */
    TOK_ELSE,         /* else                                             */
    TOK_WHILE,        /* while                                            */
    TOK_FOR,          /* for                                              */
    TOK_IN,           /* in                                               */
    TOK_RETURN,       /* return                                           */
    TOK_BREAK,        /* break                                            */
    TOK_CONTINUE,     /* continue                                         */
    TOK_AS,           /* as  (type cast)                                  */
    TOK_STRUCT,       /* struct                                           */

    /* Built-in functions (treated as keywords) */
    TOK_PRINT,        /* print                                            */
    TOK_PRINTLN,      /* println                                          */

    /* GUI builtins */
    TOK_GUI_WINDOW,   /* gui_window                                       */
    TOK_GUI_FILL,     /* gui_fill                                         */
    TOK_GUI_RECT,     /* gui_rect                                         */
    TOK_GUI_TEXT,     /* gui_text                                         */
    TOK_GUI_LINE,     /* gui_line                                         */
    TOK_GUI_CIRCLE,   /* gui_circle                                       */
    TOK_GUI_FLUSH,    /* gui_flush                                        */
    TOK_GUI_CLOSE,    /* gui_close                                        */
    TOK_GUI_WAIT,     /* gui_wait                                         */
    TOK_GUI_POLL,     /* gui_poll                                         */

    /* Types */
    TOK_TYPE_I8,      /* i8                                               */
    TOK_TYPE_I16,     /* i16                                              */
    TOK_TYPE_I32,     /* i32                                              */
    TOK_TYPE_U8,      /* u8                                               */
    TOK_TYPE_U16,     /* u16                                              */
    TOK_TYPE_U32,     /* u32                                              */
    TOK_TYPE_BOOL,    /* bool                                             */
    TOK_TYPE_STR,     /* str                                              */
    TOK_TYPE_VOID,    /* void                                             */

    /* Generic identifier */
    TOK_IDENT,        /* user-defined identifier                          */

    /* Structural */
    TOK_NEWLINE,      /* \n  (kept to aid parser recovery)                */
    TOK_EOF,          /* end of source                                    */
    TOK_ERROR         /* unrecognised character                           */
} TokenType;

/* ------------------------------------------------------------------ */
/*  Token                                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    TokenType type;
    i32  int_val;                 /* valid when type == TOK_NUMBER             */
    char text[MAX_IDENT_LEN];     /* identifier text or string literal content */
    int  str_len;                 /* length of string content (TOK_STRING)     */
    int  line;                    /* 1-based source line                       */
    int  col;                     /* 1-based source column                     */
} Token;

/* ------------------------------------------------------------------ */
/*  Lexer state                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *src;              /* source string (not owned)                  */
    int  pos;                     /* current read position in src               */
    int  line;                    /* current line (1-based)                     */
    int  col;                     /* current column (1-based)                   */
    Token tokens[MAX_TOKENS];     /* output token array                         */
    int  count;                   /* number of tokens produced                  */
    int  had_error;               /* non-zero if lexer hit an error             */
} Lexer;

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

/* Initialise lexer with the source string.  Must be called first. */
void lexer_init(Lexer *l, const char *src);

/* Tokenise the entire source.  Returns 0 on success, -1 on error. */
int  lexer_tokenize(Lexer *l);

/* Human-readable name of a token type (for diagnostics / dumps). */
const char *token_type_name(TokenType t);

/* Print all tokens to the display (--emit-tokens). */
void lexer_dump(Lexer *l);

#endif /* COMPILER_LEXER_H */
