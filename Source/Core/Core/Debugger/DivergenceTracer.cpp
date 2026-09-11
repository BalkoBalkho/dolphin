// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Debugger/DivergenceTracer.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "Common/Logging/Log.h"
#include "Core/PowerPC/Gekko.h"
#include "Core/PowerPC/PowerPC.h"

namespace DivergenceTracer
{
static FILE* s_file = nullptr;
static bool s_enabled = false;
static bool s_block_tracing = false;
static u64 s_block_counter = 0;
static char s_io_buffer[65536];

void Init()
{
  const char* pipe_path = std::getenv("DOLPHIN_DIVERGENCE_PIPE");
  if (!pipe_path || pipe_path[0] == '\0')
  {
    s_enabled = false;
    return;
  }

  s_file = std::fopen(pipe_path, "wb");
  if (!s_file)
  {
    ERROR_LOG_FMT(POWERPC, "DivergenceTracer: Failed to open FIFO/file '{}': {}", pipe_path,
                  std::strerror(errno));
    s_enabled = false;
    return;
  }

  std::setvbuf(s_file, s_io_buffer, _IOFBF, sizeof(s_io_buffer));
  s_enabled = true;

  const char* block_env = std::getenv("DOLPHIN_DIVERGENCE_BLOCK");
  s_block_tracing = (block_env != nullptr && block_env[0] == '1');
  s_block_counter = 0;

  NOTICE_LOG_FMT(POWERPC, "DivergenceTracer: Streaming execution trace to '{}' (Block Mode: {})",
                 pipe_path, s_block_tracing);
}

void Shutdown()
{
  if (s_file)
  {
    std::fflush(s_file);
    std::fclose(s_file);
    s_file = nullptr;
  }
  s_enabled = false;
}

bool IsEnabled()
{
  return s_enabled;
}

bool IsBlockTracingEnabled()
{
  return s_enabled && s_block_tracing;
}

static void EmitRecord(u64 cycle, u32 pc, const PowerPC::PowerPCState& ppc_state)
{
  if (!s_file)
    return;

  DivergenceRecord rec{};
  rec.cycle = cycle;
  rec.pc = pc;
  rec.msr = ppc_state.msr.Hex;
  rec.cr = ppc_state.cr.Get();
  rec.xer = ppc_state.GetXER().Hex;
  rec.lr = ppc_state.spr[SPR_LR];
  rec.ctr = ppc_state.spr[SPR_CTR];
  std::memcpy(rec.gpr, ppc_state.gpr, sizeof(rec.gpr));

  if (std::fwrite(&rec, sizeof(rec), 1, s_file) != 1)
  {
    std::fclose(s_file);
    s_file = nullptr;
    s_enabled = false;
  }
}

void LogSlice(u64 cycle, const PowerPC::PowerPCState& ppc_state)
{
  if (!s_enabled || s_block_tracing)
    return;
  EmitRecord(cycle, ppc_state.pc, ppc_state);
}

void LogBlock(u32 pc, const PowerPC::PowerPCState& ppc_state)
{
  if (!s_enabled || !s_block_tracing)
    return;
  EmitRecord(++s_block_counter, pc, ppc_state);
}

}  // namespace DivergenceTracer
