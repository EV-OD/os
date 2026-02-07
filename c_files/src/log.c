#include "log.h"
#include "stdio.h"
#include "serial.h"
#include "string.h"

/* va_list support using GCC built-ins */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#define va_copy(dest, src) __builtin_va_copy(dest, src)


static int log_device = LOG_SERIAL;


void log_init(int device)
{
    log_set_device(device);
    if (device == LOG_SERIAL || device == LOG_ALL) {
        serial_begin(9600);
    }
}


void log_set_device(int device)
{
    log_device = device;
}


void log_putchar(char c)
{
    if (log_device == LOG_FB || log_device == LOG_ALL) {
        putchar(c);
    }
    if (log_device == LOG_SERIAL || log_device == LOG_ALL) {
        serial_write_char(c);
    }
}


void log_puts(char *buf)
{
    while (*buf) {
        log_putchar(*buf++);
    }
}


/** log_vprintf:
 *  Internal variadic printf that writes formatted output to log device(s).
 */
static int log_vprintf(char *format, va_list ap)
{
    int count = 0;

    while (*format != '\0') {
        if (*format == '%') {
            format++;
            switch (*format) {
                case 'c': {
                    char c = (char)va_arg(ap, int);
                    log_putchar(c);
                    count++;
                    break;
                }
                case 'd': {
                    int d = va_arg(ap, int);
                    char buf[12];
                    itoa(d, buf, 10);
                    log_puts(buf);
                    count += strlen(buf);
                    break;
                }
                case 'x': {
                    int x = va_arg(ap, int);
                    char buf[12];
                    itoa(x, buf, 16);
                    log_puts(buf);
                    count += strlen(buf);
                    break;
                }
                case 's': {
                    char *s = va_arg(ap, char *);
                    if (!s) s = "(null)";
                    log_puts(s);
                    count += strlen(s);
                    break;
                }
                case '%': {
                    log_putchar('%');
                    count++;
                    break;
                }
                default:
                    log_putchar('%');
                    log_putchar(*format);
                    count += 2;
                    break;
            }
        } else {
            log_putchar(*format);
            count++;
        }
        format++;
    }

    return count;
}


int log_printf(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int count = log_vprintf(format, ap);
    va_end(ap);
    return count;
}


static void log_with_level(char *prefix, char *format, va_list ap)
{
    log_puts(prefix);
    log_vprintf(format, ap);
    log_putchar('\n');
}


void log_debug(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    log_with_level("[DEBUG]   ", format, ap);
    va_end(ap);
}


void log_info(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    log_with_level("[INFO]    ", format, ap);
    va_end(ap);
}


void log_warning(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    log_with_level("[WARNING] ", format, ap);
    va_end(ap);
}


void log_error(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    log_with_level("[ERROR]   ", format, ap);
    va_end(ap);
}
