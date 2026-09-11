// Copyright 2014 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/PowerPC/StencilInterpreter/StencilInterpreter.h"

#include <cstring>
#include <fstream>
#include <span>
#include <sstream>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#endif

#include <fmt/ostream.h>

#include "Common/CommonTypes.h"
#include "Common/GekkoDisassembler.h"
#include "Common/Logging/Log.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/DivergenceTracer.h"
#include "Core/HLE/HLE.h"
#include "Core/HW/CPU.h"
#include "Core/HW/Memmap.h"
#include "Core/Host.h"
#include "Core/PowerPC/Gekko.h"
#include "Core/PowerPC/Interpreter/Interpreter.h"
#include "Core/PowerPC/Jit64Common/Jit64Constants.h"
#include "Core/PowerPC/PPCAnalyst.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "Common/Timer.h"
#include "VideoCommon/PerformanceMetrics.h"
#include <optional>
#include "ppc_stencils_generated.h"
#include "stencil_trampoline.h"

namespace
{
// Helper to map PPC GPR index to pinned index (0->r0, 1->r3, 2->r4, 3->r5, 4->r31)
constexpr int GetPinnedIndex(u32 gpr)
{
  switch (gpr)
  {
  case 0:  return 0;
  case 3:  return 1;
  case 4:  return 2;
  case 5:  return 3;
  case 31: return 4;
  default: return -1;
  }
}

#if defined(__aarch64__) || defined(_M_ARM64)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(PowerPC::PowerPCState, gpr) == 32, "GPR offset must be 32 for stencil_trampoline.S");
static_assert(offsetof(PowerPC::PowerPCState, cr.fields) == 672, "CR offset must be 672 for stencil_trampoline.S");
static_assert(offsetof(PowerPC::PowerPCState, xer_so_ov) == 757, "xer_so_ov offset must be 757 for stencil_trampoline.S");
static_assert(offsetof(PowerPC::PowerPCState, spr[SPR_CTR]) == 868, "CTR offset must be 868 for stencil_trampoline.S");
#pragma GCC diagnostic pop
#endif
}  // namespace

static Core::System* s_system_instance = nullptr;

#if defined(__aarch64__) || defined(_M_ARM64)
extern "C" {
uint32_t Stencil_SlowReadU8(PowerPC::PowerPCState* ppc_state, uint32_t addr)
{
  return s_system_instance ? s_system_instance->GetMMU().Read<u8>(addr) : 0;
}

uint32_t Stencil_SlowReadU16(PowerPC::PowerPCState* ppc_state, uint32_t addr)
{
  return s_system_instance ? s_system_instance->GetMMU().Read<u16>(addr) : 0;
}

uint32_t Stencil_SlowReadU32(PowerPC::PowerPCState* ppc_state, uint32_t addr)
{
  return s_system_instance ? s_system_instance->GetMMU().Read<u32>(addr) : 0;
}

void Stencil_SlowWriteU8(PowerPC::PowerPCState* ppc_state, uint32_t addr, uint32_t val)
{
  if (s_system_instance)
    s_system_instance->GetMMU().Write<u8>(val, addr);
}

void Stencil_SlowWriteU16(PowerPC::PowerPCState* ppc_state, uint32_t addr, uint32_t val)
{
  if (s_system_instance)
    s_system_instance->GetMMU().Write<u16>(val, addr);
}

void Stencil_SlowWriteU32(PowerPC::PowerPCState* ppc_state, uint32_t addr, uint32_t val)
{
  if (s_system_instance)
    s_system_instance->GetMMU().Write<u32>(val, addr);
}
}
#endif

StencilInterpreter::StencilInterpreter(Core::System& system)
    : JitBase(system), m_block_cache(*this)
{
}

StencilInterpreter::~StencilInterpreter()
{
  Shutdown();
}

void StencilInterpreter::Init()
{
  s_system_instance = &m_system;
  jo.enableBlocklink = !SConfig::GetInstance().bJITNoBlockLinking;

  AllocCodeSpace(CODE_SIZE);
  ResetFreeMemoryRanges();

  // Allocate an executable memory region for the stencil binary pool (RX)
  m_exec_stencil_pool_size = get_total_stencil_bytes();
  if (m_exec_stencil_pool_size > 0 && !m_exec_stencil_pool)
  {
#if defined(__unix__) || defined(__APPLE__)
    m_exec_stencil_pool = static_cast<u8*>(mmap(
        nullptr, m_exec_stencil_pool_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (m_exec_stencil_pool != MAP_FAILED)
    {
      std::memcpy(m_exec_stencil_pool, get_stencil_pool_base(), m_exec_stencil_pool_size);
      mprotect(m_exec_stencil_pool, m_exec_stencil_pool_size, PROT_READ | PROT_EXEC);
    }
    else
    {
      m_exec_stencil_pool = nullptr;
    }
#endif
  }

  jo.enableBlocklink = !SConfig::GetInstance().bJITNoBlockLinking;

  m_block_cache.Init();

  code_block.m_stats = &js.st;
  code_block.m_gpa = &js.gpa;
  code_block.m_fpa = &js.fpa;
}

void StencilInterpreter::Shutdown()
{
  m_block_cache.Shutdown();
  if (m_exec_stencil_pool)
  {
#if defined(__unix__) || defined(__APPLE__)
    munmap(m_exec_stencil_pool, m_exec_stencil_pool_size);
#endif
    m_exec_stencil_pool = nullptr;
    m_exec_stencil_pool_size = 0;
  }
}

const u8* StencilInterpreter::GetExecutableStencilBytes(const StencilEntry* s)
{
  if (!s || !s->bytes)
    return nullptr;
  if (m_exec_stencil_pool)
  {
    const ptrdiff_t offset = s->bytes - get_stencil_pool_base();
    if (offset >= 0 && static_cast<size_t>(offset) < m_exec_stencil_pool_size)
      return m_exec_stencil_pool + offset;
  }
  return s->bytes;
}

void StencilInterpreter::ExecuteOneBlock()
{
  const u8* normal_entry = m_block_cache.Dispatch();
  if (!normal_entry)
  {
    Jit(m_ppc_state.pc);
    return;
  }

  u32 start_pc = m_ppc_state.pc;
  auto& ppc_state = m_ppc_state;
  u32 stencils_executed = 0;
  u32 interprets_executed = 0;

  static u64 s_total_blocks = 0;
  static u64 s_total_stencils = 0;
  static u64 s_total_interprets = 0;
  static auto s_last_report = std::chrono::steady_clock::now();

  while (true)
  {
    const auto callback = *reinterpret_cast<const AnyCallback*>(normal_entry);
    const u8* payload = normal_entry + sizeof(callback);
    if (callback == AnyCallbackCast(Interpret<false>)) [[likely]]
    {
      Interpret<false>(ppc_state, *reinterpret_cast<const InterpretOperands*>(payload));
      normal_entry = payload + sizeof(InterpretOperands);
      ++interprets_executed;
    }
    else if (callback == AnyCallbackCast(Interpret<true>))
    {
      Interpret<true>(ppc_state, *reinterpret_cast<const InterpretOperands*>(payload));
      normal_entry = payload + sizeof(InterpretOperands);
      ++interprets_executed;
    }
    else if (callback == AnyCallbackCast(ExecuteStencil<false>))
    {
      ExecuteStencil<false>(ppc_state, *reinterpret_cast<const StencilOperands*>(payload));
      normal_entry = payload + sizeof(StencilOperands);
      ++stencils_executed;
    }
    else if (callback == AnyCallbackCast(ExecuteStencil<true>))
    {
      ExecuteStencil<true>(ppc_state, *reinterpret_cast<const StencilOperands*>(payload));
      normal_entry = payload + sizeof(StencilOperands);
      ++stencils_executed;
    }
    else if (callback == AnyCallbackCast(ExecuteTapeRun<false>))
    {
      ExecuteTapeRun<false>(ppc_state, payload);
      const auto* operands = reinterpret_cast<const TapeRunOperands*>(payload);
      stencils_executed += operands->tape_num_entries;
      normal_entry = payload + sizeof(TapeRunOperands) + operands->tape_num_entries * sizeof(uint64_t);
    }
    else if (callback == AnyCallbackCast(ExecuteTapeRun<true>))
    {
      ExecuteTapeRun<true>(ppc_state, payload);
      const auto* operands = reinterpret_cast<const TapeRunOperands*>(payload);
      stencils_executed += operands->tape_num_entries;
      break;
    }
    else if (callback == AnyCallbackCast(ExecuteBlockLinkBranch))
    {
      const auto distance = ExecuteBlockLinkBranch(ppc_state, *reinterpret_cast<const BlockLinkBranchOperands*>(payload));
      ++s_total_blocks;
      ++m_bench_blocks_executed;
      if (distance != 0) [[likely]]
      {
        normal_entry += distance;
      }
      else
      {
        break;
      }
    }
    else if (callback == AnyCallbackCast(ExecuteBlockLinkDirect))
    {
      const auto distance = ExecuteBlockLinkDirect(ppc_state, *reinterpret_cast<const BlockLinkDirectOperands*>(payload));
      ++s_total_blocks;
      ++m_bench_blocks_executed;
      if (distance != 0) [[likely]]
      {
        normal_entry += distance;
      }
      else
      {
        break;
      }
    }
    else
    {
      if (const auto distance = callback(ppc_state, payload))
        normal_entry += distance;
      else
        break;
    }
  }

  // Record into ring buffer history
  auto& h_entry = m_block_history[m_block_history_index % m_block_history.size()];
  h_entry.start_pc = start_pc;
  h_entry.end_pc = m_ppc_state.pc;
  h_entry.lr = m_ppc_state.spr[SPR_LR];
  h_entry.ctr = m_ppc_state.spr[SPR_CTR];
  h_entry.r3 = m_ppc_state.gpr[3];
  h_entry.r12 = m_ppc_state.gpr[12];
  h_entry.stencils_executed = stencils_executed;
  h_entry.interprets_executed = interprets_executed;
  ++m_block_history_index;

  ++s_total_blocks;
  s_total_stencils += stencils_executed;
  s_total_interprets += interprets_executed;

  if (s_total_blocks % 50000 == 0)
  {
    auto now = std::chrono::steady_clock::now();
    double elapsed_sec = std::chrono::duration<double>(now - s_last_report).count();
    s_last_report = now;
    double blocks_per_sec = elapsed_sec > 0 ? (50000.0 / elapsed_sec) : 0.0;
    double total_ops = static_cast<double>(s_total_stencils + s_total_interprets);
    double stencil_ratio = total_ops > 0 ? (100.0 * s_total_stencils / total_ops) : 0.0;
    WARN_LOG_FMT(POWERPC, "[STENCIL_PERF] Core=6(StencilInterpreter) | Blocks: {} ({:.1f} blk/s) | Native Stencils: {} ({:.2f}%) | Fallback Interprets: {}",
                 s_total_blocks, blocks_per_sec, s_total_stencils, stencil_ratio, s_total_interprets);
  }


  if (start_pc >= 0x80000000 && m_ppc_state.pc < 0x80000000)
  {
    static bool logged = false;
    if (!logged)
    {
      logged = true;
      std::ofstream trace_file("/sdcard/dolphin_trace.txt");
      auto log_both = [&](const std::string& line) {
        WARN_LOG_FMT(POWERPC, "{}", line);
        if (trace_file.is_open())
          trace_file << line << "\n";
      };

      log_both(fmt::format("=== EXCEPTION TRANSITION: from block {:08x} to PC={:08x} ===", start_pc, m_ppc_state.pc));
      log_both(fmt::format("SRR0={:08x} SRR1={:08x} DAR={:08x} DSISR={:08x} LR={:08x} CTR={:08x} MSR={:08x} Exceptions={:#x}",
                           m_ppc_state.spr[SPR_SRR0], m_ppc_state.spr[SPR_SRR1],
                           m_ppc_state.spr[SPR_DAR], m_ppc_state.spr[SPR_DSISR],
                           m_ppc_state.spr[SPR_LR], m_ppc_state.spr[SPR_CTR],
                           m_ppc_state.msr.Hex, m_ppc_state.Exceptions));

      // Dump all 32 GPRs
      for (int i = 0; i < 32; i += 4)
      {
        log_both(fmt::format("r{:02d}={:08x} r{:02d}={:08x} r{:02d}={:08x} r{:02d}={:08x}",
                             i, m_ppc_state.gpr[i],
                             i + 1, m_ppc_state.gpr[i + 1],
                             i + 2, m_ppc_state.gpr[i + 2],
                             i + 3, m_ppc_state.gpr[i + 3]));
      }

      // Dump recent block execution history
      log_both("=== RECENT BLOCK EXECUTION HISTORY (last 32 blocks, chronological) ===");
      const size_t total = std::min(m_block_history_index, m_block_history.size());
      const size_t start_idx = m_block_history_index >= m_block_history.size() ?
                               (m_block_history_index % m_block_history.size()) : 0;
      for (size_t i = 0; i < total; ++i)
      {
        const auto& h = m_block_history[(start_idx + i) % m_block_history.size()];
        log_both(fmt::format("  [{:02d}] {:08x} -> {:08x} | LR={:08x} CTR={:08x} r3={:08x} r12={:08x} | stencils={} interp={}",
                             i, h.start_pc, h.end_pc, h.lr, h.ctr, h.r3, h.r12, h.stencils_executed, h.interprets_executed));
      }

      // Disassemble around SRR0
      const u32 srr0_addr = m_ppc_state.spr[SPR_SRR0];
      if (srr0_addr >= 0x80000000)
      {
        log_both(fmt::format("=== DISASSEMBLY AROUND SRR0 ({:08x}) ===", srr0_addr));
        for (u32 a = (srr0_addr >= 0x20 ? srr0_addr - 0x20 : 0x80000000); a <= srr0_addr + 0x20; a += 4)
        {
          const u8* ptr = m_system.GetMemory().GetPointerForRange(a, 4);
          if (ptr)
          {
            u32 opc = Common::swap32(*reinterpret_cast<const u32*>(ptr));
            log_both(fmt::format("  {:08x}: {:08x} -> {}{}",
                                 a, opc, Common::GekkoDisassembler::Disassemble(opc, a),
                                 a == srr0_addr ? " <== EXCEPTION INSTRUCTION" : ""));
          }
        }
      }

      // Disassemble around LR
      const u32 lr_addr = m_ppc_state.spr[SPR_LR];
      if (lr_addr >= 0x80000000)
      {
        log_both(fmt::format("=== DISASSEMBLY AROUND LR ({:08x}) ===", lr_addr));
        for (u32 a = (lr_addr >= 0x20 ? lr_addr - 0x20 : 0x80000000); a <= lr_addr + 0x10; a += 4)
        {
          const u8* ptr = m_system.GetMemory().GetPointerForRange(a, 4);
          if (ptr)
          {
            u32 opc = Common::swap32(*reinterpret_cast<const u32*>(ptr));
            log_both(fmt::format("  {:08x}: {:08x} -> {}{}",
                                 a, opc, Common::GekkoDisassembler::Disassemble(opc, a),
                                 a == lr_addr - 4 ? " <== CALLER BRANCH" : (a == lr_addr ? " <== RETURN POINT" : "")));
          }
        }
      }

      // Disassemble offending block
      if (start_pc >= 0x80000000)
      {
        log_both(fmt::format("=== DISASSEMBLY OF OFFENDING BLOCK ({:08x}) ===", start_pc));
        for (u32 a = start_pc; a <= start_pc + 0x30; a += 4)
        {
          const u8* ptr = m_system.GetMemory().GetPointerForRange(a, 4);
          if (ptr)
          {
            u32 opc = Common::swap32(*reinterpret_cast<const u32*>(ptr));
            log_both(fmt::format("  {:08x}: {:08x} -> {}", a, opc, Common::GekkoDisassembler::Disassemble(opc, a)));
          }
        }
      }

      // Disassemble 0x00000800 (FPU Unavailable Vector)
      log_both("=== DISASSEMBLY OF 0x00000800 (FPU Unavailable Vector) ===");
      for (u32 a = 0x00000800; a <= 0x00000898; a += 4)
      {
        const u8* ptr = m_system.GetMemory().GetPointerForRange(a, 4);
        if (ptr)
        {
          u32 opc = Common::swap32(*reinterpret_cast<const u32*>(ptr));
          log_both(fmt::format("  {:08x}: {:08x} -> {}", a, opc, Common::GekkoDisassembler::Disassemble(opc, a)));
        }
      }

      // Disassemble 0x801a7b50..0x801a7c50
      log_both("=== DISASSEMBLY OF 0x801a7b50..0x801a7c50 ===");
      for (u32 a = 0x801a7b50; a <= 0x801a7c50; a += 4)
      {
        const u8* ptr = m_system.GetMemory().GetPointerForRange(a, 4);
        if (ptr)
        {
          u32 opc = Common::swap32(*reinterpret_cast<const u32*>(ptr));
          log_both(fmt::format("  {:08x}: {:08x} -> {}", a, opc, Common::GekkoDisassembler::Disassemble(opc, a)));
        }
      }

      // Disassemble 0x801a6af0..0x801a6b40
      log_both("=== DISASSEMBLY OF 0x801a6af0..0x801a6b40 ===");
      for (u32 a = 0x801a6af0; a <= 0x801a6b40; a += 4)
      {
        const u8* ptr = m_system.GetMemory().GetPointerForRange(a, 4);
        if (ptr)
        {
          u32 opc = Common::swap32(*reinterpret_cast<const u32*>(ptr));
          log_both(fmt::format("  {:08x}: {:08x} -> {}", a, opc, Common::GekkoDisassembler::Disassemble(opc, a)));
        }
      }

      // Scan RAM for 0x7c9043a6 (mtspr SPRG0, r4 - exception vector header)
      log_both("=== SCANNING RAM FOR 0x7c9043a6 (mtspr SPRG0, r4) ===");
      for (u32 a = 0x80000000; a < 0x81800000; a += 4)
      {
        const u8* ptr = m_system.GetMemory().GetPointerForRange(a, 4);
        if (ptr)
        {
          u32 opc = Common::swap32(*reinterpret_cast<const u32*>(ptr));
          if (opc == 0x7c9043a6 && a != 0x80000800)
          {
            log_both(fmt::format("  Found vector header at {:08x}", a));
            for (u32 sub = a; sub <= a + 0x90; sub += 4)
            {
              const u8* sub_ptr = m_system.GetMemory().GetPointerForRange(sub, 4);
              if (sub_ptr)
              {
                u32 sub_opc = Common::swap32(*reinterpret_cast<const u32*>(sub_ptr));
                log_both(fmt::format("    {:08x}: {:08x} -> {}", sub, sub_opc, Common::GekkoDisassembler::Disassemble(sub_opc, sub)));
              }
            }
          }
        }
      }

      if (trace_file.is_open())
        trace_file.close();
    }
  }
  if (start_pc == 0x800040f0)
  {
    static bool logged_40f0 = false;
    if (!logged_40f0)
    {
      logged_40f0 = true;
      WARN_LOG_FMT(POWERPC, "=== HIT 0x800040f0! MSR={:08x} LR={:08x} CTR={:08x} ===",
                   m_ppc_state.msr.Hex, m_ppc_state.spr[SPR_LR], m_ppc_state.spr[SPR_CTR]);
      for (int i = 0; i < 32; i += 4)
      {
        WARN_LOG_FMT(POWERPC, "  r{:02d}={:08x} r{:02d}={:08x} r{:02d}={:08x} r{:02d}={:08x}",
                     i, m_ppc_state.gpr[i],
                     i + 1, m_ppc_state.gpr[i + 1],
                     i + 2, m_ppc_state.gpr[i + 2],
                     i + 3, m_ppc_state.gpr[i + 3]);
      }
      WARN_LOG_FMT(POWERPC, "=== DISASSEMBLY OF 0x800040c0..0x80004120 ===");
      for (u32 a = 0x800040c0; a <= 0x80004120; a += 4)
      {
        const u8* ptr = m_system.GetMemory().GetPointerForRange(a, 4);
        if (ptr)
        {
          u32 opc = Common::swap32(*reinterpret_cast<const u32*>(ptr));
          WARN_LOG_FMT(POWERPC, "  {:08x}: {:08x} -> {}{}", a, opc, Common::GekkoDisassembler::Disassemble(opc, a),
                       a == start_pc ? " <== 800040F0" : "");
        }
      }
    }
  }

  if (start_pc == 0x8015e780)
  {
    static int mtmsr_count = 0;
    ++mtmsr_count;
    WARN_LOG_FMT(POWERPC, "=== mtmsr (8015e780) CALLED (#{}): r3={:08x}, MSR was {:08x}, LR={:08x} ===",
                 mtmsr_count, m_ppc_state.gpr[3], m_ppc_state.msr.Hex, m_ppc_state.spr[SPR_LR]);
  }

  static bool fp_was_enabled = true;
  if (fp_was_enabled && !m_ppc_state.msr.FP)
  {
    fp_was_enabled = false;
    const u32 caller_lr = m_ppc_state.spr[SPR_LR];
    WARN_LOG_FMT(POWERPC, "=== CALLER OF 8015e780 is LR={:08x} ===", caller_lr);
    if (caller_lr >= 0x80000000)
    {
      for (u32 a = (caller_lr >= 0x20 ? caller_lr - 0x20 : 0x80000000); a <= caller_lr + 0x10; a += 4)
      {
        const u8* ptr = m_system.GetMemory().GetPointerForRange(a, 4);
        if (ptr)
        {
          u32 opc = Common::swap32(*reinterpret_cast<const u32*>(ptr));
          WARN_LOG_FMT(POWERPC, "  {:08x}: {:08x} -> {}{}", a, opc, Common::GekkoDisassembler::Disassemble(opc, a),
                       a == caller_lr - 4 ? " <== CALLER BL" : "");
        }
      }
    }
    WARN_LOG_FMT(POWERPC, "=== FUNCTION 801a8430..801a8520 DISASSEMBLY ===");
    for (u32 a = 0x801a8430; a <= 0x801a8520; a += 4)
    {
      const u8* ptr = m_system.GetMemory().GetPointerForRange(a, 4);
      if (ptr)
      {
        u32 opc = Common::swap32(*reinterpret_cast<const u32*>(ptr));
        WARN_LOG_FMT(POWERPC, "  {:08x}: {:08x} -> {}", a, opc, Common::GekkoDisassembler::Disassemble(opc, a));
      }
    }
    for (u32 a = (start_pc >= 0x10 ? start_pc - 0x10 : start_pc); a <= start_pc + 0x20; a += 4)
    {
      const u8* ptr = m_system.GetMemory().GetPointerForRange(a, 4);
      if (ptr)
      {
        u32 opc = Common::swap32(*reinterpret_cast<const u32*>(ptr));
        WARN_LOG_FMT(POWERPC, "  {:08x}: {:08x} -> {}{}", a, opc, Common::GekkoDisassembler::Disassemble(opc, a),
                     a == start_pc ? " <== BLOCK START" : "");
      }
    }
    for (int i = 0; i < 32; i += 4)
    {
      WARN_LOG_FMT(POWERPC, "  r{:02d}={:08x} r{:02d}={:08x} r{:02d}={:08x} r{:02d}={:08x}",
                   i, m_ppc_state.gpr[i],
                   i + 1, m_ppc_state.gpr[i + 1],
                   i + 2, m_ppc_state.gpr[i + 2],
                   i + 3, m_ppc_state.gpr[i + 3]);
    }
  }
}

void StencilInterpreter::Run()
{
  WARN_LOG_FMT(POWERPC, "=== StencilInterpreter::Run entry: MSR={:08x}, MSR.FP={} ===",
               m_ppc_state.msr.Hex, static_cast<u32>(m_ppc_state.msr.FP));
  auto& core_timing = m_system.GetCoreTiming();

  const CPU::State* state_ptr = m_system.GetCPU().GetStatePtr();
  m_bench_last_time_ms = Common::Timer::NowMs();
  m_bench_blocks_executed = 0;
  m_bench_last_downcount = 0;
  m_bench_last_ticks = core_timing.GetTicks();

  while (*state_ptr == CPU::State::Running)
  {
    core_timing.Advance();

    do
    {
      ExecuteOneBlock();
      ++m_bench_blocks_executed;
    } while (m_ppc_state.downcount > 0 && *state_ptr == CPU::State::Running);

    const u64 now_ms = Common::Timer::NowMs();
    const u64 elapsed = now_ms - m_bench_last_time_ms;
    if (elapsed >= 1000)
    {
      const s64 current_ticks = core_timing.GetTicks();
      const s64 ticks_delta = current_ticks - m_bench_last_ticks;
      const double elapsed_sec = elapsed / 1000.0;
      const double mips = (ticks_delta / 1e6) / elapsed_sec;
      const double speed_pct = (mips / 729.0) * 100.0;
      const double blocks_per_sec = (m_bench_blocks_executed * 1000.0) / elapsed;

      NOTICE_LOG_FMT(POWERPC,
                     "[StencilBench] PC: 0x{:08x} | Speed: {:5.1f}% | MIPS: {:6.1f} | Blocks/s: {:8.0f}",
                     m_ppc_state.pc, speed_pct, mips, blocks_per_sec);

      m_bench_last_time_ms = now_ms;
      m_bench_last_ticks = current_ticks;
      m_bench_blocks_executed = 0;
    }
  }
}

void StencilInterpreter::SingleStep()
{
  m_system.GetCoreTiming().Advance();
  ExecuteOneBlock();
}

s32 StencilInterpreter::StartProfiledBlock(PowerPC::PowerPCState& ppc_state,
                                           const StartProfiledBlockOperands& operands)
{
  JitBlock::ProfileData::BeginProfiling(operands.profile_data);
  return sizeof(AnyCallback) + sizeof(operands);
}

template <bool profiled>
s32 StencilInterpreter::EndBlock(PowerPC::PowerPCState& ppc_state,
                                 const EndBlockOperands<profiled>& operands)
{
  ppc_state.pc = ppc_state.npc;
  ppc_state.downcount -= operands.downcount;
  PowerPC::UpdatePerformanceMonitor(operands.downcount, operands.num_load_stores,
                                    operands.num_fp_inst, ppc_state);
  if constexpr (profiled)
    JitBlock::ProfileData::EndProfiling(operands.profile_data, operands.downcount);

  if (DivergenceTracer::IsBlockTracingEnabled())
    DivergenceTracer::LogBlock(ppc_state.pc, ppc_state);

  return 0;
}

template <bool write_pc>
s32 StencilInterpreter::Interpret(PowerPC::PowerPCState& ppc_state,
                                  const InterpretOperands& operands)
{
  if constexpr (write_pc)
  {
    ppc_state.pc = operands.current_pc;
    ppc_state.npc = operands.current_pc + 4;
  }
  operands.func(operands.interpreter, operands.inst);
  return sizeof(AnyCallback) + sizeof(operands);
}

template <bool write_pc>
s32 StencilInterpreter::InterpretAndCheckExceptions(
    PowerPC::PowerPCState& ppc_state, const InterpretAndCheckExceptionsOperands& operands)
{
  if constexpr (write_pc)
  {
    ppc_state.pc = operands.current_pc;
    ppc_state.npc = operands.current_pc + 4;
  }
  operands.func(operands.interpreter, operands.inst);

  if ((ppc_state.Exceptions & (EXCEPTION_DSI | EXCEPTION_PROGRAM)) != 0)
  {
    ppc_state.pc = operands.current_pc;
    ppc_state.downcount -= operands.downcount;
    operands.power_pc.CheckExceptions();
    if (DivergenceTracer::IsBlockTracingEnabled())
      DivergenceTracer::LogBlock(ppc_state.pc, ppc_state);
    return 0;
  }
  return sizeof(AnyCallback) + sizeof(operands);
}

template <bool write_pc>
s32 StencilInterpreter::ExecuteStencil(PowerPC::PowerPCState& ppc_state,
                                      const StencilOperands& operands)
{
  if constexpr (write_pc)
  {
    ppc_state.pc = operands.current_pc;
    ppc_state.npc = operands.current_pc + 4;
  }

#if defined(__aarch64__) || defined(_M_ARM64)
  // Tape layout:
  // tape[0] = 0
  // tape[1] = operands.operand_arg (imm32)
  // tape[2] = a642a64_stencil_exit (the return point for the stencil tail-call: LDR X16,[X20,#16]!; BR X16)
  alignas(16) uint64_t tape[4] = {
      0,
      operands.operand_arg,
      reinterpret_cast<uint64_t>(&a642a64_stencil_exit),
      0};

  a642a64_run_stencil(operands.exec_bytes, tape, &ppc_state, s_system_instance->GetMemory().GetRAM(), nullptr);

#else
  // Non-ARM64 fallback: emulate ALU operations directly
  if (operands.ra_gpr < 32 && operands.rd_gpr < 32)
  {
    u32 ra_val = (operands.ra_gpr == 0) ? 0 : ppc_state.gpr[operands.ra_gpr];
    ppc_state.gpr[operands.rd_gpr] = ra_val + operands.operand_arg;
  }
#endif

  return sizeof(AnyCallback) + sizeof(operands);
}

template <bool write_pc>
s32 StencilInterpreter::ExecuteTapeRun(PowerPC::PowerPCState& ppc_state, const void* payload)
{
  const auto* operands = reinterpret_cast<const TapeRunOperands*>(payload);
  if constexpr (write_pc)
  {
    ppc_state.pc = operands->current_pc;
    ppc_state.npc = operands->current_pc + 4;
  }
#if defined(__aarch64__) || defined(_M_ARM64)
  const uint64_t* tape = reinterpret_cast<const uint64_t*>(
      reinterpret_cast<const u8*>(payload) + sizeof(TapeRunOperands));

  a642a64_run_stencil(operands->first_stencil, const_cast<uint64_t*>(tape), &ppc_state,
                      s_system_instance->GetMemory().GetRAM(), &ppc_state.downcount);
#endif

  if constexpr (write_pc)
    return 0;

  return static_cast<s32>(sizeof(AnyCallback) + sizeof(TapeRunOperands) +
                          operands->tape_num_entries * sizeof(uint64_t));
}

void StencilInterpreter::EmitTapeRun(const std::vector<StencilTapeItem>& run, u32 pc)
{
  if (run.empty())
    return;

  // Stencils in the tape run will decrement downcount in registers at runtime.
  // Deduct their cycles from js.downcountAmount so EndBlock doesn't double-count them.
  if (js.downcountAmount >= run.size())
    js.downcountAmount -= static_cast<u32>(run.size());
  else
    js.downcountAmount = 0;

  const size_t M = run.size();
  const size_t num_words = 2 * M + 2;
  std::vector<uint64_t> tape(num_words, 0);

  tape[0] = 0;
  tape[1] = run[0].operand;
  for (size_t k = 1; k < M; ++k)
  {
    tape[2 * k] = reinterpret_cast<uint64_t>(run[k].code);
    tape[2 * k + 1] = run[k].operand;
  }
  tape[2 * M] = reinterpret_cast<uint64_t>(&a642a64_stencil_exit);
  tape[2 * M + 1] = 0;

  TapeRunOperands header{
      .first_stencil = run[0].code,
      .tape_num_entries = static_cast<u32>(num_words),
      .current_pc = run.back().pc,
  };

  const bool can_end = run.back().canEndBlock;
  const size_t total_payload_size = sizeof(TapeRunOperands) + num_words * sizeof(uint64_t);
  std::vector<u8> payload(total_payload_size);
  std::memcpy(payload.data(), &header, sizeof(header));
  std::memcpy(payload.data() + sizeof(header), tape.data(), num_words * sizeof(uint64_t));

  Write(can_end ? AnyCallbackCast(ExecuteTapeRun<true>) :
                  AnyCallbackCast(ExecuteTapeRun<false>),
        payload.data(), total_payload_size);
}

#if defined(__aarch64__) || defined(_M_ARM64)
static_assert(sizeof(StencilInterpreter::TapeTargetLink) == 16);
static_assert(offsetof(StencilInterpreter::TapeDirectLinkData, target.first_stencil) == 0);
static_assert(offsetof(StencilInterpreter::TapeDirectLinkData, target.tape_cursor) == 8);
static_assert(offsetof(StencilInterpreter::TapeDirectLinkData, destination_pc) == 16);
static_assert(offsetof(StencilInterpreter::TapeDirectLinkData, downcount_cycles) == 20);

static_assert(offsetof(StencilInterpreter::TapeCondBranchLinkData, taken.first_stencil) == 0);
static_assert(offsetof(StencilInterpreter::TapeCondBranchLinkData, taken.tape_cursor) == 8);
static_assert(offsetof(StencilInterpreter::TapeCondBranchLinkData, taken_destination_pc) == 16);
static_assert(offsetof(StencilInterpreter::TapeCondBranchLinkData, downcount_cycles) == 20);
static_assert(offsetof(StencilInterpreter::TapeCondBranchLinkData, fallthrough.first_stencil) == 24);
static_assert(offsetof(StencilInterpreter::TapeCondBranchLinkData, fallthrough.tape_cursor) == 32);
static_assert(offsetof(StencilInterpreter::TapeCondBranchLinkData, fallthrough_destination_pc) == 40);
static_assert(offsetof(StencilInterpreter::TapeCondBranchLinkData, bo) == 48);
static_assert(offsetof(StencilInterpreter::TapeCondBranchLinkData, bi) == 52);
#endif

bool StencilInterpreter::TryEmitTapeBranch(const PPCAnalyst::CodeOp& op, JitBlock* b,
                                           std::vector<StencilTapeItem>& tape_run)
{
  return false;
#if defined(__aarch64__) || defined(_M_ARM64)
  if (!jo.enableBlocklink || op.branchIsIdleLoop || IsProfilingEnabled() || b == nullptr)
    return false;

  const u32 primary_op = op.inst.OPCD;
  const bool is_direct = (primary_op == 18 && op.inst.LK == 0);
  const bool is_cond = (primary_op == 16 && op.inst.LK == 0);

  if (!is_direct && !is_cond)
    return false;

  const size_t M = tape_run.size();
  const size_t num_words = 2 * (M + 1);

  const u8* current_code = GetWritableCodePtr();
  u8* payload_base = const_cast<u8*>(current_code) + sizeof(AnyCallback);
  const size_t tape_bytes = num_words * sizeof(uint64_t);

  std::vector<uint64_t> tape(num_words, 0);

  if (M > 0)
  {
    tape[0] = 0;
    tape[1] = tape_run[0].operand;
    for (size_t k = 1; k < M; ++k)
    {
      tape[2 * k] = reinterpret_cast<uint64_t>(tape_run[k].code);
      tape[2 * k + 1] = tape_run[k].operand;
    }
  }

  const size_t branch_idx = M;

  if (is_direct)
  {
    const size_t total_payload_size = sizeof(TapeRunOperands) + tape_bytes + sizeof(TapeDirectLinkData);
    auto* link_data = reinterpret_cast<TapeDirectLinkData*>(
        payload_base + sizeof(TapeRunOperands) + tape_bytes);

    tape[2 * branch_idx] = (branch_idx == 0) ? 0 : reinterpret_cast<uint64_t>(&stencil_tape_direct_jump);
    tape[2 * branch_idx + 1] = reinterpret_cast<uint64_t>(link_data);

    const u8* first_stencil = (M > 0) ? tape_run[0].code :
                                        reinterpret_cast<const u8*>(&stencil_tape_direct_jump);

    TapeDirectLinkData link_data_init{
        .target = {nullptr, nullptr},
        .destination_pc = op.branchTo,
        .downcount_cycles = js.downcountAmount,
    };

    TapeRunOperands header{
        .first_stencil = first_stencil,
        .tape_num_entries = static_cast<u32>(num_words),
        .current_pc = op.address,
    };

    std::vector<u8> payload(total_payload_size);
    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), tape.data(), tape_bytes);
    std::memcpy(payload.data() + sizeof(header) + tape_bytes, &link_data_init, sizeof(link_data_init));

    Write(AnyCallbackCast(ExecuteTapeRun<true>), payload.data(), total_payload_size);

    JitBlock::LinkData link;
    link.exitAddress = op.branchTo;
    link.exitPtrs = reinterpret_cast<u8*>(&link_data->target);
    link.exitFarcode = reinterpret_cast<const u8*>(1);
    link.linkStatus = false;
    link.call = false;
    b->linkData.push_back(link);
  }
  else
  {
    const size_t total_payload_size = sizeof(TapeRunOperands) + tape_bytes + sizeof(TapeCondBranchLinkData);
    auto* link_data = reinterpret_cast<TapeCondBranchLinkData*>(
        payload_base + sizeof(TapeRunOperands) + tape_bytes);

    tape[2 * branch_idx] = (branch_idx == 0) ? 0 : reinterpret_cast<uint64_t>(&stencil_tape_cond_branch);
    tape[2 * branch_idx + 1] = reinterpret_cast<uint64_t>(link_data);

    const u8* first_stencil = (M > 0) ? tape_run[0].code :
                                        reinterpret_cast<const u8*>(&stencil_tape_cond_branch);

    TapeCondBranchLinkData link_data_init{
        .taken = {nullptr, nullptr},
        .taken_destination_pc = op.branchTo,
        .downcount_cycles = js.downcountAmount,
        .fallthrough = {nullptr, nullptr},
        .fallthrough_destination_pc = op.address + 4,
        .pad = 0,
        .bo = op.inst.BO,
        .bi = op.inst.BI,
    };

    TapeRunOperands header{
        .first_stencil = first_stencil,
        .tape_num_entries = static_cast<u32>(num_words),
        .current_pc = op.address,
    };

    std::vector<u8> payload(total_payload_size);
    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), tape.data(), tape_bytes);
    std::memcpy(payload.data() + sizeof(header) + tape_bytes, &link_data_init, sizeof(link_data_init));

    Write(AnyCallbackCast(ExecuteTapeRun<true>), payload.data(), total_payload_size);

    JitBlock::LinkData link_taken;
    link_taken.exitAddress = op.branchTo;
    link_taken.exitPtrs = reinterpret_cast<u8*>(&link_data->taken);
    link_taken.exitFarcode = reinterpret_cast<const u8*>(1);
    link_taken.linkStatus = false;
    link_taken.call = false;
    b->linkData.push_back(link_taken);

    JitBlock::LinkData link_fallthrough;
    link_fallthrough.exitAddress = op.address + 4;
    link_fallthrough.exitPtrs = reinterpret_cast<u8*>(&link_data->fallthrough);
    link_fallthrough.exitFarcode = reinterpret_cast<const u8*>(1);
    link_fallthrough.linkStatus = false;
    link_fallthrough.call = false;
    b->linkData.push_back(link_fallthrough);
  }

  tape_run.clear();
  return true;
#else
  return false;
#endif
}

std::optional<StencilInterpreter::StencilTapeItem>
StencilInterpreter::TryEmitStencil(const PPCAnalyst::CodeOp& op)
{
#if defined(__aarch64__) || defined(_M_ARM64)
  const u32 primary_op = op.inst.OPCD;

  // 1. addi (primary 14)
  if (primary_op == 14)
  {
    const s16 imm = static_cast<s16>(op.inst.SIMM_16);
    uint64_t packed = (static_cast<uint64_t>(op.inst.RD) << 24) |
                      (static_cast<uint64_t>(op.inst.RA) << 16) |
                      static_cast<uint16_t>(imm);
    return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_addi), packed, op.address, op.canEndBlock};
  }

  // 2. addis (primary 15)
  if (primary_op == 15)
  {
    const s16 imm = static_cast<s16>(op.inst.SIMM_16);
    uint64_t packed = (static_cast<uint64_t>(op.inst.RD) << 24) |
                      (static_cast<uint64_t>(op.inst.RA) << 16) |
                      static_cast<uint16_t>(imm);
    return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_addis), packed, op.address, op.canEndBlock};
  }

  // 3. ori (primary 24): RA = RS | UIMM
  if (primary_op == 24)
  {
    const u16 uimm = op.inst.UIMM;
    uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                      (static_cast<uint64_t>(op.inst.RD) << 16) |
                      uimm;
    return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_ori), packed, op.address, op.canEndBlock};
  }

  // 4. oris (primary 25): RA = RS | (UIMM << 16)
  if (primary_op == 25)
  {
    const u16 uimm = op.inst.UIMM;
    uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                      (static_cast<uint64_t>(op.inst.RD) << 16) |
                      uimm;
    return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_oris), packed, op.address, op.canEndBlock};
  }

  // 5. xori (primary 26): RA = RS ^ UIMM
  if (primary_op == 26)
  {
    const u16 uimm = op.inst.UIMM;
    uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                      (static_cast<uint64_t>(op.inst.RD) << 16) |
                      uimm;
    return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_xori), packed, op.address, op.canEndBlock};
  }

  // 6. xoris (primary 27): RA = RS ^ (UIMM << 16)
  if (primary_op == 27)
  {
    const u16 uimm = op.inst.UIMM;
    uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                      (static_cast<uint64_t>(op.inst.RD) << 16) |
                      uimm;
    return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_xoris), packed, op.address, op.canEndBlock};
  }

  // 7. rlwinm (primary 21, Rc == 0): RA = Rot(RS, SH) & Mask
  if (primary_op == 21 && op.inst.Rc == 0)
  {
    const u32 mask = MakeRotationMask(op.inst.MB, op.inst.ME);
    const u32 ror_amt = (32 - op.inst.SH) & 31;
    uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 48) |
                      (static_cast<uint64_t>(op.inst.RD) << 40) |
                      (static_cast<uint64_t>(ror_amt) << 32) |
                      mask;
    return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_rlwinm), packed, op.address, op.canEndBlock};
  }

  // 8. cmpli (primary 10): Compare Logical Immediate
  if (primary_op == 10)
  {
    const u16 uimm = op.inst.UIMM;
    uint64_t packed = (static_cast<uint64_t>(op.inst.CRFD) << 24) |
                      (static_cast<uint64_t>(op.inst.RA) << 16) |
                      uimm;
    return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_cmplwi), packed, op.address, op.canEndBlock};
  }

  // 9. cmpi (primary 11): Compare Immediate
  if (primary_op == 11)
  {
    const s16 simm = static_cast<s16>(op.inst.SIMM_16);
    uint64_t packed = (static_cast<uint64_t>(op.inst.CRFD) << 24) |
                      (static_cast<uint64_t>(op.inst.RA) << 16) |
                      static_cast<uint16_t>(simm);
    return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_cmpwi), packed, op.address, op.canEndBlock};
  }

  // 10. Primary 31 ALU, logical, and compare operations (Rc == 0)
  if (primary_op == 31 && op.inst.Rc == 0)
  {
    const u32 subop = op.inst.SUBOP10;
    // cmp (0): Compare Signed
    if (subop == 0)
    {
      uint64_t packed = (static_cast<uint64_t>(op.inst.CRFD) << 24) |
                        (static_cast<uint64_t>(op.inst.RA) << 16) |
                        (static_cast<uint64_t>(op.inst.RB) << 8);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_cmp), packed, op.address, op.canEndBlock};
    }
    // cmpl (32): Compare Logical
    if (subop == 32)
    {
      uint64_t packed = (static_cast<uint64_t>(op.inst.CRFD) << 24) |
                        (static_cast<uint64_t>(op.inst.RA) << 16) |
                        (static_cast<uint64_t>(op.inst.RB) << 8);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_cmpl), packed, op.address, op.canEndBlock};
    }
    // add (266): RD = RA + RB
    if (subop == 266)
    {
      uint64_t packed = (static_cast<uint64_t>(op.inst.RD) << 24) |
                        (static_cast<uint64_t>(op.inst.RA) << 16) |
                        (static_cast<uint64_t>(op.inst.RB) << 8);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_add), packed, op.address, op.canEndBlock};
    }
    // subf (40): RD = RB - RA
    if (subop == 40)
    {
      uint64_t packed = (static_cast<uint64_t>(op.inst.RD) << 24) |
                        (static_cast<uint64_t>(op.inst.RA) << 16) |
                        (static_cast<uint64_t>(op.inst.RB) << 8);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_subf), packed, op.address, op.canEndBlock};
    }
    // or (444): RA = RS | RB. Note: mr rA, rS is or rA, rS, rS
    if (subop == 444)
    {
      if (op.inst.RD == op.inst.RB) // mr rA, rS
      {
        uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                          (static_cast<uint64_t>(op.inst.RD) << 16);
        return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_mr), packed, op.address, op.canEndBlock};
      }
      uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                        (static_cast<uint64_t>(op.inst.RD) << 16) |
                        (static_cast<uint64_t>(op.inst.RB) << 8);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_or), packed, op.address, op.canEndBlock};
    }

    // and (28): RA = RS & RB
    if (subop == 28)
    {
      uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                        (static_cast<uint64_t>(op.inst.RD) << 16) |
                        (static_cast<uint64_t>(op.inst.RB) << 8);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_and), packed, op.address, op.canEndBlock};
    }
    // xor (316): RA = RS ^ RB
    if (subop == 316)
    {
      uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                        (static_cast<uint64_t>(op.inst.RD) << 16) |
                        (static_cast<uint64_t>(op.inst.RB) << 8);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_xor), packed, op.address, op.canEndBlock};
    }
    // nor (124): RA = ~(RS | RB)
    if (subop == 124)
    {
      uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                        (static_cast<uint64_t>(op.inst.RD) << 16) |
                        (static_cast<uint64_t>(op.inst.RB) << 8);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_nor), packed, op.address, op.canEndBlock};
    }
    // slw (24): RA = RS << (RB & 31)
    if (subop == 24)
    {
      uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                        (static_cast<uint64_t>(op.inst.RD) << 16) |
                        (static_cast<uint64_t>(op.inst.RB) << 8);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_slw), packed, op.address, op.canEndBlock};
    }
    // srw (536): RA = RS >> (RB & 31)
    if (subop == 536)
    {
      uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                        (static_cast<uint64_t>(op.inst.RD) << 16) |
                        (static_cast<uint64_t>(op.inst.RB) << 8);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_srw), packed, op.address, op.canEndBlock};
    }
    // neg (104): RD = -RA
    if (subop == 104)
    {
      uint64_t packed = (static_cast<uint64_t>(op.inst.RD) << 24) |
                        (static_cast<uint64_t>(op.inst.RA) << 16);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_neg), packed, op.address, op.canEndBlock};
    }
    // extsb (954): RA = (s8)RS
    if (subop == 954)
    {
      uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                        (static_cast<uint64_t>(op.inst.RD) << 16);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_extsb), packed, op.address, op.canEndBlock};
    }
    // extsh (922): RA = (s16)RS
    if (subop == 922)
    {
      uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                        (static_cast<uint64_t>(op.inst.RD) << 16);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_extsh), packed, op.address, op.canEndBlock};
    }
    // cntlzw (26): RA = cntlzw(RS)
    if (subop == 26)
    {
      uint64_t packed = (static_cast<uint64_t>(op.inst.RA) << 24) |
                        (static_cast<uint64_t>(op.inst.RD) << 16);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_cntlzw), packed, op.address, op.canEndBlock};
    }
  }

  if (!jo.memcheck)
  {
    // 11. lbz (primary 34): RD = [RA + SIMM_16]
    if (primary_op == 34)
    {
      const s16 simm = static_cast<s16>(op.inst.SIMM_16);
      uint64_t packed = (static_cast<uint64_t>(op.inst.RD) << 24) |
                        (static_cast<uint64_t>(op.inst.RA) << 16) |
                        static_cast<uint16_t>(simm);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_lbz), packed, op.address, op.canEndBlock};
    }

    // 12. lhz (primary 40): RD = [RA + SIMM_16]
    if (primary_op == 40)
    {
      const s16 simm = static_cast<s16>(op.inst.SIMM_16);
      uint64_t packed = (static_cast<uint64_t>(op.inst.RD) << 24) |
                        (static_cast<uint64_t>(op.inst.RA) << 16) |
                        static_cast<uint16_t>(simm);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_lhz), packed, op.address, op.canEndBlock};
    }

    // 13. lwz (primary 32): RD = [RA + SIMM_16]
    if (primary_op == 32)
    {
      const s16 simm = static_cast<s16>(op.inst.SIMM_16);
      uint64_t packed = (static_cast<uint64_t>(op.inst.RD) << 24) |
                        (static_cast<uint64_t>(op.inst.RA) << 16) |
                        static_cast<uint16_t>(simm);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_lwz), packed, op.address, op.canEndBlock};
    }

    // 14. stw (primary 36): [RA + SIMM_16] = RS
    if (primary_op == 36)
    {
      const s16 simm = static_cast<s16>(op.inst.SIMM_16);
      uint64_t packed = (static_cast<uint64_t>(op.inst.RD) << 24) |
                        (static_cast<uint64_t>(op.inst.RA) << 16) |
                        static_cast<uint16_t>(simm);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_stw), packed, op.address, op.canEndBlock};
    }

    // 15. stb (primary 38): [RA + SIMM_16] = RS
    if (primary_op == 38)
    {
      const s16 simm = static_cast<s16>(op.inst.SIMM_16);
      uint64_t packed = (static_cast<uint64_t>(op.inst.RD) << 24) |
                        (static_cast<uint64_t>(op.inst.RA) << 16) |
                        static_cast<uint16_t>(simm);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_stb), packed, op.address, op.canEndBlock};
    }

    // 16. sth (primary 44): [RA + SIMM_16] = RS
    if (primary_op == 44)
    {
      const s16 simm = static_cast<s16>(op.inst.SIMM_16);
      uint64_t packed = (static_cast<uint64_t>(op.inst.RD) << 24) |
                        (static_cast<uint64_t>(op.inst.RA) << 16) |
                        static_cast<uint16_t>(simm);
      return StencilTapeItem{reinterpret_cast<const u8*>(&stencil_dyn_sth), packed, op.address, op.canEndBlock};
    }
  }

#endif

  return std::nullopt;
}

s32 StencilInterpreter::HLEFunction(PowerPC::PowerPCState& ppc_state,
                                    const HLEFunctionOperands& operands)
{
  const auto& [system, current_pc, hook_index] = operands;
  ppc_state.pc = current_pc;
  HLE::Execute(Core::CPUThreadGuard{system}, current_pc, hook_index);
  return sizeof(AnyCallback) + sizeof(operands);
}

s32 StencilInterpreter::WriteBrokenBlockNPC(PowerPC::PowerPCState& ppc_state,
                                            const WriteBrokenBlockNPCOperands& operands)
{
  const auto& [current_pc] = operands;
  ppc_state.npc = current_pc;
  return sizeof(AnyCallback) + sizeof(operands);
}

s32 StencilInterpreter::CheckFPU(PowerPC::PowerPCState& ppc_state,
                                  const CheckHaltOperands& operands)
{
  const auto& [power_pc, current_pc, downcount] = operands;
  if (!ppc_state.msr.FP)
  {
    ppc_state.pc = current_pc;
    ppc_state.downcount -= downcount;
    ppc_state.Exceptions |= EXCEPTION_FPU_UNAVAILABLE;
    power_pc.CheckExceptions();
    if (DivergenceTracer::IsBlockTracingEnabled())
      DivergenceTracer::LogBlock(ppc_state.pc, ppc_state);
    return 0;
  }
  return sizeof(AnyCallback) + sizeof(operands);
}

s32 StencilInterpreter::CheckBreakpoint(PowerPC::PowerPCState& ppc_state,
                                        const CheckHaltOperands& operands)
{
  const auto& [power_pc, current_pc, downcount] = operands;
  ppc_state.pc = current_pc;
  if (power_pc.CheckAndHandleBreakPoints())
  {
    power_pc.GetPPCState().downcount -= downcount;
    if (DivergenceTracer::IsBlockTracingEnabled())
      DivergenceTracer::LogBlock(ppc_state.pc, ppc_state);
    return 0;
  }
  return sizeof(AnyCallback) + sizeof(operands);
}

s32 StencilInterpreter::CheckIdle(PowerPC::PowerPCState& ppc_state,
                                  const CheckIdleOperands& operands)
{
  const auto& [core_timing, idle_pc] = operands;
  if (ppc_state.npc == idle_pc)
    core_timing.Idle();
  return sizeof(AnyCallback) + sizeof(operands);
}

bool StencilInterpreter::HandleFunctionHooking(u32 address)
{
  const auto result = HLE::TryReplaceFunction(m_ppc_symbol_db, address, PowerPC::CoreMode::JIT);
  if (!result)
    return false;

  Write(HLEFunction, {m_system, address, result.hook_index});

  if (result.type != HLE::HookType::Replace)
    return false;

  js.downcountAmount += js.st.numCycles;
  WriteEndBlock();
  return true;
}

void StencilInterpreter::WriteEndBlock()
{
  if (IsProfilingEnabled())
  {
    Write(EndBlock<true>, {{js.downcountAmount, js.numLoadStoreInst, js.numFloatingPointInst},
                           js.curBlock->profile_data.get()});
  }
  else
  {
    Write(EndBlock<false>, {js.downcountAmount, js.numLoadStoreInst, js.numFloatingPointInst});
  }
}

s32 StencilInterpreter::ExecuteBlockLinkDirect(PowerPC::PowerPCState& ppc_state,
                                              const BlockLinkDirectOperands& operands)
{
  ppc_state.pc = operands.destination;
  ppc_state.npc = operands.destination;
  ppc_state.downcount -= operands.downcount_amount;
  PowerPC::UpdatePerformanceMonitor(operands.downcount_amount, operands.num_load_stores,
                                    operands.num_fp_inst, ppc_state);

  if (DivergenceTracer::IsBlockTracingEnabled())
    DivergenceTracer::LogBlock(ppc_state.pc, ppc_state);

  if (ppc_state.downcount <= 0)
    return 0;

  if (operands.target_normal_entry != nullptr)
  {
    const u8* current_callback_ptr = reinterpret_cast<const u8*>(&operands) - sizeof(AnyCallback);
    return static_cast<s32>(operands.target_normal_entry - current_callback_ptr);
  }

  return 0;
}

s32 StencilInterpreter::ExecuteBlockLinkBranch(PowerPC::PowerPCState& ppc_state,
                                              const BlockLinkBranchOperands& operands)
{
  if ((operands.inst.BO & BO_DONT_DECREMENT_FLAG) == 0)
    ppc_state.spr[SPR_CTR]--;

  const bool true_false = ((operands.inst.BO >> 3) & 1) != 0;
  const bool only_counter_check = ((operands.inst.BO >> 4) & 1) != 0;
  const bool only_condition_check = ((operands.inst.BO >> 2) & 1) != 0;
  const u32 ctr_check = ((ppc_state.spr[SPR_CTR] != 0) ^ (operands.inst.BO >> 1)) & 1;
  const bool counter = only_condition_check || ctr_check != 0;
  const bool condition = only_counter_check || (ppc_state.cr.GetBit(operands.inst.BI) == u32(true_false));

  const bool taken = counter && condition;
  const u32 destination = taken ? operands.taken_destination : operands.fallthrough_destination;
  ppc_state.pc = destination;
  ppc_state.npc = destination;

  ppc_state.downcount -= operands.downcount_amount;
  PowerPC::UpdatePerformanceMonitor(operands.downcount_amount, operands.num_load_stores,
                                    operands.num_fp_inst, ppc_state);

  if (DivergenceTracer::IsBlockTracingEnabled())
    DivergenceTracer::LogBlock(ppc_state.pc, ppc_state);

  // Safe Multi-Block Idle Loop Detection:
  // Only applies when determinism is not requested (keeps lockstep test 100% deterministic),
  // and strictly for blocks with <= 1 load and 0 stores/floats (ensuring compute/decompression loops are never skipped).
  if (!Core::WantsDeterminism() && operands.num_load_stores <= 1 && operands.num_fp_inst == 0)
  {
    thread_local u32 tl_target1 = 0;
    thread_local u32 tl_target2 = 0;
    thread_local u32 tl_ping_pong_count = 0;

    if (taken && destination == tl_target1)
    {
      if (++tl_ping_pong_count >= 64)
      {
        tl_ping_pong_count = 0;
        if (s_system_instance)
          s_system_instance->GetCoreTiming().Idle();
        return 0;
      }
      tl_target1 = tl_target2;
      tl_target2 = destination;
    }
    else if (taken)
    {
      tl_target1 = tl_target2;
      tl_target2 = destination;
      tl_ping_pong_count = 0;
    }
    else
    {
      tl_ping_pong_count = 0;
    }
  }

  if (ppc_state.downcount <= 0)
    return 0;

  const u8* target_entry = taken ? operands.taken_normal_entry : operands.fallthrough_normal_entry;
  if (target_entry != nullptr)
  {
    const u8* current_callback_ptr = reinterpret_cast<const u8*>(&operands) - sizeof(AnyCallback);
    return static_cast<s32>(target_entry - current_callback_ptr);
  }

  return 0;
}

bool StencilInterpreter::TryWriteBlockEnd(const PPCAnalyst::CodeOp& op, JitBlock* b)
{
  if (jo.enableBlocklink && !op.branchIsIdleLoop && !IsProfilingEnabled() && b != nullptr)
  {
    if (op.inst.OPCD == 18 && op.inst.LK == 0)  // bx (b, ba)
    {
      const u8* current_code = GetWritableCodePtr();
      const BlockLinkDirectOperands link_operands{
          .target_normal_entry = nullptr,
          .destination = op.branchTo,
          .downcount_amount = js.downcountAmount,
          .num_load_stores = js.numLoadStoreInst,
          .num_fp_inst = js.numFloatingPointInst,
      };
      Write(ExecuteBlockLinkDirect, link_operands);

      u8* operands_ptr = const_cast<u8*>(current_code) + sizeof(AnyCallback);
      JitBlock::LinkData link_data;
      link_data.exitAddress = op.branchTo;
      link_data.exitPtrs = operands_ptr + offsetof(BlockLinkDirectOperands, target_normal_entry);
      link_data.linkStatus = false;
      link_data.call = false;
#ifdef _M_ARM_64
      link_data.exitFarcode = nullptr;
#endif
      b->linkData.push_back(link_data);
      return true;
    }
    else if (op.inst.OPCD == 16 && op.inst.LK == 0)  // bcx (bc, bca)
    {
      const u8* current_code = GetWritableCodePtr();
      const BlockLinkBranchOperands link_operands{
          .taken_normal_entry = nullptr,
          .fallthrough_normal_entry = nullptr,
          .taken_destination = op.branchTo,
          .fallthrough_destination = op.address + 4,
          .downcount_amount = js.downcountAmount,
          .num_load_stores = js.numLoadStoreInst,
          .num_fp_inst = js.numFloatingPointInst,
          .inst = op.inst,
      };
      Write(ExecuteBlockLinkBranch, link_operands);

      u8* operands_ptr = const_cast<u8*>(current_code) + sizeof(AnyCallback);
      // Link for taken branch
      JitBlock::LinkData link_taken;
      link_taken.exitAddress = op.branchTo;
      link_taken.exitPtrs = operands_ptr + offsetof(BlockLinkBranchOperands, taken_normal_entry);
      link_taken.linkStatus = false;
      link_taken.call = false;
#ifdef _M_ARM_64
      link_taken.exitFarcode = nullptr;
#endif
      b->linkData.push_back(link_taken);

      // Link for fallthrough branch
      JitBlock::LinkData link_fallthrough;
      link_fallthrough.exitAddress = op.address + 4;
      link_fallthrough.exitPtrs = operands_ptr + offsetof(BlockLinkBranchOperands, fallthrough_normal_entry);
      link_fallthrough.linkStatus = false;
      link_fallthrough.call = false;
#ifdef _M_ARM_64
      link_fallthrough.exitFarcode = nullptr;
#endif
      b->linkData.push_back(link_fallthrough);
      return true;
    }
  }

  return false;
}

bool StencilInterpreter::SetEmitterStateToFreeCodeRegion()
{
  const auto free = m_free_ranges.by_size_begin();
  if (free == m_free_ranges.by_size_end())
  {
    WARN_LOG_FMT(DYNA_REC, "Failed to find free memory region in code region.");
    return false;
  }
  SetCodePtr(free.from(), free.to());
  return true;
}

void StencilInterpreter::FreeRanges()
{
  for (const auto& [from, to] : m_block_cache.GetRangesToFree())
    m_free_ranges.insert(from, to);
  m_block_cache.ClearRangesToFree();
}

void StencilInterpreter::ResetFreeMemoryRanges()
{
  m_free_ranges.clear();
  m_free_ranges.insert(region, region + region_size);
}

void StencilInterpreter::Jit(u32 em_address)
{
  Jit(em_address, true);
}

void StencilInterpreter::Jit(u32 em_address, bool clear_cache_and_retry_on_failure)
{
  if (IsAlmostFull() || SConfig::GetInstance().bJITNoBlockCache)
  {
    ClearCache();
  }
  FreeRanges();

  const u32 nextPC =
      analyzer.Analyze(em_address, &code_block, &m_code_buffer, m_code_buffer.size());
  if (code_block.m_memory_exception)
  {
    m_ppc_state.npc = nextPC;
    m_ppc_state.Exceptions |= EXCEPTION_ISI;
    m_system.GetPowerPC().CheckExceptions();
    WARN_LOG_FMT(POWERPC, "ISI exception at {:#010x}", nextPC);
    return;
  }

  if (SetEmitterStateToFreeCodeRegion())
  {
    JitBlock* b = m_block_cache.AllocateBlock(em_address);
    b->normalEntry = b->near_begin = GetWritableCodePtr();

    if (DoJit(em_address, b, nextPC))
    {
      b->near_end = GetWritableCodePtr();
      b->far_begin = b->far_end = nullptr;

      if (b->near_begin != b->near_end)
        m_free_ranges.erase(b->near_begin, b->near_end);

      m_block_cache.FinalizeBlock(*b, jo.enableBlocklink, code_block, m_code_buffer);

#ifdef JIT_LOG_GENERATED_CODE
      LogGeneratedCode();
#endif

      return;
    }
  }

  if (clear_cache_and_retry_on_failure)
  {
    WARN_LOG_FMT(DYNA_REC, "flushing code caches, please report if this happens a lot");
    ClearCache();
    Jit(em_address, false);
    return;
  }

  PanicAlertFmtT("JIT failed to find code space after a cache clear. This should never happen. "
                 "Please report this incident on the bug tracker. Dolphin will now exit.");
  std::exit(-1);
}

bool StencilInterpreter::DoJit(u32 em_address, JitBlock* b, u32 nextPC)
{
  js.blockStart = em_address;
  js.firstFPInstructionFound = false;
  js.fifoBytesSinceCheck = 0;
  js.downcountAmount = 0;
  js.numLoadStoreInst = 0;
  js.numFloatingPointInst = 0;
  js.curBlock = b;

  auto& interpreter = m_system.GetInterpreter();
  auto& power_pc = m_system.GetPowerPC();
  auto& cpu = m_system.GetCPU();
  auto& breakpoints = power_pc.GetBreakPoints();

  if (IsProfilingEnabled())
    Write(StartProfiledBlock, {js.curBlock->profile_data.get()});

  std::vector<StencilTapeItem> tape_run;
  auto flush_tape_run = [&]() {
    if (!tape_run.empty())
    {
      EmitTapeRun(tape_run, tape_run.front().pc);
      tape_run.clear();
    }
  };

  if (em_address >= 0x800380e0 && em_address <= 0x80038130)
  {
    WARN_LOG_FMT(POWERPC, "[IDLE_INSPECT] Block at 0x{:08x} (num_inst={}):", em_address, code_block.m_num_instructions);
    for (u32 k = 0; k < code_block.m_num_instructions; ++k)
    {
      const auto& op_insp = m_code_buffer[k];
      WARN_LOG_FMT(POWERPC, "    [{:08x}] {} (type={}, branchTo=0x{:08x}, isIdle={})",
                   op_insp.address, op_insp.opinfo->opname, static_cast<int>(op_insp.opinfo->type), op_insp.branchTo, op_insp.branchIsIdleLoop);
    }
  }

  for (u32 i = 0; i < code_block.m_num_instructions; i++)
  {
    PPCAnalyst::CodeOp& op = m_code_buffer[i];
    js.op = &op;

    js.compilerPC = op.address;
    js.instructionsLeft = (code_block.m_num_instructions - 1) - i;
    js.downcountAmount += op.opinfo->num_cycles;
    if (op.opinfo->flags & FL_LOADSTORE)
      ++js.numLoadStoreInst;
    if (op.opinfo->flags & FL_USE_FPU)
      ++js.numFloatingPointInst;

    if (HLE::TryReplaceFunction(m_ppc_symbol_db, js.compilerPC, PowerPC::CoreMode::JIT))
    {
      flush_tape_run();
      if (HandleFunctionHooking(js.compilerPC))
        break;
    }

    if (!op.skip)
    {
      auto stencil_item = TryEmitStencil(op);
      const bool can_stencil = stencil_item.has_value() &&
                               (!IsDebuggingEnabled() || cpu.IsStepping() ||
                                !breakpoints.IsAddressBreakPoint(js.compilerPC)) &&
                               (js.firstFPInstructionFound || (op.opinfo->flags & FL_USE_FPU) == 0);


      if (can_stencil)
      {
        tape_run.push_back(*stencil_item);

        if (op.canEndBlock)
        {
          flush_tape_run();
          WriteEndBlock();
        }
      }
      else
      {
        if (op.canEndBlock && TryEmitTapeBranch(op, b, tape_run))
        {
          // Emitted native tape branch chaining!
        }
        else
        {
          flush_tape_run();

          if (op.canEndBlock && TryWriteBlockEnd(op, b))
          {
            // Combined branch condition evaluation + block link written directly!
          }
        else
        {
          if (IsDebuggingEnabled() && !cpu.IsStepping() &&
              breakpoints.IsAddressBreakPoint(js.compilerPC))
          {
            Write(CheckBreakpoint, {power_pc, js.compilerPC, js.downcountAmount});
          }
          if (!js.firstFPInstructionFound && (op.opinfo->flags & FL_USE_FPU) != 0)
          {
            Write(CheckFPU, {power_pc, js.compilerPC, js.downcountAmount});
            js.firstFPInstructionFound = true;
          }

          // Fallback to standard interpreter callback
          if ((jo.memcheck && (op.opinfo->flags & FL_LOADSTORE) != 0) ||
              (!op.canEndBlock && ShouldHandleFPExceptionForInstruction(&op)))
          {
            const InterpretAndCheckExceptionsOperands operands = {
                {interpreter, Interpreter::GetInterpreterOp(op.inst), js.compilerPC, op.inst},
                power_pc,
                js.downcountAmount};
            Write(op.canEndBlock ? CallbackCast(InterpretAndCheckExceptions<true>) :
                                   CallbackCast(InterpretAndCheckExceptions<false>),
                  operands);
          }
          else
          {
            const InterpretOperands operands = {interpreter, Interpreter::GetInterpreterOp(op.inst),
                                                js.compilerPC, op.inst};
            Write(op.canEndBlock ? CallbackCast(Interpret<true>) : CallbackCast(Interpret<false>),
                  operands);
          }

          if (op.branchIsIdleLoop)
            Write(CheckIdle, {m_system.GetCoreTiming(), js.blockStart});
          if (op.canEndBlock)
            WriteEndBlock();
        }
      }
    }
  }
}

  flush_tape_run();
  if (code_block.m_broken)
  {
    Write(WriteBrokenBlockNPC, {nextPC});
    WriteEndBlock();
  }

  if (HasWriteFailed())
  {
    WARN_LOG_FMT(DYNA_REC, "JIT ran out of space in code region during code generation.");
    return false;
  }
  return true;
}

void StencilInterpreter::EraseSingleBlock(const JitBlock& block)
{
  m_block_cache.EraseSingleBlock(block);
  FreeRanges();
}

std::vector<JitBase::MemoryStats> StencilInterpreter::GetMemoryStats() const
{
  return {{"free", m_free_ranges.get_stats()}};
}

std::size_t StencilInterpreter::DisassembleNearCode(const JitBlock& block,
                                                    std::ostream& stream) const
{
  stream << "Stencil Interpreter block execution\n";
  return 0;
}

std::size_t StencilInterpreter::DisassembleFarCode(const JitBlock& block,
                                                   std::ostream& stream) const
{
  stream << "N/A\n";
  return 0;
}

void StencilInterpreter::ClearCache()
{
  m_block_cache.Clear();
  m_block_cache.ClearRangesToFree();
  ClearCodeSpace();
  ResetFreeMemoryRanges();
  RefreshConfig();
  Host_JitCacheInvalidation();
}

void StencilInterpreter::LogGeneratedCode() const
{
  std::ostringstream stream;

  stream << "\nPPC Code Buffer:\n";
  for (const PPCAnalyst::CodeOp& op :
       std::span{m_code_buffer.data(), code_block.m_num_instructions})
  {
    fmt::print(stream, "0x{:08x}\t\t{}\n", op.address,
               Common::GekkoDisassembler::Disassemble(op.inst.hex, op.address));
  }

  stream << "\nHost Code (Stencil Interpreter):\n";
  stream << "(stencil disassembly not yet implemented)\n";

  DEBUG_LOG_FMT(DYNA_REC, "{}", std::move(stream).str());
}
