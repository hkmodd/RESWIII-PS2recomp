// ============================================================================
// ir_lifter.cpp — MIPS R5900 → IR Lifter (Sprint 2)
// Part of PS2reAIcomp
// ============================================================================
#include "ps2recomp/ir_lifter.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <sstream>

namespace ps2recomp {

using namespace ir;

// ═══════════════════════════════════════════════════════════════════════════
// Construction / Dispatch table
// ═══════════════════════════════════════════════════════════════════════════

IRLifter::IRLifter() { initDispatchTable(); }
IRLifter::~IRLifter() = default;

void IRLifter::initDispatchTable() {
    // Integer ALU — R-type
    dispatchTable_["add"]    = &IRLifter::liftADD;
    dispatchTable_["addu"]   = &IRLifter::liftADDU;
    dispatchTable_["sub"]    = &IRLifter::liftSUB;
    dispatchTable_["subu"]   = &IRLifter::liftSUBU;
    dispatchTable_["dadd"]   = &IRLifter::liftDADD;
    dispatchTable_["daddu"]  = &IRLifter::liftDADDU;
    // Integer ALU — I-type
    dispatchTable_["addi"]   = &IRLifter::liftADDI;
    dispatchTable_["addiu"]  = &IRLifter::liftADDIU;
    dispatchTable_["daddi"]  = &IRLifter::liftDADDI;
    dispatchTable_["daddiu"] = &IRLifter::liftDADDIU;
    // Logic
    dispatchTable_["and"]    = &IRLifter::liftAND;
    dispatchTable_["andi"]   = &IRLifter::liftANDI;
    dispatchTable_["or"]     = &IRLifter::liftOR;
    dispatchTable_["ori"]    = &IRLifter::liftORI;
    dispatchTable_["xor"]    = &IRLifter::liftXOR;
    dispatchTable_["xori"]   = &IRLifter::liftXORI;
    dispatchTable_["nor"]    = &IRLifter::liftNOR;
    // Shifts
    dispatchTable_["sll"]    = &IRLifter::liftSLL;
    dispatchTable_["srl"]    = &IRLifter::liftSRL;
    dispatchTable_["sra"]    = &IRLifter::liftSRA;
    dispatchTable_["sllv"]   = &IRLifter::liftSLLV;
    dispatchTable_["srlv"]   = &IRLifter::liftSRLV;
    dispatchTable_["srav"]   = &IRLifter::liftSRAV;
    // Set-on-less-than
    dispatchTable_["slt"]    = &IRLifter::liftSLT;
    dispatchTable_["sltu"]   = &IRLifter::liftSLTU;
    dispatchTable_["slti"]   = &IRLifter::liftSLTI;
    dispatchTable_["sltiu"]  = &IRLifter::liftSLTIU;
    // LUI
    dispatchTable_["lui"]    = &IRLifter::liftLUI;
    // Multiply / Divide
    dispatchTable_["mult"]   = &IRLifter::liftMULT;
    dispatchTable_["multu"]  = &IRLifter::liftMULTU;
    dispatchTable_["div"]    = &IRLifter::liftDIV;
    dispatchTable_["divu"]   = &IRLifter::liftDIVU;
    dispatchTable_["mfhi"]   = &IRLifter::liftMFHI;
    dispatchTable_["mflo"]   = &IRLifter::liftMFLO;
    dispatchTable_["mthi"]   = &IRLifter::liftMTHI;
    dispatchTable_["mtlo"]   = &IRLifter::liftMTLO;
    // Memory loads
    dispatchTable_["lb"]     = &IRLifter::liftLB;
    dispatchTable_["lbu"]    = &IRLifter::liftLBU;
    dispatchTable_["lh"]     = &IRLifter::liftLH;
    dispatchTable_["lhu"]    = &IRLifter::liftLHU;
    dispatchTable_["lw"]     = &IRLifter::liftLW;
    dispatchTable_["lwu"]    = &IRLifter::liftLWU;
    dispatchTable_["ld"]     = &IRLifter::liftLD;
    dispatchTable_["lq"]     = &IRLifter::liftLQ;
    dispatchTable_["lwl"]    = &IRLifter::liftLWL;
    dispatchTable_["lwr"]    = &IRLifter::liftLWR;
    // Memory stores
    dispatchTable_["sb"]     = &IRLifter::liftSB;
    dispatchTable_["sh"]     = &IRLifter::liftSH;
    dispatchTable_["sw"]     = &IRLifter::liftSW;
    dispatchTable_["sd"]     = &IRLifter::liftSD;
    dispatchTable_["sq"]     = &IRLifter::liftSQ;
    dispatchTable_["swl"]    = &IRLifter::liftSWL;
    dispatchTable_["swr"]    = &IRLifter::liftSWR;
    // Branches
    dispatchTable_["beq"]    = &IRLifter::liftBEQ;
    dispatchTable_["bne"]    = &IRLifter::liftBNE;
    dispatchTable_["bgez"]   = &IRLifter::liftBGEZ;
    dispatchTable_["bgtz"]   = &IRLifter::liftBGTZ;
    dispatchTable_["blez"]   = &IRLifter::liftBLEZ;
    dispatchTable_["bltz"]   = &IRLifter::liftBLTZ;
    dispatchTable_["beql"]   = &IRLifter::liftBEQL;
    dispatchTable_["bnel"]   = &IRLifter::liftBNEL;
    // Jumps / Calls
    dispatchTable_["j"]      = &IRLifter::liftJ;
    dispatchTable_["jal"]    = &IRLifter::liftJAL;
    dispatchTable_["jr"]     = &IRLifter::liftJR;
    dispatchTable_["jalr"]   = &IRLifter::liftJALR;
    // Conditional move
    dispatchTable_["movz"]   = &IRLifter::liftMOVZ;
    dispatchTable_["movn"]   = &IRLifter::liftMOVN;
    // System
    dispatchTable_["syscall"]= &IRLifter::liftSYSCALL;
    dispatchTable_["break"]  = &IRLifter::liftBREAK;
    dispatchTable_["sync"]   = &IRLifter::liftSYNC;
    dispatchTable_["nop"]    = &IRLifter::liftNOP;
    // FPU
    dispatchTable_["add.s"]  = &IRLifter::liftADD_S;
    dispatchTable_["sub.s"]  = &IRLifter::liftSUB_S;
    dispatchTable_["mul.s"]  = &IRLifter::liftMUL_S;
    dispatchTable_["div.s"]  = &IRLifter::liftDIV_S;
    dispatchTable_["mov.s"]  = &IRLifter::liftMOV_S;
    dispatchTable_["neg.s"]  = &IRLifter::liftNEG_S;
    dispatchTable_["abs.s"]  = &IRLifter::liftABS_S;
    dispatchTable_["sqrt.s"] = &IRLifter::liftSQRT_S;
    dispatchTable_["mfc1"]   = &IRLifter::liftMFC1;
    dispatchTable_["mtc1"]   = &IRLifter::liftMTC1;
    dispatchTable_["cvt.s.w"]= &IRLifter::liftCVT_S_W;
    dispatchTable_["cvt.w.s"]= &IRLifter::liftCVT_W_S;
    dispatchTable_["lwc1"]   = &IRLifter::liftLWC1;
    dispatchTable_["swc1"]   = &IRLifter::liftSWC1;
    dispatchTable_["c.eq.s"] = &IRLifter::liftC_EQ_S;
    dispatchTable_["c.lt.s"] = &IRLifter::liftC_LT_S;
    dispatchTable_["c.le.s"] = &IRLifter::liftC_LE_S;
    dispatchTable_["bc1t"]   = &IRLifter::liftBC1T;
    dispatchTable_["bc1f"]   = &IRLifter::liftBC1F;
    dispatchTable_["bc1tl"]  = &IRLifter::liftBC1TL;
    dispatchTable_["bc1fl"]  = &IRLifter::liftBC1FL;
}

// ═══════════════════════════════════════════════════════════════════════════
// MIPS field extraction
// ═══════════════════════════════════════════════════════════════════════════

IRLifter::MIPSFields IRLifter::decodeFields(uint32_t raw) {
    MIPSFields f;
    f.opcode   = static_cast<uint8_t>((raw >> 26) & 0x3F);
    f.rs       = static_cast<uint8_t>((raw >> 21) & 0x1F);
    f.rt       = static_cast<uint8_t>((raw >> 16) & 0x1F);
    f.rd       = static_cast<uint8_t>((raw >> 11) & 0x1F);
    f.sa       = static_cast<uint8_t>((raw >>  6) & 0x1F);
    f.func     = static_cast<uint8_t>( raw        & 0x3F);
    f.imm16    = static_cast<uint16_t>(raw & 0xFFFF);
    f.simm16   = static_cast<int16_t>(f.imm16);
    f.target26 = raw & 0x03FFFFFF;
    f.fmt      = f.rs;   // COP1: format field shares rs position
    f.ft       = f.rt;   // COP1: ft shares rt position
    f.fs       = f.rd;   // COP1: fs shares rd position
    f.fd       = f.sa;   // COP1: fd shares sa position
    return f;
}

// ═══════════════════════════════════════════════════════════════════════════
// Pass 1: Block boundary discovery
// ═══════════════════════════════════════════════════════════════════════════

std::unordered_set<uint32_t> IRLifter::findBlockBoundaries(
        const std::vector<GhidraInstruction>& disasm) const {
    std::unordered_set<uint32_t> starts;
    if (disasm.empty()) return starts;

    // First instruction always starts a block
    starts.insert(disasm[0].addr);

    for (size_t i = 0; i < disasm.size(); ++i) {
        const auto& instr = disasm[i];
        const auto  f = decodeFields(instr.rawBytes);

        bool isBranch = false;
        uint32_t target = 0;

        // Check for branch instructions (I-type with PC-relative offset)
        if (instr.mnemonic == "beq"  || instr.mnemonic == "bne"  ||
            instr.mnemonic == "bgez" || instr.mnemonic == "bgtz" ||
            instr.mnemonic == "blez" || instr.mnemonic == "bltz" ||
            instr.mnemonic == "beql" || instr.mnemonic == "bnel" ||
            instr.mnemonic == "bgezal" || instr.mnemonic == "bltzal" ||
            instr.mnemonic == "bc1t" || instr.mnemonic == "bc1f" ||
            instr.mnemonic == "bc1tl"|| instr.mnemonic == "bc1fl") {
            isBranch = true;
            target = computeBranchTarget(instr.addr, f.simm16);
        }
        // J / JAL
        else if (instr.mnemonic == "j") {
            isBranch = true;
            target = computeJumpTarget(instr.addr, f.target26);
        }
        // JR is a terminator but target is dynamic — just mark fall-through
        else if (instr.mnemonic == "jr" || instr.mnemonic == "jalr" ||
                 instr.mnemonic == "jal") {
            isBranch = true;
            target = 0; // dynamic or call — no intra-function target
        }

        if (isBranch) {
            // Branch target starts a new block
            if (target != 0) starts.insert(target);
            // Instruction after delay slot starts a new block
            if (i + 2 < disasm.size()) {
                starts.insert(disasm[i + 2].addr);
            }
        }
    }
    return starts;
}

// ═══════════════════════════════════════════════════════════════════════════
// Main lifting entry point
// ═══════════════════════════════════════════════════════════════════════════

std::optional<IRFunction> IRLifter::liftFunction(
        const GhidraFunction& funcInfo,
        const std::vector<GhidraInstruction>& disasm,
        ProgressCallback progress) {

    if (disasm.empty()) return std::nullopt;

    resetStats();
    addrToBlockIndex_.clear();

    IRFunction func;
    func.name = funcInfo.name;
    func.mipsEntryAddr = funcInfo.startAddr;
    func.mipsEndAddr = funcInfo.endAddr;

    stats_.totalInstructions = static_cast<uint32_t>(disasm.size());

    // Pass 1: find block boundaries
    auto blockStarts = findBlockBoundaries(disasm);

    // Sort block starts to create blocks in address order
    std::vector<uint32_t> sortedStarts(blockStarts.begin(), blockStarts.end());
    std::sort(sortedStarts.begin(), sortedStarts.end());

    // Pre-create all blocks so we can reference them by index
    for (auto addr : sortedStarts) {
        char label[32];
        std::snprintf(label, sizeof(label), "bb_%08X", addr);
        auto& bb = func.addBlock(label);
        bb.mipsStartAddr = addr;
        addrToBlockIndex_[addr] = bb.index;
    }
    stats_.basicBlocks = static_cast<uint32_t>(func.blocks.size());

    // Pass 2: emit IR per block
    uint32_t currentBlockIdx = 0;
    if (!func.blocks.empty()) {
        currentBlockIdx = addrToBlockIndex_.count(disasm[0].addr)
            ? addrToBlockIndex_[disasm[0].addr] : 0;
    }

    for (uint32_t i = 0; i < disasm.size(); ++i) {
        const auto& instr = disasm[i];

        // Switch to new block if this address starts one
        auto it = addrToBlockIndex_.find(instr.addr);
        if (it != addrToBlockIndex_.end()) {
            currentBlockIdx = it->second;
        }

        auto* bb = func.getBlock(currentBlockIdx);
        if (!bb) continue;

        bb->mipsEndAddr = instr.addr + 4;

        // Decode raw fields
        auto fields = decodeFields(instr.rawBytes);

        // Emit source comment
        if (emitComments_) {
            std::string text = instr.mnemonic;
            if (!instr.operands.empty()) text += " " + instr.operands;
            emitComment(*bb, text, instr.addr);
        }

        // Skip NOPs (SLL $zero, $zero, 0)
        if (instr.isNop()) {
            stats_.skippedNops++;
            stats_.liftedInstructions++;
            if (progress) progress(i, stats_.totalInstructions);
            continue;
        }

        // Dispatch to handler
        auto handler = dispatchTable_.find(instr.mnemonic);
        if (handler != dispatchTable_.end()) {
            (this->*(handler->second))(func, *bb, instr, fields);
            stats_.liftedInstructions++;
        } else {
            liftUnhandled(func, *bb, instr, fields);
            stats_.unhandledMnemonics++;
        }

        if (progress) progress(i, stats_.totalInstructions);
    }

    // Wire up fall-through edges between consecutive blocks
    for (uint32_t b = 0; b + 1 < func.blocks.size(); ++b) {
        auto& blk = func.blocks[b];
        if (blk.instructions.empty() || !blk.instructions.back().isTerminator()) {
            blk.successors.push_back(b + 1);
            func.blocks[b + 1].predecessors.push_back(b);
        }
    }

    return func;
}

// ═══════════════════════════════════════════════════════════════════════════
// Register read/write helpers
// ═══════════════════════════════════════════════════════════════════════════

ValueId IRLifter::emitGPRRead(IRFunction& func, IRBasicBlock& bb,
                               uint8_t regIdx, uint32_t srcAddr) {
    // $zero is always 0
    if (regIdx == 0) return emitConst32(func, bb, 0);

    auto inst = makeRegRead(func, IRType::I32, IRReg::gpr(regIdx));
    inst.srcAddress = srcAddr;
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    return vid;
}

ValueId IRLifter::emitFPRRead(IRFunction& func, IRBasicBlock& bb,
                               uint8_t regIdx, uint32_t srcAddr) {
    auto inst = makeRegRead(func, IRType::F32, IRReg::fpr(regIdx));
    inst.srcAddress = srcAddr;
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    return vid;
}

void IRLifter::emitGPRWrite(IRFunction& func, IRBasicBlock& bb,
                             uint8_t regIdx, ValueId value, uint32_t srcAddr) {
    if (regIdx == 0) return;  // writes to $zero are dropped
    auto inst = makeRegWrite(IRReg::gpr(regIdx), value);
    inst.srcAddress = srcAddr;
    bb.instructions.push_back(std::move(inst));
}

void IRLifter::emitFPRWrite(IRFunction& func, IRBasicBlock& bb,
                             uint8_t regIdx, ValueId value, uint32_t srcAddr) {
    auto inst = makeRegWrite(IRReg::fpr(regIdx), value);
    inst.srcAddress = srcAddr;
    bb.instructions.push_back(std::move(inst));
}

ValueId IRLifter::emitConst32(IRFunction& func, IRBasicBlock& bb, int32_t value) {
    auto inst = makeConst(func, IRType::I32, static_cast<int64_t>(value));
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    return vid;
}

ValueId IRLifter::emitConstU32(IRFunction& func, IRBasicBlock& bb, uint32_t value) {
    auto inst = makeConstU(func, IRType::I32, static_cast<uint64_t>(value));
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    return vid;
}

ValueId IRLifter::emitConst64(IRFunction& func, IRBasicBlock& bb, int64_t value) {
    auto inst = makeConst(func, IRType::I64, value);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    return vid;
}

// ═══════════════════════════════════════════════════════════════════════════
// Comment emission
// ═══════════════════════════════════════════════════════════════════════════

void IRLifter::emitComment(IRBasicBlock& bb, const std::string& text,
                            uint32_t srcAddr) {
    IRInst inst;
    inst.op = IROp::IR_COMMENT;
    inst.comment = text;
    inst.srcAddress = srcAddr;
    bb.instructions.push_back(std::move(inst));
}

// ═══════════════════════════════════════════════════════════════════════════
// Block management
// ═══════════════════════════════════════════════════════════════════════════

uint32_t IRLifter::getOrCreateBlock(IRFunction& func, uint32_t addr) {
    auto it = addrToBlockIndex_.find(addr);
    if (it != addrToBlockIndex_.end()) return it->second;
    char label[32];
    std::snprintf(label, sizeof(label), "bb_%08X", addr);
    auto& bb = func.addBlock(label);
    bb.mipsStartAddr = addr;
    addrToBlockIndex_[addr] = bb.index;
    return bb.index;
}

// ═══════════════════════════════════════════════════════════════════════════
// Address computation helpers
// ═══════════════════════════════════════════════════════════════════════════

uint32_t IRLifter::computeBranchTarget(uint32_t pc, int16_t offset) const {
    // MIPS: target = PC + 4 + (sign_ext(offset) << 2)
    return pc + 4 + (static_cast<int32_t>(offset) << 2);
}

uint32_t IRLifter::computeJumpTarget(uint32_t pc, uint32_t target26) const {
    // MIPS: target = (PC+4)[31:28] | (target26 << 2)
    return ((pc + 4) & 0xF0000000) | (target26 << 2);
}

ValueId IRLifter::emitAddrCalc(IRFunction& func, IRBasicBlock& bb,
                                uint8_t baseReg, int16_t offset,
                                uint32_t srcAddr) {
    auto base = emitGPRRead(func, bb, baseReg, srcAddr);
    if (offset == 0) return base;
    auto off = emitConst32(func, bb, static_cast<int32_t>(offset));
    auto inst = makeBinaryOp(func, IROp::IR_ADD, IRType::I32, base, off, srcAddr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    return vid;
}

// ═══════════════════════════════════════════════════════════════════════════
// Unhandled instruction fallback
// ═══════════════════════════════════════════════════════════════════════════

void IRLifter::liftUnhandled(IRFunction& func, IRBasicBlock& bb,
                              const GhidraInstruction& instr,
                              const MIPSFields&) {
    IRInst inst;
    inst.op = IROp::IR_NOP;
    inst.srcAddress = instr.addr;
    inst.srcRaw = instr.rawBytes;
    inst.comment = "[UNHANDLED] " + instr.mnemonic + " " + instr.operands;
    bb.instructions.push_back(std::move(inst));
}

} // namespace ps2recomp
