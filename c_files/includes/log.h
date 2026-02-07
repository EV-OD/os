#ifndef INCLUDE_LOG_H
#define INCLUDE_LOG_H


/* Log output devices */
#define LOG_FB      0       /* Framebuffer */
#define LOG_SERIAL  1       /* Serial port (COM1) */
#define LOG_ALL     2       /* Both framebuffer and serial */


/* Log severity levels */
#define LOG_LEVEL_DEBUG     0
#define LOG_LEVEL_INFO      1
#define LOG_LEVEL_WARNING   2
#define LOG_LEVEL_ERROR     3


/** log_init:
 *  Initializes the logging system. Sets up the serial port if needed.
 *
 *  @param device  The output device (LOG_FB, LOG_SERIAL, LOG_ALL)
 */
void log_init(int device);


/** log_set_device:
 *  Changes the current log output device.
 *
 *  @param device  The output device (LOG_FB, LOG_SERIAL, LOG_ALL)
 */
void log_set_device(int device);


/** log_putchar:
 *  Writes a single character to the current log device(s).
 *
 *  @param c  The character to write
 */
void log_putchar(char c);


/** log_puts:
 *  Writes a null-terminated string to the current log device(s).
 *
 *  @param buf  The null-terminated string
 */
void log_puts(char *buf);


/** log_printf:
 *  A printf-like function that formats a string and writes it to the
 *  current log device(s). Supports %d, %x, %s, %c, %%.
 *
 *  @param format  The format string
 *  @param ...     The arguments to format
 *  @return        The number of characters written
 */
int log_printf(char *format, ...);


/** log_debug:
 *  Logs a message at DEBUG level.
 *
 *  @param format  The format string
 *  @param ...     The arguments to format
 */
void log_debug(char *format, ...);


/** log_info:
 *  Logs a message at INFO level.
 *
 *  @param format  The format string
 *  @param ...     The arguments to format
 */
void log_info(char *format, ...);


/** log_warning:
 *  Logs a message at WARNING level.
 *
 *  @param format  The format string
 *  @param ...     The arguments to format
 */
void log_warning(char *format, ...);


/** log_error:
 *  Logs a message at ERROR level.
 *
 *  @param format  The format string
 *  @param ...     The arguments to format
 */
void log_error(char *format, ...);


#endif /* INCLUDE_LOG_H */
