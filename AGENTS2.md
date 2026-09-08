# Dolphin Stencil Interpreter (A642A64) - Comprehensive Handover & Field Notes (AGENTS2.md)

This document contains full operational knowledge, debugging breakthroughs, active root-cause analyses, and roadmap instructions for subsequent AI agents working on the **`A642A64` W^X Stencil Interpreter** for Dolphin on ARM64.

---

## 1. Executive Summary & Environment

- **Target Architecture:** ARM64 (AArch64) without runtime code generation permissions (Apple iOS/iPadOS W^X compliance, tested on Samsung Galaxy S8 Android ARM64 Exynos 8895).
- **Emulated Target:** Nintendo Wii / GameCube PowerPC 750CL ("Broadway" / "Gekko").
- **Current Test Target:** *Sin & Punishment: Star Successor* (`R2VE01`, `/sdcard/Download/Sin & Punishment - Star Successor (USA).rvz`).
- **Core Engine:** `Source/Core/Core/PowerPC/StencilInterpreter/StencilInterpreter.cpp` & `Externals/A642A64/`.
- **Primary Toolchains:**
  - Android SDK ADB: `/home/oneeb/Android/Sdk/platform-tools/adb` (Device ID: `ce031713a871d04b0d`)
  - JDK: `/home/oneeb/.local/share/JetBrains/Toolbox/apps/android-studio/jbr`
  - Build Command: `export JAVA_HOME=/home/oneeb/.local/share/JetBrains/Toolbox/apps/android-studio/jbr && ./Source/Android/gradlew -p Source/Android assembleDebug`
  - Install Command: `/home/oneeb/Android/Sdk/platform-tools/adb install -r Source/Android/app/build/outputs/apk/debug/app-debug.apk`
  - Launch Command:
    ```bash
    /home/oneeb/Android/Sdk/platform-tools/adb shell "am force-stop org.dolphinemu.dolphinemu.debug && logcat -c" && \
    /home/oneeb/Android/Sdk/platform-tools/adb shell 'am start -n org.dolphinemu.dolphinemu.debug/org.dolphinemu.dolphinemu.activities.EmulationActivity --esa SelectedGames "/sdcard/Download/Sin & Punishment - Star Successor (USA).rvz"'
    ```

---

## 2. Solved Traps & Critical Bug Fixes

### A. The "0.00 FPS / Complete Freeze" Mystery (SOLVED)
- **Symptom:** Stencil interpreter appeared to hang at 0.00 FPS with 0% CPU usage, whereas pure interpreter achieved ~0.5 FPS.
- **Root Cause:** When an alert or warning occurs (e.g. `unknown_instruction` or memory exception), Android's [`NativeLibrary.kt`](file:///home/oneeb/Documents/code/android/dolphin/Source/Android/app/src/main/java/org/dolphinemu/dolphinemu/NativeLibrary.kt#L514) executes `alertMessageSemaphore.acquire()`, indefinitely halting the CPU emulation thread until an interactive user touch dismisses the modal dialog.
- **Fix:** Permanently defaulted `UsePanicHandlers = false` across the codebase:
  1. [`Source/Core/Core/Config/MainSettings.cpp:432`](file:///home/oneeb/Documents/code/android/dolphin/Source/Core/Core/Config/MainSettings.cpp#L432): changed default to `false`.
  2. [`Source/Android/.../BooleanSetting.kt:159`](file:///home/oneeb/Documents/code/android/dolphin/Source/Android/app/src/main/java/org/dolphinemu/dolphinemu/features/settings/model/BooleanSetting.kt#L159): changed default to `false`.
  3. `/sdcard/Android/data/org.dolphinemu.dolphinemu.debug/files/Config/Dolphin.ini`: set `UsePanicHandlers = False` under `[Interface]`.

---

### B. `PowerPCState` GPR Struct Offset Mismatch (SOLVED)
- **Symptom:** Register `r13` (small data area pointer) read `0x00000000`, causing immediate crash when referencing data at `0xffffaaf8`.
- **Root Cause:** In [`stencil_trampoline.S`](file:///home/oneeb/Documents/code/android/dolphin/Externals/A642A64/src/stencil_trampoline.S), `READ_GPR` and `WRITE_GPR` used `add x15, x19, #40`.
- **Actual `PowerPCState` Struct Layout ([`PowerPC.h`](file:///home/oneeb/Documents/code/android/dolphin/Source/Core/Core/PowerPC/PowerPC.h#L121)):
  ```cpp
  u32 pc;                      // offset 0  (4 bytes)
  u32 npc;                     // offset 4  (4 bytes)
  u8* stored_stack_pointer;    // offset 8  (8 bytes)
  u8* gather_pipe_ptr;         // offset 16 (8 bytes)
  u8* gather_pipe_base_ptr;    // offset 24 (8 bytes)
  u32 gpr[32];                 // offset 32 (0x20) (128 bytes)
  ```
- **Fix:** Restored `add x15, x19, #32` in `READ_GPR` and `WRITE_GPR`. Registers `r1` (`0x804c68c0`), `r2` (`0x804ba920`), and `r13` (`0x804b7700`) are now correctly read and written.

---

### C. PPC `addi`/`addis` Immediate Zero ($rA == 0$) Semantics (SOLVED)
- **Symptom:** Register values corrupted whenever `li` (`addi rD, 0, imm`) or `lis` (`addis rD, 0, imm`) executed.
- **PowerPC Specification Rule:** If $rA == 0$, the value $0$ is used, **NOT** the contents of register $r0$.
- **Bug in Generator:** In `gen_stencils.cpp`, static stencils treat pinned index 0 ($r0$) as literal 0 (`c.MOV(wD, W0)`). But pinned index 0 is actually PPC register $r0$ ($W23$).
- **Fix:** In `TryEmitStencil`:
  ```cpp
  if (op.inst.RA != 0 && rd_idx != -1 && ra_idx != -1)
  {
      // Safe to use static stencil
  }
  ```
  When $rA == 0$, instructions route through `stencil_dyn_addi` and `stencil_dyn_addis`, which test `cbz w2, 1f` to safely supply numeric 0.

---

### D. Carry Flag ($XER.CA$) Memory Offset in `gen_stencils.cpp` (CRITICAL CAVEAT)
- **Bug in Generator:**
  In [`Externals/A642A64/src/gen_stencils.cpp:81-88`](file:///home/oneeb/Documents/code/android/dolphin/Externals/A642A64/src/gen_stencils.cpp#L81-L88):
  ```cpp
  inline void LoadCarry(VectorCodeGenerator& c) {
      c.LDRB(W0, X19, 288);
      c.CMP(W0, 1);
  }
  inline void SaveCarry(VectorCodeGenerator& c) {
      c.CSET(W1, CS);
      c.STRB(W1, X19, 288);
  }
  ```
- **The Catch:**
  On ARM64 (`#ifndef _M_X86_64`), `PairedSingle ps[32]` is positioned at offset **160 to 672**. Offset **288** is in the middle of `ps[8]`!
  The true offset of `xer_ca` on ARM64 is **696** (`PPCSTATE_OFF(xer_ca)`).
- **Status:** Static stencils for `addc`, `adde`, `subfe` are currently **NOT** enabled in `TryEmitStencil`, preventing this bug from corrupting execution right now. **Do NOT enable static stencils that use `LoadCarry`/`SaveCarry` until `gen_stencils.cpp` is re-generated with offset 696!**

---

## 3. Active Investigation: The Untranslated PC / Exception Loop

### Current Log Output
```
CRASH DETECTED in block 0042844c! ending PC=00428450, SRR0=800041c0, SRR1=00000030, DAR=00000000, DSISR=00000000, LR=801a7c08, MSR=00000000
IntCPU: Unknown instruction 00000000 at PC = 00428450 last_PC = 8132ffc8 LR = 801a7c08
r0: 0x801a7bf4 r1: 0x804c6878 r2: 0x00000000 r3: 0x0000000e
r8: 0x804c6868 r9: 0x0011c664 r10: 0x0011c66c r11: 0x00000000
r12: 0x817fe2e0 r13: 0x804b7700
r30: 0x80429ec0 r31: 0x803119c8
```

### Forensic Analysis of the Crash State
1. **$SRR1 = 0x00000030$:**
   - PowerPC Bit 4 ($0x10$) = `DR` (Data Relocation active).
   - PowerPC Bit 5 ($0x20$) = `IR` (Instruction Relocation active).
   - Proves the system was executing in valid translated virtual space before the exception.
2. **$MSR = 0x00000000$:**
   - When an exception fires, PowerPC hardware sets `SRR0 = PC`, `SRR1 = MSR`, and clears `MSR` to 0 (disabling `IR` and `DR`).
3. **$SRR0 = 0x800041c0$:**
   - The exception fired while executing at address `0x800041c0`.
   - In Wii OS / Broadway architecture, `0x80004000..0x80004300` is the operating system exception dispatch table and trap trampoline.
4. **$LR = 0x801a7c08$ and $r0 = 0x801a7bf4$:**
   - An indirect function call or branch-and-link (`bctrl` / `bl`) was executed at `0x801a7c04` (setting $LR = 0x801a7c08$).
   - The caller setup loaded `r0 = 0x801a7bf4`.
5. **Physical Memory Fallthrough:**
   - With `MSR.IR = 0`, the CPU fetches instructions physically at `0x00428450` (which is unmapped RAM filled with 0s), generating infinite `unknown_instruction` warnings.

---

## 4. Next Concrete Steps for the Successor Agent

1. **Disassemble the Code Range `0x801a7bf0..0x801a7c08`:**
   - In [`StencilInterpreter.cpp:188`](file:///home/oneeb/Documents/code/android/dolphin/Source/Core/Core/PowerPC/StencilInterpreter/StencilInterpreter.cpp#L188), we added memory dumping using `ppc_state.mem_ptr + (addr & 0x03ffffff)`.
   - Run the game and inspect the disassembly around `0x801a7c04`. Determine what instruction triggered the call to `0x800041c0` (likely an indirect branch `bctrl` or OS syscall `sc`).
2. **Check Register State Preceding the Call:**
   - If `0x801a7c04` is a `bctrl`, check how `CTR` was computed. Ensure all ALU operations feeding into `CTR` (or `r12`) used the correct opcode semantics.
3. **Fix `LoadCarry` / `SaveCarry` Offset in `gen_stencils.cpp`:**
   - Replace constant `288` with `PPCSTATE_OFF(xer_ca)` (offset `696`) so arithmetic with carry (`addc`, `adde`, `subfe`, etc.) can be safely enabled.
4. **In-Tape Condition Register & Branch Support:**
   - Expand `TryEmitStencil` to support comparison operations (`cmp`, `cmpl`, `cmpi`) and conditional branches within the block to eliminate constant round-trips to the C++ callback dispatcher.
