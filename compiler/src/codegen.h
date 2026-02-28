#ifndef COMPILER_CODEGEN_H
#define COMPILER_CODEGEN_H

/* -----------------------------------------------------------------------
 * codegen.h  --  x86-32 code generator for the rosc compiler.
 *
 * Generates a flat binary suitable for execution as a user-mode (ring 3)
 * process inside RandomOS.  The binary uses int 0x80 syscalls for I/O
 * and process exit.
 *
 * Phase 1 strategy: constant folding + print results
 * ---------------------------------------------------
 * Because Phase 1 only supports integer literals and previously-defined
 * let-bindings (no mutable state), every expression is fully evaluated
 * at compile time.  The generated binary prints each binding's result
 * via SYS_WRITE and then calls SYS_EXIT(0).
 *
 * Binary layout (loaded at USER_CODE_BASE):
 *
 *   [code section]    mov/int 0x80 instructions for each binding + exit
 *   [data section]    embedded "name = value\n" strings
 *
 * Syscall ABI:
 *   EAX = syscall number
 *   EBX = arg1 (fd for write, exit code for exit)
 *   ECX = arg2 (buffer pointer for write)
 *   EDX = arg3 (length for write)
 *   int 0x80
 * --------------------------------------------------------------------- */

#include "ast.h"

/* Base virtual address where user code is loaded by the OS. */
#define USER_CODE_BASE  0x08048000u

/* Syscall numbers (must match kernel syscall.h). */
#define ROSC_SYS_EXIT   0
#define ROSC_SYS_WRITE  1

/* ------------------------------------------------------------------ */
/*  Codegen state                                                       */
/* ------------------------------------------------------------------ */

/* Embedded string entry for the data section. */
typedef struct {
    char str[80];      /* the string content (e.g. "x = 42\n") */
    int  len;          /* string length in bytes                */
} CodegenString;

typedef struct {
    /* Compile-time binding table  (name -> value) */
    char bind_name[MAX_BINDINGS][MAX_IDENT_LEN];
    i32  bind_val [MAX_BINDINGS];
    int  bind_count;

    /* Embedded string data */
    CodegenString strings[MAX_BINDINGS];
    int string_count;

    /* Generated x86 machine code (code section) */
    u8  code[MAX_CODE_BYTES];
    int code_len;

    /* Total binary = code + data */
    u8  binary[MAX_CODE_BYTES * 2];
    int binary_len;

    int had_error;
} Codegen;

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

/* Zero-initialise the Codegen struct. */
void codegen_init(Codegen *cg);

/* Walk the AST, constant-fold all expressions, and emit the binary.
 * Returns 0 on success, -1 on error. */
int  codegen_run(Codegen *cg, AstNode *prog);

/* Hex-dump the generated machine code bytes to the display. */
void codegen_dump_code(Codegen *cg);

/* Print the compile-time binding table (name = value pairs). */
void codegen_dump_bindings(Codegen *cg);

/* Execute the generated binary in-place (ring 0, for diagnostics only). */
i32  codegen_execute(Codegen *cg);

#endif /* COMPILER_CODEGEN_H */
