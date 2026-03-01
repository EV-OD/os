/* =========================================================================
 * textbuf.c – Kernel-side line-oriented text buffer for rxt editor
 * ========================================================================= */
#include "textbuf.h"
#include "vfs.h"
#include "string.h"
#include "log.h"

/* Virtual key codes (must match keyboard.c) */
#define KEY_UP    200
#define KEY_DOWN  201
#define KEY_LEFT  202
#define KEY_RIGHT 203
#define KEY_HOME  204
#define KEY_END   205
#define KEY_BS    8
#define KEY_ENTER 13
#define KEY_DEL   127

/* -------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */
typedef struct {
    char lines[TBUF_MAX_LINES][TBUF_LINE_LEN];
    int  line_count;
    int  cur_line;
    int  cur_col;
    char filename[256];
    int  in_use;
} textbuf_t;

/* All buffers live in BSS – no heap allocation needed.
 * 4 × (512 × 256 + 256 + 12) ≈ 512 KB total. */
static textbuf_t bufs[TBUF_HANDLES];

/* Static scratch buffer for tbuf_numstr */
static char numstr_buf[32];

/* -------------------------------------------------------------------------
 * helpers
 * ------------------------------------------------------------------------- */
static int valid(int h)
{
    return (h >= 0 && h < TBUF_HANDLES && bufs[h].in_use);
}

/* Clamp col to the length of the current line. */
static void clamp_col(textbuf_t *b)
{
    int len = (int)strlen(b->lines[b->cur_line]);
    if (b->cur_col > len) b->cur_col = len;
    if (b->cur_col < 0)   b->cur_col = 0;
}

/* -------------------------------------------------------------------------
 * tbuf_open
 * ------------------------------------------------------------------------- */
int tbuf_open(const char *path)
{
    int h;
    for (h = 0; h < TBUF_HANDLES; h++)
        if (!bufs[h].in_use) break;
    if (h == TBUF_HANDLES) return -1;

    textbuf_t *b = &bufs[h];
    memset(b, 0, sizeof(*b));
    /* Normalise to absolute path: "foo.txt" → "/foo.txt" */
    if (path && path[0] != '/') {
        b->filename[0] = '/';
        strncpy(b->filename + 1, path, 254);
        b->filename[255] = '\0';
    } else {
        strncpy(b->filename, (path && path[0]) ? path : "/untitled", 255);
        b->filename[255] = '\0';
    }
    b->in_use     = 1;
    b->line_count = 1;   /* always at least one (empty) line */
    b->cur_line   = 0;
    b->cur_col    = 0;

    /* Try to load existing file content – use the normalised filename */
    int fd = vfs_open(b->filename, VFS_O_RDONLY);
    if (fd >= 0) {
        char raw[TBUF_MAX_LINES * TBUF_LINE_LEN];
        int  total = 0, n;
        while ((n = vfs_read(fd, raw + total,
                             (int)sizeof(raw) - total - 1)) > 0)
            total += n;
        vfs_close(fd);
        raw[total] = '\0';

        /* Split into lines */
        int li = 0, ci = 0;
        for (int i = 0; i < total && li < TBUF_MAX_LINES; i++) {
            char c = raw[i];
            if (c == '\r') continue;
            if (c == '\n') {
                b->lines[li][ci] = '\0';
                li++;
                ci = 0;
                if (li >= TBUF_MAX_LINES) break;
            } else {
                if (ci < TBUF_LINE_LEN - 1)
                    b->lines[li][ci++] = c;
            }
        }
        b->lines[li][ci] = '\0';
        b->line_count = (li == 0 && ci == 0) ? 1 : li + 1;
        if (b->line_count < 1) b->line_count = 1;
        log_info("[tbuf] loaded '%s' (%d lines)", path, b->line_count);
    } else {
        log_info("[tbuf] new file '%s'", path);
    }

    return h;
}

/* -------------------------------------------------------------------------
 * tbuf_close
 * ------------------------------------------------------------------------- */
int tbuf_close(int h)
{
    if (!valid(h)) return -1;
    bufs[h].in_use = 0;
    return 0;
}

/* -------------------------------------------------------------------------
 * tbuf_save
 * ------------------------------------------------------------------------- */
int tbuf_save(int h)
{
    if (!valid(h)) return -1;
    textbuf_t *b = &bufs[h];

    int fd = vfs_open(b->filename, VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        log_error("[tbuf] cannot open '%s' for writing", b->filename);
        return -1;
    }
    for (int i = 0; i < b->line_count; i++) {
        vfs_write(fd, b->lines[i], strlen(b->lines[i]));
        vfs_write(fd, "\n", 1);
    }
    vfs_close(fd);
    log_info("[tbuf] saved '%s' (%d lines)", b->filename, b->line_count);
    return 0;
}

/* -------------------------------------------------------------------------
 * tbuf_getline
 * ------------------------------------------------------------------------- */
const char *tbuf_getline(int h, int n)
{
    static const char empty[] = "";
    if (!valid(h)) return empty;
    textbuf_t *b = &bufs[h];
    if (n < 0 || n >= b->line_count) return empty;
    return b->lines[n];
}

/* -------------------------------------------------------------------------
 * tbuf_input – process one keypress
 * ------------------------------------------------------------------------- */
int tbuf_input(int h, int key)
{
    if (!valid(h)) return -1;
    textbuf_t *b = &bufs[h];

    int line = b->cur_line;
    int col  = b->cur_col;
    int len  = (int)strlen(b->lines[line]);

    /* ---- Movement ---- */
    if (key == KEY_UP) {
        if (b->cur_line > 0) {
            b->cur_line--;
            clamp_col(b);
        }
        return 0;
    }
    if (key == KEY_DOWN) {
        if (b->cur_line < b->line_count - 1) {
            b->cur_line++;
            clamp_col(b);
        }
        return 0;
    }
    if (key == KEY_LEFT) {
        if (b->cur_col > 0) {
            b->cur_col--;
        } else if (b->cur_line > 0) {
            b->cur_line--;
            b->cur_col = (int)strlen(b->lines[b->cur_line]);
        }
        return 0;
    }
    if (key == KEY_RIGHT) {
        if (b->cur_col < len) {
            b->cur_col++;
        } else if (b->cur_line < b->line_count - 1) {
            b->cur_line++;
            b->cur_col = 0;
        }
        return 0;
    }
    if (key == KEY_HOME) { b->cur_col = 0; return 0; }
    if (key == KEY_END)  { b->cur_col = len; return 0; }

    /* ---- Backspace ---- */
    if (key == KEY_BS) {
        if (col > 0) {
            /* Remove char before cursor */
            char *ln = b->lines[line];
            memmove(ln + col - 1, ln + col, (unsigned int)(len - col + 1));
            b->cur_col--;
        } else if (line > 0) {
            /* Merge this line onto the previous */
            int prev_len = (int)strlen(b->lines[line - 1]);
            if (prev_len + len < TBUF_LINE_LEN - 1) {
                strncat(b->lines[line - 1], b->lines[line],
                        (unsigned int)(TBUF_LINE_LEN - 1 - prev_len));
            }
            /* Shift lines up */
            for (int i = line; i < b->line_count - 1; i++)
                memcpy(b->lines[i], b->lines[i + 1], TBUF_LINE_LEN);
            b->lines[b->line_count - 1][0] = '\0';
            b->line_count--;
            b->cur_line--;
            b->cur_col = prev_len;
        }
        return 0;
    }

    /* ---- Enter: split line ---- */
    if (key == KEY_ENTER || key == '\n') {
        if (b->line_count >= TBUF_MAX_LINES) return 0;
        /* Shift lines down to make room */
        for (int i = b->line_count; i > line + 1; i--)
            memcpy(b->lines[i], b->lines[i - 1], TBUF_LINE_LEN);
        /* Copy tail of current line to new line */
        strncpy(b->lines[line + 1], b->lines[line] + col,
                (unsigned int)(TBUF_LINE_LEN - 1));
        b->lines[line + 1][TBUF_LINE_LEN - 1] = '\0';
        /* Truncate current line at cursor */
        b->lines[line][col] = '\0';
        b->line_count++;
        b->cur_line++;
        b->cur_col = 0;
        return 0;
    }

    /* ---- Printable ASCII (32-126) and common extended chars ---- */
    if (key >= 32 && key <= 126) {
        if (len < TBUF_LINE_LEN - 1) {
            char *ln = b->lines[line];
            memmove(ln + col + 1, ln + col, (unsigned int)(len - col + 1));
            ln[col] = (char)key;
            b->cur_col++;
        }
        return 0;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * tbuf_linecount
 * ------------------------------------------------------------------------- */
int tbuf_linecount(int h)
{
    if (!valid(h)) return 0;
    return bufs[h].line_count;
}

/* -------------------------------------------------------------------------
 * tbuf_cursor  – packed (cur_line | cur_col << 16)
 * ------------------------------------------------------------------------- */
int tbuf_cursor(int h)
{
    if (!valid(h)) return 0;
    textbuf_t *b = &bufs[h];
    return (b->cur_line & 0xFFFF) | ((b->cur_col & 0xFFFF) << 16);
}

/* -------------------------------------------------------------------------
 * tbuf_numstr  – int → decimal string (static single-call buffer)
 * ------------------------------------------------------------------------- */
const char *tbuf_numstr(int h, int n)
{
    (void)h;
    /* Simple itoa into numstr_buf */
    int neg = 0;
    int pos = 30;
    numstr_buf[31] = '\0';
    if (n == 0) { numstr_buf[0] = '0'; numstr_buf[1] = '\0'; return numstr_buf; }
    if (n < 0)  { neg = 1; n = -n; }
    while (n > 0 && pos >= 0) {
        numstr_buf[pos--] = (char)('0' + n % 10);
        n /= 10;
    }
    if (neg && pos >= 0) numstr_buf[pos--] = '-';
    return &numstr_buf[pos + 1];
}
