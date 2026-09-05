#if defined(__aarch64__) || defined(_M_ARM64)
.text
.align 4

// extern "C" void a642a64_run_stencil(
//     const void* stencil_bytes, // x0
//     void* tape,                // x1
//     void* ppc_state,           // x2
//     void* mem_base,            // x3
//     uint32_t* pinned_gprs      // x4: in/out array [r0, r3, r4, r5, r31]
// );

.global a642a64_stencil_exit
a642a64_stencil_exit:
    // Store modified pinned registers back to x4
    ldr  x4, [sp, #144]
    str  w23, [x4, #0]   // r0
    str  w21, [x4, #4]   // r3
    str  w22, [x4, #8]   // r4
    str  w24, [x4, #12]  // r5
    str  w25, [x4, #16]  // r31

    // Restore callee-saved registers
    ldp  d8,  d9,  [sp, #0]
    ldp  d10, d11, [sp, #16]
    ldp  d12, d13, [sp, #32]
    ldp  d14, d15, [sp, #48]
    ldp  x19, x20, [sp, #64]
    ldp  x21, x22, [sp, #80]
    ldp  x23, x24, [sp, #96]
    ldp  x25, x26, [sp, #112]
    ldp  x27, x28, [sp, #128]
    add  sp, sp, #160
    ldp  x29, x30, [sp], #16
    ret

.global a642a64_run_stencil
.type a642a64_run_stencil, %function
a642a64_run_stencil:
    // 1. Preserve AAPCS64 callee-saved registers & frame pointer
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    sub  sp, sp, #160
    stp  d8,  d9,  [sp, #0]
    stp  d10, d11, [sp, #16]
    stp  d12, d13, [sp, #32]
    stp  d14, d15, [sp, #48]
    stp  x19, x20, [sp, #64]
    stp  x21, x22, [sp, #80]
    stp  x23, x24, [sp, #96]
    stp  x25, x26, [sp, #112]
    stp  x27, x28, [sp, #128]
    str  x4,       [sp, #144] // save pinned_gprs array pointer

    // 2. Load pinned register values into ARM64 registers
    // Pinned: 0->r0 (X23), 1->r3 (X21), 2->r4 (X22), 3->r5 (X24), 4->r31 (X25)
    ldr  w23, [x4, #0]   // r0
    ldr  w21, [x4, #4]   // r3
    ldr  w22, [x4, #8]   // r4
    ldr  w24, [x4, #12]  // r5
    ldr  w25, [x4, #16]  // r31

    // 3. Set environment registers
    mov  x19, x2         // ppc_state
    mov  x20, x1         // tape
    mov  x28, x3         // mem_base

    // 4. Branch to stencil
    br   x0

#endif
