// ============================================================================
// ir_lifter_alu.cpp — Integer ALU instruction handlers
// Part of PS2reAIcomp — Sprint 2
// ============================================================================
#include "ps2recomp/ir_lifter.h"
namespace ps2recomp {
using namespace ir;

// ── Three-register ALU (R-type: rd = rs OP rt) ─────────────────────────────

#define LIFT_R_ARITH(Name, Op, Type)                                       \
void IRLifter::lift##Name(IRFunction& func, uint32_t blockIdx,              \
                          const GhidraInstruction& instr,                  \
                          const MIPSFields& f) {                           \
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);                     \
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);                     \
    auto inst = makeBinaryOp(func, IROp::Op, IRType::Type, rs, rt,         \
                             instr.addr);                                  \
    ValueId vid = inst.result.id;                                          \
    func.blocks[blockIdx].instructions.push_back(std::move(inst));                            \
    emitGPRWrite(func, blockIdx, f.rd, vid, instr.addr);                         \
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

void IRLifter::liftNOR(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto rs  = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt  = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto orv = makeBinaryOp(func, IROp::IR_OR, IRType::I32, rs, rt, instr.addr);
    ValueId orId = orv.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(orv));
    auto notv = makeUnaryOp(func, IROp::IR_NOT, IRType::I32, orId, instr.addr);
    ValueId nId = notv.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(notv));
    emitGPRWrite(func, blockIdx, f.rd, nId, instr.addr);
}

// ── Immediate ALU (I-type: rt = rs OP imm) ─────────────────────────────────

#define LIFT_I_ARITH(Name, Op, Type, signedImm)                            \
void IRLifter::lift##Name(IRFunction& func, uint32_t blockIdx,              \
                          const GhidraInstruction& instr,                  \
                          const MIPSFields& f) {                           \
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);                     \
    ValueId imm;                                                           \
    if constexpr (signedImm)                                               \
        imm = emitConst32(func, blockIdx, static_cast<int32_t>(f.simm16));       \
    else                                                                   \
        imm = emitConstU32(func, blockIdx, static_cast<uint32_t>(f.imm16));      \
    auto inst = makeBinaryOp(func, IROp::Op, IRType::Type, rs, imm,        \
                             instr.addr);                                  \
    ValueId vid = inst.result.id;                                          \
    func.blocks[blockIdx].instructions.push_back(std::move(inst));                            \
    emitGPRWrite(func, blockIdx, f.rt, vid, instr.addr);                         \
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

void IRLifter::liftLUI(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    uint32_t val = static_cast<uint32_t>(f.imm16) << 16;
    auto cv = emitConstU32(func, blockIdx, val);
    emitGPRWrite(func, blockIdx, f.rt, cv, instr.addr);
}

// ── Shifts (constant shamt) ─────────────────────────────────────────────────

#define LIFT_SHIFT_CONST(Name, Op)                                         \
void IRLifter::lift##Name(IRFunction& func, uint32_t blockIdx,              \
                          const GhidraInstruction& instr,                  \
                          const MIPSFields& f) {                           \
    auto rt  = emitGPRRead(func, blockIdx, f.rt, instr.addr);                    \
    auto sa  = emitConst32(func, blockIdx, f.sa);                                \
    auto inst = makeBinaryOp(func, IROp::Op, IRType::I32, rt, sa,          \
                             instr.addr);                                  \
    ValueId vid = inst.result.id;                                          \
    func.blocks[blockIdx].instructions.push_back(std::move(inst));                            \
    emitGPRWrite(func, blockIdx, f.rd, vid, instr.addr);                         \
}

LIFT_SHIFT_CONST(SLL, IR_SHL)
LIFT_SHIFT_CONST(SRL, IR_LSHR)
LIFT_SHIFT_CONST(SRA, IR_ASHR)
#undef LIFT_SHIFT_CONST

// ── Shifts (variable: rd = rt << rs) ────────────────────────────────────────

#define LIFT_SHIFT_VAR(Name, Op)                                           \
void IRLifter::lift##Name(IRFunction& func, uint32_t blockIdx,              \
                          const GhidraInstruction& instr,                  \
                          const MIPSFields& f) {                           \
    auto rt  = emitGPRRead(func, blockIdx, f.rt, instr.addr);                    \
    auto rs  = emitGPRRead(func, blockIdx, f.rs, instr.addr);                    \
    auto inst = makeBinaryOp(func, IROp::Op, IRType::I32, rt, rs,          \
                             instr.addr);                                  \
    ValueId vid = inst.result.id;                                          \
    func.blocks[blockIdx].instructions.push_back(std::move(inst));                            \
    emitGPRWrite(func, blockIdx, f.rd, vid, instr.addr);                         \
}

LIFT_SHIFT_VAR(SLLV, IR_SHL)
LIFT_SHIFT_VAR(SRLV, IR_LSHR)
LIFT_SHIFT_VAR(SRAV, IR_ASHR)
#undef LIFT_SHIFT_VAR

// ── Set on less than ────────────────────────────────────────────────────────

void IRLifter::liftSLT(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto cmp = makeBinaryOp(func, IROp::IR_SLT, IRType::I1, rs, rt, instr.addr);
    ValueId cId = cmp.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(cmp));
    auto zext = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I32, cId, instr.addr);
    ValueId zId = zext.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(zext));
    emitGPRWrite(func, blockIdx, f.rd, zId, instr.addr);
}

void IRLifter::liftSLTU(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto cmp = makeBinaryOp(func, IROp::IR_SLTU, IRType::I1, rs, rt, instr.addr);
    ValueId cId = cmp.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(cmp));
    auto zext = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I32, cId, instr.addr);
    ValueId zId = zext.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(zext));
    emitGPRWrite(func, blockIdx, f.rd, zId, instr.addr);
}

void IRLifter::liftSLTI(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs  = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto imm = emitConst32(func, blockIdx, static_cast<int32_t>(f.simm16));
    auto cmp = makeBinaryOp(func, IROp::IR_SLT, IRType::I1, rs, imm, instr.addr);
    ValueId cId = cmp.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(cmp));
    auto zext = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I32, cId, instr.addr);
    ValueId zId = zext.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(zext));
    emitGPRWrite(func, blockIdx, f.rt, zId, instr.addr);
}

void IRLifter::liftSLTIU(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto rs  = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto imm = emitConst32(func, blockIdx, static_cast<int32_t>(f.simm16));
    auto cmp = makeBinaryOp(func, IROp::IR_SLTU, IRType::I1, rs, imm, instr.addr);
    ValueId cId = cmp.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(cmp));
    auto zext = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I32, cId, instr.addr);
    ValueId zId = zext.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(zext));
    emitGPRWrite(func, blockIdx, f.rt, zId, instr.addr);
}

// ── Multiply / Divide ───────────────────────────────────────────────────────

void IRLifter::liftMULT(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto mul = makeBinaryOp(func, IROp::IR_MUL, IRType::I64, rs, rt, instr.addr);
    ValueId mId = mul.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(mul));
    // LO = low32, HI = high32  (approximate: store full result in LO)
    auto wLo = makeRegWrite(IRReg::lo(), mId);
    wLo.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(wLo));
    // Shift right 32 for HI
    auto c32 = emitConst32(func, blockIdx, 32);
    auto hi = makeBinaryOp(func, IROp::IR_LSHR, IRType::I64, mId, c32, instr.addr);
    ValueId hId = hi.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(hi));
    auto wHi = makeRegWrite(IRReg::hi(), hId);
    wHi.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(wHi));
}

void IRLifter::liftMULTU(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    // Same structure as MULT, semantically unsigned
    liftMULT(func, blockIdx, instr, f);
}

void IRLifter::liftDIV(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    // LO = quotient
    auto dv = makeBinaryOp(func, IROp::IR_DIV, IRType::I32, rs, rt, instr.addr);
    ValueId qId = dv.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(dv));
    auto wLo = makeRegWrite(IRReg::lo(), qId);
    wLo.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(wLo));
    // HI = remainder
    auto rm = makeBinaryOp(func, IROp::IR_MOD, IRType::I32, rs, rt, instr.addr);
    ValueId rId = rm.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(rm));
    auto wHi = makeRegWrite(IRReg::hi(), rId);
    wHi.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(wHi));
}

void IRLifter::liftDIVU(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto dv = makeBinaryOp(func, IROp::IR_DIVU, IRType::I32, rs, rt, instr.addr);
    ValueId qId = dv.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(dv));
    auto wLo = makeRegWrite(IRReg::lo(), qId);
    wLo.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(wLo));
    // HI = remainder (unsigned)
    auto rm = makeBinaryOp(func, IROp::IR_MODU, IRType::I32, rs, rt, instr.addr);
    ValueId rId = rm.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(rm));
    auto wHi = makeRegWrite(IRReg::hi(), rId);
    wHi.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(wHi));
}

// ── HI/LO move ──────────────────────────────────────────────────────────────

void IRLifter::liftMFHI(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto inst = makeRegRead(func, IRType::I32, IRReg::hi());
    inst.srcAddress = instr.addr;
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    emitGPRWrite(func, blockIdx, f.rd, vid, instr.addr);
}

void IRLifter::liftMFLO(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto inst = makeRegRead(func, IRType::I32, IRReg::lo());
    inst.srcAddress = instr.addr;
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    emitGPRWrite(func, blockIdx, f.rd, vid, instr.addr);
}

void IRLifter::liftMTHI(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto w = makeRegWrite(IRReg::hi(), rs);
    w.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(w));
}

void IRLifter::liftMTLO(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto w = makeRegWrite(IRReg::lo(), rs);
    w.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(w));
}

// ── Conditional moves ───────────────────────────────────────────────────────

void IRLifter::liftMOVZ(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto zero = emitConst32(func, blockIdx, 0);
    auto cmp = makeBinaryOp(func, IROp::IR_EQ, IRType::I1, rt, zero, instr.addr);
    ValueId cId = cmp.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(cmp));
    auto rd_old = emitGPRRead(func, blockIdx, f.rd, instr.addr);
    auto sel = makeBinaryOp(func, IROp::IR_SELECT, IRType::I32, cId, rs, instr.addr);
    // SELECT needs 3 operands: cond, trueVal, falseVal
    sel.operands.push_back(rd_old);
    ValueId sId = sel.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(sel));
    emitGPRWrite(func, blockIdx, f.rd, sId, instr.addr);
}

void IRLifter::liftMOVN(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto zero = emitConst32(func, blockIdx, 0);
    auto cmp = makeBinaryOp(func, IROp::IR_NE, IRType::I1, rt, zero, instr.addr);
    ValueId cId = cmp.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(cmp));
    auto rd_old = emitGPRRead(func, blockIdx, f.rd, instr.addr);
    auto sel = makeBinaryOp(func, IROp::IR_SELECT, IRType::I32, cId, rs, instr.addr);
    sel.operands.push_back(rd_old);
    ValueId sId = sel.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(sel));
    emitGPRWrite(func, blockIdx, f.rd, sId, instr.addr);
}

// ── Pseudo / Synthetic instructions ─────────────────────────────────────────

void IRLifter::liftMOVE(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    if (f.opcode == 0 && f.func == 0x2D) liftDADDU(func, blockIdx, instr, f);
    else if (f.opcode == 0 && f.func == 0x21) liftADDU(func, blockIdx, instr, f);
    else liftOR(func, blockIdx, instr, f);
}

void IRLifter::liftCLEAR(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    if (f.opcode == 0x11) { // COP1
        if (f.rs == 4) { // mtc1
            liftMTC1(func, blockIdx, instr, f);
            return;
        } else if (f.rs == 2 || f.rs == 6) { // ctc1
            emitComment(func, blockIdx, "[UNHANDLED] clear fcsr (ctc1 ignored)", instr.addr);
            return;
        }
    }
    
    // Otherwise treating it as integer clear (addu, or, daddu, por)
    uint8_t dest = f.rd;
    if (f.opcode != 0 && f.opcode != 0x1C) {
        dest = f.rt; // Assume I-Type fallback like addiu
    }
    
    emitGPRWrite(func, blockIdx, dest, emitConst32(func, blockIdx, 0), instr.addr);
}

void IRLifter::liftLI(IRFunction& func, uint32_t blockIdx,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    if (f.opcode == 0x19) liftDADDIU(func, blockIdx, instr, f);
    else if (f.opcode == 0x0D) liftORI(func, blockIdx, instr, f);
    else liftADDIU(func, blockIdx, instr, f);
}

void IRLifter::liftNEGU(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    // subu rd, zero, rt
    liftSUBU(func, blockIdx, instr, f);
}

} // namespace ps2recomp
