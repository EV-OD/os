#ifndef COMPILER_PARSER_H
#define COMPILER_PARSER_H

/* -----------------------------------------------------------------------
 * parser.h  --  Recursive-descent parser for the full rosc language.
 *
 * Grammar (simplified EBNF):
 *
 *   program      ::= top_item*
 *   top_item     ::= fn_def | stmt
 *
 *   fn_def       ::= "fn" IDENT "(" param_list ")" [ "->" type ] block
 *   param_list   ::= [ param { "," param } ]
 *   param        ::= IDENT ":" type
 *
 *   block        ::= "{" stmt* "}"
 *
 *   stmt         ::= let_stmt
 *                  | mut_stmt
 *                  | assign_stmt
 *                  | if_stmt
 *                  | while_stmt
 *                  | for_stmt
 *                  | return_stmt
 *                  | break_stmt
 *                  | continue_stmt
 *                  | print_stmt
 *                  | expr_stmt
 *
 *   let_stmt     ::= "let" IDENT ":" type "=" expr stmt_end
 *   mut_stmt     ::= "mut" IDENT ":" type "=" expr stmt_end
 *   assign_stmt  ::= IDENT ( "=" | "+=" | "-=" | "*=" | "/=" | "%=" ) expr stmt_end
 *   if_stmt      ::= "if" expr block [ "else" ( if_stmt | block ) ]
 *   while_stmt   ::= "while" expr block
 *   for_stmt     ::= "for" IDENT ":" type "in" expr block
 *   return_stmt  ::= "return" [ expr ] stmt_end
 *   break_stmt   ::= "break" stmt_end
 *   continue_stmt::= "continue" stmt_end
 *   print_stmt   ::= ("print"|"println") "(" expr ")" stmt_end
 *   expr_stmt    ::= call_expr stmt_end
 *   stmt_end     ::= newline | ";" | eof
 *
 *   type         ::= "i8"|"i16"|"i32"|"u8"|"u16"|"u32"|"bool"|"str"|"void"
 *
 *   expr         ::= logical_or
 *   logical_or   ::= logical_and { "||" logical_and }
 *   logical_and  ::= comparison  { "&&" comparison  }
 *   comparison   ::= bitwise_or  { ("=="|"!="|"<"|">"|"<="|">=") bitwise_or }
 *   bitwise_or   ::= bitwise_xor { "|" bitwise_xor }
 *   bitwise_xor  ::= bitwise_and { "^" bitwise_and }
 *   bitwise_and  ::= shift       { "&" shift       }
 *   shift        ::= additive    { ("<<"|">>") additive }
 *   additive     ::= multiplicative { ("+"|"-") multiplicative }
 *   multiplicative ::= unary     { ("*"|"/"|"%") unary       }
 *   unary        ::= ("!"|"-"|"~") unary | postfix
 *   postfix      ::= primary { "[" expr "]" | "as" type }
 *   primary      ::= NUMBER | STRING | "true" | "false"
 *                  | IDENT [ "(" arg_list ")" ]
 *                  | gui_call
 *                  | "(" expr ")"
 *   arg_list     ::= [ expr { "," expr } ]
 *   gui_call     ::= GUI_KEYWORD "(" arg_list ")"
 * --------------------------------------------------------------------- */

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer  *lexer;
    int     cur;
    int     had_error;
    int     loop_depth;   /* for break/continue validation */
} Parser;

/** Initialise parser against an already-tokenised Lexer. */
void     parser_init(Parser *p, Lexer *l);

/**
 * Parse the full token stream.
 * Returns the root AstNode (AST_PROGRAM) or NULL on fatal error.
 */
AstNode *parser_parse(Parser *p);

#endif /* COMPILER_PARSER_H */
