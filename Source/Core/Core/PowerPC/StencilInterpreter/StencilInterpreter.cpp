// Copyright 2014 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/PowerPC/StencilInterpreter/StencilInterpreter.h"

#include <span>
#include <sstream>
#include <utility>

#include <fmt/ostream.h>

#include "Common/CommonTypes.h"
#include "Common/GekkoDisassembler.h"
#include "Common/Logging/Log.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/HLE.h"
#include "Core/HW/CPU.h"
#include "Core/Host.h"
#include "Core/PowerPC/Gekko.h"
#include "Core/PowerPC/Interpreter/Interpreter.h"
#include "Core/PowerPC/Jit64Common/Jit64Constants.h"
#include "Core/PowerPC/PPCAnalyst.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "ppc_stencils_generated.h"
#include "Interpreter.h"

namespace
{
// Helper to map PPC GPR index to pinned index (0->r0, 1->r3, 2->r4, 3->r5, 4->r31)
// Returns -1 if not a pinned register
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

// Convert pinned index to ARM64 register index
constexpr int PinnedToARM64Reg(int p_idx)
{
  switch (p_idx)
  {
  case 0: return 23; // r0 -> X23
  case 1: return 21; // r3 -> X21
  case 2: return 22; // r4 -> X22
  case 3: return 24; // r5 -> X24
  case 4: return 25; // r31 -> X25
  default: return 0;
  }
}
}  // namespace

StencilInterpreter::StencilInterpreter(Core::System& system)
    : JitBase(system), m_block_cache(*this)
{
}

StencilInterpreter::~StencilInterpreter() = default;

void StencilInterpreter::Init()
{
  RefreshConfig();

  AllocCodeSpace(CODE_SIZE);
  ResetFreeMemoryRanges();

  jo.enableBlocklink = false;

  m_block_cache.Init();

  code_block.m_stats = &js.st;
  code_block.m_gpa = &js.gpa;
  code_block.m_fpa = &js.fpa;
}

void StencilInterpreter::Shutdown()
{
  m_block_cache.Shutdown();
}

void StencilInterpreter::ExecuteOneBlock()
{
  const u8* normal_entry = m_block_cache.Dispatch();
  if (!normal_entry)
  {
    Jit(m_ppc_state.pc);
    return;
  }

  auto& ppc_state = m_ppc_state;
  while (true)
  {
    const auto callback = *reinterpret_cast<const AnyCallback*>(normal_entry);
    const u8* payload = normal_entry + sizeof(callback);
    // Direct dispatch to the most commonly used callbacks for better performance
    if (callback == AnyCallbackCast(Interpret<false>)) [[likely]]
    {
      Interpret<false>(ppc_state, *reinterpret_cast<const InterpretOperands*>(payload));
      normal_entry = payload + sizeof(InterpretOperands);
    }
    else if (callback == AnyCallbackCast(Interpret<true>))
    {
      Interpret<true>(ppc_state, *reinterpret_cast<const InterpretOperands*>(payload));
      normal_entry = payload + sizeof(InterpretOperands);
    }
    else if (callback == AnyCallbackCast(ExecuteStencil<false>))
    {
      ExecuteStencil<false>(ppc_state, *reinterpret_cast<const StencilOperands*>(payload));
      normal_entry = payload + sizeof(StencilOperands);
    }
    else if (callback == AnyCallbackCast(ExecuteStencil<true>))
    {
      ExecuteStencil<true>(ppc_state, *reinterpret_cast<const StencilOperands*>(payload));
      normal_entry = payload + sizeof(StencilOperands);
    }
    else
    {
      if (const auto distance = callback(ppc_state, payload))
        normal_entry += distance;
      else
        break;
    }
  }
}

void StencilInterpreter::Run()
{
  auto& core_timing = m_system.GetCoreTiming();

  const CPU::State* state_ptr = m_system.GetCPU().GetStatePtr();
  while (*state_ptr == CPU::State::Running)
  {
    // Start new timing slice
    // NOTE: Exceptions may change PC
    core_timing.Advance();

    do
    {
      ExecuteOneBlock();
    } while (m_ppc_state.downcount > 0 && *state_ptr == CPU::State::Running);
  }
}

void StencilInterpreter::SingleStep()
{
  // Enter new timing slice
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
  // Direct native stencil invocation when running on ARM64 host
  // Tape layout matching gen_stencils ABI:
  // tape[0] = 0 (entry)
  // tape[1] = operand_arg (imm32 / second op)
  // tape[2] = return address / terminator
  alignas(16) static const uint32_t kRetStub[2] = {0xd65f03c0, 0xd65f03c0}; // RET, RET
  alignas(16) uint64_t tape[4] = {0, operands.operand_arg, reinterpret_cast<uint64_t>(kRetStub), 0};

  alignas(16) VMState vm_state{};
  // Load pinned registers into VMState
  for (int i = 0; i < 5; ++i)
  {
    const int arm_reg = PinnedToARM64Reg(i);
    vm_state.regs[arm_reg] = ppc_state.gpr[kPinnedRegs[i]];
  }
  vm_state.regs[20] = reinterpret_cast<uint64_t>(tape);
  vm_state.regs[28] = reinterpret_cast<uint64_t>(ppc_state.mem_ptr);

  using StencilFunc = void (*)(VMState*);
  reinterpret_cast<StencilFunc>(const_cast<uint8_t*>(operands.stencil->bytes))(&vm_state);

  // Sync pinned registers back to ppc_state
  for (int i = 0; i < 5; ++i)
  {
    const int arm_reg = PinnedToARM64Reg(i);
    ppc_state.gpr[kPinnedRegs[i]] = static_cast<u32>(vm_state.regs[arm_reg]);
  }
#else
  // Non-ARM64 fallback: execute simple ALU/logic ops directly using the operand information
  if (operands.ra_gpr < 32 && operands.rd_gpr < 32)
  {
    u32 ra_val = (operands.ra_gpr == 0) ? 0 : ppc_state.gpr[operands.ra_gpr];
    ppc_state.gpr[operands.rd_gpr] = ra_val + operands.operand_arg;
  }
#endif

  return sizeof(AnyCallback) + sizeof(operands);
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

    if (HandleFunctionHooking(js.compilerPC))
      break;

    if (!op.skip)
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

      // Check if this PPC instruction can be matched to an A642A64 stencil
      bool emitted_stencil = false;
      const u32 primary_op = op.inst.OPCD;

      int rd_idx = GetPinnedIndex(op.inst.RD);
      int ra_idx = GetPinnedIndex(op.inst.RA);
      int rb_idx = GetPinnedIndex(op.inst.RB);

      if (primary_op == 14 && rd_idx != -1 && ra_idx != -1)  // addi
      {
        const StencilEntry* s = lookup_stencil(14, 0, false, rd_idx, ra_idx, 0);
        if (s != nullptr && s->bytes != nullptr && s->length > 0)
        {
          const StencilOperands stencil_ops = {
              s,
              js.compilerPC,
              static_cast<u32>(static_cast<s32>(op.inst.SIMM_16)),
              op.inst.RD,
              op.inst.RA,
              0};
          Write(op.canEndBlock ? CallbackCast(ExecuteStencil<true>) :
                                 CallbackCast(ExecuteStencil<false>),
                stencil_ops);
          emitted_stencil = true;
        }
      }
      else if (primary_op == 15 && rd_idx != -1 && ra_idx != -1)  // addis
      {
        const StencilEntry* s = lookup_stencil(15, 0, false, rd_idx, ra_idx, 0);
        if (s != nullptr && s->bytes != nullptr && s->length > 0)
        {
          const StencilOperands stencil_ops = {
              s,
              js.compilerPC,
              static_cast<u32>(static_cast<s32>(op.inst.SIMM_16) << 16),
              op.inst.RD,
              op.inst.RA,
              0};
          Write(op.canEndBlock ? CallbackCast(ExecuteStencil<true>) :
                                 CallbackCast(ExecuteStencil<false>),
                stencil_ops);
          emitted_stencil = true;
        }
      }
      else if (primary_op == 31 && op.inst.SUBOP10 == 266 && rd_idx != -1 && ra_idx != -1 && rb_idx != -1) // add
      {
        const StencilEntry* s = lookup_stencil(31, 266, op.inst.Rc != 0, rd_idx, ra_idx, rb_idx);
        if (s != nullptr && s->bytes != nullptr && s->length > 0)
        {
          const StencilOperands stencil_ops = {
              s, js.compilerPC, 0, op.inst.RD, op.inst.RA, op.inst.RB};
          Write(op.canEndBlock ? CallbackCast(ExecuteStencil<true>) :
                                 CallbackCast(ExecuteStencil<false>),
                stencil_ops);
          emitted_stencil = true;
        }
      }
      else if (primary_op == 31 && op.inst.SUBOP10 == 40 && rd_idx != -1 && ra_idx != -1 && rb_idx != -1) // subf
      {
        const StencilEntry* s = lookup_stencil(31, 40, op.inst.Rc != 0, rd_idx, ra_idx, rb_idx);
        if (s != nullptr && s->bytes != nullptr && s->length > 0)
        {
          const StencilOperands stencil_ops = {
              s, js.compilerPC, 0, op.inst.RD, op.inst.RA, op.inst.RB};
          Write(op.canEndBlock ? CallbackCast(ExecuteStencil<true>) :
                                 CallbackCast(ExecuteStencil<false>),
                stencil_ops);
          emitted_stencil = true;
        }
      }

      if (!emitted_stencil)
      {
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
      }

      if (op.branchIsIdleLoop)
        Write(CheckIdle, {m_system.GetCoreTiming(), js.blockStart});
      if (op.canEndBlock)
        WriteEndBlock();
    }
  }
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
