// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"

namespace PowerPC
{
struct PowerPCState;
}

namespace DivergenceTracer
{
#pragma pack(push, 1)
struct DivergenceRecord
{
  u64 cycle;
  u32 pc;
  u32 msr;
  u32 cr;
  u32 xer;
  u32 lr;
  u32 ctr;
  u32 gpr[32];
};
#pragma pack(pop)
static_assert(sizeof(DivergenceRecord) == 160);

void Init();
void Shutdown();
bool IsEnabled();
bool IsBlockTracingEnabled();
void LogSlice(u64 cycle, const PowerPC::PowerPCState& ppc_state);
void LogBlock(u32 pc, const PowerPC::PowerPCState& ppc_state);
}  // namespace DivergenceTracer
