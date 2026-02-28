/* =========================================================================
 * stream.c – Kernel byte-stream implementation
 *
 * Simple SPSC (single-producer / single-consumer) ring buffer.
 * See stream.h for the full API description.
 * ========================================================================= */

#include "stream.h"
#include "kheap.h"
#include "string.h"

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */
stream_t *stream_create(void)
{
    stream_t *s = (stream_t *)kmalloc(sizeof(stream_t));
    if (!s) return (stream_t *)0;

    memset(s->buf, 0, STREAM_BUF_SIZE);
    s->head   = 0;
    s->tail   = 0;
    s->count  = 0;
    s->closed = 0;
    return s;
}

void stream_close(stream_t *s)
{
    if (s) s->closed = 1;
}

void stream_destroy(stream_t *s)
{
    if (s) kfree(s);
}

/* -------------------------------------------------------------------------
 * Writer side
 * ------------------------------------------------------------------------- */
int stream_write(stream_t *s, const void *buf, unsigned int count)
{
    if (!s || s->closed) return -1;

    const char *src = (const char *)buf;
    unsigned int written = 0;

    while (written < count) {
        if (s->count >= STREAM_BUF_SIZE) {
            /* Buffer full – return partial write */
            break;
        }
        s->buf[s->tail] = src[written];
        s->tail = (s->tail + 1) & STREAM_BUF_MASK;
        s->count++;
        written++;
    }

    return (int)written;
}

int stream_putchar(stream_t *s, char c)
{
    return stream_write(s, &c, 1);
}

int stream_puts(stream_t *s, const char *str)
{
    if (!str) return 0;
    unsigned int len = 0;
    const char *p = str;
    while (*p) { len++; p++; }
    return stream_write(s, str, len);
}

/* -------------------------------------------------------------------------
 * Reader side
 * ------------------------------------------------------------------------- */
int stream_read(stream_t *s, void *buf, unsigned int count)
{
    if (!s) return -1;

    if (s->count == 0) {
        return (s->closed) ? -1 : 0;
    }

    char *dst = (char *)buf;
    unsigned int nread = 0;

    while (nread < count && s->count > 0) {
        dst[nread] = s->buf[s->head];
        s->head = (s->head + 1) & STREAM_BUF_MASK;
        s->count--;
        nread++;
    }

    return (int)nread;
}

int stream_getchar(stream_t *s)
{
    if (!s) return -1;

    /* Spin-wait until data is available or EOF */
    while (s->count == 0) {
        if (s->closed) return -1;
        /* Enable interrupts so the PIT/keyboard can fire, then sleep.
         * Without sti the CPU would spin with IRQs masked and freeze. */
        __asm__ volatile("sti; hlt");
    }

    char c = s->buf[s->head];
    s->head = (s->head + 1) & STREAM_BUF_MASK;
    s->count--;
    return (unsigned char)c;
}

int stream_readline(stream_t *s, char *buf, unsigned int max_len)
{
    if (!s || max_len == 0) return 0;

    unsigned int count = 0;
    while (1) {
        int ch = stream_getchar(s);
        if (ch < 0) break;            /* EOF */
        if (ch == '\r') continue;      /* ignore CR */
        if (ch == '\n') break;         /* end of line */

        /* Backspace handling */
        if (ch == 8 || ch == '\b') {
            if (count > 0) count--;
            continue;
        }

        if (count + 1 < max_len) {
            buf[count++] = (char)ch;
        }
    }

    buf[count] = '\0';
    return (int)count;
}

unsigned int stream_available(stream_t *s)
{
    return s ? s->count : 0;
}

int stream_eof(stream_t *s)
{
    return (s && s->closed && s->count == 0) ? 1 : 0;
}
