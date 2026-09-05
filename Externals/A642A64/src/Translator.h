#pragma once
#include "Bytecode.h"
#include <cstdint>
#include <vector>
#include <unordered_map>

// A translated basic block: a contiguous sequence of BytecodeInst
// ending with a branch/halt instruction.
struct BytecodeBlock {
    std::vector<BytecodeInst> insts;
    uint64_t guest_start;  // guest PC of first instruction
};

// Translates a buffer of raw ARM64 machine code (little-endian 32-bit words)
// into bytecode blocks. Uses Capstone for decoding.
class Translator {
public:
    Translator();
    ~Translator();

    // Translate a single basic block starting at guest_pc.
    // 'code' points to raw ARM64 bytes, 'size' is the buffer length.
    // Returns nullptr on failure.
    BytecodeBlock* translate(uint64_t guest_pc, const uint8_t* code, size_t size);

    // Get or translate a block (uses internal cache).
    BytecodeBlock* getBlock(uint64_t guest_pc, const uint8_t* code, size_t size);

private:
    void* cs_handle_;  // Capstone handle (void* to avoid exposing capstone.h)
    std::unordered_map<uint64_t, BytecodeBlock> cache_;

    BytecodeInst liftInsn(const void* cs_insn);
};
