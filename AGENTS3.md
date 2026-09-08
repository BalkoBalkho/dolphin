# Dolphin Stencil Interpreter (A642A64) - Architecture & LLM Agent Guide (Part 3)

This document records the architectural discoveries, memory layout constraints, register-access optimizations, condition-register emulation details, and benchmarking metrics established during the implementation of the `A642A64` stencil interpreter.

---

## 1. Branchless Direct-Access Dynamic Register Architecture

### The Problem with Conditional Branch Pinned Emulation
Originally, dynamic stencils (`READ_GPR` and `WRITE_GPR`) tested whether every register operand matched pinned registers `0, 3, 4, 5, 31`:
```armasm
// Anti-pattern: up to 15 branches per 3-register instruction!
cmp  \reg_idx, #3; b.eq 103f
cmp  \reg_idx, #4; b.eq 104f
cmp  \reg_idx, #5; b.eq 105f
cmp  \reg_idx, #0; b.eq 100f
cmp  \reg_idx, #31; b.eq 131f
```
With 2 source registers and 1 destination, every single PowerPC ALU instruction executed up to **15 conditional branches**, destroying the host CPU's branch predictor (BTB).

### The Solution: Dedicated `X27` L1 Cache Pointer
By reserving callee-saved register `X27` as the base address of `ppc_state.gpr` (`add x27, x19, #32`):
```armasm
.macro READ_GPR dst, reg_idx
    ldr   \dst, [x27, \reg_idx, uxtw #2]
.endm

.macro WRITE_GPR src, reg_idx
    str   \src, [x27, \reg_idx, uxtw #2]
.endm
```
- **0 branches, 0 compares, 0 branch mispredictions.**
- Hits the ARM64 L1 D-cache in 1 cycle (the entire PPC 32-register bank is only 128 bytes, fitting entirely in two 64-byte L1 cache lines).
- At block exit, `a642a64_stencil_exit` synchronizes `pinned[5]` directly from `[x27]`, keeping the C++ caller perfectly coherent.

---

## 2. Condition Register (CR) Emulation in Native Assembly

Dolphin stores the Condition Register in a non-standard 64-bit format per field in `ppc_state.cr.fields[0..7]` (`Source/Core/Core/PowerPC/ConditionRegister.h`):
- `EQ`: Set if lower 32 bits are `0`.
- `GT`: Set if signed `(s64)cr_val > 0`.
- `LT`: Set if bit 62 is set.
- `SO`: Set if bit 59 is set.

### Assembly Compare Stencil Pattern (`cmplwi`, `cmpwi`, `cmpl`, `cmp`)
In `stencil_trampoline.S`:
1. Test operands using host `cmp` (unsigned `cmp` for logical, signed `cmp` for arithmetic).
2. Construct the 4-bit PowerPC flag nibble: `(LT=8 | GT=4 | EQ=2) | XER.SO`.
3. Load the precomputed 64-bit internal representation from `g_cr_table` (matching `ConditionRegister::s_crTable`).
4. Store directly into `ppc_state.cr.fields[CRFD]` (`str x8, [x19, #(672 + CRFD*8)]`).

### Verified Compiler Struct Offsets
Verified with compile-time `static_assert`:
```cpp
offsetof(PowerPC::PowerPCState, gpr)        == 32
offsetof(PowerPC::PowerPCState, cr.fields)  == 672
offsetof(PowerPC::PowerPCState, xer_so_ov)  == 757
```

---

## 3. Fastmem & Signal Recovery Constraints

Memory operations (`lwz`, `stw`, `lbz`, etc.) **cannot** simply perform raw unhandled loads/stores against `X28` (`mem_base`):
- GameCube/Wii address space includes MMIO ranges (`0xCC00xxxx` for Audio, Video, Command Processor, DVD).
- A raw ARM64 `ldr/str` against unmapped memory or MMIO triggers a host `SIGSEGV`.
- **Requirement for Stenciled Fastmem:** To re-enable memory operations on the stencil tape, `StencilInterpreter::HandleFault` must be implemented using Android's `ucontext_t` signal handler to catch faults, identify the faulting instruction, handle MMIO via C++ fallback, and resume the tape.

---

## 4. Benchmark History on Target Device (Galaxy S8 / Exynos 8895)

Target Title: *Sin & Punishment: Star Successor* (Wii)

| Configuration | Native Stencil Ratio | Emulation Speed | Screen FPS | VPS | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Cached Interpreter (`CPUCore=5`)** | 0% | ~25–27% | ~15.8 FPS | ~15.8 VPS | Baseline interpreter. |
| **Stencil Interpreter (Early)** | ~26.5% | ~27% (drops to 3%) | ~7.5 FPS | ~14.5 VPS | Throttled by logcat flooding in `CheckExceptions`. |
| **Stencil + Direct `X27` GPR Access** | ~26.8% | ~27%–50% | ~15.8–29.8 FPS | ~15.8–29.8 VPS | Clean logcat + branchless register reads/writes. |
| **Stencil + Native Compares (`cmp*`)** | **63.8%** | **49%** | **28.1 FPS** | **28.2 VPS** | Tape unbroken across comparisons. Over 440M native stencils executed. |

---

## 5. Vision / Image Processing Token Economics & OCR Guidance

### How PNG Images Are Read by the Agent
When calling `view_file` on a `.png` or image file, the file is read as raw binary, base64-encoded, and passed directly to the multimodal visual encoder of the underlying LLM.

### Token Cost Breakdown
- High-resolution images (e.g., 1480x720 mobile screenshots) are dynamically tiled into multiple **256x256 or 512x512 visual patches**.
- A full-screen 720p screenshot costs approximately **800 to 1,600 vision tokens** per image inspection.
- Calling `view_file` on 10 consecutive screenshots consumes **~10,000–16,000 tokens**, which quickly fills context windows and increases latency.

### Should You Install Tesseract?
**Yes, installing `tesseract-ocr` is strongly recommended for text/metrics reading:**
- **Why:** Instead of passing whole images to the LLM vision model just to read framerate numbers, a quick shell command:
  ```bash
  convert /sdcard/screen.png -crop 300x150+1180+0 /tmp/crop.png && tesseract /tmp/crop.png stdout --psm 6
  ```
  extracts `Speed: 49% FPS: 28.06 VPS: 28.15` as plain ASCII text.
- **Token Efficiency:** A text string costs **~15 tokens**, compared to **~1,200 tokens** for an image—a **98.8% token reduction**!
- Install via: `sudo apt-get install -y tesseract-ocr imagemagick`
