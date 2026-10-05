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

---

## 8. Lockstep Divergence Debugging & Mobile FPS Diagnosis (AGENTS Session Handover)

### 8.1 Confusions Encountered & Concrete Root Causes During 1,000,000 Step Parity

During lockstep parity verification between `StencilInterpreter` (Core 6) and `CachedInterpreter` (Core 5) using `divergence_detector.py`:

1. **Confusion 1: Divergence at Step 82 with Block Linking Enabled**
   - **Symptom:** As soon as `JITNoBlockLinking = False`, execution diverged at block 82 with completely different PCs and GPRs.
   - **Root Cause:** `DivergenceTracer::LogBlock` was originally placed *outside* the `while(true)` dispatch loop in `ExecuteOneBlock()`. When `ExecuteBlockLinkBranch` chained to the next block, it executed multiple blocks in a single C++ call before returning, skipping intermediate basic block trace logs and comparing Block 1 in JIT against Block 15 in Interpreter.
   - **Solution:** Moved `LogBlock` directly to block exits: `ExecuteBlockLinkDirect`, `ExecuteBlockLinkBranch`, and `EndBlock`.

2. **Confusion 2: Divergence at Step 16,335 in OSGetTime**
   - **Symptom:** Execution diverged at `0x803426fc` with `r4` (Time Base Lower) mismatching.
   - **Root Cause:** An experimental 2-block ping-pong idle loop detector (`tl_ping_pong_count >= 16`) inside `ExecuteBlockLinkBranch` was calling `GetCoreTiming().Idle()` after 16 iterations of any 2-block branch, advancing the cycle counter prematurely. Right after, `OSGetTime()` ran `mftbu r4`, reading a skewed Time Base.
   - **Solution:** Removed the artificial 16-iteration threshold from `ExecuteBlockLinkBranch`.

3. **Confusion 3: Intermittent Parity Failure at Step 16,337 by Exactly 40,500,000 Cycles (1.0 Second)**
   - **Symptom:** In some test runs, `r5` and `r29` diverged at step 16,337 by exactly `0x269fb20` (40,500,000 cycles = 1.0 second), while in other runs it passed 100,000+ steps with 0 divergence.
   - **Root Cause:** `CEXIIPL::GetEmulatedTime()` in `EXI_DeviceIPL.cpp`, `EXI.cpp`, and `GCMemcardRaw.cpp` queried the host OS real-time clock via `Common::Timer::GetLocalTimeSinceJan1970()` without honoring `MAIN_CUSTOM_RTC_ENABLE` / `MAIN_CUSTOM_RTC_VALUE`. When Instance A and Instance B were spawned across a 1-second host clock boundary, one instance started with a 1-second offset.
   - **Solution:** Updated `CEXIIPL::GetEmulatedTime()`, `EXI.cpp`, and `GCMemcardRaw.cpp` so that when `MAIN_CUSTOM_RTC_ENABLE` is active, emulated time strictly starts from `CustomRTCValue` and advances purely deterministically based on emulated CPU ticks (`ticks / ticks_per_sec`).

4. **Confusion 4: Divergence at Step 693,545 (`PC=0x00000888` vs `PC=0x00000800`)**
   - **Symptom:** At step 693,544, block `0x802aecbc` executed `lfs` with `MSR.FP == 0`, raising an FP Unavailable exception. CachedInterpreter logged `0x00000800`, while StencilInterpreter logged `0x00000888`.
   - **Root Cause:** In `CachedInterpreter`, when `CheckFPU` returns 0, the dispatch loop breaks and `LogBlock` logs the exception exit PC (`0x00000800`). In `StencilInterpreter`, `CheckFPU` returned 0 without calling `LogBlock`, so the exception block's exit was never logged, causing the trace streams to shift by 1 block.
   - **Solution:** Added `DivergenceTracer::LogBlock` inside `CheckFPU`, `CheckBreakpoint`, and `InterpretAndCheckExceptions` when an exception alters PC and aborts the block.
   - **Result:** **1,000,000 basic blocks reached with ZERO divergence.**

---

### 8.2 Why the Phone Gets 30 FPS on Safety Screen and 2.8 FPS on Title Screen

1. **Safety Screen (30 FPS):**
   - The Wii safety/wrist-strap screen is a simple 2D static scene with minimal CPU load and low vertex counts. The PowerPC CPU easily completes all frame logic within the allocated slice and syncs to VSync (30 FPS cap).
2. **Title Screen (2.8 FPS) & The 2-Block Idle Polling Bottleneck:**
   - On the 3D title screen, *Sin & Punishment* enters a tight 2-block polling loop (`0x800380F4` <-> `0x80038114`) waiting for Wiimote input and VBlank timers.
   - When we had the 2-block ping-pong detector calling `CoreTiming::Idle()`, the CPU yielded immediately upon detecting the wait loop, and the title screen reached **8.76 FPS (27.4 VPS)**.
   - In order to pass the 1,000,000 step lockstep test against CachedInterpreter (which has no multi-block idle detector), we disabled that ping-pong idle skip.
   - Without idle skipping, the CPU executes all **6,300,000+ basic blocks per second** (`[StencilBench] Speed: 34.0% | MIPS: 248.2 | Blocks/s: 6.3M`) in that tight polling loop, completely starving the frame budget and dragging title screen rendering down to 2.8 FPS.
3. **Path Forward to Recover 8.8+ FPS on Mobile:**
   - Gate the multi-block idle skip on `!Core::WantsDeterminism()` or a configuration setting (e.g. `SyncOnSkipIdle` / `JITIdleLoopOptimization`), so divergence testing runs 100% deterministic lockstep, while real gameplay on device uses idle skipping to fast-forward polling loops to 8.8+ FPS.

---

## 9. Comprehensive Field Notes: Stencil Interpreter, Divergence Architecture, & Android GFX (AGENTS Handover)

### 9.1 Divergence Testing Tooling & CPUCore Trap
- **Core Identifiers in Dolphin:**
  - `CPUCore::Interpreter = 0`
  - `CPUCore::JIT64 = 1`
  - `CPUCore::JITARM64 = 4`
  - `CPUCore::CachedInterpreter = 5`
  - `CPUCore::StencilInterpreter = 6`
- **Trap Encountered:**
  - When invoking `divergence_detector.py`, running `--jit-core 1 --interp-core 1` tests upstream x86 JIT against itself, completely bypassing the new stencil interpreter.
  - To test the new StencilInterpreter under lockstep parity, **always specify `--jit-core 6 --interp-core 5`**.
  - Host binary `build/Binaries/dolphin-emu-nogui` must be rebuilt after modifying `StencilInterpreter.cpp` (`make -C build dolphin-nogui -j$(nproc)`).
- **Triad Lockstep Mode (`--triad`):**
  - Spawns three instances: Target (Core 6) + Ref 1 (Core 5) + Ref 2 (Core 5).
  - If Ref 1 and Ref 2 diverge on an instruction (e.g., non-deterministic OS delay loops reading `mftb`/TimeBase, or asynchronous hardware timers), the script logs `[SKIP] DIVERGENT INSTRUCTION HYPOTHESIS` and skips past it without aborting execution.

### 9.2 Critical AArch64 vs Gekko PowerPC Semantic Differences
1. **The `ps_msub` / `fmsub` Sign Inversion Trap:**
   - **Gekko PowerPC Definition:** $FD = (FA \times FC) - FB$.
   - **AArch64 `fmsub` Definition:** `fmsub dD, dN, dM, dA` evaluates to $dA - (dN \times dM) = -((dN \times dM) - dA)$.
   - Using `fmsub` in AArch64 produces the negated result ($-36$ instead of $+36$).
   - **Solution:** Use AArch64 **`fnmsub`**, which evaluates to $(dN \times dM) - dA = (FA \times FC) - FB$.
2. **3-Operand Gekko Packing Order:**
   - In Gekko opcodes with 3 float/paired operands (`ps_madd`, `ps_msub`, `fmadds`, `fmsubs`, `fsel`):
     ```text
     packed = (FD << 24) | (FA << 16) | (FB << 8) | (FC)
     ```
   - Bits 24..28: $FD$ (Destination).
   - Bits 16..20: $FA$ (First source / multiplier).
   - Bits 8..12: $FB$ (Second source / addend or subtrahend).
   - Bits 0..4: $FC$ (Third source / multiplier).
3. **`fsel` Truth-Value Argument Mapping:**
   - PowerPC: `fsel FD, FA, FC, FB` means `if (FA >= 0.0) FD = FC; else FD = FB;`.
   - In AArch64: `fcmp d0, #0.0` followed by `fcsel d0, d1, d2, ge` ($d1 = FC, d2 = FB$).
4. **`PairedSingle` Register Representation in Dolphin Memory:**
   - In `PowerPCState.ps[32]`, each register is a 16-byte `PairedSingle` composed of two 64-bit IEEE doubles:
     - `ps0` at offset `#0` (64 bits).
     - `ps1` at offset `#8` (64 bits).
   - **OP59 Single-Precision Scalar FP (`fadds`, `fsubs`, `fmuls`, `fdivs`, `fmadds`, `fmsubs`):**
     - Reads `FA.ps0` and `FB.ps0` (and `FC.ps0`) as 64-bit doubles.
     - Performs arithmetic, rounds result to single-precision float (`ForceSingle`), then converts back to 64-bit double.
     - **CRITICAL:** Upstream Dolphin `fadds` executes `ppc_state.ps[FD].Fill(result);`, copying the result to **both** `ps0` and `ps1`.
     - In assembly: `stp d0, d0, [x9]`. Never load a 32-bit `s` register directly from `[x7]` without loading the full double `d` and converting.

### 9.3 Android GFX & Framebuffer Corruption (The Pink Screen Issue)
- **Symptom:** Emulation displays a solid purple/magenta (`RGBA 183, 0, 236`) screen with a green line (`RGBA 0, 135, 0`) at the bottom.
- **Root Cause:**
  - GameCube Video Interface (VI) framebuffers in RAM are formatted in YUYV (BT.601).
  - Uninitialized or uncopied RAM containing all `0xFF` bytes decodes under BT.601 YUV-to-RGB conversion as `Y=0, Cb=255, Cr=255 -> RGBA (178, 0, 225)` (magenta/purple). Memory containing `0x00` decodes as `Y=0, Cb=0, Cr=0 -> RGBA (0, 135, 0)` (green).
  - Overriding `GFX.ini` with:
    ```ini
    [Hacks]
    ImmediateXFBEnable = False
    EFBToTextureEnable = True
    FastDepthCalc = True
    EFBAccessEnable = False
    ```
    breaks EFB-to-RAM copies that *The Legend of Zelda: Twilight Princess* requires for its external framebuffer (XFB).
  - **Resolution:** Remove the corrupted `GFX.ini` override from `/sdcard/Android/data/org.dolphinemu.dolphinemu.debug/files/Config/GFX.ini` to let Dolphin use native XFB handling.
