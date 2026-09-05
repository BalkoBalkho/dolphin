#pragma once
#include "VMState.h"
#include "Bytecode.h"
#include <cstddef>

// Initialise the interpreter (builds the dispatch table).
// Must be called once before any interpret() calls.
void interpreter_init();

// Execute bytecode instructions starting at 'block'.
// 'state' is the VM register state (modified in place).
// Runs until OP_HLT is encountered.
extern "C" void interpreter_run(VMState* state, const BytecodeInst* block);

// The dispatch table — array of function pointers indexed by Opcode.
// Defined in Interpreter.cpp, filled by interpreter_init().
extern void* g_dispatch_table[OP_COUNT];
