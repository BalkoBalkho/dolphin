#pragma once
#include <cstdint>

// Virtual machine register state.
// Kept small so it fits in L1 data cache.
// Total size: 31*8 + 8 + 8 + 4 + 4 = 272 bytes
struct alignas(16) VMState {
    uint64_t regs[31];  // 0..240: X0..X30
    uint64_t xzr_zero;  // 248: XZR read source (always 0)
    uint64_t xzr_sink;  // 256: XZR write discard sink
    uint64_t sp;        // 264: SP
    uint64_t pc;        // 272: current guest PC (byte address)
    uint32_t nzcv;      // 280: condition flags: bit3=N bit2=Z bit1=C bit0=V
    uint32_t _pad;      // 284

    // Helper: read any register including XZR (index 31 = XZR = 0, 32 = SP)
    uint64_t read(int idx) const {
        if (idx >= 0 && idx < 31) return regs[idx];
        if (idx == 32) return sp;
        return 0; // XZR
    }
    void write(int idx, uint64_t val) {
        if (idx >= 0 && idx < 31) regs[idx] = val;
        else if (idx == 32) sp = val;
    }

    // NZCV flag helpers
    bool N() const { return (nzcv >> 3) & 1; }
    bool Z() const { return (nzcv >> 2) & 1; }
    bool C() const { return (nzcv >> 1) & 1; }
    bool V() const { return (nzcv >> 0) & 1; }
};
