#include "Translator.h"
#include "Interpreter.h"
#include <capstone/capstone.h>
#include <capstone/aarch64.h>
#include <cstring>

static uint8_t regIdx(aarch64_reg r) {
    if (r >= AARCH64_REG_X0 && r <= AARCH64_REG_X28) return static_cast<uint8_t>(r - AARCH64_REG_X0);
    if (r == AARCH64_REG_FP || r == AARCH64_REG_X29) return 29;
    if (r == AARCH64_REG_LR || r == AARCH64_REG_X30) return 30;
    if (r == AARCH64_REG_XZR || r == AARCH64_REG_WZR) return 31;
    if (r == AARCH64_REG_SP || r == AARCH64_REG_WSP) return 32;
    if (r >= AARCH64_REG_W0 && r <= AARCH64_REG_W28) return static_cast<uint8_t>(r - AARCH64_REG_W0);
    if (r == AARCH64_REG_W29) return 29;
    if (r == AARCH64_REG_W30) return 30;
    return 31; // unknown / unsupported -> XZR
}

static inline bool is64BitReg(aarch64_reg r) {
    if (r >= AARCH64_REG_X0 && r <= AARCH64_REG_X28) return true;
    if (r == AARCH64_REG_FP || r == AARCH64_REG_X29) return true;
    if (r == AARCH64_REG_LR || r == AARCH64_REG_X30) return true;
    if (r == AARCH64_REG_XZR || r == AARCH64_REG_SP) return true;
    return false;
}

static uint8_t mapShift(aarch64_shifter s) {
    switch (s) {
        case AARCH64_SFT_LSL: return SH_LSL;
        case AARCH64_SFT_LSR: return SH_LSR;
        case AARCH64_SFT_ASR: return SH_ASR;
        case AARCH64_SFT_ROR: return SH_ROR;
        default: return SH_LSL;
    }
}

static uint8_t mapCond(AArch64CC_CondCode cc) {
    switch (cc) {
        case AArch64CC_EQ: return COND_EQ;
        case AArch64CC_NE: return COND_NE;
        case AArch64CC_HS: return COND_CS;
        case AArch64CC_LO: return COND_CC;
        case AArch64CC_MI: return COND_MI;
        case AArch64CC_PL: return COND_PL;
        case AArch64CC_VS: return COND_VS;
        case AArch64CC_VC: return COND_VC;
        case AArch64CC_HI: return COND_HI;
        case AArch64CC_LS: return COND_LS;
        case AArch64CC_GE: return COND_GE;
        case AArch64CC_LT: return COND_LT;
        case AArch64CC_GT: return COND_GT;
        case AArch64CC_LE: return COND_LE;
        case AArch64CC_AL: return COND_AL;
        case AArch64CC_NV: return COND_NV;
        default: return COND_AL;
    }
}

Translator::Translator() {
    csh handle;
    if (cs_open(CS_ARCH_AARCH64, CS_MODE_ARM, &handle) == CS_ERR_OK) {
        cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
        cs_handle_ = reinterpret_cast<void*>(handle);
    } else {
        cs_handle_ = nullptr;
    }
    interpreter_init();
}

Translator::~Translator() {
    if (cs_handle_) {
        csh handle = reinterpret_cast<csh>(cs_handle_);
        cs_close(&handle);
        cs_handle_ = nullptr;
    }
}

BytecodeBlock* Translator::translate(uint64_t guest_pc, const uint8_t* code, size_t size) {
    if (!cs_handle_ || !code || size == 0) return nullptr;

    csh handle = reinterpret_cast<csh>(cs_handle_);
    cs_insn* insns = nullptr;
    size_t count = cs_disasm(handle, code, size, guest_pc, 0, &insns);
    if (count == 0) return nullptr;

    BytecodeBlock block;
    block.guest_start = guest_pc;

    for (size_t i = 0; i < count; i++) {
        BytecodeInst inst = liftInsn(&insns[i]);
        block.insts.push_back(inst);
    }

    // Always emit OP_HLT as sentinel at end of block
    BytecodeInst hlt{};
    hlt.opcode = OP_HLT;
    block.insts.push_back(hlt);

    cs_free(insns, count);

    cache_[guest_pc] = std::move(block);
    return &cache_[guest_pc];
}

BytecodeBlock* Translator::getBlock(uint64_t guest_pc, const uint8_t* code, size_t size) {
    auto it = cache_.find(guest_pc);
    if (it != cache_.end()) {
        return &it->second;
    }
    return translate(guest_pc, code, size);
}

BytecodeInst Translator::liftInsn(const void* cs_insn_ptr) {
    const auto* insn = static_cast<const cs_insn*>(cs_insn_ptr);
    BytecodeInst inst{};
    const auto& aarch64 = insn->detail->aarch64;
    const auto& ops = aarch64.operands;
    uint8_t op_count = aarch64.op_count;

    // Capstone 6: aliases (CMP, CMN, TST, CSET, NOP, MOV) have their own ALIAS id.
    // For standard instructions (ADD, SUB, LDR, etc.), use the canonical insn->id.
    uint64_t dispatch_id = insn->id;
    if (insn->is_alias) {
        if (insn->alias_id == AARCH64_INS_ALIAS_CMP ||
            insn->alias_id == AARCH64_INS_ALIAS_CMN ||
            insn->alias_id == AARCH64_INS_ALIAS_TST ||
            insn->alias_id == AARCH64_INS_ALIAS_CSET ||
            insn->alias_id == AARCH64_INS_ALIAS_NOP ||
            insn->alias_id == AARCH64_INS_ALIAS_MOV ||
            insn->alias_id == AARCH64_INS_ALIAS_LSL ||
            insn->alias_id == AARCH64_INS_ALIAS_LSR ||
            insn->alias_id == AARCH64_INS_ALIAS_ASR) {
            dispatch_id = insn->alias_id;
        }
    }

    switch (dispatch_id) {
        case AARCH64_INS_ADD: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            if (ops[2].type == AARCH64_OP_IMM) {
                inst.opcode = OP_ADD_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[2].imm);
            } else {
                inst.opcode = OP_ADD_REG;
                inst.rm = regIdx(ops[2].reg);
                inst.shift_type = mapShift(ops[2].shift.type);
                inst.shift_amt = static_cast<uint8_t>(ops[2].shift.value);
            }
            break;
        }
        case AARCH64_INS_SUB: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            if (ops[2].type == AARCH64_OP_IMM) {
                inst.opcode = OP_SUB_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[2].imm);
            } else {
                inst.opcode = OP_SUB_REG;
                inst.rm = regIdx(ops[2].reg);
                inst.shift_type = mapShift(ops[2].shift.type);
                inst.shift_amt = static_cast<uint8_t>(ops[2].shift.value);
            }
            break;
        }
        case AARCH64_INS_MUL:
        case AARCH64_INS_MADD: {
            inst.opcode = OP_MUL_REG;
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            inst.rm = regIdx(ops[2].reg);
            break;
        }
        case AARCH64_INS_AND: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            if (ops[2].type == AARCH64_OP_IMM) {
                inst.opcode = OP_AND_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[2].imm);
            } else {
                inst.opcode = OP_AND_REG;
                inst.rm = regIdx(ops[2].reg);
                inst.shift_type = mapShift(ops[2].shift.type);
                inst.shift_amt = static_cast<uint8_t>(ops[2].shift.value);
            }
            break;
        }
        case AARCH64_INS_ORR: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            if (ops[2].type == AARCH64_OP_IMM) {
                inst.opcode = OP_ORR_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[2].imm);
            } else {
                inst.opcode = OP_ORR_REG;
                inst.rm = regIdx(ops[2].reg);
                inst.shift_type = mapShift(ops[2].shift.type);
                inst.shift_amt = static_cast<uint8_t>(ops[2].shift.value);
            }
            break;
        }
        case AARCH64_INS_EOR: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            if (ops[2].type == AARCH64_OP_IMM) {
                inst.opcode = OP_EOR_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[2].imm);
            } else {
                inst.opcode = OP_EOR_REG;
                inst.rm = regIdx(ops[2].reg);
                inst.shift_type = mapShift(ops[2].shift.type);
                inst.shift_amt = static_cast<uint8_t>(ops[2].shift.value);
            }
            break;
        }
        case AARCH64_INS_LSL:
        case AARCH64_INS_LSLR: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            if (ops[2].type == AARCH64_OP_IMM) {
                inst.opcode = OP_LSL_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[2].imm);
            } else {
                inst.opcode = OP_LSL_REG;
                inst.rm = regIdx(ops[2].reg);
            }
            break;
        }
        case AARCH64_INS_LSR:
        case AARCH64_INS_LSRR: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            if (ops[2].type == AARCH64_OP_IMM) {
                inst.opcode = OP_LSR_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[2].imm);
            } else {
                inst.opcode = OP_LSR_REG;
                inst.rm = regIdx(ops[2].reg);
            }
            break;
        }
        case AARCH64_INS_ASR:
        case AARCH64_INS_ASRR: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            if (ops[2].type == AARCH64_OP_IMM) {
                inst.opcode = OP_ASR_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[2].imm);
            } else {
                inst.opcode = OP_ASR_REG;
                inst.rm = regIdx(ops[2].reg);
            }
            break;
        }
        case AARCH64_INS_ALIAS_LSL:
        case AARCH64_INS_ALIAS_LSR:
        case AARCH64_INS_UBFM: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            if (op_count == 2) {
                if (ops[1].shift.type == AARCH64_SFT_LSR || dispatch_id == AARCH64_INS_ALIAS_LSR) {
                    inst.opcode = OP_LSR_IMM;
                    inst.imm32 = static_cast<uint32_t>(ops[1].shift.value);
                } else {
                    inst.opcode = OP_LSL_IMM;
                    inst.imm32 = static_cast<uint32_t>(ops[1].shift.value);
                }
            } else if (op_count == 3) {
                inst.opcode = (dispatch_id == AARCH64_INS_ALIAS_LSR) ? OP_LSR_IMM : OP_LSL_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[2].imm);
            } else if (op_count == 4) {
                uint32_t immr = static_cast<uint32_t>(ops[2].imm);
                uint32_t imms = static_cast<uint32_t>(ops[3].imm);
                uint32_t width = is64BitReg(ops[0].reg) ? 64 : 32;
                if (imms != (width - 1)) {
                    inst.opcode = OP_LSL_IMM;
                    inst.imm32 = (width - immr) & (width - 1);
                } else {
                    inst.opcode = OP_LSR_IMM;
                    inst.imm32 = immr;
                }
            }
            break;
        }
        case AARCH64_INS_ALIAS_ASR:
        case AARCH64_INS_SBFM: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            if (op_count == 2) {
                inst.opcode = OP_ASR_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[1].shift.value);
            } else if (op_count == 3) {
                inst.opcode = OP_ASR_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[2].imm);
            } else if (op_count == 4) {
                uint32_t immr = static_cast<uint32_t>(ops[2].imm);
                inst.opcode = OP_ASR_IMM;
                inst.imm32 = immr;
            }
            break;
        }
        case AARCH64_INS_SDIV: {
            inst.opcode = OP_SDIV_REG;
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            inst.rm = regIdx(ops[2].reg);
            break;
        }
        case AARCH64_INS_UDIV: {
            inst.opcode = OP_UDIV_REG;
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            inst.rm = regIdx(ops[2].reg);
            break;
        }
        case AARCH64_INS_REV: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            inst.opcode = is64BitReg(ops[0].reg) ? OP_REV64_REG : OP_REV_REG;
            break;
        }
        case AARCH64_INS_REV16: {
            inst.opcode = OP_REV16_REG;
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            break;
        }
        case AARCH64_INS_REV32: {
            inst.opcode = OP_REV_REG;
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            break;
        }
        case AARCH64_INS_REV64: {
            inst.opcode = OP_REV64_REG;
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            break;
        }
        case AARCH64_INS_MOV:
        case AARCH64_INS_ALIAS_MOV: {
            inst.rd = regIdx(ops[0].reg);
            if (ops[1].type == AARCH64_OP_REG) {
                inst.opcode = OP_MOV_REG;
                inst.rn = regIdx(ops[1].reg);
            } else {
                if (ops[1].imm < 0) {
                    inst.opcode = OP_MOVN;
                    uint64_t val = static_cast<uint64_t>(ops[1].imm);
                    uint64_t inv = ~val;
                    uint8_t shift = 0;
                    while (shift < 4 && (inv & 0xFFFF) != inv) {
                        inv >>= 16;
                        shift++;
                    }
                    inst.imm32 = static_cast<uint32_t>(inv & 0xFFFF) | (static_cast<uint32_t>(shift) << 16);
                } else {
                    inst.opcode = OP_MOVZ;
                    inst.imm32 = static_cast<uint32_t>(ops[1].imm);
                }
            }
            break;
        }
        case AARCH64_INS_MOVZ: {
            inst.opcode = OP_MOVZ;
            inst.rd = regIdx(ops[0].reg);
            inst.imm32 = static_cast<uint32_t>(ops[1].imm);
            break;
        }
        case AARCH64_INS_MOVK: {
            inst.opcode = OP_MOVK;
            inst.rd = regIdx(ops[0].reg);
            uint8_t shift_val = static_cast<uint8_t>(ops[1].shift.value / 16);
            uint16_t imm16 = static_cast<uint16_t>(ops[1].imm);
            inst.imm32 = static_cast<uint32_t>(imm16) | (static_cast<uint32_t>(shift_val) << 16);
            break;
        }
        case AARCH64_INS_MOVN: {
            inst.opcode = OP_MOVN;
            inst.rd = regIdx(ops[0].reg);
            uint8_t shift_val = static_cast<uint8_t>(ops[1].shift.value / 16);
            uint16_t imm16 = static_cast<uint16_t>(ops[1].imm);
            inst.imm32 = static_cast<uint32_t>(imm16) | (static_cast<uint32_t>(shift_val) << 16);
            break;
        }
        case AARCH64_INS_ADDS: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            if (ops[2].type == AARCH64_OP_IMM) {
                inst.opcode = OP_ADDS_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[2].imm);
            } else {
                inst.opcode = OP_ADDS_REG;
                inst.rm = regIdx(ops[2].reg);
                inst.shift_type = mapShift(ops[2].shift.type);
                inst.shift_amt = static_cast<uint8_t>(ops[2].shift.value);
            }
            break;
        }
        case AARCH64_INS_SUBS: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            if (ops[2].type == AARCH64_OP_IMM) {
                inst.opcode = OP_SUBS_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[2].imm);
            } else {
                inst.opcode = OP_SUBS_REG;
                inst.rm = regIdx(ops[2].reg);
                inst.shift_type = mapShift(ops[2].shift.type);
                inst.shift_amt = static_cast<uint8_t>(ops[2].shift.value);
            }
            break;
        }
        case AARCH64_INS_ANDS: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            if (ops[2].type == AARCH64_OP_IMM) {
                inst.opcode = OP_ANDS_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[2].imm);
            } else {
                inst.opcode = OP_ANDS_REG;
                inst.rm = regIdx(ops[2].reg);
                inst.shift_type = mapShift(ops[2].shift.type);
                inst.shift_amt = static_cast<uint8_t>(ops[2].shift.value);
            }
            break;
        }
        case AARCH64_INS_ALIAS_CMP: {
            inst.rn = regIdx(ops[0].reg);
            if (ops[1].type == AARCH64_OP_IMM) {
                inst.opcode = OP_CMP_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[1].imm);
            } else {
                inst.opcode = OP_CMP_REG;
                inst.rm = regIdx(ops[1].reg);
                inst.shift_type = mapShift(ops[1].shift.type);
                inst.shift_amt = static_cast<uint8_t>(ops[1].shift.value);
            }
            break;
        }
        case AARCH64_INS_ALIAS_CMN: {
            inst.rn = regIdx(ops[0].reg);
            if (ops[1].type == AARCH64_OP_IMM) {
                inst.opcode = OP_CMN_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[1].imm);
            } else {
                inst.opcode = OP_CMN_REG;
                inst.rm = regIdx(ops[1].reg);
                inst.shift_type = mapShift(ops[1].shift.type);
                inst.shift_amt = static_cast<uint8_t>(ops[1].shift.value);
            }
            break;
        }
        case AARCH64_INS_ALIAS_TST: {
            inst.rn = regIdx(ops[0].reg);
            if (ops[1].type == AARCH64_OP_IMM) {
                inst.opcode = OP_TST_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[1].imm);
            } else {
                inst.opcode = OP_TST_REG;
                inst.rm = regIdx(ops[1].reg);
                inst.shift_type = mapShift(ops[1].shift.type);
                inst.shift_amt = static_cast<uint8_t>(ops[1].shift.value);
            }
            break;
        }
        case AARCH64_INS_CSEL: {
            inst.opcode = OP_CSEL;
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            inst.rm = regIdx(ops[2].reg);
            inst.ext_flags = mapCond(aarch64.cc);
            break;
        }
        case AARCH64_INS_ALIAS_CSET: {
            inst.opcode = OP_CSET;
            inst.rd = regIdx(ops[0].reg);
            inst.ext_flags = mapCond(aarch64.cc);
            break;
        }
        case AARCH64_INS_CSINC: {
            inst.opcode = OP_CSINC;
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            inst.rm = regIdx(ops[2].reg);
            inst.ext_flags = mapCond(aarch64.cc);
            break;
        }
        case AARCH64_INS_CSINV: {
            inst.opcode = OP_CSINV;
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            inst.rm = regIdx(ops[2].reg);
            inst.ext_flags = mapCond(aarch64.cc);
            break;
        }
        case AARCH64_INS_CSNEG: {
            inst.opcode = OP_CSNEG;
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].reg);
            inst.rm = regIdx(ops[2].reg);
            inst.ext_flags = mapCond(aarch64.cc);
            break;
        }
        case AARCH64_INS_LDR:
        case AARCH64_INS_LDRB:
        case AARCH64_INS_LDRH:
        case AARCH64_INS_LDRSW:
        case AARCH64_INS_LDRSB:
        case AARCH64_INS_LDRSH: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].mem.base);
            if (ops[1].mem.index != AARCH64_REG_INVALID) {
                inst.opcode = OP_LDR_REG;
                inst.rm = regIdx(ops[1].mem.index);
                inst.shift_type = mapShift(ops[1].shift.type);
                inst.shift_amt = static_cast<uint8_t>(ops[1].shift.value);
            } else {
                bool is_w = (ops[0].reg >= AARCH64_REG_W0 && ops[0].reg <= AARCH64_REG_W30) || (ops[0].reg == AARCH64_REG_WZR);
                if (insn->id == AARCH64_INS_LDRB) inst.opcode = OP_LDRB_IMM;
                else if (insn->id == AARCH64_INS_LDRH) inst.opcode = OP_LDRH_IMM;
                else if (insn->id == AARCH64_INS_LDRSW) inst.opcode = OP_LDRSW_IMM;
                else if (insn->id == AARCH64_INS_LDRSB) inst.opcode = OP_LDRSB_IMM;
                else if (insn->id == AARCH64_INS_LDRSH) inst.opcode = OP_LDRSH_IMM;
                else inst.opcode = is_w ? OP_LDRSW_IMM : OP_LDR_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[1].mem.disp);
            }
            break;
        }
        case AARCH64_INS_STR:
        case AARCH64_INS_STRB:
        case AARCH64_INS_STRH: {
            inst.rd = regIdx(ops[0].reg);
            inst.rn = regIdx(ops[1].mem.base);
            if (ops[1].mem.index != AARCH64_REG_INVALID) {
                inst.opcode = OP_STR_REG;
                inst.rm = regIdx(ops[1].mem.index);
                inst.shift_type = mapShift(ops[1].shift.type);
                inst.shift_amt = static_cast<uint8_t>(ops[1].shift.value);
            } else {
                bool is_w = (ops[0].reg >= AARCH64_REG_W0 && ops[0].reg <= AARCH64_REG_W30) || (ops[0].reg == AARCH64_REG_WZR);
                if (insn->id == AARCH64_INS_STRB) inst.opcode = OP_STRB_IMM;
                else if (insn->id == AARCH64_INS_STRH) inst.opcode = OP_STRH_IMM;
                else inst.opcode = is_w ? OP_STRW_IMM : OP_STR_IMM;
                inst.imm32 = static_cast<uint32_t>(ops[1].mem.disp);
            }
            break;
        }
        case AARCH64_INS_LDP: {
            inst.opcode = OP_LDP;
            inst.rd = regIdx(ops[0].reg);
            inst.reg2 = regIdx(ops[1].reg);
            inst.rn = regIdx(ops[2].mem.base);
            inst.mem_disp = static_cast<int16_t>(ops[2].mem.disp);
            break;
        }
        case AARCH64_INS_STP: {
            inst.opcode = OP_STP;
            inst.rd = regIdx(ops[0].reg);
            inst.reg2 = regIdx(ops[1].reg);
            inst.rn = regIdx(ops[2].mem.base);
            inst.mem_disp = static_cast<int16_t>(ops[2].mem.disp);
            break;
        }
        case AARCH64_INS_B: {
            int64_t target = ops[0].imm;
            int64_t next_pc = static_cast<int64_t>(insn->address) + 4;
            int32_t byte_offset = static_cast<int32_t>((target - next_pc) / 4 * sizeof(BytecodeInst));

            if (aarch64.cc != AArch64CC_Invalid && aarch64.cc != AArch64CC_AL && aarch64.cc != AArch64CC_NV) {
                inst.opcode = OP_B_COND;
                inst.ext_flags = mapCond(aarch64.cc);
                inst.imm32 = static_cast<uint32_t>(byte_offset & 0x00FFFFFF);
            } else {
                inst.opcode = OP_B;
                inst.imm32 = static_cast<uint32_t>(byte_offset);
            }
            break;
        }
        case AARCH64_INS_BL: {
            inst.opcode = OP_BL;
            int64_t target = ops[0].imm;
            int64_t next_pc = static_cast<int64_t>(insn->address) + 4;
            int32_t byte_offset = static_cast<int32_t>((target - next_pc) / 4 * sizeof(BytecodeInst));
            inst.imm32 = static_cast<uint32_t>(byte_offset);
            break;
        }
        case AARCH64_INS_BLR: {
            inst.opcode = OP_BLR;
            inst.rn = regIdx(ops[0].reg);
            break;
        }
        case AARCH64_INS_BR: {
            inst.opcode = OP_BR;
            inst.rn = regIdx(ops[0].reg);
            break;
        }
        case AARCH64_INS_RET: {
            inst.opcode = OP_RET;
            inst.rn = (op_count > 0) ? regIdx(ops[0].reg) : 30;
            break;
        }
        case AARCH64_INS_CBZ: {
            inst.opcode = OP_CBZ;
            inst.rn = regIdx(ops[0].reg);
            int64_t target = ops[1].imm;
            int64_t next_pc = static_cast<int64_t>(insn->address) + 4;
            int32_t byte_offset = static_cast<int32_t>((target - next_pc) / 4 * sizeof(BytecodeInst));
            inst.imm32 = static_cast<uint32_t>(byte_offset);
            break;
        }
        case AARCH64_INS_CBNZ: {
            inst.opcode = OP_CBNZ;
            inst.rn = regIdx(ops[0].reg);
            int64_t target = ops[1].imm;
            int64_t next_pc = static_cast<int64_t>(insn->address) + 4;
            int32_t byte_offset = static_cast<int32_t>((target - next_pc) / 4 * sizeof(BytecodeInst));
            inst.imm32 = static_cast<uint32_t>(byte_offset);
            break;
        }
        case AARCH64_INS_TBZ: {
            inst.opcode = OP_TBZ;
            inst.rn = regIdx(ops[0].reg);
            inst.rd = static_cast<uint8_t>(ops[1].imm);
            int64_t target = ops[2].imm;
            int64_t next_pc = static_cast<int64_t>(insn->address) + 4;
            int32_t byte_offset = static_cast<int32_t>((target - next_pc) / 4 * sizeof(BytecodeInst));
            inst.imm32 = static_cast<uint32_t>(byte_offset & 0x00FFFFFF);
            break;
        }
        case AARCH64_INS_TBNZ: {
            inst.opcode = OP_TBNZ;
            inst.rn = regIdx(ops[0].reg);
            inst.rd = static_cast<uint8_t>(ops[1].imm);
            int64_t target = ops[2].imm;
            int64_t next_pc = static_cast<int64_t>(insn->address) + 4;
            int32_t byte_offset = static_cast<int32_t>((target - next_pc) / 4 * sizeof(BytecodeInst));
            inst.imm32 = static_cast<uint32_t>(byte_offset & 0x00FFFFFF);
            break;
        }
        case AARCH64_INS_HINT:
        case AARCH64_INS_ALIAS_NOP: {
            inst.opcode = OP_NOP;
            break;
        }
        default: {
            inst.opcode = OP_NOP;
            break;
        }
    }
    return inst;
}
