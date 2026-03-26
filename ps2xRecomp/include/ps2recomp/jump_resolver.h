// ============================================================================
// jump_resolver.h — MIPS jump table resolution via backward pattern matching
// Part of PS2reAIcomp — Sprint 1: Ghidra Bridge & Jump Resolver
// ============================================================================
#pragma once

#include "ghidra_types.h"
#include "ghidra_bridge.h"
#include <vector>
#include <functional>

namespace ps2recomp {

// ── JumpResolver ────────────────────────────────────────────────────────────
// Resolves indirect jumps (jr $reg) in MIPS R5900 code by performing backward
// pattern matching on the disassembly to identify jump table patterns:
//
//   Pattern A (index + offset table):
//     sltiu  $at, $reg, N        ; N = number of cases
//     ...
//     lui    $rX, %hi(table)
//     addiu  $rX, $rX, %lo(table)
//     sll    $rY, $reg, 2        ; index * 4
//     addu   $rY, $rY, $rX       ; &table[index]
//     lw     $rZ, 0($rY)         ; load target
//     jr     $rZ
//
//   Pattern B (computed address table):
//     sltiu  $at, $reg, N
//     ...
//     sll    $rY, $reg, 2
//     lui    $rX, %hi(table)
//     lw     $rZ, %lo(table)($rY)
//     jr     $rZ
//
// The resolver walks backward from each `jr` instruction, extracts:
// - The .rodata table address from lui + addiu / lw offset
// - The case count from sltiu
// Then reads the table entries from memory via GhidraBridge.
//
class JumpResolver {
public:
    // Max instructions to walk backward from a jr
    static constexpr uint32_t DEFAULT_BACKWARD_WINDOW = 30;

    // Progress callback: (currentJr, totalJrs)
    using ProgressCallback = std::function<void(uint32_t, uint32_t)>;

    explicit JumpResolver(GhidraBridge& bridge);
    ~JumpResolver() = default;

    // Resolve all jump tables in a function
    std::vector<ResolvedJumpTable> resolveFunction(
        const GhidraFunctionDetail& func);

    // Resolve all jump tables across all functions
    std::vector<ResolvedJumpTable> resolveAll(
        const std::vector<GhidraFunction>& functions,
        ProgressCallback progress = nullptr);

    // Resolve a single jr instruction (given surrounding disassembly context)
    std::optional<ResolvedJumpTable> resolveJr(
        const std::vector<GhidraInstruction>& disasm,
        size_t jrIndex);

    // Validate resolved targets against known function boundaries
    bool validateTargets(const ResolvedJumpTable& table,
                         const GhidraFunction& ownerFunc,
                         const std::vector<GhidraSegment>& segments);

    uint32_t resolvedCount() const { return resolvedCount_; }
    uint32_t failedCount()   const { return failedCount_; }

private:
    GhidraBridge& bridge_;
    uint32_t      resolvedCount_ = 0;
    uint32_t      failedCount_   = 0;

    // Extract register name from operands string
    static std::string extractRegFromJr(const std::string& operands);
    static std::string extractDestReg(const std::string& operands);
    static std::string extractSrcReg1(const std::string& operands);
    static int32_t     extractImmediate(const std::string& operands);
    static uint16_t    extractHiImm(const std::string& operands);
    static int16_t     extractLoImm(const std::string& operands);

    // Build table address from lui+addiu pattern
    static uint32_t combineHiLo(uint16_t hi, int16_t lo);
};

} // namespace ps2recomp
