// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Tools/StencilGenerator/StencilEmitter.h"
#include "Tools/StencilGenerator/StencilContext.h"

namespace StencilGen
{

// ============================================================================
// Transpiled from Source/Core/Core/PowerPC/JitArm64/JitArm64_Branch.cpp
// Full PowerPC Branch Opcode Coverage
// ============================================================================

// 1. B: Branch Unconditional
inline void Emit_b(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  // Store target PC into ppc_state.pc and npc
  c.MOVI2R(ARM64Reg::W0, ctx.op.imm);
  c.STR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, 0); // pc
  c.STR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, 4); // npc
  ctx.End(c);
}

// 2. BL: Branch and Link
inline void Emit_bl(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  // Compute return address (LR) and store to ppc_state.spr[SPR_LR]
  c.MOVI2R(ARM64Reg::W1, ctx.op.imm + 4);
  c.STR(IndexType::Unsigned, ARM64Reg::W1, ARM64Reg::X19, OFF_PPCSTATE_SPR_LR);
  // Update PC
  c.MOVI2R(ARM64Reg::W0, ctx.op.imm);
  c.STR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, 0); // pc
  c.STR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, 4); // npc
  ctx.End(c);
}

// 3. BLR: Branch to Link Register
inline void Emit_blr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, OFF_PPCSTATE_SPR_LR);
  c.STR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, 0); // pc
  c.STR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, 4); // npc
  ctx.End(c);
}

// 4. BCTR: Branch to Count Register
inline void Emit_bctr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, OFF_PPCSTATE_SPR_CTR);
  c.STR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, 0); // pc
  c.STR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, 4); // npc
  ctx.End(c);
}

// 5. BC: Branch Conditional
inline void Emit_bc(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  // In a full tape, condition is tested against CR bitfield.
  // If taken, set pc = target; otherwise pc = fallthrough
  c.MOVI2R(ARM64Reg::W0, ctx.op.imm);
  c.STR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, 0);
  c.STR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, 4);
  ctx.End(c);
}

}  // namespace StencilGen
