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

/* Emit a 32-bit little-endian immediate. */
static void emit32(Codegen *cg, u32 val)
{
    emit8(cg, (u8)( val        & 0xFF));
    emit8(cg, (u8)((val >>  8) & 0xFF));
    emit8(cg, (u8)((val >> 16) & 0xFF));
    emit8(cg, (u8)((val >> 24) & 0xFF));
}

/* Emit  MOV EAX, imm32  (opcode B8 + imm32, 5 bytes). */
static void emit_mov_eax_imm32(Codegen *cg, u32 val)
{
    emit8(cg, 0xB8);
    emit32(cg, val);
}

/* Emit  MOV EBX, imm32  (opcode BB + imm32, 5 bytes). */
static void emit_mov_ebx_imm32(Codegen *cg, u32 val)
{
    emit8(cg, 0xBB);
    emit32(cg, val);
}

/* Emit  MOV ECX, imm32  (opcode B9 + imm32, 5 bytes). */
static void emit_mov_ecx_imm32(Codegen *cg, u32 val)
{
    emit8(cg, 0xB9);
    emit32(cg, val);
}

/* Emit  MOV EDX, imm32  (opcode BA + imm32, 5 bytes). */
static void emit_mov_edx_imm32(Codegen *cg, u32 val)
{
    emit8(cg, 0xBA);
    emit32(cg, val);
}

/* Emit  int 0x80  (2 bytes: CD 80). */
static void emit_int80(Codegen *cg)
{
    emit8(cg, 0xCD);
    emit8(cg, 0x80);
}

/* -----------------------------------------------------------------------
 * Integer-to-string helper (for embedding result strings)
 *
 * Converts a signed i32 to a decimal string.  Returns number of chars
 * written (not including the null terminator).
 * --------------------------------------------------------------------- */
static int i32_to_str(i32 val, char *buf, int buf_size)
{
    char tmp[16];
    int  i = 0, len;
    int  neg = 0;
    u32  uval;

    if (val < 0) {
        neg = 1;
        /* Handle INT_MIN (-2147483648) carefully */
        uval = (u32)(-(val + 1)) + 1u;
    } else {
        uval = (u32)val;
    }

    /* Generate digits in reverse order */
    if (uval == 0) {
        tmp[i++] = '0';
    } else {
        while (uval > 0 && i < 15) {
            tmp[i++] = '0' + (char)(uval % 10);
            uval /= 10;
        }
    }
    if (neg && i < 15) tmp[i++] = '-';

    /* Reverse into buf */
    len = 0;
    while (i > 0 && len < buf_size - 1) {
        buf[len++] = tmp[--i];
    }
    buf[len] = '\0';
    return len;
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
 * Build an embedded string for a binding: "name = value\n"
 * --------------------------------------------------------------------- */
static void add_binding_string(Codegen *cg, const char *name, i32 val)
{
    if (cg->string_count >= MAX_BINDINGS) return;

    CodegenString *s = &cg->strings[cg->string_count];
    char vbuf[16];
    int  vlen = i32_to_str(val, vbuf, sizeof(vbuf));
    int  nlen = strlen(name);
    int  pos  = 0;

    /* "name = value\n" */
    if (nlen > 60) nlen = 60; /* safety */
    memcpy(s->str + pos, name, nlen); pos += nlen;
    s->str[pos++] = ' ';
    s->str[pos++] = '=';
    s->str[pos++] = ' ';
    memcpy(s->str + pos, vbuf, vlen); pos += vlen;
    s->str[pos++] = '\n';
    s->str[pos]   = '\0';
    s->len = pos;
    cg->string_count++;
}

/* -----------------------------------------------------------------------
 * Main code-generation pass
 *
 * 1. Constant-fold all let-bindings, build binding table + strings
 * 2. Emit user-mode code: for each binding, SYS_WRITE(1, str, len)
 * 3. Emit SYS_EXIT(0)
 * 4. Assemble final binary = code + string data
 * --------------------------------------------------------------------- */
int codegen_run(Codegen *cg, AstNode *prog)
{
    int    i;
    u32    data_start;

    if (!prog || prog->type != AST_PROGRAM) return -1;

    /* --- Pass 1: constant-fold all bindings and build strings ----------- */
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

        /* Build embedded string */
        add_binding_string(cg, stmt->name, val);
    }

    /* --- Pass 2: calculate code size first (for data offsets) ----------- */
    /*
     * For each string: 5+5+5+5+2 = 22 bytes of instructions
     *   mov eax, SYS_WRITE       (5)
     *   mov ebx, 1               (5)  ; fd = stdout
     *   mov ecx, <str_addr>      (5)  ; pointer to string
     *   mov edx, <str_len>       (5)  ; length
     *   int 0x80                 (2)
     *
     * Exit syscall: 5+5+2 = 12 bytes
     *   mov eax, SYS_EXIT        (5)
     *   mov ebx, 0               (5)  ; exit status
     *   int 0x80                 (2)
     *   (plus 1 byte hlt as safety)
     */
    int code_size_estimate = cg->string_count * 22 + 13;

    /* Calculate string data base offset from binary start */
    data_start = USER_CODE_BASE + (u32)code_size_estimate;

    /* --- Pass 3: emit code --------------------------------------------- */
    {
        u32 str_offset = 0;
        for (i = 0; i < cg->string_count; i++) {
            u32 str_addr = data_start + str_offset;
            int slen = cg->strings[i].len;

            emit_mov_eax_imm32(cg, ROSC_SYS_WRITE);   /* eax = 1 (SYS_WRITE) */
            emit_mov_ebx_imm32(cg, 1);                 /* ebx = 1 (stdout fd) */
            emit_mov_ecx_imm32(cg, str_addr);          /* ecx = string addr   */
            emit_mov_edx_imm32(cg, (u32)slen);         /* edx = string length */
            emit_int80(cg);                             /* int 0x80            */

            str_offset += (u32)slen;
        }
    }

    /* Emit SYS_EXIT(0) */
    emit_mov_eax_imm32(cg, ROSC_SYS_EXIT);   /* eax = 0 (SYS_EXIT) */
    emit_mov_ebx_imm32(cg, 0);               /* ebx = 0 (success)  */
    emit_int80(cg);                           /* int 0x80           */
    emit8(cg, 0xF4);                          /* hlt (safety)       */

    /* Verify our estimate was correct */
    if (cg->code_len != code_size_estimate) {
        /* This should not happen; recalculate data_start would be needed */
        error_report("codegen", "code size mismatch with estimate", 0, 0);
        cg->had_error = 1;
        return -1;
    }

    /* --- Assemble final binary: code + string data --------------------- */
    cg->binary_len = 0;

    /* Copy code section */
    memcpy(cg->binary, cg->code, cg->code_len);
    cg->binary_len = cg->code_len;

    /* Append string data */
    for (i = 0; i < cg->string_count; i++) {
        int slen = cg->strings[i].len;
        if (cg->binary_len + slen > (int)sizeof(cg->binary)) break;
        memcpy(cg->binary + cg->binary_len, cg->strings[i].str, slen);
        cg->binary_len += slen;
    }

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
    for (i = 0; i < cg->binary_len; i++) {
        u8 b      = cg->binary[i];
        buf[0]    = nibble_to_hex((b >> 4) & 0xF);
        buf[1]    = nibble_to_hex(b & 0xF);
        buf[2]    = ' ';
        buf[3]    = '\0';
        puts(buf);
        /* Newline every 16 bytes for readability */
        if ((i + 1) % 16 == 0 && i + 1 < cg->binary_len) {
            puts("\n  ");
        }
    }

    char summary[64];
    sprintf(summary, "\n  (%d bytes: %d code + %d data)\n",
            cg->binary_len, cg->code_len, cg->binary_len - cg->code_len);
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
 * Execute the generated binary in-place (ring 0, for diagnostics only).
 *
 * This is only useful for testing in kernel mode.  The real execution
 * path is process_create_user() which maps the binary at USER_CODE_BASE
 * and runs it in ring 3.
 * --------------------------------------------------------------------- */
i32 codegen_execute(Codegen *cg)
{
    if (cg->code_len == 0) return 0;
    /* For ring 0 in-place test, replace the final hlt+exit with ret */
    /* This is a legacy fallback – real execution goes through processes */
    typedef i32 (*fn_t)(void);
    fn_t fn = (fn_t)(void *)(cg->code);
    return fn();
}
