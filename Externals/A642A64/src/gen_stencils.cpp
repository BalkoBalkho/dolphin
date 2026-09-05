#include "oaknut/oaknut.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>

using namespace oaknut;
using namespace oaknut::util;

inline WReg PinnedRegW(int idx) {
    switch (idx) {
        case 0: return W23;
        case 1: return W21;
        case 2: return W22;
        case 3: return W24;
        case 4: return W25;
        default: return WZR;
    }
}

inline XReg PinnedRegX(int idx) {
    switch (idx) {
        case 0: return X23;
        case 1: return X21;
        case 2: return X22;
        case 3: return X24;
        case 4: return X25;
        default: return ZR;
    }
}

inline DReg PinnedRegD(int idx) {
    switch (idx) {
        case 0: return D0;
        case 1: return D1;
        case 2: return D2;
        case 3: return D3;
        case 4: return D4;
        default: return D0;
    }
}

inline SReg PinnedRegS(int idx) {
    switch (idx) {
        case 0: return S0;
        case 1: return S1;
        case 2: return S2;
        case 3: return S3;
        case 4: return S4;
        default: return S0;
    }
}

inline VReg_2S PinnedRegV2S(int idx) {
    switch (idx) {
        case 0: return V0.S2();
        case 1: return V1.S2();
        case 2: return V2.S2();
        case 3: return V3.S2();
        case 4: return V4.S2();
        default: return V0.S2();
    }
}

inline void EmitTailCall(VectorCodeGenerator& c) {
    c.LDR(X16, X20, PRE_INDEXED, 16);
    c.BR(X16);
}

inline void EmitRc(VectorCodeGenerator& c, WReg reg_w) {
    c.SXTW(X16, reg_w);
}

inline void LoadCarry(VectorCodeGenerator& c) {
    c.LDRB(W0, X19, 288);
    c.CMP(W0, 1);
}

inline void SaveCarry(VectorCodeGenerator& c) {
    c.CSET(W1, CS);
    c.STRB(W1, X19, 288);
}

struct PPCInstrDef {
    uint32_t    ppc_opcode;
    uint32_t    ppc_subopcode;
    const char* name;
    bool        has_Rc;
    bool        has_rb;
    bool        is_load;
    bool        is_store;
    void (*emit_fn)(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb);
};

static void emit_addi(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    if (ra == 0) {
        c.MOV(wD, W0);
    } else {
        c.ADD(wD, wA, W0);
    }
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_addis(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    emit_addi(c, Rc, rd, ra, rb);
}

static void emit_addic(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.ADDS(wD, wA, W0);
    SaveCarry(c);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_subfic(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.SUBS(wD, W0, wA);
    SaveCarry(c);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_mulli(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.MUL(wD, wA, W0);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_add(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.ADD(wD, wA, wB);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_addc(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.ADDS(wD, wA, wB);
    SaveCarry(c);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_adde(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    LoadCarry(c);
    c.ADCS(wD, wA, wB);
    SaveCarry(c);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_addme(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    LoadCarry(c);
    c.MOVN(W2, 0);
    c.ADCS(wD, wA, W2);
    SaveCarry(c);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_addze(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    LoadCarry(c);
    c.ADCS(wD, wA, WZR);
    SaveCarry(c);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_subf(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.SUB(wD, wB, wA);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_subfc(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.SUBS(wD, wB, wA);
    SaveCarry(c);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_subfe(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    LoadCarry(c);
    c.SBCS(wD, wB, wA);
    SaveCarry(c);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_subfme(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    LoadCarry(c);
    c.MOVN(W2, 0);
    c.SBCS(wD, W2, wA);
    SaveCarry(c);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_subfze(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    LoadCarry(c);
    c.SBCS(wD, WZR, wA);
    SaveCarry(c);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_neg(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.NEG(wD, wA);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_mullw(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.MUL(wD, wA, wB);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_mulhw(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    XReg xD = PinnedRegX(rd);
    c.SMULL(xD, wA, wB);
    c.LSR(xD, xD, 32);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_mulhwu(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    XReg xD = PinnedRegX(rd);
    c.UMULL(xD, wA, wB);
    c.LSR(xD, xD, 32);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_divw(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.SDIV(wD, wA, wB);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_divwu(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.UDIV(wD, wA, wB);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_and(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.AND(wD, wA, wB);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_andc(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.BIC(wD, wA, wB);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_or(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == rb) {
        c.MOV(wD, wA);
    } else {
        c.ORR(wD, wA, wB);
    }
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_orc(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.ORN(wD, wA, wB);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_xor(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.EOR(wD, wA, wB);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_nand(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.AND(W0, wA, wB);
    c.MVN(wD, W0);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_nor(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == rb) {
        c.MVN(wD, wA);
    } else {
        c.ORR(W0, wA, wB);
        c.MVN(wD, W0);
    }
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_eqv(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.EON(wD, wA, wB);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_ori(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.ORR(wD, wA, W0);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_oris(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    emit_ori(c, Rc, rd, ra, rb);
}

static void emit_xori(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.EOR(wD, wA, W0);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_xoris(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    emit_xori(c, Rc, rd, ra, rb);
}

static void emit_andi(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.AND(wD, wA, W0);
    EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_andis(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    emit_andi(c, Rc, rd, ra, rb);
}

static void emit_extsb(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.SXTB(wD, wA);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_extsh(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.SXTH(wD, wA);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_cntlzw(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.CLZ(wD, wA);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

// -------------------------------------------------------------
// 2. Rotate / Shift
// -------------------------------------------------------------

static void emit_slw(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.LSL(wD, wA, wB);
    c.TST(wB, 32);
    c.CSEL(wD, WZR, wD, NE);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_srw(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.LSR(wD, wA, wB);
    c.TST(wB, 32);
    c.CSEL(wD, WZR, wD, NE);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_sraw(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.ASR(wD, wA, wB);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_srawi(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.ASR(wD, wA, W0);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_rlwinm(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.LDR(W1, X20, 12);
    c.ROR(wD, wA, W1);
    c.AND(wD, wD, W0);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_rlwimi(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.LDR(W1, X20, 12);
    c.ROR(W2, wA, W1);
    c.BIC(wD, wD, W0);
    c.AND(W2, W2, W0);
    c.ORR(wD, wD, W2);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

static void emit_rlwnm(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.LDR(W0, X20, 8);
    c.ROR(wD, wA, wB);
    c.AND(wD, wD, W0);
    if (Rc) EmitRc(c, wD);
    EmitTailCall(c);
}

// -------------------------------------------------------------
// 3. Compares
// -------------------------------------------------------------

static void emit_cmp(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.CMP(wA, wB);
    EmitTailCall(c);
}

static void emit_cmpl(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.CMP(wA, wB);
    EmitTailCall(c);
}

static void emit_cmpi(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.CMP(wA, W0);
    EmitTailCall(c);
}

static void emit_cmpli(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.CMP(wA, W0);
    EmitTailCall(c);
}

// -------------------------------------------------------------
// 4. Integer Loads & Stores
// -------------------------------------------------------------

static void emit_lwz(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.LDR(wD, X28, W0, IndexExt::UXTW);
    c.REV(wD, wD);
    EmitTailCall(c);
}

static void emit_lwzu(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.ADD(wA, wA, W0);
    c.LDR(wD, X28, wA, IndexExt::UXTW);
    c.REV(wD, wD);
    EmitTailCall(c);
}

static void emit_lwzx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.LDR(wD, X28, W0, IndexExt::UXTW);
    c.REV(wD, wD);
    EmitTailCall(c);
}

static void emit_lwzux(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.ADD(wA, wA, wB);
    c.LDR(wD, X28, wA, IndexExt::UXTW);
    c.REV(wD, wD);
    EmitTailCall(c);
}

static void emit_lbz(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.LDRB(wD, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_lbzu(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.ADD(wA, wA, W0);
    c.LDRB(wD, X28, wA, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_lbzx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.LDRB(wD, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_lbzux(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.ADD(wA, wA, wB);
    c.LDRB(wD, X28, wA, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_lhz(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.LDRH(wD, X28, W0, IndexExt::UXTW);
    c.REV16(wD, wD);
    EmitTailCall(c);
}

static void emit_lhzu(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.ADD(wA, wA, W0);
    c.LDRH(wD, X28, wA, IndexExt::UXTW);
    c.REV16(wD, wD);
    EmitTailCall(c);
}

static void emit_lhzx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.LDRH(wD, X28, W0, IndexExt::UXTW);
    c.REV16(wD, wD);
    EmitTailCall(c);
}

static void emit_lhzux(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.ADD(wA, wA, wB);
    c.LDRH(wD, X28, wA, IndexExt::UXTW);
    c.REV16(wD, wD);
    EmitTailCall(c);
}

static void emit_lha(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.LDRH(wD, X28, W0, IndexExt::UXTW);
    c.REV16(wD, wD);
    c.SXTH(wD, wD);
    EmitTailCall(c);
}

static void emit_lhau(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.ADD(wA, wA, W0);
    c.LDRH(wD, X28, wA, IndexExt::UXTW);
    c.REV16(wD, wD);
    c.SXTH(wD, wD);
    EmitTailCall(c);
}

static void emit_lhax(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.LDRH(wD, X28, W0, IndexExt::UXTW);
    c.REV16(wD, wD);
    c.SXTH(wD, wD);
    EmitTailCall(c);
}

static void emit_lhaux(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.ADD(wA, wA, wB);
    c.LDRH(wD, X28, wA, IndexExt::UXTW);
    c.REV16(wD, wD);
    c.SXTH(wD, wD);
    EmitTailCall(c);
}

static void emit_lwbrx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.LDR(wD, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_lhbrx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.LDRH(wD, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stw(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.REV(W1, wD);
    c.STR(W1, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stwu(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.ADD(wA, wA, W0);
    c.REV(W1, wD);
    c.STR(W1, X28, wA, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stwx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.REV(W1, wD);
    c.STR(W1, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stwux(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.ADD(wA, wA, wB);
    c.REV(W1, wD);
    c.STR(W1, X28, wA, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stb(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.STRB(wD, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stbu(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.ADD(wA, wA, W0);
    c.STRB(wD, X28, wA, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stbx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.STRB(wD, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stbux(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.ADD(wA, wA, wB);
    c.STRB(wD, X28, wA, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_sth(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.REV16(W1, wD);
    c.STRH(W1, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_sthu(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.ADD(wA, wA, W0);
    c.REV16(W1, wD);
    c.STRH(W1, X28, wA, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_sthx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.REV16(W1, wD);
    c.STRH(W1, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_sthux(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.ADD(wA, wA, wB);
    c.REV16(W1, wD);
    c.STRH(W1, X28, wA, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stwbrx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.STR(wD, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_sthbrx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.STRH(wD, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

// -------------------------------------------------------------
// 5. Floating Point Loads & Stores & Ops
// -------------------------------------------------------------

static void emit_lfs(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.LDR(W1, X28, W0, IndexExt::UXTW);
    c.REV(W1, W1);
    c.STR(W1, X19, 296 + rd * 8);
    EmitTailCall(c);
}

static void emit_lfsu(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.ADD(wA, wA, W0);
    c.LDR(W1, X28, wA, IndexExt::UXTW);
    c.REV(W1, W1);
    c.STR(W1, X19, 296 + rd * 8);
    EmitTailCall(c);
}

static void emit_lfsx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.LDR(W1, X28, W0, IndexExt::UXTW);
    c.REV(W1, W1);
    c.STR(W1, X19, 296 + rd * 8);
    EmitTailCall(c);
}

static void emit_lfd(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.LDR(X1, X28, W0, IndexExt::UXTW);
    c.REV(X1, X1);
    c.STR(X1, X19, 296 + rd * 8);
    EmitTailCall(c);
}

static void emit_lfdu(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.ADD(wA, wA, W0);
    c.LDR(X1, X28, wA, IndexExt::UXTW);
    c.REV(X1, X1);
    c.STR(X1, X19, 296 + rd * 8);
    EmitTailCall(c);
}

static void emit_lfdx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.LDR(X1, X28, W0, IndexExt::UXTW);
    c.REV(X1, X1);
    c.STR(X1, X19, 296 + rd * 8);
    EmitTailCall(c);
}

static void emit_stfs(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(W1, X19, 296 + rd * 8);
    c.REV(W1, W1);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.STR(W1, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stfsu(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(W1, X19, 296 + rd * 8);
    c.REV(W1, W1);
    c.LDR(W0, X20, 8);
    c.ADD(wA, wA, W0);
    c.STR(W1, X28, wA, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stfsx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.LDR(W1, X19, 296 + rd * 8);
    c.REV(W1, W1);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.STR(W1, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stfd(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(X1, X19, 296 + rd * 8);
    c.REV(X1, X1);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.STR(X1, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stfdu(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(X1, X19, 296 + rd * 8);
    c.REV(X1, X1);
    c.LDR(W0, X20, 8);
    c.ADD(wA, wA, W0);
    c.STR(X1, X28, wA, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_stfdx(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    WReg wB = PinnedRegW(rb);
    c.LDR(X1, X19, 296 + rd * 8);
    c.REV(X1, X1);
    if (ra == 0) {
        c.MOV(W0, wB);
    } else {
        c.ADD(W0, wA, wB);
    }
    c.STR(X1, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_fadd(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    DReg dD = PinnedRegD(rd);
    DReg dA = PinnedRegD(ra);
    DReg dB = PinnedRegD(rb);
    c.FADD(dD, dA, dB);
    EmitTailCall(c);
}

static void emit_fsub(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    DReg dD = PinnedRegD(rd);
    DReg dA = PinnedRegD(ra);
    DReg dB = PinnedRegD(rb);
    c.FSUB(dD, dA, dB);
    EmitTailCall(c);
}

static void emit_fmul(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    DReg dD = PinnedRegD(rd);
    DReg dA = PinnedRegD(ra);
    DReg dB = PinnedRegD(rb);
    c.FMUL(dD, dA, dB);
    EmitTailCall(c);
}

static void emit_fdiv(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    DReg dD = PinnedRegD(rd);
    DReg dA = PinnedRegD(ra);
    DReg dB = PinnedRegD(rb);
    c.FDIV(dD, dA, dB);
    EmitTailCall(c);
}

static void emit_fmadd(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    DReg dD = PinnedRegD(rd);
    DReg dA = PinnedRegD(ra);
    DReg dB = PinnedRegD(rb);
    c.FMADD(dD, dA, dB, dD);
    EmitTailCall(c);
}

static void emit_fmsub(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    DReg dD = PinnedRegD(rd);
    DReg dA = PinnedRegD(ra);
    DReg dB = PinnedRegD(rb);
    c.FMSUB(dD, dA, dB, dD);
    EmitTailCall(c);
}

static void emit_fneg(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    DReg dD = PinnedRegD(rd);
    DReg dA = PinnedRegD(ra);
    c.FNEG(dD, dA);
    EmitTailCall(c);
}

static void emit_fabs(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    DReg dD = PinnedRegD(rd);
    DReg dA = PinnedRegD(ra);
    c.FABS(dD, dA);
    EmitTailCall(c);
}

static void emit_fnabs(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    DReg dD = PinnedRegD(rd);
    DReg dA = PinnedRegD(ra);
    c.FABS(dD, dA);
    c.FNEG(dD, dD);
    EmitTailCall(c);
}

static void emit_fcmpu(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    DReg dA = PinnedRegD(ra);
    DReg dB = PinnedRegD(rb);
    c.FCMP(dA, dB);
    EmitTailCall(c);
}

static void emit_fcmpo(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    DReg dA = PinnedRegD(ra);
    DReg dB = PinnedRegD(rb);
    c.FCMP(dA, dB);
    EmitTailCall(c);
}

static void emit_fctiw(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    DReg dD = PinnedRegD(rd);
    DReg dA = PinnedRegD(ra);
    c.FRINTI(dD, dA);
    c.FCVTZS(W0, dD);
    c.STR(W0, X19, 296 + rd * 8);
    EmitTailCall(c);
}

static void emit_fctiwz(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    DReg dA = PinnedRegD(ra);
    c.FCVTZS(W0, dA);
    c.STR(W0, X19, 296 + rd * 8);
    EmitTailCall(c);
}

// -------------------------------------------------------------
// 6. Paired Singles / SIMD Super-instructions
// -------------------------------------------------------------

static void emit_ps_add(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    VReg_2S vD = PinnedRegV2S(rd);
    VReg_2S vA = PinnedRegV2S(ra);
    VReg_2S vB = PinnedRegV2S(rb);
    c.FADD(vD, vA, vB);
    EmitTailCall(c);
}

static void emit_ps_sub(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    VReg_2S vD = PinnedRegV2S(rd);
    VReg_2S vA = PinnedRegV2S(ra);
    VReg_2S vB = PinnedRegV2S(rb);
    c.FSUB(vD, vA, vB);
    EmitTailCall(c);
}

static void emit_ps_mul(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    VReg_2S vD = PinnedRegV2S(rd);
    VReg_2S vA = PinnedRegV2S(ra);
    VReg_2S vB = PinnedRegV2S(rb);
    c.FMUL(vD, vA, vB);
    EmitTailCall(c);
}

static void emit_ps_madd(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    VReg_2S vD = PinnedRegV2S(rd);
    VReg_2S vA = PinnedRegV2S(ra);
    VReg_2S vB = PinnedRegV2S(rb);
    c.FMLA(vD, vA, vB);
    EmitTailCall(c);
}

static void emit_ps_neg(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    VReg_2S vD = PinnedRegV2S(rd);
    VReg_2S vA = PinnedRegV2S(ra);
    c.FNEG(vD, vA);
    EmitTailCall(c);
}

static void emit_ps_abs(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    VReg_2S vD = PinnedRegV2S(rd);
    VReg_2S vA = PinnedRegV2S(ra);
    c.FABS(vD, vA);
    EmitTailCall(c);
}

static void emit_ps_merge00(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    VReg_2S vD = PinnedRegV2S(rd);
    VReg_2S vA = PinnedRegV2S(ra);
    VReg_2S vB = PinnedRegV2S(rb);
    c.TRN1(vD, vA, vB);
    EmitTailCall(c);
}

static void emit_ps_merge01(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    VReg_2S vD = PinnedRegV2S(rd);
    VReg_2S vA = PinnedRegV2S(ra);
    VReg_2S vB = PinnedRegV2S(rb);
    if (rd != ra) {
        c.FMOV(PinnedRegD(rd), PinnedRegD(ra));
    }
    c.INS(PinnedRegD(rd).Selem()[1], PinnedRegD(rb).Selem()[1]);
    EmitTailCall(c);
}

static void emit_ps_merge10(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    VReg_2S vD = PinnedRegV2S(rd);
    VReg_2S vA = PinnedRegV2S(ra);
    VReg_2S vB = PinnedRegV2S(rb);
    c.EXT(PinnedRegD(rd).B8(), PinnedRegD(ra).B8(), PinnedRegD(rb).B8(), 4);
    EmitTailCall(c);
}

static void emit_ps_merge11(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    VReg_2S vD = PinnedRegV2S(rd);
    VReg_2S vA = PinnedRegV2S(ra);
    VReg_2S vB = PinnedRegV2S(rb);
    c.TRN2(vD, vA, vB);
    EmitTailCall(c);
}

static void emit_psq_l(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.LDR(X1, X28, W0, IndexExt::UXTW);
    c.REV(X1, X1);
    c.STR(X1, X19, 296 + rd * 8);
    EmitTailCall(c);
}

static void emit_psq_st(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(X1, X19, 296 + rd * 8);
    c.REV(X1, X1);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.STR(X1, X28, W0, IndexExt::UXTW);
    EmitTailCall(c);
}

// -------------------------------------------------------------
// 7. Control / Branch / System
// -------------------------------------------------------------

static void emit_b(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    c.LDR(X20, X20, 8);
    c.LDR(X16, X20, 0);
    c.BR(X16);
}

static void emit_bl(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    c.LDR(W0, X20, 8);
    c.STR(W0, X19, 256);
    c.LDR(X20, X20, 16);
    c.LDR(X16, X20, 0);
    c.BR(X16);
}

static void emit_blr(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    c.LDR(W0, X19, 256);
    c.STR(W0, X19, 272);
    c.RET();
}

static void emit_bctr(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    c.LDR(W0, X19, 260);
    c.STR(W0, X19, 272);
    c.RET();
}

static void emit_mtlr(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.STR(wA, X19, 256);
    EmitTailCall(c);
}

static void emit_mflr(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    c.LDR(wD, X19, 256);
    EmitTailCall(c);
}

static void emit_mtctr(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.STR(wA, X19, 260);
    EmitTailCall(c);
}

static void emit_mfctr(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    c.LDR(wD, X19, 260);
    EmitTailCall(c);
}

static void emit_mfcr(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD = PinnedRegW(rd);
    c.LDR(wD, X19, 264);
    EmitTailCall(c);
}

static void emit_mtcrf(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.STR(wA, X19, 264);
    EmitTailCall(c);
}

static void emit_nop_inst(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    EmitTailCall(c);
}

// -------------------------------------------------------------
// 8. Super-Instructions
// -------------------------------------------------------------

static void emit_lwz_lwz_ldp(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD1 = PinnedRegW(rd);
    WReg wD2 = PinnedRegW(rb);
    WReg wA  = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    if (ra != 0) {
        c.ADD(W0, W0, wA);
    }
    c.LDR(X1, X28, W0, IndexExt::UXTW);
    c.REV(wD1, W1);
    c.LSR(X2, X1, 32);
    c.REV(wD2, W2);
    EmitTailCall(c);
}

static void emit_stw_stw_stp(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wD1 = PinnedRegW(rd);
    WReg wD2 = PinnedRegW(rb);
    WReg wA  = PinnedRegW(ra);
    c.REV(W0, wD1);
    c.REV(W1, wD2);
    c.LSL(X1, X1, 32);
    c.ORR(X0, X0, X1);
    c.LDR(W2, X20, 8);
    if (ra != 0) {
        c.ADD(W2, W2, wA);
    }
    c.STR(X0, X28, W2, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_cmpwi_beq_cbz(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    oaknut::Label taken;
    c.CBZ(wA, taken);
    EmitTailCall(c);
    c.l(taken);
    c.LDR(X20, X20, 8);
    c.LDR(X16, X20, 0);
    c.BR(X16);
}

static void emit_cmpwi_bne_cbnz(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    oaknut::Label taken;
    c.CBNZ(wA, taken);
    EmitTailCall(c);
    c.l(taken);
    c.LDR(X20, X20, 8);
    c.LDR(X16, X20, 0);
    c.BR(X16);
}

static void emit_stwu_mflr(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X19, 256);
    c.LDR(W1, X20, 8);
    c.REV(W2, wA);
    c.ADD(wA, wA, W1);
    c.STR(W2, X28, wA, IndexExt::UXTW);
    c.REV(W0, W0);
    c.ADD(W3, wA, 4);
    c.STR(W0, X28, W3, IndexExt::UXTW);
    EmitTailCall(c);
}

static void emit_mtlr_addi_blr(VectorCodeGenerator& c, bool Rc, int rd, int ra, int rb) {
    WReg wA = PinnedRegW(ra);
    c.LDR(W0, X20, 8);
    c.ADD(wA, wA, W0);
    c.LDR(W1, X19, 256);
    c.STR(W1, X19, 272);
    c.RET();
}

static const PPCInstrDef kPPCInstrs[] = {
    // 1. Integer Arithmetic & Logical
    {14, 0, "addi", false, false, false, false, emit_addi},
    {15, 0, "addis", false, false, false, false, emit_addis},
    {12, 0, "addic", false, false, false, false, emit_addic},
    {13, 0, "addic_rc", true, false, false, false, emit_addic},
    {8, 0, "subfic", false, false, false, false, emit_subfic},
    {7, 0, "mulli", false, false, false, false, emit_mulli},

    {31, 266, "add", true, true, false, false, emit_add},
    {31, 10, "addc", true, true, false, false, emit_addc},
    {31, 138, "adde", true, true, false, false, emit_adde},
    {31, 234, "addme", true, false, false, false, emit_addme},
    {31, 202, "addze", true, false, false, false, emit_addze},
    {31, 40, "subf", true, true, false, false, emit_subf},
    {31, 8, "subfc", true, true, false, false, emit_subfc},
    {31, 136, "subfe", true, true, false, false, emit_subfe},
    {31, 232, "subfme", true, false, false, false, emit_subfme},
    {31, 200, "subfze", true, false, false, false, emit_subfze},
    {31, 104, "neg", true, false, false, false, emit_neg},
    {31, 235, "mullw", true, true, false, false, emit_mullw},
    {31, 75, "mulhw", true, true, false, false, emit_mulhw},
    {31, 11, "mulhwu", true, true, false, false, emit_mulhwu},
    {31, 491, "divw", true, true, false, false, emit_divw},
    {31, 459, "divwu", true, true, false, false, emit_divwu},

    {24, 0, "ori", false, false, false, false, emit_ori},
    {25, 0, "oris", false, false, false, false, emit_oris},
    {26, 0, "xori", false, false, false, false, emit_xori},
    {27, 0, "xoris", false, false, false, false, emit_xoris},
    {28, 0, "andi_rc", true, false, false, false, emit_andi},
    {29, 0, "andis_rc", true, false, false, false, emit_andis},

    {31, 28, "and", true, true, false, false, emit_and},
    {31, 60, "andc", true, true, false, false, emit_andc},
    {31, 444, "or", true, true, false, false, emit_or},
    {31, 412, "orc", true, true, false, false, emit_orc},
    {31, 316, "xor", true, true, false, false, emit_xor},
    {31, 476, "nand", true, true, false, false, emit_nand},
    {31, 124, "nor", true, true, false, false, emit_nor},
    {31, 284, "eqv", true, true, false, false, emit_eqv},
    {31, 954, "extsb", true, false, false, false, emit_extsb},
    {31, 922, "extsh", true, false, false, false, emit_extsh},
    {31, 26, "cntlzw", true, false, false, false, emit_cntlzw},

    // 2. Rotate / Shift
    {21, 0, "rlwinm", true, false, false, false, emit_rlwinm},
    {20, 0, "rlwimi", true, false, false, false, emit_rlwimi},
    {23, 0, "rlwnm", true, true, false, false, emit_rlwnm},
    {31, 24, "slw", true, true, false, false, emit_slw},
    {31, 536, "srw", true, true, false, false, emit_srw},
    {31, 792, "sraw", true, true, false, false, emit_sraw},
    {31, 824, "srawi", true, false, false, false, emit_srawi},

    // 3. Compares
    {31, 0, "cmp", false, true, false, false, emit_cmp},
    {31, 32, "cmpl", false, true, false, false, emit_cmpl},
    {11, 0, "cmpi", false, false, false, false, emit_cmpi},
    {10, 0, "cmpli", false, false, false, false, emit_cmpli},

    // 4. Integer Load/Store
    {32, 0, "lwz", false, false, true, false, emit_lwz},
    {33, 0, "lwzu", false, false, true, false, emit_lwzu},
    {31, 23, "lwzx", false, true, true, false, emit_lwzx},
    {31, 55, "lwzux", false, true, true, false, emit_lwzux},
    {34, 0, "lbz", false, false, true, false, emit_lbz},
    {35, 0, "lbzu", false, false, true, false, emit_lbzu},
    {31, 87, "lbzx", false, true, true, false, emit_lbzx},
    {31, 119, "lbzux", false, true, true, false, emit_lbzux},
    {40, 0, "lhz", false, false, true, false, emit_lhz},
    {41, 0, "lhzu", false, false, true, false, emit_lhzu},
    {31, 279, "lhzx", false, true, true, false, emit_lhzx},
    {31, 311, "lhzux", false, true, true, false, emit_lhzux},
    {42, 0, "lha", false, false, true, false, emit_lha},
    {43, 0, "lhau", false, false, true, false, emit_lhau},
    {31, 343, "lhax", false, true, true, false, emit_lhax},
    {31, 375, "lhaux", false, true, true, false, emit_lhaux},
    {31, 534, "lwbrx", false, true, true, false, emit_lwbrx},
    {31, 790, "lhbrx", false, true, true, false, emit_lhbrx},
    {36, 0, "stw", false, false, false, true, emit_stw},
    {37, 0, "stwu", false, false, false, true, emit_stwu},
    {31, 151, "stwx", false, true, false, true, emit_stwx},
    {31, 183, "stwux", false, true, false, true, emit_stwux},
    {38, 0, "stb", false, false, false, true, emit_stb},
    {39, 0, "stbu", false, false, false, true, emit_stbu},
    {31, 215, "stbx", false, true, false, true, emit_stbx},
    {31, 247, "stbux", false, true, false, true, emit_stbux},
    {44, 0, "sth", false, false, false, true, emit_sth},
    {45, 0, "sthu", false, false, false, true, emit_sthu},
    {31, 407, "sthx", false, true, false, true, emit_sthx},
    {31, 439, "sthux", false, true, false, true, emit_sthux},
    {31, 662, "stwbrx", false, true, false, true, emit_stwbrx},
    {31, 918, "sthbrx", false, true, false, true, emit_sthbrx},

    // 5. Floating Point Loads & Stores & Ops
    {48, 0, "lfs", false, false, true, false, emit_lfs},
    {49, 0, "lfsu", false, false, true, false, emit_lfsu},
    {31, 535, "lfsx", false, true, true, false, emit_lfsx},
    {50, 0, "lfd", false, false, true, false, emit_lfd},
    {51, 0, "lfdu", false, false, true, false, emit_lfdu},
    {31, 599, "lfdx", false, true, true, false, emit_lfdx},
    {52, 0, "stfs", false, false, false, true, emit_stfs},
    {53, 0, "stfsu", false, false, false, true, emit_stfsu},
    {31, 663, "stfsx", false, true, false, true, emit_stfsx},
    {54, 0, "stfd", false, false, false, true, emit_stfd},
    {55, 0, "stfdu", false, false, false, true, emit_stfdu},
    {31, 727, "stfdx", false, true, false, true, emit_stfdx},
    {63, 21, "fadd", true, true, false, false, emit_fadd},
    {63, 20, "fsub", true, true, false, false, emit_fsub},
    {63, 25, "fmul", true, true, false, false, emit_fmul},
    {63, 18, "fdiv", true, true, false, false, emit_fdiv},
    {63, 29, "fmadd", true, true, false, false, emit_fmadd},
    {63, 28, "fmsub", true, true, false, false, emit_fmsub},
    {63, 40, "fneg", true, false, false, false, emit_fneg},
    {63, 264, "fabs", true, false, false, false, emit_fabs},
    {63, 136, "fnabs", true, false, false, false, emit_fnabs},
    {63, 0, "fcmpu", false, true, false, false, emit_fcmpu},
    {63, 32, "fcmpo", false, true, false, false, emit_fcmpo},
    {63, 14, "fctiw", true, false, false, false, emit_fctiw},
    {63, 15, "fctiwz", true, false, false, false, emit_fctiwz},

    // 6. Paired Singles / SIMD Super-instructions
    {4, 21, "ps_add", true, true, false, false, emit_ps_add},
    {4, 20, "ps_sub", true, true, false, false, emit_ps_sub},
    {4, 25, "ps_mul", true, true, false, false, emit_ps_mul},
    {4, 29, "ps_madd", true, true, false, false, emit_ps_madd},
    {4, 40, "ps_neg", true, false, false, false, emit_ps_neg},
    {4, 264, "ps_abs", true, false, false, false, emit_ps_abs},
    {4, 528, "ps_merge00", true, true, false, false, emit_ps_merge00},
    {4, 560, "ps_merge01", true, true, false, false, emit_ps_merge01},
    {4, 592, "ps_merge10", true, true, false, false, emit_ps_merge10},
    {4, 624, "ps_merge11", true, true, false, false, emit_ps_merge11},
    {56, 0, "psq_l", false, false, true, false, emit_psq_l},
    {60, 0, "psq_st", false, false, false, true, emit_psq_st},

    // 7. Control / Branch / System
    {18, 0, "b", false, false, false, false, emit_b},
    {18, 1, "bl", false, false, false, false, emit_bl},
    {19, 16, "blr", false, false, false, false, emit_blr},
    {19, 528, "bctr", false, false, false, false, emit_bctr},
    {31, 467, "mtlr", false, false, false, false, emit_mtlr},
    {31, 339, "mflr", false, false, false, false, emit_mflr},
    {31, 468, "mtctr", false, false, false, false, emit_mtctr},
    {31, 340, "mfctr", false, false, false, false, emit_mfctr},
    {31, 19, "mfcr", false, false, false, false, emit_mfcr},
    {31, 144, "mtcrf", false, false, false, false, emit_mtcrf},
    {19, 150, "isync", false, false, false, false, emit_nop_inst},
    {31, 598, "sync", false, false, false, false, emit_nop_inst},
    {31, 854, "eieio", false, false, false, false, emit_nop_inst},

    // 8. Super-instructions
    {0x1000, 0, "lwz_lwz_ldp", false, true, true, false, emit_lwz_lwz_ldp},
    {0x1001, 0, "stw_stw_stp", false, true, false, true, emit_stw_stw_stp},
    {0x1002, 0, "cmpwi_beq_cbz", false, false, false, false, emit_cmpwi_beq_cbz},
    {0x1003, 0, "cmpwi_bne_cbnz", false, false, false, false, emit_cmpwi_bne_cbnz},
    {0x1004, 0, "stwu_mflr", false, false, false, true, emit_stwu_mflr},
    {0x1005, 0, "mtlr_addi_blr", false, false, false, false, emit_mtlr_addi_blr},
};

static constexpr size_t kNumPPCInstrs = sizeof(kPPCInstrs) / sizeof(kPPCInstrs[0]);

int main(int argc, char* argv[]) {
    std::string header_path = "src/ppc_stencils_generated.h";
    std::string cpp_path    = "src/ppc_stencils_generated.cpp";
    if (argc >= 3) {
        header_path = argv[1];
        cpp_path    = argv[2];
    }

    std::stringstream h_out;
    std::stringstream cpp_out;

    h_out << "// AUTO-GENERATED by gen_stencils — do not edit manually\n";
    h_out << "#pragma once\n";
    h_out << "#include <cstdint>\n";
    h_out << "#include <cstddef>\n\n";
    h_out << "struct StencilEntry {\n";
    h_out << "    const uint8_t* bytes;\n";
    h_out << "    uint16_t       length;  // in bytes\n";
    h_out << "};\n\n";
    h_out << "// Pinned register index mapping: 0->r0, 1->r3, 2->r4, 3->r5, 4->r31\n";
    h_out << "constexpr int kPinnedRegs[5] = {0, 3, 4, 5, 31};\n\n";

    cpp_out << "// AUTO-GENERATED by gen_stencils — do not edit manually\n";
    cpp_out << "#include \"ppc_stencils_generated.h\"\n";
    cpp_out << "#include <unordered_map>\n\n";

    std::vector<uint8_t> byte_pool;
    size_t total_stencils = 0;

    struct StencilInfo {
        size_t offset;
        uint16_t length;
    };

    struct InstrData {
        const PPCInstrDef* def;
        std::vector<StencilInfo> entries;
    };

    std::vector<InstrData> all_data;

    for (size_t i = 0; i < kNumPPCInstrs; i++) {
        const auto& def = kPPCInstrs[i];
        InstrData idata;
        idata.def = &def;

        int rc_variants = def.has_Rc ? 2 : 1;
        int rb_variants = def.has_rb ? 5 : 1;

        for (int rc = 0; rc < rc_variants; rc++) {
            for (int rd = 0; rd < 5; rd++) {
                for (int ra = 0; ra < 5; ra++) {
                    for (int rb = 0; rb < rb_variants; rb++) {
                        std::vector<uint32_t> code_buf;
                        VectorCodeGenerator c{code_buf};
                        def.emit_fn(c, rc == 1, rd, ra, rb);

                        StencilInfo sinfo;
                        sinfo.offset = byte_pool.size();
                        sinfo.length = static_cast<uint16_t>(code_buf.size() * sizeof(uint32_t));

                        const uint8_t* d = reinterpret_cast<const uint8_t*>(code_buf.data());
                        byte_pool.insert(byte_pool.end(), d, d + sinfo.length);

                        idata.entries.push_back(sinfo);
                        total_stencils++;
                    }
                }
            }
        }
        all_data.push_back(idata);
    }

    cpp_out << "alignas(64) static const uint8_t g_stencil_pool[" << byte_pool.size() << "] = {\n  ";
    for (size_t i = 0; i < byte_pool.size(); i++) {
        cpp_out << "0x" << std::hex << (int)byte_pool[i] << ", " << std::dec;
        if (i % 16 == 15 || i + 1 == byte_pool.size()) cpp_out << "\n  ";
    }
    cpp_out << "};\n\n";

    for (const auto& idata : all_data) {
        const auto& def = *idata.def;
        int rc_variants = def.has_Rc ? 2 : 1;
        int rb_variants = def.has_rb ? 5 : 1;

        for (int rc = 0; rc < rc_variants; rc++) {
            std::string name = std::string("stencil_") + def.name + "_Rc" + std::to_string(rc);

            if (def.has_rb) {
                h_out << "extern const StencilEntry " << name << "[5][5][5];\n";
                cpp_out << "const StencilEntry " << name << "[5][5][5] = {\n";
                for (int rd = 0; rd < 5; rd++) {
                    cpp_out << "  {\n";
                    for (int ra = 0; ra < 5; ra++) {
                        cpp_out << "    {";
                        for (int rb = 0; rb < rb_variants; rb++) {
                            size_t idx = (rc * 5 * 5 * 5) + (rd * 5 * 5) + (ra * 5) + rb;
                            const auto& s = idata.entries[idx];
                            cpp_out << "{&g_stencil_pool[" << s.offset << "], " << s.length << "}";
                            if (rb < 4) cpp_out << ", ";
                        }
                        cpp_out << "}";
                        if (ra < 4) cpp_out << ",";
                        cpp_out << "\n";
                    }
                    cpp_out << "  }";
                    if (rd < 4) cpp_out << ",";
                    cpp_out << "\n";
                }
                cpp_out << "};\n\n";
            } else {
                h_out << "extern const StencilEntry " << name << "[5][5];\n";
                cpp_out << "const StencilEntry " << name << "[5][5] = {\n";
                for (int rd = 0; rd < 5; rd++) {
                    cpp_out << "  {";
                    for (int ra = 0; ra < 5; ra++) {
                        size_t idx = (rc * 5 * 5) + (rd * 5) + ra;
                        const auto& s = idata.entries[idx];
                        cpp_out << "{&g_stencil_pool[" << s.offset << "], " << s.length << "}";
                        if (ra < 4) cpp_out << ", ";
                    }
                    cpp_out << "}";
                    if (rd < 4) cpp_out << ",";
                    cpp_out << "\n";
                }
                cpp_out << "};\n\n";
            }
        }
    }

    h_out << "\n// Master lookup: (ppc_primary_opcode << 16 | ppc_subopcode) -> flat table\n";
    h_out << "// Call: lookup_stencil(opcode, subop, Rc, rd_pinned_idx, ra_pinned_idx, rb_pinned_idx)\n";
    h_out << "const StencilEntry* lookup_stencil(uint32_t opcode, uint32_t subop, bool Rc,\n";
    h_out << "                                   int rd_idx, int ra_idx, int rb_idx);\n\n";
    h_out << "// Helper: count total stencils\n";
    h_out << "size_t get_total_stencil_count();\n";
    h_out << "size_t get_total_stencil_bytes();\n";

    cpp_out << "struct InstrLookupInfo {\n";
    cpp_out << "    const StencilEntry* table;\n";
    cpp_out << "    bool has_Rc;\n";
    cpp_out << "    bool has_rb;\n";
    cpp_out << "};\n\n";

    cpp_out << "static const std::unordered_map<uint32_t, InstrLookupInfo> s_lookup_map = {\n";
    for (const auto& idata : all_data) {
        const auto& def = *idata.def;
        uint32_t key = (def.ppc_opcode << 16) | (def.ppc_subopcode & 0xFFFF);
        std::string table_name = std::string("(const StencilEntry*)stencil_") + def.name + "_Rc0";
        cpp_out << "    {0x" << std::hex << key << std::dec << "u, {" << table_name << ", "
                << (def.has_Rc ? "true" : "false") << ", "
                << (def.has_rb ? "true" : "false") << "}},\n";
    }
    cpp_out << "};\n\n";

    cpp_out << "const StencilEntry* lookup_stencil(uint32_t opcode, uint32_t subop, bool Rc,\n";
    cpp_out << "                                   int rd_idx, int ra_idx, int rb_idx) {\n";
    cpp_out << "    if (rd_idx < 0 || rd_idx >= 5 || ra_idx < 0 || ra_idx >= 5 || rb_idx < 0 || rb_idx >= 5) return nullptr;\n";
    cpp_out << "    uint32_t key = (opcode << 16) | (subop & 0xFFFF);\n";
    cpp_out << "    auto it = s_lookup_map.find(key);\n";
    cpp_out << "    if (it == s_lookup_map.end()) return nullptr;\n";
    cpp_out << "    const auto& info = it->second;\n";
    cpp_out << "    int rc_idx = (Rc && info.has_Rc) ? 1 : 0;\n";
    cpp_out << "    if (info.has_rb) {\n";
    cpp_out << "        size_t idx = (rc_idx * 5 * 5 * 5) + (rd_idx * 5 * 5) + (ra_idx * 5) + rb_idx;\n";
    cpp_out << "        return &info.table[idx];\n";
    cpp_out << "    } else {\n";
    cpp_out << "        size_t idx = (rc_idx * 5 * 5) + (rd_idx * 5) + ra_idx;\n";
    cpp_out << "        return &info.table[idx];\n";
    cpp_out << "    }\n";
    cpp_out << "}\n\n";

    cpp_out << "size_t get_total_stencil_count() { return " << total_stencils << "; }\n";
    cpp_out << "size_t get_total_stencil_bytes() { return sizeof(g_stencil_pool); }\n";

    {
        std::ofstream f(header_path);
        if (!f) return 1;
        f << h_out.str();
    }
    {
        std::ofstream f(cpp_path);
        if (!f) return 1;
        f << cpp_out.str();
    }

    std::cout << "Successfully generated " << total_stencils << " stencils ("
              << (byte_pool.size() / 1024.0) << " KB)\n";

    return 0;
}
