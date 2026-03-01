/* =========================================================================
 * textbuf.h – Kernel-side line-oriented text buffer for rxt editor
 *
 * Manages up to TBUF_HANDLES concurrent text buffers.  Each buffer holds
 * up to TBUF_MAX_LINES lines of TBUF_LINE_LEN characters.  The cursor
 * is tracked as (cur_line, cur_col).
 *
 * Exposed to user processes via syscalls 21-29 (SYS_TBUF_*).
 * ========================================================================= */
#ifndef TEXTBUF_H
#define TEXTBUF_H

#define TBUF_HANDLES    4
#define TBUF_MAX_LINES  512
#define TBUF_LINE_LEN   256

/* -------------------------------------------------------------------------
 * tbuf_open – open a file into a text buffer.
 *
 * If the file exists its content is loaded.  If not, an empty buffer is
 * created and the file will be created on tbuf_save().
 *
 * @param path  Absolute VFS path.
 * @return  Handle 0-3 on success, -1 if all handles are busy or OOM.
 * ------------------------------------------------------------------------- */
int tbuf_open(const char *path);

/* tbuf_close – release the buffer handle (does NOT save). */
int tbuf_close(int h);

/* tbuf_save – write the buffer content back to the original file. */
int tbuf_save(int h);

/* tbuf_getline – return a pointer to the NUL-terminated text of line n.
 * Returns pointer to a static empty string if h or n is out of range. */
const char *tbuf_getline(int h, int n);

/* tbuf_input – process one keypress and update cursor / content.
 *   Printable chars: insert at cursor.
 *   8  = backspace.
 *   13 = enter (split line or append new line).
 *   200-205 = arrow/Home/End (move cursor). */
int tbuf_input(int h, int key);

/* tbuf_linecount – current number of lines in buffer h. */
int tbuf_linecount(int h);

/* tbuf_cursor – packed cursor: (cur_line) | (cur_col << 16). */
int tbuf_cursor(int h);

/* tbuf_numstr – convert integer n to a decimal string.
 * Returns pointer to a static single-call buffer (NOT re-entrant). */
const char *tbuf_numstr(int h, int n);

#endif /* TEXTBUF_H */
