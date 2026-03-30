// ============================================================================
// ir_lifter_mmi.cpp — MMI (128-bit SIMD) instruction handlers
// Part of PS2reAIcomp
// ============================================================================
#include "ps2recomp/ir_lifter.h"

namespace ps2recomp {
using namespace ir;

// ── Generic MMI Macro for 3-register R-Type (rd = rs OP rt) ────────────────
#define LIFT_MMI_R3(Name, Op)                                              \
void IRLifter::lift##Name(IRFunction& func, uint32_t blockIdx,             \
                          const GhidraInstruction& instr,                  \
                          const MIPSFields& f) {                           \
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr, IRType::I128); \
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr, IRType::I128); \
    auto inst = makeBinaryOp(func, IROp::Op, IRType::I128, rs, rt, instr.addr); \
    ValueId vid = inst.result.id;                                          \
    func.blocks[blockIdx].instructions.push_back(std::move(inst));         \
    emitGPRWrite(func, blockIdx, f.rd, vid, instr.addr);                   \
}

LIFT_MMI_R3(PADDH,   IR_PADDH)
LIFT_MMI_R3(PADDW,   IR_PADDW)
LIFT_MMI_R3(PSUBB,   IR_PSUBB)
LIFT_MMI_R3(PSUBH,   IR_PSUBH)
LIFT_MMI_R3(PSUBW,   IR_PSUBW)

LIFT_MMI_R3(PMAXH,   IR_PMAXH)
LIFT_MMI_R3(PMINH,   IR_PMINH)

LIFT_MMI_R3(PAND,    IR_PAND)
LIFT_MMI_R3(POR,     IR_POR)
LIFT_MMI_R3(PXOR,    IR_PXOR)
LIFT_MMI_R3(PNOR,    IR_PNOR)

LIFT_MMI_R3(PCEQB,   IR_PCEQB)
LIFT_MMI_R3(PCEQH,   IR_PCEQH)
LIFT_MMI_R3(PCEQW,   IR_PCEQW)
LIFT_MMI_R3(PCGTH,   IR_PCGTH)

LIFT_MMI_R3(PEXTLB,  IR_PEXTLB)
LIFT_MMI_R3(PEXTLW,  IR_PEXTLW)
LIFT_MMI_R3(PEXTUB,  IR_PEXTUB)
LIFT_MMI_R3(PEXTUW,  IR_PEXTUW)

LIFT_MMI_R3(PCPYLD,  IR_PCPYLD)
LIFT_MMI_R3(PCPYUD,  IR_PCPYUD)

LIFT_MMI_R3(PINTEH,  IR_PINTEH)

LIFT_MMI_R3(PPACB,   IR_PPACB)
LIFT_MMI_R3(PPACW,   IR_PPACW)

#undef LIFT_MMI_R3

// ── Generic MMI Macro for 2-register + SA (Shift Amount) ────────────────
#define LIFT_MMI_SHIFT(Name, Op)                                           \
void IRLifter::lift##Name(IRFunction& func, uint32_t blockIdx,             \
                          const GhidraInstruction& instr,                  \
                          const MIPSFields& f) {                           \
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr, IRType::I128); \
    auto sa = emitConst32(func, blockIdx, f.sa);                           \
    auto inst = makeBinaryOp(func, IROp::Op, IRType::I128, rt, sa, instr.addr); \
    ValueId vid = inst.result.id;                                          \
    func.blocks[blockIdx].instructions.push_back(std::move(inst));         \
    emitGPRWrite(func, blockIdx, f.rd, vid, instr.addr);                   \
}

LIFT_MMI_SHIFT(PSLLH, IR_PSLLH)
LIFT_MMI_SHIFT(PSLLW, IR_PSLLW)
LIFT_MMI_SHIFT(PSRAH, IR_PSRAH)
LIFT_MMI_SHIFT(PSRLH, IR_PSRLH)
LIFT_MMI_SHIFT(PSRLW, IR_PSRLW)

#undef LIFT_MMI_SHIFT

// ── Generic MMI Macro for 2-register R-Type (rd = OP rt) ────────────────
#define LIFT_MMI_R2(Name, Op)                                              \
void IRLifter::lift##Name(IRFunction& func, uint32_t blockIdx,             \
                          const GhidraInstruction& instr,                  \
                          const MIPSFields& f) {                           \
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr, IRType::I128); \
    auto inst = makeUnaryOp(func, IROp::Op, IRType::I128, rt, instr.addr); \
    ValueId vid = inst.result.id;                                          \
    func.blocks[blockIdx].instructions.push_back(std::move(inst));         \
    emitGPRWrite(func, blockIdx, f.rd, vid, instr.addr);                   \
}

LIFT_MMI_R2(PCPYH,   IR_PCPYH)
LIFT_MMI_R2(PEXCH,   IR_PEXCH)
LIFT_MMI_R2(PEXCW,   IR_PEXCW)
LIFT_MMI_R2(PEXEH,   IR_PEXEH)
LIFT_MMI_R2(PEXEW,   IR_PEXEW)
LIFT_MMI_R2(PLZCW,   IR_PLZCW)
LIFT_MMI_R2(PREVH,   IR_PREVH)
LIFT_MMI_R2(PROT3W,  IR_PROT3W)

#undef LIFT_MMI_R2

// ── Special MMI Instructions ────────────────────────────────────────────────

// QFSRV rd, rt, rs
void IRLifter::liftQFSRV(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr, IRType::I128);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr, IRType::I128);
    
    auto sa_read = makeRegRead(func, IRType::I32, IRReg::sa());
    sa_read.srcAddress = instr.addr;
    ValueId sa_val = sa_read.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(sa_read));

    IRInst inst;
    inst.op = IROp::IR_QFSRV;
    inst.result = func.allocTypedValue(IRType::I128);
    inst.operands = {rs, rt, sa_val};
    inst.srcAddress = instr.addr;
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    emitGPRWrite(func, blockIdx, f.rd, vid, instr.addr);
}

// PMFHL.LH, PMFHL.LW, PMFHL.UW (opcode is PMFHL, differentiated by sa/instruction text)
void IRLifter::liftPMFHL(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto hi = makeRegRead(func, IRType::I128, IRReg::hi());
    hi.srcAddress = instr.addr;
    ValueId hiVal = hi.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(hi));

    auto lo = makeRegRead(func, IRType::I128, IRReg::lo());
    lo.srcAddress = instr.addr;
    ValueId loVal = lo.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(lo));

    // The sub-function is passed via the SA field (or we can extract it if needed).
    // Usually sa=0 for LW, sa=1 for UW, sa=2 for SLW, sa=3 for LH.
    // In our IROp we use the SA value as the third operand to the backend function.
    auto subFunc = emitConst32(func, blockIdx, f.sa);

    IRInst inst;
    inst.op = IROp::IR_PMFHL;
    inst.result = func.allocTypedValue(IRType::I128);
    inst.operands = {hiVal, loVal, subFunc};
    inst.srcAddress = instr.addr;
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));

    emitGPRWrite(func, blockIdx, f.rd, vid, instr.addr);
}

// PMTHI rs
void IRLifter::liftPMTHI(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr, IRType::I128);
    auto inst = ir::makeRegWrite(IRReg::hi(), rs);
    inst.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
}

// PMTLO rs
void IRLifter::liftPMTLO(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr, IRType::I128);
    auto inst = ir::makeRegWrite(IRReg::lo(), rs);
    inst.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
}

} // namespace ps2recomp
