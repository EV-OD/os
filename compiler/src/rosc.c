#include "rosc.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "codegen.h"
#include "error.h"
#include "stdio.h"
#include "string.h"

/* -----------------------------------------------------------------------
 * Hardcoded test program
 *
 * Phase 1 has no file system.  The source to compile is embedded here as
 * a C string constant.  When the file system is ready this will be
 * replaced by a file-read call.
 *
 * Language subset (Phase 1):
 *   - let bindings   :  let <name>: i32 = <expr>
 *   - arithmetic     :  +  -  *  /  with full operator precedence
 *   - parentheses    :  ( expr )
 *   - variable refs  :  earlier bindings may appear in later expressions
 *   - line comments  :  // ...
 * --------------------------------------------------------------------- */
static const char *TEST_SOURCE =
    "// Phase 1 test program\n"
    "let a: i32 = 10 + 5 * 2\n"     /* a = 20 */
    "let b: i32 = a - 4\n"           /* b = 16 */
    "let c: i32 = (a + b) / 4\n"    /* c =  9 */
    "let d: i32 = c * c - 1\n";      /* d = 80 */

/* -----------------------------------------------------------------------
 * Static stage instances (no heap)
 * --------------------------------------------------------------------- */
static Lexer   g_lexer;
static Parser  g_parser;
static Codegen g_codegen;

/* -----------------------------------------------------------------------
 * Banner / separator helpers
 * --------------------------------------------------------------------- */
static void banner(void)
{
    puts("\n");
    puts("============================================\n");
    puts("  rosc  --  RandomOS Compiler  [Phase 1]\n");
    puts("============================================\n");
}

static void section(char *title)
{
    puts("\n--- ");
    puts(title);
    puts(" ---\n");
}

/* Wait for a keypress, then clear the screen for the next stage. */
static void pause_and_clear(void)
{
    puts("\n[Press any key for next stage...]\n");
    getchar();
    fb_clear();
    cursor_move_home();
}

/* -----------------------------------------------------------------------
 * rosc_run  --  compile and execute the hardcoded test program.
 * --------------------------------------------------------------------- */
void rosc_run(void)
{
    banner();

    /* ----------------------------------------------------------------
     * Print source
     * -------------------------------------------------------------- */
    section("Source");
    /* Cast: OS puts() takes char*, TEST_SOURCE is const char* */
    puts((char *)TEST_SOURCE);
    pause_and_clear();

    /* ----------------------------------------------------------------
     * Stage 1: Lex
     * -------------------------------------------------------------- */
    section("Tokens");
    error_init();
    lexer_init(&g_lexer, TEST_SOURCE);

    if (lexer_tokenize(&g_lexer) != 0 || error_count() > 0) {
        puts("[FAIL] Lexer errors -- aborting compilation.\n");
        return;
    }
    lexer_dump(&g_lexer);
    pause_and_clear();

    /* ----------------------------------------------------------------
     * Stage 2: Parse
     * -------------------------------------------------------------- */
    section("AST");
    ast_init();
    parser_init(&g_parser, &g_lexer);
    AstNode *prog = parser_parse(&g_parser);

    if (!prog || g_parser.had_error || error_count() > 0) {
        puts("[FAIL] Parser errors -- aborting compilation.\n");
        return;
    }
    ast_dump(prog, 0);
    pause_and_clear();

    /* ----------------------------------------------------------------
     * Stage 3: Code generation  (constant folding + x86-32 emit)
     * -------------------------------------------------------------- */
    section("Code generation");
    codegen_init(&g_codegen);

    if (codegen_run(&g_codegen, prog) != 0 || g_codegen.had_error) {
        puts("[FAIL] Codegen errors -- aborting compilation.\n");
        return;
    }
    codegen_dump_code(&g_codegen);
    puts("\n");
    codegen_dump_bindings(&g_codegen);
    pause_and_clear();

    /* ----------------------------------------------------------------
     * Stage 4: Execute the generated flat binary in-place
     * -------------------------------------------------------------- */
    section("Execution");
    i32 ret = codegen_execute(&g_codegen);
    char buf[64];
    sprintf(buf, "Compiled function returned: %d\n", ret);
    puts(buf);

    puts("\n[OK] Phase 1 compilation complete.\n");
}
