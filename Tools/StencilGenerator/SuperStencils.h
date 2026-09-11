// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Tools/StencilGenerator/StencilEmitter.h"
#include "Tools/StencilGenerator/StencilContext.h"
#include "Tools/StencilGenerator/ExampleStencils.h"

// Declarations of slow-path memory helpers
extern "C" {
uint32_t Stencil_SlowReadU32(void* s, uint32_t a);
void Stencil_SlowWriteU32(void* s, uint32_t a, uint32_t v);
}

namespace StencilGen
{

// ============================================================================
// Super-Instruction Emitters (Fused Multi-Opcode Stencils)
// ============================================================================

// 1. Fused 'lis' + 'addi' (or 'ori') -> Load 32-bit constant (li32)
inline void Emit_li32(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.MOVI2R(ctx.GetRD(), ctx.op.imm);
  ctx.End(c);
}

inline void Emit_super_lis_addi(StencilEmitter& c, StencilContext& ctx1, StencilContext& ctx2)
{
  Emit_addis_op(c, ctx1);
  Emit_addi_op(c, ctx2);
  c.EmitTailCall();
}

inline void Emit_super_lis_ori(StencilEmitter& c, StencilContext& ctx1, StencilContext& ctx2)
{
  Emit_addis_op(c, ctx1);
  Emit_ori_op(c, ctx2);
  c.EmitTailCall();
}

// 2. Fused 'lwz' + 'lwz' (Load Pair)
inline void Emit_super_lwz_lwz(StencilEmitter& c, StencilContext& ctx1, StencilContext& ctx2)
{
  Emit_lwz_op(c, ctx1);
  Emit_lwz_op(c, ctx2);
  c.EmitTailCall();
}

inline void Emit_ldp(StencilEmitter& c, StencilContext& ctx)
{
  StencilContext ctx1 = ctx;
  StencilContext ctx2 = ctx;
  ctx2.op.rd = ctx.op.rb;
  ctx2.op.imm = ctx.op.imm + 4;
  Emit_super_lwz_lwz(c, ctx1, ctx2);
}

// 3. Fused 'stw' + 'stw' (Store Pair)
inline void Emit_super_stw_stw(StencilEmitter& c, StencilContext& ctx1, StencilContext& ctx2)
{
  Emit_stw_op(c, ctx1);
  Emit_stw_op(c, ctx2);
  c.EmitTailCall();
}

inline void Emit_stp(StencilEmitter& c, StencilContext& ctx)
{
  StencilContext ctx1 = ctx;
  StencilContext ctx2 = ctx;
  ctx2.op.rd = ctx.op.rb;
  ctx2.op.imm = ctx.op.imm + 4;
  Emit_super_stw_stw(c, ctx1, ctx2);
}

// 4. Fused 'stwu' + 'mflr' (Function Prologue)
inline void Emit_super_stwu_mflr(StencilEmitter& c, StencilContext& ctx1, StencilContext& ctx2)
{
  Emit_stwu_op(c, ctx1);
  Emit_mflr_op(c, ctx2);
  c.EmitTailCall();
}

// 5. Fused 'mtlr' + 'addi' + 'blr' (Function Epilogue)
inline void Emit_super_mtlr_addi_blr(StencilEmitter& c, StencilContext& ctx1, StencilContext& ctx2, StencilContext& ctx3)
{
  Emit_mtlr_op(c, ctx1);
  Emit_addi_op(c, ctx2);
  Emit_blr_op(c, ctx3);
}

// 6. Fused 'mr' + 'mr'
inline void Emit_super_mr_mr(StencilEmitter& c, StencilContext& ctx1, StencilContext& ctx2)
{
  Emit_mr_op(c, ctx1);
  Emit_mr_op(c, ctx2);
  c.EmitTailCall();
}

// 7. Fused 'lwz' + 'cmpwi'
inline void Emit_super_lwz_cmpwi(StencilEmitter& c, StencilContext& ctx1, StencilContext& ctx2)
{
  Emit_lwz_op(c, ctx1);
  Emit_cmpwi_op(c, ctx2);
  c.EmitTailCall();
}

// 8. Fused 'cmpwi' + conditional branch ('beq' / 'bne')
inline void Emit_super_cmpwi_bcx(StencilEmitter& c, StencilContext& ctx, CCFlags cond)
{
  Emit_cmpwi_op(c, ctx);
  FixupBranch taken = c.B(cond);
  c.EmitTailCall(); // Fallthrough to next tape slot
  c.SetJumpTarget(taken);
  // Branch taken: jump to target tape slot at [X20, #8]
  c.LDR(IndexType::Unsigned, ARM64Reg::X20, ARM64Reg::X20, 8);
  c.LDR(IndexType::Unsigned, ARM64Reg::X16, ARM64Reg::X20, 0);
  c.BR(ARM64Reg::X16);
}

// 9. Fused 'addi' + 'addi' (or 'li' + 'li')
inline void Emit_super_addi_addi(StencilEmitter& c, StencilContext& ctx1, StencilContext& ctx2)
{
  Emit_addi_op(c, ctx1);
  Emit_addi_op(c, ctx2);
  c.EmitTailCall();
}

// 10. Fused 'lwz' + 'stw'
inline void Emit_super_lwz_stw(StencilEmitter& c, StencilContext& ctx1, StencilContext& ctx2)
{
  Emit_lwz_op(c, ctx1);
  Emit_stw_op(c, ctx2);
  c.EmitTailCall();
}

// 11. Fused 'stw' + 'lwz'
inline void Emit_super_stw_lwz(StencilEmitter& c, StencilContext& ctx1, StencilContext& ctx2)
{
  Emit_stw_op(c, ctx1);
  Emit_lwz_op(c, ctx2);
  c.EmitTailCall();
}

// 12. Fused 'stw' + 'mr'
inline void Emit_super_stw_mr(StencilEmitter& c, StencilContext& ctx1, StencilContext& ctx2)
{
  Emit_stw_op(c, ctx1);
  Emit_mr_op(c, ctx2);
  c.EmitTailCall();
}

// 13. Fused 'addi' + 'stw' (or 'li' + 'stw')
inline void Emit_super_addi_stw(StencilEmitter& c, StencilContext& ctx1, StencilContext& ctx2)
{
  Emit_addi_op(c, ctx1);
  Emit_stw_op(c, ctx2);
  c.EmitTailCall();
}

}  // namespace StencilGen
