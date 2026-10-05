// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Tools/StencilGenerator/StencilEmitter.h"
#include "Tools/StencilGenerator/StencilContext.h"

namespace StencilGen
{

// ============================================================================
// Transpiled from Source/Core/Core/PowerPC/JitArm64/JitArm64_FloatingPoint.cpp
// Full PowerPC Floating Point (Scalar & Double) Opcode Coverage
// ============================================================================

// Helper: load double from PowerPCState::ps[reg].ps0 into ARM64 D register
inline void EmitLoadFPR(StencilEmitter& c, ARM64Reg d_reg, int fpr_idx)
{
  c.LDR(IndexType::Unsigned, d_reg, ARM64Reg::X19, OFF_PPCSTATE_PS + fpr_idx * 16);
}

// Helper: store ARM64 D register into PowerPCState::ps[reg].ps0
inline void EmitStoreFPR(StencilEmitter& c, ARM64Reg d_reg, int fpr_idx)
{
  c.STR(IndexType::Unsigned, d_reg, ARM64Reg::X19, OFF_PPCSTATE_PS + fpr_idx * 16);
}

// 1. LFS: Load Float Single (replicates converted double into ps0 and ps1)
inline void Emit_lfs(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  if (ctx.op.ra == 0)
    c.MOVI2R(ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  else
    c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);

  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(ARM64Reg::W1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV32(ARM64Reg::W1, ARM64Reg::W1);
  c.fp.FMOV(ARM64Reg::S0, ARM64Reg::W1);
  c.fp.FCVT(64, 32, ARM64Reg::D0, ARM64Reg::S0);

  // Store into ps[rd].ps0 and ps[rd].ps1
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16 + 8);
  ctx.End(c);
}

// 2. LFSU: Load Float Single with Update
inline void Emit_lfsu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(ARM64Reg::W1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV32(ARM64Reg::W1, ARM64Reg::W1);
  c.fp.FMOV(ARM64Reg::S0, ARM64Reg::W1);
  c.fp.FCVT(64, 32, ARM64Reg::D0, ARM64Reg::S0);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16 + 8);
  ctx.End(c);
}

// 3. LFSX: Load Float Single Indexed
inline void Emit_lfsx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  if (ctx.op.ra == 0)
    c.MOV(ARM64Reg::W0, ctx.GetRB());
  else
    c.ADD(ARM64Reg::W0, ctx.GetRA(), ctx.GetRB());

  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(ARM64Reg::W1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV32(ARM64Reg::W1, ARM64Reg::W1);
  c.fp.FMOV(ARM64Reg::S0, ARM64Reg::W1);
  c.fp.FCVT(64, 32, ARM64Reg::D0, ARM64Reg::S0);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16 + 8);
  ctx.End(c);
}

// 4. LFD: Load Float Double
inline void Emit_lfd(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  if (ctx.op.ra == 0)
    c.MOVI2R(ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  else
    c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);

  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(ARM64Reg::X1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV64(ARM64Reg::X1, ARM64Reg::X1);
  c.STR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

// 5. LFDU: Load Float Double with Update
inline void Emit_lfdu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(ARM64Reg::X1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV64(ARM64Reg::X1, ARM64Reg::X1);
  c.STR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

// 6. LFDX: Load Float Double Indexed
inline void Emit_lfdx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  if (ctx.op.ra == 0)
    c.MOV(ARM64Reg::W0, ctx.GetRB());
  else
    c.ADD(ARM64Reg::W0, ctx.GetRA(), ctx.GetRB());

  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(ARM64Reg::X1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  c.REV64(ARM64Reg::X1, ARM64Reg::X1);
  c.STR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

// 7. STFS: Store Float Single
inline void Emit_stfs(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  if (ctx.op.ra == 0)
    c.MOVI2R(ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  else
    c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);

  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.fp.FCVT(32, 64, ARM64Reg::S0, ARM64Reg::D0);
  c.fp.FMOV(ARM64Reg::W1, ARM64Reg::S0);
  c.REV32(ARM64Reg::W1, ARM64Reg::W1);
  c.STR(ARM64Reg::W1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 8. STFSU: Store Float Single with Update
inline void Emit_stfsu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.fp.FCVT(32, 64, ARM64Reg::S0, ARM64Reg::D0);
  c.fp.FMOV(ARM64Reg::W1, ARM64Reg::S0);
  c.REV32(ARM64Reg::W1, ARM64Reg::W1);
  c.STR(ARM64Reg::W1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 9. STFSX: Store Float Single Indexed
inline void Emit_stfsx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  if (ctx.op.ra == 0)
    c.MOV(ARM64Reg::W0, ctx.GetRB());
  else
    c.ADD(ARM64Reg::W0, ctx.GetRA(), ctx.GetRB());

  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.fp.FCVT(32, 64, ARM64Reg::S0, ARM64Reg::D0);
  c.fp.FMOV(ARM64Reg::W1, ARM64Reg::S0);
  c.REV32(ARM64Reg::W1, ARM64Reg::W1);
  c.STR(ARM64Reg::W1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 10. STFD: Store Float Double
inline void Emit_stfd(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  if (ctx.op.ra == 0)
    c.MOVI2R(ARM64Reg::W0, static_cast<s16>(ctx.op.imm));
  else
    c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);

  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.REV64(ARM64Reg::X1, ARM64Reg::X1);
  c.STR(ARM64Reg::X1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 11. STFDU: Store Float Double with Update
inline void Emit_stfdu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.ADDI2R(ARM64Reg::W0, ctx.GetRA(), static_cast<s16>(ctx.op.imm), ARM64Reg::W16);
  c.MOV(ctx.GetRA(), ARM64Reg::W0);
  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.REV64(ARM64Reg::X1, ARM64Reg::X1);
  c.STR(ARM64Reg::X1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 12. STFDX: Store Float Double Indexed
inline void Emit_stfdx(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  if (ctx.op.ra == 0)
    c.MOV(ARM64Reg::W0, ctx.GetRB());
  else
    c.ADD(ARM64Reg::W0, ctx.GetRA(), ctx.GetRB());

  c.ANDI2R(ARM64Reg::W0, ARM64Reg::W0, 0x01FFFFFF, ARM64Reg::W16);
  c.LDR(IndexType::Unsigned, ARM64Reg::X1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  c.REV64(ARM64Reg::X1, ARM64Reg::X1);
  c.STR(ARM64Reg::X1, ARM64Reg::X28, ArithOption(ARM64Reg::W0, false));
  ctx.End(c);
}

// 13. FMR: frD = frB
inline void Emit_fmr(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

// 14. FABS: frD = |frB|
inline void Emit_fabs(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FABS(ARM64Reg::D0, ARM64Reg::D0);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

// 15. FNEG: frD = -frB
inline void Emit_fneg(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FNEG(ARM64Reg::D0, ARM64Reg::D0);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

// 16. FNABS: frD = -|frB|
inline void Emit_fnabs(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FABS(ARM64Reg::D0, ARM64Reg::D0);
  c.fp.FNEG(ARM64Reg::D0, ARM64Reg::D0);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

// 17. FADD / FADDS: frD = frA + frB
inline void Emit_fadd(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FADD(ARM64Reg::D0, ARM64Reg::D1, ARM64Reg::D2);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

inline void Emit_fadds(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FCVT(32, 64, ARM64Reg::S1, ARM64Reg::D1);
  c.fp.FCVT(32, 64, ARM64Reg::S2, ARM64Reg::D2);
  c.fp.FADD(ARM64Reg::S0, ARM64Reg::S1, ARM64Reg::S2);
  c.fp.FCVT(64, 32, ARM64Reg::D0, ARM64Reg::S0);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

// 18. FSUB / FSUBS: frD = frA - frB
inline void Emit_fsub(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FSUB(ARM64Reg::D0, ARM64Reg::D1, ARM64Reg::D2);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

inline void Emit_fsubs(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FCVT(32, 64, ARM64Reg::S1, ARM64Reg::D1);
  c.fp.FCVT(32, 64, ARM64Reg::S2, ARM64Reg::D2);
  c.fp.FSUB(ARM64Reg::S0, ARM64Reg::S1, ARM64Reg::S2);
  c.fp.FCVT(64, 32, ARM64Reg::D0, ARM64Reg::S0);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

// 19. FMUL / FMULS: frD = frA * frC
inline void Emit_fmul(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FMUL(ARM64Reg::D0, ARM64Reg::D1, ARM64Reg::D2);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

inline void Emit_fmuls(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FCVT(32, 64, ARM64Reg::S1, ARM64Reg::D1);
  c.fp.FCVT(32, 64, ARM64Reg::S2, ARM64Reg::D2);
  c.fp.FMUL(ARM64Reg::S0, ARM64Reg::S1, ARM64Reg::S2);
  c.fp.FCVT(64, 32, ARM64Reg::D0, ARM64Reg::S0);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

// 20. FDIV / FDIVS: frD = frA / frB
inline void Emit_fdiv(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FDIV(ARM64Reg::D0, ARM64Reg::D1, ARM64Reg::D2);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

inline void Emit_fdivs(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FCVT(32, 64, ARM64Reg::S1, ARM64Reg::D1);
  c.fp.FCVT(32, 64, ARM64Reg::S2, ARM64Reg::D2);
  c.fp.FDIV(ARM64Reg::S0, ARM64Reg::S1, ARM64Reg::S2);
  c.fp.FCVT(64, 32, ARM64Reg::D0, ARM64Reg::S0);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

// 21. FMADD / FMADDS: frD = (frA * frC) + frB
inline void Emit_fmadd(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FMADD(ARM64Reg::D0, ARM64Reg::D1, ARM64Reg::D2, ARM64Reg::D0);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

// 22. FSEL: frD = (frA >= 0.0) ? frC : frB
inline void Emit_fsel(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FCMP(ARM64Reg::D1);
  c.fp.FCSEL(ARM64Reg::D0, ARM64Reg::D2, ARM64Reg::D1, CC_GE);
  c.STR(IndexType::Unsigned, ARM64Reg::D0, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rd * 16);
  ctx.End(c);
}

// 23. FCMPU / FCMPO: Compare Float
inline void Emit_fcmpu(StencilEmitter& c, StencilContext& ctx)
{
  ctx.Begin(c);
  c.LDR(IndexType::Unsigned, ARM64Reg::D1, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.ra * 16);
  c.LDR(IndexType::Unsigned, ARM64Reg::D2, ARM64Reg::X19, OFF_PPCSTATE_PS + ctx.op.rb * 16);
  c.fp.FCMP(ARM64Reg::D1, ARM64Reg::D2);
  ctx.End(c);
}

}  // namespace StencilGen
