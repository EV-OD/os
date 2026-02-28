#ifndef STREAM_H
#define STREAM_H

/* =========================================================================
 * stream.h – Kernel byte-stream (pipe / channel)
 *
 * A simple ring-buffer that connects a writer to a reader.  Used to
 * implement per-process stdin/stdout so I/O is not hard-wired to the
 * VGA framebuffer.
 *
 *   Writer side: stream_write(s, buf, n)  – appends bytes
 *   Reader side: stream_read(s, buf, n)   – consumes bytes (non-blocking)
 *                stream_read_blocking()   – waits until data available
 *                stream_getchar()         – single-byte blocking read
 *
 * The stream owns its buffer (allocated at creation time).  It is safe
 * to call from both the writer and reader concurrently because the PIT
 * ISR can preempt between writes/reads, and the ring-buffer indices are
 * only modified by one side each (single-producer / single-consumer).
 *
 * Lifecycle:
 *   stream_create()  → fresh stream
 *   stream_close()   → marks EOF; no more writes, remaining data drainable
 *   stream_destroy() → frees memory
 * ========================================================================= */

/* Ring buffer size (must be power of 2 for masking) */
#define STREAM_BUF_SIZE  1024u
#define STREAM_BUF_MASK  (STREAM_BUF_SIZE - 1u)

typedef struct stream {
    char          buf[STREAM_BUF_SIZE];
    unsigned int  head;        /**< Next read position          */
    unsigned int  tail;        /**< Next write position         */
    volatile unsigned int count; /**< Bytes currently in buffer */
    int           closed;      /**< 1 = writer closed (EOF)     */
} stream_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

/** Allocate and initialise a new stream.  Returns NULL on OOM. */
stream_t *stream_create(void);

/** Mark the stream as closed (EOF).  Remaining data is still readable. */
void stream_close(stream_t *s);

/** Free a stream's memory. */
void stream_destroy(stream_t *s);

/* -------------------------------------------------------------------------
 * Writer side
 * ------------------------------------------------------------------------- */

/**
 * Write up to @count bytes into the stream.
 * @return  Number of bytes actually written (may be < count if buffer full).
 */
int stream_write(stream_t *s, const void *buf, unsigned int count);

/** Write a single character into the stream. */
int stream_putchar(stream_t *s, char c);

/** Write a NUL-terminated string. */
int stream_puts(stream_t *s, const char *str);

/* -------------------------------------------------------------------------
 * Reader side
 * ------------------------------------------------------------------------- */

/**
 * Read up to @count bytes from the stream (non-blocking).
 * @return  Number of bytes read (0 when empty, -1 on closed+empty = EOF).
 */
int stream_read(stream_t *s, void *buf, unsigned int count);

/**
 * Read a single character from the stream (blocking).
 * Spin-waits until a byte is available or the stream is closed.
 * @return  The character (0-255), or -1 on EOF.
 */
int stream_getchar(stream_t *s);

/**
 * Read a line into buf (blocking, handles backspace).
 * Stops at '\n' or when max_len-1 bytes are read.  Always NUL-terminates.
 * @return  Number of characters in the line (not counting NUL).
 */
int stream_readline(stream_t *s, char *buf, unsigned int max_len);

/** Return the number of bytes available for reading. */
unsigned int stream_available(stream_t *s);

/** Return 1 if the stream is closed AND empty (true EOF). */
int stream_eof(stream_t *s);

#endif /* STREAM_H */
