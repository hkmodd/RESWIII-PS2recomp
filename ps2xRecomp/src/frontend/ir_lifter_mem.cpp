// ============================================================================
// ir_lifter_mem.cpp — Memory load/store instruction handlers
// Part of PS2reAIcomp — Sprint 2
// ============================================================================
#include "ps2recomp/ir_lifter.h"
namespace ps2recomp {
using namespace ir;

// ── Loads ────────────────────────────────────────────────────────────────────

void IRLifter::liftLB(IRFunction& func, uint32_t blockIdx,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I8, addr, instr.addr);
    ValueId lId = ld.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(ld));
    auto sx = makeUnaryOp(func, IROp::IR_SEXT, IRType::I32, lId, instr.addr);
    ValueId sId = sx.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(sx));
    emitGPRWrite(func, blockIdx, f.rt, sId, instr.addr);
}

void IRLifter::liftLBU(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I8, addr, instr.addr);
    ValueId lId = ld.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(ld));
    auto zx = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I32, lId, instr.addr);
    ValueId zId = zx.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(zx));
    emitGPRWrite(func, blockIdx, f.rt, zId, instr.addr);
}

void IRLifter::liftLH(IRFunction& func, uint32_t blockIdx,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I16, addr, instr.addr);
    ValueId lId = ld.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(ld));
    auto sx = makeUnaryOp(func, IROp::IR_SEXT, IRType::I32, lId, instr.addr);
    ValueId sId = sx.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(sx));
    emitGPRWrite(func, blockIdx, f.rt, sId, instr.addr);
}

void IRLifter::liftLHU(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I16, addr, instr.addr);
    ValueId lId = ld.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(ld));
    auto zx = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I32, lId, instr.addr);
    ValueId zId = zx.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(zx));
    emitGPRWrite(func, blockIdx, f.rt, zId, instr.addr);
}

void IRLifter::liftLW(IRFunction& func, uint32_t blockIdx,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I32, addr, instr.addr);
    ValueId lId = ld.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(ld));
    emitGPRWrite(func, blockIdx, f.rt, lId, instr.addr);
}

void IRLifter::liftLWU(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    // LWU: zero-extends word to 64-bit
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I32, addr, instr.addr);
    ValueId lId = ld.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(ld));
    auto zx = makeUnaryOp(func, IROp::IR_ZEXT, IRType::I64, lId, instr.addr);
    ValueId zId = zx.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(zx));
    emitGPRWrite(func, blockIdx, f.rt, zId, instr.addr);
}

void IRLifter::liftLD(IRFunction& func, uint32_t blockIdx,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I64, addr, instr.addr);
    ValueId lId = ld.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(ld));
    emitGPRWrite(func, blockIdx, f.rt, lId, instr.addr);
}

void IRLifter::liftLQ(IRFunction& func, uint32_t blockIdx,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto ld = makeLoad(func, IRType::I128, addr, instr.addr);
    ValueId lId = ld.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(ld));
    emitGPRWrite(func, blockIdx, f.rt, lId, instr.addr);
}

// LWL / LWR — unaligned load helpers (emit as opaque intrinsics)
void IRLifter::liftLWL(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    liftUnhandled(func, blockIdx, instr, f); // Intrinsic TODO
}

void IRLifter::liftLWR(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    liftUnhandled(func, blockIdx, instr, f); // Intrinsic TODO
}

// ── Stores ──────────────────────────────────────────────────────────────────

void IRLifter::liftSB(IRFunction& func, uint32_t blockIdx,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto val  = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto trunc = makeUnaryOp(func, IROp::IR_TRUNC, IRType::I8, val, instr.addr);
    ValueId tId = trunc.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(trunc));
    auto st = makeStore(IRType::I8, addr, tId, instr.addr);
    func.blocks[blockIdx].instructions.push_back(std::move(st));
}

void IRLifter::liftSH(IRFunction& func, uint32_t blockIdx,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto val  = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto trunc = makeUnaryOp(func, IROp::IR_TRUNC, IRType::I16, val, instr.addr);
    ValueId tId = trunc.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(trunc));
    auto st = makeStore(IRType::I16, addr, tId, instr.addr);
    func.blocks[blockIdx].instructions.push_back(std::move(st));
}

void IRLifter::liftSW(IRFunction& func, uint32_t blockIdx,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto val  = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto st = makeStore(IRType::I32, addr, val, instr.addr);
    func.blocks[blockIdx].instructions.push_back(std::move(st));
}

void IRLifter::liftSD(IRFunction& func, uint32_t blockIdx,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto val  = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto st = makeStore(IRType::I64, addr, val, instr.addr);
    func.blocks[blockIdx].instructions.push_back(std::move(st));
}

void IRLifter::liftSQ(IRFunction& func, uint32_t blockIdx,
                       const GhidraInstruction& instr,
                       const MIPSFields& f) {
    auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
    auto val  = emitGPRRead(func, blockIdx, f.rt, instr.addr);
    auto st = makeStore(IRType::I128, addr, val, instr.addr);
    func.blocks[blockIdx].instructions.push_back(std::move(st));
}

// SWL / SWR — unaligned store helpers
void IRLifter::liftSWL(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    liftUnhandled(func, blockIdx, instr, f);
}

void IRLifter::liftSWR(IRFunction& func, uint32_t blockIdx,
                        const GhidraInstruction& instr,
                        const MIPSFields& f) {
    liftUnhandled(func, blockIdx, instr, f);
}

} // namespace ps2recomp
