// Copyright 2014 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/RangeSizeSet.h"
#include "Core/PowerPC/CachedInterpreter/CachedInterpreterBlockCache.h"
#include "Core/PowerPC/CachedInterpreter/CachedInterpreterEmitter.h"
#include "Core/PowerPC/JitCommon/JitBase.h"
#include "Core/PowerPC/PPCAnalyst.h"
#include "Core/PowerPC/StencilInterpreter/StencilInterpreterBlockCache.h"

struct StencilEntry;

namespace CoreTiming
{
class CoreTimingManager;
}
namespace CPU
{
enum class State;
}
class Interpreter;

class StencilInterpreter : public JitBase, public CachedInterpreterCodeBlock
{
public:
  explicit StencilInterpreter(Core::System& system);
  StencilInterpreter(const StencilInterpreter&) = delete;
  StencilInterpreter(StencilInterpreter&&) = delete;
  StencilInterpreter& operator=(const StencilInterpreter&) = delete;
  StencilInterpreter& operator=(StencilInterpreter&&) = delete;
  ~StencilInterpreter() override;

  void Init() override;
  void Shutdown() override;

  bool HandleFault(uintptr_t access_address, SContext* ctx) override { return false; }
  void ClearCache() override;

  void Run() override;
  void SingleStep() override;

  void Jit(u32 address) override;
  void Jit(u32 address, bool clear_cache_and_retry_on_failure);
  bool DoJit(u32 address, JitBlock* b, u32 nextPC);

  void EraseSingleBlock(const JitBlock& block) override;
  std::vector<MemoryStats> GetMemoryStats() const override;

  std::size_t DisassembleNearCode(const JitBlock& block, std::ostream& stream) const override;
  std::size_t DisassembleFarCode(const JitBlock& block, std::ostream& stream) const override;

  JitBaseBlockCache* GetBlockCache() override { return &m_block_cache; }
  const char* GetName() const override { return "Stencil Interpreter"; }
  const CommonAsmRoutinesBase* GetAsmRoutines() override { return nullptr; }

  const u8* GetExecutableStencilBytes(const StencilEntry* s);

private:
  void ExecuteOneBlock();

  bool HandleFunctionHooking(u32 address);
  void WriteEndBlock();

  bool SetEmitterStateToFreeCodeRegion();

  void FreeRanges();
  void ResetFreeMemoryRanges();

  void LogGeneratedCode() const;

  struct StartProfiledBlockOperands;
  template <bool profiled>
  struct EndBlockOperands;
  struct InterpretOperands;
  struct InterpretAndCheckExceptionsOperands;
  struct StencilOperands;
  struct TapeRunOperands;
  struct HLEFunctionOperands;
  struct WriteBrokenBlockNPCOperands;
  struct CheckHaltOperands;
  struct CheckIdleOperands;
  struct BlockLinkDirectOperands;
  struct BlockLinkBranchOperands;

  struct StencilTapeItem
  {
    const u8* code;
    uint64_t operand;
    u32 pc;
    bool canEndBlock;
  };

  void EmitTapeRun(const std::vector<StencilTapeItem>& run, u32 pc);
  std::optional<StencilTapeItem> TryEmitStencil(const PPCAnalyst::CodeOp& op);
  bool TryWriteBlockEnd(const PPCAnalyst::CodeOp& op, JitBlock* b);

  static s32 StartProfiledBlock(PowerPC::PowerPCState& ppc_state,
                                const StartProfiledBlockOperands& operands);
  template <bool profiled>
  static s32 EndBlock(PowerPC::PowerPCState& ppc_state, const EndBlockOperands<profiled>& operands);
  template <bool write_pc>
  static s32 Interpret(PowerPC::PowerPCState& ppc_state, const InterpretOperands& operands);
  template <bool write_pc>
  static s32 InterpretAndCheckExceptions(PowerPC::PowerPCState& ppc_state,
                                         const InterpretAndCheckExceptionsOperands& operands);
  template <bool write_pc>
  static s32 ExecuteStencil(PowerPC::PowerPCState& ppc_state, const StencilOperands& operands);
  template <bool write_pc>
  static s32 ExecuteTapeRun(PowerPC::PowerPCState& ppc_state, const void* payload);
  static s32 ExecuteBlockLinkDirect(PowerPC::PowerPCState& ppc_state,
                                    const BlockLinkDirectOperands& operands);
  static s32 ExecuteBlockLinkBranch(PowerPC::PowerPCState& ppc_state,
                                    const BlockLinkBranchOperands& operands);
  static s32 HLEFunction(PowerPC::PowerPCState& ppc_state, const HLEFunctionOperands& operands);
  static s32 WriteBrokenBlockNPC(PowerPC::PowerPCState& ppc_state,
                                 const WriteBrokenBlockNPCOperands& operands);
  static s32 CheckFPU(PowerPC::PowerPCState& ppc_state, const CheckHaltOperands& operands);
  static s32 CheckBreakpoint(PowerPC::PowerPCState& ppc_state, const CheckHaltOperands& operands);
  static s32 CheckIdle(PowerPC::PowerPCState& ppc_state, const CheckIdleOperands& operands);

  Common::RangeSizeSet<u8*> m_free_ranges;
  StencilInterpreterBlockCache m_block_cache;

  u8* m_exec_stencil_pool = nullptr;
  size_t m_exec_stencil_pool_size = 0;

  // Benchmark stats
  u64 m_bench_last_time_ms = 0;
  u64 m_bench_blocks_executed = 0;
  s64 m_bench_last_downcount = 0;
  s64 m_bench_last_ticks = 0;

  struct BlockTraceEntry
  {
    u32 start_pc = 0;
    u32 end_pc = 0;
    u32 lr = 0;
    u32 ctr = 0;
    u32 r3 = 0;
    u32 r12 = 0;
    u32 stencils_executed = 0;
    u32 interprets_executed = 0;
  };
  std::array<BlockTraceEntry, 32> m_block_history{};
  size_t m_block_history_index = 0;
};

struct StencilInterpreter::StartProfiledBlockOperands
{
  JitBlock::ProfileData* profile_data;
};

template <>
struct StencilInterpreter::EndBlockOperands<false>
{
  u32 downcount;
  u32 num_load_stores;
  u32 num_fp_inst;
  u32 : 32;
};

template <>
struct StencilInterpreter::EndBlockOperands<true> : StencilInterpreter::EndBlockOperands<false>
{
  JitBlock::ProfileData* profile_data;
};

struct StencilInterpreter::InterpretOperands
{
  Interpreter& interpreter;
  void (*func)(Interpreter&, UGeckoInstruction);  // Interpreter::Instruction
  u32 current_pc;
  UGeckoInstruction inst;
};

struct StencilInterpreter::InterpretAndCheckExceptionsOperands : InterpretOperands
{
  PowerPC::PowerPCManager& power_pc;
  u32 downcount;
};

struct StencilInterpreter::StencilOperands
{
  const u8* exec_bytes;
  u32 current_pc;
  u32 operand_arg;  // immediate or secondary operand for the stencil
  u32 rd_gpr;
  u32 ra_gpr;
  u32 rb_gpr;
  u32 : 32;
};

struct StencilInterpreter::TapeRunOperands
{
  const u8* first_stencil;
  u32 tape_num_entries;
  u32 current_pc;
  u32 : 32;
};

struct StencilInterpreter::HLEFunctionOperands
{
  Core::System& system;
  u32 current_pc;
  u32 hook_index;
};

struct StencilInterpreter::WriteBrokenBlockNPCOperands
{
  u32 current_pc;
  u32 : 32;
};

struct StencilInterpreter::CheckHaltOperands
{
  PowerPC::PowerPCManager& power_pc;
  u32 current_pc;
  u32 downcount;
};

struct StencilInterpreter::CheckIdleOperands
{
  CoreTiming::CoreTimingManager& core_timing;
  u32 idle_pc;
};

struct StencilInterpreter::BlockLinkDirectOperands
{
  const u8* target_normal_entry;
  u32 destination;
  u32 downcount_amount;
  u32 num_load_stores;
  u32 num_fp_inst;
};

struct StencilInterpreter::BlockLinkBranchOperands
{
  const u8* taken_normal_entry;
  const u8* fallthrough_normal_entry;
  u32 taken_destination;
  u32 fallthrough_destination;
  u32 downcount_amount;
  u32 num_load_stores;
  u32 num_fp_inst;
  UGeckoInstruction inst;
};
