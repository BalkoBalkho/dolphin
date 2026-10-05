// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Tools/StencilGenerator/StencilEmitter.h"
#include "Tools/StencilGenerator/StencilContext.h"

namespace StencilGen
{

// ============================================================================
// Transpiled from Source/Core/Core/PowerPC/JitArm64/JitArm64_Paired.cpp
// Full PowerPC Gekko Paired-Single SIMD Opcode Coverage
// ============================================================================

// Helpers for loading and storing individual paired single lanes (ps0 at +0, ps1 at +8)
inline void EmitLoadPS0(StencilEmitter& c, ARM64Reg d_reg, int ps_idx)
{
  c.fp.LDR(64, IndexType::Unsigned, d_reg, ARM64Reg::X19, OFF_PPCSTATE_PS + ps_idx * 16);
}

inline void EmitLoadPS1(StencilEmitter& c, ARM64Reg d_reg, int ps_idx)
{
  c.fp.LDR(64, IndexType::Unsigned, d_reg, ARM64Reg::X19, OFF_PPCSTATE_PS + ps_idx * 16 + 8);
}

inline void EmitStorePS0(StencilEmitter& c, ARM64Reg d_reg, int ps_idx)
{
  c.fp.STR(64, IndexType::Unsigned, d_reg, ARM64Reg::X19, OFF_PPCSTATE_PS + ps_idx * 16);
}

inline void EmitStorePS1(StencilEmitter& c, ARM64Reg d_reg, int ps_idx)
{
  c.fp.STR(64, IndexType::Unsigned, d_reg, ARM64Reg::X19, OFF_PPCSTATE_PS + ps_idx * 16 + 8);
}

// 1. PS_ADD: vD = vA + vB (both ps0 and ps1 lanes)
inline void Emit_ps_add(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitLoadPS0(c, ARM64Reg::D1, ctx.op.ra);
  EmitLoadPS0(c, ARM64Reg::D2, ctx.op.rb);
  c.fp.FADD(ARM64Reg::D0, ARM64Reg::D1, ARM64Reg::D2);
  EmitLoadPS1(c, ARM64Reg::D3, ctx.op.ra);
  EmitLoadPS1(c, ARM64Reg::D4, ctx.op.rb);
  c.fp.FADD(ARM64Reg::D5, ARM64Reg::D3, ARM64Reg::D4);
  EmitStorePS0(c, ARM64Reg::D0, ctx.op.rd);
  EmitStorePS1(c, ARM64Reg::D5, ctx.op.rd);
  ctx.End(c);
}

// 2. PS_SUB: vD = vA - vB
inline void Emit_ps_sub(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitLoadPS0(c, ARM64Reg::D1, ctx.op.ra);
  EmitLoadPS0(c, ARM64Reg::D2, ctx.op.rb);
  c.fp.FSUB(ARM64Reg::D0, ARM64Reg::D1, ARM64Reg::D2);
  EmitLoadPS1(c, ARM64Reg::D3, ctx.op.ra);
  EmitLoadPS1(c, ARM64Reg::D4, ctx.op.rb);
  c.fp.FSUB(ARM64Reg::D5, ARM64Reg::D3, ARM64Reg::D4);
  EmitStorePS0(c, ARM64Reg::D0, ctx.op.rd);
  EmitStorePS1(c, ARM64Reg::D5, ctx.op.rd);
  ctx.End(c);
}

// 3. PS_MUL: vD = vA * vB
inline void Emit_ps_mul(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitLoadPS0(c, ARM64Reg::D1, ctx.op.ra);
  EmitLoadPS0(c, ARM64Reg::D2, ctx.op.rb);
  c.fp.FMUL(ARM64Reg::D0, ARM64Reg::D1, ARM64Reg::D2);
  EmitLoadPS1(c, ARM64Reg::D3, ctx.op.ra);
  EmitLoadPS1(c, ARM64Reg::D4, ctx.op.rb);
  c.fp.FMUL(ARM64Reg::D5, ARM64Reg::D3, ARM64Reg::D4);
  EmitStorePS0(c, ARM64Reg::D0, ctx.op.rd);
  EmitStorePS1(c, ARM64Reg::D5, ctx.op.rd);
  ctx.End(c);
}

// 4. PS_DIV: vD = vA / vB
inline void Emit_ps_div(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitLoadPS0(c, ARM64Reg::D1, ctx.op.ra);
  EmitLoadPS0(c, ARM64Reg::D2, ctx.op.rb);
  c.fp.FDIV(ARM64Reg::D0, ARM64Reg::D1, ARM64Reg::D2);
  EmitLoadPS1(c, ARM64Reg::D3, ctx.op.ra);
  EmitLoadPS1(c, ARM64Reg::D4, ctx.op.rb);
  c.fp.FDIV(ARM64Reg::D5, ARM64Reg::D3, ARM64Reg::D4);
  EmitStorePS0(c, ARM64Reg::D0, ctx.op.rd);
  EmitStorePS1(c, ARM64Reg::D5, ctx.op.rd);
  ctx.End(c);
}

// 5. PS_MADD: vD = (vA * vC) + vB
inline void Emit_ps_madd(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitLoadPS0(c, ARM64Reg::D1, ctx.op.ra);
  EmitLoadPS0(c, ARM64Reg::D2, ctx.op.rb);
  EmitLoadPS0(c, ARM64Reg::D0, ctx.op.rd);
  c.fp.FMADD(ARM64Reg::D0, ARM64Reg::D1, ARM64Reg::D2, ARM64Reg::D0);
  EmitLoadPS1(c, ARM64Reg::D3, ctx.op.ra);
  EmitLoadPS1(c, ARM64Reg::D4, ctx.op.rb);
  EmitLoadPS1(c, ARM64Reg::D5, ctx.op.rd);
  c.fp.FMADD(ARM64Reg::D5, ARM64Reg::D3, ARM64Reg::D4, ARM64Reg::D5);
  EmitStorePS0(c, ARM64Reg::D0, ctx.op.rd);
  EmitStorePS1(c, ARM64Reg::D5, ctx.op.rd);
  ctx.End(c);
}

// 6. PS_MSUB: vD = (vA * vC) - vB
inline void Emit_ps_msub(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitLoadPS0(c, ARM64Reg::D1, ctx.op.ra);
  EmitLoadPS0(c, ARM64Reg::D2, ctx.op.rb);
  EmitLoadPS0(c, ARM64Reg::D0, ctx.op.rd);
  c.fp.FMSUB(ARM64Reg::D0, ARM64Reg::D1, ARM64Reg::D2, ARM64Reg::D0);
  EmitLoadPS1(c, ARM64Reg::D3, ctx.op.ra);
  EmitLoadPS1(c, ARM64Reg::D4, ctx.op.rb);
  EmitLoadPS1(c, ARM64Reg::D5, ctx.op.rd);
  c.fp.FMSUB(ARM64Reg::D5, ARM64Reg::D3, ARM64Reg::D4, ARM64Reg::D5);
  EmitStorePS0(c, ARM64Reg::D0, ctx.op.rd);
  EmitStorePS1(c, ARM64Reg::D5, ctx.op.rd);
  ctx.End(c);
}

// 7. PS_NEG: vD = -vB
inline void Emit_ps_neg(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitLoadPS0(c, ARM64Reg::D1, ctx.op.rb);
  c.fp.FNEG(ARM64Reg::D0, ARM64Reg::D1);
  EmitLoadPS1(c, ARM64Reg::D3, ctx.op.rb);
  c.fp.FNEG(ARM64Reg::D5, ARM64Reg::D3);
  EmitStorePS0(c, ARM64Reg::D0, ctx.op.rd);
  EmitStorePS1(c, ARM64Reg::D5, ctx.op.rd);
  ctx.End(c);
}

// 8. PS_ABS: vD = |vB|
inline void Emit_ps_abs(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitLoadPS0(c, ARM64Reg::D1, ctx.op.rb);
  c.fp.FABS(ARM64Reg::D0, ARM64Reg::D1);
  EmitLoadPS1(c, ARM64Reg::D3, ctx.op.rb);
  c.fp.FABS(ARM64Reg::D5, ARM64Reg::D3);
  EmitStorePS0(c, ARM64Reg::D0, ctx.op.rd);
  EmitStorePS1(c, ARM64Reg::D5, ctx.op.rd);
  ctx.End(c);
}

// 9. PS_NABS: vD = -|vB|
inline void Emit_ps_nabs(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitLoadPS0(c, ARM64Reg::D1, ctx.op.rb);
  c.fp.FABS(ARM64Reg::D0, ARM64Reg::D1);
  c.fp.FNEG(ARM64Reg::D0, ARM64Reg::D0);
  EmitLoadPS1(c, ARM64Reg::D3, ctx.op.rb);
  c.fp.FABS(ARM64Reg::D5, ARM64Reg::D3);
  c.fp.FNEG(ARM64Reg::D5, ARM64Reg::D5);
  EmitStorePS0(c, ARM64Reg::D0, ctx.op.rd);
  EmitStorePS1(c, ARM64Reg::D5, ctx.op.rd);
  ctx.End(c);
}

// 10. PS_MR: vD = vB
inline void Emit_ps_mr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitLoadPS0(c, ARM64Reg::D0, ctx.op.rb);
  EmitLoadPS1(c, ARM64Reg::D5, ctx.op.rb);
  EmitStorePS0(c, ARM64Reg::D0, ctx.op.rd);
  EmitStorePS1(c, ARM64Reg::D5, ctx.op.rd);
  ctx.End(c);
}

// 11. PS_MERGE00: vD = (vA.ps0, vB.ps0)
inline void Emit_ps_merge00(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16);
  c.LDR(IndexType::Unsigned, ARM64Reg::X2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.STR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.STR(IndexType::Unsigned, ARM64Reg::X2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16 + 8);
  ctx.End(c);
}

// 12. PS_MERGE01: vD = (vA.ps0, vB.ps1)
inline void Emit_ps_merge01(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16);
  c.LDR(IndexType::Unsigned, ARM64Reg::X2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16 + 8);
  c.STR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.STR(IndexType::Unsigned, ARM64Reg::X2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16 + 8);
  ctx.End(c);
}

// 13. PS_MERGE10: vD = (vA.ps1, vB.ps0)
inline void Emit_ps_merge10(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16 + 8);
  c.LDR(IndexType::Unsigned, ARM64Reg::X2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.STR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.STR(IndexType::Unsigned, ARM64Reg::X2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16 + 8);
  ctx.End(c);
}

// 14. PS_MERGE11: vD = (vA.ps1, vB.ps1)
inline void Emit_ps_merge11(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16 + 8);
  c.LDR(IndexType::Unsigned, ARM64Reg::X2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16 + 8);
  c.STR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.STR(IndexType::Unsigned, ARM64Reg::X2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16 + 8);
  ctx.End(c);
}

// 15. PSQ_L: Quantized Paired-Single Load
inline void Emit_psq_l(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  if (ctx.op.ra == 0)
    c.MOVI2R(ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  else
    c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);

  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  // Default dequantization: load 2 32-bit floats
  c.LDR(ARM64Reg::W1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.ADDI2R(ARM64Reg::W5, ARM64Reg::W0, 4, ARM64Reg::W16);
  c.LDR(ARM64Reg::W2, ARM64Reg::X28, ArithOption(ARM64Reg::W5, false));
  c.REV32(ARM64Reg::W1, ARM64Reg::W1);
  c.REV32(ARM64Reg::W2, ARM64Reg::W2);
  c.fp.FMOV(ARM64Reg::S0, ARM64Reg::W1);
  c.fp.FMOV(ARM64Reg::S1, ARM64Reg::W2);
  c.fp.FCVT(64, 32, ARM64Reg::D0, ARM64Reg::S0);
  c.fp.FCVT(64, 32, ARM64Reg::D1, ARM64Reg::S1);
  c.fp.STR(64, IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.fp.STR(64, IndexType::Unsigned, ARM64Reg::D1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16 + 8);
  ctx.End(c);
}

// 16. PSQ_ST: Quantized Paired-Single Store
inline void Emit_psq_st(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  if (ctx.op.ra == 0)
    c.MOVI2R(ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  else
    c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);

  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.fp.LDR(64, IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.fp.LDR(64, IndexType::Unsigned, ARM64Reg::D1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16 + 8);
  c.fp.FCVT(32, 64, ARM64Reg::S0, ARM64Reg::D0);
  c.fp.FCVT(32, 64, ARM64Reg::S1, ARM64Reg::D1);
  c.fp.FMOV(ARM64Reg::W1, ARM64Reg::S0);
  c.fp.FMOV(ARM64Reg::W2, ARM64Reg::S1);
  c.REV32(ARM64Reg::W1, ARM64Reg::W1);
  c.REV32(ARM64Reg::W2, ARM64Reg::W2);
  c.STR(ARM64Reg::W1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.ADDI2R(ARM64Reg::W5, ARM64Reg::W0, 4, ARM64Reg::W16);
  c.STR(ARM64Reg::W2, ARM64Reg::X28, ArithOption(ARM64Reg::W5, false));
  ctx.End(c);
}

}  // namespace StencilGen
