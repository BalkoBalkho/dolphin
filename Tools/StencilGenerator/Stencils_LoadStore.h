// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Tools/StencilGenerator/StencilEmitter.h"
#include "Tools/StencilGenerator/StencilContext.h"

namespace StencilGen
{

// ============================================================================
// Transpiled from Source/Core/Core/PowerPC/JitArm64/JitArm64_LoadStore.cpp
// Full PowerPC Load/Store Integer Opcode Coverage (Fast MEM1 via X28)
// ============================================================================

// Helper to compute effective address: if rA == 0, base is 0; otherwise rA + offset
inline void EmitComputeEA_Imm(StencilEmitter& c, StencilContext& ctx, ARM64Reg ea_reg, s16 offset)
{
  if (ctx.op.ra == 0)
  {
    c.MOVI2R(ea_reg, offset);
  }
  else
  {
    if (offset == 0)
      c.MOV(ea_reg, ctx.GetRA());
    else
      c.ADDI2R(ea_reg, ctx.GetRA(), offset, ARM64Reg::W16);
  }
}

// Helper to compute effective address: if rA == 0, base is 0; otherwise rA + rB
inline void EmitComputeEA_Reg(StencilEmitter& c, StencilContext& ctx, ARM64Reg ea_reg)
{
  if (ctx.op.ra == 0)
  {
    c.MOV(ea_reg, ctx.GetRB());
  }
  else
  {
    c.ADD(ea_reg, ctx.GetRA(), ctx.GetRB());
  }
}

// 1. LWZ: rD = MEM[EA]
inline void Emit_lwz(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Imm(c, ctx, ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV32(ctx.GetRD(), ctx.GetRD());
  ctx.End(c);
}

// 2. LWZU: rD = MEM[EA], rA = EA
inline void Emit_lwzu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV32(ctx.GetRD(), ctx.GetRD());
  ctx.End(c);
}

// 3. LWZX: rD = MEM[rA + rB]
inline void Emit_lwzx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Reg(c, ctx, ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV32(ctx.GetRD(), ctx.GetRD());
  ctx.End(c);
}

// 4. LWZUX: rD = MEM[rA + rB], rA = EA
inline void Emit_lwzux(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADD(ARM64Reg::W0, ctx.GetRA(), ctx.GetRB());
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV32(ctx.GetRD(), ctx.GetRD());
  ctx.End(c);
}

// 5. LBZ: rD = (u8)MEM[EA]
inline void Emit_lbz(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Imm(c, ctx, ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDRB(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 6. LBZU: rD = (u8)MEM[EA], rA = EA
inline void Emit_lbzu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDRB(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 7. LBZX: rD = (u8)MEM[rA + rB]
inline void Emit_lbzx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Reg(c, ctx, ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDRB(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 8. LBZUX: rD = (u8)MEM[rA + rB], rA = EA
inline void Emit_lbzux(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADD(ARM64Reg::W0, ctx.GetRA(), ctx.GetRB());
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDRB(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 9. LHZ: rD = (u16)MEM[EA]
inline void Emit_lhz(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Imm(c, ctx, ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDRH(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV16(ctx.GetRD(), ctx.GetRD());
  ctx.End(c);
}

// 10. LHZU: rD = (u16)MEM[EA], rA = EA
inline void Emit_lhzu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDRH(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV16(ctx.GetRD(), ctx.GetRD());
  ctx.End(c);
}

// 11. LHZX: rD = (u16)MEM[rA + rB]
inline void Emit_lhzx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Reg(c, ctx, ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDRH(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV16(ctx.GetRD(), ctx.GetRD());
  ctx.End(c);
}

// 12. LHZUX: rD = (u16)MEM[rA + rB], rA = EA
inline void Emit_lhzux(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADD(ARM64Reg::W0, ctx.GetRA(), ctx.GetRB());
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDRH(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV16(ctx.GetRD(), ctx.GetRD());
  ctx.End(c);
}

// 13. LHA: rD = (s16)MEM[EA] (sign-extended)
inline void Emit_lha(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Imm(c, ctx, ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDRH(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV16(ctx.GetRD(), ctx.GetRD());
  c.SXTH(ctx.GetRD(), ctx.GetRD());
  ctx.End(c);
}

// 14. LHAU: rD = (s16)MEM[EA], rA = EA
inline void Emit_lhau(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDRH(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV16(ctx.GetRD(), ctx.GetRD());
  c.SXTH(ctx.GetRD(), ctx.GetRD());
  ctx.End(c);
}

// 15. LHAX: rD = (s16)MEM[rA + rB]
inline void Emit_lhax(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Reg(c, ctx, ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDRH(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV16(ctx.GetRD(), ctx.GetRD());
  c.SXTH(ctx.GetRD(), ctx.GetRD());
  ctx.End(c);
}

// 16. LHAUX: rD = (s16)MEM[rA + rB], rA = EA
inline void Emit_lhaux(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADD(ARM64Reg::W0, ctx.GetRA(), ctx.GetRB());
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDRH(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV16(ctx.GetRD(), ctx.GetRD());
  c.SXTH(ctx.GetRD(), ctx.GetRD());
  ctx.End(c);
}

// 17. LWBRX: rD = byte_reversed(MEM[rA + rB]) (direct host endian ldr)
inline void Emit_lwbrx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Reg(c, ctx, ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 18. LHBRX: rD = byte_reversed(MEM[rA + rB])
inline void Emit_lhbrx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Reg(c, ctx, ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDRH(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 19. STW: MEM[EA] = rD
inline void Emit_stw(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Imm(c, ctx, ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.REV32(ARM64Reg::W5, ctx.GetRD());
  c.STR(ARM64Reg::W5, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 20. STWU: MEM[EA] = rD, rA = EA
inline void Emit_stwu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.REV32(ARM64Reg::W5, ctx.GetRD());
  c.STR(ARM64Reg::W5, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 21. STWX: MEM[rA + rB] = rD
inline void Emit_stwx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Reg(c, ctx, ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.REV32(ARM64Reg::W5, ctx.GetRD());
  c.STR(ARM64Reg::W5, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 22. STWUX: MEM[rA + rB] = rD, rA = EA
inline void Emit_stwux(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADD(ARM64Reg::W0, ctx.GetRA(), ctx.GetRB());
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.REV32(ARM64Reg::W5, ctx.GetRD());
  c.STR(ARM64Reg::W5, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 23. STB: MEM[EA] = (u8)rD
inline void Emit_stb(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Imm(c, ctx, ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.STRB(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 24. STBU: MEM[EA] = (u8)rD, rA = EA
inline void Emit_stbu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.STRB(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 25. STBX: MEM[rA + rB] = (u8)rD
inline void Emit_stbx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Reg(c, ctx, ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.STRB(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 26. STBUX: MEM[rA + rB] = (u8)rD, rA = EA
inline void Emit_stbux(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADD(ARM64Reg::W0, ctx.GetRA(), ctx.GetRB());
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.STRB(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 27. STH: MEM[EA] = (u16)rD
inline void Emit_sth(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Imm(c, ctx, ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.REV16(ARM64Reg::W5, ctx.GetRD());
  c.STRH(ARM64Reg::W5, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 28. STHU: MEM[EA] = (u16)rD, rA = EA
inline void Emit_sthu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.REV16(ARM64Reg::W5, ctx.GetRD());
  c.STRH(ARM64Reg::W5, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 29. STHX: MEM[rA + rB] = (u16)rD
inline void Emit_sthx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Reg(c, ctx, ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.REV16(ARM64Reg::W5, ctx.GetRD());
  c.STRH(ARM64Reg::W5, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 30. STHUX: MEM[rA + rB] = (u16)rD, rA = EA
inline void Emit_sthux(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADD(ARM64Reg::W0, ctx.GetRA(), ctx.GetRB());
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.REV16(ARM64Reg::W5, ctx.GetRD());
  c.STRH(ARM64Reg::W5, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 31. STWBRX: MEM[rA + rB] = byte_reversed(rD) (direct host endian str)
inline void Emit_stwbrx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Reg(c, ctx, ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.STR(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 32. STHBRX: MEM[rA + rB] = byte_reversed((u16)rD)
inline void Emit_sthbrx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Reg(c, ctx, ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.STRH(ctx.GetRD(), ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 33. LMW: Load Multiple Word
inline void Emit_lmw(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Imm(c, ctx, ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  for (int reg = ctx.op.rd; reg < 32; ++reg)
  {
    c.ANDI2R(ARM64Reg::W5, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
    c.LDR(ARM64Reg::W6, ARM64Reg::X28, ArithOption(ARM64Reg::W5, false));
    c.REV32(ARM64Reg::W6, ARM64Reg::W6);
    ctx.StoreGPR(c, ARM64Reg::W6, reg);
    c.ADDI2R(ARM64Reg::W0, ARM64Reg::W0, 4, ARM64Reg::W16);
  }
  ctx.End(c);
}

// 34. STMW: Store Multiple Word
inline void Emit_stmw(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  EmitComputeEA_Imm(c, ctx, ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  for (int reg = ctx.op.rd; reg < 32; ++reg)
  {
    c.ANDI2R(ARM64Reg::W5, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
    ctx.LoadGPR(c, ARM64Reg::W6, reg);
    c.REV32(ARM64Reg::W6, ARM64Reg::W6);
    c.STR(ARM64Reg::W6, ARM64Reg::X28, ArithOption(ARM64Reg::W5, false));
    c.ADDI2R(ARM64Reg::W0, ARM64Reg::W0, 4, ARM64Reg::W16);
  }
  ctx.End(c);
}

}  // namespace StencilGen
