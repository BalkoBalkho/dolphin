#if defined(__aarch64__) || defined(_M_ARM64)
.text
.align 4

// Register conventions:
// x19 = VMState*
// x20 = const BytecodeInst* pc
// x21 = void** dispatch_table
// x22 = saved LR / scratch

.macro DISPATCH
    add  x20, x20, #8
    ldrh w0, [x20]
    ldr  x16, [x21, x0, lsl #3]
    br   x16
.endm

.macro DISPATCH_DIRECT
    ldrh w0, [x20]
    ldr  x16, [x21, x0, lsl #3]
    br   x16
.endm

.macro READ_REG dst, idx
    cmp   \idx, #31
    b.lo  .Lread_reg_\@
    b.eq  .Lread_xzr_\@
    // idx == 32 (SP)
    ldr   \dst, [x19, #264]
    b     .Lread_done_\@
.Lread_reg_\@:
    ldr   \dst, [x19, \idx, uxtw #3]
    b     .Lread_done_\@
.Lread_xzr_\@:
    mov   \dst, #0
.Lread_done_\@:
.endm

.macro WRITE_REG src, idx
    cmp   \idx, #31
    b.lo  .Lwrite_reg_\@
    b.ne  .Lwrite_sp_\@
    // idx == 31 (XZR): discard write
    b     .Lwrite_done_\@
.Lwrite_sp_\@:
    // idx == 32 (SP)
    str   \src, [x19, #264]
    b     .Lwrite_done_\@
.Lwrite_reg_\@:
    str   \src, [x19, \idx, uxtw #3]
.Lwrite_done_\@:
.endm

.macro APPLY_SHIFT val, stype, samt
    cbz   \samt, .Lshift_done_\@
    uxtw  x15, \samt
    cmp   \stype, #0
    b.eq  .Lshift_lsl_\@
    cmp   \stype, #1
    b.eq  .Lshift_lsr_\@
    cmp   \stype, #2
    b.eq  .Lshift_asr_\@
    ror   \val, \val, x15
    b     .Lshift_done_\@
.Lshift_lsl_\@:
    lsl   \val, \val, x15
    b     .Lshift_done_\@
.Lshift_lsr_\@:
    lsr   \val, \val, x15
    b     .Lshift_done_\@
.Lshift_asr_\@:
    asr   \val, \val, x15
.Lshift_done_\@:
.endm

.macro EVAL_COND dst_reg, cond_code
    ldr  w6, [x19, #280]
    lsl  x6, x6, #28
    msr  nzcv, x6
    and  \cond_code, \cond_code, #15
    adr  x7, .Lcond_tbl_\@
    add  x7, x7, \cond_code, uxtw #3
    br   x7
.balign 8
.Lcond_tbl_\@:
    cset \dst_reg, eq; b .Lcond_done_\@
    cset \dst_reg, ne; b .Lcond_done_\@
    cset \dst_reg, cs; b .Lcond_done_\@
    cset \dst_reg, cc; b .Lcond_done_\@
    cset \dst_reg, mi; b .Lcond_done_\@
    cset \dst_reg, pl; b .Lcond_done_\@
    cset \dst_reg, vs; b .Lcond_done_\@
    cset \dst_reg, vc; b .Lcond_done_\@
    cset \dst_reg, hi; b .Lcond_done_\@
    cset \dst_reg, ls; b .Lcond_done_\@
    cset \dst_reg, ge; b .Lcond_done_\@
    cset \dst_reg, lt; b .Lcond_done_\@
    cset \dst_reg, gt; b .Lcond_done_\@
    cset \dst_reg, le; b .Lcond_done_\@
    mov  \dst_reg, #1; b .Lcond_done_\@
    mov  \dst_reg, #1; b .Lcond_done_\@
.Lcond_done_\@:
.endm

// -------------------------------------------------------------
// Entry point
// -------------------------------------------------------------
.global interpreter_run
interpreter_run:
    // x0 = VMState*, x1 = const BytecodeInst* block
    stp  x19, x20, [sp, #-48]!
    stp  x21, x22, [sp, #16]
    stp  x29, x30, [sp, #32]
    mov  x29, sp

    mov  x19, x0
    mov  x20, x1
    adrp x21, g_dispatch_table
    add  x21, x21, :lo12:g_dispatch_table

    ldrh w0, [x20]
    ldr  x16, [x21, x0, lsl #3]
    br   x16

// -------------------------------------------------------------
// Halt / NOP
// -------------------------------------------------------------
.global handle_hlt
handle_hlt:
    ldp  x29, x30, [sp, #32]
    ldp  x21, x22, [sp, #16]
    ldp  x19, x20, [sp], #48
    ret

.global handle_nop
handle_nop:
    DISPATCH

// -------------------------------------------------------------
// ALU Register operations
// -------------------------------------------------------------
.global handle_add_reg
handle_add_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #5]      // shift_type
    ldrb w5, [x20, #6]      // shift_amt
    READ_REG x8, w2
    READ_REG x9, w3
    APPLY_SHIFT x9, w4, w5
    add  x10, x8, x9
    WRITE_REG x10, w1
    DISPATCH

.global handle_sub_reg
handle_sub_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #5]      // shift_type
    ldrb w5, [x20, #6]      // shift_amt
    READ_REG x8, w2
    READ_REG x9, w3
    APPLY_SHIFT x9, w4, w5
    sub  x10, x8, x9
    WRITE_REG x10, w1
    DISPATCH

.global handle_mul_reg
handle_mul_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    READ_REG x8, w2
    READ_REG x9, w3
    mul  x10, x8, x9
    WRITE_REG x10, w1
    DISPATCH

.global handle_and_reg
handle_and_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #5]      // shift_type
    ldrb w5, [x20, #6]      // shift_amt
    READ_REG x8, w2
    READ_REG x9, w3
    APPLY_SHIFT x9, w4, w5
    and  x10, x8, x9
    WRITE_REG x10, w1
    DISPATCH

.global handle_orr_reg
handle_orr_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #5]      // shift_type
    ldrb w5, [x20, #6]      // shift_amt
    READ_REG x8, w2
    READ_REG x9, w3
    APPLY_SHIFT x9, w4, w5
    orr  x10, x8, x9
    WRITE_REG x10, w1
    DISPATCH

.global handle_eor_reg
handle_eor_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #5]      // shift_type
    ldrb w5, [x20, #6]      // shift_amt
    READ_REG x8, w2
    READ_REG x9, w3
    APPLY_SHIFT x9, w4, w5
    eor  x10, x8, x9
    WRITE_REG x10, w1
    DISPATCH

.global handle_lsl_reg
handle_lsl_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    READ_REG x8, w2
    READ_REG x9, w3
    lsl  x10, x8, x9
    WRITE_REG x10, w1
    DISPATCH

.global handle_lsr_reg
handle_lsr_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    READ_REG x8, w2
    READ_REG x9, w3
    lsr  x10, x8, x9
    WRITE_REG x10, w1
    DISPATCH

.global handle_asr_reg
handle_asr_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    READ_REG x8, w2
    READ_REG x9, w3
    asr  x10, x8, x9
    WRITE_REG x10, w1
    DISPATCH

.global handle_sdiv_reg
handle_sdiv_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    READ_REG x8, w2
    READ_REG x9, w3
    sdiv x10, x8, x9
    WRITE_REG x10, w1
    DISPATCH

.global handle_udiv_reg
handle_udiv_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    READ_REG x8, w2
    READ_REG x9, w3
    udiv x10, x8, x9
    WRITE_REG x10, w1
    DISPATCH

// -------------------------------------------------------------
// ALU Immediate operations
// -------------------------------------------------------------
.global handle_add_imm
handle_add_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x8, w2
    add  x10, x8, x3
    WRITE_REG x10, w1
    DISPATCH

.global handle_sub_imm
handle_sub_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x8, w2
    sub  x10, x8, x3
    WRITE_REG x10, w1
    DISPATCH

.global handle_and_imm
handle_and_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    READ_REG x8, w2
    and  x10, x8, x3
    WRITE_REG x10, w1
    DISPATCH

.global handle_orr_imm
handle_orr_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    READ_REG x8, w2
    orr  x10, x8, x3
    WRITE_REG x10, w1
    DISPATCH

.global handle_eor_imm
handle_eor_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    READ_REG x8, w2
    eor  x10, x8, x3
    WRITE_REG x10, w1
    DISPATCH

.global handle_lsl_imm
handle_lsl_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    READ_REG x8, w2
    lsl  x10, x8, x3
    WRITE_REG x10, w1
    DISPATCH

.global handle_lsr_imm
handle_lsr_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    READ_REG x8, w2
    lsr  x10, x8, x3
    WRITE_REG x10, w1
    DISPATCH

.global handle_asr_imm
handle_asr_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    READ_REG x8, w2
    asr  x10, x8, x3
    WRITE_REG x10, w1
    DISPATCH

// -------------------------------------------------------------
// Move operations
// -------------------------------------------------------------
.global handle_mov_reg
handle_mov_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    READ_REG x8, w2
    WRITE_REG x8, w1
    DISPATCH

.global handle_rev_reg
handle_rev_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    READ_REG x8, w2
    rev  w10, w8
    uxtw x10, w10
    WRITE_REG x10, w1
    DISPATCH

.global handle_rev16_reg
handle_rev16_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    READ_REG x8, w2
    rev16 w10, w8
    uxtw x10, w10
    WRITE_REG x10, w1
    DISPATCH

.global handle_rev64_reg
handle_rev64_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    READ_REG x8, w2
    rev  x10, x8
    WRITE_REG x10, w1
    DISPATCH

.global handle_movz
handle_movz:
    ldrb w1, [x20, #2]      // rd
    ldr  w3, [x20, #4]      // imm32 (direct full value)
    uxtw x3, w3
    WRITE_REG x3, w1
    DISPATCH

.global handle_movk
handle_movk:
    ldrb w1, [x20, #2]      // rd
    ldrh w3, [x20, #4]      // imm16
    ldrb w4, [x20, #6]      // shift_amt (0,1,2,3)
    READ_REG x8, w1         // current value
    lsl  w4, w4, #4         // shift * 16
    uxtw x4, w4
    mov  x5, #0xffff
    lsl  x5, x5, x4         // mask
    bic  x8, x8, x5         // clear field
    uxtw x3, w3
    lsl  x3, x3, x4
    orr  x8, x8, x3
    WRITE_REG x8, w1
    DISPATCH

.global handle_movn
handle_movn:
    ldrb w1, [x20, #2]      // rd
    ldrh w3, [x20, #4]      // imm16
    ldrb w4, [x20, #6]      // shift_amt (0,1,2,3)
    lsl  w4, w4, #4         // shift * 16
    uxtw x4, w4
    uxtw x3, w3
    lsl  x3, x3, x4
    mvn  x3, x3
    WRITE_REG x3, w1
    DISPATCH

// -------------------------------------------------------------
// Flags-setting ALU operations
// -------------------------------------------------------------
.global handle_adds_reg
handle_adds_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #5]      // shift_type
    ldrb w5, [x20, #6]      // shift_amt
    READ_REG x8, w2
    READ_REG x9, w3
    APPLY_SHIFT x9, w4, w5
    adds x10, x8, x9
    mrs  x6, nzcv
    lsr  x6, x6, #28
    str  w6, [x19, #280]
    WRITE_REG x10, w1
    DISPATCH

.global handle_subs_reg
handle_subs_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #5]      // shift_type
    ldrb w5, [x20, #6]      // shift_amt
    READ_REG x8, w2
    READ_REG x9, w3
    APPLY_SHIFT x9, w4, w5
    subs x10, x8, x9
    mrs  x6, nzcv
    lsr  x6, x6, #28
    str  w6, [x19, #280]
    WRITE_REG x10, w1
    DISPATCH

.global handle_adds_imm
handle_adds_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x8, w2
    adds x10, x8, x3
    mrs  x6, nzcv
    lsr  x6, x6, #28
    str  w6, [x19, #280]
    WRITE_REG x10, w1
    DISPATCH

.global handle_subs_imm
handle_subs_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x8, w2
    subs x10, x8, x3
    mrs  x6, nzcv
    lsr  x6, x6, #28
    str  w6, [x19, #280]
    WRITE_REG x10, w1
    DISPATCH

.global handle_ands_reg
handle_ands_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #5]      // shift_type
    ldrb w5, [x20, #6]      // shift_amt
    READ_REG x8, w2
    READ_REG x9, w3
    APPLY_SHIFT x9, w4, w5
    ands x10, x8, x9
    mrs  x6, nzcv
    lsr  x6, x6, #28
    str  w6, [x19, #280]
    WRITE_REG x10, w1
    DISPATCH

.global handle_ands_imm
handle_ands_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    READ_REG x8, w2
    ands x10, x8, x3
    mrs  x6, nzcv
    lsr  x6, x6, #28
    str  w6, [x19, #280]
    WRITE_REG x10, w1
    DISPATCH

.global handle_cmp_reg
handle_cmp_reg:
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #5]      // shift_type
    ldrb w5, [x20, #6]      // shift_amt
    READ_REG x8, w2
    READ_REG x9, w3
    APPLY_SHIFT x9, w4, w5
    cmp  x8, x9
    mrs  x6, nzcv
    lsr  x6, x6, #28
    str  w6, [x19, #280]
    DISPATCH

.global handle_cmp_imm
handle_cmp_imm:
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x8, w2
    cmp  x8, x3
    mrs  x6, nzcv
    lsr  x6, x6, #28
    str  w6, [x19, #280]
    DISPATCH

.global handle_cmn_reg
handle_cmn_reg:
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #5]      // shift_type
    ldrb w5, [x20, #6]      // shift_amt
    READ_REG x8, w2
    READ_REG x9, w3
    APPLY_SHIFT x9, w4, w5
    cmn  x8, x9
    mrs  x6, nzcv
    lsr  x6, x6, #28
    str  w6, [x19, #280]
    DISPATCH

.global handle_cmn_imm
handle_cmn_imm:
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x8, w2
    cmn  x8, x3
    mrs  x6, nzcv
    lsr  x6, x6, #28
    str  w6, [x19, #280]
    DISPATCH

.global handle_tst_reg
handle_tst_reg:
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #5]      // shift_type
    ldrb w5, [x20, #6]      // shift_amt
    READ_REG x8, w2
    READ_REG x9, w3
    APPLY_SHIFT x9, w4, w5
    tst  x8, x9
    mrs  x6, nzcv
    lsr  x6, x6, #28
    str  w6, [x19, #280]
    DISPATCH

.global handle_tst_imm
handle_tst_imm:
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    READ_REG x8, w2
    tst  x8, x3
    mrs  x6, nzcv
    lsr  x6, x6, #28
    str  w6, [x19, #280]
    DISPATCH

// -------------------------------------------------------------
// Conditional select / set operations
// -------------------------------------------------------------
.global handle_csel
handle_csel:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #7]      // cond
    READ_REG x8, w2
    READ_REG x9, w3
    EVAL_COND w5, w4
    cmp  w5, #0
    csel x10, x8, x9, ne
    WRITE_REG x10, w1
    DISPATCH

.global handle_cset
handle_cset:
    ldrb w1, [x20, #2]      // rd
    ldrb w4, [x20, #7]      // cond
    EVAL_COND w5, w4
    uxtw x10, w5
    WRITE_REG x10, w1
    DISPATCH

.global handle_csinc
handle_csinc:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #7]      // cond
    READ_REG x8, w2
    READ_REG x9, w3
    EVAL_COND w5, w4
    add  x11, x9, #1
    cmp  w5, #0
    csel x10, x8, x11, ne
    WRITE_REG x10, w1
    DISPATCH

.global handle_csinv
handle_csinv:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #7]      // cond
    READ_REG x8, w2
    READ_REG x9, w3
    EVAL_COND w5, w4
    mvn  x11, x9
    cmp  w5, #0
    csel x10, x8, x11, ne
    WRITE_REG x10, w1
    DISPATCH

.global handle_csneg
handle_csneg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldrb w3, [x20, #4]      // rm
    ldrb w4, [x20, #7]      // cond
    READ_REG x8, w2
    READ_REG x9, w3
    EVAL_COND w5, w4
    neg  x11, x9
    cmp  w5, #0
    csel x10, x8, x11, ne
    WRITE_REG x10, w1
    DISPATCH

// -------------------------------------------------------------
// Memory operations
// -------------------------------------------------------------
.global handle_ldr_imm
handle_ldr_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x8, w2
    add  x8, x8, x3
    ldr  x10, [x8]
    WRITE_REG x10, w1
    DISPATCH

.global handle_str_imm
handle_str_imm:
    ldrb w1, [x20, #2]      // rd (data)
    ldrb w2, [x20, #3]      // rn (base)
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x10, w1
    READ_REG x8, w2
    add  x8, x8, x3
    str  x10, [x8]
    DISPATCH

.global handle_strw_imm
handle_strw_imm:
    ldrb w1, [x20, #2]      // rd (data)
    ldrb w2, [x20, #3]      // rn (base)
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x10, w1
    READ_REG x8, w2
    add  x8, x8, x3
    str  w10, [x8]
    DISPATCH

.global handle_ldrb_imm
handle_ldrb_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x8, w2
    add  x8, x8, x3
    ldrb w10, [x8]
    uxtw x10, w10
    WRITE_REG x10, w1
    DISPATCH

.global handle_strb_imm
handle_strb_imm:
    ldrb w1, [x20, #2]      // rd (data)
    ldrb w2, [x20, #3]      // rn (base)
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x10, w1
    READ_REG x8, w2
    add  x8, x8, x3
    strb w10, [x8]
    DISPATCH

.global handle_ldrh_imm
handle_ldrh_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x8, w2
    add  x8, x8, x3
    ldrh w10, [x8]
    uxtw x10, w10
    WRITE_REG x10, w1
    DISPATCH

.global handle_strh_imm
handle_strh_imm:
    ldrb w1, [x20, #2]      // rd (data)
    ldrb w2, [x20, #3]      // rn (base)
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x10, w1
    READ_REG x8, w2
    add  x8, x8, x3
    strh w10, [x8]
    DISPATCH

.global handle_ldrsw_imm
handle_ldrsw_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x8, w2
    add  x8, x8, x3
    ldrsw x10, [x8]
    WRITE_REG x10, w1
    DISPATCH

.global handle_ldrsb_imm
handle_ldrsb_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x8, w2
    add  x8, x8, x3
    ldrsb x10, [x8]
    WRITE_REG x10, w1
    DISPATCH

.global handle_ldrsh_imm
handle_ldrsh_imm:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn
    ldr  w3, [x20, #4]      // imm32
    sxtw x3, w3
    READ_REG x8, w2
    add  x8, x8, x3
    ldrsh x10, [x8]
    WRITE_REG x10, w1
    DISPATCH

.global handle_ldr_reg
handle_ldr_reg:
    ldrb w1, [x20, #2]      // rd
    ldrb w2, [x20, #3]      // rn (base)
    ldrb w3, [x20, #4]      // rm (index)
    ldrb w4, [x20, #5]      // shift_type
    ldrb w5, [x20, #6]      // shift_amt
    READ_REG x8, w2
    READ_REG x9, w3
    APPLY_SHIFT x9, w4, w5
    add  x8, x8, x9
    ldr  x10, [x8]
    WRITE_REG x10, w1
    DISPATCH

.global handle_str_reg
handle_str_reg:
    ldrb w1, [x20, #2]      // rd (data)
    ldrb w2, [x20, #3]      // rn (base)
    ldrb w3, [x20, #4]      // rm (index)
    ldrb w4, [x20, #5]      // shift_type
    ldrb w5, [x20, #6]      // shift_amt
    READ_REG x10, w1
    READ_REG x8, w2
    READ_REG x9, w3
    APPLY_SHIFT x9, w4, w5
    add  x8, x8, x9
    str  x10, [x8]
    DISPATCH

.global handle_ldp
handle_ldp:
    ldrb w1, [x20, #2]      // rd (reg1)
    ldrb w2, [x20, #3]      // rn (base)
    ldrb w3, [x20, #4]      // reg2
    ldrsh x4, [x20, #5]     // mem_disp (signed 16-bit)
    READ_REG x8, w2
    add  x8, x8, x4
    ldr  x10, [x8]
    ldr  x11, [x8, #8]
    WRITE_REG x10, w1
    WRITE_REG x11, w3
    DISPATCH

.global handle_stp
handle_stp:
    ldrb w1, [x20, #2]      // rd (reg1)
    ldrb w2, [x20, #3]      // rn (base)
    ldrb w3, [x20, #4]      // reg2
    ldrsh x4, [x20, #5]     // mem_disp (signed 16-bit)
    READ_REG x10, w1
    READ_REG x11, w3
    READ_REG x8, w2
    add  x8, x8, x4
    str  x10, [x8]
    str  x11, [x8, #8]
    DISPATCH

// -------------------------------------------------------------
// Branch operations
// -------------------------------------------------------------
.global handle_b
handle_b:
    ldr  w3, [x20, #4]
    sxtw x3, w3
    add  x20, x20, #8
    add  x20, x20, x3
    DISPATCH_DIRECT

.global handle_bl
handle_bl:
    add  x8, x20, #8
    str  x8, [x19, #240]    // regs[30] = address of next BytecodeInst
    ldr  w3, [x20, #4]
    sxtw x3, w3
    add  x20, x20, #8
    add  x20, x20, x3
    DISPATCH_DIRECT

.global handle_blr
handle_blr:
    ldrb w2, [x20, #3]
    READ_REG x9, w2
    add  x8, x20, #8
    str  x8, [x19, #240]    // regs[30]
    mov  x20, x9
    DISPATCH_DIRECT

.global handle_br
handle_br:
    ldrb w2, [x20, #3]
    READ_REG x20, w2
    DISPATCH_DIRECT

.global handle_ret
handle_ret:
    ldrb w2, [x20, #3]
    cbnz w2, 1f
    mov  w2, #30
1:  READ_REG x20, w2
    DISPATCH_DIRECT

.global handle_cbz
handle_cbz:
    ldrb w2, [x20, #3]
    READ_REG x8, w2
    cbz  x8, .Lcbz_taken
    DISPATCH
.Lcbz_taken:
    ldr  w3, [x20, #4]
    sxtw x3, w3
    add  x20, x20, #8
    add  x20, x20, x3
    DISPATCH_DIRECT

.global handle_cbnz
handle_cbnz:
    ldrb w2, [x20, #3]
    READ_REG x8, w2
    cbnz x8, .Lcbnz_taken
    DISPATCH
.Lcbnz_taken:
    ldr  w3, [x20, #4]
    sxtw x3, w3
    add  x20, x20, #8
    add  x20, x20, x3
    DISPATCH_DIRECT

.global handle_tbz
handle_tbz:
    ldrb w1, [x20, #2]      // bit index from rd
    ldrb w2, [x20, #3]      // rn
    READ_REG x8, w2
    lsr  x8, x8, x1
    tbz  x8, #0, .Ltbz_taken
    DISPATCH
.Ltbz_taken:
    ldr  w3, [x20, #4]
    sbfx x3, x3, #0, #24
    add  x20, x20, #8
    add  x20, x20, x3
    DISPATCH_DIRECT

.global handle_tbnz
handle_tbnz:
    ldrb w1, [x20, #2]      // bit index from rd
    ldrb w2, [x20, #3]      // rn
    READ_REG x8, w2
    lsr  x8, x8, x1
    tbnz x8, #0, .Ltbnz_taken
    DISPATCH
.Ltbnz_taken:
    ldr  w3, [x20, #4]
    sbfx x3, x3, #0, #24
    add  x20, x20, #8
    add  x20, x20, x3
    DISPATCH_DIRECT

.global handle_b_cond
handle_b_cond:
    ldr  w6, [x19, #280]
    lsl  x6, x6, #28
    msr  nzcv, x6
    ldrb w2, [x20, #7]
    and  w2, w2, #15
    adr  x7, .Lcond_b_table
    add  x7, x7, x2, lsl #3
    br   x7

.balign 8
.Lcond_b_table:
    b.eq .Lb_taken; b .Lb_not_taken
    b.ne .Lb_taken; b .Lb_not_taken
    b.cs .Lb_taken; b .Lb_not_taken
    b.cc .Lb_taken; b .Lb_not_taken
    b.mi .Lb_taken; b .Lb_not_taken
    b.pl .Lb_taken; b .Lb_not_taken
    b.vs .Lb_taken; b .Lb_not_taken
    b.vc .Lb_taken; b .Lb_not_taken
    b.hi .Lb_taken; b .Lb_not_taken
    b.ls .Lb_taken; b .Lb_not_taken
    b.ge .Lb_taken; b .Lb_not_taken
    b.lt .Lb_taken; b .Lb_not_taken
    b.gt .Lb_taken; b .Lb_not_taken
    b.le .Lb_taken; b .Lb_not_taken
    b    .Lb_taken; b .Lb_not_taken
    b    .Lb_taken; b .Lb_not_taken

.Lb_taken:
    ldr  w3, [x20, #4]
    sbfx x3, x3, #0, #24
    add  x20, x20, #8
    add  x20, x20, x3
    DISPATCH_DIRECT

.Lb_not_taken:
    DISPATCH
#endif
