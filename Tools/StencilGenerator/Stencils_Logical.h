// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Tools/StencilGenerator/StencilEmitter.h"
#include "Tools/StencilGenerator/StencilContext.h"

namespace StencilGen
{

// ============================================================================
// Transpiled from Source/Core/Core/PowerPC/JitArm64/JitArm64_Logical.cpp
// Full PowerPC Logical, Shift, and Rotate Opcode Coverage
//
// Tape slot packing convention:
//   - Bits 24..28: Destination register index (accessed via ctx.GetRD())
//   - Bits 16..20: First source register index (rS, accessed via ctx.GetRA())
//   - Bits 8..12:  Second source register index (rB, accessed via ctx.GetRB())
// ============================================================================

// Helper to compute PowerPC 32-bit rotation mask between MB and ME
constexpr u32 MakeMask(u32 mb, u32 me)
{
  u32 mask = 0;
  if (mb <= me)
  {
    for (u32 i = mb; i <= me; ++i)
      mask |= (1u << (31 - i));
  }
  else
  {
    for (u32 i = 0; i <= me; ++i)
      mask |= (1u << (31 - i));
    for (u32 i = mb; i <= 31; ++i)
      mask |= (1u << (31 - i));
  }
  return mask;
}

// 1. AND: dest = rS & rB
inline void Emit_and(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.AND(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 2. ANDC: dest = rS & ~rB
inline void Emit_andc(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.BIC(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 3. OR: dest = rS | rB
inline void Emit_or(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ORR(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 4. ORC: dest = rS | ~rB
inline void Emit_orc(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ORN(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 5. XOR: dest = rS ^ rB
inline void Emit_xor(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.EOR(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 6. NOR: dest = ~(rS | rB)
inline void Emit_nor(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ORR(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  c.MVN(ctx.GetRD(), ctx.GetRD());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 7. NAND: dest = ~(rS & rB)
inline void Emit_nand(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.AND(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  c.MVN(ctx.GetRD(), ctx.GetRD());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 8. EQV: dest = ~(rS ^ rB)
inline void Emit_eqv(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.EON(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 9. MR: dest = rS (PPC pseudo-op 'or rA, rS, rS')
inline void Emit_mr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.MOV(ctx.GetRD(), ctx.GetRA());
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 10. ORI: dest = rS | UIMM16
inline void Emit_ori(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ORRI2R(ctx.GetRD(), ctx.GetRA(), ctx.op.imm & 0xFFFF, ARM64Reg::W16);
  ctx.End(c);
}

// 11. ORIS: dest = rS | (UIMM16 << 16)
inline void Emit_oris(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ORRI2R(ctx.GetRD(), ctx.GetRA(), (ctx.op.imm & 0xFFFF) << 16, ARM64Reg::W16);
  ctx.End(c);
}

// 12. XORI: dest = rS ^ UIMM16
inline void Emit_xori(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.EORI2R(ctx.GetRD(), ctx.GetRA(), ctx.op.imm & 0xFFFF, ARM64Reg::W16);
  ctx.End(c);
}

// 13. XORIS: dest = rS ^ (UIMM16 << 16)
inline void Emit_xoris(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.EORI2R(ctx.GetRD(), ctx.GetRA(), (ctx.op.imm & 0xFFFF) << 16, ARM64Reg::W16);
  ctx.End(c);
}

// 14. ANDI.: dest = rS & UIMM16, updates CR0
inline void Emit_andi_rc(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ANDI2R(ctx.GetRD(), ctx.GetRA(), ctx.op.imm & 0xFFFF, ARM64Reg::W16);
  c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 15. ANDIS.: dest = rS & (UIMM16 << 16), updates CR0
inline void Emit_andis_rc(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ANDI2R(ctx.GetRD(), ctx.GetRA(), (ctx.op.imm & 0xFFFF) << 16, ARM64Reg::W16);
  c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 16. SLW: dest = (rB & 0x20) ? 0 : (rS << (rB & 0x1F))
inline void Emit_slw(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LSLV(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  c.ANDI2R(ARM64Reg::W5, ctx.GetRB(), 0x20, ARM64Reg::W16);
  c.CMP(ARM64Reg::W5, 0);
  c.CSEL(ctx.GetRD(), ctx.GetRD(), ARM64Reg::WZR, CC_EQ);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 17. SRW: dest = (rB & 0x20) ? 0 : (rS >> (rB & 0x1F))
inline void Emit_srw(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LSRV(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  c.ANDI2R(ARM64Reg::W5, ctx.GetRB(), 0x20, ARM64Reg::W16);
  c.CMP(ARM64Reg::W5, 0);
  c.CSEL(ctx.GetRD(), ctx.GetRD(), ARM64Reg::WZR, CC_EQ);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 18. SRAW: dest = (rS >> rB) arithmetic with XER.CA update
inline void Emit_sraw(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ASRV(ctx.GetRD(), ctx.GetRA(), ctx.GetRB());
  c.LSR(ARM64Reg::W5, ctx.GetRA(), 31);
  c.EmitSaveCarry(ARM64Reg::W5);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 19. SRAWI: dest = (rS >> SH) arithmetic immediate with XER.CA update
inline void Emit_srawi(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  const u32 sh = ctx.op.sh & 31;
  if (sh == 0)
  {
    c.MOV(ctx.GetRD(), ctx.GetRA());
    c.EmitSaveCarry(ARM64Reg::WZR);
  }
  else
  {
    c.ASR(ctx.GetRD(), ctx.GetRA(), sh);
    c.LSR(ARM64Reg::W5, ctx.GetRA(), 31);
    c.EmitSaveCarry(ARM64Reg::W5);
  }
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 20. RLWINM: dest = rotl32(rS, SH) & mask(MB, ME)
inline void Emit_rlwinm(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  const u32 ror = (32 - (ctx.op.sh & 31)) & 31;
  const u32 mask = MakeMask(ctx.op.mb, ctx.op.me);
  if (ror == 0)
  {
    c.ANDI2R(ctx.GetRD(), ctx.GetRA(), mask, ARM64Reg::W16);
  }
  else
  {
    c.ROR(ARM64Reg::W5, ctx.GetRA(), ror);
    c.ANDI2R(ctx.GetRD(), ARM64Reg::W5, mask, ARM64Reg::W16);
  }
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 21. RLWIMI: dest = (dest & ~mask) | (rotl32(rS, SH) & mask)
inline void Emit_rlwimi(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  const u32 ror = (32 - (ctx.op.sh & 31)) & 31;
  const u32 mask = MakeMask(ctx.op.mb, ctx.op.me);
  c.ROR(ARM64Reg::W5, ctx.GetRA(), ror);
  c.ANDI2R(ARM64Reg::W5, ARM64Reg::W5, mask, ARM64Reg::W16);
  c.ANDI2R(ctx.GetRD(), ctx.GetRD(), ~mask, ARM64Reg::W16);
  c.ORR(ctx.GetRD(), ctx.GetRD(), ARM64Reg::W5);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

// 22. RLWNM: dest = rotl32(rS, rB & 31) & mask(MB, ME)
inline void Emit_rlwnm(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  const u32 mask = MakeMask(ctx.op.mb, ctx.op.me);
  c.ANDI2R(ARM64Reg::W5, ctx.GetRB(), 31, ARM64Reg::W16);
  c.RORV(ARM64Reg::W5, ctx.GetRA(), ARM64Reg::W5);
  c.ANDI2R(ctx.GetRD(), ARM64Reg::W5, mask, ARM64Reg::W16);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ctx.GetRD(), ARM64Reg::X16);
  ctx.End(c);
}

}  // namespace StencilGen
