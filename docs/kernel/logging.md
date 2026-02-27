# Logging System

This document describes the kernel logging subsystem. The implementation lives in `c_files/src/log.c` with the API in `c_files/includes/log.h`.

## Overview

The logging system provides structured, leveled output that can be directed to the serial port, the framebuffer, or both. It is used throughout the kernel for diagnostics, status messages, and error reporting.

## Log Devices

Output can be routed to one or both devices:

| Constant | Value | Target |
|----------|-------|--------|
| `LOG_FB` | 0 | Framebuffer (VGA text console) |
| `LOG_SERIAL` | 1 | COM1 serial port |
| `LOG_ALL` | 2 | Both framebuffer and serial |

The default device is `LOG_SERIAL`. This can be changed at runtime with `log_set_device()`.

## Log Levels

Four severity levels are defined:

| Constant | Value | Prefix |
|----------|-------|--------|
| `LOG_LEVEL_DEBUG` | 0 | `[DEBUG]   ` |
| `LOG_LEVEL_INFO` | 1 | `[INFO]    ` |
| `LOG_LEVEL_WARNING` | 2 | `[WARNING] ` |
| `LOG_LEVEL_ERROR` | 3 | `[ERROR]   ` |

The level constants are defined for future filtering support. Currently all levels are always emitted.

## API

### Initialization

```c
void log_init(int device);       // Set the output device
void log_set_device(int device); // Change device at runtime
```

### Low-Level Output

```c
void log_putchar(char c);       // Write one character to the active device(s)
void log_puts(char *buf);       // Write a string to the active device(s)
int  log_printf(char *fmt, ...);// printf-style formatted output
```

### Leveled Logging

Each function prepends a level tag and appends a newline:

```c
void log_debug(char *fmt, ...);   // [DEBUG]   <message>
void log_info(char *fmt, ...);    // [INFO]    <message>
void log_warning(char *fmt, ...); // [WARNING] <message>
void log_error(char *fmt, ...);   // [ERROR]   <message>
```

## Format Specifiers

`log_printf` and the leveled functions support:

| Specifier | Type | Description |
|-----------|------|-------------|
| `%c` | char | Single character |
| `%d` | int | Signed decimal integer |
| `%x` | int | Hexadecimal integer (lowercase) |
| `%s` | char* | String (prints `(null)` for NULL pointers) |
| `%%` | — | Literal percent sign |

## Usage Pattern

```c
log_init(LOG_ALL);                      // Send to both serial and framebuffer
log_info("[paging] CR3=0x%x", cr3);     // Prefixed with [INFO]
log_error("[module] load failed: %s", reason);
```

## Serial Monitor Integration

The serial-monitor web app in `serial-monitor/` parses the `[LEVEL]` prefixes to display color-coded, filterable kernel logs in real time. The bracketed subsystem tags like `[paging]` appear as subsystem badges in the UI.

## Implementation Notes

- Uses GCC's `__builtin_va_list` for variadic arguments (no libc dependency).
- `log_vprintf` is the internal va_list-based formatter shared by all public functions.
- The leveled helpers (`log_debug`, etc.) call `log_with_level` which prepends the tag, calls `log_vprintf`, and appends a newline.
