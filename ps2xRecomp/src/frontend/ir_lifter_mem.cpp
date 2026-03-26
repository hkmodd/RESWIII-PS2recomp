// ============================================================================
// ir_lifter_mem.cpp — Memory load/store instruction handlers
// Part of PS2reAIcomp — Sprint 2
// ============================================================================
#include "ps2recomp/ir_lifter.h"
namespace ps2recomp {
using namespace ir;

// ── Loads ────────────────────────────────────────────────────────────────────

void IRLifter::liftLB(IRFunction& func, IRBasicBlock& bb,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I8, addr, false, instr.addr);
    ValueId lId = ld.result.id;
    bb.instructions.push_back(std::move(ld));
    auto sx = makeUnaryOp(func, IROp::IR_SEXT, IRType::I32, lId, instr.addr);
    ValueId sId = sx.result.id;
    bb.instructions.push_back(std::move(sx));
    emitGPRWrite(func, bb, f.rt, sId, instr.addr);
}

void IRLifter::liftLBU(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I8, addr, false, instr.addr);
    ValueId lId = ld.result.id;
    bb.instructions.push_back(std::move(ld));
    auto zx = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I32, lId, instr.addr);
    ValueId zId = zx.result.id;
    bb.instructions.push_back(std::move(zx));
    emitGPRWrite(func, bb, f.rt, zId, instr.addr);
}

void IRLifter::liftLH(IRFunction& func, IRBasicBlock& bb,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I16, addr, false, instr.addr);
    ValueId lId = ld.result.id;
    bb.instructions.push_back(std::move(ld));
    auto sx = makeUnaryOp(func, IROp::IR_SEXT, IRType::I32, lId, instr.addr);
    ValueId sId = sx.result.id;
    bb.instructions.push_back(std::move(sx));
    emitGPRWrite(func, bb, f.rt, sId, instr.addr);
}

void IRLifter::liftLHU(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I16, addr, false, instr.addr);
    ValueId lId = ld.result.id;
    bb.instructions.push_back(std::move(ld));
    auto zx = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I32, lId, instr.addr);
    ValueId zId = zx.result.id;
    bb.instructions.push_back(std::move(zx));
    emitGPRWrite(func, bb, f.rt, zId, instr.addr);
}

void IRLifter::liftLW(IRFunction& func, IRBasicBlock& bb,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I32, addr, false, instr.addr);
    ValueId lId = ld.result.id;
    bb.instructions.push_back(std::move(ld));
    emitGPRWrite(func, bb, f.rt, lId, instr.addr);
}

void IRLifter::liftLWU(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    // LWU: zero-extends word to 64-bit
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I32, addr, false, instr.addr);
    ValueId lId = ld.result.id;
    bb.instructions.push_back(std::move(ld));
    auto zx = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I64, lId, instr.addr);
    ValueId zId = zx.result.id;
    bb.instructions.push_back(std::move(zx));
    emitGPRWrite(func, bb, f.rt, zId, instr.addr);
}

void IRLifter::liftLD(IRFunction& func, IRBasicBlock& bb,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I64, addr, false, instr.addr);
    ValueId lId = ld.result.id;
    bb.instructions.push_back(std::move(ld));
    emitGPRWrite(func, bb, f.rt, lId, instr.addr);
}

void IRLifter::liftLQ(IRFunction& func, IRBasicBlock& bb,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I128, addr, false, instr.addr);
    ValueId lId = ld.result.id;
    bb.instructions.push_back(std::move(ld));
    emitGPRWrite(func, bb, f.rt, lId, instr.addr);
}

// LWL / LWR — unaligned load helpers (emit as opaque intrinsics)
void IRLifter::liftLWL(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    liftUnhandled(func, bb, instr, f); // Intrinsic TODO
}

void IRLifter::liftLWR(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    liftUnhandled(func, bb, instr, f); // Intrinsic TODO
}

// ── Stores ──────────────────────────────────────────────────────────────────

void IRLifter::liftSB(IRFunction& func, IRBasicBlock& bb,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto val  = emitGPRRead(func, bb, f.rt, instr.addr);
    auto trunc = makeUnaryOp(func, IROp::IR_TRUNC, IRType::I8, val, instr.addr);
    ValueId tId = trunc.result.id;
    bb.instructions.push_back(std::move(trunc));
    auto st = makeStore(IRType::I8, addr, tId, instr.addr);
    bb.instructions.push_back(std::move(st));
}

void IRLifter::liftSH(IRFunction& func, IRBasicBlock& bb,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto val  = emitGPRRead(func, bb, f.rt, instr.addr);
    auto trunc = makeUnaryOp(func, IROp::IR_TRUNC, IRType::I16, val, instr.addr);
    ValueId tId = trunc.result.id;
    bb.instructions.push_back(std::move(trunc));
    auto st = makeStore(IRType::I16, addr, tId, instr.addr);
    bb.instructions.push_back(std::move(st));
}

void IRLifter::liftSW(IRFunction& func, IRBasicBlock& bb,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto val  = emitGPRRead(func, bb, f.rt, instr.addr);
    auto st = makeStore(IRType::I32, addr, val, instr.addr);
    bb.instructions.push_back(std::move(st));
}

void IRLifter::liftSD(IRFunction& func, IRBasicBlock& bb,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto val  = emitGPRRead(func, bb, f.rt, instr.addr);
    auto st = makeStore(IRType::I64, addr, val, instr.addr);
    bb.instructions.push_back(std::move(st));
}

void IRLifter::liftSQ(IRFunction& func, IRBasicBlock& bb,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, bb, f.rs, f.simm16, instr.addr);
    auto val  = emitGPRRead(func, bb, f.rt, instr.addr);
    auto st = makeStore(IRType::I128, addr, val, instr.addr);
    bb.instructions.push_back(std::move(st));
}

// SWL / SWR — unaligned store helpers
void IRLifter::liftSWL(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    liftUnhandled(func, bb, instr, f);
}

void IRLifter::liftSWR(IRFunction& func, IRBasicBlock& bb,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    liftUnhandled(func, bb, instr, f);
}

} // namespace ps2recomp
