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
 * Compiler limits  (Phase 2+ – full language)
 * --------------------------------------------------------------------- */
#define MAX_TOKENS        4096   /* max tokens per source unit                  */
#define MAX_AST_NODES     2048   /* max AST nodes per source unit               */
#define MAX_CODE_BYTES    65536  /* max bytes in the generated flat binary       */
#define MAX_DATA_BYTES    32768  /* max bytes for embedded string / data section */
#define MAX_BINDINGS      256    /* max variables / bindings per scope           */
#define MAX_IDENT_LEN     48     /* max identifier / type-name length            */
#define MAX_SRC_LEN       65536  /* max combined (preprocessed) source length    */

/* Import system */
#define MAX_IMPORTS       16     /* max import statements per compilation unit  */
#define MAX_IMPORT_PATH   256    /* max file path length for an import          */
#define MAX_IMPORT_BUF    16384  /* max source bytes per imported library file  */
#define MAX_FUNCTIONS     64     /* max user-defined function definitions        */
#define MAX_PARAMS        8      /* max parameters per function                  */
#define MAX_LOCALS        64     /* max local variables per function             */
#define MAX_STRINGS       128    /* max distinct string literals per program     */
#define MAX_STR_LEN       256    /* max characters in one string literal         */
#define MAX_FIXUPS        512    /* max jump-fixup entries                       */
#define MAX_BREAK_STACK   16     /* max nesting depth of break-able loops        */
#define MAX_STMTS         256    /* max statements in one block                  */
#define MAX_ARGS          8      /* max arguments in a function call             */

/* -----------------------------------------------------------------------
 * Helper macros
 * --------------------------------------------------------------------- */
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))

#endif /* COMPILER_COMMON_H */
