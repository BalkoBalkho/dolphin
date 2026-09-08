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

.global a642a64_stencil_exit
a642a64_stencil_exit:
    // 1. Calculate instructions executed from tape cursor advance: (x20 - tape_base) / 16
    ldr  x1, [sp, #64]
    sub  x0, x20, x1
    lsr  w0, w0, #4

    // 2. Decrement downcount in w26 and sync back to memory
    ldr  x4, [sp, #56]
    cbz  x4, 1f
    sub  w26, w26, w0
    str  w26, [x4]
1:
    // 3. Restore callee-saved registers
    ldp  x19, x20, [sp, #16]
    ldp  x27, x28, [sp, #32]
    ldr  x26,      [sp, #48]
    ldp  x29, x30, [sp], #80
    ret

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
    ldr   x16, [x20, #16]!
    br    x16
.endm

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
1:  READ_GPR w5, w3
    rev   w5, w5
    str   w5, [x28, w0, uxtw]
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

.global stencil_dyn_stb
stencil_dyn_stb:
    ldr   x1, [x20, #8]
    sbfx  w0, w1, #0, #16
    ubfx  w2, w1, #16, #5
    ubfx  w3, w1, #24, #5
    cbz   w2, 1f
    READ_GPR w4, w2
    add   w0, w4, w0
1:  READ_GPR w5, w3
    strb  w5, [x28, w0, uxtw]
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
1:  READ_GPR w5, w3
    rev16 w5, w5
    strh  w5, [x28, w0, uxtw]
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

#endif
