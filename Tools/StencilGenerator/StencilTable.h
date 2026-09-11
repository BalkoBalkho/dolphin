// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <array>
#include "Common/CommonTypes.h"

namespace StencilGen
{

using StencilFunction = void (*)();

// Maps PPC general register (0..31) to pinned index (0..4)
// Returns -1 if register is not in pinned set {r0, r3, r4, r5, r31}
constexpr int GetPinnedIndex(u32 gpr)
{
  switch (gpr)
  {
  case 0:  return 0; // W23
  case 3:  return 1; // W21
  case 4:  return 2; // W22
  case 5:  return 3; // W24
  case 31: return 4; // W25
  default: return -1;
  }
}

// Stencil descriptor stored in the global opcode table
struct StencilDescriptor
{
  // If instruction has 3 register operands: [5][5][5] -> 125 entries
  const StencilFunction (*pinned_3d)[5][5] = nullptr;

  // If instruction has 2 register operands: [5][5] -> 25 entries
  const StencilFunction (*pinned_2d)[5] = nullptr;

  // Universal dynamic fallback stencil (handles all 32 PPC registers)
  StencilFunction dynamic_stencil = nullptr;

  // Metadata for operand packing
  bool uses_rb = true;
  bool is_logical_dest_ra = false; // In PowerPC logical/shift ops, RA is DESTINATION, RD is SOURCE!
};

// Pack operands into a single 64-bit tape word for dynamic stencils:
// - Bits 0..15:  Immediate (SIMM/UIMM)
// - Bits 16..20: RA (5 bits)
// - Bits 24..28: RD (5 bits)
// - Bits 8..12:  RB (5 bits)
inline uint64_t PackTapeOperand(u32 rd, u32 ra, u32 rb, u16 imm = 0)
{
  return (static_cast<uint64_t>(rd & 0x1F) << 24) |
         (static_cast<uint64_t>(ra & 0x1F) << 16) |
         (static_cast<uint64_t>(rb & 0x1F) << 8)  |
         static_cast<uint64_t>(imm);
}

// Fast O(1) runtime lookup that returns the stencil function pointer and packed operand
struct StencilMatch
{
  StencilFunction code;
  uint64_t operand;
  bool is_pinned;
};

inline std::optional<StencilMatch> MatchStencil(const StencilDescriptor& desc,
                                                u32 rd, u32 ra, u32 rb, u16 imm = 0)
{
  if (!desc.dynamic_stencil && !desc.pinned_3d && !desc.pinned_2d)
    return std::nullopt;

  const int rd_p = GetPinnedIndex(rd);
  const int ra_p = GetPinnedIndex(ra);
  const int rb_p = desc.uses_rb ? GetPinnedIndex(rb) : 0;

  // Check if all active operands fall within the pinned register set
  if (rd_p >= 0 && ra_p >= 0 && rb_p >= 0)
  {
    if (desc.pinned_3d)
    {
      StencilFunction fn = desc.pinned_3d[rd_p][ra_p][rb_p];
      if (fn)
        return StencilMatch{fn, static_cast<uint64_t>(imm), true};
    }
    else if (desc.pinned_2d)
    {
      StencilFunction fn = desc.pinned_2d[rd_p][ra_p];
      if (fn)
        return StencilMatch{fn, static_cast<uint64_t>(imm), true};
    }
  }

  // Otherwise route to the universal dynamic stencil
  if (desc.dynamic_stencil)
  {
    const uint64_t packed = PackTapeOperand(rd, ra, rb, imm);
    return StencilMatch{desc.dynamic_stencil, packed, false};
  }

  return std::nullopt;
}

}  // namespace StencilGen
