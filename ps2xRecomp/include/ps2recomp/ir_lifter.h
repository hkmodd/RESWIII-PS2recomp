// ============================================================================
// ir_lifter.h — MIPS R5900 → IR Lifter (Sprint 2)
// Part of PS2reAIcomp — Sprint 2: Disassembly → IR Pipeline
// ============================================================================
// Converts GhidraInstruction sequences into SSA-form IRFunction objects.
// Operates on Ghidra's mnemonic/operand strings + raw instruction words,
// producing basic blocks with proper control-flow edges.
//
// Design:
//   1. Two-pass lifting: Pass 1 identifies basic block boundaries from
//      branch/jump targets. Pass 2 emits IR instructions per block.
//   2. Raw-word decoding for register fields (rd/rs/rt/sa/imm) using
//      standard MIPS R-type / I-type / J-type field extraction.
//   3. Mnemonic dispatch via lookup table for O(1) instruction mapping.
//   4. SSA: each register read emits IR_REG_READ, each write emits
//      IR_REG_WRITE. PHI insertion is deferred to a later SSA pass.
// ============================================================================
#pragma once

#include "ir.h"
#include "ghidra_types.h"
#include "jump_resolver.h"

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <optional>

namespace ps2recomp {

// ── Lifter Statistics ───────────────────────────────────────────────────────
struct IRLifterStats {
    uint32_t totalInstructions   = 0;  // MIPS instructions processed
    uint32_t liftedInstructions  = 0;  // Successfully lifted to IR
    uint32_t skippedNops         = 0;  // NOP/SLL $zero,$zero,0
    uint32_t unhandledMnemonics  = 0;  // Mnemonics not yet implemented
    uint32_t basicBlocks         = 0;  // Basic blocks created
    uint32_t branchesLifted      = 0;  // Branch/jump instructions
    uint32_t callsLifted         = 0;  // JAL/JALR calls
    uint32_t delaySlotsFolded    = 0;  // Delay slots folded into branches

    double coveragePercent() const {
        if (totalInstructions == 0) return 0.0;
        return 100.0 * liftedInstructions / totalInstructions;
    }
};

// ── IRLifter ────────────────────────────────────────────────────────────────
// Main class: takes a vector of GhidraInstructions (from a single function)
// and produces an IRFunction in pre-SSA form.
//
// Usage:
//   IRLifter lifter;
//   auto irFunc = lifter.liftFunction(detail.info, detail.disasm);
//   auto stats  = lifter.stats();
//
class IRLifter {
public:
    // Progress callback: (instructionIndex, totalInstructions)
    using ProgressCallback = std::function<void(uint32_t, uint32_t)>;

    IRLifter();
    ~IRLifter();

    // ── Main entry point ────────────────────────────────────────────────
    // Lifts a complete function from Ghidra disassembly into IR.
    // Returns the IRFunction, or nullopt if disasm is empty.
    std::optional<ir::IRFunction> liftFunction(
        const GhidraFunction& funcInfo,
        const std::vector<GhidraInstruction>& disasm,
        const std::vector<ResolvedJumpTable>* resolvedJumps = nullptr,
        ProgressCallback progress = nullptr);

    // ── Configuration ───────────────────────────────────────────────────
    // If true, emit IR_COMMENT instructions with source disassembly text.
    void setEmitComments(bool v) { emitComments_ = v; }
    bool emitComments() const { return emitComments_; }

    // If true, fold delay slot instructions into the preceding branch.
    void setFoldDelaySlots(bool v) { foldDelaySlots_ = v; }
    bool foldDelaySlots() const { return foldDelaySlots_; }

    // ── Statistics ──────────────────────────────────────────────────────
    const IRLifterStats& stats() const { return stats_; }
    void resetStats() { stats_ = {}; }

private:
    // ── MIPS field extraction from raw 32-bit instruction word ────────
    struct MIPSFields {
        uint8_t  opcode;     // bits [31:26]
        uint8_t  rs;         // bits [25:21]
        uint8_t  rt;         // bits [20:16]
        uint8_t  rd;         // bits [15:11]
        uint8_t  sa;         // bits [10:6]
        uint8_t  func;       // bits [5:0]
        uint16_t imm16;      // bits [15:0]  (I-type immediate)
        int16_t  simm16;     // bits [15:0]  (sign-extended)
        uint32_t target26;   // bits [25:0]  (J-type target)
        uint8_t  fmt;        // bits [25:21] (COP1 format)
        uint8_t  ft;         // bits [20:16] (COP1 ft)
        uint8_t  fs;         // bits [15:11] (COP1 fs)
        uint8_t  fd;         // bits [10:6]  (COP1 fd)
    };

    static MIPSFields decodeFields(uint32_t raw);

    // ── Pass 1: Basic block boundary discovery ──────────────────────────
    // Scans instructions to find all branch/jump targets and fall-through
    // points, building a set of basic block start addresses.
    std::unordered_set<uint32_t> findBlockBoundaries(
        const std::vector<GhidraInstruction>& disasm) const;

    // ── Pass 2: IR emission ─────────────────────────────────────────────
    // Instruction handler type: emits IR for a single MIPS instruction
    using LiftHandler = void (IRLifter::*)(
        ir::IRFunction& func,
        uint32_t blockIdx,
        const GhidraInstruction& instr,
        const MIPSFields& fields);

    // Dispatch table: mnemonic string → handler
    std::unordered_map<std::string, LiftHandler> dispatchTable_;
    void initDispatchTable();

    // ── Register read/write helpers ─────────────────────────────────────
    // Emit IR_REG_READ for a GPR and return the SSA ValueId.
    // $zero always returns a constant 0.
    ir::ValueId emitGPRRead(ir::IRFunction& func, uint32_t blockIdx,
                            uint8_t regIdx, uint32_t srcAddr);
    ir::ValueId emitFPRRead(ir::IRFunction& func, uint32_t blockIdx,
                            uint8_t regIdx, uint32_t srcAddr);

    // Emit IR_REG_WRITE for a GPR. Writes to $zero are silently dropped.
    void emitGPRWrite(ir::IRFunction& func, uint32_t blockIdx,
                      uint8_t regIdx, ir::ValueId value, uint32_t srcAddr);
    void emitFPRWrite(ir::IRFunction& func, uint32_t blockIdx,
                      uint8_t regIdx, ir::ValueId value, uint32_t srcAddr);

    // Emit a signed/unsigned immediate constant
    ir::ValueId emitConst32(ir::IRFunction& func, uint32_t blockIdx,
                            int32_t value);
    ir::ValueId emitConstU32(ir::IRFunction& func, uint32_t blockIdx,
                             uint32_t value);
    ir::ValueId emitConst64(ir::IRFunction& func, uint32_t blockIdx,
                            int64_t value);

    // ── Comment emission ────────────────────────────────────────────────
    void emitComment(ir::IRFunction& func, uint32_t blockIdx, const std::string& text,
                     uint32_t srcAddr);

    // ── Block management ────────────────────────────────────────────────
    // Maps MIPS address → block index (populated in pass 1)
    std::unordered_map<uint32_t, uint32_t> addrToBlockIndex_;

    uint32_t getOrCreateBlock(ir::IRFunction& func, uint32_t addr);

    void inlineDelaySlot(ir::IRFunction& func, uint32_t blockIdx, bool isLikely, std::optional<ir::ValueId> condId = std::nullopt);
    void emitTerminator(ir::IRFunction& func, uint32_t blockIdx, ir::IRInst termInst, bool isLikely = false, bool hasFallthrough = true);

    const std::vector<GhidraInstruction>* currentDisasm_ = nullptr;
    size_t currentInstrIndex_ = 0;
    std::unordered_set<size_t> skipInstructionIndices_;

    // ── Individual instruction lifters ──────────────────────────────────
    // Integer ALU
    void liftADD   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftADDU  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftADDI  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftADDIU (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSUB   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSUBU  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftDADD  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftDADDU (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftDADDI (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftDADDIU(ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // Logic
    void liftAND   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftANDI  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftOR    (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftORI   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftXOR   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftXORI  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftNOR   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // Shifts
    void liftSLL   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSRL   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSRA   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSLLV  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSRLV  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSRAV  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // Set-on-less-than
    void liftSLT   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSLTU  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSLTI  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSLTIU (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // LUI
    void liftLUI   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // Multiply / Divide
    void liftMULT  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftMULTU (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftDIV   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftDIVU  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftMFHI  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftMFLO  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftMTHI  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftMTLO  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // Memory loads
    void liftLB    (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftLBU   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftLH    (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftLHU   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftLW    (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftLWU   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftLD    (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftLQ    (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftLWL   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftLWR   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // Memory stores
    void liftSB    (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSH    (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSW    (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSD    (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSQ    (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSWL   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSWR   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // Branches
    void liftBEQ   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftBNE   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftBGEZ  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftBGTZ  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftBLEZ  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftBLTZ  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftBEQL  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftBNEL  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // Jumps / Calls
    void liftJ     (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftJAL   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftJR    (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftJALR  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // Conditional move
    void liftMOVZ  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftMOVN  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // System
    void liftSYSCALL(ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftBREAK  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSYNC   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftNOP    (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftEI     (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftDI     (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // Pseudo / Synthetic
    void liftMOVE   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftDMOVE  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftCLEAR  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftLI     (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftNEGU   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftB      (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // FPU (COP1)
    void liftADD_S  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftADDA_S (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSUB_S  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftMUL_S  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftDIV_S  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftMOV_S  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftNEG_S  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftABS_S  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSQRT_S (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftMFC1   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftMTC1   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftCVT_S_W(ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftCVT_W_S(ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftLWC1   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftSWC1   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftC_EQ_S (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftC_LT_S (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftC_LE_S (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftBC1T   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftBC1F   (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftBC1TL  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);
    void liftBC1FL  (ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // Fallback for unhandled instructions
    void liftUnhandled(ir::IRFunction&, uint32_t, const GhidraInstruction&, const MIPSFields&);

    // ── Helper: compute branch target address ───────────────────────────
    uint32_t computeBranchTarget(uint32_t pc, int16_t offset) const;
    uint32_t computeJumpTarget(uint32_t pc, uint32_t target26) const;

    // ── Helper: emit conditional branch ──────────────────────────────────
    void emitCondBranch(ir::IRFunction& func, uint32_t blockIdx,
                        ir::IROp cmpOp, ir::ValueId lhs, ir::ValueId rhs,
                        uint32_t targetAddr, uint32_t srcAddr, bool isLikely = false);

    // ── Helper: emit a memory address computation (base + offset) ───────
    ir::ValueId emitAddrCalc(ir::IRFunction& func, uint32_t blockIdx,
                             uint8_t baseReg, int16_t offset, uint32_t srcAddr);

    // ── Helper: emit FPU compare ────────────────────────────────────────
    void emitFPUCompare(ir::IRFunction& func, uint32_t blockIdx,
                        ir::IROp cmpOp, const GhidraInstruction& instr,
                        const MIPSFields& f);

    // ── State ───────────────────────────────────────────────────────────
    bool           emitComments_   = true;
    bool           foldDelaySlots_ = true;
    IRLifterStats  stats_;
    const std::vector<ResolvedJumpTable>* currentResolvedJumps_ = nullptr;
};

} // namespace ps2recomp
