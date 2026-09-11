// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Tools/StencilGenerator/StencilEmitter.h"

namespace StencilGen
{

// Operation type descriptor
enum class StencilMode
{
  Pinned,   // Direct register access via pinned host registers {W23, W21, W22, W24, W25}
  Dynamic,  // Universal fallback via X27 L1 indexing using tape operands in [X20, #8]
};

struct StencilOperands
{
  int rd = -1;  // PPC destination register index (0..31) or pinned index (0..4)
  int ra = -1;  // PPC source A register index (0..31) or pinned index (0..4)
  int rb = -1;  // PPC source B register index (0..31) or pinned index (0..4)
  bool rc = false; // PowerPC Record flag (updates CR0)
  bool oe = false; // PowerPC Overflow Enable flag (updates XER.OV/SO)

  u32 imm = 0;
  u32 sh = 0;
  u32 mb = 0;
  u32 me = 0;

  u32 bo = 0;
  u32 bi = 0;
};

class StencilContext
{
public:
  StencilMode mode = StencilMode::Pinned;
  StencilOperands op;

  // Scratch registers allocated for dynamic mode (distinct from X1 packed operand)
  ARM64Reg reg_packed = ARM64Reg::X1;
  ARM64Reg scratch_ra = ARM64Reg::W2;
  ARM64Reg scratch_rb = ARM64Reg::W3;
  ARM64Reg scratch_rd = ARM64Reg::W4;
  ARM64Reg scratch_tmp = ARM64Reg::W5;

  // Returns the host register mapped to the given role
  ARM64Reg GetRD() const
  {
    if (mode == StencilMode::Pinned)
      return kPinnedRegsW[op.rd];
    return scratch_rd;
  }

  ARM64Reg GetRA() const
  {
    if (mode == StencilMode::Pinned)
      return kPinnedRegsW[op.ra];
    return scratch_ra;
  }

  ARM64Reg GetRB() const
  {
    if (mode == StencilMode::Pinned)
      return kPinnedRegsW[op.rb];
    return scratch_rb;
  }

  // Load a PPC GPR value (0..31) from PowerPCState into host register
  void LoadGPR(StencilEmitter& c, ARM64Reg dst, int gpr_idx) const
  {
    c.LDR(IndexType::Unsigned, dst, ARM64Reg::X27, static_cast<s32>(gpr_idx * 4));
  }

  // Store a host register into PowerPCState GPR (0..31)
  void StoreGPR(StencilEmitter& c, ARM64Reg src, int gpr_idx) const
  {
    c.STR(IndexType::Unsigned, src, ARM64Reg::X27, static_cast<s32>(gpr_idx * 4));
  }

  // Load base address: if ra == 0, base is 0; otherwise gpr[ra] + offset
  void LoadBaseAddress(StencilEmitter& c, ARM64Reg dst, int ra_idx, s32 offset) const
  {
    if (ra_idx == 0)
    {
      c.MOVI2R(dst, offset);
    }
    else
    {
      LoadGPR(c, dst, ra_idx);
      if (offset != 0)
      {
        c.ADDI2R(dst, dst, offset, ARM64Reg::W16);
      }
    }
  }

  // Auto-emits operand loading for dynamic stencils
  void Begin(StencilEmitter& c)
  {
    if (mode == StencilMode::Dynamic)
    {
      // Word 1 of tape slot holds packed operand data
      c.LDR(IndexType::Unsigned, reg_packed, ARM64Reg::X20, 8);

      // Unpack register indices and immediates from X1
      // Bits 0..15:  Immediate
      // Bits 16..20: RA (5 bits)
      // Bits 24..28: RD (5 bits)
      // Bits 8..12:  RB (5 bits)
      if (op.ra >= 0)
      {
        c.UBFX(ARM64Reg::W6, ARM64Reg::W1, 16, 5); // W6 = RA index
        c.EmitReadGPR(scratch_ra, -1, ARM64Reg::W6);
      }
      if (op.rb >= 0)
      {
        c.UBFX(ARM64Reg::W7, ARM64Reg::W1, 8, 5);  // W7 = RB index
        c.EmitReadGPR(scratch_rb, -1, ARM64Reg::W7);
      }
    }
  }

  // Auto-emits result writeback and intra-block tail-call
  void End(StencilEmitter& c)
  {
    if (mode == StencilMode::Dynamic && op.rd >= 0)
    {
      c.UBFX(ARM64Reg::W6, ARM64Reg::W1, 24, 5); // W6 = RD index
      c.EmitWriteGPR(scratch_rd, -1, ARM64Reg::W6);
    }
    c.EmitTailCall();
  }
};

}  // namespace StencilGen
