// ============================================================================
// ghidra_types.h — Pure data types for Ghidra bridge communication
// Part of PS2reAIcomp — Sprint 1: Ghidra Bridge & Jump Resolver
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace ps2recomp {

// ── GhidraFunction ──────────────────────────────────────────────────────────
// Represents a single function as seen by Ghidra's analysis.
// startAddr comes from Ghidra directly; endAddr is computed from the last
// instruction address + 4 (MIPS R5900 fixed-width instructions).
struct GhidraFunction {
    uint32_t    startAddr   = 0;
    uint32_t    endAddr     = 0;    // computed from disassembly
    std::string name;
    std::string signature;          // e.g. "undefined entry(void)"
    std::string returnType;         // e.g. "undefined", "int", "void"
    bool        isThunk     = false; // j + nop = thunk stub

    uint32_t size() const { return endAddr > startAddr ? endAddr - startAddr : 0; }
    bool isNamed() const { return !name.empty() && name.substr(0, 4) != "FUN_"; }
};

// ── GhidraXRef ──────────────────────────────────────────────────────────────
// Cross-reference record from Ghidra.
struct GhidraXRef {
    enum Type {
        UNCONDITIONAL_CALL,
        CONDITIONAL_JUMP,
        UNCONDITIONAL_JUMP,
        READ,
        WRITE,
        PARAM,
        DATA,
        COMPUTED_CALL,
        UNKNOWN
    };

    uint32_t    fromAddr = 0;
    uint32_t    toAddr   = 0;
    Type        type     = UNKNOWN;
    std::string fromInstruction;  // e.g. "jal 0x00111960"
    std::string toSymbol;         // e.g. "FlushCache"

    static Type parseRefType(const std::string& s) {
        if (s == "UNCONDITIONAL_CALL")  return UNCONDITIONAL_CALL;
        if (s == "CONDITIONAL_JUMP")    return CONDITIONAL_JUMP;
        if (s == "UNCONDITIONAL_JUMP")  return UNCONDITIONAL_JUMP;
        if (s == "READ")                return READ;
        if (s == "WRITE")               return WRITE;
        if (s == "PARAM")               return PARAM;
        if (s == "DATA")                return DATA;
        if (s == "COMPUTED_CALL")       return COMPUTED_CALL;
        return UNKNOWN;
    }

    static const char* typeToString(Type t) {
        switch (t) {
            case UNCONDITIONAL_CALL:  return "CALL";
            case CONDITIONAL_JUMP:    return "COND_JMP";
            case UNCONDITIONAL_JUMP:  return "UNCOND_JMP";
            case READ:                return "READ";
            case WRITE:               return "WRITE";
            case PARAM:               return "PARAM";
            case DATA:                return "DATA";
            case COMPUTED_CALL:       return "COMP_CALL";
            case UNKNOWN:             return "UNKNOWN";
        }
        return "UNKNOWN";
    }
};

// ── GhidraInstruction ───────────────────────────────────────────────────────
// Single disassembled MIPS instruction.
struct GhidraInstruction {
    uint32_t    addr     = 0;
    uint32_t    rawBytes = 0;      // decoded from hex string "280C0070"
    std::string mnemonic;          // e.g. "jal", "addiu", "jr"
    std::string operands;          // e.g. "a0,sp,0x10"

    bool isJumpRegister() const { return mnemonic == "jr"; }
    bool isJal()          const { return mnemonic == "jal"; }
    bool isJalr()         const { return mnemonic == "jalr"; }
    bool isJ()            const { return mnemonic == "j"; }
    bool isBranch()       const {
        return mnemonic == "beq"  || mnemonic == "bne"  ||
               mnemonic == "bgez" || mnemonic == "bgtz" ||
               mnemonic == "blez" || mnemonic == "bltz" ||
               mnemonic == "bgezal" || mnemonic == "bltzal" ||
               mnemonic == "beql" || mnemonic == "bnel";
    }
    bool isNop()          const { return mnemonic == "nop" || (rawBytes == 0); }
    bool isLui()          const { return mnemonic == "lui"; }
    bool isAddiu()        const { return mnemonic == "addiu"; }
    bool isSll()          const { return mnemonic == "sll"; }
    bool isLw()           const { return mnemonic == "lw"; }
    bool isAddu()         const { return mnemonic == "addu"; }
};

// ── GhidraSegment ───────────────────────────────────────────────────────────
// Memory segment from the ELF / Ghidra analysis.
struct GhidraSegment {
    std::string name;
    uint32_t    startAddr    = 0;
    uint32_t    endAddr      = 0;
    uint32_t    size         = 0;
    bool        readable     = false;
    bool        writable     = false;
    bool        executable   = false;
    bool        initialized  = false;

    bool contains(uint32_t addr) const {
        return addr >= startAddr && addr <= endAddr;
    }
};

// ── GhidraFunctionDetail ────────────────────────────────────────────────────
// Composite: function info + full disassembly + cross-references.
// Built by GhidraBridge::fetchFunctionDetail() via multiple REST queries.
struct GhidraFunctionDetail {
    GhidraFunction                  info;
    uint32_t                        totalInstructions = 0;
    std::vector<GhidraInstruction>  disasm;
    std::vector<GhidraXRef>         xrefsFrom;  // outgoing refs
    std::vector<GhidraXRef>         xrefsTo;    // incoming refs
};

// ── ResolvedJumpEntry / ResolvedJumpTable ────────────────────────────────────
// Output of JumpResolver — resolved indirect jump (jr $reg) via .rodata.
struct ResolvedJumpEntry {
    uint32_t caseIndex;   // 0-based index into table
    uint32_t targetAddr;  // branch target address
};

struct ResolvedJumpTable {
    uint32_t    jrAddr      = 0;  // address of the jr instruction
    uint32_t    tableStart  = 0;  // .rodata address where table begins
    uint32_t    entryCount  = 0;
    std::string regName;          // register used by jr (e.g. "t0")
    std::vector<ResolvedJumpEntry> entries;
};

// ── ValidationResult ────────────────────────────────────────────────────────
// Output of boundary validation pass.
struct ValidationResult {
    struct Overlap {
        uint32_t funcA, funcB;    // overlapping pair start addresses
        uint32_t overlapStart, overlapEnd;
    };
    struct DualEntry {
        uint32_t targetAddr;      // address targeted mid-function
        uint32_t ownerFunc;       // the function that "owns" this address
        uint32_t callerAddr;      // who is jumping into the middle
    };

    std::vector<Overlap>    overlapping;
    std::vector<DualEntry>  dualEntries;
    std::vector<uint32_t>   unreachable;   // functions with zero incoming xrefs

    bool isClean() const {
        return overlapping.empty() && dualEntries.empty();
    }
    uint32_t totalIssues() const {
        return static_cast<uint32_t>(overlapping.size() + dualEntries.size() +
                                     unreachable.size());
    }
};

} // namespace ps2recomp
