// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Tools/StencilGenerator/StencilEmitter.h"
#include "Tools/StencilGenerator/StencilContext.h"
#include "Tools/StencilGenerator/Stencils_All.h"

namespace StencilGen
{

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

// PPC 'ori' (OPCD 24)
inline void Emit_ori_op(StencilEmitter& c, StencilContext& ctx)
{
  ctx.LoadGPR(c, ARM64Reg::W0, ctx.op.ra);
  c.ORRI2R(ARM64Reg::W0, ARM64Reg::W0, ctx.op.imm & 0xFFFF, ARM64Reg::W16);
  ctx.StoreGPR(c, ARM64Reg::W0, ctx.op.rd);
  if (ctx.op.rc)
    c.EmitUpdateCR0(ARM64Reg::W0, ARM64Reg::X16);
}

// PPC 'cmpwi' (OPCD 11) - Compare Signed Immediate
inline void Emit_cmpwi_op(StencilEmitter& c, StencilContext& ctx)
{
  ctx.LoadGPR(c, ARM64Reg::W0, ctx.op.ra);
  c.CMPI2R(ARM64Reg::W0, static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
}

// PPC 'cmplwi' (OPCD 10) - Compare Logical Immediate
inline void Emit_cmplwi_op(StencilEmitter& c, StencilContext& ctx)
{
  ctx.LoadGPR(c, ARM64Reg::W0, ctx.op.ra);
  c.CMPI2R(ARM64Reg::W0, ctx.op.imm & 0xFFFF, ARM64Reg::W16);
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

// PPC 'stw' (OPCD 36) - Store Word
inline void Emit_stw_op(StencilEmitter& c, StencilContext& ctx)
{
  ctx.LoadBaseAddress(c, ARM64Reg::W0, ctx.op.ra, static_cast<s16>(ctx.op.imm));
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  ctx.LoadGPR(c, ARM64Reg::W1, ctx.op.rd);
  c.REV32(ARM64Reg::W1, ARM64Reg::W1);
  c.STR(ARM64Reg::W1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
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

// PPC 'mflr' (mfspr rD, 8)
inline void Emit_mflr_op(StencilEmitter& c, StencilContext& ctx)
{
  c.LDR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, OFF_PPCSTATE_SPR_LR);
  ctx.StoreGPR(c, ARM64Reg::W0, ctx.op.rd);
}

// PPC 'mtlr' (mtspr 8, rS)
inline void Emit_mtlr_op(StencilEmitter& c, StencilContext& ctx)
{
  ctx.LoadGPR(c, ARM64Reg::W0, ctx.op.rd);
  c.STR(IndexType::Unsigned, ARM64Reg::W0, ARM64Reg::X19, OFF_PPCSTATE_SPR_LR);
}

// PPC 'blr'
inline void Emit_blr_op(StencilEmitter& c, StencilContext& ctx)
{
  c.LDR(IndexType::Unsigned, ARM64Reg::X16, ARM64Reg::X19, OFF_PPCSTATE_SPR_LR);
  c.BR(ARM64Reg::X16);
}

}  // namespace StencilGen
