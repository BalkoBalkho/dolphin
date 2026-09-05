#include "Interpreter.h"
#include "Bytecode.h"
#include <cstring>

#if defined(__aarch64__) || defined(_M_ARM64)
void* g_dispatch_table[OP_COUNT];

// Declare all handler symbols from Interpreter.S
extern "C" {
    void handle_hlt(VMState*, const BytecodeInst*);
    void handle_nop(VMState*, const BytecodeInst*);
    void handle_add_reg(VMState*, const BytecodeInst*);
    void handle_sub_reg(VMState*, const BytecodeInst*);
    void handle_mul_reg(VMState*, const BytecodeInst*);
    void handle_and_reg(VMState*, const BytecodeInst*);
    void handle_orr_reg(VMState*, const BytecodeInst*);
    void handle_eor_reg(VMState*, const BytecodeInst*);
    void handle_lsl_reg(VMState*, const BytecodeInst*);
    void handle_lsr_reg(VMState*, const BytecodeInst*);
    void handle_asr_reg(VMState*, const BytecodeInst*);
    void handle_sdiv_reg(VMState*, const BytecodeInst*);
    void handle_udiv_reg(VMState*, const BytecodeInst*);
    void handle_add_imm(VMState*, const BytecodeInst*);
    void handle_sub_imm(VMState*, const BytecodeInst*);
    void handle_and_imm(VMState*, const BytecodeInst*);
    void handle_orr_imm(VMState*, const BytecodeInst*);
    void handle_eor_imm(VMState*, const BytecodeInst*);
    void handle_lsl_imm(VMState*, const BytecodeInst*);
    void handle_lsr_imm(VMState*, const BytecodeInst*);
    void handle_asr_imm(VMState*, const BytecodeInst*);
    void handle_mov_reg(VMState*, const BytecodeInst*);
    void handle_rev_reg(VMState*, const BytecodeInst*);
    void handle_rev16_reg(VMState*, const BytecodeInst*);
    void handle_rev64_reg(VMState*, const BytecodeInst*);
    void handle_movz(VMState*, const BytecodeInst*);
    void handle_movk(VMState*, const BytecodeInst*);
    void handle_movn(VMState*, const BytecodeInst*);
    void handle_adds_reg(VMState*, const BytecodeInst*);
    void handle_subs_reg(VMState*, const BytecodeInst*);
    void handle_adds_imm(VMState*, const BytecodeInst*);
    void handle_subs_imm(VMState*, const BytecodeInst*);
    void handle_ands_reg(VMState*, const BytecodeInst*);
    void handle_ands_imm(VMState*, const BytecodeInst*);
    void handle_cmp_reg(VMState*, const BytecodeInst*);
    void handle_cmp_imm(VMState*, const BytecodeInst*);
    void handle_cmn_reg(VMState*, const BytecodeInst*);
    void handle_cmn_imm(VMState*, const BytecodeInst*);
    void handle_tst_reg(VMState*, const BytecodeInst*);
    void handle_tst_imm(VMState*, const BytecodeInst*);
    void handle_csel(VMState*, const BytecodeInst*);
    void handle_cset(VMState*, const BytecodeInst*);
    void handle_csinc(VMState*, const BytecodeInst*);
    void handle_csinv(VMState*, const BytecodeInst*);
    void handle_csneg(VMState*, const BytecodeInst*);
    void handle_ldr_imm(VMState*, const BytecodeInst*);
    void handle_str_imm(VMState*, const BytecodeInst*);
    void handle_strw_imm(VMState*, const BytecodeInst*);
    void handle_ldrb_imm(VMState*, const BytecodeInst*);
    void handle_strb_imm(VMState*, const BytecodeInst*);
    void handle_ldrh_imm(VMState*, const BytecodeInst*);
    void handle_strh_imm(VMState*, const BytecodeInst*);
    void handle_ldrsw_imm(VMState*, const BytecodeInst*);
    void handle_ldrsb_imm(VMState*, const BytecodeInst*);
    void handle_ldrsh_imm(VMState*, const BytecodeInst*);
    void handle_ldr_reg(VMState*, const BytecodeInst*);
    void handle_str_reg(VMState*, const BytecodeInst*);
    void handle_ldp(VMState*, const BytecodeInst*);
    void handle_stp(VMState*, const BytecodeInst*);
    void handle_b(VMState*, const BytecodeInst*);
    void handle_bl(VMState*, const BytecodeInst*);
    void handle_blr(VMState*, const BytecodeInst*);
    void handle_br(VMState*, const BytecodeInst*);
    void handle_ret(VMState*, const BytecodeInst*);
    void handle_cbz(VMState*, const BytecodeInst*);
    void handle_cbnz(VMState*, const BytecodeInst*);
    void handle_tbz(VMState*, const BytecodeInst*);
    void handle_tbnz(VMState*, const BytecodeInst*);
    void handle_b_cond(VMState*, const BytecodeInst*);
}

void interpreter_init() {
    g_dispatch_table[OP_HLT]      = (void*)handle_hlt;
    g_dispatch_table[OP_NOP]      = (void*)handle_nop;
    g_dispatch_table[OP_ADD_REG]  = (void*)handle_add_reg;
    g_dispatch_table[OP_SUB_REG]  = (void*)handle_sub_reg;
    g_dispatch_table[OP_MUL_REG]  = (void*)handle_mul_reg;
    g_dispatch_table[OP_AND_REG]  = (void*)handle_and_reg;
    g_dispatch_table[OP_ORR_REG]  = (void*)handle_orr_reg;
    g_dispatch_table[OP_EOR_REG]  = (void*)handle_eor_reg;
    g_dispatch_table[OP_LSL_REG]  = (void*)handle_lsl_reg;
    g_dispatch_table[OP_LSR_REG]  = (void*)handle_lsr_reg;
    g_dispatch_table[OP_ASR_REG]  = (void*)handle_asr_reg;
    g_dispatch_table[OP_SDIV_REG] = (void*)handle_sdiv_reg;
    g_dispatch_table[OP_UDIV_REG] = (void*)handle_udiv_reg;
    g_dispatch_table[OP_ADD_IMM]  = (void*)handle_add_imm;
    g_dispatch_table[OP_SUB_IMM]  = (void*)handle_sub_imm;
    g_dispatch_table[OP_AND_IMM]  = (void*)handle_and_imm;
    g_dispatch_table[OP_ORR_IMM]  = (void*)handle_orr_imm;
    g_dispatch_table[OP_EOR_IMM]  = (void*)handle_eor_imm;
    g_dispatch_table[OP_LSL_IMM]  = (void*)handle_lsl_imm;
    g_dispatch_table[OP_LSR_IMM]  = (void*)handle_lsr_imm;
    g_dispatch_table[OP_ASR_IMM]  = (void*)handle_asr_imm;
    g_dispatch_table[OP_MOV_REG]    = (void*)handle_mov_reg;
    g_dispatch_table[OP_REV_REG]    = (void*)handle_rev_reg;
    g_dispatch_table[OP_REV16_REG]  = (void*)handle_rev16_reg;
    g_dispatch_table[OP_REV64_REG]  = (void*)handle_rev64_reg;
    g_dispatch_table[OP_MOVZ]       = (void*)handle_movz;
    g_dispatch_table[OP_MOVK]     = (void*)handle_movk;
    g_dispatch_table[OP_MOVN]     = (void*)handle_movn;
    g_dispatch_table[OP_ADDS_REG] = (void*)handle_adds_reg;
    g_dispatch_table[OP_SUBS_REG] = (void*)handle_subs_reg;
    g_dispatch_table[OP_ADDS_IMM] = (void*)handle_adds_imm;
    g_dispatch_table[OP_SUBS_IMM] = (void*)handle_subs_imm;
    g_dispatch_table[OP_ANDS_REG] = (void*)handle_ands_reg;
    g_dispatch_table[OP_ANDS_IMM] = (void*)handle_ands_imm;
    g_dispatch_table[OP_CMP_REG]  = (void*)handle_cmp_reg;
    g_dispatch_table[OP_CMP_IMM]  = (void*)handle_cmp_imm;
    g_dispatch_table[OP_CMN_REG]  = (void*)handle_cmn_reg;
    g_dispatch_table[OP_CMN_IMM]  = (void*)handle_cmn_imm;
    g_dispatch_table[OP_TST_REG]  = (void*)handle_tst_reg;
    g_dispatch_table[OP_TST_IMM]  = (void*)handle_tst_imm;
    g_dispatch_table[OP_CSEL]     = (void*)handle_csel;
    g_dispatch_table[OP_CSET]     = (void*)handle_cset;
    g_dispatch_table[OP_CSINC]    = (void*)handle_csinc;
    g_dispatch_table[OP_CSINV]    = (void*)handle_csinv;
    g_dispatch_table[OP_CSNEG]    = (void*)handle_csneg;
    g_dispatch_table[OP_LDR_IMM]  = (void*)handle_ldr_imm;
    g_dispatch_table[OP_STR_IMM]  = (void*)handle_str_imm;
    g_dispatch_table[OP_STRW_IMM] = (void*)handle_strw_imm;
    g_dispatch_table[OP_LDRB_IMM] = (void*)handle_ldrb_imm;
    g_dispatch_table[OP_STRB_IMM] = (void*)handle_strb_imm;
    g_dispatch_table[OP_LDRH_IMM] = (void*)handle_ldrh_imm;
    g_dispatch_table[OP_STRH_IMM] = (void*)handle_strh_imm;
    g_dispatch_table[OP_LDRSW_IMM]= (void*)handle_ldrsw_imm;
    g_dispatch_table[OP_LDRSB_IMM]= (void*)handle_ldrsb_imm;
    g_dispatch_table[OP_LDRSH_IMM]= (void*)handle_ldrsh_imm;
    g_dispatch_table[OP_LDR_REG]  = (void*)handle_ldr_reg;
    g_dispatch_table[OP_STR_REG]  = (void*)handle_str_reg;
    g_dispatch_table[OP_LDP]      = (void*)handle_ldp;
    g_dispatch_table[OP_STP]      = (void*)handle_stp;
    g_dispatch_table[OP_B]        = (void*)handle_b;
    g_dispatch_table[OP_BL]       = (void*)handle_bl;
    g_dispatch_table[OP_BLR]      = (void*)handle_blr;
    g_dispatch_table[OP_BR]       = (void*)handle_br;
    g_dispatch_table[OP_RET]      = (void*)handle_ret;
    g_dispatch_table[OP_CBZ]      = (void*)handle_cbz;
    g_dispatch_table[OP_CBNZ]     = (void*)handle_cbnz;
    g_dispatch_table[OP_TBZ]      = (void*)handle_tbz;
    g_dispatch_table[OP_TBNZ]     = (void*)handle_tbnz;
    g_dispatch_table[OP_B_COND]   = (void*)handle_b_cond;
}
#else
void* g_dispatch_table[OP_COUNT] = {nullptr};
void interpreter_init() {}
extern "C" void interpreter_run(VMState* state, const BytecodeInst* block) {
    if (!state || !block) return;
    const BytecodeInst* pc = block;
    while (true) {
        if (pc->opcode == OP_HLT) break;
        if (pc->opcode == OP_NOP) { pc++; continue; }
        // Simple C++ fallbacks for testing on host x86
        if (pc->opcode == OP_ADD_IMM) {
            state->write(pc->rd, state->read(pc->rn) + pc->imm32);
        } else if (pc->opcode == OP_ADD_REG) {
            state->write(pc->rd, state->read(pc->rn) + state->read(pc->rm));
        } else if (pc->opcode == OP_MOV_REG) {
            state->write(pc->rd, state->read(pc->rn));
        }
        pc++;
    }
}
#endif
