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

uint32_t Stencil_SlowReadU8(void* s, uint32_t a) { return 0; }
uint32_t Stencil_SlowReadU16(void* s, uint32_t a) { return 0; }
uint32_t Stencil_SlowReadU32(void* s, uint32_t a) { return 0; }

void Stencil_SlowWriteU8(void* s, uint32_t a, uint32_t v) {}
void Stencil_SlowWriteU16(void* s, uint32_t a, uint32_t v) {}
void Stencil_SlowWriteU32(void* s, uint32_t a, uint32_t v) {}
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
  g_code_buffer = static_cast<u8*>(mmap(nullptr, 64 * 1024, PROT_READ | PROT_WRITE | PROT_EXEC,
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

static int g_divergence_count = 0;

static void CheckDivergence(const char* name, const TestResult& asm_res, const TestResult& ref_res,
                            u32 in_a, u32 in_b, uint64_t packed)
{
  bool match = (asm_res.gpr_rd == ref_res.gpr_rd) &&
               (asm_res.xer_ca == ref_res.xer_ca);
  if (!match)
  {
    g_divergence_count++;
    printf("[-] DIVERGENCE in %s!\n", name);
    printf("    Input A=0x%08x, B=0x%08x (packed=0x%016lx)\n", in_a, in_b, packed);
    printf("    Assembly:   RD=0x%08x, CA=%d, CR0=0x%016lx\n", asm_res.gpr_rd, asm_res.xer_ca, asm_res.cr0);
    printf("    Reference:  RD=0x%08x, CA=%d, CR0=0x%016lx\n", ref_res.gpr_rd, ref_res.xer_ca, ref_res.cr0);
  }
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
  void* mem_base = mmap(nullptr, 0x100000000ULL, PROT_READ | PROT_WRITE,
                        MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
  if (mem_base == MAP_FAILED)
  {
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

  printf("\n====================================================\n");
  if (g_divergence_count == 0)
  {
    printf("[+] ALL DIVERGENCE TESTS PASSED! Zero discrepancies.\n");
  }
  else
  {
    printf("[-] FAILURE: Caught %d divergence(s) between assembly and reference!\n", g_divergence_count);
  }
  printf("====================================================\n");

  munmap(mem_base, 0x100000000ULL);
  return (g_divergence_count > 0) ? 1 : 0;
}
