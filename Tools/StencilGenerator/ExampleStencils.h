// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Tools/StencilGenerator/StencilEmitter.h"
#include "Tools/StencilGenerator/StencilContext.h"

namespace StencilGen
{

// ============================================================================
// Transpiled from Source/Core/Core/PowerPC/JitArm64/JitArm64_Integer.cpp
// ============================================================================

// PPC 'add' / 'add.' (OPCD 31, SUBOP 266)
inline void Emit_add(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADD(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// PPC 'addc' / 'addc.' (OPCD 31, SUBOP 10) - Add with Carry Out
inline void Emit_addc(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADDS(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  c.CSET(ARM64Reg::W3, CC_CS);
  c.EmitSaveCarry(ARM64Reg::W3);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// PPC 'adde' / 'adde.' (OPCD 31, SUBOP 138) - Add Extended (with Carry In & Out)
inline void Emit_adde(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.EmitLoadCarry(ARM64Reg::W3);
  c.CMP(ARM64Reg::W3, 1);
  c.ADCS(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  c.CSET(ARM64Reg::W3, CC_CS);
  c.EmitSaveCarry(ARM64Reg::W3);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// PPC 'subf' / 'subf.' (OPCD 31, SUBOP 40) - Subtract From (rD = rB - rA)
inline void Emit_subf(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.SUB(ctx.GetRD(), ctx.GetRB(), ctx.GetRA());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// PPC 'subfc' / 'subfc.' (OPCD 31, SUBOP 8) - Subtract From with Carry Out
// PowerPC Carry on subf is inverted borrow: Carry = (rB >= rA) == ARM64 CS (Carry Set)
inline void Emit_subfc(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.SUBS(ctx.GetRD(), ctx.GetRB(), ctx.GetRA());
  c.CSET(ARM64Reg::W3, CC_CS);
  c.EmitSaveCarry(ARM64Reg::W3);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// PPC 'neg' / 'neg.' (OPCD 31, SUBOP 104)
inline void Emit_neg(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.NEG(ctx.GetRD(), ctx.GetRA());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// PPC 'extsb' / 'extsb.' (OPCD 31, SUBOP 954) - Extend Sign Byte
inline void Emit_extsb(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.SBFM(ctx.GetRD(), ctx.GetRA(), 0, 7);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// PPC 'extsh' / 'extsh.' (OPCD 31, SUBOP 922) - Extend Sign Halfword
inline void Emit_extsh(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.SBFM(ctx.GetRD(), ctx.GetRA(), 0, 15);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// PPC 'cntlzw' / 'cntlzw.' (OPCD 31, SUBOP 26) - Count Leading Zeros
inline void Emit_cntlzw(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.CLZ(ctx.GetRD(), ctx.GetRA());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// ============================================================================
// Transpiled from Source/Core/Core/PowerPC/JitArm64/JitArm64_Logical.cpp
// Note: In logical operations, PowerPC specifies rA as DESTINATION and rS as SOURCE.
// ============================================================================

// PPC 'or' / 'or.' (OPCD 31, SUBOP 444)
inline void Emit_or(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ORR(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// PPC 'and' / 'and.' (OPCD 31, SUBOP 28)
inline void Emit_and(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.AND(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// PPC 'xor' / 'xor.' (OPCD 31, SUBOP 316)
inline void Emit_xor(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.EOR(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// PPC 'nor' / 'nor.' (OPCD 31, SUBOP 124)
inline void Emit_nor(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ORR(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  c.MVN(ctx.GetRD(), ctx.GetRD());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// ============================================================================
// Base Instruction Operation Bodies (for standalone stencils and fusion)
// ============================================================================

// PPC 'mr' (or rD, rA, rA)
inline void Emit_mr_op(StencilEmitter& c, StencilContext& ctx)
{
  ctx.LoadGPR(c, ARM64Reg::W0, ctx.op.ra);
  ctx.StoreGPR(c, ARM64Reg::W0, ctx.op.rd);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ARM64Reg::W0, ARM64Reg::X16);
}

inline void Emit_mr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  Emit_mr_op(c, ctx);
  ctx.End(c);
}

// PPC 'addi' / 'li' (OPCD 14)
inline void Emit_addi_op(StencilEmitter& c, StencilContext& ctx)
{
  if (ctx.op.ra == 0)
  {
    c.MOVI2R(ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  }
  else
  {
    ctx.LoadGPR(c, ARM64Reg::W0, ctx.op.ra);
    c.ADDI2R(ARM64Reg::W0, ARM64Reg::W0, static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  }
  ctx.StoreGPR(c, ARM64Reg::W0, ctx.op.rd);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ARM64Reg::W0, ARM64Reg::X16);
}

inline void Emit_addi(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  Emit_addi_op(c, ctx);
  ctx.End(c);
}

// PPC 'addis' / 'lis' (OPCD 15)
inline void Emit_addis_op(StencilEmitter& c, StencilContext& ctx)
{
  const u32 shifted = (ctx.op.imm & 0xFFFF) << 16;
  if (ctx.op.ra == 0)
  {
    c.MOVI2R(ARM64Reg::W0, shifted);
  }
  else
  {
    ctx.LoadGPR(c, ARM64Reg::W0, ctx.op.ra);
    c.ADDI2R(ARM64Reg::W0, ARM64Reg::W0, shifted, ARM64Reg::W16);
  }
  ctx.StoreGPR(c, ARM64Reg::W0, ctx.op.rd);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ARM64Reg::W0, ARM64Reg::X16);
}

inline void Emit_addis(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  Emit_addis_op(c, ctx);
  ctx.End(c);
}

// PPC 'ori' (OPCD 24)
inline void Emit_ori_op(StencilEmitter& c, StencilContext& ctx)
{
  ctx.LoadGPR(c, ARM64Reg::W0, ctx.op.ra);
  c.ORRI2R(ARM64Reg::W0, ARM64Reg::W0, ctx.op.imm & 0xFFFF, ARM64Reg::W16);
  ctx.StoreGPR(c, ARM64Reg::W0, ctx.op.rd);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ARM64Reg::W0, ARM64Reg::X16);
}

inline void Emit_ori(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  Emit_ori_op(c, ctx);
  ctx.End(c);
}

// PPC 'cmpwi' (OPCD 11) - Compare Signed Immediate
inline void Emit_cmpwi_op(StencilEmitter& c, StencilContext& ctx)
{
  ctx.LoadGPR(c, ARM64Reg::W0, ctx.op.ra);
  c.CMPI2R(ARM64Reg::W0, static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
}

inline void Emit_cmpwi(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  Emit_cmpwi_op(c, ctx);
  ctx.End(c);
}

// PPC 'cmplwi' (OPCD 10) - Compare Logical Immediate
inline void Emit_cmplwi_op(StencilEmitter& c, StencilContext& ctx)
{
  ctx.LoadGPR(c, ARM64Reg::W0, ctx.op.ra);
  c.CMPI2R(ARM64Reg::W0, ctx.op.imm & 0xFFFF, ARM64Reg::W16);
}

inline void Emit_cmplwi(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  Emit_cmplwi_op(c, ctx);
  ctx.End(c);
}

// PPC 'lwz' (OPCD 32) - Load Word and Zero
inline void Emit_lwz_op(StencilEmitter& c, StencilContext& ctx)
{
  ctx.LoadBaseAddress(c, ARM64Reg::W0, ctx.op.ra, static_cast<s16>(ctx.op.imm));
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(ARM64Reg::W1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV32(ARM64Reg::W1, ARM64Reg::W1);
  ctx.StoreGPR(c, ARM64Reg::W1, ctx.op.rd);
}

inline void Emit_lwz(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  Emit_lwz_op(c, ctx);
  ctx.End(c);
}

// PPC 'stw' (OPCD 36) - Store Word
inline void Emit_stw_op(StencilEmitter& c, StencilContext& ctx)
{
  ctx.LoadBaseAddress(c, ARM64Reg::W0, ctx.op.ra, static_cast<s16>(ctx.op.imm));
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  ctx.LoadGPR(c, ARM64Reg::W1, ctx.op.rd);
  c.REV32(ARM64Reg::W1, ARM64Reg::W1);
  c.STR(ARM64Reg::W1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
}

inline void Emit_stw(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  Emit_stw_op(c, ctx);
  ctx.End(c);
}

// PPC 'stwu' (OPCD 37) - Store Word with Update
inline void Emit_stwu_op(StencilEmitter& c, StencilContext& ctx)
{
  ctx.LoadGPR(c, ARM64Reg::W1, ctx.op.rd);
  c.REV32(ARM64Reg::W1, ARM64Reg::W1);
  ctx.LoadGPR(c, ARM64Reg::W2, ctx.op.ra);
  c.ADDI2R(ARM64Reg::W2, ARM64Reg::W2, static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  ctx.StoreGPR(c, ARM64Reg::W2, ctx.op.ra);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W2, 0x01FFFFFF, ARM64Reg::W16);
  c.STR(ARM64Reg::W1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
}

inline void Emit_stwu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  Emit_stwu_op(c, ctx);
  ctx.End(c);
}

// PPC 'mflr' (mfspr rD, 8)
inline void Emit_mflr_op(StencilEmitter& c, StencilContext& ctx)
{
  c.LDR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, OFF_PPCSTATE_SPR_LR);
  ctx.StoreGPR(c, ARM64Reg::W0, ctx.op.rd);
}

inline void Emit_mflr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  Emit_mflr_op(c, ctx);
  ctx.End(c);
}

// PPC 'mtlr' (mtspr 8, rS)
inline void Emit_mtlr_op(StencilEmitter& c, StencilContext& ctx)
{
  ctx.LoadGPR(c, ARM64Reg::W0, ctx.op.rd);
  c.STR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, OFF_PPCSTATE_SPR_LR);
}

inline void Emit_mtlr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  Emit_mtlr_op(c, ctx);
  ctx.End(c);
}

// PPC 'blr'
inline void Emit_blr_op(StencilEmitter& c, StencilContext& ctx)
{
  c.LDR(IndexType::Unsigned, ARM64Reg::X16, ARM64Reg::X19, OFF_PPCSTATE_SPR_LR);
  c.BR(ARM64Reg::X16);
}

inline void Emit_blr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  Emit_blr_op(c, ctx);
}

}  // namespace StencilGen
