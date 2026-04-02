// ============================================================================
// ir_lifter_branch.cpp — Branch, jump, and system instruction handlers
// Part of PS2reAIcomp — Sprint 2
// ============================================================================
#include "ps2recomp/ir_lifter.h"
#include <algorithm>
#include <iostream>
namespace ps2recomp {
using namespace ir;

// Helper: emit a conditional branch (BEQ/BNE/BGEZ/etc.)
void IRLifter::emitCondBranch(IRFunction& func, uint32_t blockIdx,
                               IROp cmpOp, ValueId lhs, ValueId rhs,
                               uint32_t targetAddr, uint32_t srcAddr, bool isLikely) {
    auto cmp = makeBinaryOp(func, cmpOp, IRType::I1, lhs, rhs, srcAddr);
    ValueId cId = cmp.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(cmp));

    // ── Safety check: warn if branch targets external function ─────────
    // MIPS compilers never generate conditional branches to external
    // functions, but if encountered, log it for diagnosis.
    if (targetAddr < currentFuncStart_ || targetAddr >= currentFuncEnd_) {
        std::cerr << "[LIFTER WARNING] Conditional branch at 0x" << std::hex << srcAddr
                  << " targets external address 0x" << targetAddr << std::dec
                  << " — may create empty block\n";
    }

    uint32_t tgtIdx = getOrCreateBlock(func, targetAddr);

    // fall-through block is wired in liftFunction's post-pass

    IRInst br;
    br.op = IROp::IR_BRANCH;
    br.srcAddress = srcAddr;
    br.operands = {cId};
    br.branchTarget = tgtIdx;
    br.branchLikely = isLikely;
    emitTerminator(func, blockIdx, std::move(br), isLikely);

    func.blocks[blockIdx].successors.push_back(tgtIdx);
    func.blocks[tgtIdx].predecessors.push_back(blockIdx);
}

// ── BEQ / BNE ───────────────────────────────────────────────────────────────

void IRLifter::liftBEQ(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, blockIdx, IROp::IR_EQ, rs, rt, target, instr.addr);
}

void IRLifter::liftBNE(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, blockIdx, IROp::IR_NE, rs, rt, target, instr.addr);
}

void IRLifter::liftBEQL(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, blockIdx, IROp::IR_EQ, rs, rt, target, instr.addr, true);
}

void IRLifter::liftBNEL(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, blockIdx, IROp::IR_NE, rs, rt, target, instr.addr, true);
}

void IRLifter::liftB(IRFunction& func, uint32_t blockIdx,
                      const GhidraInstruction& instr,
                      const MIPSFields& f) {
    // b is equivalent to beq zero, zero, offset
    auto zero = emitConst32(func, blockIdx, 0);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, blockIdx, IROp::IR_EQ, zero, zero, target, instr.addr);
}

// ── BGEZ / BGTZ / BLEZ / BLTZ ──────────────────────────────────────────────

void IRLifter::liftBGEZ(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs   = emitGPRRead(func, blockIdx, f.rs, instr.addr, IRType::I64);
    auto zero = emitConst64(func, blockIdx, 0);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    // rs >= 0 ↔ !(rs < 0)
    emitCondBranch(func, blockIdx, IROp::IR_SGE, rs, zero, target, instr.addr);
}

void IRLifter::liftBGTZ(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs   = emitGPRRead(func, blockIdx, f.rs, instr.addr, IRType::I64);
    auto zero = emitConst64(func, blockIdx, 0);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, blockIdx, IROp::IR_SGT, rs, zero, target, instr.addr);
}

void IRLifter::liftBLEZ(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs   = emitGPRRead(func, blockIdx, f.rs, instr.addr, IRType::I64);
    auto zero = emitConst64(func, blockIdx, 0);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, blockIdx, IROp::IR_SLE, rs, zero, target, instr.addr);
}

void IRLifter::liftBLTZ(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs   = emitGPRRead(func, blockIdx, f.rs, instr.addr, IRType::I64);
    auto zero = emitConst64(func, blockIdx, 0);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, blockIdx, IROp::IR_SLT, rs, zero, target, instr.addr);
}

void IRLifter::liftBGEZL(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs   = emitGPRRead(func, blockIdx, f.rs, instr.addr, IRType::I64);
    auto zero = emitConst64(func, blockIdx, 0);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    // isLikely = true
    emitCondBranch(func, blockIdx, IROp::IR_SGE, rs, zero, target, instr.addr, true);
}

void IRLifter::liftBGTZL(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs   = emitGPRRead(func, blockIdx, f.rs, instr.addr, IRType::I64);
    auto zero = emitConst64(func, blockIdx, 0);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, blockIdx, IROp::IR_SGT, rs, zero, target, instr.addr, true);
}

void IRLifter::liftBLEZL(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs   = emitGPRRead(func, blockIdx, f.rs, instr.addr, IRType::I64);
    auto zero = emitConst64(func, blockIdx, 0);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, blockIdx, IROp::IR_SLE, rs, zero, target, instr.addr, true);
}

void IRLifter::liftBLTZL(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs   = emitGPRRead(func, blockIdx, f.rs, instr.addr, IRType::I64);
    auto zero = emitConst64(func, blockIdx, 0);
    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    emitCondBranch(func, blockIdx, IROp::IR_SLT, rs, zero, target, instr.addr, true);
}


// ── J / JAL / JR / JALR ────────────────────────────────────────────────────

void IRLifter::liftJ(IRFunction& func, uint32_t blockIdx,
                      const GhidraInstruction& instr,
                      const MIPSFields& f) {
    uint32_t target = computeJumpTarget(instr.addr, f.target26);

    // ── Tail-call detection ───────────────────────────────────────────────
    // If the jump target is outside the current function's address range,
    // this is a tail-call to an external function, NOT an internal branch.
    // Emit IR_CALL (dispatcher return: ctx->pc = target; return;) instead
    // of IR_BRANCH (goto bb_N) which would create an empty basic block
    // and cause infinite re-entry loops in the dispatcher.
    if (target < currentFuncStart_ || target >= currentFuncEnd_) {
        IRInst tailcall;
        tailcall.op = IROp::IR_CALL;
        tailcall.srcAddress = instr.addr;
        tailcall.branchTarget = target;
        tailcall.comment = "J tail-call (external)";
        emitTerminator(func, blockIdx, std::move(tailcall), false, false);
        return;
    }

    // Internal branch — target is within this function
    uint32_t tgtIdx = getOrCreateBlock(func, target);

    IRInst br;
    br.op = IROp::IR_BRANCH;
    br.srcAddress = instr.addr;
    br.branchTarget = tgtIdx;
    emitTerminator(func, blockIdx, std::move(br), false, false);

    func.blocks[blockIdx].successors.push_back(tgtIdx);
    func.blocks[tgtIdx].predecessors.push_back(blockIdx);
}

void IRLifter::liftJAL(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    uint32_t target = computeJumpTarget(instr.addr, f.target26);
    // Save return address: $ra = PC + 8 (after delay slot)
    auto raVal = emitConstU32(func, blockIdx, instr.addr + 8);
    emitGPRWrite(func, blockIdx, 31, raVal, instr.addr); // $ra = reg 31

    IRInst call;
    call.op = IROp::IR_CALL;
    call.srcAddress = instr.addr;
    call.branchTarget = target; // store call target address
    call.comment = "JAL";
    emitTerminator(func, blockIdx, std::move(call));
}

void IRLifter::liftJR(IRFunction& func, uint32_t blockIdx,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    if (f.rs == 31) {
        // JR $ra = function return
        IRInst ret;
        ret.op = IROp::IR_RETURN;
        ret.srcAddress = instr.addr;
        ret.operands.push_back(rs);
        emitTerminator(func, blockIdx, std::move(ret), false, false);
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
                            func.blocks[blockIdx].successors.push_back(targetIdx);
                            func.blocks[targetIdx].predecessors.push_back(blockIdx);
                        }
                    }
                    emitTerminator(func, blockIdx, std::move(sw), false, false);
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
            emitTerminator(func, blockIdx, std::move(ibr), false, false);
        }
    }
}

void IRLifter::liftJALR(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    // Save return address
    auto raVal = emitConstU32(func, blockIdx, instr.addr + 8);
    emitGPRWrite(func, blockIdx, f.rd, raVal, instr.addr);

    IRInst call;
    call.op = IROp::IR_CALL;
    call.srcAddress = instr.addr;
    call.operands = {rs};
    call.comment = "JALR (indirect call)";
    emitTerminator(func, blockIdx, std::move(call));
}

// ── System instructions ─────────────────────────────────────────────────────

void IRLifter::liftSYSCALL(IRFunction& func, uint32_t blockIdx,
                            const GhidraInstruction& instr,
                            const MIPSFields&) {
    IRInst inst;
    inst.op = IROp::IR_SYSCALL;
    inst.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
}

void IRLifter::liftBREAK(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields&) {
    IRInst inst;
    inst.op = IROp::IR_BREAK;
    inst.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
}

void IRLifter::liftEI(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields&) {
    IRInst inst;
    inst.op = IROp::IR_EI;
    inst.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
}

void IRLifter::liftDI(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields&) {
    IRInst inst;
    inst.op = IROp::IR_DI;
    inst.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
}

void IRLifter::liftSYNC(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields&) {
    IRInst inst;
    inst.op = IROp::IR_NOP;
    inst.srcAddress = instr.addr;
    inst.comment = "SYNC (memory barrier)";
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
}

void IRLifter::liftNOP(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields&) {
    // Explicit NOP — already counted in stats
}

} // namespace ps2recomp
