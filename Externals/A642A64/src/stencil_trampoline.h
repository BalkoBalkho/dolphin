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
}
#else
inline void a642a64_stencil_exit() {}
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
#endif
