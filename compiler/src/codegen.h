#ifndef COMPILER_CODEGEN_H
#define COMPILER_CODEGEN_H

/* -----------------------------------------------------------------------
 * codegen.h  --  Full x86-32 code generator for the rosc compiler.
 *
 * Generates a flat binary for user-mode (ring 3) execution inside
 * RandomOS.  Uses int 0x80 syscalls for all I/O, process control,
 * and GUI operations.
 *
 * Binary layout:
 *
 *   [__ros_print_int: 103 bytes]     runtime helper – always first
 *   [main code]                      top-level statements
 *   [user function bodies]           in definition order
 *   [data section]                   string literals
 *
 * Execution model:
 *   - Entry point  = start of main code (after the print_int helper)
 *   - Local vars   = EBP-based stack slots  [EBP - slot*4]
 *   - Params       = EBP-based, positive    [EBP + (n+2)*4]
 *   - All values   = 32-bit  (i8/u8/i16/u16 zero/sign-extended to 32)
 *   - Bool         = 0 or 1 in EAX
 *
 * Expression evaluation convention:
 *   Each emit_expr() call leaves its result in EAX.
 *   To compute a binary op the left operand is PUSHed, right evaluated
 *   into EAX, then left is POPped into EBX.
 *
 * Jump patching:
 *   Forward jumps emit a 4-byte placeholder (0) at the offset field and
 *   record a Fixup entry.  When the label is later resolved the fixup is
 *   patched in-place.
 *
 * GUI syscalls (SYS_GUI_*) are emitted as plain int 0x80 calls with
 * args on the stack (cdecl-style push / pop).
 * --------------------------------------------------------------------- */

#include "ast.h"

/* Base virtual address where the OS loads user code. */
#define USER_CODE_BASE  0x08048000u

/* -----------------------------------------------------------------------
 * Syscall numbers  (must match kernel syscall.h)
 * --------------------------------------------------------------------- */
#define ROSC_SYS_EXIT       0
#define ROSC_SYS_WRITE      1
#define ROSC_SYS_READ       2
#define ROSC_SYS_GETPID     3
#define ROSC_SYS_YIELD      4
#define ROSC_SYS_SBRK       5
/* GUI syscalls */
#define ROSC_SYS_GUI_OPEN   6
#define ROSC_SYS_GUI_CLOSE  7
#define ROSC_SYS_GUI_FILL   8
#define ROSC_SYS_GUI_TEXT   9
#define ROSC_SYS_GUI_LINE   10
#define ROSC_SYS_GUI_RECT   11
#define ROSC_SYS_GUI_CIRCLE 12
#define ROSC_SYS_GUI_FLUSH  13
#define ROSC_SYS_GUI_POLL   14
#define ROSC_SYS_GUI_WAIT   15

/* -----------------------------------------------------------------------
 * Variable scope entry
 * --------------------------------------------------------------------- */
typedef struct {
    char    name[MAX_IDENT_LEN];
    RosType type;
    int     slot;       /* local:  EBP-offset = -(slot*4)         */
                        /* param:  EBP-offset = +(slot*4) (>=2)   */
    int     is_param;
} VarEntry;

/* -----------------------------------------------------------------------
 * Function table entry
 * --------------------------------------------------------------------- */
typedef struct {
    char    name[MAX_IDENT_LEN];
    int     code_offset;    /* byte offset in code[] where fn starts */
    int     param_count;
    RosType ret_type;
    int     is_defined;
} FuncEntry;

/* -----------------------------------------------------------------------
 * String literal entry  (data section)
 * --------------------------------------------------------------------- */
typedef struct {
    char bytes[MAX_STR_LEN];
    int  len;
    int  data_offset;   /* byte offset from start of data section   */
} StrEntry;

/* -----------------------------------------------------------------------
 * Fixup entry  (patches an unresolved 32-bit field in code[])
 * --------------------------------------------------------------------- */
typedef enum {
    FIX_ABS_STR,    /* Patch with: USER_CODE_BASE + code_size + str.data_offset   */
    FIX_REL_FUNC,   /* Patch with: func.code_offset - (patch_pos + 4)  (relative) */
    FIX_REL_LABEL,  /* Patch with: target_pos       - (patch_pos + 4)  (relative) */
} FixupKind;

typedef struct {
    FixupKind kind;
    int  patch_pos;     /* position in code[] of the 32-bit field to patch */
    int  idx;           /* StrEntry / FuncEntry index for STR/FUNC fixups  */
    int  target_pos;    /* for LABEL fixups: resolved target                */
    int  resolved;
} Fixup;

/* -----------------------------------------------------------------------
 * Break / continue target stack (one entry per loop nesting level)
 * --------------------------------------------------------------------- */
typedef struct {
    int  break_fixup_start;     /* first Fixup index belonging to this loop  */
    int  continue_target;       /* code offset of continue target (loop top) */
    /* break fixups are added from break_fixup_start onward */
} LoopFrame;

/* -----------------------------------------------------------------------
 * Main codegen state
 * --------------------------------------------------------------------- */
typedef struct {
    /* --- Emitted machine code ---------------------------------------- */
    u8  code[MAX_CODE_BYTES];
    int code_len;

    /* --- Data section (string literals) ------------------------------ */
    u8  data[MAX_DATA_BYTES];
    int data_len;

    /* --- Final assembled binary ------------------------------------- */
    u8  binary[MAX_CODE_BYTES + MAX_DATA_BYTES];
    int binary_len;

    /* --- String table ----------------------------------------------- */
    StrEntry strings[MAX_STRINGS];
    int      string_count;

    /* --- Function table --------------------------------------------- */
    FuncEntry funcs[MAX_FUNCTIONS];
    int       func_count;

    /* --- Current scope (locals + params) ----------------------------- */
    VarEntry locals[MAX_LOCALS];
    int      local_count;
    int      next_slot;     /* 1-based, grows up for each new local      */

    /* --- Stack frame size patch location ----------------------------- */
    int  frame_size_patch_pos; /* offset in code[] of the SUB ESP imm32  */
    int  frame_size_imm_pos;   /* offset in code[] of the imm32 itself    */

    /* --- Fixup table ------------------------------------------------ */
    Fixup fixups[MAX_FIXUPS];
    int   fixup_count;

    /* --- Loop break/continue targets -------------------------------- */
    LoopFrame loop_stack[MAX_BREAK_STACK];
    int       loop_depth;

    /* --- Per-loop break fixup lists (index into fixups[]) ----------- */
    int break_fixup_indices[MAX_FIXUPS];
    int break_fixup_count;

    /* --- Entry offset (for ROX header) ------------------------------ */
    int  entry_offset;

    /* --- Offset of print_int runtime helper in code[] --------------- */
    int  print_int_offset;

    int  had_error;
} Codegen;

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

/** Zero-initialise the Codegen struct. */
void codegen_init(Codegen *cg);

/**
 * Pre-scan phase: collect all function definitions from the top-level AST
 * so calls can be resolved.  Must be called before codegen_run().
 */
void codegen_prescan(Codegen *cg, AstNode *prog);

/**
 * Main compilation phase: walk the AST and emit the full binary.
 * Returns 0 on success, -1 on error.
 */
int  codegen_run(Codegen *cg, AstNode *prog);

/** Hex-dump the generated binary to the display. */
void codegen_dump_code(Codegen *cg);

#endif /* COMPILER_CODEGEN_H */
