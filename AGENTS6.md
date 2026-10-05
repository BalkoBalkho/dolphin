# Dolphin Stencil Interpreter (A642A64) - Architectural Handover & Deep Technical Field Notes (AGENTS6.md)

This document is the primary technical handover, architectural reference, and progress ledger for continuing the development of the **`A642A64` W^X Stencil Interpreter** in Dolphin on mobile hardware.

---

## 1. Verified Hardware & Benchmark Environment

- **Target Architecture:** AArch64 (ARM64) with strict W^X enforcement (zero runtime executable code generation; precompiled stencils in static RX memory, operands in RW tape).
- **Physical Test Device:** Samsung Galaxy S8 (`SM-G950F`, Exynos 8895 Octa: 4x Mongoose M2 @ 2.3 GHz + 4x Cortex-A53 @ 1.7 GHz, Mali-G71 MP20).
- **ADB Serial:** `ce031713a871d04b0d`.
- **Operating System:** Android 9 (Pie), Linux 4.4.111.
- **NDK Clang:** `/home/oneeb/Android/Sdk/ndk/30.0.16138531/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang++`.
- **Target Game:** *The Legend of Zelda: Twilight Princess* (`GZ2E01`, `/sdcard/Download/GZ2E01.iso`).
- **Safety Critical Protocol:**
  - `settings put system screen_brightness 1`
  - `svc power stayon true`
  - **NEVER** send `input keyevent 223` (`KEYCODE_SLEEP`), as it causes Android to destroy the `SurfaceView`/`EGLSurface`, breaking emulation rendering.

---

## 2. Performance Evolution Across Optimization Milestones

All telemetry recorded on live Galaxy S8 running release build with debug symbols (`RelWithDebInfo`/`Debug` APK):

| Milestone | Key Optimizations | Title / Menus FPS | In-Game 3D FPS | Core Throughput | Native Stencil Ratio |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Baseline** | Hardware spinlock hang / pink screen | 1.20 FPS | 1.20 FPS | ~12 MIPS | ~60% |
| **Pass 1** | PPCAnalyst idle spinlock restore (`0x80341144`) | 4.77 FPS | 4.77 FPS | ~35 MIPS | 76.5% |
| **Pass 2** | Scalar FP stencils (`lfs`, `stfs`, `lfd`, `stfd`, `fmr`) | 6.80 FPS | 6.80 FPS | ~50 MIPS | 88.2% |
| **Pass 3** | Direct block links for `bl`, `blr`, `rlwimi`, `lbzu` | **59.8 VPS (101%)** | **8.00 FPS / 13.75 VPS** | 62.8 MIPS | **94.94%** |
| **Pass 4** | Tailcall `dcbi`, `mtctr`, `mfctr`, `mfmsr` | **57.6 VPS (97%)** | **3.4 – 6.9 FPS (13–22%)** | **97.2 MIPS** | **94.08%** |

---

## 3. Deep Technical Analysis: The 24.8M `dcbi` Storm & Elimination

### 3.1 The `dcbi` Invalidation Storm (OP31 SUB470)
Prior to Milestone 4, the live fallback profiler (`[FALLBACK_PROFILE]`) revealed that GameCube vertex setup repeatedly executed cacheline invalidations:
```armasm
dcbi   rX
addi   rX, rX, 32
bdnz+  -8
```
Because `dcbi` was unhandled by stencils:
1. Every 32 bytes forced an exit from native execution to C++ `Interpret<false>`.
2. The C++ interpreter called `InvalidateICacheLine`, triggering full CPU pipeline stalls and cache bookkeeping **24,790,700 times**.

### 3.2 The Zero-Overhead `dcbi` Stencil Solution
In an emulated environment running on a unified memory architecture (Android RAM), cache line data invalidation does not require physical L1 D-cache bus operations. In upstream Dolphin's AArch64 JIT, this is recognized and fast-pathed.
In `stencil_trampoline.S`:
```armasm
.global stencil_dyn_dcbi
stencil_dyn_dcbi:
    STENCIL_TAILCALL
```
**Result:** In live profiling after deployment, `SUB470` dropped from **24.8 million fallbacks to exactly 0**.

### 3.3 Counter Register (`mtctr` / `mfctr`) Stencils
Twilight Princess extensively uses `mtctr` (`mtspr spr=9`) to initialize counter registers before every vertex and memory loop.
- Offset for `spr[SPR_CTR]` in `PowerPCState` is **868** (verified with static assertion).
- Offset for `msr` in `PowerPCState` is **736**.

In `stencil_trampoline.S`:
```armasm
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
```
**Result:** Eliminates all 6.5 million `mtctr` dropouts.

---

## 4. Root-Cause Analysis: Why Framerate Fluctuated Between 8 FPS and ~3.4 FPS

When testing Twilight Princess, the observed framerate ranged from 8.0 FPS down to 3.4 FPS. Detailed frame-by-frame and telemetry inspection revealed:
1. **The code did NOT regress in CPU execution efficiency.** Peak throughput climbed from ~50 MIPS to **97.2 MIPS** and **3.29 Million blocks/second**.
2. **The drop was caused by camera choreography and scene geometry:**
   - **Wide Landscape (8.00 FPS / 13.75 VPS, 22% Speed):** Link and Epona gallop across Hyrule Field in the distance. Low on-screen polygon count, minimal dynamic vertex skinning.
   - **Extreme Close-Up (3.28 – 3.80 FPS / 6.08 – 7.57 VPS, 13% Speed):** The camera cuts directly in front of Link and Epona. Dust particle clouds erupt from hooves, volumetric twilight sun shafts cover the screen, and complex skeletal animation runs at maximum detail.
3. **The 3D Vertex Engine Bottleneck (773 Million Fallbacks):**
   Live profiler telemetry in the close-up scene captured:
   ```text
   [FALLBACK_PROFILE] Total: 773,849,088 fallbacks
   1. OP4:  144,675,869 (18.7%)  <-- Gekko Paired Single arithmetic (ps_add, ps_sub, ps_mul, ps_madd)
   2. OP56: 112,739,335 (14.6%)  <-- psq_l (Paired Single Quantized Load)
   3. OP59: 101,691,622 (13.1%)  <-- Single-precision FP math (fadds, fsubs, fmuls, fmadds)
   4. OP63:  79,830,158 (10.3%)  <-- Double-precision FP math (fadd, fsub, fmul, fmadd)
   5. OP31:  70,247,015  (9.1%)  <-- lbzx (SUB87), subfe (SUB136), lfsx (SUB535), dcbf (SUB86)
   ```
   **Paired Singles and Floating Point arithmetic represent 56.7% (438.9 Million) of every single dropout.** In dense 3D scenes, vertex processing dominates the workload.

---

## 5. Implementation Roadmap to Push Past 15+ FPS

### 5.1 OP59: Single-Precision FP Math (101.7 Million Fallbacks)
Instructions: `fadds` (subop 21), `fsubs` (subop 20), `fmuls` (subop 25), `fmadds` (subop 29).
On AArch64, these map directly 1:1 to single hardware floating-point instructions:
```armasm
// Example fadds:
fadd  sD, sA, sB

// Example fmadds:
fmadd sD, sA, sC, sB
```

### 5.2 OP4: Gekko Paired Single SIMD Math (144.7 Million Fallbacks)
PowerPC Gekko paired single instructions store two 32-bit floats in one 64-bit FPR (`ps0` and `ps1`).
On AArch64, these map directly to 64-bit NEON vector operations (`vD.2s`):
```armasm
// ps_add:
fadd  vD.2s, vA.2s, vB.2s

// ps_sub:
fsub  vD.2s, vA.2s, vB.2s

// ps_mul:
fmul  vD.2s, vA.2s, vB.2s
```

### 5.3 OP56: Quantized Vertex Loads (`psq_l`) (112.7 Million Fallbacks)
Loads 1 or 2 quantized floats from memory into a paired single register using `GQR` (Graphics Quantization Registers). Stencilizing standard dequantization eliminates 112.7M C++ exits.

---

## 6. Build, Test, and Deployment Protocol

### 6.1 Testing Before Deployment (DivergenceTest)
Always verify new assembly stencils in `DivergenceTest` on the Galaxy S8 before runtime integration:
```bash
/home/oneeb/Android/Sdk/ndk/30.0.16138531/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang++ \
    -O3 -std=c++20 \
    -I/home/oneeb/Documents/code/android/dolphin/Source/Core \
    -I/home/oneeb/Documents/code/android/dolphin/Externals/A642A64/src \
    /home/oneeb/Documents/code/android/dolphin/Tools/StencilGenerator/DivergenceTest.cpp \
    /home/oneeb/Documents/code/android/dolphin/Externals/A642A64/src/stencil_trampoline.S \
    -o /tmp/divergence_test

/home/oneeb/Android/Sdk/platform-tools/adb push /tmp/divergence_test /data/local/tmp/
/home/oneeb/Android/Sdk/platform-tools/adb shell "chmod +x /data/local/tmp/divergence_test && /data/local/tmp/divergence_test"
```
Ensure output reports: `[+] ALL DIVERGENCE TESTS PASSED! Zero discrepancies.`

### 6.2 Building & Deploying Debug APK
```bash
cd /home/oneeb/Documents/code/android/dolphin/Source/Android
JAVA_HOME=/home/oneeb/.local/share/JetBrains/Toolbox/apps/android-studio/jbr ./gradlew assembleDebug
/home/oneeb/Android/Sdk/platform-tools/adb install -r app/build/outputs/apk/debug/app-debug.apk
```

### 6.3 Launching Twilight Princess
```bash
/home/oneeb/Android/Sdk/platform-tools/adb shell "settings put system screen_brightness 1 && svc power stayon true"
/home/oneeb/Android/Sdk/platform-tools/adb shell "am force-stop org.dolphinemu.dolphinemu.debug && logcat -c"
/home/oneeb/Android/Sdk/platform-tools/adb shell "am start -n org.dolphinemu.dolphinemu.debug/org.dolphinemu.dolphinemu.activities.EmulationActivity --esa SelectedGames '/sdcard/Download/GZ2E01.iso'"
```
To advance past the "Health and Safety" screen if prompted with "Press any button to continue", hold touch for 300ms:
```bash
/home/oneeb/Android/Sdk/platform-tools/adb shell "input swipe 1260 540 1260 540 300"
```
