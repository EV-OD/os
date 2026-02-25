#ifndef COMPILER_CODEGEN_H
#define COMPILER_CODEGEN_H

/* -----------------------------------------------------------------------
 * codegen.h  --  x86-32 code generator for the rosc compiler (Phase 1).
 *
 * Phase 1 strategy: constant folding
 * -----------------------------------
 * Because Phase 1 only supports integer literals and previously-defined
 * let-bindings (no I/O or mutable state), every expression can be fully
 * evaluated at compile time.  The generated flat binary is therefore
 * minimal:
 *
 *   mov  eax, <value_of_last_binding>   ; B8 imm32
 *   ret                                 ; C3
 *
 * The compiled function has the C signature  int fn(void)  and returns
 * its value in EAX per the cdecl convention.
 *
 * The codegen also builds a binding table (name → compile-time i32) that
 * the rosc shell uses to display each let-binding's result without having
 * to actually execute the generated code per-binding.
 *
 * Execution
 * ---------
 * codegen_execute() casts the code buffer to a function pointer and
 * calls it directly.  This works in ring 0 on x86 protected mode without
 * NX enforcement (which RandomOS does not yet enable).
 * --------------------------------------------------------------------- */

#include "ast.h"

/* ------------------------------------------------------------------ */
/*  Codegen state                                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    /* Compile-time binding table  (name → value) */
    char bind_name[MAX_BINDINGS][MAX_IDENT_LEN];
    i32  bind_val [MAX_BINDINGS];
    int  bind_count;

    /* Generated x86 machine code */
    u8  code[MAX_CODE_BYTES];
    int code_len;

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

/* Cast the code buffer to a function and execute it; returns EAX. */
i32  codegen_execute(Codegen *cg);

#endif /* COMPILER_CODEGEN_H */
