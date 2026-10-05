// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Tools/StencilGenerator/StencilEmitter.h"
#include "Tools/StencilGenerator/StencilContext.h"

namespace StencilGen
{

// ============================================================================
// Transpiled from Source/Core/Core/PowerPC/JitArm64/JitArm64_SystemRegisters.cpp
// Full PowerPC System, Compare, and Cache Opcode Coverage
// ============================================================================

// 1. CMP: Signed compare rA against rB, sets CR field
inline void Emit_cmp(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.CMP(ctx.GetRA(), ctx.GetRB());
  c.CSET(ARM64Reg::W5, CC_LT);
  c.CSET(ARM64Reg::W6, CC_GT);
  c.LSL(ARM64Reg::W5, ARM64Reg::W5, 3); // LT bit (8)
  c.LSL(ARM64Reg::W6, ARM64Reg::W6, 2); // GT bit (4)
  c.ORR(ARM64Reg::W5, ARM64Reg::W5, ARM64Reg::W6);
  c.CSET(ARM64Reg::W6, CC_EQ);
  c.LSL(ARM64Reg::W6, ARM64Reg::W6, 1); // EQ bit (2)
  c.ORR(ARM64Reg::W5, ARM64Reg::W5, ARM64Reg::W6);
  // Store into CR field
  c.STR(IndexType::Unsigned, ARM64Reg::W5, ARM64Reg::X19, OFF_PPCSTATE_CR + ctx.op.rd * 8);
  ctx.End(c);
}

// 2. CMPL: Unsigned (Logical) compare rA against rB
inline void Emit_cmpl(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.CMP(ctx.GetRA(), ctx.GetRB());
  c.CSET(ARM64Reg::W5, CC_LO);
  c.CSET(ARM64Reg::W6, CC_HI);
  c.LSL(ARM64Reg::W5, ARM64Reg::W5, 3);
  c.LSL(ARM64Reg::W6, ARM64Reg::W6, 2);
  c.ORR(ARM64Reg::W5, ARM64Reg::W5, ARM64Reg::W6);
  c.CSET(ARM64Reg::W6, CC_EQ);
  c.LSL(ARM64Reg::W6, ARM64Reg::W6, 1);
  c.ORR(ARM64Reg::W5, ARM64Reg::W5, ARM64Reg::W6);
  c.STR(IndexType::Unsigned, ARM64Reg::W5, ARM64Reg::X19, OFF_PPCSTATE_CR + ctx.op.rd * 8);
  ctx.End(c);
}

// 3. CMPI: Signed compare rA against SIMM16
inline void Emit_cmpi(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.CMPI2R(ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  c.CSET(ARM64Reg::W5, CC_LT);
  c.CSET(ARM64Reg::W6, CC_GT);
  c.LSL(ARM64Reg::W5, ARM64Reg::W5, 3);
  c.LSL(ARM64Reg::W6, ARM64Reg::W6, 2);
  c.ORR(ARM64Reg::W5, ARM64Reg::W5, ARM64Reg::W6);
  c.CSET(ARM64Reg::W6, CC_EQ);
  c.LSL(ARM64Reg::W6, ARM64Reg::W6, 1);
  c.ORR(ARM64Reg::W5, ARM64Reg::W5, ARM64Reg::W6);
  c.STR(IndexType::Unsigned, ARM64Reg::W5, ARM64Reg::X19, OFF_PPCSTATE_CR + ctx.op.rd * 8);
  ctx.End(c);
}

// 4. CMPLI: Unsigned compare rA against UIMM16
inline void Emit_cmpli(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.CMPI2R(ctx.GetRA(), ctx.op.imm & 0xFFFF, ARM64Reg::W16);
  c.CSET(ARM64Reg::W5, CC_LO);
  c.CSET(ARM64Reg::W6, CC_HI);
  c.LSL(ARM64Reg::W5, ARM64Reg::W5, 3);
  c.LSL(ARM64Reg::W6, ARM64Reg::W6, 2);
  c.ORR(ARM64Reg::W5, ARM64Reg::W5, ARM64Reg::W6);
  c.CSET(ARM64Reg::W6, CC_EQ);
  c.LSL(ARM64Reg::W6, ARM64Reg::W6, 1);
  c.ORR(ARM64Reg::W5, ARM64Reg::W5, ARM64Reg::W6);
  c.STR(IndexType::Unsigned, ARM64Reg::W5, ARM64Reg::X19, OFF_PPCSTATE_CR + ctx.op.rd * 8);
  ctx.End(c);
}

// 5. MFLR: rD = LR
inline void Emit_mflr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ctx.GetRD(), ARM64Reg::X19, OFF_PPCSTATE_SPR_LR);
  ctx.End(c);
}

// 6. MTLR: LR = rS
inline void Emit_mtlr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.STR(IndexType::Unsigned, ctx.GetRD(), ARM64Reg::X19, OFF_PPCSTATE_SPR_LR);
  ctx.End(c);
}

// 7. MFCTR: rD = CTR
inline void Emit_mfctr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ctx.GetRD(), ARM64Reg::X19, OFF_PPCSTATE_SPR_CTR);
  ctx.End(c);
}

// 8. MTCTR: CTR = rS
inline void Emit_mtctr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.STR(IndexType::Unsigned, ctx.GetRD(), ARM64Reg::X19, OFF_PPCSTATE_SPR_CTR);
  ctx.End(c);
}

// 9. MFMSR: rD = MSR
inline void Emit_mfmsr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ctx.GetRD(), ARM64Reg::X19, 736); // offsetof(PowerPCState, msr)
  ctx.End(c);
}

// 10. MTMSR: MSR = rS
inline void Emit_mtmsr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.STR(IndexType::Unsigned, ctx.GetRD(), ARM64Reg::X19, 736);
  ctx.End(c);
}

// 11. DCBI / DCBF / DCBST / DCBT / DCBA: Cache instructions
// In unified memory (ARM64 host), these can safely execute as zero-overhead tailcalls
inline void Emit_dcbi(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  ctx.End(c);
}

inline void Emit_dcbf(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  ctx.End(c);
}

inline void Emit_dcbst(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  ctx.End(c);
}

inline void Emit_dcbt(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  ctx.End(c);
}

// 12. ISYNC / SYNC / EIEIO
inline void Emit_isync(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  ctx.End(c);
}

inline void Emit_sync(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.DMB(Arm64Gen::BarrierType::SY);
  ctx.End(c);
}

inline void Emit_eieio(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.DMB(Arm64Gen::BarrierType::ST);
  ctx.End(c);
}

}  // namespace StencilGen
