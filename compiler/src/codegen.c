#include "codegen.h"
#include "error.h"
#include "stdio.h"
#include "string.h"

static void emit8(Codegen *cg, u8 b) {
    if (cg->code_len < MAX_CODE_BYTES) cg->code[cg->code_len++] = b;
}
static void emit32(Codegen *cg, u32 val) {
    emit8(cg, val & 0xFF);
    emit8(cg, (val >> 8) & 0xFF);
    emit8(cg, (val >> 16) & 0xFF);
    emit8(cg, (val >> 24) & 0xFF);
}
static void patch32(Codegen *cg, int pos, u32 val) {
    if (pos + 3 >= MAX_CODE_BYTES) return;
    cg->code[pos] = val & 0xFF;
    cg->code[pos+1] = (val >> 8) & 0xFF;
    cg->code[pos+2] = (val >> 16) & 0xFF;
    cg->code[pos+3] = (val >> 24) & 0xFF;
}

static void emit_int80(Codegen *cg) { emit8(cg, 0xCD); emit8(cg, 0x80); }

/* Basic mov reg, imm32 */
static void emit_mov_eax_imm(Codegen *cg, u32 v) { emit8(cg, 0xB8); emit32(cg, v); }
static void emit_mov_ebx_imm(Codegen *cg, u32 v) { emit8(cg, 0xBB); emit32(cg, v); }
static void emit_mov_ecx_imm(Codegen *cg, u32 v) { emit8(cg, 0xB9); emit32(cg, v); }
static void emit_mov_edx_imm(Codegen *cg, u32 v) { emit8(cg, 0xBA); emit32(cg, v); }

/* push / pop */
static void emit_push_eax(Codegen *cg) { emit8(cg, 0x50); }
// static void emit_pop_eax(Codegen *cg)  { emit8(cg, 0x58); }
static void emit_pop_ebx(Codegen *cg)  { emit8(cg, 0x5B); }
static void emit_pop_ecx(Codegen *cg)  { emit8(cg, 0x59); }
static void emit_pop_edx(Codegen *cg)  { emit8(cg, 0x5A); }
static void emit_pop_esi(Codegen *cg)  { emit8(cg, 0x5E); }
static void emit_pop_edi(Codegen *cg)  { emit8(cg, 0x5F); }

static void emit_jmp(Codegen *cg, int dest) {
    emit8(cg, 0xE9);
    emit32(cg, dest - (cg->code_len + 4));
}
static int emit_jmp_fwd(Codegen *cg) {
    emit8(cg, 0xE9);
    int pos = cg->code_len;
    emit32(cg, 0);
    return pos;
}
static int emit_jcc_fwd(Codegen *cg, u8 cond) {
    emit8(cg, 0x0F);
    emit8(cg, cond);
    int pos = cg->code_len;
    emit32(cg, 0);
    return pos;
}
static void resolve_fwd(Codegen *cg, int pos) {
    patch32(cg, pos, cg->code_len - (pos + 4));
}

void codegen_init(Codegen *cg) {
    memset(cg, 0, sizeof(*cg));
}

void codegen_prescan(Codegen *cg, AstNode *prog) {
    if (!prog || prog->type != AST_PROGRAM) return;
    for (int i = 0; i < prog->stmt_count; i++) {
        AstNode *n = prog->stmts[i];
        if (n->type == AST_FUNC_DEF) {
            if (cg->func_count >= MAX_FUNCTIONS) continue;
            FuncEntry *f = &cg->funcs[cg->func_count++];
            strncpy(f->name, n->name, MAX_IDENT_LEN - 1);
            f->param_count = n->param_count;
            f->ret_type = n->ret_resolved;
            f->is_defined = 1;
        }
    }
}

static int add_string(Codegen *cg, const char *str, int len) {
    if (cg->string_count >= MAX_STRINGS) return 0;
    StrEntry *se = &cg->strings[cg->string_count++];
    int copy_len = len < MAX_STR_LEN - 1 ? len : MAX_STR_LEN - 1;
    memcpy(se->bytes, str, copy_len);
    se->bytes[copy_len] = '\0';
    se->len = copy_len;
    /* allocate in data section immediately */
    se->data_offset = cg->data_len;
    memcpy(cg->data + cg->data_len, se->bytes, copy_len + 1);
    cg->data_len += copy_len + 1;
    return cg->string_count - 1;
}

static VarEntry *find_var(Codegen *cg, const char *name) {
    for (int i = cg->local_count - 1; i >= 0; i--) {
        if (strcmp(cg->locals[i].name, name) == 0) return &cg->locals[i];
    }
    return NULL;
}

static void emit_expr(Codegen *cg, AstNode *n);

static void emit_binop(Codegen *cg, char op, char op2) {
    emit_pop_ebx(cg);  /* ebx = left, eax = right */
    if (op == '+') { emit8(cg,0x01); emit8(cg,0xD8); } /* add eax, ebx */
    else if (op == '-') { emit8(cg,0x29); emit8(cg,0xC3); emit8(cg,0x89); emit8(cg,0xD8); } /* sub ebx, eax; mov eax, ebx */
    else if (op == '*') { emit8(cg,0x0F); emit8(cg,0xAF); emit8(cg,0xC3); } /* imul eax, ebx */
    else if (op == '/') { emit8(cg,0x89); emit8(cg,0xC1); emit8(cg,0x89); emit8(cg,0xD8); emit8(cg,0x99); emit8(cg,0xF7); emit8(cg,0xF9); } /* xchg eax, ebx; cdq; idiv ecx */
    else if (op == '%') { emit8(cg,0x89); emit8(cg,0xC1); emit8(cg,0x89); emit8(cg,0xD8); emit8(cg,0x99); emit8(cg,0xF7); emit8(cg,0xF9); emit8(cg,0x89); emit8(cg,0xD0); } /* div as above, mov eax, edx */
    else if (op == '=') {
        /* cmp ebx, eax -> 39 C3 */
        emit8(cg,0x39); emit8(cg,0xC3);
        if (op2 == '=') { emit8(cg,0x0F); emit8(cg,0x94); emit8(cg,0xC0); } /* sete al */
        emit8(cg,0x0F); emit8(cg,0xB6); emit8(cg,0xC0); /* movzx eax, al */
    }
    else if (op == '!') {
        if (op2 == '=') { emit8(cg,0x39); emit8(cg,0xC3); emit8(cg,0x0F); emit8(cg,0x95); emit8(cg,0xC0); }
        emit8(cg,0x0F); emit8(cg,0xB6); emit8(cg,0xC0);
    }
    else if (op == '<') {
        if (op2 == '<') { /* shl: ebx=value, eax=count */
            emit8(cg,0x89); emit8(cg,0xC1); /* mov ecx, eax  (count -> CL) */
            emit8(cg,0x89); emit8(cg,0xD8); /* mov eax, ebx  (value -> EAX) */
            emit8(cg,0xD3); emit8(cg,0xE0); /* shl eax, cl */
        } else {
            emit8(cg,0x39); emit8(cg,0xC3);
            if (op2 == '=') { emit8(cg,0x0F); emit8(cg,0x9E); emit8(cg,0xC0); } /* setle al */
            else { emit8(cg,0x0F); emit8(cg,0x9C); emit8(cg,0xC0); } /* setl al */
            emit8(cg,0x0F); emit8(cg,0xB6); emit8(cg,0xC0);
        }
    }
    else if (op == '>') {
        if (op2 == '>') { /* sar: ebx=value, eax=count */
            emit8(cg,0x89); emit8(cg,0xC1); /* mov ecx, eax  (count -> CL) */
            emit8(cg,0x89); emit8(cg,0xD8); /* mov eax, ebx  (value -> EAX) */
            emit8(cg,0xD3); emit8(cg,0xF8); /* sar eax, cl */
        } else {
            emit8(cg,0x39); emit8(cg,0xC3);
            if (op2 == '=') { emit8(cg,0x0F); emit8(cg,0x9D); emit8(cg,0xC0); } /* setge al */
            else { emit8(cg,0x0F); emit8(cg,0x9F); emit8(cg,0xC0); } /* setg al */
            emit8(cg,0x0F); emit8(cg,0xB6); emit8(cg,0xC0);
        }
    }
    else if (op == '&') { emit8(cg,0x21); emit8(cg,0xD8); } /* and eax, ebx */
    else if (op == '|') { emit8(cg,0x09); emit8(cg,0xD8); } /* or eax, ebx */
    else if (op == '^') { emit8(cg,0x31); emit8(cg,0xD8); } /* xor eax, ebx */
    else if (op == 'A') { /* && logic usually needs short-circuit, basic binop ok for now */ emit8(cg,0x21); emit8(cg,0xD8); }
    else if (op == 'O') { emit8(cg,0x09); emit8(cg,0xD8); }
}

static void emit_expr(Codegen *cg, AstNode *n) {
    if (!n) return;
    switch (n->type) {
        case AST_NUMBER:
            emit_mov_eax_imm(cg, n->number);
            break;
        case AST_BOOL:
            emit_mov_eax_imm(cg, n->bool_val ? 1 : 0);
            break;
        case AST_STRING: {
            int sid = add_string(cg, n->name, n->number);
            emit_mov_eax_imm(cg, 0);
            /* fixup */
            Fixup *f = &cg->fixups[cg->fixup_count++];
            f->kind = FIX_ABS_STR;
            f->patch_pos = cg->code_len - 4;
            f->idx = sid;
            break;
        }
        case AST_IDENT: {
            VarEntry *v = find_var(cg, n->name);
            if (!v) { cg->had_error = 1; error_report("codegen", "undefined var", n->line, 0); return; }
            /* mov eax, [ebp +/- slot*4] */
            emit8(cg, 0x8B); 
            if (v->is_param) {
                emit8(cg, 0x45); emit8(cg, v->slot * 4);
            } else {
                emit8(cg, 0x85); emit32(cg, -(v->slot * 4));
            }
            break;
        }
        case AST_BINOP:
            /* Short circuit logic eventually... for now strict evaluation */
            emit_expr(cg, n->left);
            emit_push_eax(cg);
            emit_expr(cg, n->right);
            emit_binop(cg, n->op, n->op2);
            break;
        case AST_UNARY:
            emit_expr(cg, n->left);
            if (n->op == '-') { emit8(cg, 0xF7); emit8(cg, 0xD8); } /* neg eax */
            else if (n->op == '!') { emit8(cg,0x85); emit8(cg,0xC0); emit8(cg,0x0F); emit8(cg,0x94); emit8(cg,0xC0); emit8(cg,0x0F); emit8(cg,0xB6); emit8(cg,0xC0); } /* test eax, eax; sete al; movzx eax, al */
            else if (n->op == '~') { emit8(cg,0xF7); emit8(cg,0xD0); } /* not eax */
            break;
        case AST_CALL: {
            /* push args right-to-left */
            for (int i = n->arg_count - 1; i >= 0; i--) {
                emit_expr(cg, n->args[i]);
                emit_push_eax(cg);
            }
            /* call */
            int func_idx = -1;
            for (int i=0; i<cg->func_count; i++) {
                if (strcmp(cg->funcs[i].name, n->name)==0) { func_idx = i; break; }
            }
            if (func_idx < 0) { cg->had_error = 1; error_report("codegen","undefined func",n->line,0); return; }
            emit8(cg, 0xE8);
            Fixup *f = &cg->fixups[cg->fixup_count++];
            f->kind = FIX_REL_FUNC;
            f->patch_pos = cg->code_len;
            f->idx = func_idx;
            emit32(cg, 0);
            /* cleanup stack */
            if (n->arg_count > 0) {
                emit8(cg, 0x83); emit8(cg, 0xC4); emit8(cg, n->arg_count * 4); /* add esp, count*4 */
            }
            break;
        }
        case AST_GUI_CALL: {
            /* GUI system calls */
            /* push args right to left, then pop into syscall regs.
             * Syscall convention: EAX=op, EBX=arg0, ECX=arg1, EDX=arg2,
             *                     ESI=arg3, EDI=arg4.
             * If more than 5 args are passed we push extra slots onto the
             * stack but only consume the first 5; clean the rest up with
             * ADD ESP afterwards so ESP stays balanced. */
            int pushed = n->arg_count;
            for (int i = n->arg_count - 1; i >= 0; i--) {
                emit_expr(cg, n->args[i]);
                emit_push_eax(cg);
            }
            int popped = 0;
            if (n->arg_count >= 1) { emit_pop_ebx(cg); popped++; }
            if (n->arg_count >= 2) { emit_pop_ecx(cg); popped++; }
            if (n->arg_count >= 3) { emit_pop_edx(cg); popped++; }
            if (n->arg_count >= 4) { emit_pop_esi(cg); popped++; }
            if (n->arg_count >= 5) { emit_pop_edi(cg); popped++; }
            /* Discard any remaining args beyond 5 */
            if (pushed > popped) {
                int extra = (pushed - popped) * 4;
                emit8(cg, 0x83); emit8(cg, 0xC4); emit8(cg, (u8)extra); /* add esp, extra */
            }
            emit_mov_eax_imm(cg, n->gui_op);
            emit_int80(cg);
            break;
        }
        default: break;
    }
}

static void emit_stmt(Codegen *cg, AstNode *n) {
    if (!n) return;
    switch (n->type) {
        case AST_LET:
        case AST_MUT: {
            emit_expr(cg, n->expr);
            VarEntry *v = &cg->locals[cg->local_count++];
            strncpy(v->name, n->name, MAX_IDENT_LEN-1);
            v->slot = cg->next_slot++;
            v->is_param = 0;
            v->type = rostype_from_str(n->type_name);
            /* mov [ebp - slot*4], eax */
            emit8(cg, 0x89); emit8(cg, 0x85); emit32(cg, -(v->slot * 4));
            break;
        }
        case AST_ASSIGN: {
            VarEntry *v = find_var(cg, n->name);
            if (!v) { error_report("codegen", "undefined var", n->line,0); cg->had_error=1; return; }
            if (n->op == 0) {
                emit_expr(cg, n->expr);
            } else {
                /* compound */
                emit8(cg, 0x8B); 
                if (v->is_param) { emit8(cg, 0x45); emit8(cg, v->slot * 4); }
                else             { emit8(cg, 0x85); emit32(cg, -(v->slot * 4)); }
                emit_push_eax(cg);
                emit_expr(cg, n->expr);
                emit_binop(cg, n->op, n->op2);
            }
            emit8(cg, 0x89); 
            if (v->is_param) { emit8(cg, 0x45); emit8(cg, v->slot * 4); }
            else             { emit8(cg, 0x85); emit32(cg, -(v->slot * 4)); }
            break;
        }
        case AST_PRINT:
        case AST_PRINTLN: {
            if (n->expr->type == AST_STRING) {
                int sid = add_string(cg, n->expr->name, n->expr->number);
                if (n->type == AST_PRINTLN) {
                    /* add newline implicitly */
                    char nlb[1] = {'\n'};
                    add_string(cg, nlb, 1);
                }
                /* Print using SYS_WRITE */
                emit_mov_eax_imm(cg, ROSC_SYS_WRITE);
                emit_mov_ebx_imm(cg, 1);
                emit_mov_ecx_imm(cg, 0);
                Fixup *f = &cg->fixups[cg->fixup_count++];
                f->kind = FIX_ABS_STR;
                f->patch_pos = cg->code_len - 4;
                f->idx = sid;
                emit_mov_edx_imm(cg, n->expr->number); /* length */
                emit_int80(cg);

                if (n->type == AST_PRINTLN) {
                    emit_mov_eax_imm(cg, ROSC_SYS_WRITE);
                    emit_mov_ebx_imm(cg, 1);
                    emit_mov_ecx_imm(cg, 0);
                    Fixup *f2 = &cg->fixups[cg->fixup_count++];
                    f2->kind = FIX_ABS_STR; f2->patch_pos = cg->code_len - 4; f2->idx = sid+1;
                    emit_mov_edx_imm(cg, 1);
                    emit_int80(cg);
                }
            } else {
                /* Print integer -> invoke runtime helper */
                emit_expr(cg, n->expr);
                emit_push_eax(cg);
                /* call print_int helper at its recorded code offset */
                emit8(cg, 0xE8); emit32(cg, cg->print_int_offset - (cg->code_len + 4));
                emit8(cg, 0x83); emit8(cg, 0xC4); emit8(cg, 4); /* cleanup */
                if (n->type == AST_PRINTLN) {
                    /* call print_newline_helper at code_offset ~60 (we'll place it right after print_int) */
                    // But easier to just emit sys_write \n right here
                    int sid = add_string(cg, "\n", 1);
                    emit_mov_eax_imm(cg, 1); emit_mov_ebx_imm(cg, 1); emit_mov_ecx_imm(cg, 0);
                    Fixup *f = &cg->fixups[cg->fixup_count++]; f->kind = FIX_ABS_STR; f->patch_pos = cg->code_len-4; f->idx = sid;
                    emit_mov_edx_imm(cg, 1); emit_int80(cg);
                }
            }
            break;
        }
        case AST_IF: {
            emit_expr(cg, n->cond);
            emit8(cg, 0x85); emit8(cg, 0xC0); /* test eax, eax */
            int jz_off = emit_jcc_fwd(cg, 0x84);
            emit_stmt(cg, n->then_b);
            if (n->else_b) {
                int jmp_off = emit_jmp_fwd(cg);
                resolve_fwd(cg, jz_off);
                emit_stmt(cg, n->else_b);
                resolve_fwd(cg, jmp_off);
            } else {
                resolve_fwd(cg, jz_off);
            }
            break;
        }
        case AST_WHILE: {
            int top = cg->code_len;
            int saved_loop_depth = cg->loop_depth++;
            int saved_breaks = cg->break_fixup_count;
            LoopFrame *lf = &cg->loop_stack[saved_loop_depth];
            lf->continue_target = top;
            lf->break_fixup_start = cg->break_fixup_count;

            emit_expr(cg, n->cond);
            emit8(cg, 0x85); emit8(cg, 0xC0);
            int jz_off = emit_jcc_fwd(cg, 0x84);

            emit_stmt(cg, n->body);
            emit_jmp(cg, top);
            resolve_fwd(cg, jz_off);

            /* resolve breaks */
            for (int i=lf->break_fixup_start; i<cg->break_fixup_count; i++) {
                resolve_fwd(cg, cg->break_fixup_indices[i]);
            }
            cg->break_fixup_count = saved_breaks;
            cg->loop_depth--;
            break;
        }
        case AST_FOR: {
            if (!n->range) break;
            int saved_locals = cg->local_count;
            int saved_brk    = cg->break_fixup_count;
            int saved_depth  = cg->loop_depth++;
            LoopFrame *lf    = &cg->loop_stack[saved_depth];
            lf->break_fixup_start = saved_brk;

            /* 1. Init: evaluate lo, store in loop variable */
            emit_expr(cg, n->range->left);   /* lo → eax */
            VarEntry *lv = &cg->locals[cg->local_count++];
            strncpy(lv->name, n->name, MAX_IDENT_LEN - 1);
            lv->slot     = cg->next_slot++;
            lv->is_param = 0;
            lv->type     = TY_I32;
            emit8(cg, 0x89); emit8(cg, 0x85); emit32(cg, -(lv->slot * 4)); /* mov [var], eax */

            /* 2. Condition: jge exit when var >= hi */
            int loop_top = cg->code_len;

            /* Jump over the continue-trampoline into body */
            int over_tramp = emit_jmp_fwd(cg);

            /* Continue-trampoline: landed on by 'continue', jumps to increment */
            int trampoline_pos = cg->code_len;
            lf->continue_target = trampoline_pos;   /* continue → trampoline → increment */
            int tramp_to_incr   = emit_jmp_fwd(cg); /* jmp increment (patched below)      */

            /* over-trampoline lands here */
            resolve_fwd(cg, over_tramp);

            /* Condition check */
            emit8(cg, 0x8B); emit8(cg, 0x8D); emit32(cg, -(lv->slot * 4)); /* mov ecx, [var] */
            emit_expr(cg, n->range->right);          /* hi → eax */
            emit8(cg, 0x39); emit8(cg, 0xC1);        /* cmp ecx, eax */
            int jge_pos = emit_jcc_fwd(cg, 0x8D);   /* jge exit */

            /* 3. Body */
            emit_stmt(cg, n->body);

            /* 4. Increment: patch trampoline here, then inc [var] */
            resolve_fwd(cg, tramp_to_incr);          /* trampoline → increment */
            emit8(cg, 0xFF); emit8(cg, 0x85); emit32(cg, -(lv->slot * 4)); /* inc [var] */

            /* 5. Back to condition */
            emit_jmp(cg, loop_top);

            /* 6. Exit label */
            resolve_fwd(cg, jge_pos);

            /* 7. Resolve break fixups */
            cg->loop_depth = saved_depth;
            for (int bi = lf->break_fixup_start; bi < cg->break_fixup_count; bi++)
                resolve_fwd(cg, cg->break_fixup_indices[bi]);
            cg->break_fixup_count = saved_brk;

            /* 8. Remove loop variable from scope */
            cg->local_count = saved_locals;
            break;
        }
        case AST_BREAK:
            if (cg->loop_depth > 0) {
                cg->break_fixup_indices[cg->break_fixup_count++] = emit_jmp_fwd(cg);
            }
            break;
        case AST_CONTINUE:
            if (cg->loop_depth > 0) {
                emit_jmp(cg, cg->loop_stack[cg->loop_depth-1].continue_target);
            }
            break;
        case AST_BLOCK: {
            int saved_locals = cg->local_count;
            for (int i=0; i<n->stmt_count; i++) emit_stmt(cg, n->stmts[i]);
            cg->local_count = saved_locals;
            break;
        }
        case AST_EXPR_STMT:
            emit_expr(cg, n->expr);
            break;
        case AST_RETURN:
            if (n->expr) emit_expr(cg, n->expr);
            /* ret prologue */
            emit8(cg, 0x89); emit8(cg, 0xEC); /* mov esp, ebp */
            emit8(cg, 0x5D); /* pop ebp */
            emit8(cg, 0xC3); /* ret */
            break;
        default: break;
    }
}

/* Hardcoded helper to print integer (u32, crude hex works best but let's do dec)
   Translates to: 
   print_int: push ebp; mov ebp, esp; sub esp, 16; mov eax,[ebp+8]; 
   ... div 10 logic. 
   To save space, just raw hex or very small loop */
static void emit_print_int_helper(Codegen *cg) {
    /*
     * Decimal i32 printer  (sys_write = 1, Linux/RosOS i386 ABI)
     *
     * void print_int(int n)   -- called cdecl, [ebp+8] = n
     *
     * Layout (73 bytes):
     *  [0]  prologue
     *  [6]  load arg
     *  [9]  test zero → [13] zero path → jmp [57]
     *  [27] nonzero: build decimal string backwards into [ebp-16..ebp-1]
     *  [52] set up sys_write args from loop results
     *  [57] sys_write(1, buf, len) → epilogue → ret
     *
     *  Disassembly:
     *   55              push ebp
     *   89 E5           mov  ebp, esp
     *   83 EC 10        sub  esp, 16
     *   8B 45 08        mov  eax, [ebp+8]       ; n
     *   85 C0           test eax, eax
     *   75 0E           jnz  .nonzero
     *   C6 45 FF 30     mov  byte [ebp-1], '0'
     *   8D 4D FF        lea  ecx, [ebp-1]
     *   BA 01 00 00 00  mov  edx, 1
     *   EB 1E           jmp  .do_write
     *  .nonzero:
     *   BB 0A 00 00 00  mov  ebx, 10
     *   8D 7D FF        lea  edi, [ebp-1]        ; fill ptr (backwards)
     *   31 C9           xor  ecx, ecx            ; digit count
     *  .loop:
     *   31 D2           xor  edx, edx
     *   F7 F3           div  ebx                 ; eax=q, edx=r
     *   83 C2 30        add  edx, 0x30
     *   88 17           mov  [edi], dl
     *   4F              dec  edi
     *   41              inc  ecx
     *   85 C0           test eax, eax
     *   75 F1           jnz  .loop
     *   89 CA           mov  edx, ecx            ; length
     *   8D 4F 01        lea  ecx, [edi+1]        ; buf start
     *  .do_write:
     *   B8 01 00 00 00  mov  eax, 1              ; sys_write
     *   BB 01 00 00 00  mov  ebx, 1              ; fd=stdout
     *   CD 80           int  0x80
     *   89 EC           mov  esp, ebp
     *   5D              pop  ebp
     *   C3              ret
     */
    u8 code[] = {
        /* 0  */ 0x55,                           /* push ebp           */
        /* 1  */ 0x89, 0xE5,                     /* mov ebp, esp       */
        /* 3  */ 0x83, 0xEC, 0x10,               /* sub esp, 16        */
        /* 6  */ 0x8B, 0x45, 0x08,               /* mov eax, [ebp+8]   */
        /* 9  */ 0x85, 0xC0,                     /* test eax, eax      */
        /* 11 */ 0x75, 0x0E,                     /* jnz .nonzero (+14) */
        /* 13 */ 0xC6, 0x45, 0xFF, 0x30,         /* mov [ebp-1],'0'    */
        /* 17 */ 0x8D, 0x4D, 0xFF,               /* lea ecx,[ebp-1]    */
        /* 20 */ 0xBA, 0x01, 0x00, 0x00, 0x00,   /* mov edx, 1         */
        /* 25 */ 0xEB, 0x1E,                     /* jmp .do_write (+30)*/
        /* 27 */ 0xBB, 0x0A, 0x00, 0x00, 0x00,  /* mov ebx, 10        */
        /* 32 */ 0x8D, 0x7D, 0xFF,               /* lea edi,[ebp-1]    */
        /* 35 */ 0x31, 0xC9,                     /* xor ecx, ecx       */
        /* 37 */ 0x31, 0xD2,                     /* xor edx, edx       */
        /* 39 */ 0xF7, 0xF3,                     /* div ebx            */
        /* 41 */ 0x83, 0xC2, 0x30,               /* add edx, 0x30      */
        /* 44 */ 0x88, 0x17,                     /* mov [edi], dl      */
        /* 46 */ 0x4F,                           /* dec edi            */
        /* 47 */ 0x41,                           /* inc ecx            */
        /* 48 */ 0x85, 0xC0,                     /* test eax, eax      */
        /* 50 */ 0x75, 0xF1,                     /* jnz .loop (-15)    */
        /* 52 */ 0x89, 0xCA,                     /* mov edx, ecx       */
        /* 54 */ 0x8D, 0x4F, 0x01,               /* lea ecx,[edi+1]    */
        /* 57 */ 0xB8, 0x01, 0x00, 0x00, 0x00,  /* mov eax, 1         */
        /* 62 */ 0xBB, 0x01, 0x00, 0x00, 0x00,  /* mov ebx, 1 (stdout)*/
        /* 67 */ 0xCD, 0x80,                     /* int 0x80           */
        /* 69 */ 0x89, 0xEC,                     /* mov esp, ebp       */
        /* 71 */ 0x5D,                           /* pop ebp            */
        /* 72 */ 0xC3                            /* ret                */
    };
    for (unsigned int i=0; i<sizeof(code); i++) emit8(cg, code[i]);
}

int codegen_run(Codegen *cg, AstNode *prog) {
    /* 1. prologue jump to main */
    int jmp_main = emit_jmp_fwd(cg);

    /* 2. Runtime helpers */
    cg->print_int_offset = cg->code_len;   /* record helper offset BEFORE emitting */
    emit_print_int_helper(cg);

    /* 3. Global functions */
    for (int i = 0; i < prog->stmt_count; i++) {
        AstNode *n = prog->stmts[i];
        if (n->type == AST_FUNC_DEF) {
            FuncEntry *f = NULL;
            for (int k=0; k<cg->func_count; k++) {
                if (strcmp(cg->funcs[k].name, n->name) == 0) { f = &cg->funcs[k]; break; }
            }
            if (!f) {
                /* Function was dropped in prescan (exceeded MAX_FUNCTIONS) */
                error_report("codegen", "function not registered (MAX_FUNCTIONS exceeded)", n->line, 0);
                cg->had_error = 1;
                continue;
            }
            f->code_offset = cg->code_len;
            /* Function prologue */
            emit8(cg, 0x55); /* push ebp */
            emit8(cg, 0x89); emit8(cg, 0xE5); /* mov ebp, esp */
            emit8(cg, 0x81); emit8(cg, 0xEC); 
            int frame_patch = cg->code_len; emit32(cg, 0); /* sub esp, FRAME */
            
            cg->local_count = 0; cg->next_slot = 1;
            /* setup params mapped to args */
            for (int p=0; p<n->param_count; p++) {
                VarEntry *v = &cg->locals[cg->local_count++];
                strncpy(v->name, n->params[p].name, MAX_IDENT_LEN-1);
                v->is_param = 1;
                v->slot = p + 2; /* ebp+8, ebp+12 */
            }

            emit_stmt(cg, n->body);

            patch32(cg, frame_patch, cg->next_slot * 4);

            /* Epilogue */
            emit8(cg, 0x89); emit8(cg, 0xEC); /* mov esp, ebp */
            emit8(cg, 0x5D); /* pop ebp */
            emit8(cg, 0xC3); /* ret */
        }
    }

    /* 4. Entry stub */
    resolve_fwd(cg, jmp_main);
    cg->entry_offset = cg->code_len; /* true entry */

    emit8(cg, 0x55);               /* push ebp */
    emit8(cg, 0x89); emit8(cg, 0xE5); /* mov ebp, esp */
    emit8(cg, 0x81); emit8(cg, 0xEC);
    int main_frame_patch = cg->code_len; emit32(cg, 0);

    cg->local_count = 0; cg->next_slot = 1;

    /* If there is a fn main(), call it; otherwise run top-level stmts */
    int main_idx = -1;
    for (int k = 0; k < cg->func_count; k++) {
        if (strcmp(cg->funcs[k].name, "main") == 0) { main_idx = k; break; }
    }

    if (main_idx >= 0) {
        /* call main  (E8 rel32 – patched by FIX_REL_FUNC fixup) */
        emit8(cg, 0xE8);
        if (cg->fixup_count < MAX_FIXUPS) {
            Fixup *fx = &cg->fixups[cg->fixup_count++];
            fx->kind = FIX_REL_FUNC;
            fx->patch_pos = cg->code_len;
            fx->idx = main_idx;
        }
        emit32(cg, 0); /* placeholder, filled by fixup pass */
    } else {
        /* no fn main – emit bare top-level statements */
        for (int i = 0; i < prog->stmt_count; i++) {
            AstNode *n = prog->stmts[i];
            if (n->type != AST_FUNC_DEF) emit_stmt(cg, n);
        }
    }
    patch32(cg, main_frame_patch, cg->next_slot * 4);

    /* SYS_EXIT(0) */
    emit_mov_eax_imm(cg, ROSC_SYS_EXIT);
    emit_mov_ebx_imm(cg, 0);
    emit_int80(cg);

    /* 5. Fixup resolution */
    int data_start = USER_CODE_BASE + cg->code_len;
    for (int i=0; i<cg->fixup_count; i++) {
        Fixup *f = &cg->fixups[i];
        if (f->kind == FIX_ABS_STR) {
            patch32(cg, f->patch_pos, data_start + cg->strings[f->idx].data_offset);
        } else if (f->kind == FIX_REL_FUNC) {
            FuncEntry *fn = &cg->funcs[f->idx];
            patch32(cg, f->patch_pos, fn->code_offset - (f->patch_pos + 4));
        }
    }

    /* Assemble */
    cg->binary_len = 0;
    memcpy(cg->binary, cg->code, cg->code_len);
    cg->binary_len = cg->code_len;
    memcpy(cg->binary + cg->binary_len, cg->data, cg->data_len);
    cg->binary_len += cg->data_len;

    return cg->had_error ? -1 : 0;
}

void codegen_dump_code(Codegen *cg) {
    printf("Code output length: %d\n", cg->binary_len);
}
