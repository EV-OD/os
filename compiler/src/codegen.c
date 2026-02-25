#include "codegen.h"
#include "error.h"
#include "stdio.h"
#include "string.h"

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */
void codegen_init(Codegen *cg)
{
    memset(cg, 0, sizeof(Codegen));
}

/* Append one byte to the code buffer. */
static void emit8(Codegen *cg, u8 b)
{
    if (cg->code_len < MAX_CODE_BYTES)
        cg->code[cg->code_len++] = b;
}

/* Emit  MOV EAX, imm32  (opcode B8 /id, 5 bytes, little-endian). */
static void emit_mov_eax_imm32(Codegen *cg, i32 val)
{
    u32 v = (u32)val;
    emit8(cg, 0xB8);
    emit8(cg, (u8)( v        & 0xFF));
    emit8(cg, (u8)((v >>  8) & 0xFF));
    emit8(cg, (u8)((v >> 16) & 0xFF));
    emit8(cg, (u8)((v >> 24) & 0xFF));
}

/* -----------------------------------------------------------------------
 * Constant folding
 *
 * Recursively evaluates an expression node to an i32 at compile time.
 * Variable references are resolved from cg->bind_name/bind_val.
 * Returns 0 on success and stores the result in *out; -1 on error.
 * --------------------------------------------------------------------- */
static int eval_expr(Codegen *cg, AstNode *node, i32 *out)
{
    if (!node) return -1;

    switch (node->type) {
        case AST_NUMBER:
            *out = node->number;
            return 0;

        case AST_IDENT: {
            int i;
            for (i = 0; i < cg->bind_count; i++) {
                if (strcmp(cg->bind_name[i], node->name) == 0) {
                    *out = cg->bind_val[i];
                    return 0;
                }
            }
            /* Not found */
            char msg[64];
            /* Build without %s to be safe – just concatenate manually */
            strcpy(msg, "undefined variable: ");
            strncpy(msg + 20, node->name, MAX_IDENT_LEN - 1);
            error_report("codegen", msg, 0, 0);
            cg->had_error = 1;
            return -1;
        }

        case AST_BINOP: {
            i32 lv, rv;
            if (eval_expr(cg, node->left,  &lv) != 0) return -1;
            if (eval_expr(cg, node->right, &rv) != 0) return -1;
            switch (node->op) {
                case '+': *out = lv + rv; break;
                case '-': *out = lv - rv; break;
                case '*': *out = lv * rv; break;
                case '/':
                    if (rv == 0) {
                        error_report("codegen", "division by zero", 0, 0);
                        cg->had_error = 1;
                        return -1;
                    }
                    *out = lv / rv;
                    break;
                default:
                    error_report("codegen", "unknown binary operator", 0, 0);
                    cg->had_error = 1;
                    return -1;
            }
            return 0;
        }

        default:
            error_report("codegen", "unexpected AST node in expression", 0, 0);
            cg->had_error = 1;
            return -1;
    }
}

/* -----------------------------------------------------------------------
 * Main code-generation pass
 * --------------------------------------------------------------------- */
int codegen_run(Codegen *cg, AstNode *prog)
{
    int    i;
    i32    last_val = 0;

    if (!prog || prog->type != AST_PROGRAM) return -1;

    for (i = 0; i < prog->stmt_count; i++) {
        AstNode *stmt = prog->stmts[i];
        if (!stmt || stmt->type != AST_LET) continue;

        i32 val;
        if (eval_expr(cg, stmt->expr, &val) != 0) return -1;

        /* Record in the binding table */
        if (cg->bind_count < MAX_BINDINGS) {
            strncpy(cg->bind_name[cg->bind_count], stmt->name, MAX_IDENT_LEN - 1);
            cg->bind_val[cg->bind_count] = val;
            cg->bind_count++;
        }
        last_val = val;
    }

    /*
     * Emit the flat binary for the compiled function:
     *
     *   mov  eax, <last_val>   ; return value
     *   ret
     */
    emit_mov_eax_imm32(cg, last_val);
    emit8(cg, 0xC3);   /* RET */

    return 0;
}

/* -----------------------------------------------------------------------
 * Diagnostics / display helpers
 * --------------------------------------------------------------------- */

/* Convert a nibble (0-15) to its hex digit character. */
static char nibble_to_hex(u8 n)
{
    return (n < 10) ? ('0' + n) : ('A' + n - 10);
}

void codegen_dump_code(Codegen *cg)
{
    char buf[8];
    int  i;

    puts("Generated code (hex):\n  ");
    for (i = 0; i < cg->code_len; i++) {
        u8 b      = cg->code[i];
        buf[0]    = nibble_to_hex((b >> 4) & 0xF);
        buf[1]    = nibble_to_hex(b & 0xF);
        buf[2]    = ' ';
        buf[3]    = '\0';
        puts(buf);
    }

    char summary[64];
    sprintf(summary, "\n  (%d bytes)\n", cg->code_len);
    puts(summary);
}

void codegen_dump_bindings(Codegen *cg)
{
    char buf[128];
    int  i;

    puts("Compile-time results:\n");
    for (i = 0; i < cg->bind_count; i++) {
        sprintf(buf, "  %-16s = %d\n", cg->bind_name[i], cg->bind_val[i]);
        puts(buf);
    }
}

/* -----------------------------------------------------------------------
 * Execute the generated binary in-place
 *
 * We cast the code buffer to a function pointer and call it.  Because
 * RandomOS does not yet enforce No-Execute (NX / XD) memory protection,
 * executing from a kernel-mode data buffer is safe in ring 0.
 * --------------------------------------------------------------------- */
i32 codegen_execute(Codegen *cg)
{
    if (cg->code_len == 0) return 0;
    typedef i32 (*fn_t)(void);
    fn_t fn = (fn_t)(void *)(cg->code);
    return fn();
}
