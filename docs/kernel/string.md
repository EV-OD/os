# String and Memory Utilities

This document covers the freestanding string and memory functions implemented by the kernel. The code is in `c_files/src/string.c` with declarations in `c_files/includes/string.h`.

## Overview

Since the kernel runs without the C standard library (`-nostdlib -nostdinc`), commonly-used string and memory functions must be provided manually. These implementations follow standard C semantics where possible.

## String Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `strlen` | `unsigned int strlen(const char *str)` | Returns the length of a NUL-terminated string (not counting the terminator). |
| `strcpy` | `char *strcpy(char *dest, const char *src)` | Copies `src` to `dest` including the NUL terminator. Returns `dest`. |
| `strncpy` | `char *strncpy(char *dest, const char *src, unsigned int n)` | Copies up to `n` characters from `src` to `dest`. Pads with NUL bytes if `src` is shorter than `n`. |
| `strcat` | `char *strcat(char *dest, const char *src)` | Appends `src` to the end of `dest`. Returns `dest`. |
| `strncat` | `char *strncat(char *dest, const char *src, unsigned int n)` | Appends at most `n` characters from `src` to `dest` and NUL-terminates. |
| `strcmp` | `int strcmp(const char *str1, const char *str2)` | Compares two strings lexicographically. Returns 0 if equal, negative if str1 < str2, positive otherwise. |
| `strncmp` | `int strncmp(const char *str1, const char *str2, unsigned int n)` | Like `strcmp` but compares at most `n` characters. |

## Memory Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `memset` | `void *memset(void *dest, int val, unsigned int n)` | Fills `n` bytes of `dest` with the byte value `val`. |
| `memcpy` | `void *memcpy(void *dest, const void *src, unsigned int n)` | Copies `n` bytes from `src` to `dest`. Regions must not overlap. |
| `memcmp` | `int memcmp(const void *ptr1, const void *ptr2, unsigned int n)` | Compares `n` bytes. Returns 0 if identical. |

## Conversion Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `atoi` | `int atoi(const char *str)` | Converts a decimal string to an integer. Supports a leading `-` sign. Stops at the first non-digit character. |
| `itoa` | `char *itoa(int value, char *str, int base)` | Converts an integer to a string in the given base (2–36). Handles negative values for base 10. Digits above 9 use lowercase letters. |

## Formatted Output

| Function | Signature | Description |
|----------|-----------|-------------|
| `sprintf` | `int sprintf(char *str, const char *format, ...)` | Writes formatted output to a string buffer. Returns the number of characters written (excluding NUL). |

### sprintf Format Specifiers

| Specifier | Argument | Output |
|-----------|----------|--------|
| `%c` | `int` (promoted char) | Single character |
| `%d` | `int` | Signed decimal |
| `%x` | `int` | Hexadecimal (lowercase, no `0x` prefix) |
| `%s` | `char*` | String (prints `(null)` for NULL) |
| `%%` | — | Literal `%` |

## Notes

- All functions use `unsigned int` for sizes instead of `size_t` since no standard headers are available.
- `sprintf` uses GCC's `__builtin_va_list` for variadic arguments.
- `itoa` reverses the digit string in-place after generation to produce the correct order.
