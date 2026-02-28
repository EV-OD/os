#include "rosc.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "codegen.h"
#include "error.h"
#include "stdio.h"
#include "string.h"
#include "vfs.h"
#include "rox.h"
#include "log.h"

/* -----------------------------------------------------------------------
 * Static stage instances (no heap)
 * --------------------------------------------------------------------- */
static Lexer   g_lexer;
static Parser  g_parser;
static Codegen g_codegen;

/* Source buffer – max source file size. */
static char g_src_buf[MAX_SRC_LEN];

/* -----------------------------------------------------------------------
 * Helper: derive .rox output path from .ros source path
 *
 * Replaces the last ".ros" with ".rox", or appends ".rox" if no .ros
 * extension.  Writes into out_buf (must be >= 256 bytes).
 * --------------------------------------------------------------------- */
static void derive_output_path(const char *src_path, char *out_buf, int buf_size)
{
    int len = strlen(src_path);
    strncpy(out_buf, src_path, buf_size - 1);
    out_buf[buf_size - 1] = '\0';

    /* Check if it ends with ".ros" */
    if (len >= 4 && strcmp(src_path + len - 4, ".ros") == 0) {
        /* Replace .ros → .rox */
        out_buf[len - 1] = 'x';
    } else {
        /* Append ".rox" */
        strncat(out_buf, ".rox", buf_size - strlen(out_buf) - 1);
    }
}

/* -----------------------------------------------------------------------
 * Helper: extract the program name from a path for the .rox header.
 * Uses the filename without path and without extension.
 * --------------------------------------------------------------------- */
static void extract_prog_name(const char *path, char *name, int name_size)
{
    /* Find last '/' */
    const char *slash = path;
    const char *p = path;
    while (*p) {
        if (*p == '/') slash = p + 1;
        p++;
    }
    if (*slash == '\0') slash = path;

    /* Copy up to the last '.' or end */
    int i = 0;
    p = slash;
    while (*p && *p != '.' && i < name_size - 1) {
        name[i++] = *p++;
    }
    name[i] = '\0';
}

/* -----------------------------------------------------------------------
 * rosc_compile – compile a .ros source file to a .rox executable.
 * --------------------------------------------------------------------- */
int rosc_compile(const char *src_path, const char *out_path, int force)
{
    char output_path[256];
    char prog_name[12];
    int  n;

    log_info("[rosc] compiling %s", src_path);

    /* --- Determine output path ----------------------------------------- */
    if (out_path && out_path[0] != '\0') {
        strncpy(output_path, out_path, sizeof(output_path) - 1);
        output_path[sizeof(output_path) - 1] = '\0';
    } else {
        derive_output_path(src_path, output_path, sizeof(output_path));
    }

    /* --- Read source file ---------------------------------------------- */
    int fd = vfs_open(src_path, VFS_O_RDONLY);
    if (fd < 0) {
        puts_color("rosc: ", COLOR_LIGHT_RED);
        puts("cannot open '");
        puts((char *)src_path);
        puts("'\n");
        return -1;
    }

    memset(g_src_buf, 0, sizeof(g_src_buf));
    n = vfs_read(fd, g_src_buf, MAX_SRC_LEN - 1);
    vfs_close(fd);

    if (n <= 0) {
        puts_color("rosc: ", COLOR_LIGHT_RED);
        puts("empty or unreadable source file\n");
        return -1;
    }
    g_src_buf[n] = '\0';

    /* --- Stage 1: Lex -------------------------------------------------- */
    error_init();
    lexer_init(&g_lexer, g_src_buf);

    if (lexer_tokenize(&g_lexer) != 0 || error_count() > 0) {
        puts_color("[FAIL] ", COLOR_LIGHT_RED);
        puts("lexer errors -- compilation aborted.\n");
        return -2;
    }

    /* --- Stage 2: Parse ------------------------------------------------ */
    ast_init();
    parser_init(&g_parser, &g_lexer);
    AstNode *prog = parser_parse(&g_parser);

    if (!prog || g_parser.had_error || error_count() > 0) {
        puts_color("[FAIL] ", COLOR_LIGHT_RED);
        puts("parser errors -- compilation aborted.\n");
        return -3;
    }

    /* --- Stage 3: Code generation -------------------------------------- */
    codegen_init(&g_codegen);
    codegen_prescan(&g_codegen, prog);

    if (codegen_run(&g_codegen, prog) != 0 || g_codegen.had_error) {
        puts_color("[FAIL] ", COLOR_LIGHT_RED);
        puts("codegen errors -- compilation aborted.\n");
        return -4;
    }

    /* --- Stage 4: Build .rox file (header + code) ---------------------- */
    extract_prog_name(src_path, prog_name, sizeof(prog_name));

    rox_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic        = ROX_MAGIC;
    hdr.version      = ROX_VERSION;
    hdr.entry_offset = (unsigned int)g_codegen.entry_offset;
    hdr.code_size    = (unsigned int)g_codegen.binary_len;
    hdr.flags        = 0;
    strncpy(hdr.name, prog_name, 11);
    hdr.name[11] = '\0';

    /* --- Write .rox file ----------------------------------------------- */
    /* Refuse to silently overwrite an existing .rox */
    if (!force) {
        vfs_stat_t _st;
        if (vfs_stat(output_path, &_st) == 0) {
            puts_color("rosc: output file already exists: ", COLOR_LIGHT_BROWN);
            puts(output_path);
            puts("\n  use 'rosc -f " );
            puts(output_path);
            puts("' to force overwrite\n");
            return -5;
        }
    }

    fd = vfs_open(output_path, VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        puts_color("rosc: ", COLOR_LIGHT_RED);
        puts("cannot create output file '");
        puts(output_path);
        puts("'\n");
        return -5;
    }

    /* Write header */
    vfs_write(fd, &hdr, sizeof(hdr));

    /* Write code+data binary */
    vfs_write(fd, g_codegen.binary, g_codegen.binary_len);

    vfs_close(fd);

    /* --- Success message ----------------------------------------------- */
    {
        char buf[256];
        sprintf(buf, "  compiled: %s -> %s (%d bytes code+data)\n",
                src_path, output_path, g_codegen.binary_len);
        puts_color(buf, COLOR_LIGHT_GREEN);
    }

    /* dump code / stats if requested */
    // codegen_dump_code(&g_codegen);

    log_info("[rosc] wrote %s (%d + %d bytes)",
             output_path, (int)sizeof(hdr), g_codegen.binary_len);

    return 0;
}

/* -----------------------------------------------------------------------
 * Hardcoded test program (legacy Phase 1 demo)
 * --------------------------------------------------------------------- */
static const char *TEST_SOURCE =
    "// Phase 1 test program\n"
    "let a: i32 = 10 + 5 * 2\n"
    "let b: i32 = a - 4\n"
    "let c: i32 = (a + b) / 4\n"
    "let d: i32 = c * c - 1\n";

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

static void pause_and_clear(void)
{
    puts("\n[Press any key for next stage...]\n");
    getchar();
    fb_clear();
    cursor_move_home();
}

/* -----------------------------------------------------------------------
 * rosc_run – interactive demo mode (legacy).
 * --------------------------------------------------------------------- */
void rosc_run(void)
{
    banner();

    section("Source");
    puts((char *)TEST_SOURCE);
    pause_and_clear();

    section("Tokens");
    error_init();
    lexer_init(&g_lexer, TEST_SOURCE);

    if (lexer_tokenize(&g_lexer) != 0 || error_count() > 0) {
        puts("[FAIL] Lexer errors -- aborting compilation.\n");
        return;
    }
    lexer_dump(&g_lexer);
    pause_and_clear();

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

    section("Code generation");
    codegen_init(&g_codegen);
    codegen_prescan(&g_codegen, prog);

    if (codegen_run(&g_codegen, prog) != 0 || g_codegen.had_error) {
        puts("[FAIL] Codegen errors -- aborting compilation.\n");
        return;
    }
    codegen_dump_code(&g_codegen);
    puts("\n");
    

    puts("\n[OK] Phase 1 compilation complete.\n");
}
