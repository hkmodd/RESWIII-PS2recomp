// ============================================================================
// ir_lifter_fpu.cpp — FPU (COP1) instruction handlers
// Part of PS2reAIcomp — Sprint 2
// ============================================================================
#include "ps2recomp/ir_lifter.h"
namespace ps2recomp {
using namespace ir;

// ── FPU arithmetic (.s = single-precision float) ────────────────────────────

#define LIFT_FPU_BIN(Name, Op)                                             \
void IRLifter::lift##Name(IRFunction& func, uint32_t blockIdx,              \
                          const GhidraInstruction& instr,                  \
                          const MIPSFields& f) {                           \
    auto fs = emitFPRRead(func, blockIdx, f.fs, instr.addr);                     \
    auto ft = emitFPRRead(func, blockIdx, f.ft, instr.addr);                     \
    auto inst = makeBinaryOp(func, IROp::Op, IRType::F32, fs, ft,          \
                             instr.addr);                                  \
    ValueId vid = inst.result.id;                                          \
    func.blocks[blockIdx].instructions.push_back(std::move(inst));                            \
    emitFPRWrite(func, blockIdx, f.fd, vid, instr.addr);                         \
}

LIFT_FPU_BIN(ADD_S, IR_FADD)

void IRLifter::liftADDA_S(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto fs = emitFPRRead(func, blockIdx, f.fs, instr.addr);
    auto ft = emitFPRRead(func, blockIdx, f.ft, instr.addr);
    auto inst = makeBinaryOp(func, IROp::IR_FADDA, IRType::F32, fs, ft, instr.addr);
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    auto w = ir::makeRegWrite(IRReg::fpuAcc(), vid);
    w.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(w));
}
LIFT_FPU_BIN(SUB_S, IR_FSUB)
LIFT_FPU_BIN(MUL_S, IR_FMUL)
LIFT_FPU_BIN(DIV_S, IR_FDIV)
#undef LIFT_FPU_BIN

// ── FPU unary operations ────────────────────────────────────────────────────

void IRLifter::liftMOV_S(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto fs = emitFPRRead(func, blockIdx, f.fs, instr.addr);
    emitFPRWrite(func, blockIdx, f.fd, fs, instr.addr);
}

void IRLifter::liftNEG_S(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto fs = emitFPRRead(func, blockIdx, f.fs, instr.addr);
    auto inst = makeUnaryOp(func, IROp::IR_FNEG, IRType::F32, fs, instr.addr);
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    emitFPRWrite(func, blockIdx, f.fd, vid, instr.addr);
}

void IRLifter::liftABS_S(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto fs = emitFPRRead(func, blockIdx, f.fs, instr.addr);
    auto inst = makeUnaryOp(func, IROp::IR_FABS, IRType::F32, fs, instr.addr);
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    emitFPRWrite(func, blockIdx, f.fd, vid, instr.addr);
}

void IRLifter::liftSQRT_S(IRFunction& func, uint32_t blockIdx,
                            const GhidraInstruction& instr,
                            const MIPSFields& f) {
    auto fs = emitFPRRead(func, blockIdx, f.fs, instr.addr);
    auto inst = makeUnaryOp(func, IROp::IR_FSQRT, IRType::F32, fs, instr.addr);
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    emitFPRWrite(func, blockIdx, f.fd, vid, instr.addr);
}

// ── MFC1 / MTC1 (move between GPR and FPR) ──────────────────────────────────

void IRLifter::liftMFC1(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto fp = emitFPRRead(func, blockIdx, f.fs, instr.addr);
    auto bcast = makeUnaryOp(func, IROp::IR_BITCAST, IRType::I32, fp, instr.addr);
    ValueId bId = bcast.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(bcast));
    emitGPRWrite(func, blockIdx, f.rt, bId, instr.addr);
}

void IRLifter::liftMTC1(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto gp = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto bcast = makeUnaryOp(func, IROp::IR_BITCAST, IRType::F32, gp, instr.addr);
    ValueId bId = bcast.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(bcast));
    emitFPRWrite(func, blockIdx, f.fs, bId, instr.addr);
}

// ── Conversions ─────────────────────────────────────────────────────────────

void IRLifter::liftCVT_S_W(IRFunction& func, uint32_t blockIdx,
                             const GhidraInstruction& instr,
                             const MIPSFields& f) {
    auto fs = emitFPRRead(func, blockIdx, f.fs, instr.addr);
    // Bitcast FPR to I32 then convert int->float
    auto bcI = makeUnaryOp(func, IROp::IR_BITCAST, IRType::I32, fs, instr.addr);
    ValueId iId = bcI.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(bcI));
    auto cvt = makeUnaryOp(func, IROp::IR_CVT_S_W, IRType::F32, iId, instr.addr);
    ValueId cId = cvt.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(cvt));
    emitFPRWrite(func, blockIdx, f.fd, cId, instr.addr);
}

void IRLifter::liftCVT_W_S(IRFunction& func, uint32_t blockIdx,
                             const GhidraInstruction& instr,
                             const MIPSFields& f) {
    auto fs = emitFPRRead(func, blockIdx, f.fs, instr.addr);
    auto cvt = makeUnaryOp(func, IROp::IR_TRUNC_W_S, IRType::I32, fs, instr.addr);
    ValueId cId = cvt.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(cvt));
    // Bitcast I32 back to F32 for FPR storage
    auto bcF = makeUnaryOp(func, IROp::IR_BITCAST, IRType::F32, cId, instr.addr);
    ValueId bId = bcF.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(bcF));
    emitFPRWrite(func, blockIdx, f.fd, bId, instr.addr);
}

// ── FPU load/store ──────────────────────────────────────────────────────────

void IRLifter::liftLWC1(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::F32, addr, instr.addr);
    ValueId lId = ld.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(ld));
    emitFPRWrite(func, blockIdx, f.ft, lId, instr.addr);
}

void IRLifter::liftSWC1(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto val  = emitFPRRead(func, blockIdx, f.ft, instr.addr);
    auto st = makeStore(IRType::F32, addr, val, instr.addr);
    func.blocks[blockIdx].instructions.push_back(std::move(st));
}

// ── FPU compare ─────────────────────────────────────────────────────────────

void IRLifter::emitFPUCompare(IRFunction& func, uint32_t blockIdx,
                            IROp cmpOp, const GhidraInstruction& instr,
                            const IRLifter::MIPSFields& f) {
    auto fs = emitFPRRead(func, blockIdx, f.fs, instr.addr);
    auto ft = emitFPRRead(func, blockIdx, f.ft, instr.addr);
    auto cmp = ir::makeBinaryOp(func, cmpOp, IRType::I1, fs, ft, instr.addr);
    ValueId cId = cmp.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(cmp));
    // Write to FCC (FPU condition code) register
    auto w = ir::makeRegWrite(IRReg::fpuCC(), cId);
    w.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(w));
}

void IRLifter::liftC_EQ_S(IRFunction& func, uint32_t blockIdx,
                           const GhidraInstruction& instr,
                           const MIPSFields& f) {
    emitFPUCompare(func, blockIdx, IROp::IR_FCMP_EQ, instr, f);
}

void IRLifter::liftC_LT_S(IRFunction& func, uint32_t blockIdx,
                           const GhidraInstruction& instr,
                           const MIPSFields& f) {
    emitFPUCompare(func, blockIdx, IROp::IR_FCMP_LT, instr, f);
}

void IRLifter::liftC_LE_S(IRFunction& func, uint32_t blockIdx,
                           const GhidraInstruction& instr,
                           const MIPSFields& f) {
    emitFPUCompare(func, blockIdx, IROp::IR_FCMP_LE, instr, f);
}

// ── BC1T / BC1F (branch on FPU condition) ───────────────────────────────────

void IRLifter::liftBC1T(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto fcc = ir::makeRegRead(func, IRType::I1, IRReg::fpuCC());
    fcc.srcAddress = instr.addr;
    ValueId fcId = fcc.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(fcc));

    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    auto one = emitConst32(func, blockIdx, 1);
    emitCondBranch(func, blockIdx, IROp::IR_EQ, fcId, one, target, instr.addr);
}

void IRLifter::liftBC1F(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto fcc = ir::makeRegRead(func, IRType::I1, IRReg::fpuCC());
    fcc.srcAddress = instr.addr;
    ValueId fcId = fcc.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(fcc));

    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    auto zero = emitConst32(func, blockIdx, 0);
    emitCondBranch(func, blockIdx, IROp::IR_EQ, fcId, zero, target, instr.addr);
}

void IRLifter::liftBC1TL(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    liftBC1T(func, blockIdx, instr, f); // likely variant
}

void IRLifter::liftBC1FL(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    liftBC1F(func, blockIdx, instr, f); // likely variant
}

} // namespace ps2recomp
