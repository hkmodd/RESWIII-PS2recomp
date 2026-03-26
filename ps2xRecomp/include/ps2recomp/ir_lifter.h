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
        ir::IRBasicBlock& bb,
        const GhidraInstruction& instr,
        const MIPSFields& fields);

    // Dispatch table: mnemonic string → handler
    std::unordered_map<std::string, LiftHandler> dispatchTable_;
    void initDispatchTable();

    // ── Register read/write helpers ─────────────────────────────────────
    // Emit IR_REG_READ for a GPR and return the SSA ValueId.
    // $zero always returns a constant 0.
    ir::ValueId emitGPRRead(ir::IRFunction& func, ir::IRBasicBlock& bb,
                            uint8_t regIdx, uint32_t srcAddr);
    ir::ValueId emitFPRRead(ir::IRFunction& func, ir::IRBasicBlock& bb,
                            uint8_t regIdx, uint32_t srcAddr);

    // Emit IR_REG_WRITE for a GPR. Writes to $zero are silently dropped.
    void emitGPRWrite(ir::IRFunction& func, ir::IRBasicBlock& bb,
                      uint8_t regIdx, ir::ValueId value, uint32_t srcAddr);
    void emitFPRWrite(ir::IRFunction& func, ir::IRBasicBlock& bb,
                      uint8_t regIdx, ir::ValueId value, uint32_t srcAddr);

    // Emit a signed/unsigned immediate constant
    ir::ValueId emitConst32(ir::IRFunction& func, ir::IRBasicBlock& bb,
                            int32_t value);
    ir::ValueId emitConstU32(ir::IRFunction& func, ir::IRBasicBlock& bb,
                             uint32_t value);
    ir::ValueId emitConst64(ir::IRFunction& func, ir::IRBasicBlock& bb,
                            int64_t value);

    // ── Comment emission ────────────────────────────────────────────────
    void emitComment(ir::IRBasicBlock& bb, const std::string& text,
                     uint32_t srcAddr);

    // ── Block management ────────────────────────────────────────────────
    // Maps MIPS address → block index (populated in pass 1)
    std::unordered_map<uint32_t, uint32_t> addrToBlockIndex_;

    uint32_t getOrCreateBlock(ir::IRFunction& func, uint32_t addr);

    // ── Branch / compare helpers ────────────────────────────────────────
    void emitCondBranch(ir::IRFunction& func, ir::IRBasicBlock& bb,
                        ir::IROp cmpOp, ir::ValueId lhs, ir::ValueId rhs,
                        uint32_t targetAddr, uint32_t srcAddr);
    void emitFPUCompare(ir::IRFunction& func, ir::IRBasicBlock& bb,
                        ir::IROp cmpOp, const GhidraInstruction& instr,
                        const MIPSFields& f);

    // ── Individual instruction lifters ──────────────────────────────────
    // Integer ALU
    void liftADD   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftADDU  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftADDI  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftADDIU (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSUB   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSUBU  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftDADD  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftDADDU (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftDADDI (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftDADDIU(ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // Logic
    void liftAND   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftANDI  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftOR    (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftORI   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftXOR   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftXORI  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftNOR   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // Shifts
    void liftSLL   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSRL   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSRA   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSLLV  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSRLV  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSRAV  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // Set-on-less-than
    void liftSLT   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSLTU  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSLTI  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSLTIU (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // LUI
    void liftLUI   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // Multiply / Divide
    void liftMULT  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftMULTU (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftDIV   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftDIVU  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftMFHI  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftMFLO  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftMTHI  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftMTLO  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // Memory loads
    void liftLB    (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftLBU   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftLH    (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftLHU   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftLW    (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftLWU   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftLD    (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftLQ    (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftLWL   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftLWR   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // Memory stores
    void liftSB    (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSH    (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSW    (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSD    (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSQ    (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSWL   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSWR   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // Branches
    void liftBEQ   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftBNE   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftBGEZ  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftBGTZ  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftBLEZ  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftBLTZ  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftBEQL  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftBNEL  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // Jumps / Calls
    void liftJ     (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftJAL   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftJR    (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftJALR  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // Conditional move
    void liftMOVZ  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftMOVN  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // System
    void liftSYSCALL(ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftBREAK  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSYNC   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftNOP    (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // FPU (COP1)
    void liftADD_S  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSUB_S  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftMUL_S  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftDIV_S  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftMOV_S  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftNEG_S  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftABS_S  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSQRT_S (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftMFC1   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftMTC1   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftCVT_S_W(ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftCVT_W_S(ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftLWC1   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftSWC1   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftC_EQ_S (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftC_LT_S (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftC_LE_S (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftBC1T   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftBC1F   (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftBC1TL  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);
    void liftBC1FL  (ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // Fallback for unhandled instructions
    void liftUnhandled(ir::IRFunction&, ir::IRBasicBlock&, const GhidraInstruction&, const MIPSFields&);

    // ── Helper: compute branch target address ───────────────────────────
    uint32_t computeBranchTarget(uint32_t pc, int16_t offset) const;
    uint32_t computeJumpTarget(uint32_t pc, uint32_t target26) const;

    // ── Helper: emit a memory address computation (base + offset) ───────
    ir::ValueId emitAddrCalc(ir::IRFunction& func, ir::IRBasicBlock& bb,
                             uint8_t baseReg, int16_t offset, uint32_t srcAddr);

    // ── State ───────────────────────────────────────────────────────────
    bool           emitComments_   = true;
    bool           foldDelaySlots_ = true;
    IRLifterStats  stats_;
};

} // namespace ps2recomp
