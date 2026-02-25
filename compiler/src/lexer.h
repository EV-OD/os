#ifndef COMPILER_LEXER_H
#define COMPILER_LEXER_H

/* -----------------------------------------------------------------------
 * lexer.h  --  Tokeniser for the RandomOS compiler (Phase 1).
 *
 * Input : a NUL-terminated source string.
 * Output: a flat array of Token values stored inside the Lexer struct
 *         (static – no heap allocation).
 *
 * Phase 1 token set:
 *   Literals  : integer numbers
 *   Operators : + - * /
 *   Delimiters: ( ) : =
 *   Keywords  : let
 *   Types     : i32  u32  bool
 *   Other     : identifiers, newlines, end-of-file
 * --------------------------------------------------------------------- */

#include "common.h"

/* ------------------------------------------------------------------ */
/*  Token types                                                         */
/* ------------------------------------------------------------------ */
typedef enum {
    TOK_NUMBER,     /* integer literal, e.g. 42          */
    TOK_PLUS,       /* +                                  */
    TOK_MINUS,      /* -                                  */
    TOK_STAR,       /* *                                  */
    TOK_SLASH,      /* /                                  */
    TOK_LPAREN,     /* (                                  */
    TOK_RPAREN,     /* )                                  */
    TOK_LET,        /* let                                */
    TOK_COLON,      /* :                                  */
    TOK_EQ,         /* =                                  */
    TOK_IDENT,      /* user-defined identifier            */
    TOK_TYPE_I32,   /* i32                                */
    TOK_TYPE_U32,   /* u32                                */
    TOK_TYPE_BOOL,  /* bool                               */
    TOK_NEWLINE,    /* \n  (kept to aid parser recovery)  */
    TOK_EOF,        /* end of source                      */
    TOK_ERROR       /* unrecognised character             */
} TokenType;

/* ------------------------------------------------------------------ */
/*  Token                                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    TokenType type;
    i32  int_val;               /* valid when type == TOK_NUMBER            */
    char text[MAX_IDENT_LEN];   /* valid for TOK_IDENT and TOK_TYPE_*       */
    int  line;                  /* 1-based source line                      */
    int  col;                   /* 1-based source column                    */
} Token;

/* ------------------------------------------------------------------ */
/*  Lexer state                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *src;            /* source string (not owned)                */
    int  pos;                   /* current read position in src             */
    int  line;                  /* current line (1-based)                   */
    int  col;                   /* current column (1-based)                 */
    Token tokens[MAX_TOKENS];   /* output token array                       */
    int  count;                 /* number of tokens produced                */
    int  had_error;             /* non-zero if lexer hit an error           */
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
