// ============================================================================
// jump_resolver.cpp — MIPS R5900 jump table resolution via pattern matching
// Part of PS2reAIcomp — Sprint 1: Ghidra Bridge & Jump Resolver
// ============================================================================

#include "ps2recomp/jump_resolver.h"
#include <algorithm>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <cctype>

namespace ps2recomp {

// ── Constructor ─────────────────────────────────────────────────────────────

JumpResolver::JumpResolver(GhidraBridge& bridge)
    : bridge_(bridge)
{
}

// ── Operand Parsing Utilities ───────────────────────────────────────────────

std::string JumpResolver::extractRegFromJr(const std::string& operands) {
    // "ra" or "t0" — jr operands have just the register
    std::string s = operands;
    // Trim whitespace
    while (!s.empty() && std::isspace(s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace(s.back())) s.pop_back();
    return s;
}

std::string JumpResolver::extractDestReg(const std::string& operands) {
    // "t0,v0,0x2" → "t0"
    auto pos = operands.find(',');
    if (pos == std::string::npos) return operands;
    std::string s = operands.substr(0, pos);
    while (!s.empty() && std::isspace(s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace(s.back())) s.pop_back();
    return s;
}

std::string JumpResolver::extractSrcReg1(const std::string& operands) {
    // "t0,v0,0x2" → "v0"
    auto pos1 = operands.find(',');
    if (pos1 == std::string::npos) return "";
    auto pos2 = operands.find(',', pos1 + 1);
    std::string s;
    if (pos2 != std::string::npos)
        s = operands.substr(pos1 + 1, pos2 - pos1 - 1);
    else
        s = operands.substr(pos1 + 1);
    while (!s.empty() && std::isspace(s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace(s.back())) s.pop_back();
    return s;
}

int32_t JumpResolver::extractImmediate(const std::string& operands) {
    // Find last comma and parse what follows as an integer
    auto pos = operands.rfind(',');
    if (pos == std::string::npos) return 0;
    std::string s = operands.substr(pos + 1);
    while (!s.empty() && std::isspace(s.front())) s.erase(s.begin());

    // Handle both "0x1234" and "-0x10" and plain decimal
    char* end = nullptr;
    long val;
    if (s.find("0x") != std::string::npos || s.find("0X") != std::string::npos) {
        val = std::strtol(s.c_str(), &end, 0);
    } else {
        val = std::strtol(s.c_str(), &end, 0);
    }
    return static_cast<int32_t>(val);
}

uint16_t JumpResolver::extractHiImm(const std::string& operands) {
    // "at,0x10" → 0x10  (lui rd, imm)
    auto pos = operands.rfind(',');
    if (pos == std::string::npos) return 0;
    std::string s = operands.substr(pos + 1);
    while (!s.empty() && std::isspace(s.front())) s.erase(s.begin());
    return static_cast<uint16_t>(std::strtoul(s.c_str(), nullptr, 0));
}

int16_t JumpResolver::extractLoImm(const std::string& operands) {
    // From addiu: "t0,t0,0x1d0" → 0x1d0
    // From lw: "t0,0x1d0(t1)" → 0x1d0
    auto pos = operands.rfind(',');
    if (pos == std::string::npos) {
        // Try lw format: "reg,offset(base)"
        pos = operands.find(',');
    }
    if (pos == std::string::npos) return 0;

    std::string s = operands.substr(pos + 1);
    while (!s.empty() && std::isspace(s.front())) s.erase(s.begin());

    // Handle lw format: "0x1d0(t1)" — stop at '('
    auto paren = s.find('(');
    if (paren != std::string::npos) s = s.substr(0, paren);

    return static_cast<int16_t>(std::strtol(s.c_str(), nullptr, 0));
}

uint32_t JumpResolver::combineHiLo(uint16_t hi, int16_t lo) {
    // MIPS hi/lo addressing: (hi << 16) + sign_extend(lo)
    return (static_cast<uint32_t>(hi) << 16) + static_cast<int32_t>(lo);
}

// ── Single JR Resolution ────────────────────────────────────────────────────

std::optional<ResolvedJumpTable> JumpResolver::resolveJr(
    const std::vector<GhidraInstruction>& disasm,
    size_t jrIndex)
{
    if (jrIndex >= disasm.size()) return std::nullopt;

    const auto& jrInsn = disasm[jrIndex];
    if (!jrInsn.isJumpRegister()) return std::nullopt;

    std::string jrReg = extractRegFromJr(jrInsn.operands);

    // jr $ra is a function return — skip
    if (jrReg == "ra") return std::nullopt;

    // Backward scan window
    size_t start = (jrIndex > DEFAULT_BACKWARD_WINDOW)
                   ? jrIndex - DEFAULT_BACKWARD_WINDOW : 0;

    // State for pattern matching
    bool      foundLui    = false;
    bool      foundLwOffset = false;
    bool      foundAddiu  = false;
    bool      foundSltiu  = false;
    uint16_t  hiImm       = 0;
    int16_t   lwOffset    = 0;
    int16_t   addiuImm    = 0;
    uint32_t  caseCount   = 0;
    std::string luiReg;
    std::string indexReg;

    // Walk backward from the jr instruction
    for (size_t i = jrIndex; i > start; --i) {
        const auto& insn = disasm[i - 1]; // -1 because we check backwards

        // Pattern: lw $jrReg, offset($baseReg) — load from computed table addr
        if (insn.isLw()) {
            std::string dest = extractDestReg(insn.operands);
            if (dest == jrReg || !foundLwOffset) {
                // Try extracting the lo offset from lw operands
                // Format: "t0,0x1d0(t1)"
                auto parenPos = insn.operands.find('(');
                if (parenPos != std::string::npos) {
                    auto commaPos = insn.operands.find(',');
                    if (commaPos != std::string::npos) {
                        std::string offsetStr = insn.operands.substr(
                            commaPos + 1, parenPos - commaPos - 1);
                        while (!offsetStr.empty() && std::isspace(offsetStr.front()))
                            offsetStr.erase(offsetStr.begin());
                        lwOffset = static_cast<int16_t>(
                            std::strtol(offsetStr.c_str(), nullptr, 0));
                        foundLwOffset = true;
                    }
                }
            }
        }

        // Pattern: addiu $reg, $reg, lo_imm — adds lo half of table address
        if (insn.isAddiu() && !foundAddiu) {
            std::string dest = extractDestReg(insn.operands);
            std::string src  = extractSrcReg1(insn.operands);
            if (dest == src) {
                if (luiReg.empty() || dest == luiReg) {
                    addiuImm = extractLoImm(insn.operands);
                    luiReg = src;  // this register is built from lui + addiu
                    foundAddiu = true;
                }
            }
        }

        // Pattern: lui $reg, hi_imm — loads hi half of table address
        if (insn.isLui()) {
            std::string dest = extractDestReg(insn.operands);
            // Accept if it matches the register from addiu or if we're looking
            if (!foundLui) {
                if (luiReg.empty() || dest == luiReg) {
                    hiImm = extractHiImm(insn.operands);
                    luiReg = dest;
                    foundLui = true;
                }
            }
        }

        // Pattern: sltiu $at, $reg, N — bounds check giving case count
        if (insn.mnemonic == "sltiu" && !foundSltiu) {
            caseCount = static_cast<uint32_t>(extractImmediate(insn.operands));
            indexReg  = extractSrcReg1(insn.operands);
            foundSltiu = true;
        }

        // Pattern: sll $reg, $reg, 2 — multiply index by 4 (32-bit entries)
        // This confirms a jump table pattern
        if (insn.isSll()) {
            int32_t shift = extractImmediate(insn.operands);
            if (shift == 2 || shift == 0x2) {
                // Good — confirms 4-byte table entries
            }
        }
    }

    // Must have at least lui to resolve
    if (!foundLui) {
        return std::nullopt;
    }

    // Compute table address
    int32_t totalLo = 0;
    if (foundLwOffset) totalLo += lwOffset;
    if (foundAddiu) totalLo += addiuImm;

    uint32_t tableAddr;
    if (foundLwOffset || foundAddiu) {
        tableAddr = combineHiLo(hiImm, static_cast<int16_t>(totalLo));
    } else {
        // Only lui found — table might start at hi << 16
        tableAddr = static_cast<uint32_t>(hiImm) << 16;
    }

    // Determine case count — if sltiu not found, try reading up to 64 entries
    // and stop when we find an entry outside the code segment
    if (!foundSltiu || caseCount == 0) {
        caseCount = 64; // max to try
    }

    // Clamp case count for safety
    if (caseCount > 256) caseCount = 256;

    // Read the jump table from memory
    std::vector<uint8_t> tableData = bridge_.readMemory(
        tableAddr, caseCount * 4);

    if (tableData.size() < 4) {
        return std::nullopt;
    }

    // Build the resolved jump table
    ResolvedJumpTable result;
    result.jrAddr     = jrInsn.addr;
    result.tableStart = tableAddr;
    result.regName    = jrReg;

    uint32_t maxEntries = static_cast<uint32_t>(tableData.size()) / 4;

    for (uint32_t i = 0; i < maxEntries; ++i) {
        // Read 32-bit LE entry
        uint32_t target =
            static_cast<uint32_t>(tableData[i * 4 + 0])       |
            (static_cast<uint32_t>(tableData[i * 4 + 1]) << 8)  |
            (static_cast<uint32_t>(tableData[i * 4 + 2]) << 16) |
            (static_cast<uint32_t>(tableData[i * 4 + 3]) << 24);

        // Validate: target should be in code range (0x00100000 - 0x001FFFFF)
        if (target < 0x00100000 || target > 0x001FFFFF) {
            // If sltiu wasn't found, stop here — we've passed the end
            if (!foundSltiu) break;
            // If sltiu was found, include but mark as potentially invalid
        }

        ResolvedJumpEntry entry;
        entry.caseIndex  = i;
        entry.targetAddr = target;
        result.entries.push_back(entry);
    }

    result.entryCount = static_cast<uint32_t>(result.entries.size());

    if (result.entryCount == 0) {
        return std::nullopt;
    }

    return result;
}

// ── Function-Level Resolution ───────────────────────────────────────────────

std::vector<ResolvedJumpTable> JumpResolver::resolveFunction(
    const GhidraFunctionDetail& func)
{
    std::vector<ResolvedJumpTable> results;

    // Find all jr instructions that aren't "jr $ra" (returns)
    for (size_t i = 0; i < func.disasm.size(); ++i) {
        const auto& insn = func.disasm[i];
        if (insn.isJumpRegister()) {
            std::string reg = extractRegFromJr(insn.operands);
            if (reg == "ra") continue; // skip returns

            auto resolved = resolveJr(func.disasm, i);
            if (resolved) {
                ++resolvedCount_;
                results.push_back(std::move(*resolved));
            } else {
                ++failedCount_;
            }
        }
    }

    return results;
}

// ── Global Resolution ───────────────────────────────────────────────────────

std::vector<ResolvedJumpTable> JumpResolver::resolveAll(
    const std::vector<GhidraFunction>& functions,
    ProgressCallback progress)
{
    std::vector<ResolvedJumpTable> allResults;

    for (uint32_t i = 0; i < static_cast<uint32_t>(functions.size()); ++i) {
        // Fetch full detail for this function
        auto detail = bridge_.fetchFunctionDetail(functions[i].startAddr);
        if (!detail) continue;

        // Resolve jump tables
        auto tables = resolveFunction(*detail);
        allResults.insert(allResults.end(), tables.begin(), tables.end());

        if (progress && (i % 50 == 0 || i == functions.size() - 1)) {
            progress(i + 1, static_cast<uint32_t>(functions.size()));
        }
    }

    return allResults;
}

// ── Target Validation ───────────────────────────────────────────────────────

bool JumpResolver::validateTargets(
    const ResolvedJumpTable& table,
    const GhidraFunction& ownerFunc,
    const std::vector<GhidraSegment>& segments)
{
    if (table.entries.empty()) return false;

    for (const auto& entry : table.entries) {
        // Check if target is within a valid executable segment
        bool inSegment = false;
        for (const auto& seg : segments) {
            if (seg.executable && seg.contains(entry.targetAddr)) {
                inSegment = true;
                break;
            }
        }
        if (!inSegment) return false;

        // Check if target is within the owner function's range
        // (jump tables typically branch within the same function)
        if (ownerFunc.endAddr > 0) {
            if (entry.targetAddr < ownerFunc.startAddr ||
                entry.targetAddr >= ownerFunc.endAddr)
            {
                // Target outside function — warn but don't fail
                // Some jump tables can target other functions
            }
        }
    }

    return true;
}

} // namespace ps2recomp
