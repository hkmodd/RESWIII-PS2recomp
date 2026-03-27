// ============================================================================
// ir_lifter_alu.cpp — Integer ALU instruction handlers
// Part of PS2reAIcomp — Sprint 2
// ============================================================================
#include "ps2recomp/ir_lifter.h"
namespace ps2recomp {
using namespace ir;

// ── Three-register ALU (R-type: rd = rs OP rt) ─────────────────────────────

#define LIFT_R_ARITH(Name, Op, Type)                                       \
void IRLifter::lift##Name(IRFunction& func, IRBasicBlock& bb,              \
                          const GhidraInstruction& instr,                  \
                          const MIPSFields& f) {                           \
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);                     \
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);                     \
    auto inst = makeBinaryOp(func, IROp::Op, IRType::Type, rs, rt,         \
                             instr.addr);                                  \
    ValueId vid = inst.result.id;                                          \
    bb.instructions.push_back(std::move(inst));                            \
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);                         \
}

LIFT_R_ARITH(ADD,   IR_ADD,  I32)
LIFT_R_ARITH(ADDU,  IR_ADD,  I32)
LIFT_R_ARITH(SUB,   IR_SUB,  I32)
LIFT_R_ARITH(SUBU,  IR_SUB,  I32)
LIFT_R_ARITH(DADD,  IR_ADD,  I64)
LIFT_R_ARITH(DADDU, IR_ADD,  I64)
LIFT_R_ARITH(AND,   IR_AND,  I32)
LIFT_R_ARITH(OR,    IR_OR,   I32)
LIFT_R_ARITH(XOR,   IR_XOR,  I32)
#undef LIFT_R_ARITH

void IRLifter::liftNOR(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto rs  = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt  = emitGPRRead(func, bb, f.rt, instr.addr);
    auto orv = makeBinaryOp(func, IROp::IR_OR, IRType::I32, rs, rt, instr.addr);
    ValueId orId = orv.result.id;
    bb.instructions.push_back(std::move(orv));
    auto notv = makeUnaryOp(func, IROp::IR_NOT, IRType::I32, orId, instr.addr);
    ValueId nId = notv.result.id;
    bb.instructions.push_back(std::move(notv));
    emitGPRWrite(func, bb, f.rd, nId, instr.addr);
}

// ── Immediate ALU (I-type: rt = rs OP imm) ─────────────────────────────────

#define LIFT_I_ARITH(Name, Op, Type, signedImm)                            \
void IRLifter::lift##Name(IRFunction& func, IRBasicBlock& bb,              \
                          const GhidraInstruction& instr,                  \
                          const MIPSFields& f) {                           \
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);                     \
    ValueId imm;                                                           \
    if constexpr (signedImm)                                               \
        imm = emitConst32(func, bb, static_cast<int32_t>(f.simm16));       \
    else                                                                   \
        imm = emitConstU32(func, bb, static_cast<uint32_t>(f.imm16));      \
    auto inst = makeBinaryOp(func, IROp::Op, IRType::Type, rs, imm,        \
                             instr.addr);                                  \
    ValueId vid = inst.result.id;                                          \
    bb.instructions.push_back(std::move(inst));                            \
    emitGPRWrite(func, bb, f.rt, vid, instr.addr);                         \
}

LIFT_I_ARITH(ADDI,   IR_ADD,  I32, true)
LIFT_I_ARITH(ADDIU,  IR_ADD,  I32, true)
LIFT_I_ARITH(DADDI,  IR_ADD,  I64, true)
LIFT_I_ARITH(DADDIU, IR_ADD,  I64, true)
LIFT_I_ARITH(ANDI,   IR_AND,  I32, false)
LIFT_I_ARITH(ORI,    IR_OR,   I32, false)
LIFT_I_ARITH(XORI,   IR_XOR,  I32, false)
#undef LIFT_I_ARITH

// ── LUI: rt = imm << 16 ────────────────────────────────────────────────────

void IRLifter::liftLUI(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    uint32_t val = static_cast<uint32_t>(f.imm16) << 16;
    auto cv = emitConstU32(func, bb, val);
    emitGPRWrite(func, bb, f.rt, cv, instr.addr);
}

// ── Shifts (constant shamt) ─────────────────────────────────────────────────

#define LIFT_SHIFT_CONST(Name, Op)                                         \
void IRLifter::lift##Name(IRFunction& func, IRBasicBlock& bb,              \
                          const GhidraInstruction& instr,                  \
                          const MIPSFields& f) {                           \
    auto rt  = emitGPRRead(func, bb, f.rt, instr.addr);                    \
    auto sa  = emitConst32(func, bb, f.sa);                                \
    auto inst = makeBinaryOp(func, IROp::Op, IRType::I32, rt, sa,          \
                             instr.addr);                                  \
    ValueId vid = inst.result.id;                                          \
    bb.instructions.push_back(std::move(inst));                            \
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);                         \
}

LIFT_SHIFT_CONST(SLL, IR_SLL)
LIFT_SHIFT_CONST(SRL, IR_SRL)
LIFT_SHIFT_CONST(SRA, IR_SRA)
#undef LIFT_SHIFT_CONST

// ── Shifts (variable: rd = rt << rs) ────────────────────────────────────────

#define LIFT_SHIFT_VAR(Name, Op)                                           \
void IRLifter::lift##Name(IRFunction& func, IRBasicBlock& bb,              \
                          const GhidraInstruction& instr,                  \
                          const MIPSFields& f) {                           \
    auto rt  = emitGPRRead(func, bb, f.rt, instr.addr);                    \
    auto rs  = emitGPRRead(func, bb, f.rs, instr.addr);                    \
    auto inst = makeBinaryOp(func, IROp::Op, IRType::I32, rt, rs,          \
                             instr.addr);                                  \
    ValueId vid = inst.result.id;                                          \
    bb.instructions.push_back(std::move(inst));                            \
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);                         \
}

LIFT_SHIFT_VAR(SLLV, IR_SLL)
LIFT_SHIFT_VAR(SRLV, IR_SRL)
LIFT_SHIFT_VAR(SRAV, IR_SRA)
#undef LIFT_SHIFT_VAR

// ── Set on less than ────────────────────────────────────────────────────────

void IRLifter::liftSLT(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto cmp = makeBinaryOp(func, IROp::IR_SLT, IRType::I1, rs, rt, instr.addr);
    ValueId cId = cmp.result.id;
    bb.instructions.push_back(std::move(cmp));
    auto zext = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I32, cId, instr.addr);
    ValueId zId = zext.result.id;
    bb.instructions.push_back(std::move(zext));
    emitGPRWrite(func, bb, f.rd, zId, instr.addr);
}

void IRLifter::liftSLTU(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto cmp = makeBinaryOp(func, IROp::IR_SLTU, IRType::I1, rs, rt, instr.addr);
    ValueId cId = cmp.result.id;
    bb.instructions.push_back(std::move(cmp));
    auto zext = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I32, cId, instr.addr);
    ValueId zId = zext.result.id;
    bb.instructions.push_back(std::move(zext));
    emitGPRWrite(func, bb, f.rd, zId, instr.addr);
}

void IRLifter::liftSLTI(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs  = emitGPRRead(func, bb, f.rs, instr.addr);
    auto imm = emitConst32(func, bb, static_cast<int32_t>(f.simm16));
    auto cmp = makeBinaryOp(func, IROp::IR_SLT, IRType::I1, rs, imm, instr.addr);
    ValueId cId = cmp.result.id;
    bb.instructions.push_back(std::move(cmp));
    auto zext = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I32, cId, instr.addr);
    ValueId zId = zext.result.id;
    bb.instructions.push_back(std::move(zext));
    emitGPRWrite(func, bb, f.rt, zId, instr.addr);
}

void IRLifter::liftSLTIU(IRFunction& func, IRBasicBlock& bb,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto rs  = emitGPRRead(func, bb, f.rs, instr.addr);
    auto imm = emitConst32(func, bb, static_cast<int32_t>(f.simm16));
    auto cmp = makeBinaryOp(func, IROp::IR_SLTU, IRType::I1, rs, imm, instr.addr);
    ValueId cId = cmp.result.id;
    bb.instructions.push_back(std::move(cmp));
    auto zext = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I32, cId, instr.addr);
    ValueId zId = zext.result.id;
    bb.instructions.push_back(std::move(zext));
    emitGPRWrite(func, bb, f.rt, zId, instr.addr);
}

// ── Multiply / Divide ───────────────────────────────────────────────────────

void IRLifter::liftMULT(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto mul = makeBinaryOp(func, IROp::IR_MUL, IRType::I64, rs, rt, instr.addr);
    ValueId mId = mul.result.id;
    bb.instructions.push_back(std::move(mul));
    // LO = low32, HI = high32  (approximate: store full result in LO)
    auto wLo = makeRegWrite(IRReg::lo(), mId);
    wLo.srcAddress = instr.addr;
    bb.instructions.push_back(std::move(wLo));
    // Shift right 32 for HI
    auto c32 = emitConst32(func, bb, 32);
    auto hi = makeBinaryOp(func, IROp::IR_SRL, IRType::I64, mId, c32, instr.addr);
    ValueId hId = hi.result.id;
    bb.instructions.push_back(std::move(hi));
    auto wHi = makeRegWrite(IRReg::hi(), hId);
    wHi.srcAddress = instr.addr;
    bb.instructions.push_back(std::move(wHi));
}

void IRLifter::liftMULTU(IRFunction& func, IRBasicBlock& bb,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    // Same structure as MULT, semantically unsigned
    liftMULT(func, bb, instr, f);
}

void IRLifter::liftDIV(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    // LO = quotient
    auto dv = makeBinaryOp(func, IROp::IR_DIV, IRType::I32, rs, rt, instr.addr);
    ValueId qId = dv.result.id;
    bb.instructions.push_back(std::move(dv));
    auto wLo = makeRegWrite(IRReg::lo(), qId);
    wLo.srcAddress = instr.addr;
    bb.instructions.push_back(std::move(wLo));
    // HI = remainder
    auto rm = makeBinaryOp(func, IROp::IR_MOD, IRType::I32, rs, rt, instr.addr);
    ValueId rId = rm.result.id;
    bb.instructions.push_back(std::move(rm));
    auto wHi = makeRegWrite(IRReg::hi(), rId);
    wHi.srcAddress = instr.addr;
    bb.instructions.push_back(std::move(wHi));
}

void IRLifter::liftDIVU(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto dv = makeBinaryOp(func, IROp::IR_DIVU, IRType::I32, rs, rt, instr.addr);
    ValueId qId = dv.result.id;
    bb.instructions.push_back(std::move(dv));
    auto wLo = makeRegWrite(IRReg::lo(), qId);
    wLo.srcAddress = instr.addr;
    bb.instructions.push_back(std::move(wLo));
    // HI = remainder (unsigned)
    auto rm = makeBinaryOp(func, IROp::IR_MODU, IRType::I32, rs, rt, instr.addr);
    ValueId rId = rm.result.id;
    bb.instructions.push_back(std::move(rm));
    auto wHi = makeRegWrite(IRReg::hi(), rId);
    wHi.srcAddress = instr.addr;
    bb.instructions.push_back(std::move(wHi));
}

// ── HI/LO move ──────────────────────────────────────────────────────────────

void IRLifter::liftMFHI(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto inst = makeRegRead(func, IRType::I32, IRReg::hi());
    inst.srcAddress = instr.addr;
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

void IRLifter::liftMFLO(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto inst = makeRegRead(func, IRType::I32, IRReg::lo());
    inst.srcAddress = instr.addr;
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

void IRLifter::liftMTHI(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto w = makeRegWrite(IRReg::hi(), rs);
    w.srcAddress = instr.addr;
    bb.instructions.push_back(std::move(w));
}

void IRLifter::liftMTLO(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto w = makeRegWrite(IRReg::lo(), rs);
    w.srcAddress = instr.addr;
    bb.instructions.push_back(std::move(w));
}

// ── Conditional moves ───────────────────────────────────────────────────────

void IRLifter::liftMOVZ(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto zero = emitConst32(func, bb, 0);
    auto cmp = makeBinaryOp(func, IROp::IR_EQ, IRType::I1, rt, zero, instr.addr);
    ValueId cId = cmp.result.id;
    bb.instructions.push_back(std::move(cmp));
    auto rd_old = emitGPRRead(func, bb, f.rd, instr.addr);
    auto sel = makeBinaryOp(func, IROp::IR_SELECT, IRType::I32, cId, rs, instr.addr);
    // SELECT needs 3 operands: cond, trueVal, falseVal
    sel.operands.push_back(rd_old);
    ValueId sId = sel.result.id;
    bb.instructions.push_back(std::move(sel));
    emitGPRWrite(func, bb, f.rd, sId, instr.addr);
}

void IRLifter::liftMOVN(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto zero = emitConst32(func, bb, 0);
    auto cmp = makeBinaryOp(func, IROp::IR_NE, IRType::I1, rt, zero, instr.addr);
    ValueId cId = cmp.result.id;
    bb.instructions.push_back(std::move(cmp));
    auto rd_old = emitGPRRead(func, bb, f.rd, instr.addr);
    auto sel = makeBinaryOp(func, IROp::IR_SELECT, IRType::I32, cId, rs, instr.addr);
    sel.operands.push_back(rd_old);
    ValueId sId = sel.result.id;
    bb.instructions.push_back(std::move(sel));
    emitGPRWrite(func, bb, f.rd, sId, instr.addr);
}

// ── MMI (128-bit) Instructions ─────────────────────────────────────────────

void IRLifter::liftPADDB(IRFunction& func, IRBasicBlock& bb,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto inst = makeBinaryOp(func, IROp::IR_PADDB, IRType::I128, rs, rt, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

void IRLifter::liftPEXTUW(IRFunction& func, IRBasicBlock& bb,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto inst = makeBinaryOp(func, IROp::IR_PEXTUW, IRType::I128, rs, rt, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

void IRLifter::liftPCPYLD(IRFunction& func, IRBasicBlock& bb,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto inst = makeBinaryOp(func, IROp::IR_PCPYLD, IRType::I128, rs, rt, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

// ── MMI Additions (Pack/Unpack/Shift) ───────────────────────────────────────

void IRLifter::liftPEXTLH(IRFunction& func, IRBasicBlock& bb, const GhidraInstruction& instr, const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto inst = makeBinaryOp(func, IROp::IR_PEXTLH, IRType::I128, rs, rt, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

void IRLifter::liftPEXTUH(IRFunction& func, IRBasicBlock& bb, const GhidraInstruction& instr, const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto inst = makeBinaryOp(func, IROp::IR_PEXTUH, IRType::I128, rs, rt, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

void IRLifter::liftPEXTLB(IRFunction& func, IRBasicBlock& bb, const GhidraInstruction& instr, const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto inst = makeBinaryOp(func, IROp::IR_PEXTLB, IRType::I128, rs, rt, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

void IRLifter::liftPEXTUB(IRFunction& func, IRBasicBlock& bb, const GhidraInstruction& instr, const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto inst = makeBinaryOp(func, IROp::IR_PEXTUB, IRType::I128, rs, rt, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

void IRLifter::liftPPACB(IRFunction& func, IRBasicBlock& bb, const GhidraInstruction& instr, const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto inst = makeBinaryOp(func, IROp::IR_PPACB, IRType::I128, rs, rt, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

void IRLifter::liftPPACW(IRFunction& func, IRBasicBlock& bb, const GhidraInstruction& instr, const MIPSFields& f) {
    auto rs = emitGPRRead(func, bb, f.rs, instr.addr);
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto inst = makeBinaryOp(func, IROp::IR_PPACW, IRType::I128, rs, rt, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

void IRLifter::liftPSLLW(IRFunction& func, IRBasicBlock& bb, const GhidraInstruction& instr, const MIPSFields& f) {
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto sa = emitConst32(func, bb, f.sa);
    auto inst = makeBinaryOp(func, IROp::IR_PSLLW, IRType::I128, rt, sa, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

void IRLifter::liftPSRLW(IRFunction& func, IRBasicBlock& bb, const GhidraInstruction& instr, const MIPSFields& f) {
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto sa = emitConst32(func, bb, f.sa);
    auto inst = makeBinaryOp(func, IROp::IR_PSRLW, IRType::I128, rt, sa, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

void IRLifter::liftPSRAW(IRFunction& func, IRBasicBlock& bb, const GhidraInstruction& instr, const MIPSFields& f) {
    auto rt = emitGPRRead(func, bb, f.rt, instr.addr);
    auto sa = emitConst32(func, bb, f.sa);
    auto inst = makeBinaryOp(func, IROp::IR_PSRAW, IRType::I128, rt, sa, instr.addr);
    ValueId vid = inst.result.id;
    bb.instructions.push_back(std::move(inst));
    emitGPRWrite(func, bb, f.rd, vid, instr.addr);
}

} // namespace ps2recomp
