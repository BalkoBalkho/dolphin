#if defined(__aarch64__) || defined(_M_ARM64)
.text
.align 4

// extern "C" uint32_t a642a64_run_stencil(
//     const void* stencil_bytes, // x0
//     void* tape,                // x1
//     void* ppc_state,           // x2
//     void* mem_base,            // x3
//     int* downcount_ptr         // x4: in/out downcount pointer
// );

.global a642a64_stencil_exit_nodec
a642a64_stencil_exit_nodec:
    // Sync downcount in w26 back to memory
    ldr  x4, [sp, #56]
    cbz  x4, 1f
    str  w26, [x4]
1:
    // Restore callee-saved registers
    ldp  x19, x20, [sp, #16]
    ldp  x27, x28, [sp, #32]
    ldr  x26,      [sp, #48]
    ldp  x29, x30, [sp], #80
    ret

.global a642a64_stencil_exit
a642a64_stencil_exit:
    // 1. Calculate instructions executed from tape cursor advance: (x20 - tape_base) / 16
    ldr  x1, [sp, #64]
    sub  x0, x20, x1
    lsr  w0, w0, #4

    // 2. Decrement downcount in w26
    sub  w26, w26, w0
    b    a642a64_stencil_exit_nodec

.global a642a64_run_stencil
.type a642a64_run_stencil, %function
a642a64_run_stencil:
    // 1. Preserve only callee-saved registers modified by stencils: x19, x20, x26, x27, x28, x29, x30
    stp  x29, x30, [sp, #-80]!
    mov  x29, sp
    stp  x19, x20, [sp, #16]
    stp  x27, x28, [sp, #32]
    stp  x26, x4,  [sp, #48]   // x26 and downcount_ptr (x4)
    str  x1,       [sp, #64]   // save initial tape_base

    // 2. Load downcount into register w26
    cbz  x4, 2f
    ldr  w26, [x4]
2:
    // 3. Set environment registers
    mov  x19, x2         // ppc_state
    mov  x20, x1         // tape
    mov  x28, x3         // mem_base
    add  x27, x19, #32   // ppc_state.gpr base pointer for branchless access

    // 4. Branch to stencil
    ldr  x1, [x20, #8]
    br   x0

// -------------------------------------------------------------
// Dynamic Stencils for arbitrary registers (r0..r31)
// -------------------------------------------------------------

.macro READ_GPR dst, reg_idx
    ldr   \dst, [x27, \reg_idx, uxtw #2]
.endm

.macro WRITE_GPR src, reg_idx
    str   \src, [x27, \reg_idx, uxtw #2]
.endm

.macro STENCIL_TAILCALL
    b     __stencil_tailcall
.endm

.global __stencil_tailcall
.type __stencil_tailcall, %function
__stencil_tailcall:
    ldp   x16, x1, [x20, #16]!
    br    x16

.global stencil_dyn_addi
stencil_dyn_addi:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_addis
stencil_dyn_addis:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    lsl   w0, w0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_add
stencil_dyn_add:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    READ_GPR w5, w4
    add   w0, w0, w5
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_subf
stencil_dyn_subf:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    READ_GPR w5, w4
    sub   w0, w5, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_ori
stencil_dyn_ori:
    ldr   x1, [x20, #8]
    ubfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w4, w2
    orr   w0, w4, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_oris
stencil_dyn_oris:
    ldr   x1, [x20, #8]
    ubfx  w0, w1, #0, #16
    lsl   w0, w0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w4, w2
    orr   w0, w4, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_xori
stencil_dyn_xori:
    ldr   x1, [x20, #8]
    ubfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w4, w2
    eor   w0, w4, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_xoris
stencil_dyn_xoris:
    ldr   x1, [x20, #8]
    ubfx  w0, w1, #0, #16
    lsl   w0, w0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w4, w2
    eor   w0, w4, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_andi
stencil_dyn_andi:
    ldr   x1, [x20, #8]
    ubfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w4, w2
    and   w0, w4, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_andis
stencil_dyn_andis:
    ldr   x1, [x20, #8]
    ubfx  w0, w1, #0, #16
    lsl   w0, w0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w4, w2
    and   w0, w4, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_or
stencil_dyn_or:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    READ_GPR w5, w4
    orr   w0, w0, w5
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_and
stencil_dyn_and:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    READ_GPR w5, w4
    and   w0, w0, w5
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_xor
stencil_dyn_xor:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    READ_GPR w5, w4
    eor   w0, w0, w5
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_nor
stencil_dyn_nor:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    READ_GPR w5, w4
    orr   w0, w0, w5
    mvn   w0, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_slw
stencil_dyn_slw:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    READ_GPR w5, w4
    tbnz  w5, #5, 1f
    and   w5, w5, #31
    lsl   w0, w0, w5
    b     2f
1:  mov   w0, #0
2:  WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_srw
stencil_dyn_srw:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    READ_GPR w5, w4
    tbnz  w5, #5, 1f
    and   w5, w5, #31
    lsr   w0, w0, w5
    b     2f
1:  mov   w0, #0
2:  WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_sraw
stencil_dyn_sraw:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    READ_GPR w5, w4
    tbnz  w5, #5, 1f
    and   w5, w5, #31
    asr   w0, w0, w5
    b     2f
1:  asr   w0, w0, #31
2:  WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_srawi
stencil_dyn_srawi:
    ldr   x1, [x20, #8]
    ubfx  w5, w1, #0,  #5
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    asr   w0, w0, w5
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_rlwinm
stencil_dyn_rlwinm:
    ldr   x1, [x20, #8]
    ubfx  w0, w1, #0,  #32
    ubfx  x2, x1, #32, #5
    ubfx  x4, x1, #40, #5
    ubfx  x3, x1, #48, #5
    READ_GPR w5, w4
    ror   w5, w5, w2
    and   w0, w5, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_rlwimi
stencil_dyn_rlwimi:
    ldr   x1, [x20, #8]
    ubfx  w0, w1, #0,  #32
    ubfx  x2, x1, #32, #5
    ubfx  x4, x1, #40, #5
    ubfx  x3, x1, #48, #5
    READ_GPR w5, w4
    READ_GPR w6, w3
    ror   w5, w5, w2
    and   w5, w5, w0
    bic   w6, w6, w0
    orr   w0, w6, w5
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_mr
stencil_dyn_mr:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_neg
stencil_dyn_neg:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    neg   w0, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_extsb
stencil_dyn_extsb:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    sxtb  w0, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_extsh
stencil_dyn_extsh:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    sxth  w0, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_cntlzw
stencil_dyn_cntlzw:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w2
    clz   w0, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_lwz
stencil_dyn_lwz:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  tst   w0, #0x3e000000
    b.ne  2f
    and   w0, w0, #0x01ffffff
    ldr   w5, [x28, w0, uxtw]
    rev   w5, w5
    WRITE_GPR w5, w3
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowReadU32
    ldp   x3, x30, [sp], #16
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_stw
stencil_dyn_stw:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  tst   w0, #0x3e000000
    b.ne  2f
    and   w0, w0, #0x01ffffff
    READ_GPR w5, w3
    rev   w5, w5
    str   w5, [x28, w0, uxtw]
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    READ_GPR w2, w3
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowWriteU32
    ldp   x3, x30, [sp], #16
    STENCIL_TAILCALL

.global stencil_dyn_lbz
stencil_dyn_lbz:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  tst   w0, #0x3e000000
    b.ne  2f
    and   w0, w0, #0x01ffffff
    ldrb  w5, [x28, w0, uxtw]
    WRITE_GPR w5, w3
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowReadU8
    ldp   x3, x30, [sp], #16
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_lbzu
stencil_dyn_lbzu:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w4, w2
    add   w0, w4, w0
    WRITE_GPR w0, w2
    tst   w0, #0x3e000000
    b.ne  2f
    and   w4, w0, #0x01ffffff
    ldrb  w5, [x28, w4, uxtw]
    WRITE_GPR w5, w3
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowReadU8
    ldp   x3, x30, [sp], #16
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_stb
stencil_dyn_stb:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  tst   w0, #0x3e000000
    b.ne  2f
    and   w0, w0, #0x01ffffff
    READ_GPR w5, w3
    strb  w5, [x28, w0, uxtw]
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    READ_GPR w2, w3
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowWriteU8
    ldp   x3, x30, [sp], #16
    STENCIL_TAILCALL

.global stencil_dyn_lhz
stencil_dyn_lhz:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  tst   w0, #0x3e000000
    b.ne  2f
    and   w0, w0, #0x01ffffff
    ldrh  w5, [x28, w0, uxtw]
    rev16 w5, w5
    WRITE_GPR w5, w3
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowReadU16
    ldp   x3, x30, [sp], #16
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_sth
stencil_dyn_sth:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  tst   w0, #0x3e000000
    b.ne  2f
    and   w0, w0, #0x01ffffff
    READ_GPR w5, w3
    rev16 w5, w5
    strh  w5, [x28, w0, uxtw]
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    READ_GPR w2, w3
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowWriteU16
    ldp   x3, x30, [sp], #16
    STENCIL_TAILCALL

// -------------------------------------------------------------
// Compare Stencils: cmplwi, cmpwi, cmpl, cmp
// -------------------------------------------------------------

// Precomputed Dolphin CR internal 64-bit table (matches ConditionRegister::s_crTable)
.align 4
g_cr_table:
    .quad 0x8000000100000001 // 0
    .quad 0x8800000100000001 // 1
    .quad 0x8000000100000000 // 2: EQ
    .quad 0x8800000100000000 // 3: EQ | SO
    .quad 0x0000000100000001 // 4: GT
    .quad 0x0800000100000001 // 5: GT | SO
    .quad 0x0000000100000000 // 6
    .quad 0x0800000100000000 // 7
    .quad 0xc000000100000001 // 8: LT
    .quad 0xc800000100000001 // 9: LT | SO
    .quad 0xc000000100000000 // a
    .quad 0xc800000100000000 // b
    .quad 0x4000000100000001 // c
    .quad 0x4800000100000001 // d
    .quad 0x4000000100000000 // e
    .quad 0x4800000100000000 // f

// cmplwi: operand packed = (CRFD << 24) | (RA << 16) | UIMM_16
.global stencil_dyn_cmplwi
stencil_dyn_cmplwi:
    ldr   x1, [x20, #8]
    ubfx  w3, w1, #0, #16        // UIMM
    ubfx  w2, w1, #16, #5        // RA
    ubfx  w4, w1, #24, #3        // CRFD (0..7)
    READ_GPR w0, w2              // w0 = gpr[RA]
    cmp   w0, w3                 // unsigned compare
    mov   w5, #2                 // CR_EQ
    b.eq  1f
    mov   w5, #8                 // CR_LT
    b.lo  1f
    mov   w5, #4                 // CR_GT
1:
    ldrb  w6, [x19, #757]        // xer_so_ov format: (SO << 1) | OV
    lsr   w6, w6, #1
    and   w6, w6, #1             // XER.SO
    orr   w5, w5, w6             // 4-bit index: (LT/GT/EQ) | SO
    adrp  x7, g_cr_table
    add   x7, x7, :lo12:g_cr_table
    ldr   x8, [x7, w5, uxtw #3]  // load 64-bit internal CR value
    add   x9, x19, #672          // ppc_state.cr.fields base (offset 672)
    str   x8, [x9, w4, uxtw #3]  // cr.fields[CRFD] = x8
    STENCIL_TAILCALL

// cmpwi: operand packed = (CRFD << 24) | (RA << 16) | (u16)SIMM_16
.global stencil_dyn_cmpwi
stencil_dyn_cmpwi:
    ldr   x1, [x20, #8]
    sbfx  w3, w1, #0, #16        // sign-extended SIMM_16
    ubfx  w2, w1, #16, #5        // RA
    ubfx  w4, w1, #24, #3        // CRFD
    READ_GPR w0, w2              // w0 = gpr[RA]
    cmp   w0, w3                 // signed compare
    mov   w5, #2                 // CR_EQ
    b.eq  1f
    mov   w5, #8                 // CR_LT
    b.lt  1f
    mov   w5, #4                 // CR_GT
1:
    ldrb  w6, [x19, #757]        // xer_so_ov
    lsr   w6, w6, #1
    and   w6, w6, #1             // XER.SO
    orr   w5, w5, w6             // (LT/GT/EQ) | SO
    adrp  x7, g_cr_table
    add   x7, x7, :lo12:g_cr_table
    ldr   x8, [x7, w5, uxtw #3]
    add   x9, x19, #672
    str   x8, [x9, w4, uxtw #3]
    STENCIL_TAILCALL

// cmpl: operand packed = (CRFD << 24) | (RA << 16) | (RB << 8)
.global stencil_dyn_cmpl
stencil_dyn_cmpl:
    ldr   x1, [x20, #8]
    ubfx  w3, w1, #8, #5         // RB
    ubfx  w2, w1, #16, #5        // RA
    ubfx  w4, w1, #24, #3        // CRFD
    READ_GPR w0, w2              // gpr[RA]
    READ_GPR w1, w3              // gpr[RB]
    cmp   w0, w1                 // unsigned compare
    mov   w5, #2                 // CR_EQ
    b.eq  1f
    mov   w5, #8                 // CR_LT
    b.lo  1f
    mov   w5, #4                 // CR_GT
1:
    ldrb  w6, [x19, #757]
    lsr   w6, w6, #1
    and   w6, w6, #1
    orr   w5, w5, w6
    adrp  x7, g_cr_table
    add   x7, x7, :lo12:g_cr_table
    ldr   x8, [x7, w5, uxtw #3]
    add   x9, x19, #672
    str   x8, [x9, w4, uxtw #3]
    STENCIL_TAILCALL

// cmp: operand packed = (CRFD << 24) | (RA << 16) | (RB << 8)
.global stencil_dyn_cmp
stencil_dyn_cmp:
    ldr   x1, [x20, #8]
    ubfx  w3, w1, #8, #5         // RB
    ubfx  w2, w1, #16, #5        // RA
    ubfx  w4, w1, #24, #3        // CRFD
    READ_GPR w0, w2              // gpr[RA]
    READ_GPR w1, w3              // gpr[RB]
    cmp   w0, w1                 // signed compare
    mov   w5, #2                 // CR_EQ
    b.eq  1f
    mov   w5, #8                 // CR_LT
    b.lt  1f
    mov   w5, #4                 // CR_GT
1:
    ldrb  w6, [x19, #757]
    lsr   w6, w6, #1
    and   w6, w6, #1
    orr   w5, w5, w6
    adrp  x7, g_cr_table
    add   x7, x7, :lo12:g_cr_table
    ldr   x8, [x7, w5, uxtw #3]
    add   x9, x19, #672
    str   x8, [x9, w4, uxtw #3]
    STENCIL_TAILCALL


// stencil_tape_jump: operand = pointer to target tape slot
.global stencil_tape_jump
stencil_tape_jump:
    ldr   x20, [x20, #8]
    ldr   x16, [x20]
    br    x16

// -------------------------------------------------------------
// Native Tape Inter-Block Linking Stencils
// -------------------------------------------------------------

// TapeDirectLinkData:
// offset 0:  target.first_stencil (ptr)
// offset 8:  target.tape_cursor (ptr)
// offset 16: destination_pc (u32)
// offset 20: downcount_cycles (u32)

.global stencil_tape_direct_jump
stencil_tape_direct_jump:
    ldr   x1, [x20, #8]            // x1 = pointer to TapeDirectLinkData
    ldr   w2, [x1, #20]            // w2 = downcount_cycles
    subs  w26, w26, w2             // decrement register downcount
    b.le  10f                      // timeslice expired -> return to C++

    ldr   w3, [x1, #16]            // destination_pc
    str   w3, [x19, #0]            // ppc_state.pc = destination_pc
    str   w3, [x19, #4]            // ppc_state.npc = destination_pc

    ldr   x20, [x1, #8]            // x20 = target.tape_cursor
    cbz   x20, 20f                 // unlinked -> return to C++ to compile

    ldr   x16, [x1, #0]            // x16 = target.first_stencil
    ldr   x1,  [x20, #8]           // x1 = target's first operand
    br    x16                      // Jump directly into target block!

10: // Timeslice expired (w26 <= 0)
    ldr   w3, [x1, #16]            // destination_pc
    str   w3, [x19, #0]            // ppc_state.pc = destination_pc
    str   w3, [x19, #4]            // ppc_state.npc = destination_pc
20: // Unlinked or timeslice exit
    b     a642a64_stencil_exit_nodec


// TapeCondBranchLinkData:
// offset 0:  taken.first_stencil (ptr)
// offset 8:  taken.tape_cursor (ptr)
// offset 16: taken_destination_pc (u32)
// offset 20: downcount_cycles (u32)
// offset 24: fallthrough.first_stencil (ptr)
// offset 32: fallthrough.tape_cursor (ptr)
// offset 40: fallthrough_destination_pc (u32)
// offset 44: pad (u32)
// offset 48: bo (u32)
// offset 52: bi (u32)

.global stencil_tape_cond_branch
stencil_tape_cond_branch:
    ldr   x1, [x20, #8]            // x1 = pointer to TapeCondBranchLinkData
    ldr   w2, [x1, #20]            // w2 = downcount_cycles
    subs  w26, w26, w2             // decrement register downcount

    // Load BO and BI
    ldr   w10, [x1, #48]           // w10 = BO
    ldr   w11, [x1, #52]           // w11 = BI

    // 1. Counter check: BO & BO_DONT_DECREMENT_FLAG (4)
    tbz   w10, #2, 100f            // if (BO & 4) == 0, check counter
    mov   w12, #1                  // counter = true
    b     101f
100:
    // Decrement CTR (spr[SPR_CTR] at offset 868)
    ldr   w14, [x19, #868]
    sub   w14, w14, #1
    str   w14, [x19, #868]
    cmp   w14, #0
    cset  w15, ne
    ubfx  w16, w10, #1, #1
    eor   w12, w15, w16            // w12 = counter
101:
    // 2. Condition check: BO & BO_DONT_CHECK_CONDITION (16)
    tbnz  w10, #4, 102f            // if (BO & 16) != 0, ignore condition (true)

    // BI: cr_field = BI >> 2, cr_sub = BI & 3
    ubfx  w13, w11, #2, #3         // cr_field (0..7)
    add   x14, x19, #672           // cr.fields base
    ldr   x15, [x14, w13, uxtw #3] // 64-bit cr_val
    and   w16, w11, #3             // cr_sub (0: LT, 1: GT, 2: EQ, 3: SO)

    cmp   w16, #2
    b.eq  200f                     // EQ
    cmp   w16, #0
    b.eq  201f                     // LT
    cmp   w16, #1
    b.eq  202f                     // GT
    // SO: bit 59
    lsr   x15, x15, #59
    and   w14, w15, #1
    b     203f
200: // EQ: lower 32 bits == 0
    cmp   w15, #0
    cset  w14, eq
    b     203f
201: // LT: bit 62
    lsr   x15, x15, #62
    and   w14, w15, #1
    b     203f
202: // GT: (s64)cr_val > 0
    cmp   x15, #0
    cset  w14, gt
203:
    // true_false = (BO >> 3) & 1
    ubfx  w16, w10, #3, #1
    cmp   w14, w16
    cset  w13, eq                  // w13 = condition
    b     103f
102:
    mov   w13, #1                  // condition = true
103:
    // taken = counter && condition
    and   w12, w12, w13

    // Select target: if taken (w12 != 0), x1 stays at x1; if fallthrough (w12 == 0), x1 += 24
    cbnz  w12, 105f
    add   x1, x1, #24
105:
    cmp   w26, #0
    b.le  10f

    ldr   w3, [x1, #16]            // destination_pc
    str   w3, [x19, #0]            // ppc_state.pc = destination_pc
    str   w3, [x19, #4]            // ppc_state.npc = destination_pc

    ldr   x20, [x1, #8]            // x20 = target.tape_cursor
    cbz   x20, 20f                 // unlinked -> return to C++ to compile

    ldr   x16, [x1, #0]            // x16 = target.first_stencil
    ldr   x1,  [x20, #8]           // x1 = target's first operand
    br    x16                      // Jump directly into target block!

10: // Timeslice expired
    ldr   w3, [x1, #16]            // destination_pc
    str   w3, [x19, #0]            // ppc_state.pc = destination_pc
    str   w3, [x19, #4]            // ppc_state.npc = destination_pc
20:
    b     a642a64_stencil_exit_nodec

// ============================================================================
// Super-Instructions (Macro-Instruction Fusion)
// ============================================================================

.macro DO_LWZ_OP reg_op
    sbfx  w0, \reg_op, #0, #16
    ubfx  w2, \reg_op, #16, #5
    ubfx  w3, \reg_op, #24, #5
    cbz   w2, 80f
    READ_GPR w4, w2
    add   w0, w4, w0
80: tst   w0, #0x3e000000
    b.ne  81f
    and   w0, w0, #0x01ffffff
    ldr   w5, [x28, w0, uxtw]
    rev   w5, w5
    WRITE_GPR w5, w3
    b     82f
81:
    stp   x1, x30, [sp, #-32]!
    stp   x3, x6,  [sp, #16]
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowReadU32
    ldp   x3, x6,  [sp, #16]
    ldp   x1, x30, [sp], #32
    WRITE_GPR w0, w3
82:
.endm

.macro DO_STW_OP reg_op
    sbfx  w0, \reg_op, #0, #16
    ubfx  w2, \reg_op, #16, #5
    ubfx  w3, \reg_op, #24, #5
    cbz   w2, 83f
    READ_GPR w4, w2
    add   w0, w4, w0
83: tst   w0, #0x3e000000
    b.ne  84f
    and   w0, w0, #0x01ffffff
    READ_GPR w5, w3
    rev   w5, w5
    str   w5, [x28, w0, uxtw]
    b     85f
84:
    stp   x1, x30, [sp, #-32]!
    str   x6,      [sp, #16]
    READ_GPR w2, w3
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowWriteU32
    ldr   x6,      [sp, #16]
    ldp   x1, x30, [sp], #32
85:
.endm

.macro DO_ADDIS_OP reg_op
    sbfx  w0, \reg_op, #0, #16
    lsl   w0, w0, #16
    ubfx  w2, \reg_op, #16, #5
    ubfx  w3, \reg_op, #24, #5
    cbz   w2, 10f
    READ_GPR w4, w2
    add   w0, w4, w0
10: WRITE_GPR w0, w3
.endm

.macro DO_ADDI_OP reg_op
    sbfx  w0, \reg_op, #0, #16
    ubfx  w2, \reg_op, #16, #5
    ubfx  w3, \reg_op, #24, #5
    cbz   w2, 10f
    READ_GPR w4, w2
    add   w0, w4, w0
10: WRITE_GPR w0, w3
.endm

.macro DO_ORI_OP reg_op
    ubfx  w0, \reg_op, #0, #16
    ubfx  w2, \reg_op, #16, #5
    ubfx  w3, \reg_op, #24, #5
    READ_GPR w4, w2
    orr   w0, w4, w0
    WRITE_GPR w0, w3
.endm

.macro DO_MR_OP reg_op
    ubfx  w2, \reg_op, #16, #5
    ubfx  w3, \reg_op, #24, #5
    READ_GPR w0, w2
    WRITE_GPR w0, w3
.endm

.global stencil_dyn_super_lwz_lwz
stencil_dyn_super_lwz_lwz:
    ldr   x1, [x20, #8]
    lsr   x6, x1, #32
    DO_LWZ_OP w6
    DO_LWZ_OP w1
    STENCIL_TAILCALL

.global stencil_dyn_super_stw_stw
stencil_dyn_super_stw_stw:
    ldr   x1, [x20, #8]
    lsr   x6, x1, #32
    DO_STW_OP w6
    DO_STW_OP w1
    STENCIL_TAILCALL

.global stencil_dyn_super_lis_addi
stencil_dyn_super_lis_addi:
    ldr   x1, [x20, #8]
    lsr   x6, x1, #32
    DO_ADDIS_OP w6
    DO_ADDI_OP w1
    STENCIL_TAILCALL

.global stencil_dyn_super_lis_ori
stencil_dyn_super_lis_ori:
    ldr   x1, [x20, #8]
    lsr   x6, x1, #32
    DO_ADDIS_OP w6
    DO_ORI_OP w1
    STENCIL_TAILCALL

.global stencil_dyn_super_mr_mr
stencil_dyn_super_mr_mr:
    ldr   x1, [x20, #8]
    lsr   x6, x1, #32
    DO_MR_OP w6
    DO_MR_OP w1
    STENCIL_TAILCALL

.global stencil_dyn_mflr
stencil_dyn_mflr:
    ldr   x1, [x20, #8]
    ubfx  w3, w1, #24, #5
    ldr   w0, [x19, #864]
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_mtlr
stencil_dyn_mtlr:
    ldr   x1, [x20, #8]
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w3
    str   w0, [x19, #864]
    STENCIL_TAILCALL

.global stencil_dyn_stwu
stencil_dyn_stwu:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w4, w2
    add   w0, w4, w0
    WRITE_GPR w0, w2
    tst   w0, #0x3e000000
    b.ne  2f
    and   w0, w0, #0x01ffffff
    READ_GPR w5, w3
    rev   w5, w5
    str   w5, [x28, w0, uxtw]
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    READ_GPR w2, w3
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowWriteU32
    ldp   x3, x30, [sp], #16
    STENCIL_TAILCALL

.global stencil_dyn_mulli
stencil_dyn_mulli:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w4, w2
    mul   w0, w4, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_mullw
stencil_dyn_mullw:
    ldr   x1, [x20, #8]
    ubfx  w4, w1, #8, #5
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w5, w4
    READ_GPR w4, w2
    mul   w0, w4, w5
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_lwzx
stencil_dyn_lwzx:
    ldr   x1, [x20, #8]
    ubfx  w4, w1, #8, #5
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w4
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  tst   w0, #0x3e000000
    b.ne  2f
    and   w0, w0, #0x01ffffff
    ldr   w5, [x28, w0, uxtw]
    rev   w5, w5
    WRITE_GPR w5, w3
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowReadU32
    ldp   x3, x30, [sp], #16
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_stwx
stencil_dyn_stwx:
    ldr   x1, [x20, #8]
    ubfx  w4, w1, #8, #5
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    READ_GPR w0, w4
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  tst   w0, #0x3e000000
    b.ne  2f
    and   w0, w0, #0x01ffffff
    READ_GPR w5, w3
    rev   w5, w5
    str   w5, [x28, w0, uxtw]
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    READ_GPR w2, w3
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowWriteU32
    ldp   x3, x30, [sp], #16
    STENCIL_TAILCALL

.global stencil_dyn_lfs
stencil_dyn_lfs:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  tst   w0, #0x3e000000
    b.ne  2f
    and   w0, w0, #0x01ffffff
    ldr   w5, [x28, w0, uxtw]
    rev   w5, w5
    fmov  s0, w5
    fcvt  d0, s0
    add   x6, x19, #160
    add   x6, x6, x3, lsl #4
    stp   d0, d0, [x6]
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowReadU32
    fmov  s0, w0
    fcvt  d0, s0
    ldr   x3, [sp]
    add   x6, x19, #160
    add   x6, x6, x3, lsl #4
    stp   d0, d0, [x6]
    ldp   x3, x30, [sp], #16
    STENCIL_TAILCALL

.global stencil_dyn_stfs
stencil_dyn_stfs:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  add   x6, x19, #160
    add   x6, x6, x3, lsl #4
    ldr   d0, [x6]
    fcvt  s0, d0
    fmov  w5, s0
    tst   w0, #0x3e000000
    b.ne  2f
    and   w0, w0, #0x01ffffff
    rev   w5, w5
    str   w5, [x28, w0, uxtw]
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    mov   w2, w5
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowWriteU32
    ldp   x3, x30, [sp], #16
    STENCIL_TAILCALL

.global stencil_dyn_lfd
stencil_dyn_lfd:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  tst   w0, #0x3e000000
    b.ne  2f
    and   w0, w0, #0x01ffffff
    ldr   x5, [x28, w0, uxtw]
    rev   x5, x5
    add   x6, x19, #160
    add   x6, x6, x3, lsl #4
    str   x5, [x6]
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowReadU64
    ldr   x3, [sp]
    add   x6, x19, #160
    add   x6, x6, x3, lsl #4
    str   x0, [x6]
    ldp   x3, x30, [sp], #16
    STENCIL_TAILCALL

.global stencil_dyn_stfd
stencil_dyn_stfd:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  add   x6, x19, #160
    add   x6, x6, x3, lsl #4
    ldr   x5, [x6]
    tst   w0, #0x3e000000
    b.ne  2f
    and   w0, w0, #0x01ffffff
    rev   x5, x5
    str   x5, [x28, w0, uxtw]
    STENCIL_TAILCALL
2:  stp   x3, x30, [sp, #-16]!
    mov   x2, x5
    mov   w1, w0
    mov   x0, x19
    bl    Stencil_SlowWriteU64
    ldp   x3, x30, [sp], #16
    STENCIL_TAILCALL

.global stencil_dyn_fmr
stencil_dyn_fmr:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #8, #5
    ubfx  w3, w1, #24, #5
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x3, lsl #4
    str   d0, [x8]
    STENCIL_TAILCALL

.global stencil_dyn_mtctr
stencil_dyn_mtctr:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #24, #5
    READ_GPR w0, w2
    str   w0, [x19, #868]
    STENCIL_TAILCALL

.global stencil_dyn_mfctr
stencil_dyn_mfctr:
    ldr   x1, [x20, #8]
    ubfx  w3, w1, #24, #5
    ldr   w0, [x19, #868]
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_mfmsr
stencil_dyn_mfmsr:
    ldr   x1, [x20, #8]
    ubfx  w3, w1, #24, #5
    ldr   w0, [x19, #736]
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_dcbi
stencil_dyn_dcbi:
    STENCIL_TAILCALL

// ============================================================================
// Gekko Paired-Single SIMD Stencils (OP4)
// ============================================================================

.global stencil_dyn_ps_add
stencil_dyn_ps_add:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldp   d0, d1, [x7]
    add   x8, x6, x4, lsl #4
    ldp   d2, d3, [x8]
    fadd  d0, d0, d2
    fadd  d1, d1, d3
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_ps_sub
stencil_dyn_ps_sub:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldp   d0, d1, [x7]
    add   x8, x6, x4, lsl #4
    ldp   d2, d3, [x8]
    fsub  d0, d0, d2
    fsub  d1, d1, d3
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_ps_mul
stencil_dyn_ps_mul:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #0,  #5    // FC
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldp   d0, d1, [x7]
    add   x8, x6, x4, lsl #4
    ldp   d2, d3, [x8]
    fmul  d0, d0, d2
    fmul  d1, d1, d3
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_ps_div
stencil_dyn_ps_div:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldp   d0, d1, [x7]
    add   x8, x6, x4, lsl #4
    ldp   d2, d3, [x8]
    fdiv  d0, d0, d2
    fdiv  d1, d1, d3
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_ps_madd
stencil_dyn_ps_madd:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #0,  #5    // FC
    ubfx  w5, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldp   d0, d1, [x7]
    add   x8, x6, x4, lsl #4
    ldp   d2, d3, [x8]
    add   x10, x6, x5, lsl #4
    ldp   d4, d5, [x10]
    fmadd d0, d0, d2, d4
    fmadd d1, d1, d3, d5
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_ps_msub
stencil_dyn_ps_msub:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #0,  #5    // FC
    ubfx  w5, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldp   d0, d1, [x7]
    add   x8, x6, x4, lsl #4
    ldp   d2, d3, [x8]
    add   x10, x6, x5, lsl #4
    ldp   d4, d5, [x10]
    fnmsub d0, d0, d2, d4
    fnmsub d1, d1, d3, d5
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_ps_neg
stencil_dyn_ps_neg:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldp   d0, d1, [x7]
    fneg  d0, d0
    fneg  d1, d1
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_ps_abs
stencil_dyn_ps_abs:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldp   d0, d1, [x7]
    fabs  d0, d0
    fabs  d1, d1
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_ps_nabs
stencil_dyn_ps_nabs:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldp   d0, d1, [x7]
    fabs  d0, d0
    fneg  d0, d0
    fabs  d1, d1
    fneg  d1, d1
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_ps_merge00
stencil_dyn_ps_merge00:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_ps_merge01
stencil_dyn_ps_merge01:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8, #8]
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_ps_merge10
stencil_dyn_ps_merge10:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7, #8]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_ps_merge11
stencil_dyn_ps_merge11:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7, #8]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8, #8]
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
    STENCIL_TAILCALL

// ============================================================================
// Single-Precision Floating Point Stencils (OP59)
// ============================================================================

.global stencil_dyn_fadds
stencil_dyn_fadds:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    fadd  d0, d0, d1
    fcvt  s0, d0
    fcvt  d0, s0
    add   x9, x6, x3, lsl #4
    stp   d0, d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fsubs
stencil_dyn_fsubs:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    fsub  d0, d0, d1
    fcvt  s0, d0
    fcvt  d0, s0
    add   x9, x6, x3, lsl #4
    stp   d0, d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fmuls
stencil_dyn_fmuls:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #0,  #5    // FC
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    fmul  d0, d0, d1
    fcvt  s0, d0
    fcvt  d0, s0
    add   x9, x6, x3, lsl #4
    stp   d0, d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fdivs
stencil_dyn_fdivs:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    fdiv  d0, d0, d1
    fcvt  s0, d0
    fcvt  d0, s0
    add   x9, x6, x3, lsl #4
    stp   d0, d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fmadds
stencil_dyn_fmadds:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #0,  #5    // FC
    ubfx  w5, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    add   x10, x6, x5, lsl #4
    ldr   d2, [x10]
    fmadd d0, d0, d1, d2
    fcvt  s0, d0
    fcvt  d0, s0
    add   x9, x6, x3, lsl #4
    stp   d0, d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fmsubs
stencil_dyn_fmsubs:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #0,  #5    // FC
    ubfx  w5, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    add   x10, x6, x5, lsl #4
    ldr   d2, [x10]
    fnmsub d0, d0, d1, d2
    fcvt  s0, d0
    fcvt  d0, s0
    add   x9, x6, x3, lsl #4
    stp   d0, d0, [x9]
    STENCIL_TAILCALL

// ============================================================================
// Double-Precision Floating Point Stencils (OP63)
// ============================================================================

.global stencil_dyn_fadd
stencil_dyn_fadd:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    fadd  d0, d0, d1
    add   x9, x6, x3, lsl #4
    str   d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fsub
stencil_dyn_fsub:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    fsub  d0, d0, d1
    add   x9, x6, x3, lsl #4
    str   d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fmul
stencil_dyn_fmul:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #0,  #5    // FC
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    fmul  d0, d0, d1
    add   x9, x6, x3, lsl #4
    str   d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fdiv
stencil_dyn_fdiv:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    fdiv  d0, d0, d1
    add   x9, x6, x3, lsl #4
    str   d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fmadd
stencil_dyn_fmadd:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #0,  #5    // FC
    ubfx  w5, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    add   x10, x6, x5, lsl #4
    ldr   d2, [x10]
    fmadd d0, d0, d1, d2
    add   x9, x6, x3, lsl #4
    str   d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fmsub
stencil_dyn_fmsub:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #0,  #5    // FC
    ubfx  w5, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]
    add   x10, x6, x5, lsl #4
    ldr   d2, [x10]
    fnmsub d0, d0, d1, d2
    add   x9, x6, x3, lsl #4
    str   d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fabs
stencil_dyn_fabs:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    fabs  d0, d0
    add   x9, x6, x3, lsl #4
    str   d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fneg
stencil_dyn_fneg:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    fneg  d0, d0
    add   x9, x6, x3, lsl #4
    str   d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fnabs
stencil_dyn_fnabs:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]
    fabs  d0, d0
    fneg  d0, d0
    add   x9, x6, x3, lsl #4
    str   d0, [x9]
    STENCIL_TAILCALL

.global stencil_dyn_fsel
stencil_dyn_fsel:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5    // FA
    ubfx  w4, w1, #0,  #5    // FC
    ubfx  w5, w1, #8,  #5    // FB
    ubfx  w3, w1, #24, #5    // FD
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldr   d0, [x7]           // FA
    add   x8, x6, x4, lsl #4
    ldr   d1, [x8]           // FC
    add   x10, x6, x5, lsl #4
    ldr   d2, [x10]          // FB
    fcmp  d0, #0.0
    fcsel d0, d1, d2, ge     // if FA >= 0.0 then FC else FB
    add   x9, x6, x3, lsl #4
    str   d0, [x9]
    STENCIL_TAILCALL

// ============================================================================
// Additional Integer Stencils (OP31)
// ============================================================================

.global stencil_dyn_andc
stencil_dyn_andc:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w5, w2
    READ_GPR w6, w4
    bic   w0, w5, w6
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_orc
stencil_dyn_orc:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w5, w2
    READ_GPR w6, w4
    orn   w0, w5, w6
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_nand
stencil_dyn_nand:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w5, w2
    READ_GPR w6, w4
    and   w0, w5, w6
    mvn   w0, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_eqv
stencil_dyn_eqv:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w5, w2
    READ_GPR w6, w4
    eor   w0, w5, w6
    mvn   w0, w0
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_divw
stencil_dyn_divw:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w5, w2
    READ_GPR w6, w4
    sdiv  w0, w5, w6
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_divwu
stencil_dyn_divwu:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w5, w2
    READ_GPR w6, w4
    udiv  w0, w5, w6
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_mulhw
stencil_dyn_mulhw:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w5, w2
    READ_GPR w6, w4
    smull x0, w5, w6
    lsr   x0, x0, #32
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

.global stencil_dyn_mulhwu
stencil_dyn_mulhwu:
    ldr   x1, [x20, #8]
    ubfx  w2, w1, #16, #5
    ubfx  w4, w1, #8,  #5
    ubfx  w3, w1, #24, #5
    READ_GPR w5, w2
    READ_GPR w6, w4
    umull x0, w5, w6
    lsr   x0, x0, #32
    WRITE_GPR w0, w3
    STENCIL_TAILCALL

// ============================================================================
// Vertex Super-Instruction Stencils (Item 3)
// ============================================================================

.macro DO_PS_MUL_OP reg_op
    ubfx  w2, \reg_op, #16, #5
    ubfx  w4, \reg_op, #0,  #5
    ubfx  w3, \reg_op, #24, #5
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldp   d0, d1, [x7]
    add   x8, x6, x4, lsl #4
    ldp   d2, d3, [x8]
    fmul  d0, d0, d2
    fmul  d1, d1, d3
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
.endm

.macro DO_PS_ADD_OP reg_op
    ubfx  w2, \reg_op, #16, #5
    ubfx  w4, \reg_op, #8,  #5
    ubfx  w3, \reg_op, #24, #5
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldp   d0, d1, [x7]
    add   x8, x6, x4, lsl #4
    ldp   d2, d3, [x8]
    fadd  d0, d0, d2
    fadd  d1, d1, d3
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
.endm

.macro DO_PS_MADD_OP reg_op
    ubfx  w2, \reg_op, #16, #5
    ubfx  w4, \reg_op, #0,  #5
    ubfx  w5, \reg_op, #8,  #5
    ubfx  w3, \reg_op, #24, #5
    add   x6, x19, #160
    add   x7, x6, x2, lsl #4
    ldp   d0, d1, [x7]
    add   x8, x6, x4, lsl #4
    ldp   d2, d3, [x8]
    add   x10, x6, x5, lsl #4
    ldp   d4, d5, [x10]
    fmadd d0, d0, d2, d4
    fmadd d1, d1, d3, d5
    add   x9, x6, x3, lsl #4
    stp   d0, d1, [x9]
.endm

.global stencil_dyn_super_ps_mul_ps_add
stencil_dyn_super_ps_mul_ps_add:
    ldr   x1, [x20, #8]
    lsr   x11, x1, #32
    DO_PS_MUL_OP w11
    DO_PS_ADD_OP w1
    STENCIL_TAILCALL

.global stencil_dyn_super_ps_madd_ps_add
stencil_dyn_super_ps_madd_ps_add:
    ldr   x1, [x20, #8]
    lsr   x11, x1, #32
    DO_PS_MADD_OP w11
    DO_PS_ADD_OP w1
    STENCIL_TAILCALL

#endif

