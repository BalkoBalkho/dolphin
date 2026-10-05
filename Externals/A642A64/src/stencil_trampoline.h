#pragma once
#include <cstdint>

#if defined(__aarch64__) || defined(_M_ARM64)
extern "C" {
void a642a64_stencil_exit();
uint32_t a642a64_run_stencil(
    const void* stencil_bytes,
    void* tape,
    void* ppc_state,
    void* mem_base,
    int* downcount_ptr
);

void stencil_dyn_addi();
void stencil_dyn_addis();
void stencil_dyn_add();
void stencil_dyn_subf();
void stencil_dyn_ori();
void stencil_dyn_oris();
void stencil_dyn_xori();
void stencil_dyn_xoris();
void stencil_dyn_andi();
void stencil_dyn_andis();
void stencil_dyn_or();
void stencil_dyn_and();
void stencil_dyn_xor();
void stencil_dyn_nor();
void stencil_dyn_slw();
void stencil_dyn_srw();
void stencil_dyn_sraw();
void stencil_dyn_srawi();
void stencil_dyn_rlwinm();
void stencil_dyn_mr();
void stencil_dyn_neg();
void stencil_dyn_extsb();
void stencil_dyn_extsh();
void stencil_dyn_cntlzw();
void stencil_dyn_lwz();
void stencil_dyn_stw();
void stencil_dyn_lbz();
void stencil_dyn_stb();
void stencil_dyn_lhz();
void stencil_dyn_sth();
void stencil_dyn_cmplwi();
void stencil_dyn_cmpwi();
void stencil_dyn_cmpl();
void stencil_dyn_cmp();
void stencil_tape_jump();
void a642a64_stencil_exit_nodec();
void stencil_tape_direct_jump();
void stencil_tape_cond_branch();
void stencil_dyn_super_lwz_lwz();
void stencil_dyn_super_stw_stw();
void stencil_dyn_super_lis_addi();
void stencil_dyn_super_lis_ori();
void stencil_dyn_super_mr_mr();
void stencil_dyn_mflr();
void stencil_dyn_mtlr();
void stencil_dyn_stwu();
void stencil_dyn_mulli();
void stencil_dyn_mullw();
void stencil_dyn_lwzx();
void stencil_dyn_stwx();
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

// Paired-Single SIMD stencils (OP4)
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

// Single-Precision Floating Point stencils (OP59)
void stencil_dyn_fadds();
void stencil_dyn_fsubs();
void stencil_dyn_fmuls();
void stencil_dyn_fdivs();
void stencil_dyn_fmadds();
void stencil_dyn_fmsubs();

// Double-Precision Floating Point stencils (OP63)
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

// Additional Integer stencils (OP31)
void stencil_dyn_andc();
void stencil_dyn_orc();
void stencil_dyn_nand();
void stencil_dyn_eqv();
void stencil_dyn_divw();
void stencil_dyn_divwu();
void stencil_dyn_mulhw();
void stencil_dyn_mulhwu();

// Vertex Super-Instruction Stencils (Item 3)
void stencil_dyn_super_ps_mul_ps_add();
void stencil_dyn_super_ps_madd_ps_add();
}
#else
inline void a642a64_stencil_exit() {}
inline void a642a64_stencil_exit_nodec() {}
inline uint32_t a642a64_run_stencil(const void*, void*, void*, void*, int*) { return 0; }
inline void stencil_dyn_addi() {}
inline void stencil_dyn_addis() {}
inline void stencil_dyn_add() {}
inline void stencil_dyn_subf() {}
inline void stencil_dyn_ori() {}
inline void stencil_dyn_oris() {}
inline void stencil_dyn_xori() {}
inline void stencil_dyn_xoris() {}
inline void stencil_dyn_andi() {}
inline void stencil_dyn_andis() {}
inline void stencil_dyn_or() {}
inline void stencil_dyn_and() {}
inline void stencil_dyn_xor() {}
inline void stencil_dyn_nor() {}
inline void stencil_dyn_slw() {}
inline void stencil_dyn_srw() {}
inline void stencil_dyn_sraw() {}
inline void stencil_dyn_srawi() {}
inline void stencil_dyn_rlwinm() {}
inline void stencil_dyn_mr() {}
inline void stencil_dyn_neg() {}
inline void stencil_dyn_extsb() {}
inline void stencil_dyn_extsh() {}
inline void stencil_dyn_cntlzw() {}
inline void stencil_dyn_lwz() {}
inline void stencil_dyn_stw() {}
inline void stencil_dyn_lbz() {}
inline void stencil_dyn_stb() {}
inline void stencil_dyn_lhz() {}
inline void stencil_dyn_sth() {}
inline void stencil_dyn_cmplwi() {}
inline void stencil_dyn_cmpwi() {}
inline void stencil_dyn_cmpl() {}
inline void stencil_dyn_cmp() {}
inline void stencil_tape_jump() {}
inline void stencil_tape_direct_jump() {}
inline void stencil_tape_cond_branch() {}
inline void stencil_dyn_super_lwz_lwz() {}
inline void stencil_dyn_super_stw_stw() {}
inline void stencil_dyn_super_mr_mr() {}
inline void stencil_dyn_mflr() {}
inline void stencil_dyn_mtlr() {}
inline void stencil_dyn_stwu() {}
inline void stencil_dyn_mulli() {}
inline void stencil_dyn_mullw() {}
inline void stencil_dyn_lwzx() {}
inline void stencil_dyn_stwx() {}
inline void stencil_dyn_lfs() {}
inline void stencil_dyn_stfs() {}
inline void stencil_dyn_lfd() {}
inline void stencil_dyn_stfd() {}
inline void stencil_dyn_fmr() {}
inline void stencil_dyn_rlwimi() {}
inline void stencil_dyn_lbzu() {}
inline void stencil_dyn_mtctr() {}
inline void stencil_dyn_mfctr() {}
inline void stencil_dyn_mfmsr() {}
inline void stencil_dyn_dcbi() {}
inline void stencil_dyn_ps_add() {}
inline void stencil_dyn_ps_sub() {}
inline void stencil_dyn_ps_mul() {}
inline void stencil_dyn_ps_div() {}
inline void stencil_dyn_ps_madd() {}
inline void stencil_dyn_ps_msub() {}
inline void stencil_dyn_ps_neg() {}
inline void stencil_dyn_ps_abs() {}
inline void stencil_dyn_ps_nabs() {}
inline void stencil_dyn_ps_merge00() {}
inline void stencil_dyn_ps_merge01() {}
inline void stencil_dyn_ps_merge10() {}
inline void stencil_dyn_ps_merge11() {}
inline void stencil_dyn_fadds() {}
inline void stencil_dyn_fsubs() {}
inline void stencil_dyn_fmuls() {}
inline void stencil_dyn_fdivs() {}
inline void stencil_dyn_fmadds() {}
inline void stencil_dyn_fmsubs() {}
inline void stencil_dyn_fadd() {}
inline void stencil_dyn_fsub() {}
inline void stencil_dyn_fmul() {}
inline void stencil_dyn_fdiv() {}
inline void stencil_dyn_fmadd() {}
inline void stencil_dyn_fmsub() {}
inline void stencil_dyn_fabs() {}
inline void stencil_dyn_fneg() {}
inline void stencil_dyn_fnabs() {}
inline void stencil_dyn_fsel() {}
inline void stencil_dyn_andc() {}
inline void stencil_dyn_orc() {}
inline void stencil_dyn_nand() {}
inline void stencil_dyn_eqv() {}
inline void stencil_dyn_divw() {}
inline void stencil_dyn_divwu() {}
inline void stencil_dyn_mulhw() {}
inline void stencil_dyn_mulhwu() {}
inline void stencil_dyn_super_ps_mul_ps_add() {}
inline void stencil_dyn_super_ps_madd_ps_add() {}
#endif
