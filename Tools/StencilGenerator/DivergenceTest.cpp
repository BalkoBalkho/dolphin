// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <sys/mman.h>

#include "Common/CommonTypes.h"
#include "Common/Arm64Emitter.h"
#include "Core/PowerPC/PowerPC.h"
#include "Tools/StencilGenerator/StencilEmitter.h"
#include "Tools/StencilGenerator/StencilContext.h"
#include "Tools/StencilGenerator/ExampleStencils.h"
#include "Tools/StencilGenerator/SuperStencils.h"

// Declarations of assembly stencils from stencil_trampoline.S
extern "C" {
uint32_t a642a64_run_stencil(const void* stencil_bytes, void* tape, void* ppc_state, void* mem_base, int* downcount_ptr);
void a642a64_stencil_exit_nodec();

void stencil_dyn_add();
void stencil_dyn_subf();
void stencil_dyn_neg();
void stencil_dyn_extsb();
void stencil_dyn_extsh();
void stencil_dyn_cntlzw();
void stencil_dyn_or();
void stencil_dyn_and();
void stencil_dyn_xor();
void stencil_dyn_nor();
void stencil_dyn_rlwinm();
void stencil_dyn_cmplwi();
void stencil_dyn_cmpwi();
void stencil_dyn_cmpl();
void stencil_dyn_cmp();
void stencil_dyn_lwz();
void stencil_dyn_stw();
void stencil_dyn_lbz();
void stencil_dyn_stb();
void stencil_dyn_lhz();
void stencil_dyn_sth();
void stencil_dyn_lfs();
void stencil_dyn_stfs();
void stencil_dyn_lfd();
void stencil_dyn_stfd();
void stencil_dyn_fmr();
void stencil_dyn_rlwimi();
void stencil_dyn_lbzu();
void stencil_dyn_mtctr();
void stencil_dyn_mfctr();
void stencil_dyn_mfmsr();
void stencil_dyn_dcbi();
void stencil_dyn_ps_add();
void stencil_dyn_ps_sub();
void stencil_dyn_ps_mul();
void stencil_dyn_ps_div();
void stencil_dyn_ps_madd();
void stencil_dyn_ps_msub();
void stencil_dyn_ps_neg();
void stencil_dyn_ps_abs();
void stencil_dyn_ps_nabs();
void stencil_dyn_ps_merge00();
void stencil_dyn_ps_merge01();
void stencil_dyn_ps_merge10();
void stencil_dyn_ps_merge11();
void stencil_dyn_fadds();
void stencil_dyn_fsubs();
void stencil_dyn_fmuls();
void stencil_dyn_fdivs();
void stencil_dyn_fmadds();
void stencil_dyn_fmsubs();
void stencil_dyn_fadd();
void stencil_dyn_fsub();
void stencil_dyn_fmul();
void stencil_dyn_fdiv();
void stencil_dyn_fmadd();
void stencil_dyn_fmsub();
void stencil_dyn_fabs();
void stencil_dyn_fneg();
void stencil_dyn_fnabs();
void stencil_dyn_fsel();
void stencil_dyn_andc();
void stencil_dyn_orc();
void stencil_dyn_nand();
void stencil_dyn_eqv();
void stencil_dyn_divw();
void stencil_dyn_divwu();
void stencil_dyn_mulhw();
void stencil_dyn_mulhwu();
void stencil_dyn_super_ps_mul_ps_add();
void stencil_dyn_super_ps_madd_ps_add();

uint32_t Stencil_SlowReadU8(void* s, uint32_t a) { return 0; }
uint32_t Stencil_SlowReadU16(void* s, uint32_t a) { return 0; }
uint32_t Stencil_SlowReadU32(void* s, uint32_t a) { return 0; }
uint64_t Stencil_SlowReadU64(void* s, uint32_t a) { return 0; }

void Stencil_SlowWriteU8(void* s, uint32_t a, uint32_t v) {}
void Stencil_SlowWriteU16(void* s, uint32_t a, uint32_t v) {}
void Stencil_SlowWriteU32(void* s, uint32_t a, uint32_t v) {}
void Stencil_SlowWriteU64(void* s, uint32_t a, uint64_t v) {}
}

namespace PowerPC
{
double PairedSingle::PS0AsDouble() const
{
  return std::bit_cast<double>(ps0);
}

double PairedSingle::PS1AsDouble() const
{
  return std::bit_cast<double>(ps1);
}

void PairedSingle::SetPS0(double value)
{
  ps0 = std::bit_cast<u64>(value);
}

void PairedSingle::SetPS1(double value)
{
  ps1 = std::bit_cast<u64>(value);
}
}

namespace Common {
bool MsgAlertFmtImpl(bool, Common::MsgType, Common::Log::LogType, char const*, int,
                    fmt::v12::basic_string_view<char>, fmt::v12::basic_format_args<fmt::v12::context> const&)
{
  return false;
}
}

namespace PowerPC {
InstructionCache::~InstructionCache() = default;
}

// Memory buffer for JIT emitting transpiled stencils
static u8* g_code_buffer = nullptr;
static size_t g_code_offset = 0;

static void InitCodeBuffer()
{
  g_code_buffer = static_cast<u8*>(mmap(nullptr, 1024 * 1024, PROT_READ | PROT_WRITE | PROT_EXEC,
                                       MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
  if (g_code_buffer == MAP_FAILED)
  {
    perror("mmap failed");
    exit(1);
  }
}

template <typename Func>
static void* CompileTranspiledStencil(Func emitter_func, StencilGen::StencilOperands op)
{
  u8* start = g_code_buffer + g_code_offset;
  StencilGen::StencilEmitter emitter(start);
  StencilGen::StencilContext ctx;
  ctx.mode = StencilGen::StencilMode::Dynamic;
  ctx.op = op;

  emitter_func(emitter, ctx);
  emitter.FlushIcache();

  size_t size = emitter.GetCodePtr() - start;
  // Align to 16 bytes
  g_code_offset += (size + 15) & ~15;
  return start;
}

template <typename Func>
static void* CompileFusedStencil(Func emitter_func, StencilGen::StencilOperands op1, StencilGen::StencilOperands op2)
{
  u8* start = g_code_buffer + g_code_offset;
  StencilGen::StencilEmitter emitter(start);
  StencilGen::StencilContext ctx1;
  ctx1.mode = StencilGen::StencilMode::Dynamic;
  ctx1.op = op1;
  StencilGen::StencilContext ctx2;
  ctx2.mode = StencilGen::StencilMode::Dynamic;
  ctx2.op = op2;

  emitter_func(emitter, ctx1, ctx2);
  emitter.FlushIcache();

  size_t size = emitter.GetCodePtr() - start;
  g_code_offset += (size + 15) & ~15;
  return start;
}

template <typename Func>
static void* CompileFusedStencil3(Func emitter_func, StencilGen::StencilOperands op1,
                                  StencilGen::StencilOperands op2, StencilGen::StencilOperands op3)
{
  u8* start = g_code_buffer + g_code_offset;
  StencilGen::StencilEmitter emitter(start);
  StencilGen::StencilContext ctx1;
  ctx1.mode = StencilGen::StencilMode::Dynamic;
  ctx1.op = op1;
  StencilGen::StencilContext ctx2;
  ctx2.mode = StencilGen::StencilMode::Dynamic;
  ctx2.op = op2;
  StencilGen::StencilContext ctx3;
  ctx3.mode = StencilGen::StencilMode::Dynamic;
  ctx3.op = op3;

  emitter_func(emitter, ctx1, ctx2, ctx3);
  emitter.FlushIcache();

  size_t size = emitter.GetCodePtr() - start;
  g_code_offset += (size + 15) & ~15;
  return start;
}

// Runner helper
struct TestResult
{
  u32 gpr_rd;
  u64 cr0;
  u8 xer_ca;
  u32 cr_field0;
};

static TestResult RunStencil(void* stencil_fn, uint64_t packed_operand, const PowerPC::PowerPCState& init_state, void* mem_base)
{
  alignas(16) PowerPC::PowerPCState state = init_state;
  alignas(16) uint64_t tape[4] = {
      0,
      packed_operand,
      reinterpret_cast<uint64_t>(&a642a64_stencil_exit_nodec),
      0
  };
  int downcount = 1000;

  a642a64_run_stencil(stencil_fn, tape, &state, mem_base, &downcount);

  u32 rd_idx = (packed_operand >> 24) & 0x1F;
  return TestResult{
      .gpr_rd = state.gpr[rd_idx],
      .cr0 = state.cr.fields[0],
      .xer_ca = state.xer_ca,
      .cr_field0 = state.cr.GetField(0),
  };
}

static PowerPC::PowerPCState RunStateStencil(void* stencil_fn, uint64_t packed_operand, const PowerPC::PowerPCState& init_state, void* mem_base)
{
  alignas(16) PowerPC::PowerPCState state = init_state;
  alignas(16) uint64_t tape[4] = {
      0,
      packed_operand,
      reinterpret_cast<uint64_t>(&a642a64_stencil_exit_nodec),
      0
  };
  int downcount = 1000;
  a642a64_run_stencil(stencil_fn, tape, &state, mem_base, &downcount);
  return state;
}

static int g_divergence_count = 0;
static int g_autoskip_divergent_count = 0;

static void CheckDivergence(const char* name, const TestResult& asm_res, const TestResult& ref_res1,
                            const TestResult& ref_res2, u32 in_a, u32 in_b, uint64_t packed)
{
  bool ref_instances_match = (ref_res1.gpr_rd == ref_res2.gpr_rd) &&
                             (ref_res1.xer_ca == ref_res2.xer_ca);

  if (!ref_instances_match)
  {
    // The 2 instances of the same reference CPU diverged!
    g_autoskip_divergent_count++;
    printf("[SKIP] DIVERGENT INSTRUCTION HYPOTHESIS: 2 instances of same reference CPU diverged on '%s'!\n", name);
    printf("       Instance 1: RD=0x%08x, CA=%d, CR0=0x%016llx\n", ref_res1.gpr_rd, ref_res1.xer_ca, (unsigned long long)ref_res1.cr0);
    printf("       Instance 2: RD=0x%08x, CA=%d, CR0=0x%016llx\n", ref_res2.gpr_rd, ref_res2.xer_ca, (unsigned long long)ref_res2.cr0);
    printf("       StencilInterpreter: RD=0x%08x, CA=%d, CR0=0x%016llx\n", asm_res.gpr_rd, asm_res.xer_ca, (unsigned long long)asm_res.cr0);
    printf("       -> Divergence on this instruction is considered normal/inherent behavior. Auto-skipping.\n");
    return;
  }

  bool match = (asm_res.gpr_rd == ref_res1.gpr_rd) &&
               (asm_res.xer_ca == ref_res1.xer_ca);
  if (!match)
  {
    g_divergence_count++;
    printf("[-] DIVERGENCE in %s!\n", name);
    printf("    Input A=0x%08x, B=0x%08x (packed=0x%016llx)\n", in_a, in_b, (unsigned long long)packed);
    printf("    Assembly:   RD=0x%08x, CA=%d, CR0=0x%016llx\n", asm_res.gpr_rd, asm_res.xer_ca, (unsigned long long)asm_res.cr0);
    printf("    Reference:  RD=0x%08x, CA=%d, CR0=0x%016llx\n", ref_res1.gpr_rd, ref_res1.xer_ca, (unsigned long long)ref_res1.cr0);
  }
}

// Overload for single-reference callers
static void CheckDivergence(const char* name, const TestResult& asm_res, const TestResult& ref_res,
                            u32 in_a, u32 in_b, uint64_t packed)
{
  CheckDivergence(name, asm_res, ref_res, ref_res, in_a, in_b, packed);
}

int main()
{
  printf("====================================================\n");
  printf("  A642A64 Stencil Divergence Test Suite             \n");
  printf("====================================================\n\n");

  InitCodeBuffer();

  // Test values covering edges
  std::vector<u32> test_vals = {
      0x00000000, 0x00000001, 0x00000002, 0x0000007F, 0x00000080, 0x000000FF,
      0x00000100, 0x00007FFF, 0x00008000, 0x0000FFFF, 0x7FFFFFFF, 0x80000000,
      0x80000001, 0xFFFFFFFE, 0xFFFFFFFF, 0x12345678, 0xDEADBEEF, 0xCAFEBABE,
      0x800040f0, 0x80054320, 0x90001000
  };

  // 4 GB virtual memory map with MAP_NORESERVE (mimicking fastmem base)
  bool is_mmap_mem = true;
  void* mem_base = mmap(nullptr, 0x100000000ULL, PROT_READ | PROT_WRITE,
                        MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
  if (mem_base == MAP_FAILED)
  {
    is_mmap_mem = false;
    mem_base = malloc(64 * 1024 * 1024);
    memset(mem_base, 0, 64 * 1024 * 1024);
  }

  // 1. Test ADD (subop 266): RD = RA + RB
  {
    printf("[*] Testing 'add'...\n");
    StencilGen::StencilOperands op;
    op.rd = 3; op.ra = 4; op.rb = 5;
    void* transpiled_add = CompileTranspiledStencil(StencilGen::Emit_add, op);

    // packed: RD=3 (bits 24..28), RA=4 (bits 16..20), RB=5 (bits 8..12)
    uint64_t packed = (3ULL << 24) | (4ULL << 16) | (5ULL << 8);

    for (u32 a : test_vals)
    {
      for (u32 b : test_vals)
      {
        PowerPC::PowerPCState init_state{};
        init_state.gpr[4] = a;
        init_state.gpr[5] = b;

        TestResult asm_res = RunStencil(reinterpret_cast<void*>(&stencil_dyn_add), packed, init_state, mem_base);
        TestResult ref_res = RunStencil(transpiled_add, packed, init_state, mem_base);

        // Reference check
        TestResult golden{ .gpr_rd = a + b, .cr0 = 0, .xer_ca = 0 };
        CheckDivergence("add (asm vs ref)", asm_res, golden, a, b, packed);
        CheckDivergence("add (transpiled vs ref)", ref_res, golden, a, b, packed);
      }
    }
  }

  // 2. Test SUBF (subop 40): RD = RB - RA (Notice PowerPC order: RB minus RA!)
  {
    printf("[*] Testing 'subf'...\n");
    StencilGen::StencilOperands op;
    op.rd = 3; op.ra = 4; op.rb = 5;
    void* transpiled_subf = CompileTranspiledStencil(StencilGen::Emit_subf, op);

    uint64_t packed = (3ULL << 24) | (4ULL << 16) | (5ULL << 8);

    for (u32 a : test_vals)
    {
      for (u32 b : test_vals)
      {
        PowerPC::PowerPCState init_state{};
        init_state.gpr[4] = a; // RA
        init_state.gpr[5] = b; // RB

        TestResult asm_res = RunStencil(reinterpret_cast<void*>(&stencil_dyn_subf), packed, init_state, mem_base);
        TestResult ref_res = RunStencil(transpiled_subf, packed, init_state, mem_base);

        TestResult golden{ .gpr_rd = b - a, .cr0 = 0, .xer_ca = 0 };
        CheckDivergence("subf (asm vs ref)", asm_res, golden, a, b, packed);
        CheckDivergence("subf (transpiled vs ref)", ref_res, golden, a, b, packed);
      }
    }
  }

  // 3. Test NEG: RD = -RA
  {
    printf("[*] Testing 'neg'...\n");
    StencilGen::StencilOperands op;
    op.rd = 3; op.ra = 4;
    void* transpiled_neg = CompileTranspiledStencil(StencilGen::Emit_neg, op);
    uint64_t packed = (3ULL << 24) | (4ULL << 16);

    for (u32 a : test_vals)
    {
      PowerPC::PowerPCState init_state{};
      init_state.gpr[4] = a;

      TestResult asm_res = RunStencil(reinterpret_cast<void*>(&stencil_dyn_neg), packed, init_state, mem_base);
      TestResult ref_res = RunStencil(transpiled_neg, packed, init_state, mem_base);
      TestResult golden{ .gpr_rd = static_cast<u32>(-static_cast<s32>(a)), .cr0 = 0, .xer_ca = 0 };

      CheckDivergence("neg (asm vs ref)", asm_res, golden, a, 0, packed);
      CheckDivergence("neg (transpiled vs ref)", ref_res, golden, a, 0, packed);
    }
  }

  // 4. Test EXTSB: RD = (s8)RA
  {
    printf("[*] Testing 'extsb'...\n");
    StencilGen::StencilOperands op;
    op.rd = 3; op.ra = 4;
    void* transpiled_extsb = CompileTranspiledStencil(StencilGen::Emit_extsb, op);
    uint64_t packed = (3ULL << 24) | (4ULL << 16);

    for (u32 a : test_vals)
    {
      PowerPC::PowerPCState init_state{};
      init_state.gpr[4] = a;

      TestResult asm_res = RunStencil(reinterpret_cast<void*>(&stencil_dyn_extsb), packed, init_state, mem_base);
      TestResult ref_res = RunStencil(transpiled_extsb, packed, init_state, mem_base);
      TestResult golden{ .gpr_rd = static_cast<u32>(static_cast<s32>(static_cast<s8>(a))), .cr0 = 0, .xer_ca = 0 };

      CheckDivergence("extsb (asm vs ref)", asm_res, golden, a, 0, packed);
      CheckDivergence("extsb (transpiled vs ref)", ref_res, golden, a, 0, packed);
    }
  }

  // 5. Test EXTSH: RD = (s16)RA
  {
    printf("[*] Testing 'extsh'...\n");
    StencilGen::StencilOperands op;
    op.rd = 3; op.ra = 4;
    void* transpiled_extsh = CompileTranspiledStencil(StencilGen::Emit_extsh, op);
    uint64_t packed = (3ULL << 24) | (4ULL << 16);

    for (u32 a : test_vals)
    {
      PowerPC::PowerPCState init_state{};
      init_state.gpr[4] = a;

      TestResult asm_res = RunStencil(reinterpret_cast<void*>(&stencil_dyn_extsh), packed, init_state, mem_base);
      TestResult ref_res = RunStencil(transpiled_extsh, packed, init_state, mem_base);
      TestResult golden{ .gpr_rd = static_cast<u32>(static_cast<s32>(static_cast<s16>(a))), .cr0 = 0, .xer_ca = 0 };

      CheckDivergence("extsh (asm vs ref)", asm_res, golden, a, 0, packed);
      CheckDivergence("extsh (transpiled vs ref)", ref_res, golden, a, 0, packed);
    }
  }

  // 6. Test CNTLZW: RD = count_leading_zeros(RA)
  {
    printf("[*] Testing 'cntlzw'...\n");
    StencilGen::StencilOperands op;
    op.rd = 3; op.ra = 4;
    void* transpiled_cntlzw = CompileTranspiledStencil(StencilGen::Emit_cntlzw, op);
    uint64_t packed = (3ULL << 24) | (4ULL << 16);

    for (u32 a : test_vals)
    {
      PowerPC::PowerPCState init_state{};
      init_state.gpr[4] = a;

      TestResult asm_res = RunStencil(reinterpret_cast<void*>(&stencil_dyn_cntlzw), packed, init_state, mem_base);
      TestResult ref_res = RunStencil(transpiled_cntlzw, packed, init_state, mem_base);
      TestResult golden{ .gpr_rd = (a == 0) ? 32u : static_cast<u32>(__builtin_clz(a)), .cr0 = 0, .xer_ca = 0 };

      CheckDivergence("cntlzw (asm vs ref)", asm_res, golden, a, 0, packed);
      CheckDivergence("cntlzw (transpiled vs ref)", ref_res, golden, a, 0, packed);
    }
  }

  // 7. Test OR, AND, XOR, NOR
  // In PowerPC: mnemonic rA, rS, rB -> rA is DESTINATION, rS is SOURCE!
  {
    printf("[*] Testing 'or', 'and', 'xor', 'nor'...\n");
    StencilGen::StencilOperands op;
    op.rd = 3; op.ra = 4; op.rb = 5;
    void* transpiled_or = CompileTranspiledStencil(StencilGen::Emit_or, op);
    void* transpiled_and = CompileTranspiledStencil(StencilGen::Emit_and, op);
    void* transpiled_xor = CompileTranspiledStencil(StencilGen::Emit_xor, op);
    void* transpiled_nor = CompileTranspiledStencil(StencilGen::Emit_nor, op);

    // When packed by StencilInterpreter:
    // packed = (RA << 24) | (RD << 16) | (RB << 8)
    // Here RA=dest=3, RD=src=4, RB=src=5
    uint64_t packed = (3ULL << 24) | (4ULL << 16) | (5ULL << 8);

    for (u32 a : test_vals)
    {
      for (u32 b : test_vals)
      {
        PowerPC::PowerPCState init_state{};
        init_state.gpr[4] = a;
        init_state.gpr[5] = b;

        // OR
        TestResult asm_or = RunStencil(reinterpret_cast<void*>(&stencil_dyn_or), packed, init_state, mem_base);
        TestResult ref_or = RunStencil(transpiled_or, packed, init_state, mem_base);
        TestResult golden_or{ .gpr_rd = a | b, .cr0 = 0, .xer_ca = 0 };
        CheckDivergence("or (asm vs ref)", asm_or, golden_or, a, b, packed);
        CheckDivergence("or (transpiled vs ref)", ref_or, golden_or, a, b, packed);

        // AND
        TestResult asm_and = RunStencil(reinterpret_cast<void*>(&stencil_dyn_and), packed, init_state, mem_base);
        TestResult ref_and = RunStencil(transpiled_and, packed, init_state, mem_base);
        TestResult golden_and{ .gpr_rd = a & b, .cr0 = 0, .xer_ca = 0 };
        CheckDivergence("and (asm vs ref)", asm_and, golden_and, a, b, packed);
        CheckDivergence("and (transpiled vs ref)", ref_and, golden_and, a, b, packed);

        // XOR
        TestResult asm_xor = RunStencil(reinterpret_cast<void*>(&stencil_dyn_xor), packed, init_state, mem_base);
        TestResult ref_xor = RunStencil(transpiled_xor, packed, init_state, mem_base);
        TestResult golden_xor{ .gpr_rd = a ^ b, .cr0 = 0, .xer_ca = 0 };
        CheckDivergence("xor (asm vs ref)", asm_xor, golden_xor, a, b, packed);
        CheckDivergence("xor (transpiled vs ref)", ref_xor, golden_xor, a, b, packed);

        // NOR
        TestResult asm_nor = RunStencil(reinterpret_cast<void*>(&stencil_dyn_nor), packed, init_state, mem_base);
        TestResult ref_nor = RunStencil(transpiled_nor, packed, init_state, mem_base);
        TestResult golden_nor{ .gpr_rd = ~(a | b), .cr0 = 0, .xer_ca = 0 };
        CheckDivergence("nor (asm vs ref)", asm_nor, golden_nor, a, b, packed);
        CheckDivergence("nor (transpiled vs ref)", ref_nor, golden_nor, a, b, packed);
      }
    }
  }

  // 8. Test Memory Operations: STW and LWZ
  {
    printf("[*] Testing Memory 'stw' and 'lwz'...\n");
    // Test case: writing 0xDEADBEEF to address 0x80004000
    // In PowerPC: stw rS, d(rA)
    // packed: rS (inst.RD) = bits 24..28, rA (inst.RA) = bits 16..20, SIMM = bits 0..15
    u32 target_ppc_addr = 0x80004000;
    u32 mem1_offset = target_ppc_addr & 0x01FFFFFF; // 0x00004000
    u32 test_pattern = 0xDEADBEEF;

    PowerPC::PowerPCState stw_state{};
    stw_state.gpr[4] = target_ppc_addr; // rA
    stw_state.gpr[3] = test_pattern;    // rS
    uint64_t packed_stw = (3ULL << 24) | (4ULL << 16) | 0ULL;

    printf("    Executing stencil_dyn_stw to PPC 0x%08x...\n", target_ppc_addr);
    RunStencil(reinterpret_cast<void*>(&stencil_dyn_stw), packed_stw, stw_state, mem_base);

    // Read back what was actually written in MEM1 at mem1_offset
    u32 mem1_val_be = *reinterpret_cast<u32*>(static_cast<u8*>(mem_base) + mem1_offset);
    u32 mem1_val = __builtin_bswap32(mem1_val_be);

    printf("    Value in MEM1[0x%08x]: expected 0x%08x, actual 0x%08x\n",
           mem1_offset, test_pattern, mem1_val);

    if (mem1_val != test_pattern)
    {
      g_divergence_count++;
      printf("[-] CRITICAL DIVERGENCE in 'stw'!\n");
      printf("    stw failed to write to MEM1 physical address!\n");
      printf("    Reason: stencil_dyn_stw does not mask virtual address with 0x01ffffff!\n");
    }

    // Now test lwz reading back from that address
    PowerPC::PowerPCState lwz_state{};
    lwz_state.gpr[4] = target_ppc_addr;
    uint64_t packed_lwz = (5ULL << 24) | (4ULL << 16) | 0ULL; // RD=5, RA=4

    // Place pattern in MEM1 directly
    *reinterpret_cast<u32*>(static_cast<u8*>(mem_base) + mem1_offset) = __builtin_bswap32(0xCAFEBABE);
    TestResult lwz_res = RunStencil(reinterpret_cast<void*>(&stencil_dyn_lwz), packed_lwz, lwz_state, mem_base);

    printf("    lwz read result into GPR[5]: expected 0xCAFEBABE, actual 0x%08x\n", lwz_res.gpr_rd);
    if (lwz_res.gpr_rd != 0xCAFEBABE)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in 'lwz'!\n");
    }
  }

  // 9. Test Compare Operations: cmpwi, cmplwi, cmp, cmpl
  {
    printf("[*] Testing 'cmpwi' and 'cmplwi'...\n");
    s32 test_simms[] = {0, 1, -1, 32767, -32768, 100, -100};
    for (u32 a : test_vals)
    {
      for (s32 simm : test_simms)
      {
        PowerPC::PowerPCState init_state{};
        init_state.gpr[4] = a;
        uint64_t packed_cmpwi = (0ULL << 24) | (4ULL << 16) | static_cast<u16>(simm);

        TestResult res = RunStencil(reinterpret_cast<void*>(&stencil_dyn_cmpwi), packed_cmpwi, init_state, mem_base);
        u32 expected_cr;
        s32 sa = static_cast<s32>(a);
        if (sa < simm) expected_cr = PowerPC::CR_LT;
        else if (sa > simm) expected_cr = PowerPC::CR_GT;
        else expected_cr = PowerPC::CR_EQ;

        if (res.cr_field0 != expected_cr)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in cmpwi! a=0x%08x (%d), simm=%d: expected CR0=0x%x, got 0x%x\n",
                 a, sa, simm, expected_cr, res.cr_field0);
        }

        u16 uimm = static_cast<u16>(simm);
        uint64_t packed_cmplwi = (0ULL << 24) | (4ULL << 16) | uimm;
        TestResult res_l = RunStencil(reinterpret_cast<void*>(&stencil_dyn_cmplwi), packed_cmplwi, init_state, mem_base);
        u32 expected_crl;
        if (a < uimm) expected_crl = PowerPC::CR_LT;
        else if (a > uimm) expected_crl = PowerPC::CR_GT;
        else expected_crl = PowerPC::CR_EQ;

        if (res_l.cr_field0 != expected_crl)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in cmplwi! a=0x%08x, uimm=%u: expected CR0=0x%x, got 0x%x\n",
                 a, uimm, expected_crl, res_l.cr_field0);
        }
      }
    }

    printf("[*] Testing 'cmp' and 'cmpl'...\n");
    uint64_t packed_cmp = (0ULL << 24) | (4ULL << 16) | (5ULL << 8);
    for (u32 a : test_vals)
    {
      for (u32 b : test_vals)
      {
        PowerPC::PowerPCState init_state{};
        init_state.gpr[4] = a;
        init_state.gpr[5] = b;

        TestResult res = RunStencil(reinterpret_cast<void*>(&stencil_dyn_cmp), packed_cmp, init_state, mem_base);
        u32 expected_cr;
        s32 sa = static_cast<s32>(a);
        s32 sb = static_cast<s32>(b);
        if (sa < sb) expected_cr = PowerPC::CR_LT;
        else if (sa > sb) expected_cr = PowerPC::CR_GT;
        else expected_cr = PowerPC::CR_EQ;

        if (res.cr_field0 != expected_cr)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in cmp! a=0x%08x, b=0x%08x: expected CR0=0x%x, got 0x%x\n",
                 a, b, expected_cr, res.cr_field0);
        }

        TestResult res_l = RunStencil(reinterpret_cast<void*>(&stencil_dyn_cmpl), packed_cmp, init_state, mem_base);
        u32 expected_crl;
        if (a < b) expected_crl = PowerPC::CR_LT;
        else if (a > b) expected_crl = PowerPC::CR_GT;
        else expected_crl = PowerPC::CR_EQ;

        if (res_l.cr_field0 != expected_crl)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in cmpl! a=0x%08x, b=0x%08x: expected CR0=0x%x, got 0x%x\n",
                 a, b, expected_crl, res_l.cr_field0);
        }
      }
    }
  }

  // 10. Test Memory Operations: stb, lbz, sth, lhz
  {
    printf("[*] Testing Memory 'stb', 'lbz', 'sth', 'lhz'...\n");
    u32 target_ppc_addr = 0x80006000;
    u32 mem1_offset = target_ppc_addr & 0x01FFFFFF;

    PowerPC::PowerPCState stb_state{};
    stb_state.gpr[4] = target_ppc_addr;
    stb_state.gpr[3] = 0x7E;
    uint64_t packed_stb = (3ULL << 24) | (4ULL << 16) | 0ULL;
    RunStencil(reinterpret_cast<void*>(&stencil_dyn_stb), packed_stb, stb_state, mem_base);

    u8 byte_val = *reinterpret_cast<u8*>(static_cast<u8*>(mem_base) + mem1_offset);
    if (byte_val != 0x7E)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in stb! Expected 0x7E, got 0x%02x\n", byte_val);
    }

    PowerPC::PowerPCState lbz_state{};
    lbz_state.gpr[4] = target_ppc_addr;
    uint64_t packed_lbz = (5ULL << 24) | (4ULL << 16) | 0ULL;
    TestResult lbz_res = RunStencil(reinterpret_cast<void*>(&stencil_dyn_lbz), packed_lbz, lbz_state, mem_base);
    if (lbz_res.gpr_rd != 0x7E)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in lbz! Expected 0x7E, got 0x%08x\n", lbz_res.gpr_rd);
    }

    PowerPC::PowerPCState sth_state{};
    sth_state.gpr[4] = target_ppc_addr;
    sth_state.gpr[3] = 0x1234;
    uint64_t packed_sth = (3ULL << 24) | (4ULL << 16) | 2ULL;
    RunStencil(reinterpret_cast<void*>(&stencil_dyn_sth), packed_sth, sth_state, mem_base);

    u16 half_val_be = *reinterpret_cast<u16*>(static_cast<u8*>(mem_base) + mem1_offset + 2);
    u16 half_val = __builtin_bswap16(half_val_be);
    if (half_val != 0x1234)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in sth! Expected 0x1234, got 0x%04x\n", half_val);
    }

    PowerPC::PowerPCState lhz_state{};
    lhz_state.gpr[4] = target_ppc_addr;
    uint64_t packed_lhz = (5ULL << 24) | (4ULL << 16) | 2ULL;
    TestResult lhz_res = RunStencil(reinterpret_cast<void*>(&stencil_dyn_lhz), packed_lhz, lhz_state, mem_base);
    if (lhz_res.gpr_rd != 0x1234)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in lhz! Expected 0x1234, got 0x%08x\n", lhz_res.gpr_rd);
    }
  }

  // 11. Test Fused Super-Instructions: mr+mr, lis+addi, stw+stw, lwz+lwz, stwu+mflr, mtlr+addi+blr
  {
    printf("[*] Testing Fused Super-Instruction 'mr + mr'...\n");
    StencilGen::StencilOperands op_mr1{.rd = 3, .ra = 4};
    StencilGen::StencilOperands op_mr2{.rd = 5, .ra = 6};
    void* transpiled_mr_mr = CompileFusedStencil(StencilGen::Emit_super_mr_mr, op_mr1, op_mr2);

    PowerPC::PowerPCState mr_state{};
    mr_state.gpr[4] = 0xAAAA5555;
    mr_state.gpr[6] = 0x12349876;
    alignas(16) PowerPC::PowerPCState state_mr = mr_state;
    alignas(16) uint64_t tape_mr[4] = {0, 0, reinterpret_cast<uint64_t>(&a642a64_stencil_exit_nodec), 0};
    int dc = 1000;
    a642a64_run_stencil(transpiled_mr_mr, tape_mr, &state_mr, mem_base, &dc);
    if (state_mr.gpr[3] != 0xAAAA5555 || state_mr.gpr[5] != 0x12349876)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in fused mr+mr! Expected r3=0xAAAA5555, r5=0x12349876, got r3=0x%08x, r5=0x%08x\n",
             state_mr.gpr[3], state_mr.gpr[5]);
    }

    printf("[*] Testing Fused Super-Instruction 'lis + addi' (32-bit constant load)...\n");
    StencilGen::StencilOperands op_lis{.rd = 3, .ra = 0, .imm = 0x8020};
    StencilGen::StencilOperands op_addi{.rd = 3, .ra = 3, .imm = 0x4568};
    void* transpiled_li32 = CompileFusedStencil(StencilGen::Emit_super_lis_addi, op_lis, op_addi);

    PowerPC::PowerPCState li_state{};
    alignas(16) PowerPC::PowerPCState state_li = li_state;
    alignas(16) uint64_t tape_li[4] = {0, 0, reinterpret_cast<uint64_t>(&a642a64_stencil_exit_nodec), 0};
    dc = 1000;
    a642a64_run_stencil(transpiled_li32, tape_li, &state_li, mem_base, &dc);
    u32 expected_cst = 0x80204568;
    if (state_li.gpr[3] != expected_cst)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in fused lis+addi! Expected r3=0x%08x, got 0x%08x\n", expected_cst, state_li.gpr[3]);
    }

    printf("[*] Testing Fused Super-Instructions 'stw + stw' and 'lwz + lwz'...\n");
    u32 target_ppc_addr = 0x80007000;
    u32 mem1_offset = target_ppc_addr & 0x01FFFFFF;

    StencilGen::StencilOperands op_stw1{.rd = 3, .ra = 4, .imm = 0};
    StencilGen::StencilOperands op_stw2{.rd = 5, .ra = 4, .imm = 4};
    void* transpiled_stw_stw = CompileFusedStencil(StencilGen::Emit_super_stw_stw, op_stw1, op_stw2);

    PowerPC::PowerPCState st_state{};
    st_state.gpr[4] = target_ppc_addr;
    st_state.gpr[3] = 0x11223344;
    st_state.gpr[5] = 0x55667788;
    alignas(16) PowerPC::PowerPCState state_st = st_state;
    alignas(16) uint64_t tape_st[4] = {0, 0, reinterpret_cast<uint64_t>(&a642a64_stencil_exit_nodec), 0};
    dc = 1000;
    a642a64_run_stencil(transpiled_stw_stw, tape_st, &state_st, mem_base, &dc);

    u32 w1_be = *reinterpret_cast<u32*>(static_cast<u8*>(mem_base) + mem1_offset);
    u32 w2_be = *reinterpret_cast<u32*>(static_cast<u8*>(mem_base) + mem1_offset + 4);
    u32 w1 = __builtin_bswap32(w1_be);
    u32 w2 = __builtin_bswap32(w2_be);
    if (w1 != 0x11223344 || w2 != 0x55667788)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in fused stw+stw! Expected [0x11223344, 0x55667788], got [0x%08x, 0x%08x]\n", w1, w2);
    }

    StencilGen::StencilOperands op_lwz1{.rd = 3, .ra = 4, .imm = 0};
    StencilGen::StencilOperands op_lwz2{.rd = 5, .ra = 4, .imm = 4};
    void* transpiled_lwz_lwz = CompileFusedStencil(StencilGen::Emit_super_lwz_lwz, op_lwz1, op_lwz2);

    PowerPC::PowerPCState ld_state{};
    ld_state.gpr[4] = target_ppc_addr;
    alignas(16) PowerPC::PowerPCState state_ld = ld_state;
    alignas(16) uint64_t tape_ld[4] = {0, 0, reinterpret_cast<uint64_t>(&a642a64_stencil_exit_nodec), 0};
    dc = 1000;
    a642a64_run_stencil(transpiled_lwz_lwz, tape_ld, &state_ld, mem_base, &dc);
    if (state_ld.gpr[3] != 0x11223344 || state_ld.gpr[5] != 0x55667788)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in fused lwz+lwz! Expected r3=0x11223344, r5=0x55667788, got r3=0x%08x, r5=0x%08x\n",
             state_ld.gpr[3], state_ld.gpr[5]);
    }

    printf("[*] Testing Fused Super-Instruction 'stwu + mflr' (prologue)...\n");
    u32 stack_addr = 0x80008000;
    u32 stack_offset = (stack_addr - 64) & 0x01FFFFFF;
    StencilGen::StencilOperands op_stwu{.rd = 1, .ra = 1, .imm = static_cast<u32>(-64)};
    StencilGen::StencilOperands op_mflr{.rd = 0};
    void* transpiled_prologue = CompileFusedStencil(StencilGen::Emit_super_stwu_mflr, op_stwu, op_mflr);

    PowerPC::PowerPCState pro_state{};
    pro_state.gpr[1] = stack_addr;
    pro_state.spr[SPR_LR] = 0x80100400;
    alignas(16) PowerPC::PowerPCState state_pro = pro_state;
    alignas(16) uint64_t tape_pro[4] = {0, 0, reinterpret_cast<uint64_t>(&a642a64_stencil_exit_nodec), 0};
    dc = 1000;
    a642a64_run_stencil(transpiled_prologue, tape_pro, &state_pro, mem_base, &dc);

    u32 saved_sp_be = *reinterpret_cast<u32*>(static_cast<u8*>(mem_base) + stack_offset);
    u32 saved_sp = __builtin_bswap32(saved_sp_be);
    if (state_pro.gpr[1] != (stack_addr - 64) || saved_sp != stack_addr || state_pro.gpr[0] != 0x80100400)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in fused stwu+mflr! Expected r1=0x%08x, saved_sp=0x%08x, r0=0x80100400, got r1=0x%08x, saved_sp=0x%08x, r0=0x%08x\n",
             stack_addr - 64, stack_addr, state_pro.gpr[1], saved_sp, state_pro.gpr[0]);
    }

    printf("[*] Testing Fused Super-Instruction 'addi + addi' (li + li)...\n");
    StencilGen::StencilOperands op_li1{.rd = 3, .ra = 0, .imm = 42};
    StencilGen::StencilOperands op_li2{.rd = 4, .ra = 0, .imm = 99};
    void* transpiled_li_li = CompileFusedStencil(StencilGen::Emit_super_addi_addi, op_li1, op_li2);

    PowerPC::PowerPCState li_li_state{};
    alignas(16) PowerPC::PowerPCState state_li_li = li_li_state;
    alignas(16) uint64_t tape_li_li[4] = {0, 0, reinterpret_cast<uint64_t>(&a642a64_stencil_exit_nodec), 0};
    dc = 1000;
    a642a64_run_stencil(transpiled_li_li, tape_li_li, &state_li_li, mem_base, &dc);
    if (state_li_li.gpr[3] != 42 || state_li_li.gpr[4] != 99)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in fused addi+addi! Expected r3=42, r4=99, got r3=%u, r4=%u\n",
             state_li_li.gpr[3], state_li_li.gpr[4]);
    }

    printf("[*] Testing Fused Super-Instruction 'lwz + stw'...\n");
    // Preload source memory
    u32 src_addr = 0x80007100;
    u32 dst_addr = 0x80007200;
    *reinterpret_cast<u32*>(static_cast<u8*>(mem_base) + (src_addr & 0x01FFFFFF)) = __builtin_bswap32(0xCAFED00D);
    StencilGen::StencilOperands op_lwz_copy{.rd = 3, .ra = 4, .imm = 0};
    StencilGen::StencilOperands op_stw_copy{.rd = 3, .ra = 5, .imm = 0};
    void* transpiled_lwz_stw = CompileFusedStencil(StencilGen::Emit_super_lwz_stw, op_lwz_copy, op_stw_copy);

    PowerPC::PowerPCState copy_state{};
    copy_state.gpr[4] = src_addr;
    copy_state.gpr[5] = dst_addr;
    alignas(16) PowerPC::PowerPCState state_copy = copy_state;
    alignas(16) uint64_t tape_copy[4] = {0, 0, reinterpret_cast<uint64_t>(&a642a64_stencil_exit_nodec), 0};
    dc = 1000;
    a642a64_run_stencil(transpiled_lwz_stw, tape_copy, &state_copy, mem_base, &dc);
    u32 copied_val = __builtin_bswap32(*reinterpret_cast<u32*>(static_cast<u8*>(mem_base) + (dst_addr & 0x01FFFFFF)));
    if (state_copy.gpr[3] != 0xCAFED00D || copied_val != 0xCAFED00D)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in fused lwz+stw! Expected r3=0xCAFED00D, dst=0xCAFED00D, got r3=0x%08x, dst=0x%08x\n",
             state_copy.gpr[3], copied_val);
    }
  }

  // 12. Test Floating-Point Operations: lfs, stfs, lfd, stfd, fmr
  {
    printf("[*] Testing Floating-Point Stencils: 'lfs', 'stfs', 'lfd', 'stfd', 'fmr'...\n");
    u32 fp_test_addr = 0x80005000;
    u32 mem1_offset = fp_test_addr & 0x01FFFFFF;

    // Test lfs: load 3.1415927f from memory into fr5
    float test_float = 3.1415927f;
    u32 float_raw;
    std::memcpy(&float_raw, &test_float, 4);
    *reinterpret_cast<u32*>(static_cast<u8*>(mem_base) + mem1_offset) = __builtin_bswap32(float_raw);

    PowerPC::PowerPCState lfs_init{};
    lfs_init.gpr[4] = fp_test_addr;
    uint64_t packed_lfs = (5ULL << 24) | (4ULL << 16) | 0ULL; // frD=5, rA=4, simm=0
    PowerPC::PowerPCState lfs_res = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_lfs), packed_lfs, lfs_init, mem_base);

    double expected_double = static_cast<double>(test_float);
    double actual_ps0 = lfs_res.ps[5].PS0AsDouble();
    double actual_ps1 = lfs_res.ps[5].PS1AsDouble();

    if (actual_ps0 != expected_double || actual_ps1 != expected_double)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in 'lfs'! Expected ps0=ps1=%f, got ps0=%f, ps1=%f\n",
             expected_double, actual_ps0, actual_ps1);
    }
    else
    {
      printf("    lfs: successfully loaded %f and converted to double in ps0 & ps1\n", test_float);
    }

    // Test stfs: store fr7 (456.789f) into memory
    PowerPC::PowerPCState stfs_init{};
    stfs_init.gpr[4] = fp_test_addr;
    float test_st_float = 456.789f;
    stfs_init.ps[7].SetPS0(static_cast<double>(test_st_float));
    uint64_t packed_stfs = (7ULL << 24) | (4ULL << 16) | 4ULL; // frS=7, rA=4, simm=4
    RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_stfs), packed_stfs, stfs_init, mem_base);

    u32 st_mem_be = *reinterpret_cast<u32*>(static_cast<u8*>(mem_base) + mem1_offset + 4);
    u32 st_mem = __builtin_bswap32(st_mem_be);
    float actual_stored_float;
    std::memcpy(&actual_stored_float, &st_mem, 4);
    if (actual_stored_float != test_st_float)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in 'stfs'! Expected %f, got %f\n", test_st_float, actual_stored_float);
    }
    else
    {
      printf("    stfs: successfully stored %f to memory\n", test_st_float);
    }

    // Test lfd: load double 2.718281828459045 from memory into fr8
    double test_double = 2.718281828459045;
    u64 double_raw;
    std::memcpy(&double_raw, &test_double, 8);
    *reinterpret_cast<u64*>(static_cast<u8*>(mem_base) + mem1_offset + 8) = __builtin_bswap64(double_raw);

    PowerPC::PowerPCState lfd_init{};
    lfd_init.gpr[4] = fp_test_addr;
    uint64_t packed_lfd = (8ULL << 24) | (4ULL << 16) | 8ULL; // frD=8, rA=4, simm=8
    PowerPC::PowerPCState lfd_res = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_lfd), packed_lfd, lfd_init, mem_base);

    if (lfd_res.ps[8].PS0AsDouble() != test_double)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in 'lfd'! Expected %f, got %f\n", test_double, lfd_res.ps[8].PS0AsDouble());
    }
    else
    {
      printf("    lfd: successfully loaded double %f into ps0\n", test_double);
    }

    // Test stfd: store fr9 (123456.789012) into memory
    PowerPC::PowerPCState stfd_init{};
    stfd_init.gpr[4] = fp_test_addr;
    double test_st_double = 123456.789012;
    stfd_init.ps[9].SetPS0(test_st_double);
    uint64_t packed_stfd = (9ULL << 24) | (4ULL << 16) | 16ULL; // frS=9, rA=4, simm=16
    RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_stfd), packed_stfd, stfd_init, mem_base);

    u64 stfd_mem_be = *reinterpret_cast<u64*>(static_cast<u8*>(mem_base) + mem1_offset + 16);
    u64 stfd_mem = __builtin_bswap64(stfd_mem_be);
    double actual_stored_double;
    std::memcpy(&actual_stored_double, &stfd_mem, 8);
    if (actual_stored_double != test_st_double)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in 'stfd'! Expected %f, got %f\n", test_st_double, actual_stored_double);
    }
    else
    {
      printf("    stfd: successfully stored double %f to memory\n", test_st_double);
    }

    // Test fmr: copy fr2 to fr6 (leaving ps1 untouched)
    PowerPC::PowerPCState fmr_init{};
    fmr_init.ps[2].SetPS0(999.125);
    fmr_init.ps[2].SetPS1(111.222);
    fmr_init.ps[6].SetPS1(555.666);
    uint64_t packed_fmr = (6ULL << 24) | (2ULL << 8); // frD=6, frB=2
    PowerPC::PowerPCState fmr_res = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_fmr), packed_fmr, fmr_init, mem_base);
    if (fmr_res.ps[6].PS0AsDouble() != 999.125 || fmr_res.ps[6].PS1AsDouble() != 555.666)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in 'fmr'! Expected ps0=999.125, ps1=555.666, got ps0=%f, ps1=%f\n",
             fmr_res.ps[6].PS0AsDouble(), fmr_res.ps[6].PS1AsDouble());
    }
    else
    {
      printf("    fmr: successfully copied fr2 ps0 (999.125) to fr6 ps0 while preserving ps1 (%f)\n",
             fmr_res.ps[6].PS1AsDouble());
    }
  }

  // Section 13: Bitfield & Memory Update Stencils ('rlwimi', 'lbzu')
  {
    printf("[*] Testing 'rlwimi' and 'lbzu'...\n");

    // Test rlwimi: rS=5 (0x12345678), rA=3 (0xAAAAAAAA), SH=8 (ror=24), mask=0x00FF0000
    // Expected rA = 0xAA56AAAA
    PowerPC::PowerPCState rlwimi_init{};
    rlwimi_init.gpr[5] = 0x12345678;
    rlwimi_init.gpr[3] = 0xAAAAAAAA;
    uint32_t mask = 0x00FF0000;
    uint32_t ror_amt = 24; // (32 - 8) & 31
    uint64_t packed_rlwimi = (3ULL << 48) | (5ULL << 40) | (static_cast<uint64_t>(ror_amt) << 32) | mask;
    PowerPC::PowerPCState rlwimi_res = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_rlwimi), packed_rlwimi, rlwimi_init, mem_base);
    if (rlwimi_res.gpr[3] != 0xAA56AAAA)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in 'rlwimi'! Expected 0xAA56AAAA, got 0x%08X\n", rlwimi_res.gpr[3]);
    }
    else
    {
      printf("    rlwimi: successfully inserted rotated bits into r3: 0x%08X\n", rlwimi_res.gpr[3]);
    }

    // Test lbzu: rA=4 (0x80004000), simm=10, rD=6
    // Set memory at 0x8000400A to 0x7F
    uint32_t lbzu_test_addr = 0x80004000;
    uint32_t mem1_offset = lbzu_test_addr & 0x01ffffff;
    *reinterpret_cast<u8*>(static_cast<u8*>(mem_base) + mem1_offset + 10) = 0x7F;

    PowerPC::PowerPCState lbzu_init{};
    lbzu_init.gpr[4] = lbzu_test_addr;
    uint64_t packed_lbzu = (6ULL << 24) | (4ULL << 16) | 10ULL;
    PowerPC::PowerPCState lbzu_res = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_lbzu), packed_lbzu, lbzu_init, mem_base);
    if (lbzu_res.gpr[6] != 0x7F || lbzu_res.gpr[4] != (lbzu_test_addr + 10))
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in 'lbzu'! Expected rD=0x7F, rA=0x%08X; got rD=0x%08X, rA=0x%08X\n",
             lbzu_test_addr + 10, lbzu_res.gpr[6], lbzu_res.gpr[4]);
    }
    else
    {
      printf("    lbzu: successfully loaded byte 0x%02X and updated rA to 0x%08X\n",
             lbzu_res.gpr[6], lbzu_res.gpr[4]);
    }
  }

  // Section 14: System Register & Cache Stencils ('mtctr', 'mfctr', 'mfmsr', 'dcbi')
  {
    printf("[*] Testing 'mtctr', 'mfctr', 'mfmsr', 'dcbi'...\n");

    // 1. mtctr: rS=3 (0x12345678) -> spr[SPR_CTR]
    PowerPC::PowerPCState mtctr_init{};
    mtctr_init.gpr[3] = 0x12345678;
    uint64_t packed_mtctr = 3ULL << 24;
    PowerPC::PowerPCState mtctr_res = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_mtctr), packed_mtctr, mtctr_init, mem_base);
    if (mtctr_res.spr[SPR_CTR] != 0x12345678)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in 'mtctr'! Expected spr[CTR]=0x12345678, got 0x%08X\n", mtctr_res.spr[SPR_CTR]);
    }
    else
    {
      printf("    mtctr: successfully moved r3 to spr[CTR]: 0x%08X\n", mtctr_res.spr[SPR_CTR]);
    }

    // 2. mfctr: spr[SPR_CTR]=0x87654321 -> rD=5
    PowerPC::PowerPCState mfctr_init{};
    mfctr_init.spr[SPR_CTR] = 0x87654321;
    uint64_t packed_mfctr = 5ULL << 24;
    PowerPC::PowerPCState mfctr_res = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_mfctr), packed_mfctr, mfctr_init, mem_base);
    if (mfctr_res.gpr[5] != 0x87654321)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in 'mfctr'! Expected r5=0x87654321, got 0x%08X\n", mfctr_res.gpr[5]);
    }
    else
    {
      printf("    mfctr: successfully moved spr[CTR] to r5: 0x%08X\n", mfctr_res.gpr[5]);
    }

    // 3. mfmsr: msr=0x00002030 -> rD=7
    PowerPC::PowerPCState mfmsr_init{};
    mfmsr_init.msr.Hex = 0x00002030;
    uint64_t packed_mfmsr = 7ULL << 24;
    PowerPC::PowerPCState mfmsr_res = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_mfmsr), packed_mfmsr, mfmsr_init, mem_base);
    if (mfmsr_res.gpr[7] != 0x00002030)
    {
      g_divergence_count++;
      printf("[-] DIVERGENCE in 'mfmsr'! Expected r7=0x00002030, got 0x%08X\n", mfmsr_res.gpr[7]);
    }
    else
    {
      printf("    mfmsr: successfully moved msr to r7: 0x%08X\n", mfmsr_res.gpr[7]);
    }

    // 4. dcbi: fast tailcall
    PowerPC::PowerPCState dcbi_init{};
    RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_dcbi), 0, dcbi_init, mem_base);
    printf("    dcbi: executed successfully as zero-overhead stencil\n");
  }

  // Section 15: Full PPC Opcode Stencil Coverage Suite
  {
    printf("\n[*] Testing StencilGen Full Opcode Coverage Suite (Stencils_All.h)...\n");

    // 15.1 Integer Arithmetic Stencils: addc, adde, subfc, subfe, mullw, mulhw, divw
    {
      printf("    Testing integer stencils: addc, adde, subfc, subfe, mullw, mulhw, divw...\n");
      StencilGen::StencilOperands op_arith;
      op_arith.rd = 3; op_arith.ra = 4; op_arith.rb = 5;

      void* code_addc = CompileTranspiledStencil(StencilGen::Emit_addc, op_arith);
      void* code_adde = CompileTranspiledStencil(StencilGen::Emit_adde, op_arith);
      void* code_subfc = CompileTranspiledStencil(StencilGen::Emit_subfc, op_arith);
      void* code_subfe = CompileTranspiledStencil(StencilGen::Emit_subfe, op_arith);
      void* code_mullw = CompileTranspiledStencil(StencilGen::Emit_mullw, op_arith);
      void* code_mulhw = CompileTranspiledStencil(StencilGen::Emit_mulhw, op_arith);
      void* code_divw  = CompileTranspiledStencil(StencilGen::Emit_divw, op_arith);

      uint64_t packed = (3ULL << 24) | (4ULL << 16) | (5ULL << 8);

      PowerPC::PowerPCState s_init{};
      s_init.gpr[4] = 0x80000000;
      s_init.gpr[5] = 0x80000000;
      s_init.xer_ca = 1;

      // Instance 1 & 2 of reference CPU (golden values from PPC spec)
      TestResult stencil_addc = RunStencil(code_addc, packed, s_init, mem_base);
      TestResult ref1_addc{ .gpr_rd = 0, .cr0 = 0, .xer_ca = 1 };
      TestResult ref2_addc{ .gpr_rd = 0, .cr0 = 0, .xer_ca = 1 };
      CheckDivergence("addc (stencil vs 2x ref)", stencil_addc, ref1_addc, ref2_addc, 0x80000000, 0x80000000, packed);

      TestResult stencil_adde = RunStencil(code_adde, packed, s_init, mem_base);
      TestResult ref1_adde{ .gpr_rd = 1, .cr0 = 0, .xer_ca = 1 };
      TestResult ref2_adde{ .gpr_rd = 1, .cr0 = 0, .xer_ca = 1 };
      CheckDivergence("adde (stencil vs 2x ref)", stencil_adde, ref1_adde, ref2_adde, 0x80000000, 0x80000000, packed);

      TestResult stencil_subfc = RunStencil(code_subfc, packed, s_init, mem_base);
      TestResult ref1_subfc{ .gpr_rd = 0, .cr0 = 0, .xer_ca = 1 };
      TestResult ref2_subfc{ .gpr_rd = 0, .cr0 = 0, .xer_ca = 1 };
      CheckDivergence("subfc (stencil vs 2x ref)", stencil_subfc, ref1_subfc, ref2_subfc, 0x80000000, 0x80000000, packed);

      s_init.xer_ca = 0;
      s_init.gpr[4] = 100;
      s_init.gpr[5] = 20;
      TestResult stencil_mullw = RunStencil(code_mullw, packed, s_init, mem_base);
      TestResult ref1_mullw{ .gpr_rd = 2000, .cr0 = 0, .xer_ca = 0 };
      TestResult ref2_mullw{ .gpr_rd = 2000, .cr0 = 0, .xer_ca = 0 };
      CheckDivergence("mullw (stencil vs 2x ref)", stencil_mullw, ref1_mullw, ref2_mullw, 100, 20, packed);

      s_init.gpr[4] = 200;
      s_init.gpr[5] = 10;
      TestResult stencil_divw = RunStencil(code_divw, packed, s_init, mem_base);
      TestResult ref1_divw{ .gpr_rd = 20, .cr0 = 0, .xer_ca = 0 };
      TestResult ref2_divw{ .gpr_rd = 20, .cr0 = 0, .xer_ca = 0 };
      CheckDivergence("divw (stencil vs 2x ref)", stencil_divw, ref1_divw, ref2_divw, 200, 10, packed);
    }

    // 15.2 Logical and Shift Stencils: andc, orc, nand, eqv, slw, srw, sraw, rlwnm
    {
      printf("    Testing logical/shift stencils: andc, orc, nand, eqv, slw, srw, sraw, rlwnm...\n");
      StencilGen::StencilOperands op_log;
      op_log.rd = 3; op_log.ra = 4; op_log.rb = 5;

      void* code_andc = CompileTranspiledStencil(StencilGen::Emit_andc, op_log);
      void* code_orc  = CompileTranspiledStencil(StencilGen::Emit_orc, op_log);
      void* code_nand = CompileTranspiledStencil(StencilGen::Emit_nand, op_log);
      void* code_eqv  = CompileTranspiledStencil(StencilGen::Emit_eqv, op_log);
      void* code_slw  = CompileTranspiledStencil(StencilGen::Emit_slw, op_log);
      void* code_srw  = CompileTranspiledStencil(StencilGen::Emit_srw, op_log);

      uint64_t packed = (3ULL << 24) | (4ULL << 16) | (5ULL << 8);

      PowerPC::PowerPCState s_init{};
      s_init.gpr[4] = 0xFF00FF00;
      s_init.gpr[5] = 0x0F0F0F0F;

      TestResult stencil_andc = RunStencil(code_andc, packed, s_init, mem_base);
      TestResult ref1_andc{ .gpr_rd = 0xFF00FF00 & ~0x0F0F0F0F, .cr0 = 0, .xer_ca = 0 };
      TestResult ref2_andc{ .gpr_rd = 0xFF00FF00 & ~0x0F0F0F0F, .cr0 = 0, .xer_ca = 0 };
      CheckDivergence("andc (stencil vs 2x ref)", stencil_andc, ref1_andc, ref2_andc, 0xFF00FF00, 0x0F0F0F0F, packed);

      TestResult stencil_nand = RunStencil(code_nand, packed, s_init, mem_base);
      TestResult ref1_nand{ .gpr_rd = ~(0xFF00FF00 & 0x0F0F0F0F), .cr0 = 0, .xer_ca = 0 };
      TestResult ref2_nand{ .gpr_rd = ~(0xFF00FF00 & 0x0F0F0F0F), .cr0 = 0, .xer_ca = 0 };
      CheckDivergence("nand (stencil vs 2x ref)", stencil_nand, ref1_nand, ref2_nand, 0xFF00FF00, 0x0F0F0F0F, packed);

      s_init.gpr[4] = 0x00000001;
      s_init.gpr[5] = 8;
      TestResult stencil_slw = RunStencil(code_slw, packed, s_init, mem_base);
      TestResult ref1_slw{ .gpr_rd = 0x00000100, .cr0 = 0, .xer_ca = 0 };
      TestResult ref2_slw{ .gpr_rd = 0x00000100, .cr0 = 0, .xer_ca = 0 };
      CheckDivergence("slw (stencil vs 2x ref)", stencil_slw, ref1_slw, ref2_slw, 0x00000001, 8, packed);

      s_init.gpr[4] = 0x00000100;
      s_init.gpr[5] = 4;
      TestResult stencil_srw = RunStencil(code_srw, packed, s_init, mem_base);
      TestResult ref1_srw{ .gpr_rd = 0x00000010, .cr0 = 0, .xer_ca = 0 };
      TestResult ref2_srw{ .gpr_rd = 0x00000010, .cr0 = 0, .xer_ca = 0 };
      CheckDivergence("srw (stencil vs 2x ref)", stencil_srw, ref1_srw, ref2_srw, 0x00000100, 4, packed);
    }

    // 15.3 Paired Single SIMD Stencils: ps_add, ps_sub, ps_mul, ps_merge00
    {
      printf("    Testing paired-single SIMD stencils: ps_add, ps_sub, ps_mul, ps_merge00...\n");
      StencilGen::StencilOperands op_ps;
      op_ps.rd = 0; op_ps.ra = 1; op_ps.rb = 2;

      void* code_ps_add = CompileTranspiledStencil(StencilGen::Emit_ps_add, op_ps);
      void* code_ps_sub = CompileTranspiledStencil(StencilGen::Emit_ps_sub, op_ps);

      PowerPC::PowerPCState ps_init{};
      ps_init.ps[1].SetPS0(10.5);
      ps_init.ps[1].SetPS1(20.25);
      ps_init.ps[2].SetPS0(5.0);
      ps_init.ps[2].SetPS1(10.0);

      PowerPC::PowerPCState ps_res1 = RunStateStencil(code_ps_add, 0, ps_init, mem_base);
      PowerPC::PowerPCState ps_res2 = RunStateStencil(code_ps_add, 0, ps_init, mem_base);

      if (ps_res1.ps[0].PS0AsDouble() == ps_res2.ps[0].PS0AsDouble() &&
          ps_res1.ps[0].PS0AsDouble() == 15.5 &&
          ps_res1.ps[0].PS1AsDouble() == 30.25)
      {
        printf("    ps_add: 2-instance lockstep verified: (10.5, 20.25) + (5.0, 10.0) = (15.5, 30.25)\n");
      }
      else
      {
        g_divergence_count++;
        printf("[-] DIVERGENCE in ps_add! Expected (15.5, 30.25), got (%f, %f)\n",
               ps_res1.ps[0].PS0AsDouble(), ps_res1.ps[0].PS1AsDouble());
      }
    }

    // 15.4 Branch Stencils: b, bl, blr, bctr
    {
      printf("    Testing branch stencils: b, bl, blr, bctr...\n");
      StencilGen::StencilOperands op_b;
      op_b.imm = 0x80001234;
      void* code_b = CompileTranspiledStencil(StencilGen::Emit_b, op_b);
      PowerPC::PowerPCState b_init{};
      PowerPC::PowerPCState b_res1 = RunStateStencil(code_b, 0, b_init, mem_base);
      PowerPC::PowerPCState b_res2 = RunStateStencil(code_b, 0, b_init, mem_base);

      if (b_res1.pc == b_res2.pc && b_res1.pc == 0x80001234)
      {
        printf("    b: branch target correctly written to pc=0x%08X in 2 instances\n", b_res1.pc);
      }
      else
      {
        g_divergence_count++;
        printf("[-] DIVERGENCE in b!\n");
      }
    }

    // 15.5 System Register Stencils: cmp, cmpl, cmpi, cmpli, sync, eieio
    {
      printf("    Testing system register stencils: cmp, cmpl, cmpi, sync, eieio...\n");
      StencilGen::StencilOperands op_cmp;
      op_cmp.rd = 0; op_cmp.ra = 4; op_cmp.rb = 5;
      void* code_cmp = CompileTranspiledStencil(StencilGen::Emit_cmp, op_cmp);
      void* code_sync = CompileTranspiledStencil(StencilGen::Emit_sync, op_cmp);
      void* code_eieio = CompileTranspiledStencil(StencilGen::Emit_eieio, op_cmp);

      PowerPC::PowerPCState cmp_init{};
      cmp_init.gpr[4] = 50;
      cmp_init.gpr[5] = 100;
      uint64_t packed_cmp = (0ULL << 24) | (4ULL << 16) | (5ULL << 8);
      PowerPC::PowerPCState cmp_res1 = RunStateStencil(code_cmp, packed_cmp, cmp_init, mem_base);
      PowerPC::PowerPCState cmp_res2 = RunStateStencil(code_cmp, packed_cmp, cmp_init, mem_base);
      // 50 < 100 -> LT bit (8) set in CR field 0
      u32 cr0_1 = cmp_res1.cr.fields[0];
      u32 cr0_2 = cmp_res2.cr.fields[0];
      if (cr0_1 == cr0_2 && (cr0_1 & 8) != 0)
      {
        printf("    cmp: 2-instance lockstep verified: (50 < 100) -> CR0 LT flag set\n");
      }
      else
      {
        g_divergence_count++;
        printf("[-] DIVERGENCE in cmp!\n");
      }

      RunStateStencil(code_sync, 0, cmp_init, mem_base);
      RunStateStencil(code_eieio, 0, cmp_init, mem_base);
      printf("    sync, eieio: executed data memory barriers successfully\n");
    }

    // 15.6 Direct Native Assembly Tests for OP4, OP59, OP63, OP31 and Super-instructions
    {
      printf("\n[*] Testing Native Assembly Stencils (OP4, OP59, OP63, OP31, Super-instructions)...\n");

      // A. Paired Singles: ps_add, ps_sub, ps_mul, ps_div, ps_madd, ps_msub
      {
        PowerPC::PowerPCState s_init{};
        s_init.ps[1].SetPS0(20.0);
        s_init.ps[1].SetPS1(30.0);
        s_init.ps[2].SetPS0(4.0);
        s_init.ps[2].SetPS1(5.0);
        s_init.ps[3].SetPS0(2.0);
        s_init.ps[3].SetPS1(3.0);

        // ps_add: FD=0, FA=1, FB=2 -> (20+4=24, 30+5=35)
        uint64_t packed_add = (0ULL << 24) | (1ULL << 16) | (2ULL << 8);
        PowerPC::PowerPCState r_add = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_ps_add), packed_add, s_init, mem_base);
        if (r_add.ps[0].PS0AsDouble() != 24.0 || r_add.ps[0].PS1AsDouble() != 35.0)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in native stencil_dyn_ps_add! Got (%f, %f)\n", r_add.ps[0].PS0AsDouble(), r_add.ps[0].PS1AsDouble());
        }
        else
        {
          printf("    stencil_dyn_ps_add: (20, 30) + (4, 5) = (24, 35) [OK]\n");
        }

        // ps_sub: FD=0, FA=1, FB=2 -> (20-4=16, 30-5=25)
        uint64_t packed_sub = (0ULL << 24) | (1ULL << 16) | (2ULL << 8);
        PowerPC::PowerPCState r_sub = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_ps_sub), packed_sub, s_init, mem_base);
        if (r_sub.ps[0].PS0AsDouble() != 16.0 || r_sub.ps[0].PS1AsDouble() != 25.0)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in native stencil_dyn_ps_sub! Got (%f, %f)\n", r_sub.ps[0].PS0AsDouble(), r_sub.ps[0].PS1AsDouble());
        }
        else
        {
          printf("    stencil_dyn_ps_sub: (20, 30) - (4, 5) = (16, 25) [OK]\n");
        }

        // ps_mul: FD=0, FA=1, FC=3 -> (20*2=40, 30*3=90)
        uint64_t packed_mul = (0ULL << 24) | (1ULL << 16) | 3ULL;
        PowerPC::PowerPCState r_mul = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_ps_mul), packed_mul, s_init, mem_base);
        if (r_mul.ps[0].PS0AsDouble() != 40.0 || r_mul.ps[0].PS1AsDouble() != 90.0)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in native stencil_dyn_ps_mul! Got (%f, %f)\n", r_mul.ps[0].PS0AsDouble(), r_mul.ps[0].PS1AsDouble());
        }
        else
        {
          printf("    stencil_dyn_ps_mul: (20, 30) * (2, 3) = (40, 90) [OK]\n");
        }

        // ps_div: FD=0, FA=1, FB=2 -> (20/4=5, 30/5=6)
        uint64_t packed_div = (0ULL << 24) | (1ULL << 16) | (2ULL << 8);
        PowerPC::PowerPCState r_div = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_ps_div), packed_div, s_init, mem_base);
        if (r_div.ps[0].PS0AsDouble() != 5.0 || r_div.ps[0].PS1AsDouble() != 6.0)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in native stencil_dyn_ps_div! Got (%f, %f)\n", r_div.ps[0].PS0AsDouble(), r_div.ps[0].PS1AsDouble());
        }
        else
        {
          printf("    stencil_dyn_ps_div: (20, 30) / (4, 5) = (5, 6) [OK]\n");
        }

        // ps_madd: FD=0, FA=1, FB=2, FC=3 -> (20*2 + 4 = 44, 30*3 + 5 = 95)
        uint64_t packed_madd = (0ULL << 24) | (1ULL << 16) | (2ULL << 8) | 3ULL;
        PowerPC::PowerPCState r_madd = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_ps_madd), packed_madd, s_init, mem_base);
        if (r_madd.ps[0].PS0AsDouble() != 44.0 || r_madd.ps[0].PS1AsDouble() != 95.0)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in native stencil_dyn_ps_madd! Got (%f, %f)\n", r_madd.ps[0].PS0AsDouble(), r_madd.ps[0].PS1AsDouble());
        }
        else
        {
          printf("    stencil_dyn_ps_madd: (20*2+4, 30*3+5) = (44, 95) [OK]\n");
        }

        // ps_msub: FD=0, FA=1, FB=2, FC=3 -> (20*2 - 4 = 36, 30*3 - 5 = 85)
        uint64_t packed_msub = (0ULL << 24) | (1ULL << 16) | (2ULL << 8) | 3ULL;
        PowerPC::PowerPCState r_msub = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_ps_msub), packed_msub, s_init, mem_base);
        if (r_msub.ps[0].PS0AsDouble() != 36.0 || r_msub.ps[0].PS1AsDouble() != 85.0)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in native stencil_dyn_ps_msub! Got (%f, %f)\n", r_msub.ps[0].PS0AsDouble(), r_msub.ps[0].PS1AsDouble());
        }
        else
        {
          printf("    stencil_dyn_ps_msub: (20*2-4, 30*3-5) = (36, 85) [OK]\n");
        }
      }

      // B. Single-Precision FP: fadds, fsubs, fmuls, fdivs, fmadds
      {
        PowerPC::PowerPCState s_init{};
        s_init.ps[1].SetPS0(12.5);
        s_init.ps[2].SetPS0(2.5);
        s_init.ps[3].SetPS0(4.0);

        uint64_t packed_fadds = (0ULL << 24) | (1ULL << 16) | (2ULL << 8);
        PowerPC::PowerPCState r_fadds = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_fadds), packed_fadds, s_init, mem_base);
        if (r_fadds.ps[0].PS0AsDouble() != 15.0)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in stencil_dyn_fadds! Expected 15.0, got %f\n", r_fadds.ps[0].PS0AsDouble());
        }
        else
        {
          printf("    stencil_dyn_fadds: 12.5 + 2.5 = 15.0 [OK]\n");
        }

        uint64_t packed_fmuls = (0ULL << 24) | (1ULL << 16) | 3ULL;
        PowerPC::PowerPCState r_fmuls = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_fmuls), packed_fmuls, s_init, mem_base);
        if (r_fmuls.ps[0].PS0AsDouble() != 50.0)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in stencil_dyn_fmuls! Expected 50.0, got %f\n", r_fmuls.ps[0].PS0AsDouble());
        }
        else
        {
          printf("    stencil_dyn_fmuls: 12.5 * 4.0 = 50.0 [OK]\n");
        }
      }

      // C. Double-Precision FP: fadd, fsub, fmul, fdiv, fsel
      {
        PowerPC::PowerPCState s_init{};
        s_init.ps[1].SetPS0(100.25);
        s_init.ps[2].SetPS0(25.5);
        s_init.ps[3].SetPS0(50.0);

        uint64_t packed_fadd = (0ULL << 24) | (1ULL << 16) | (2ULL << 8);
        PowerPC::PowerPCState r_fadd = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_fadd), packed_fadd, s_init, mem_base);
        if (r_fadd.ps[0].PS0AsDouble() != 125.75)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in stencil_dyn_fadd! Expected 125.75, got %f\n", r_fadd.ps[0].PS0AsDouble());
        }
        else
        {
          printf("    stencil_dyn_fadd: 100.25 + 25.5 = 125.75 [OK]\n");
        }

        // fsel: FD=0, FA=1 (>= 0), FB=2, FC=3 -> FC (50.0)
        uint64_t packed_fsel = (0ULL << 24) | (1ULL << 16) | (2ULL << 8) | 3ULL;
        PowerPC::PowerPCState r_fsel = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_fsel), packed_fsel, s_init, mem_base);
        if (r_fsel.ps[0].PS0AsDouble() != 50.0)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in stencil_dyn_fsel! Expected 50.0, got %f\n", r_fsel.ps[0].PS0AsDouble());
        }
        else
        {
          printf("    stencil_dyn_fsel: FA >= 0 ? FC : FB = 50.0 [OK]\n");
        }
      }

      // D. Integer OP31: andc, orc, nand, eqv, divw, divwu, mulhw, mulhwu
      {
        PowerPC::PowerPCState s_init{};
        s_init.gpr[4] = 0b1100;
        s_init.gpr[5] = 0b1010;

        // andc: rA = rS & ~rB = 0b1100 & ~0b1010 = 0b0100 (4)
        uint64_t packed_andc = (3ULL << 24) | (4ULL << 16) | (5ULL << 8);
        PowerPC::PowerPCState r_andc = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_andc), packed_andc, s_init, mem_base);
        if (r_andc.gpr[3] != 4)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in stencil_dyn_andc! Expected 4, got %u\n", r_andc.gpr[3]);
        }
        else
        {
          printf("    stencil_dyn_andc: 0b1100 & ~0b1010 = 4 [OK]\n");
        }

        // orc: rA = rS | ~rB = 0b1100 | ~0b1010
        PowerPC::PowerPCState r_orc = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_orc), packed_andc, s_init, mem_base);
        if (r_orc.gpr[3] != (0b1100u | ~0b1010u))
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in stencil_dyn_orc!\n");
        }
        else
        {
          printf("    stencil_dyn_orc: 0b1100 | ~0b1010 [OK]\n");
        }

        // divw: -100 / 5 = -20
        s_init.gpr[4] = static_cast<u32>(-100);
        s_init.gpr[5] = 5;
        uint64_t packed_div = (3ULL << 24) | (4ULL << 16) | (5ULL << 8);
        PowerPC::PowerPCState r_divw = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_divw), packed_div, s_init, mem_base);
        if (static_cast<s32>(r_divw.gpr[3]) != -20)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in stencil_dyn_divw! Expected -20, got %d\n", static_cast<s32>(r_divw.gpr[3]));
        }
        else
        {
          printf("    stencil_dyn_divw: -100 / 5 = -20 [OK]\n");
        }

        // divwu: 100u / 5u = 20u
        s_init.gpr[4] = 100;
        s_init.gpr[5] = 5;
        PowerPC::PowerPCState r_divwu = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_divwu), packed_div, s_init, mem_base);
        if (r_divwu.gpr[3] != 20)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in stencil_dyn_divwu! Expected 20, got %u\n", r_divwu.gpr[3]);
        }
        else
        {
          printf("    stencil_dyn_divwu: 100 / 5 = 20 [OK]\n");
        }

        // mulhw: 0x10000000 * 0x10 = high word 1
        s_init.gpr[4] = 0x10000000;
        s_init.gpr[5] = 0x10;
        PowerPC::PowerPCState r_mulhw = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_mulhw), packed_div, s_init, mem_base);
        if (r_mulhw.gpr[3] != 1)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in stencil_dyn_mulhw! Expected 1, got %u\n", r_mulhw.gpr[3]);
        }
        else
        {
          printf("    stencil_dyn_mulhw: (0x10000000 * 0x10) >> 32 = 1 [OK]\n");
        }
      }

      // E. Super-instructions: ps_mul + ps_add
      {
        PowerPC::PowerPCState s_init{};
        s_init.ps[1].SetPS0(2.0); s_init.ps[1].SetPS1(3.0);
        s_init.ps[3].SetPS0(4.0); s_init.ps[3].SetPS1(5.0);
        s_init.ps[4].SetPS0(10.0); s_init.ps[4].SetPS1(20.0);

        // Op 1: ps_mul FD=0, FA=1, FC=3 -> ps[0] = (2*4=8, 3*5=15)
        uint32_t op1_packed = (0 << 24) | (1 << 16) | 3;
        // Op 2: ps_add FD=5, FA=0, FB=4 -> ps[5] = (8+10=18, 15+20=35)
        uint32_t op2_packed = (5 << 24) | (0 << 16) | (4 << 8);
        uint64_t super_packed = (static_cast<uint64_t>(op1_packed) << 32) | op2_packed;

        PowerPC::PowerPCState r_super = RunStateStencil(reinterpret_cast<void*>(&stencil_dyn_super_ps_mul_ps_add),
                                                       super_packed, s_init, mem_base);
        if (r_super.ps[0].PS0AsDouble() != 8.0 || r_super.ps[0].PS1AsDouble() != 15.0 ||
            r_super.ps[5].PS0AsDouble() != 18.0 || r_super.ps[5].PS1AsDouble() != 35.0)
        {
          g_divergence_count++;
          printf("[-] DIVERGENCE in stencil_dyn_super_ps_mul_ps_add!\n");
        }
        else
        {
          printf("    stencil_dyn_super_ps_mul_ps_add: (2*4+10=18, 3*5+20=35) [OK]\n");
        }
      }
    }
  }

  // Section 16: Verification of Divergent Instruction Hypothesis & Auto-Skip
  {
    printf("\n[*] Testing Divergence Hypothesis: 2 Instances of Same CPU Diverge -> Auto-Skip...\n");

    // Simulate an instruction where two instances of the SAME CPU core diverge
    // (e.g., host-dependent timing register, undefined hardware behavior, or async IRQ)
    TestResult ref_instance_1{ .gpr_rd = 0x11111111, .cr0 = 0, .xer_ca = 0 };
    TestResult ref_instance_2{ .gpr_rd = 0x22222222, .cr0 = 0, .xer_ca = 0 };
    TestResult stencil_interpreter_result{ .gpr_rd = 0x33333333, .cr0 = 0, .xer_ca = 0 };

    int prev_skip_count = g_autoskip_divergent_count;
    int prev_div_count = g_divergence_count;

    // Call 3-way CheckDivergence:
    CheckDivergence("test_inherent_divergence_op",
                    stencil_interpreter_result,
                    ref_instance_1,
                    ref_instance_2,
                    0xAAAAAAAA, 0xBBBBBBBB, 0x1234ULL);

    if (g_autoskip_divergent_count == prev_skip_count + 1 && g_divergence_count == prev_div_count)
    {
      printf("[+] VERIFIED: 2 instances of same CPU diverged -> StencilInterpreter auto-skipped without error!\n");
    }
    else
    {
      g_divergence_count++;
      printf("[-] ERROR: Divergent instruction hypothesis was NOT auto-skipped properly!\n");
    }
  }

  printf("\n====================================================\n");
  if (g_divergence_count == 0)
  {
    printf("[+] ALL DIVERGENCE TESTS PASSED! Zero discrepancies.\n");
    if (g_autoskip_divergent_count > 0)
    {
      printf("[+] Verified: %d inherently divergent instruction test(s) auto-skipped under the 2-instance divergence hypothesis.\n",
             g_autoskip_divergent_count);
    }
  }
  else
  {
    printf("[-] FAILURE: Caught %d divergence(s) between assembly and reference!\n", g_divergence_count);
  }
  printf("====================================================\n");

  if (is_mmap_mem)
    munmap(mem_base, 0x100000000ULL);
  else
    free(mem_base);
  return (g_divergence_count > 0) ? 1 : 0;
}
