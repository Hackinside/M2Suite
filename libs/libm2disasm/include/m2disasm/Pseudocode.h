#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "m2disasm/Elf.h"

namespace m2disasm {

// Lifts a subset of PowerPC 602 machine code into readable ANSI-C-style
// pseudocode for software-preservation and documentation work. This is NOT
// a full decompiler: it recognises the instruction patterns the M2 SDK's
// Diab/MPW C compiler actually emits (stack-frame prologue/epilogue, GPR
// moves, integer arithmetic/logic, base+displacement loads/stores, compares
// and conditional branches, and bl calls) and re-expresses each function as
// a commented C-ish body. Register operands stay as r0..r31 / fr0..fr31
// pseudo-variables; calls and branch targets are resolved to symbol names
// (local .symtab functions and .imp3do folio imports) when available, using
// the SDK definitions as the source of truth for names.
//
// The intent is legibility for a human reconstructing the original ANSI C,
// not round-trippable output.
class Pseudocode {
public:
    explicit Pseudocode(const Elf& elf) : elf_(elf) {}

    // Lifts a single function beginning at `addr` (must lie in an executable
    // section). Stops at the function's blr/epilogue or after maxInstr
    // instructions. Returns a C-style listing with the machine address in a
    // trailing comment on each line.
    std::string liftFunction(uint32_t addr, size_t maxInstr = 4096) const;

    // Lifts every FUNC symbol (or, if the binary is stripped, the entry
    // point) to a combined listing.
    std::string liftAll(size_t maxInstrPerFunc = 4096) const;

private:
    const Elf& elf_;

    const Elf::Section* sectionForAddr(uint32_t addr) const;
    std::string nameForAddr(uint32_t addr) const;
};

} // namespace m2disasm
