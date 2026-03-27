// ============================================================================
// ir_lifter_fpu.cpp — FPU (COP1) instruction handlers
// Part of PS2reAIcomp — Sprint 2
// ============================================================================
#include "ps2recomp/ir_lifter.h"
namespace ps2recomp {
using namespace ir;

// ── FPU arithmetic (.s = single-precision float) ────────────────────────────

#define LIFT_FPU_BIN(Name, Op)                                             \
void IRLifter::lift##Name(IRFunction& func, IRBasicBlock& bb,              \
                          const GhidraInstruction& instr,                  \
                          const MIPSFields& f) {                           \
    auto fs = emitFPRRead(func, bb, f.fs, instr.addr);                     \
    auto ft = emitFPRRead(func, bb, f.ft, instr.addr);                     \
    auto inst = makeBinaryOp(func, IROp::Op, IRType::F32, fs, ft,          \
                             instr.addr);                                  \
    ValueId vid = inst.result.id;                                          \
    bb.instructions.push_back(std::move(inst));                            \
    emitFPRWrite(func, bb, f.fd, vid, instr.addr);                         \
}

LIFT_FPU_BIN(ADD_S, IR_FADD)
LIFT_FPU_BIN(SUB_S, IR_FSUB)
LIFT_FPU_BIN(MUL_S, IR_FMUL)
LIFT_FPU_BIN(DIV_S, IR_FDIV)
#undef LIFT_FPU_BIN

// ── FPU unary operations ────────────────────────────────────────────────────

void IRLifter::liftMOV_S(IRFunction& func, IRBasicBlock& bb,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto fs = emitFPRRead(func, bb, f.fs, instr.addr);
    emitFPRWrite(func, bb, f.fd, fs, instr.addr);
}

void IRLifter::liftNEG_S(IRFunction& func, IRBasicBlock& bb,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto fs = emitFPRRead(func, bb, f.fs, instr.addr);
    auto inst = makeUnaryOp(func, IROp::IR_FNEG, IRType::F32, fs, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitFPRWrite(func, bb, f.fd, vid, instr.addr);
}

void IRLifter::liftABS_S(IRFunction& func, IRBasicBlock& bb,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto fs = emitFPRRead(func, bb, f.fs, instr.addr);
    auto inst = makeUnaryOp(func, IROp::IR_FABS, IRType::F32, fs, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitFPRWrite(func, bb, f.fd, vid, instr.addr);
}

void IRLifter::liftSQRT_S(IRFunction& func, IRBasicBlock& bb,
                            const GhidraInstruction& instr,
                            const MIPSFields& f) {
    auto fs = emitFPRRead(func, bb, f.fs, instr.addr);
    auto inst = makeUnaryOp(func, IROp::IR_FSQRT, IRType::F32, fs, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitFPRWrite(func, bb, f.fd, vid, instr.addr);
}

// ── MFC1 / MTC1 (move between GPR and FPR) ──────────────────────────────────

void IRLifter::liftMFC1(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto fp = emitFPRRead(func, bb, f.fs, instr.addr);
    auto bcast = makeUnaryOp(func, IROp::IR_BITCAST, IRType::I32, fp, instr.addr);
    ValueId bId = bcast.result.id;
    bb.instructions.push_back(std::move(bcast));
    emitGPRWrite(func, bb, f.rt, bId, instr.addr);
}

void IRLifter::liftMTC1(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto gp = emitGPRRead(func, bb, f.rt, instr.addr);
    auto bcast = makeUnaryOp(func, IROp::IR_BITCAST, IRType::F32, gp, instr.addr);
    ValueId bId = bcast.result.id;
    bb.instructions.push_back(std::move(bcast));
    emitFPRWrite(func, bb, f.fs, bId, instr.addr);
}

// ── Conversions ─────────────────────────────────────────────────────────────

void IRLifter::liftCVT_S_W(IRFunction& func, IRBasicBlock& bb,
                             const GhidraInstruction& instr,
                             const MIPSFields& f) {
    auto fs = emitFPRRead(func, bb, f.fs, instr.addr);
    // Bitcast FPR to I32 then convert int->float
    auto bcI = makeUnaryOp(func, IROp::IR_BITCAST, IRType::I32, fs, instr.addr);
    ValueId iId = bcI.result.id;
    bb.instructions.push_back(std::move(bcI));
    auto cvt = makeUnaryOp(func, IROp::IR_SITOFP, IRType::F32, iId, instr.addr);
    ValueId cId = cvt.result.id;
    bb.instructions.push_back(std::move(cvt));
    emitFPRWrite(func, bb, f.fd, cId, instr.addr);
}

void IRLifter::liftCVT_W_S(IRFunction& func, IRBasicBlock& bb,
                             const GhidraInstruction& instr,
                             const MIPSFields& f) {
    auto fs = emitFPRRead(func, bb, f.fs, instr.addr);
    auto cvt = makeUnaryOp(func, IROp::IR_FPTOSI, IRType::I32, fs, instr.addr);
    ValueId cId = cvt.result.id;
    bb.instructions.push_back(std::move(cvt));
    // Bitcast I32 back to F32 for FPR storage
    auto bcF = makeUnaryOp(func, IROp::IR_BITCAST, IRType::F32, cId, instr.addr);
    ValueId bId = bcF.result.id;
    bb.instructions.push_back(std::move(bcF));
    emitFPRWrite(func, bb, f.fd, bId, instr.addr);
}

// ── FPU load/store ──────────────────────────────────────────────────────────

void IRLifter::liftLWC1(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::F32, addr, instr.addr);
    ValueId lId = ld.result.id;
    bb.instructions.push_back(std::move(ld));
    emitFPRWrite(func, bb, f.ft, lId, instr.addr);
}

void IRLifter::liftSWC1(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto val  = emitFPRRead(func, bb, f.ft, instr.addr);
    auto st = makeStore(IRType::F32, addr, val, instr.addr);
    bb.instructions.push_back(std::move(st));
}

// ── FPU compare ─────────────────────────────────────────────────────────────

static void emitFPUCompare(IRLifter& self, IRFunction& func, IRBasicBlock& bb,
                            IROp cmpOp, const GhidraInstruction& instr,
                            const IRLifter::MIPSFields& f) {
    auto fs = self.emitFPRRead(func, bb, f.fs, instr.addr);
    auto ft = self.emitFPRRead(func, bb, f.ft, instr.addr);
    auto cmp = ir::makeBinaryOp(func, cmpOp, IRType::I1, fs, ft, instr.addr);
    ValueId cId = cmp.result.id;
    bb.instructions.push_back(std::move(cmp));
    // Write to FCC (FPU condition code) register
    auto w = ir::makeRegWrite(IRReg::fcc(), cId);
    w.srcAddress = instr.addr;
    bb.instructions.push_back(std::move(w));
}

void IRLifter::liftC_EQ_S(IRFunction& func, IRBasicBlock& bb,
                           const GhidraInstruction& instr,
                           const MIPSFields& f) {
    emitFPUCompare(*this, func, bb, IROp::IR_FEQ, instr, f);
}

void IRLifter::liftC_LT_S(IRFunction& func, IRBasicBlock& bb,
                           const GhidraInstruction& instr,
                           const MIPSFields& f) {
    emitFPUCompare(*this, func, bb, IROp::IR_FLT, instr, f);
}

void IRLifter::liftC_LE_S(IRFunction& func, IRBasicBlock& bb,
                           const GhidraInstruction& instr,
                           const MIPSFields& f) {
    emitFPUCompare(*this, func, bb, IROp::IR_FLE, instr, f);
}

// ── BC1T / BC1F (branch on FPU condition) ───────────────────────────────────

void IRLifter::liftBC1T(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto fcc = ir::makeRegRead(func, IRType::I1, IRReg::fcc());
    fcc.srcAddress = instr.addr;
    ValueId fcId = fcc.result.id;
    bb.instructions.push_back(std::move(fcc));

    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    auto one = emitConst32(func, bb, 1);
    emitCondBranch(func, bb, IROp::IR_EQ, fcId, one, target, instr.addr);
}

void IRLifter::liftBC1F(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto fcc = ir::makeRegRead(func, IRType::I1, IRReg::fcc());
    fcc.srcAddress = instr.addr;
    ValueId fcId = fcc.result.id;
    bb.instructions.push_back(std::move(fcc));

    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    auto zero = emitConst32(func, bb, 0);
    emitCondBranch(func, bb, IROp::IR_EQ, fcId, zero, target, instr.addr);
}

void IRLifter::liftBC1TL(IRFunction& func, IRBasicBlock& bb,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    liftBC1T(func, bb, instr, f); // likely variant
}

void IRLifter::liftBC1FL(IRFunction& func, IRBasicBlock& bb,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    liftBC1F(func, bb, instr, f); // likely variant
}

} // namespace ps2recomp
