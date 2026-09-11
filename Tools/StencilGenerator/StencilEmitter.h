// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <vector>
#include "Common/Arm64Emitter.h"
#include "Common/CommonTypes.h"

namespace StencilGen
{

using namespace Arm64Gen;

// Pinned ARM64 host registers matching A642A64 convention:
// PPC r0  -> W23
// PPC r3  -> W21
// PPC r4  -> W22
// PPC r5  -> W24
// PPC r31 -> W25
constexpr ARM64Reg kPinnedRegsW[5] = {ARM64Reg::W23, ARM64Reg::W21, ARM64Reg::W22, ARM64Reg::W24, ARM64Reg::W25};
constexpr ARM64Reg kPinnedRegsX[5] = {ARM64Reg::X23, ARM64Reg::X21, ARM64Reg::X22, ARM64Reg::X24, ARM64Reg::X25};

// Pinned Paired-Single / FPR registers:
// PPC f0 -> D0 (V0.2S)
// PPC f1 -> D1 (V1.2S)
// PPC f2 -> D2 (V2.2S)
// PPC f3 -> D3 (V3.2S)
// PPC f4 -> D4 (V4.2S)
constexpr ARM64Reg kPinnedFPRsD[5] = {ARM64Reg::D0, ARM64Reg::D1, ARM64Reg::D2, ARM64Reg::D3, ARM64Reg::D4};

// Verified PowerPCState struct offsets on ARM64:
constexpr u32 OFF_PPCSTATE_GPR      = 32;   // u32 gpr[32]
constexpr u32 OFF_PPCSTATE_PS       = 160;  // PairedSingle ps[32] (512 bytes)
constexpr u32 OFF_PPCSTATE_CR       = 672;  // ConditionRegister cr (64 bytes)
constexpr u32 OFF_PPCSTATE_XER_CA   = 756;  // u8 xer_ca
constexpr u32 OFF_PPCSTATE_XER_SO   = 757;  // u8 xer_so_ov
constexpr u32 OFF_PPCSTATE_SPR_LR   = 864;  // u32 spr[SPR_LR]
constexpr u32 OFF_PPCSTATE_SPR_CTR  = 868;  // u32 spr[SPR_CTR]

class StencilEmitter : public ARM64XEmitter
{
public:
  ARM64FloatEmitter fp;

  explicit StencilEmitter(u8* ptr) : ARM64XEmitter(ptr, ptr + 4096), fp(this) {}

  // Tail-call to the next stencil on the operand tape
  // Increments X20 by 16 bytes and jumps to the function pointer in X16
  void EmitTailCall()
  {
    LDR(IndexType::Pre, ARM64Reg::X16, ARM64Reg::X20, 16);
    BR(ARM64Reg::X16);
  }

  // Load Carry Flag (xer_ca) into a host ARM64 register (0 or 1)
  void EmitLoadCarry(ARM64Reg dst)
  {
    LDRB(IndexType::Unsigned, dst, ARM64Reg::X19, OFF_PPCSTATE_XER_CA);
  }

  // Save Carry Flag (xer_ca) from a host ARM64 register
  void EmitSaveCarry(ARM64Reg src)
  {
    STRB(IndexType::Unsigned, src, ARM64Reg::X19, OFF_PPCSTATE_XER_CA);
  }

  // Branchless read from PPC GPR:
  // - If pinned index 0..4: MOV dst, pinned_reg
  // - If dynamic: LDR dst, [X27, reg_index * 4] (hitting L1 cache in 1 cycle)
  void EmitReadGPR(ARM64Reg dst, int pinned_idx, ARM64Reg dynamic_reg_idx = ARM64Reg::INVALID_REG)
  {
    if (pinned_idx >= 0 && pinned_idx < 5)
    {
      MOV(dst, kPinnedRegsW[pinned_idx]);
    }
    else
    {
      LDR(dst, ARM64Reg::X27, ArithOption(dynamic_reg_idx, true));
    }
  }

  // Branchless write to PPC GPR:
  // - If pinned index 0..4: MOV pinned_reg, src AND STR src, [X27, reg_index * 4]
  // - If dynamic: STR src, [X27, reg_index * 4]
  void EmitWriteGPR(ARM64Reg src, int pinned_idx, ARM64Reg dynamic_reg_idx = ARM64Reg::INVALID_REG)
  {
    if (pinned_idx >= 0 && pinned_idx < 5)
    {
      MOV(kPinnedRegsW[pinned_idx], src);
      STR(IndexType::Unsigned, src, ARM64Reg::X27, static_cast<s32>(pinned_idx * 4));
    }
    else
    {
      STR(src, ARM64Reg::X27, ArithOption(dynamic_reg_idx, true));
    }
  }

  // Emit PowerPC CR0 update (for instructions with Rc = 1)
  // Matches Dolphin ConditionRegister format:
  // - EQ: lower 32-bits == 0
  // - LT: bit 62 set
  // - GT: signed (s64)cr_val > 0
  // - SO: bit 59 set
  void EmitUpdateCR0(ARM64Reg result_w, ARM64Reg scratch_x1)
  {
    SXTW(scratch_x1, result_w);
    // CR0 field is at offset 672 (fields[0])
    STR(IndexType::Unsigned, scratch_x1, ARM64Reg::X19, OFF_PPCSTATE_CR);
  }
};

}  // namespace StencilGen
