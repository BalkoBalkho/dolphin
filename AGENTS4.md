# Dolphin Stencil Interpreter (A642A64) - Architectural Handover & Deep Technical Field Notes (AGENTS4.md)

This document is the primary technical handover and field reference for developers and successor LLM agents continuing the development of the **`A642A64` W^X Stencil Interpreter** in Dolphin.

---

## 1. Executive Context & Verified Platform Baseline

- **Target Architecture:** AArch64 (ARM64) where runtime code generation (`PROT_EXEC` / JIT) is restricted or prohibited (specifically Apple iOS/iPadOS W^X enforcement).
- **Core Constraint:** Zero runtime writable+executable memory generation. All executable host instructions must reside in a static, pre-compiled binary pool.
- **Test Device:** Samsung Galaxy S8 (Android ARM64, Exynos 8895, ID: `ce031713a871d04b0d`).
- **Target Title:** *Sin & Punishment: Star Successor* (`R2VE01`, `/sdcard/Download/Sin & Punishment - Star Successor (USA).rvz`).
- **Core Configuration Defaults Confirmed:**
  - **Dual Core (`CPUThread`):** `true` by default on Android (`Source/Core/Core/Config/MainSettings.cpp:61`). Verified via `top` (CPU thread @ 100% on big core, Video thread @ ~27% on separate core).
  - **Fastmem (`Fastmem`):** `true` by default (`Source/Core/Core/Config/MainSettings.cpp:46`).
- **Telemetry Baseline:**
  - **Wrist Strap / Static 2D Screen:** **28.1 FPS** (49% speed), 176.2 MIPS, ~52 instructions/block.
  - **Title Screen / Attract Mode (Before Tape Linking):** **1.4 FPS** (2.3% speed), 16.6 MIPS, 5.5 Million blocks/sec, ~3.1 instructions/block.

---

## 2. Why Did the Tape Exit at Every Block Boundary?

In the initial implementation of the Stencil Interpreter, the operand tape was scoped strictly to **intra-block execution**:
1. Within a single basic block, instructions are chained sequentially via 16-byte tape slots (`stencil_func_ptr`, `packed_operands`), tail-calling from one stencil to the next via `ldr x16, [x20, #16]!; br x16`.
2. When the basic block terminated (at a branch, exception check, or block end), the final tape slot was hardcoded to:
   ```armasm
   tape[2*M] = reinterpret_cast<uint64_t>(&a642a64_stencil_exit);
   ```
3. `a642a64_stencil_exit` calculated instruction count, decremented `downcount`, stored pinned registers back to memory, restored 18 callee-saved host registers from the stack frame (`d8..d15`, `x19..x28`, `x29`, `x30`), and returned control to C++ `ExecuteOneBlock()`.
4. C++ then called `EndBlock`, updated performance counters, returned to `Run()`, and executed `m_block_cache.Dispatch()` (a hash-map lookup) to locate the next basic block.

### Why This Worked on the Wrist Strap Screen (28 FPS)
On the wrist strap screen, basic blocks averaged **52 instructions per block**. The fixed overhead of C++ dispatch, hash lookup, and AAPCS stack spills (~320 host cycles) was amortized across 52 instructions (~6 cycles/inst), achieving **176.2 MIPS**.

---

## 3. The 1.4 FPS Mystery: The 2-Block Ping-Pong Polling Loop

On the title screen, performance plummeted to **1.4 FPS (16.6 MIPS)** despite the core executing **5.5 Million blocks per second** with **66.3% native stencils**.

By instrumenting `ExecuteOneBlock()` with `Common::GekkoDisassembler`, we dumped the exact PowerPC machine instructions being executed by the hardware:

```armasm
; === BLOCK A (start = 0x800380F4, 3 instructions) ===
800380F4: 881c2f04    lbz      r0, 0x2F04 (r28)    ; Load event flag A from memory
800380F8: 2c000000    cmpwi    r0, 0               ; Compare flag A against 0
800380FC: 41820018    beq-     -> 0x80038114       ; If flag A == 0, branch to Block B

; === BLOCK B (start = 0x80038114, 3 instructions) ===
80038114: 881c2f06    lbz      r0, 0x2F06 (r28)    ; Load event flag B from memory
80038118: 2c000000    cmpwi    r0, 0               ; Compare flag B against 0
8003811C: 4182ffd8    beq+     -> 0x800380F4       ; If flag B == 0, branch back to Block A
```

### Why Dolphin's Idle Detection Missed It
Dolphin has an idle skipper (`CoreTiming::Idle()` / `CheckIdle`), but `PPCAnalyst` only flags single-block self-loops (`B -> self`). Because this is a **2-block cycle** (`Block A -> Block B -> Block A`), Dolphin's static analyzer did not set `op.branchIsIdleLoop = true`.

### The Arithmetic of Disaster
- Each block contains only **3 PowerPC instructions**.
- Total PowerPC execution per block: ~3 host instructions.
- C++ dispatch & stack frame overhead per block: **~320 host instructions**.
- **Overhead ratio: 99.1%!**
- The CPU spent 5.5 Million times per second saving and restoring 18 registers and performing hash-table lookups to execute 3 instructions.
- Result: `5.5M blocks/s * 3 inst/block = 16.5 MIPS`. At Wii's 729 MHz clock, `16.5 / 729 = 2.26% speed = 1.38 FPS (~1.4 FPS)`.

---

## 4. Why Does the JIT Not Suffer From This?

In `JitArm64` / `Jit64`, basic blocks are **linked directly in executable host memory**:
1. When Block A compiles a branch to Block B, the JIT emits a direct native machine branch:
   ```armasm
   B <address_of_block_B_in_code_cache>
   ```
2. When Block B compiles, it emits a direct native machine branch:
   ```armasm
   B <address_of_block_A_in_code_cache>
   ```
3. **The JIT never returns to C++ between linked blocks.** There is zero dispatching, zero hash lookup, and zero register spilling.
4. The entire 2-block loop executes as **4 ARM64 instructions in ~2 nanoseconds**:
   ```armasm
   ldrb  w0, [x28, #0x2f04]
   cbz   w0, <block_B>
   ldrb  w0, [x28, #0x2f06]
   cbz   w0, <block_A>
   ```

---

## 5. The W^X-Compliant Fix: Inter-Block Tape Linking

In interpreters, `enableBlocklink` is disabled (`jo.enableBlocklink = false`) because traditional block linking patches executable machine code, which violates W^X policy on iOS (`PROT_EXEC` pages cannot be written).

**However, the Stencil Interpreter's tape is NOT executable machine code. The tape is purely readable/writable data memory (`uint64_t*`).**

We can link blocks together across block boundaries without writing any executable code!

### Implementation Blueprint: Tape Jump Stencils

Instead of ending a tape with `a642a64_stencil_exit`, we emit an **inter-block tape branch**:

#### A. Direct Tape Jump (`stencil_tape_jump`)
When a block ends with an unconditional branch or a linked target:
- In `stencil_trampoline.S`:
  ```armasm
  .global stencil_tape_jump
  stencil_tape_jump:
      // Word 1 contains the target block's tape header pointer
      ldr   x20, [x20, #8]         // X20 = target_block->tape_entry
      ldr   x16, [x20]            // load target stencil function pointer
      br    x16                   // enter target block directly!
  ```
- **Execution stays 100% on the host CPU in registers.**
- Zero C++ calls, zero hash table lookups, zero register spills on stack.

#### B. Conditional Tape Jump (`stencil_tape_branch_cond`)
For conditional branches (`beq`, `bne`, `bgt`, `blt`):
- Tape slot word 1 stores: `(target_tape_ptr << 16) | (condition_code)`.
- Test condition register `CR` in `X19` (or `NZCV` flags):
  - If condition is met: `mov x20, target_tape; ldr x16, [x20]; br x16`
  - If condition is not met: `STENCIL_TAILCALL` (advances to the fallthrough block or next stencil).

#### C. Downcount Check on Tape Loop Back-Edges
To ensure scheduled events and interrupts are not starved in infinite loops:
- On loop back-edges, insert `stencil_check_downcount`:
  ```armasm
  .global stencil_check_downcount
  stencil_check_downcount:
      subs  w26, w26, #10          // decrement downcount
      b.le  a642a64_stencil_exit  // if downcount <= 0, exit to C++ to process events!
      STENCIL_TAILCALL
  ```

---

## 6. Dual-Stencil Fastmem Architecture & Far-Code Locality

To maximize memory performance without risking MMIO segmentation faults or polluting the host CPU's branch predictor, Fastmem stencils are structured into a **Dual-Stencil / Far-Code Architecture**:

### A. The Branch Predictor Pressure Constraint
In our stencil interpreter, every instruction tail-calls through an indirect register jump (`br x16`). The CPU's Indirect Branch Target Buffer (BTB) is under continuous load. Adding conditional branches (`b.eq`, `b.ne`) inside hot memory stencils forces the branch prediction unit to track both direction and target histories simultaneously.

### B. The Dual-Stencil Model: Fast vs. Safe
1. **`stencil_dyn_*_fast` (Zero Branches, Zero Checks):**
   - Pure, unchecked direct Fastmem load/store:
     ```armasm
     ldr   w5, [x28, w0, uxtw]   ; Direct Fastmem load
     rev   w5, w5
     WRITE_GPR w5, w3
     STENCIL_TAILCALL            ; 0 conditional branches!
     ```
   - **Static Routing via `r1` (Stack):** In PowerPC code, >60% of all loads and stores are relative to `r1` (the stack pointer, e.g. `lwz rD, d(r1)`). The stack is guaranteed to reside in RAM (`0x80xxxxxx` / `0x81xxxxxx`) and never touches MMIO. `TryEmitStencil` routes all `rA == 1` accesses directly to `_fast`.
2. **`stencil_dyn_*_safe` (Speculatively Range-Checked):**
   - Used for non-stack, dynamic register loads/stores.
   - Tests upper address bits against GameCube/Wii physical MMIO (`0x0C000000` / `0xCC000000`).

### C. Far-Code Layout Separation (Matching Dolphin JIT)
Just as Dolphin's JIT splits code generation into **Near Code** (hot execution path) and **Far Code** (cold exception handlers and MMIO routines):
- **Near Stencil Pool:** Contains `_fast` and the speculative fallthrough of `_safe`. Placed contiguously in memory so hot loops stay densely packed in L1 Instruction Cache lines (32KB–128KB).
- **Far Stencil Pool (`.text.far`):** Cold slow-path MMIO handlers and C++ fallback trampolines are placed in a physically distant far-code section, ordered by MMIO device access.
- When `_safe` detects an MMIO address, it branches out of line into Far Code:
  ```armasm
  .section .text.near
  stencil_dyn_lwz_safe:
      and   w1, w0, #0x3F000000
      cmp   w1, #0x0C000000
      b.eq  stencil_dyn_lwz_mmio_far   ; Unlikely branch to Far Code!
      ldr   w5, [x28, w0, uxtw]        ; Fast path remains contiguous in L1I
      rev   w5, w5
      WRITE_GPR w5, w3
      STENCIL_TAILCALL

  .section .text.far
  stencil_dyn_lwz_mmio_far:
      ; Out-of-line slow MMIO fallback handler (does not pollute L1I cache)
      b     a642a64_stencil_exit_mmio
  ```

### D. Dynamic Tape Backpatching (100% W^X Compliant)
Because the operand tape lives in writable data memory (`uint64_t* tape`):
- If an instruction begins with `_safe` and profiling proves it never accesses MMIO, the runtime can dynamically overwrite that tape slot with the pointer to `_fast`:
  ```cpp
  tape[slot] = reinterpret_cast<uint64_t>(&stencil_dyn_lwz_fast);
  ```
- This achieves JIT-style dynamic specialization with **zero machine code modifications and zero W^X violations**.

---

## 7. Successor Agent Action Plan & Priorities

1. **Priority 1 (Immediate Blocker — Resolves 1.4 FPS Polling Trap):**
   - Implement `stencil_tape_jump` in `stencil_trampoline.S`.
   - Wire inter-block tape linking in `StencilInterpreterBlockCache` and `StencilInterpreter.cpp`.
   - Verify that the 2-block ping-pong loop (`0x800380f4` <-> `0x80038114`) chains across the tape without returning to C++, restoring title screen framerate from **1.4 FPS** to **30–60 FPS**.
2. **Priority 2 (Fastmem Memory Stencils & Far-Code Layout):**
   - Implement `_fast` (stack `r1`) and `_safe` memory stencils in `stencil_trampoline.S`.
   - Organize slow-path MMIO fallbacks into a separate `.text.far` cold section.
   - Wire memory opcodes (32 `lwz`, 36 `stw`, 34 `lbz`, 38 `stb`, 40 `lhz`, 44 `sth`) into `TryEmitStencil`, raising the native stencil execution ratio from **66% to >85%**.

