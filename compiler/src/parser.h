#ifndef COMPILER_PARSER_H
#define COMPILER_PARSER_H

/* -----------------------------------------------------------------------
 * parser.h  --  Recursive-descent parser for Phase 1 of rosc.
 *
 * Grammar (Phase 1):
 *
 *   program     ::= { newline } { stmt { newline } }
 *   stmt        ::= let_stmt
 *   let_stmt    ::= "let" IDENT ":" type_name "=" expr
 *   type_name   ::= "i32" | "u32" | "bool"
 *   expr        ::= additive
 *   additive    ::= multiplicative { ("+" | "-") multiplicative }
 *   multiplicative ::= primary { ("*" | "/") primary }
 *   primary     ::= NUMBER | IDENT | "(" expr ")"
 * --------------------------------------------------------------------- */

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer *lexer;     /* token source                               */
    int    cur;       /* index of current token in lexer->tokens    */
    int    had_error; /* non-zero after first parser error          */
} Parser;

/* Initialise parser against an already-tokenised Lexer. */
void    parser_init(Parser *p, Lexer *l);

/* Parse the full token stream; returns the root AstNode (AST_PROGRAM)
 * or NULL if a fatal error occurred. */
AstNode *parser_parse(Parser *p);

#endif /* COMPILER_PARSER_H */
