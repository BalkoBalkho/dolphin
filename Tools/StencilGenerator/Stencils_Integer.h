// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Tools/StencilGenerator/StencilEmitter.h"
#include "Tools/StencilGenerator/StencilContext.h"

namespace StencilGen
{

// ============================================================================
// Transpiled from Source/Core/Core/PowerPC/JitArm64/JitArm64_Integer.cpp
// Full PowerPC Integer Arithmetic Opcode Coverage
// ============================================================================

// 1. ADD: rD = rA + rB
inline void Emit_add(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADD(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 2. ADDC: rD = rA + rB, Carry Out
inline void Emit_addc(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADDS(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  c.CSET(ARM64Reg::W16, CC_CS);
  c.EmitSaveCarry(ARM64Reg::W16);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 3. ADDE: rD = rA + rB + Carry In, Carry Out
inline void Emit_adde(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.EmitLoadCarry(ARM64Reg::W16);
  c.CMP(ARM64Reg::W16, 1);
  c.ADCS(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  c.CSET(ARM64Reg::W16, CC_CS);
  c.EmitSaveCarry(ARM64Reg::W16);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 4. ADDME: rD = rA + Carry In - 1 (rA + CA + 0xFFFFFFFF)
inline void Emit_addme(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.EmitLoadCarry(ARM64Reg::W16);
  c.CMP(ARM64Reg::W16, 1);
  c.MOVI2R(ARM64Reg::W5, 0xFFFFFFFF);
  c.ADCS(ctx.GetRD(), ctx.GetRA(), ARM64Reg::W5);
  c.CSET(ARM64Reg::W16, CC_CS);
  c.EmitSaveCarry(ARM64Reg::W16);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 5. ADDZE: rD = rA + Carry In
inline void Emit_addze(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.EmitLoadCarry(ARM64Reg::W16);
  c.CMP(ARM64Reg::W16, 1);
  c.ADCS(ctx.GetRD(), ctx.GetRA(), ARM64Reg::WZR);
  c.CSET(ARM64Reg::W16, CC_CS);
  c.EmitSaveCarry(ARM64Reg::W16);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 6. SUBF: rD = rB - rA (PowerPC order: rB minus rA!)
inline void Emit_subf(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.SUB(ctx.GetRD(), ctx.GetRB(), ctx.GetRA());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 7. SUBFC: rD = rB - rA, Carry Out (Carry = rB >= rA == ARM64 CS)
inline void Emit_subfc(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.SUBS(ctx.GetRD(), ctx.GetRB(), ctx.GetRA());
  c.CSET(ARM64Reg::W16, CC_CS);
  c.EmitSaveCarry(ARM64Reg::W16);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 8. SUBFE: rD = rB - rA + Carry In - 1, Carry Out (rB + ~rA + CA)
inline void Emit_subfe(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.EmitLoadCarry(ARM64Reg::W16);
  c.CMP(ARM64Reg::W16, 1);
  c.SBCS(ctx.GetRD(), ctx.GetRB(), ctx.GetRA());
  c.CSET(ARM64Reg::W16, CC_CS);
  c.EmitSaveCarry(ARM64Reg::W16);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 9. SUBFME: rD = ~rA + Carry In (rB = -1)
inline void Emit_subfme(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.EmitLoadCarry(ARM64Reg::W16);
  c.CMP(ARM64Reg::W16, 1);
  c.MOVI2R(ARM64Reg::W5, 0xFFFFFFFF);
  c.SBCS(ctx.GetRD(), ARM64Reg::W5, ctx.GetRA());
  c.CSET(ARM64Reg::W16, CC_CS);
  c.EmitSaveCarry(ARM64Reg::W16);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 10. SUBFZE: rD = ~rA + Carry In (rB = 0)
inline void Emit_subfze(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.EmitLoadCarry(ARM64Reg::W16);
  c.CMP(ARM64Reg::W16, 1);
  c.SBCS(ctx.GetRD(), ARM64Reg::WZR, ctx.GetRA());
  c.CSET(ARM64Reg::W16, CC_CS);
  c.EmitSaveCarry(ARM64Reg::W16);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 11. NEG: rD = -rA
inline void Emit_neg(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.NEG(ctx.GetRD(), ctx.GetRA());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 12. EXTSB: rD = (s8)rA
inline void Emit_extsb(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.SBFM(ctx.GetRD(), ctx.GetRA(), 0, 7);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 13. EXTSH: rD = (s16)rA
inline void Emit_extsh(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.SBFM(ctx.GetRD(), ctx.GetRA(), 0, 15);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 14. CNTLZW: rD = count_leading_zeros(rA)
inline void Emit_cntlzw(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.CLZ(ctx.GetRD(), ctx.GetRA());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 15. MULLW: rD = rA * rB (32-bit signed multiply low)
inline void Emit_mullw(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.MUL(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 16. MULHW: rD = (rA * rB) >> 32 (signed multiply high)
inline void Emit_mulhw(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.SMULL(ARM64Reg::X5, ctx.GetRA(), ctx.GetRB());
  c.UBFM(ARM64Reg::X5, ARM64Reg::X5, 32, 63);
  c.MOV(ctx.GetRD(), ARM64Reg::W5);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 17. MULHWU: rD = (rA * rB) >> 32 (unsigned multiply high)
inline void Emit_mulhwu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.UMULL(ARM64Reg::X5, ctx.GetRA(), ctx.GetRB());
  c.UBFM(ARM64Reg::X5, ARM64Reg::X5, 32, 63);
  c.MOV(ctx.GetRD(), ARM64Reg::W5);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 18. DIVW: rD = (s32)rA / (s32)rB
inline void Emit_divw(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  // PowerPC handles divide-by-zero and overflow (0x80000000 / -1) gracefully
  c.SDIV(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 19. DIVWU: rD = (u32)rA / (u32)rB
inline void Emit_divwu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.UDIV(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 20. ADDI / LI: rD = (rA == 0 ? 0 : rA) + SIMM16
inline void Emit_addi(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  if (ctx.op.ra == 0)
  {
    c.MOVI2R(ctx.GetRD(), static_cast<s16>(ctx.op.imm));
  }
  else
  {
    c.ADDI2R(ctx.GetRD(), ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  }
  ctx.End(c);
}

// 21. ADDIS / LIS: rD = (rA == 0 ? 0 : rA) + (SIMM16 << 16)
inline void Emit_addis(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  const u32 shifted = (ctx.op.imm & 0xFFFF) << 16;
  if (ctx.op.ra == 0)
  {
    c.MOVI2R(ctx.GetRD(), shifted);
  }
  else
  {
    c.ADDI2R(ctx.GetRD(), ctx.GetRA(), shifted, ARM64Reg::W16);
  }
  ctx.End(c);
}

// 22. ADDIC: rD = rA + SIMM16, Carry Out
inline void Emit_addic(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.MOVI2R(ARM64Reg::W5, static_cast<s16>(ctx.op.imm));
  c.ADDS(ctx.GetRD(), ctx.GetRA(), ARM64Reg::W5);
  c.CSET(ARM64Reg::W16, CC_CS);
  c.EmitSaveCarry(ARM64Reg::W16);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 23. SUBFIC: rD = SIMM16 - rA, Carry Out
inline void Emit_subfic(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.MOVI2R(ARM64Reg::W5, static_cast<s16>(ctx.op.imm));
  c.SUBS(ctx.GetRD(), ARM64Reg::W5, ctx.GetRA());
  c.CSET(ARM64Reg::W16, CC_CS);
  c.EmitSaveCarry(ARM64Reg::W16);
  ctx.End(c);
}

// 24. MULLI: rD = rA * SIMM16
inline void Emit_mulli(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.MOVI2R(ARM64Reg::W5, static_cast<s16>(ctx.op.imm));
  c.MUL(ctx.GetRD(), ctx.GetRA(), ARM64Reg::W5);
  ctx.End(c);
}

}  // namespace StencilGen
