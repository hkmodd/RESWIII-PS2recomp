// ============================================================================
// ir_lifter_cop0.cpp — COP0/System, Pipeline-1, and misc instruction handlers
// Part of PS2reAIcomp — Sprint 2: Phases 2 & 3
// ============================================================================
#include "ps2recomp/ir_lifter.h"
namespace ps2recomp {
using namespace ir;

// ═══════════════════════════════════════════════════════════════════════════
// PHASE 2 — COP0 / System / Misc
// ═══════════════════════════════════════════════════════════════════════════

// MFC0 rt, rd — GPR[rt] = COP0[rd]
void IRLifter::liftMFC0(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rd = ir::makeRegRead(func, IRType::I32, IRReg::cop0(f.rd));
    rd.srcAddress = instr.addr;
    ValueId rdId = rd.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(rd));
    emitGPRWrite(func, blockIdx, f.rt, rdId, instr.addr);
}

// MTC0 rt, rd — COP0[rd] = GPR[rt]
void IRLifter::liftMTC0(IRFunction& func, uint32_t blockIdx,
                         const GhidraInstruction& instr,
                         const MIPSFields& f) {
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto w = ir::makeRegWrite(IRReg::cop0(f.rd), rt);
    w.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(w));
}

// BC0F offset — Branch if COP0 condition is false
void IRLifter::liftBC0F(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    // Read COP0 condition flag (cop0_condition in context)
    auto cond = ir::makeRegRead(func, IRType::I32, IRReg::cop0(0)); // COP0 condition flag
    cond.srcAddress = instr.addr;
    ValueId condId = cond.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(cond));

    uint32_t target = computeBranchTarget(instr.addr, f.simm16);
    auto zero = emitConst32(func, blockIdx, 0);
    emitCondBranch(func, blockIdx, IROp::IR_EQ, condId, zero, target, instr.addr);
}

// CACHE — Cache operation (NOP in recompilation, hardware-only)
void IRLifter::liftCACHE(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    emitComment(func, blockIdx, "CACHE op (NOP in recomp)", instr.addr);
    IRInst nop;
    nop.op = IROp::IR_NOP;
    nop.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(nop));
}

// TLBWI — TLB Write Indexed (NOP in recompilation, no TLB on host)
void IRLifter::liftTLBWI(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    emitComment(func, blockIdx, "TLBWI (NOP in recomp - no TLB on host)", instr.addr);
    IRInst nop;
    nop.op = IROp::IR_NOP;
    nop.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(nop));
}

// MTSAB rs, imm — SA = (GPR[rs] & 0xF) ^ (imm & 0xF)
void IRLifter::liftMTSAB(IRFunction& func, uint32_t blockIdx,
                           const GhidraInstruction& instr,
                           const MIPSFields& f) {
    auto rs  = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto imm = emitConst32(func, blockIdx, f.imm16 & 0xF);
    auto mask = emitConst32(func, blockIdx, 0xF);

    // rs & 0xF
    auto andInst = makeBinaryOp(func, IROp::IR_AND, IRType::I32, rs, mask, instr.addr);
    ValueId andId = andInst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(andInst));

    // (rs & 0xF) ^ (imm & 0xF) — then shift left by 3 to get byte count
    auto xorInst = makeBinaryOp(func, IROp::IR_XOR, IRType::I32, andId, imm, instr.addr);
    ValueId xorId = xorInst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(xorInst));

    auto three = emitConst32(func, blockIdx, 3);
    auto shlInst = makeBinaryOp(func, IROp::IR_SLL, IRType::I32, xorId, three, instr.addr);
    ValueId shlId = shlInst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(shlInst));

    // Write to SA register
    auto w = ir::makeRegWrite(IRReg::sa(), shlId);
    w.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(w));
}

// MOVEQ — Conditional move if equal (pseudo — maps to IR_SELECT)
// Ghidra may emit this as "moveq rd, rs, rt" meaning:
//   rd = (rs == rt) ? rs : rd  (or a similar select pattern)
// We treat as: if (rt == 0) rd = rs  [like MOVZ]
void IRLifter::liftMOVEQ(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto rd = emitGPRRead(func, blockIdx, f.rd, instr.addr);
    auto zero = emitConst32(func, blockIdx, 0);

    // cond = (rt == 0)
    auto cmp = makeBinaryOp(func, IROp::IR_EQ, IRType::I1, rt, zero, instr.addr);
    ValueId cmpId = cmp.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(cmp));

    // select: if cond ? rs : rd
    IRInst sel;
    sel.op = IROp::IR_SELECT;
    sel.result = func.allocTypedValue(IRType::I32);
    sel.operands = {cmpId, rs, rd};
    sel.srcAddress = instr.addr;
    ValueId selId = sel.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(sel));

    emitGPRWrite(func, blockIdx, f.rd, selId, instr.addr);
}

// ═══════════════════════════════════════════════════════════════════════════
// PHASE 3 — Pipeline-1 HI/LO operations  
// ═══════════════════════════════════════════════════════════════════════════

// MULT1 rs, rt — Pipeline 1: hi1:lo1 = (signed)rs * (signed)rt
void IRLifter::liftMULT1(IRFunction& func, uint32_t blockIdx,
                           const GhidraInstruction& instr,
                           const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);

    // LO1 = (int32_t)rs * (int32_t)rt  [lower 32 bits]
    auto mulInst = makeBinaryOp(func, IROp::IR_MUL, IRType::I32, rs, rt, instr.addr);
    ValueId mulId = mulInst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(mulInst));

    // Write to LO1
    auto wlo = ir::makeRegWrite(IRReg::lo(1), mulId);
    wlo.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(wlo));

    // HI1 = upper 32 bits — we approximate with signed multiply-high
    // For exact behavior: (int64_t)rs * (int64_t)rt >> 32
    auto rsS = makeUnaryOp(func, IROp::IR_SEXT, IRType::I64, rs, instr.addr);
    ValueId rsSid = rsS.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(rsS));

    auto rtS = makeUnaryOp(func, IROp::IR_SEXT, IRType::I64, rt, instr.addr);
    ValueId rtSid = rtS.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(rtS));

    auto mul64 = makeBinaryOp(func, IROp::IR_MUL, IRType::I64, rsSid, rtSid, instr.addr);
    ValueId mul64id = mul64.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(mul64));

    auto shift32 = emitConst32(func, blockIdx, 32);
    auto sextShift = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I64, shift32, instr.addr);
    ValueId shiftId = sextShift.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(sextShift));

    auto hi64 = makeBinaryOp(func, IROp::IR_SRA, IRType::I64, mul64id, shiftId, instr.addr);
    ValueId hi64id = hi64.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(hi64));

    auto hiTrunc = makeUnaryOp(func, IROp::IR_TRUNC, IRType::I32, hi64id, instr.addr);
    ValueId hiId = hiTrunc.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(hiTrunc));

    auto whi = ir::makeRegWrite(IRReg::hi(1), hiId);
    whi.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(whi));

    // MULT1 also writes rd if rd != 0 (R5900 quirk: rd = LO1)
    if (f.rd != 0) {
        emitGPRWrite(func, blockIdx, f.rd, mulId, instr.addr);
    }
}

// DIV1 rs, rt — Pipeline 1: lo1 = rs/rt, hi1 = rs%rt
void IRLifter::liftDIV1(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);

    auto divInst = makeBinaryOp(func, IROp::IR_DIV, IRType::I32, rs, rt, instr.addr);
    ValueId divId = divInst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(divInst));

    auto modInst = makeBinaryOp(func, IROp::IR_MOD, IRType::I32, rs, rt, instr.addr);
    ValueId modId = modInst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(modInst));

    auto wlo = ir::makeRegWrite(IRReg::lo(1), divId);
    wlo.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(wlo));

    auto whi = ir::makeRegWrite(IRReg::hi(1), modId);
    whi.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(whi));
}

// MFHI1 rd — rd = HI1
void IRLifter::liftMFHI1(IRFunction& func, uint32_t blockIdx,
                           const GhidraInstruction& instr,
                           const MIPSFields& f) {
    auto hi1 = ir::makeRegRead(func, IRType::I32, IRReg::hi(1));
    hi1.srcAddress = instr.addr;
    ValueId hiId = hi1.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(hi1));
    emitGPRWrite(func, blockIdx, f.rd, hiId, instr.addr);
}

// MFLO1 rd — rd = LO1
void IRLifter::liftMFLO1(IRFunction& func, uint32_t blockIdx,
                           const GhidraInstruction& instr,
                           const MIPSFields& f) {
    auto lo1 = ir::makeRegRead(func, IRType::I32, IRReg::lo(1));
    lo1.srcAddress = instr.addr;
    ValueId loId = lo1.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(lo1));
    emitGPRWrite(func, blockIdx, f.rd, loId, instr.addr);
}

// MADD rs, rt — hi:lo += (signed)rs * (signed)rt
void IRLifter::liftMADD(IRFunction& func, uint32_t blockIdx,
                          const GhidraInstruction& instr,
                          const MIPSFields& f) {
    auto rs = emitGPRRead(func, blockIdx, f.rs, instr.addr);
    auto rt = emitGPRRead(func, blockIdx, f.rt, instr.addr);

    // Read current LO and HI
    auto lo = ir::makeRegRead(func, IRType::I32, IRReg::lo(0));
    lo.srcAddress = instr.addr;
    ValueId loId = lo.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(lo));

    auto hi = ir::makeRegRead(func, IRType::I32, IRReg::hi(0));
    hi.srcAddress = instr.addr;
    ValueId hiId = hi.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(hi));

    // Widen all to 64-bit
    auto rsExt = makeUnaryOp(func, IROp::IR_SEXT, IRType::I64, rs, instr.addr);
    ValueId rsE = rsExt.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(rsExt));

    auto rtExt = makeUnaryOp(func, IROp::IR_SEXT, IRType::I64, rt, instr.addr);
    ValueId rtE = rtExt.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(rtExt));

    // product = rs64 * rt64
    auto mul = makeBinaryOp(func, IROp::IR_MUL, IRType::I64, rsE, rtE, instr.addr);
    ValueId mulId = mul.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(mul));

    // old_hilo = (hi << 32) | (uint32_t)lo
    auto hiExt = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I64, hiId, instr.addr);
    ValueId hiE = hiExt.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(hiExt));

    auto shift32 = emitConst32(func, blockIdx, 32);
    auto shiftExt = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I64, shift32, instr.addr);
    ValueId shiftId = shiftExt.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(shiftExt));

    auto hiShift = makeBinaryOp(func, IROp::IR_SLL, IRType::I64, hiE, shiftId, instr.addr);
    ValueId hiSh = hiShift.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(hiShift));

    auto loExt = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I64, loId, instr.addr);
    ValueId loE = loExt.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(loExt));

    auto oldHiLo = makeBinaryOp(func, IROp::IR_OR, IRType::I64, hiSh, loE, instr.addr);
    ValueId oldHL = oldHiLo.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(oldHiLo));

    // result = old_hilo + product
    auto sum = makeBinaryOp(func, IROp::IR_ADD, IRType::I64, oldHL, mulId, instr.addr);
    ValueId sumId = sum.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(sum));

    // Extract new LO and HI
    auto newLo = makeUnaryOp(func, IROp::IR_TRUNC, IRType::I32, sumId, instr.addr);
    ValueId newLoId = newLo.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(newLo));

    auto newHiShift = makeBinaryOp(func, IROp::IR_SRA, IRType::I64, sumId, shiftId, instr.addr);
    ValueId newHiS = newHiShift.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(newHiShift));

    auto newHi = makeUnaryOp(func, IROp::IR_TRUNC, IRType::I32, newHiS, instr.addr);
    ValueId newHiId = newHi.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(newHi));

    auto wlo = ir::makeRegWrite(IRReg::lo(0), newLoId);
    wlo.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(wlo));

    auto whi = ir::makeRegWrite(IRReg::hi(0), newHiId);
    whi.srcAddress = instr.addr;
    func.blocks[blockIdx].instructions.push_back(std::move(whi));

    // MADD also writes rd if rd != 0 (R5900: rd = LO)
    if (f.rd != 0) {
        emitGPRWrite(func, blockIdx, f.rd, newLoId, instr.addr);
    }
}

} // namespace ps2recomp
