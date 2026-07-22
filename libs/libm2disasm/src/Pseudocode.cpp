#include "m2disasm/Pseudocode.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "../third_party/CommonDefs.h"
#include "../third_party/ppcd.h"

namespace m2disasm {

namespace {
uint32_t u32be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// Splits a ppcd operand string into comma-separated fields, trimming spaces.
// ppcd formats memory operands as "disp (rA)", which we keep as one field.
std::vector<std::string> splitOperands(const char* ops) {
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = ops; *p; ++p) {
        if (*p == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += *p;
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    for (auto& s : out) {
        size_t a = s.find_first_not_of(" \t");
        size_t b = s.find_last_not_of(" \t");
        s = (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
    }
    return out;
}

// Rewrites a ppcd memory operand "disp (rA)" into a C lvalue of the given
// width, e.g. cType="u32" → "*(u32*)(r3 + 0x10)".
std::string memRef(const std::string& field, const char* cType) {
    size_t paren = field.find('(');
    if (paren == std::string::npos) {
        return field;
    }
    std::string disp = field.substr(0, paren);
    size_t close = field.find(')', paren);
    std::string reg = field.substr(paren + 1, close - paren - 1);
    // Trim.
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t");
        size_t b = s.find_last_not_of(" \t");
        return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
    };
    disp = trim(disp);
    reg = trim(reg);
    std::string addr = reg;
    if (!disp.empty() && disp != "0") {
        if (disp[0] == '-') {
            addr += " - " + disp.substr(1);
        } else {
            addr += " + " + disp;
        }
    }
    return std::string("*(") + cType + "*)(" + addr + ")";
}

bool starts(const char* s, const char* pfx) {
    return std::strncmp(s, pfx, std::strlen(pfx)) == 0;
}
} // namespace

const Elf::Section* Pseudocode::sectionForAddr(uint32_t addr) const {
    for (const auto& s : elf_.sections()) {
        if (s.executable() && s.progbits() && addr >= s.addr && addr < s.addr + s.size) {
            return &s;
        }
    }
    return nullptr;
}

std::string Pseudocode::nameForAddr(uint32_t addr) const {
    for (const auto& sym : elf_.symbols()) {
        if (sym.value == addr && !sym.name.empty()) {
            return sym.name;
        }
    }
    char buf[24];
    std::snprintf(buf, sizeof(buf), "sub_%08X", addr);
    return buf;
}

std::string Pseudocode::liftFunction(uint32_t addr, size_t maxInstr) const {
    const Elf::Section* sec = sectionForAddr(addr);
    if (!sec) {
        return "/* address 0x" + std::to_string(addr) + " is not in an executable section */\n";
    }

    std::ostringstream out;
    out << "/* ============================================================ */\n";
    out << "void " << nameForAddr(addr) << "(void)   /* @ 0x" << std::hex << addr << std::dec
        << " — reconstructed pseudocode, register-level */\n{\n";

    // Section bytes are already decompressed by Elf::load.
    // Re-fetch the raw code via a fresh disassembly of each word.
    PPCD_CB cb{};
    size_t emitted = 0;
    for (uint32_t a = addr; emitted < maxInstr; a += 4, ++emitted) {
        if (a + 4 > sec->addr + sec->size) {
            break;
        }
        size_t fileOff = size_t(sec->offset) + (a - sec->addr);
        if (fileOff + 4 > elf_.rawData().size()) {
            break;
        }
        uint32_t word = u32be(elf_.rawData().data() + fileOff);
        cb.pc = a;
        cb.instr = word;
        PPCDisasm(&cb);
        const char* m = cb.mnemonic;
        auto ops = splitOperands(cb.operands);
        std::string line;         // the C statement
        std::string raw = std::string(m) + " " + cb.operands;
        bool endFunc = false;

        // ---- control flow ----
        if (std::strcmp(m, "blr") == 0) {
            line = "return;";
            endFunc = true;
        } else if (std::strcmp(m, "bctr") == 0) {
            line = "goto *ctr;   /* computed jump (switch/vtable) */";
        } else if (std::strcmp(m, "bctrl") == 0) {
            line = "(*ctr)();    /* indirect call */";
        } else if (std::strcmp(m, "bl") == 0 && !ops.empty()) {
            uint32_t tgt = uint32_t(cb.target);
            line = nameForAddr(tgt) + "();";
        } else if (std::strcmp(m, "b") == 0 && !ops.empty()) {
            char loc[24];
            std::snprintf(loc, sizeof(loc), "loc_%08X", uint32_t(cb.target));
            line = std::string("goto ") + loc + ";";
        } else if (starts(m, "b") && (cb.iclass & PPC_DISA_BRANCH) && !ops.empty()) {
            // Conditional branch: bne/beq/blt/bgt/bge/ble ...
            std::string test = "cond";
            if (starts(m, "bne")) test = "ne";
            else if (starts(m, "beq")) test = "eq";
            else if (starts(m, "blt")) test = "lt";
            else if (starts(m, "bgt")) test = "gt";
            else if (starts(m, "bge")) test = "ge";
            else if (starts(m, "ble")) test = "le";
            char loc[24];
            std::snprintf(loc, sizeof(loc), "loc_%08X", uint32_t(cb.target));
            line = "if (" + test + ") goto " + loc + ";";
        }
        // ---- moves / immediates ----
        else if (std::strcmp(m, "mr") == 0 && ops.size() == 2) {
            line = ops[0] + " = " + ops[1] + ";";
        } else if (std::strcmp(m, "li") == 0 && ops.size() == 2) {
            line = ops[0] + " = " + ops[1] + ";";
        } else if (std::strcmp(m, "lis") == 0 && ops.size() == 2) {
            line = ops[0] + " = " + ops[1] + " << 16;";
        } else if (std::strcmp(m, "mflr") == 0 && ops.size() == 1) {
            line = ops[0] + " = lr;   /* save return address */";
        } else if (std::strcmp(m, "mtlr") == 0 && ops.size() == 1) {
            line = "lr = " + ops[0] + ";   /* restore return address */";
        } else if (std::strcmp(m, "mtctr") == 0 && ops.size() == 1) {
            line = "ctr = " + ops[0] + ";";
        }
        // ---- arithmetic / logic (3-operand reg, or reg+imm) ----
        else if ((std::strcmp(m, "add") == 0 || std::strcmp(m, "addi") == 0 ||
                   std::strcmp(m, "addic") == 0) &&
                  ops.size() == 3) {
            line = ops[0] + " = " + ops[1] + " + " + ops[2] + ";";
        } else if ((std::strcmp(m, "subf") == 0) && ops.size() == 3) {
            line = ops[0] + " = " + ops[2] + " - " + ops[1] + ";"; // subf: rD = rB - rA
        } else if ((std::strcmp(m, "sub") == 0 || std::strcmp(m, "subi") == 0) &&
                    ops.size() == 3) {
            line = ops[0] + " = " + ops[1] + " - " + ops[2] + ";";
        } else if (std::strcmp(m, "neg") == 0 && ops.size() == 2) {
            line = ops[0] + " = -" + ops[1] + ";";
        } else if ((std::strcmp(m, "mullw") == 0 || std::strcmp(m, "mulli") == 0) &&
                    ops.size() == 3) {
            line = ops[0] + " = " + ops[1] + " * " + ops[2] + ";";
        } else if ((std::strcmp(m, "divw") == 0 || std::strcmp(m, "divwu") == 0) &&
                    ops.size() == 3) {
            line = ops[0] + " = " + ops[1] + " / " + ops[2] + ";";
        } else if ((std::strcmp(m, "or") == 0 || std::strcmp(m, "ori") == 0) && ops.size() == 3) {
            line = ops[0] + " = " + ops[1] + " | " + ops[2] + ";";
        } else if ((std::strcmp(m, "and") == 0 || std::strcmp(m, "andi.") == 0) &&
                    ops.size() == 3) {
            line = ops[0] + " = " + ops[1] + " & " + ops[2] + ";";
        } else if ((std::strcmp(m, "xor") == 0 || std::strcmp(m, "xori") == 0) &&
                    ops.size() == 3) {
            line = ops[0] + " = " + ops[1] + " ^ " + ops[2] + ";";
        } else if ((std::strcmp(m, "slw") == 0 || std::strcmp(m, "slwi") == 0) && ops.size() == 3) {
            line = ops[0] + " = " + ops[1] + " << " + ops[2] + ";";
        } else if ((std::strcmp(m, "srw") == 0 || std::strcmp(m, "srwi") == 0) && ops.size() == 3) {
            line = ops[0] + " = " + ops[1] + " >> " + ops[2] + ";";
        } else if (std::strcmp(m, "rlwinm") == 0 && ops.size() >= 2) {
            line = ops[0] + " = rotl_mask(" + ops[1] + ", ...);   /* " + raw + " */";
        }
        // ---- compares (set condition for the next branch) ----
        else if ((std::strcmp(m, "cmpw") == 0 || std::strcmp(m, "cmplw") == 0) &&
                  ops.size() >= 2) {
            std::string a2 = ops[ops.size() - 2], b2 = ops[ops.size() - 1];
            line = "/* compare " + a2 + " vs " + b2 + " */";
        } else if ((std::strcmp(m, "cmpwi") == 0 || std::strcmp(m, "cmplwi") == 0) &&
                    ops.size() >= 2) {
            std::string a2 = ops[ops.size() - 2], b2 = ops[ops.size() - 1];
            line = "/* compare " + a2 + " vs " + b2 + " */";
        }
        // ---- loads ----
        else if (std::strcmp(m, "lwz") == 0 && ops.size() == 2) {
            line = ops[0] + " = " + memRef(ops[1], "u32") + ";";
        } else if (std::strcmp(m, "lwzu") == 0 && ops.size() == 2) {
            line = ops[0] + " = " + memRef(ops[1], "u32") + ";   /* + update base */";
        } else if ((std::strcmp(m, "lhz") == 0) && ops.size() == 2) {
            line = ops[0] + " = " + memRef(ops[1], "u16") + ";";
        } else if ((std::strcmp(m, "lha") == 0) && ops.size() == 2) {
            line = ops[0] + " = " + memRef(ops[1], "s16") + ";";
        } else if (std::strcmp(m, "lbz") == 0 && ops.size() == 2) {
            line = ops[0] + " = " + memRef(ops[1], "u8") + ";";
        }
        // ---- stores ----
        else if (std::strcmp(m, "stw") == 0 && ops.size() == 2) {
            line = memRef(ops[1], "u32") + " = " + ops[0] + ";";
        } else if (std::strcmp(m, "stwu") == 0 && ops.size() == 2) {
            // Almost always the prologue: stwu r1,-N(r1)
            if (ops[0] == "r1") {
                bool neg = !ops[1].empty() && ops[1][0] == '-';
                line = "/* prologue: allocate stack frame */  " + memRef(ops[1], "u32") +
                        " = r1;  r1 " + (neg ? "-=" : "+=") + " frame;";
            } else {
                line = memRef(ops[1], "u32") + " = " + ops[0] + ";   /* + update base */";
            }
        } else if (std::strcmp(m, "sth") == 0 && ops.size() == 2) {
            line = memRef(ops[1], "u16") + " = " + ops[0] + ";";
        } else if (std::strcmp(m, "stb") == 0 && ops.size() == 2) {
            line = memRef(ops[1], "u8") + " = " + ops[0] + ";";
        }
        // ---- multiple / string (register save/restore) ----
        else if (std::strcmp(m, "stmw") == 0 && ops.size() == 2) {
            line = "/* save GPRs " + ops[0] + "..r31 to " + memRef(ops[1], "u32") + " */";
        } else if (std::strcmp(m, "lmw") == 0 && ops.size() == 2) {
            line = "/* restore GPRs " + ops[0] + "..r31 from " + memRef(ops[1], "u32") + " */";
        }
        // ---- float loads/stores (kept register-level) ----
        else if (starts(m, "lfs") && ops.size() == 2) {
            line = ops[0] + " = " + memRef(ops[1], "float") + ";";
        } else if (starts(m, "lfd") && ops.size() == 2) {
            line = ops[0] + " = " + memRef(ops[1], "double") + ";";
        } else if (starts(m, "stfs") && ops.size() == 2) {
            line = memRef(ops[1], "float") + " = " + ops[0] + ";";
        } else if (starts(m, "stfd") && ops.size() == 2) {
            line = memRef(ops[1], "double") + " = " + ops[0] + ";";
        }
        // ---- no-op / unhandled: keep the asm as a comment so nothing lost ----
        else if (std::strcmp(m, "nop") == 0) {
            line = ";   /* nop */";
        } else {
            line = "/* " + raw + " */";
        }

        char addrTag[16];
        std::snprintf(addrTag, sizeof(addrTag), "0x%08X", a);
        out << "    " << line;
        // Pad and append the source address + raw mnemonic for traceability.
        if (line.find("/* " + raw) == std::string::npos) {
            out << "   /* " << addrTag << "  " << m << " " << cb.operands << " */";
        } else {
            out << "   /* " << addrTag << " */";
        }
        out << "\n";

        if (endFunc) {
            break;
        }
    }
    out << "}\n\n";
    return out.str();
}

std::string Pseudocode::liftAll(size_t maxInstrPerFunc) const {
    std::ostringstream out;
    out << "/*\n"
           " * ANSI-C pseudocode reconstruction (M2Suite / libm2disasm)\n"
           " * PowerPC 602 -> C, register-level. Names resolved from .symtab\n"
           " * and .imp3do folio imports where available.\n"
           " */\n\n";

    // Collect function entry points in address order: FUNC symbols, plus
    // NOTYPE symbols that land in an executable section (the M2 Diab/MPW
    // toolchain often emits code labels as NOTYPE). Falls back to the entry
    // point for fully stripped binaries.
    std::vector<uint32_t> funcs;
    for (const auto& sym : elf_.symbols()) {
        if (sym.value == 0) {
            continue;
        }
        if (sym.type == 2 /*FUNC*/ ||
            (sym.type == 0 /*NOTYPE*/ && sectionForAddr(sym.value))) {
            funcs.push_back(sym.value);
        }
    }
    if (funcs.empty()) {
        funcs.push_back(elf_.entryPoint());
    }
    std::sort(funcs.begin(), funcs.end());
    funcs.erase(std::unique(funcs.begin(), funcs.end()), funcs.end());

    for (uint32_t f : funcs) {
        out << liftFunction(f, maxInstrPerFunc);
    }
    return out.str();
}

} // namespace m2disasm
