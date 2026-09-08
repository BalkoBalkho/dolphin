# Dolphin Stencil Interpreter (A642A64) - Architecture & LLM Agent Guide

This document is for LLMs and developers working on the `A642A64` no-JIT stencil interpreter engine in Dolphin.

---

## 1. Project Goal & Constraints
- **Goal:** High-performance PowerPC emulation on ARM64 targets where runtime code generation (JIT) is prohibited or restricted (specifically iOS/iPadOS with W^X enforcement).
- **Core Constraint:** Zero runtime writable+executable memory generation. No JIT compiler (`mprotect(PROT_EXEC)` on runtime-generated host code is forbidden on iOS).
- **Technique:** Tail-called pre-assembled binary stencils chained together via an operand tape.

---

## 2. Environment Variables & Toolchain Locations

Add these to your environment (`~/.bashrc` or shell export) for Android builds, debugging, and deployments:

```bash
export ANDROID_HOME=/home/oneeb/Android/Sdk
export ANDROID_SDK_ROOT=/home/oneeb/Android/Sdk
export JAVA_HOME=/home/oneeb/.local/share/JetBrains/Toolbox/apps/android-studio/jbr
export PATH=$PATH:$ANDROID_HOME/platform-tools:$JAVA_HOME/bin
```

- **ADB Location:** `/home/oneeb/Android/Sdk/platform-tools/adb` (add to `$PATH` via `$ANDROID_HOME/platform-tools`)
- **Gradle Location / Wrapper:** `<repo>/Source/Android/gradlew`
- **Build APK:**
  ```bash
  export JAVA_HOME=/home/oneeb/.local/share/JetBrains/Toolbox/apps/android-studio/jbr && ./Source/Android/gradlew -p Source/Android assembleDebug
  ```
- **Install APK:**
  ```bash
  /home/oneeb/Android/Sdk/platform-tools/adb install -r Source/Android/app/build/outputs/apk/debug/app-debug.apk
  ```
- **Launch & Run Target Title:**
  ```bash
  /home/oneeb/Android/Sdk/platform-tools/adb shell "am force-stop org.dolphinemu.dolphinemu.debug && logcat -c" && \
  /home/oneeb/Android/Sdk/platform-tools/adb shell 'am start -n org.dolphinemu.dolphinemu.debug/org.dolphinemu.dolphinemu.activities.EmulationActivity --esa SelectedGames "/sdcard/Download/Sin & Punishment - Star Successor (USA).rvz"'
  ```

---

## 3. Execution Architecture & Tape Calling Convention

Each basic block is converted into a sequential execution tape:
- **Tape Slot (16 bytes / 2 `uint64_t` words per instruction):**
  - Word 0: `stencil_code_ptr` (64-bit function pointer to the stencil)
  - Word 1: `operand_data` (64-bit packed immediate and register descriptors)
- **Tape Layout:**
  ```
  tape[0] = 0 (entry padding)
  tape[1] = operand_0
  tape[2] = &stencil_1
  tape[3] = operand_1
  ...
  tape[2*M] = &a642a64_stencil_exit
  tape[2*M + 1] = 0
  ```

### Host ARM64 Register Mapping
| ARM64 Register | Role |
| :--- | :--- |
| `X19` | `PowerPC::PowerPCState*` base pointer |
| `X20` | Current tape pointer (`uint64_t* tape`) |
| `X28` | Memory base (`ppc_state.mem_ptr`) |
| `W23` | Pinned PPC Register `r0` |
| `W21` | Pinned PPC Register `r3` |
| `W22` | Pinned PPC Register `r4` |
| `W24` | Pinned PPC Register `r5` |
| `W25` | Pinned PPC Register `r31` |
| `X16` | Intra-block tail-call branch target (IP0) |

### Tail-Call Mechanism
Every stencil completes with:
```armasm
ldr x16, [x20, #16]!
br  x16
```
This increments `X20` by 16 bytes to the next instruction slot and jumps directly to the next stencil without any C++ stack frames or function returns.

The final tape entry points to `a642a64_stencil_exit`:
1. Stores pinned registers (`w23, w21, w22, w24, w25`) back to memory.
2. Restores AAPCS64 callee-saved registers (`x19-x28`, `d8-d15`, `x29`, `x30`).
3. Returns to C++ (`ExecuteOneBlock`).

---

## 4. Dynamic Register Handling (`stencil_dyn_*`)

Because 32-register 3-operand permutation tables would exceed gigabytes, stencils use a hybrid model:
1. **Pre-compiled Pinned Stencils:** Used when all instruction operands happen to fall within the pinned register set $\{r0, r3, r4, r5, r31\}$.
2. **Dynamic Fallback Stencils:** When any operand register is outside the pinned set, execution branches to `stencil_dyn_*` in `stencil_trampoline.S`.
   - `READ_GPR dst, reg_idx`: If `reg_idx` matches a pinned register, reads from `w21..w25`; otherwise loads from `[X19, #(32 + reg*4)]`.
   - `WRITE_GPR src, reg_idx`: Updates both the pinned register (if matched) and writes to memory `[X19, #(32 + reg*4)]`.
   - Both precompiled and dynamic stencils use identical tape tail-calls, allowing seamless intermixing in a single tape.

---

## 5. Critical PowerPC Architecture Trap: Register Inversion

> [!WARNING]
> **Do not confuse PowerPC instruction fields!**
> - For arithmetic (`add`, `subf`, `addi`):
>   - $rD$ is **Destination**.
>   - $rA$ and $rB$ are **Sources**.
> - For logical and shift/rotate instructions (`or`, `mr`, `and`, `xor`, `nor`, `slw`, `srw`, `sraw`, `srawi`, `rlwinm`):
>   - **$rA$ is the DESTINATION!**
>   - **$rS$ (stored in Dolphin's `op.inst.RD` field) is the SOURCE!**
>   - $rB$ is the second source.
>   - Example: `mr r3, r4` is encoded as `or 3, 4, 4` ($rA=3, rS=4, rB=4$). If $rD$ is treated as destination, $r4$ is overwritten with $r3$ instead!

---

## 6. Roadmap & Bottleneck Removal for LLM Contributors

1. **Fix Operand Order:**
   - Correct $rA \leftrightarrow rD$ mapping for all logical, shift, and rotate operations in `TryEmitStencil`.
2. **Branch & Compare In-Tape Chaining:**
   - Implement `cmp`, `cmpl`, `cmpi` stencils to set condition registers (`cr0..cr7`).
   - Allow intra-block conditional branches (`beq`, `bne`, `bgt`, etc.) to jump forward/backward along the tape by adjusting `X20` without returning to C++.
3. **Paired-Single / FPU Stencils:**
   - Map PowerPC single and paired-single float operations (`ps_add`, `ps_mul`, `fadd`, `fmul`, etc.) directly to ARM64 NEON vector registers.
4. **Fast Memory Load/Store via EMM Signal Handler:**
   - Re-enable stenciled memory operations once `StencilInterpreter::HandleFault` is implemented to recover context from fastmem segmentation faults.

---

## 7. Agent Efficiency Guideline: Never Read Full PNG Screenshots

- **Rule:** Never use the `view_file` or image viewer tool on raw `.png` screenshots.
- **Rationale:** A full-resolution screenshot (e.g. 1480x720) burns ~1,200 vision tokens per call. Inspecting a dozen screenshots depletes context window budgets rapidly.
- **Preferred Method:** Use OCR via Tesseract:
  ```bash
  adb shell screencap -p /data/local/tmp/screen.png && adb pull /data/local/tmp/screen.png /tmp/screen.png && \
  TESSDATA_PREFIX=/mnt/sparentfs/code/source/tryow2/TabwatchRPC/x64/Debug/tessdata tesseract /tmp/screen.png stdout
  ```
  Or crop targeted bounding boxes (e.g., top-left FPS overlay) using Python PIL before reading. Text output costs only ~15 tokens (>98% reduction).

