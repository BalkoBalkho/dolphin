#pragma once
#include <cstdint>

// 8-byte fixed-width bytecode instruction.
// 8 instructions fit in a single 64-byte L1 cache line.
struct BytecodeInst {
    uint16_t opcode;
    uint8_t  rd;   // destination register index (0..30; 31=XZR/SP)
    uint8_t  rn;   // source register 1 index
    union {
        uint32_t imm32;  // immediate (sign-extended when needed)
        struct {
            uint8_t  rm;
            uint8_t  shift_type;  // 0=LSL 1=LSR 2=ASR 3=ROR
            uint8_t  shift_amt;
            uint8_t  ext_flags;   // condition code for B.cond stored here
        };
        struct __attribute__((packed)) {
            uint8_t  reg2;        // byte 4 (for LDP/STP second register)
            int16_t  mem_disp;    // bytes 5..6 (for LDP/STP signed displacement)
            uint8_t  _pad;
        };
    };
};
static_assert(sizeof(BytecodeInst) == 8);

// Condition codes (stored in ext_flags for B.cond, CSEL, etc.)
enum CondCode : uint8_t {
    COND_EQ = 0, COND_NE, COND_CS, COND_CC,
    COND_MI, COND_PL, COND_VS, COND_VC,
    COND_HI, COND_LS, COND_GE, COND_LT,
    COND_GT, COND_LE, COND_AL, COND_NV
};

// Shift types
enum ShiftType : uint8_t { SH_LSL = 0, SH_LSR = 1, SH_ASR = 2, SH_ROR = 3 };

// All bytecode opcodes
enum Opcode : uint16_t {
    // --- Halt / NOP ---
    OP_HLT = 0,
    OP_NOP,

    // --- ALU register (rd = rn OP rm, with optional shift) ---
    OP_ADD_REG,
    OP_SUB_REG,
    OP_MUL_REG,
    OP_AND_REG,
    OP_ORR_REG,
    OP_EOR_REG,
    OP_LSL_REG,
    OP_LSR_REG,
    OP_ASR_REG,
    OP_SDIV_REG,
    OP_UDIV_REG,

    // --- ALU immediate (rd = rn OP imm32) ---
    OP_ADD_IMM,
    OP_SUB_IMM,
    OP_AND_IMM,
    OP_ORR_IMM,
    OP_EOR_IMM,
    OP_LSL_IMM,
    OP_LSR_IMM,
    OP_ASR_IMM,

    // --- Move / Reversal ---
    OP_MOV_REG,   // rd = rn
    OP_REV_REG,   // rd = bswap32(rn)
    OP_REV16_REG, // rd = bswap16(rn)
    OP_REV64_REG, // rd = bswap64(rn)
    OP_MOVZ,      // rd = imm32 << (shift*16) — shift stored in shift_amt
    OP_MOVK,      // rd |= imm32 << (shift*16) keeping other bits
    OP_MOVN,      // rd = ~(imm32 << (shift*16))

    // --- Flags-setting ALU (also update NZCV) ---
    OP_ADDS_REG,
    OP_SUBS_REG,
    OP_ADDS_IMM,
    OP_SUBS_IMM,
    OP_ANDS_REG,
    OP_ANDS_IMM,
    OP_CMP_REG,   // SUBS to XZR — just update flags, discard result
    OP_CMP_IMM,
    OP_CMN_REG,
    OP_CMN_IMM,
    OP_TST_REG,
    OP_TST_IMM,

    // --- Conditional select / set ---
    OP_CSEL,   // rd = (cond) ? rn : rm
    OP_CSET,   // rd = (cond) ? 1 : 0
    OP_CSINC,  // rd = (cond) ? rn : rm+1
    OP_CSINV,  // rd = (cond) ? rn : ~rm
    OP_CSNEG,  // rd = (cond) ? rn : -rm

    // --- Memory (base in rn, imm32=offset, rd=data reg) ---
    OP_LDR_IMM,
    OP_STR_IMM,
    OP_STRW_IMM,
    OP_LDRB_IMM,
    OP_STRB_IMM,
    OP_LDRH_IMM,
    OP_STRH_IMM,
    OP_LDRSW_IMM,
    OP_LDRSB_IMM,
    OP_LDRSH_IMM,
    OP_LDR_REG,
    OP_STR_REG,

    // --- Pair loads/stores (rd=reg1, rn=base, rm=reg2, imm32=offset) ---
    OP_LDP,
    OP_STP,

    // --- Branching ---
    OP_B,     // unconditional relative branch — imm32 = byte offset from start of NEXT instruction
    OP_BL,    // branch with link (X30 = return addr)
    OP_BLR,   // branch with link to register rn
    OP_BR,    // branch to register rn
    OP_RET,   // return to X30 (or rn if nonzero)
    OP_CBZ,   // branch if regs[rn] == 0, imm32=offset
    OP_CBNZ,  // branch if regs[rn] != 0
    OP_TBZ,   // branch if bit rm of regs[rn] == 0
    OP_TBNZ,
    OP_B_COND, // branch if condition met — cond in ext_flags

    OP_COUNT   // must be last
};
