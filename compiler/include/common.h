#ifndef COMPILER_COMMON_H
#define COMPILER_COMMON_H

/* -----------------------------------------------------------------------
 * common.h  --  Shared types, constants, and macros for the rosc compiler.
 *
 * NOTE: This file must use ONLY types available in the RandomOS environment.
 *       No libc, no host-OS headers.  Everything here is self-contained.
 * --------------------------------------------------------------------- */

/* Basic integer types (mirrors what the kernel already uses) */
typedef unsigned char   u8;
typedef unsigned short  u16;
typedef unsigned int    u32;
typedef signed char     i8;
typedef signed short    i16;
typedef signed int      i32;

/* Boolean */
#ifndef NULL
#define NULL ((void *)0)
#endif
#define true  1
#define false 0
typedef int bool;

/* -----------------------------------------------------------------------
 * Compiler limits – all allocation is static (no heap in Phase 1).
 * --------------------------------------------------------------------- */
#define MAX_TOKENS      512   /* max tokens per source unit              */
#define MAX_AST_NODES   256   /* max AST nodes per source unit           */
#define MAX_CODE_BYTES  4096  /* max bytes in the generated flat binary  */
#define MAX_BINDINGS    64    /* max let-bindings per program            */
#define MAX_IDENT_LEN   32    /* max identifier / type-name length       */
#define MAX_SRC_LEN     4096  /* max source string length                */

#endif /* COMPILER_COMMON_H */
