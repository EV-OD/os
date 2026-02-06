#include "string.h"

unsigned int strlen(const char *str)
{
    unsigned int len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

char *strcpy(char *dest, const char *src)
{
    char *p = dest;
    while (*src != '\0') {
        *p++ = *src++;
    }
    *p = '\0';
    return dest;
}

char *strncpy(char *dest, const char *src, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char *strcat(char *dest, const char *src)
{
    char *p = dest;
    while (*p != '\0') {
        p++;
    }
    while (*src != '\0') {
        *p++ = *src++;
    }
    *p = '\0';
    return dest;
}

char *strncat(char *dest, const char *src, unsigned int n)
{
    char *p = dest;
    unsigned int i;
    while (*p != '\0') {
        p++;
    }
    for (i = 0; i < n && src[i] != '\0'; i++) {
        *p++ = src[i];
    }
    *p = '\0';
    return dest;
}

int strcmp(const char *str1, const char *str2)
{
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(unsigned char *)str1 - *(unsigned char *)str2;
}

int strncmp(const char *str1, const char *str2, unsigned int n)
{
    if (n == 0) return 0;
    while (n-- > 1 && *str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(unsigned char *)str1 - *(unsigned char *)str2;
}

void *memset(void *dest, int val, unsigned int n)
{
    unsigned char *ptr = (unsigned char *)dest;
    while (n-- > 0) {
        *ptr++ = (unsigned char)val;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, unsigned int n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dest;
}

int memcmp(const void *ptr1, const void *ptr2, unsigned int n)
{
    const unsigned char *p1 = (const unsigned char *)ptr1;
    const unsigned char *p2 = (const unsigned char *)ptr2;
    while (n-- > 0) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

int atoi(const char *str)
{
    int res = 0;
    int sign = 1;
    int i = 0;

    if (str[0] == '-') {
        sign = -1;
        i++;
    }

    for (; str[i] != '\0'; ++i) {
        if (str[i] < '0' || str[i] > '9') break;
        res = res * 10 + str[i] - '0';
    }

    return sign * res;
}

char *itoa(int value, char *str, int base)
{
    char *rc;
    char *ptr;
    char *low;
    // Check for supported base
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    rc = ptr = str;
    // Set '-' for negative numbers
    if (value < 0 && base == 10) {
        *ptr++ = '-';
    }
    low = ptr;
    // Extract characters
    int tmp_val = value;
    do {
        int digit = tmp_val % base;
        if (digit < 0) digit = -digit;
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[digit];
        tmp_val /= base;
    } while (tmp_val);
    // Terminating the string
    *ptr-- = '\0';
    // Invert the digits
    while (low < ptr) {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
    return rc;
}

/* va_list support using GCC built-ins */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

/* sprintf: Implementation supporting %s, %d, %x, and %c */
int sprintf(char *str, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);

    char *p = str;
    const char *f = format;

    while (*f != '\0') {
        if (*f == '%') {
            f++;
            switch (*f) {
                case 'c': {
                    char c = (char)va_arg(ap, int);
                    *p++ = c;
                    break;
                }
                case 'd': {
                    int d = va_arg(ap, int);
                    char buf[12];
                    itoa(d, buf, 10);
                    char *b = buf;
                    while (*b != '\0') {
                        *p++ = *b++;
                    }
                    break;
                }
                case 'x': {
                    int x = va_arg(ap, int);
                    char buf[12];
                    itoa(x, buf, 16);
                    char *b = buf;
                    while (*b != '\0') {
                        *p++ = *b++;
                    }
                    break;
                }
                case 's': {
                    char *s = va_arg(ap, char *);
                    if (!s) s = "(null)";
                    while (*s != '\0') {
                        *p++ = *s++;
                    }
                    break;
                }
                case '%': {
                    *p++ = '%';
                    break;
                }
                default:
                    *p++ = '%';
                    *p++ = *f;
                    break;
            }
        } else {
            *p++ = *f;
        }
        f++;
    }

    *p = '\0';
    va_end(ap);

    return (int)(p - str);
}
