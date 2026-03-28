// ============================================================================
// ir_lifter_branch.cpp — Branch, jump, and system instruction handlers
// Part of PS2reAIcomp — Sprint 2
// ============================================================================
#include "ps2recomp/ir_lifter.h"
#include <algorithm>
namespace ps2recomp {
using namespace ir;

// Helper: emit a conditional branch (BEQ/BNE/BGEZ/etc.)
void IRLifter::emitCondBranch(IRFunction& func, IRBasicBlock& bb,
                               IROp cmpOp, ValueId lhs, ValueId rhs,
                               uint32_t targetAddr, uint32_t srcAddr, bool isLikely) {
    auto cmp = makeBinaryOp(func, cmpOp, IRType::I1, lhs, rhs, srcAddr);
    ValueId cId = cmp.result.id;
    bb.instructions.push_back(std::move(cmp));

    uint32_t tgtIdx = getOrCreateBlock(func, targetAddr);
    uint32_t fallIdx = 0;
    // fall-through block is wired in liftFunction's post-pass

    IRInst br;
    br.op = IROp::IR_BRANCH;
    br.srcAddress = srcAddr;
    br.operands = {cId};
    br.branchTarget = tgtIdx;
    br.branchLikely = isLikely;
    emitTerminator(func, bb, std::move(br), isLikely);

    bb.successors.push_back(tgtIdx);
    func.blocks[tgtIdx].predecessors.push_back(bb.index);
}

// ── BEQ / BNE ───────────────────────────────────────────────────────────────

void IRLifter::liftBEQ(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, bb, IROp::IR_EQ, rs, rt, target, instr.addr);
}

void IRLifter::liftBNE(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, bb, IROp::IR_NE, rs, rt, target, instr.addr);
}

void IRLifter::liftBEQL(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, bb, IROp::IR_EQ, rs, rt, target, instr.addr, true);
}

void IRLifter::liftBNEL(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, bb, IROp::IR_NE, rs, rt, target, instr.addr, true);
}

// ── BGEZ / BGTZ / BLEZ / BLTZ ──────────────────────────────────────────────

void IRLifter::liftBGEZ(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs   = emitGPRRead(func, bb, f.rs, instr.addr);
    auto zero = emitConst32(func, bb, 0);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    // rs >= 0 ↔ !(rs < 0)
    emitCondBranch(func, bb, IROp::IR_SGE, rs, zero, target, instr.addr);
}

void IRLifter::liftBGTZ(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs   = emitGPRRead(func, bb, f.rs, instr.addr);
    auto zero = emitConst32(func, bb, 0);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, bb, IROp::IR_SGT, rs, zero, target, instr.addr);
}

void IRLifter::liftBLEZ(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs   = emitGPRRead(func, bb, f.rs, instr.addr);
    auto zero = emitConst32(func, bb, 0);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, bb, IROp::IR_SLE, rs, zero, target, instr.addr);
}

void IRLifter::liftBLTZ(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs   = emitGPRRead(func, bb, f.rs, instr.addr);
    auto zero = emitConst32(func, bb, 0);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, bb, IROp::IR_SLT, rs, zero, target, instr.addr);
}

// ── J / JAL / JR / JALR ────────────────────────────────────────────────────

void IRLifter::liftJ(IRFunction& func, IRBasicBlock& bb,
                      const GhidraInstruction& instr,
                      const MIPSFields& f) {
    uint32_t target = computeJumpTarget(instr.addr, f.target26);
    uint32_t tgtIdx = getOrCreateBlock(func, target);

    IRInst br;
    br.op = IROp::IR_BRANCH;
    br.srcAddress = instr.addr;
    br.branchTarget = tgtIdx;
    emitTerminator(func, bb, std::move(br), false, false);

    bb.successors.push_back(tgtIdx);
    func.blocks[tgtIdx].predecessors.push_back(bb.index);
}

void IRLifter::liftJAL(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    uint32_t target = computeJumpTarget(instr.addr, f.target26);
    // Save return address: $ra = PC + 8 (after delay slot)
    auto raVal = emitConstU32(func, bb, instr.addr + 8);
    emitGPRWrite(func, bb, 31, raVal, instr.addr); // $ra = reg 31

    IRInst call;
    call.op = IROp::IR_CALL;
    call.srcAddress = instr.addr;
    call.branchTarget = target; // store call target address
    call.comment = "JAL";
    emitTerminator(func, bb, std::move(call));
}

void IRLifter::liftJR(IRFunction& func, IRBasicBlock& bb,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    if (f.rs == 31) {
        // JR $ra = function return
        IRInst ret;
        ret.op = IROp::IR_RETURN;
        ret.srcAddress = instr.addr;
        emitTerminator(func, bb, std::move(ret), false, false);
    } else {
        // Indirect jump — check if it's a resolved jump table
        bool resolved = false;
        if (currentResolvedJumps_) {
            for (const auto& jt : *currentResolvedJumps_) {
                if (jt.jrAddr == instr.addr) {
                    IRInst sw;
                    sw.op = IROp::IR_SWITCH;
                    sw.srcAddress = instr.addr;
                    sw.operands = {rs}; // Usually we'd want the index reg, but we'll use the target address for now
                    sw.comment = "Switch statement (resolved)";
                    // Save cases (Deduplicated)
                    std::vector<uint32_t> seen;
                    for (const auto& case_val : jt.entries) {
                        if (std::find(seen.begin(), seen.end(), case_val.targetAddr) == seen.end()) {
                            seen.push_back(case_val.targetAddr);
                            uint32_t targetIdx = getOrCreateBlock(func, case_val.targetAddr);
                            sw.switchTargets.push_back(targetIdx);
                            sw.switchValues.push_back(case_val.targetAddr);
                            
                            // Only add edge once
                            bb.successors.push_back(targetIdx);
                            func.blocks[targetIdx].predecessors.push_back(bb.index);
                        }
                    }
                    emitTerminator(func, bb, std::move(sw), false, false);
                    resolved = true;
                    break;
                }
            }
        }
        
        if (!resolved) {
            // Emitting indirect branch
            IRInst ibr;
            ibr.op = IROp::IR_JUMP_INDIRECT;
            ibr.srcAddress = instr.addr;
            ibr.operands = {rs};
            ibr.comment = "indirect jump (JR)";
            emitTerminator(func, bb, std::move(ibr), false, false);
        }
    }
}

void IRLifter::liftJALR(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    // Save return address
    auto raVal = emitConstU32(func, bb, instr.addr + 8);
    emitGPRWrite(func, bb, f.rd, raVal, instr.addr);

    IRInst call;
    call.op = IROp::IR_CALL;
    call.srcAddress = instr.addr;
    call.operands = {rs};
    call.comment = "JALR (indirect call)";
    emitTerminator(func, bb, std::move(call));
}

// ── System instructions ─────────────────────────────────────────────────────

void IRLifter::liftSYSCALL(IRFunction&, IRBasicBlock& bb,
                            const GhidraInstruction& instr,
                            const MIPSFields&) {
    IRInst inst;
    inst.op = IROp::IR_SYSCALL;
    inst.srcAddress = instr.addr;
    bb.instructions.push_back(std::move(inst));
}

void IRLifter::liftBREAK(IRFunction&, IRBasicBlock& bb,
                          const GhidraInstruction& instr,
                          const MIPSFields&) {
    IRInst inst;
    inst.op = IROp::IR_BREAK;
    inst.srcAddress = instr.addr;
    bb.instructions.push_back(std::move(inst));
}

void IRLifter::liftSYNC(IRFunction&, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields&) {
    IRInst inst;
    inst.op = IROp::IR_NOP;
    inst.srcAddress = instr.addr;
    inst.comment = "SYNC (memory barrier)";
    bb.instructions.push_back(std::move(inst));
}

void IRLifter::liftNOP(IRFunction&, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields&) {
    // Explicit NOP — already counted in stats
}

} // namespace ps2recomp
