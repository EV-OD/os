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

/* Source buffer – max combined (preprocessed) source length. */
static char g_src_buf[MAX_SRC_LEN];

/* -----------------------------------------------------------------------
 * Import preprocessor
 *
 * Syntax supported:
 *   import <name>                  → /lib/ros/<name>.ros
 *   import <name> from <path>      → <path>  (absolute or relative to src)
 *
 * The preprocessor strips import lines and prepends the library source so
 * the rest of the pipeline sees one unified token stream.
 * --------------------------------------------------------------------- */
static char   g_pp_out[MAX_SRC_LEN];        /* combined output source         */
static char   g_pp_lib_buf[MAX_IMPORT_BUF]; /* temp: one imported file        */
static char   g_pp_imported[MAX_IMPORTS][MAX_IMPORT_PATH]; /* dedup list      */
static int    g_pp_import_count;

static int pp_already_imported(const char *path)
{
    int i;
    for (i = 0; i < g_pp_import_count; i++)
        if (strcmp(g_pp_imported[i], path) == 0) return 1;
    return 0;
}

static void pp_mark_imported(const char *path)
{
    if (g_pp_import_count < MAX_IMPORTS)
        strncpy(g_pp_imported[g_pp_import_count++], path, MAX_IMPORT_PATH - 1);
}

/* Load file into buf, return bytes read or -1 on error. */
static int pp_load_file(const char *path, char *buf, int buf_max)
{
    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return -1;
    int n = vfs_read(fd, buf, buf_max - 1);
    vfs_close(fd);
    if (n < 0) n = 0;
    buf[n] = '\0';
    return n;
}

/*
 * Resolve a relative import path against the directory of src_file.
 * e.g. src_file="/home/test.ros", rel="./utils.ros" → "/home/utils.ros"
 * Writes into out_path (max out_max bytes).
 */
static void pp_resolve_path(const char *src_file, const char *rel, char *out_path, int out_max)
{
    if (rel[0] == '/' || rel[0] == '\0') {
        /* Already absolute */
        strncpy(out_path, rel, out_max - 1);
        out_path[out_max - 1] = '\0';
        return;
    }
    /* Find directory part of src_file */
    int len = strlen(src_file);
    int slash = 0, i;
    for (i = 0; i < len; i++)
        if (src_file[i] == '/') slash = i + 1;
    /* Copy dir prefix */
    if (slash > 0 && slash < out_max - 1) {
        strncpy(out_path, src_file, slash);
        out_path[slash] = '\0';
    } else {
        out_path[0] = '\0';
    }
    /* Strip leading ./ from rel */
    const char *r = rel;
    if (r[0] == '.' && r[1] == '/') r += 2;
    strncat(out_path, r, out_max - strlen(out_path) - 1);
}

/*
 * Main preprocessing pass.
 *
 * - Scans src for 'import' directives at the start of a line.
 * - Loads referenced library files and prepends them.
 * - Strips import lines from the main source.
 * - Returns pointer to g_pp_out containing the combined source.
 */
static const char *preprocess_imports(const char *src, const char *src_file)
{
    /* Static buffers for prepend accumulation */
    static char prepend[MAX_SRC_LEN / 2];  /* accumulated library source */
    int prepend_len = 0;
    int strip_len   = 0;
    static char stripped[MAX_SRC_LEN / 2]; /* main src with import lines removed */

    g_pp_import_count = 0;
    prepend[0] = '\0';
    stripped[0] = '\0';

    const char *p = src;
    while (*p) {
        /* Detect 'import ...' at the start of a line (skip leading spaces) */
        const char *line_start = p;
        while (*p == ' ' || *p == '\t') p++;

        if (strncmp(p, "import ", 7) == 0) {
            /* Parse: import <name> [from <path>] */
            const char *q = p + 7;
            while (*q == ' ') q++;

            /* Read module name */
            char name[64];
            int ni = 0;
            while (*q && *q != ' ' && *q != '\n' && *q != '\r' && ni < 63)
                name[ni++] = *q++;
            name[ni] = '\0';

            /* Skip to end-of-line to consume the import directive */
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;

            if (ni == 0) continue; /* empty import name */

            /* Determine library path */
            char lib_path[MAX_IMPORT_PATH];
            while (*q == ' ') q++;
            if (strncmp(q, "from ", 5) == 0) {
                q += 5;
                while (*q == ' ') q++;
                int pi = 0;
                while (*q && *q != '\n' && *q != '\r' && pi < MAX_IMPORT_PATH - 1)
                    lib_path[pi++] = *q++;
                lib_path[pi] = '\0';
                /* Trim trailing whitespace */
                while (pi > 0 && (lib_path[pi-1] == ' ' || lib_path[pi-1] == '\t'))
                    lib_path[--pi] = '\0';
                /* Resolve relative path */
                char resolved[MAX_IMPORT_PATH];
                pp_resolve_path(src_file, lib_path, resolved, MAX_IMPORT_PATH);
                strncpy(lib_path, resolved, MAX_IMPORT_PATH - 1);
            } else {
                /* Standard lib: /lib/ros/<name>.ros */
                sprintf(lib_path, "/lib/ros/%s.ros", name);
            }

            if (pp_already_imported(lib_path)) continue;
            pp_mark_imported(lib_path);

            int n = pp_load_file(lib_path, g_pp_lib_buf, MAX_IMPORT_BUF);
            if (n > 0) {
                if (prepend_len + n + 2 < (int)sizeof(prepend)) {
                    memcpy(prepend + prepend_len, g_pp_lib_buf, n);
                    prepend_len += n;
                    prepend[prepend_len++] = '\n';
                    prepend[prepend_len]   = '\0';
                }
            } else {
                /* File not found – emit a warning comment */
                char warn[128];
                sprintf(warn, "// WARNING: import '%s' not found\n", lib_path);
                int wl = strlen(warn);
                if (strip_len + wl < (int)sizeof(stripped) - 1) {
                    memcpy(stripped + strip_len, warn, wl);
                    strip_len += wl;
                    stripped[strip_len] = '\0';
                }
            }
        } else {
            /* Normal line: copy verbatim from line_start to end-of-line */
            p = line_start;
            while (*p && *p != '\n') {
                if (strip_len < (int)sizeof(stripped) - 2)
                    stripped[strip_len++] = *p;
                p++;
            }
            if (*p == '\n') {
                if (strip_len < (int)sizeof(stripped) - 1)
                    stripped[strip_len++] = '\n';
                p++;
            }
            stripped[strip_len] = '\0';
        }
    }

    /* Combine: prepend + stripped */
    int total = prepend_len + strip_len;
    if (total >= MAX_SRC_LEN - 1) total = MAX_SRC_LEN - 2;
    memcpy(g_pp_out, prepend, prepend_len);
    memcpy(g_pp_out + prepend_len, stripped, strip_len);
    g_pp_out[prepend_len + strip_len] = '\0';

    if (prepend_len > 0)
        log_info("[rosc] preprocessor: merged %d bytes of library source", prepend_len);

    return g_pp_out;
}

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

    /* --- Stage 0: Preprocess imports ---------------------------------- */
    const char *pp_src = preprocess_imports(g_src_buf, src_path);

    /* --- Stage 1: Lex -------------------------------------------------- */
    error_init();
    lexer_init(&g_lexer, pp_src);

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
