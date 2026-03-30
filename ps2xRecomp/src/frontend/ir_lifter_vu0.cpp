// ============================================================================
// ir_lifter_vu0.cpp — VU0 Macro Mode (COP2) instruction handlers
// Part of PS2reAIcomp — Sprint 2
// ============================================================================
// This is the SINGLE source of truth for ALL VU0 macro-mode instructions.
// 
// Architecture:
//   - liftVU0_Transfer:  qmfc2, qmtc2, cfc2, ctc2, lqc2, sqc2
//   - liftVU0_Generic:   All v{op}{bc?}{acc?}.{mask} arithmetic instructions
//                         (~170 mnemonics covered by ONE parser)
//   - liftVU0_Special:   vdiv, vsqrt, vrsqrt, vwaitq, vnop, vclipw, vmtir, vlqd, vlqi
// ============================================================================
#include "ps2recomp/ir_lifter.h"
#include <algorithm>
#include <cstring>

namespace ps2recomp {
using namespace ir;

// ════════════════════════════════════════════════════════════════════════════
// VF / VI Register Helpers
// ════════════════════════════════════════════════════════════════════════════

ValueId IRLifter::emitVFRead(IRFunction& func, uint32_t blockIdx,
                             uint8_t regIdx, uint32_t srcAddr) {
    auto inst = makeRegRead(func, IRType::V128, IRReg::vf(regIdx));
    inst.srcAddress = srcAddr;
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    return vid;
}

void IRLifter::emitVFWrite(IRFunction& func, uint32_t blockIdx,
                           uint8_t regIdx, ValueId value, uint32_t srcAddr) {
    auto inst = makeRegWrite(IRReg::vf(regIdx), value);
    inst.srcAddress = srcAddr;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
}

ValueId IRLifter::emitVIRead(IRFunction& func, uint32_t blockIdx,
                             uint8_t regIdx, uint32_t srcAddr) {
    auto inst = makeRegRead(func, IRType::I16, IRReg::vi(regIdx));
    inst.srcAddress = srcAddr;
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    return vid;
}

void IRLifter::emitVIWrite(IRFunction& func, uint32_t blockIdx,
                           uint8_t regIdx, ValueId value, uint32_t srcAddr) {
    auto inst = makeRegWrite(IRReg::vi(regIdx), value);
    inst.srcAddress = srcAddr;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
}

// ════════════════════════════════════════════════════════════════════════════
// VU0 Transfer Instructions (COP2 ←→ EE)
// ════════════════════════════════════════════════════════════════════════════

void IRLifter::liftVU0_Transfer(IRFunction& func, uint32_t blockIdx,
                                const GhidraInstruction& instr,
                                const MIPSFields& f) {
    const std::string& mn = instr.mnemonic;
    // Strip leading underscore for comparison
    std::string base = mn;
    if (!base.empty() && base[0] == '_') base = base.substr(1);

    if (base == "qmfc2") {
        // VF[rd] → GPR[rt]  (128-bit)
        auto vf = emitVFRead(func, blockIdx, f.rd, instr.addr);
        IRInst inst;
        inst.op = IROp::IR_VU_QMFC2;
        inst.result = func.allocTypedValue(IRType::I128);
        inst.operands = {vf};
        inst.srcAddress = instr.addr;
        ValueId vid = inst.result.id;
        func.blocks[blockIdx].instructions.push_back(std::move(inst));
        emitGPRWrite(func, blockIdx, f.rt, vid, instr.addr);
    }
    else if (base == "qmtc2") {
        // GPR[rt] → VF[rd]  (128-bit)
        auto gpr = emitGPRRead(func, blockIdx, f.rt, instr.addr, IRType::I128);
        IRInst inst;
        inst.op = IROp::IR_VU_QMTC2;
        inst.result = func.allocTypedValue(IRType::V128);
        inst.operands = {gpr};
        inst.srcAddress = instr.addr;
        ValueId vid = inst.result.id;
        func.blocks[blockIdx].instructions.push_back(std::move(inst));
        emitVFWrite(func, blockIdx, f.rd, vid, instr.addr);
    }
    else if (base == "cfc2") {
        // VI[rd] → GPR[rt]
        auto vi = emitVIRead(func, blockIdx, f.rd, instr.addr);
        IRInst inst;
        inst.op = IROp::IR_VU_CFC2;
        inst.result = func.allocTypedValue(IRType::I32);
        inst.operands = {vi};
        inst.srcAddress = instr.addr;
        ValueId vid = inst.result.id;
        func.blocks[blockIdx].instructions.push_back(std::move(inst));
        emitGPRWrite(func, blockIdx, f.rt, vid, instr.addr);
    }
    else if (base == "ctc2") {
        // GPR[rt] → VI[rd]
        auto gpr = emitGPRRead(func, blockIdx, f.rt, instr.addr);
        IRInst inst;
        inst.op = IROp::IR_VU_CTC2;
        inst.result = func.allocTypedValue(IRType::I16);
        inst.operands = {gpr};
        inst.srcAddress = instr.addr;
        ValueId vid = inst.result.id;
        func.blocks[blockIdx].instructions.push_back(std::move(inst));
        emitVIWrite(func, blockIdx, f.rd, vid, instr.addr);
    }
    else if (base == "lqc2") {
        // mem[base+offset] → VF[ft]
        auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
        auto loadInst = makeLoad(func, IRType::V128, addr, false, instr.addr);
        loadInst.op = IROp::IR_LOAD_VF;
        ValueId vid = loadInst.result.id;
        func.blocks[blockIdx].instructions.push_back(std::move(loadInst));
        emitVFWrite(func, blockIdx, f.ft, vid, instr.addr);
    }
    else if (base == "sqc2") {
        // VF[ft] → mem[base+offset]
        auto addr = emitAddrCalc(func, blockIdx, f.rs, f.simm16, instr.addr);
        auto vf = emitVFRead(func, blockIdx, f.ft, instr.addr);
        auto storeInst = makeStore(IRType::V128, addr, vf, instr.addr);
        storeInst.op = IROp::IR_STORE_VF;
        func.blocks[blockIdx].instructions.push_back(std::move(storeInst));
    }

    stats_.liftedInstructions++;
}

// ════════════════════════════════════════════════════════════════════════════
// VU0 Generic Mnemonic Parser
// ════════════════════════════════════════════════════════════════════════════
// Parses: v{base_op}{accumulator_suffix?}{broadcast_suffix?}.{dest_mask}
//
// Examples:
//   vmaddax.xyzw → base=madd, acc=true, broadcast=X, mask=0xF
//   vsub.xyz     → base=sub,  acc=false, broadcast=None, mask=0xE (xyz)
//   vminibcw.x   → base=mini, acc=false, broadcast=W, mask=0x8 (x)
// ════════════════════════════════════════════════════════════════════════════

// Helper: parse dest mask from dot-suffix
static uint8_t parseDestMask(const std::string& suffix) {
    uint8_t mask = 0;
    for (char c : suffix) {
        switch (c) {
            case 'x': mask |= 0x8; break;  // bit 3
            case 'y': mask |= 0x4; break;  // bit 2
            case 'z': mask |= 0x2; break;  // bit 1
            case 'w': mask |= 0x1; break;  // bit 0
            default: break;
        }
    }
    return mask ? mask : 0xF;  // default xyzw if empty
}

struct VU0ParseResult {
    IROp        op;
    VUBroadcast broadcast;
    uint8_t     destMask;
    bool        writesAcc;      // true for ADDA, MULA, MADDA, SUBA, MSUBA, OPMULA
    bool        readsAcc;       // true for MADD, MSUB, OPMSUB
    int         numSrcRegs;     // 1 for abs/move/mr32/itof/ftoi, 2 for add/sub/mul etc.
    bool        valid;
};

static VU0ParseResult parseVU0Mnemonic(const std::string& mnemonic) {
    VU0ParseResult r = {};
    r.valid = false;
    r.broadcast = VUBroadcast::None;
    r.destMask = 0xF;
    r.numSrcRegs = 2;

    std::string mn = mnemonic;
    // Strip leading underscore (Ghidra prefix for PS2 instructions)
    if (!mn.empty() && mn[0] == '_') mn = mn.substr(1);

    // Extract dest mask from dot suffix
    auto dotPos = mn.find('.');
    if (dotPos != std::string::npos) {
        r.destMask = parseDestMask(mn.substr(dotPos + 1));
        mn = mn.substr(0, dotPos);
    }

    // Strip leading 'v'
    if (mn.empty() || mn[0] != 'v') return r;
    mn = mn.substr(1);

    // Handle special Ghidra "vminibcx/vminibcw" exotic names
    if (mn.size() >= 6 && mn.substr(0, 6) == "minibc") {
        r.op = IROp::IR_VU_MINI;
        char bc = mn[6]; // 'x' or 'w'
        switch (bc) {
            case 'x': r.broadcast = VUBroadcast::X; break;
            case 'y': r.broadcast = VUBroadcast::Y; break;
            case 'z': r.broadcast = VUBroadcast::Z; break;
            case 'w': r.broadcast = VUBroadcast::W; break;
            default:  r.broadcast = VUBroadcast::X; break;
        }
        r.valid = true;
        return r;
    }

    // Try to match base operation (longest match first)
    struct OpEntry {
        const char* prefix;
        IROp        op;
        bool        writesAcc;
        bool        readsAcc;
        int         numSrc;
    };

    // Order: longest prefix first to avoid partial matches
    static const OpEntry ops[] = {
        // Accumulator-writing operations (must come before non-acc versions)
        {"opmula",  IROp::IR_VU_OPMULA,  true,  false, 2},
        {"opmsub",  IROp::IR_VU_OPMSUB,  false, true,  2},
        {"madda",   IROp::IR_VU_MADDA,   true,  true,  2},
        {"msuba",   IROp::IR_VU_MSUBA,   true,  true,  2},
        {"adda",    IROp::IR_VU_ADDA,    true,  false, 2},
        {"suba",    IROp::IR_VU_SUBA,    true,  false, 2},
        {"mula",    IROp::IR_VU_MULA,    true,  false, 2},
        // Regular 2-operand
        {"madd",    IROp::IR_VU_MADD,    false, true,  2},
        {"msub",    IROp::IR_VU_MSUB,    false, true,  2},
        {"max",     IROp::IR_VU_MAX,     false, false, 2},
        {"mini",    IROp::IR_VU_MINI,    false, false, 2},
        {"add",     IROp::IR_VU_ADD,     false, false, 2},
        {"sub",     IROp::IR_VU_SUB,     false, false, 2},
        {"mul",     IROp::IR_VU_MUL,     false, false, 2},
        // Unary operations
        {"abs",     IROp::IR_VU_ABS,     false, false, 1},
        {"move",    IROp::IR_VU_MOVE,    false, false, 1},
        {"mr32",    IROp::IR_VU_MR32,    false, false, 1},
        {"itof15",  IROp::IR_VU_ITOF15,  false, false, 1},
        {"itof12",  IROp::IR_VU_ITOF12,  false, false, 1},
        {"itof4",   IROp::IR_VU_ITOF4,   false, false, 1},
        {"itof0",   IROp::IR_VU_ITOF0,   false, false, 1},
        {"ftoi15",  IROp::IR_VU_FTOI15,  false, false, 1},
        {"ftoi12",  IROp::IR_VU_FTOI12,  false, false, 1},
        {"ftoi4",   IROp::IR_VU_FTOI4,   false, false, 1},
        {"ftoi0",   IROp::IR_VU_FTOI0,   false, false, 1},
    };

    for (const auto& e : ops) {
        size_t plen = std::strlen(e.prefix);
        if (mn.size() >= plen && mn.substr(0, plen) == e.prefix) {
            r.op = e.op;
            r.writesAcc = e.writesAcc;
            r.readsAcc = e.readsAcc;
            r.numSrcRegs = e.numSrc;

            // Remaining after base op is the broadcast suffix
            std::string tail = mn.substr(plen);
            if (!tail.empty()) {
                if (tail == "q") {
                    r.broadcast = VUBroadcast::Q;
                } else if (tail == "i") {
                    r.broadcast = VUBroadcast::I;
                } else if (tail.size() == 1) {
                    switch (tail[0]) {
                        case 'x': r.broadcast = VUBroadcast::X; break;
                        case 'y': r.broadcast = VUBroadcast::Y; break;
                        case 'z': r.broadcast = VUBroadcast::Z; break;
                        case 'w': r.broadcast = VUBroadcast::W; break;
                        default: break;
                    }
                }
            }

            r.valid = true;
            return r;
        }
    }

    return r;
}

void IRLifter::liftVU0_Generic(IRFunction& func, uint32_t blockIdx,
                               const GhidraInstruction& instr,
                               const MIPSFields& f) {
    auto parsed = parseVU0Mnemonic(instr.mnemonic);
    if (!parsed.valid) {
        liftUnhandled(func, blockIdx, instr, f);
        return;
    }

    // COP2 register encoding:
    //   fd = bits[10:6] (sa field) → destination VF
    //   fs = bits[15:11] (rd field) → source VF
    //   ft = bits[20:16] (rt field) → source VF
    uint8_t vfd = f.sa;   // destination
    uint8_t vfs = f.rd;   // source 1
    uint8_t vft = f.rt;   // source 2

    IRInst inst;
    inst.op = parsed.op;
    inst.srcAddress = instr.addr;
    inst.srcRaw = instr.rawBytes;
    inst.vuDestMask = parsed.destMask;
    inst.vuBroadcast = parsed.broadcast;

    // Read accumulator if needed (MADD, MSUB, OPMSUB)
    ValueId accId = INVALID_VALUE_ID;
    if (parsed.readsAcc) {
        auto accRead = makeRegRead(func, IRType::V128, IRReg::vuAcc());
        accRead.srcAddress = instr.addr;
        accId = accRead.result.id;
        func.blocks[blockIdx].instructions.push_back(std::move(accRead));
    }

    // Read source registers
    ValueId fsId = emitVFRead(func, blockIdx, vfs, instr.addr);
    ValueId ftId = INVALID_VALUE_ID;
    if (parsed.numSrcRegs >= 2) {
        // For Q-broadcast, read Q register instead of VF[ft]
        if (parsed.broadcast == VUBroadcast::Q) {
            auto qRead = makeRegRead(func, IRType::F32, IRReg::vuQ());
            qRead.srcAddress = instr.addr;
            ftId = qRead.result.id;
            func.blocks[blockIdx].instructions.push_back(std::move(qRead));
        } else {
            ftId = emitVFRead(func, blockIdx, vft, instr.addr);
        }
    }

    // Build operand list
    if (parsed.readsAcc) {
        inst.operands = {accId, fsId};
        if (ftId != INVALID_VALUE_ID) inst.operands.push_back(ftId);
    } else if (parsed.numSrcRegs == 1) {
        inst.operands = {fsId};
    } else {
        inst.operands = {fsId, ftId};
    }

    // Set result type
    inst.result = func.allocTypedValue(IRType::V128);
    ValueId resultId = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));

    // Write result to ACC or VF[fd]
    if (parsed.writesAcc) {
        auto w = makeRegWrite(IRReg::vuAcc(), resultId);
        w.srcAddress = instr.addr;
        func.blocks[blockIdx].instructions.push_back(std::move(w));
    } else {
        emitVFWrite(func, blockIdx, vfd, resultId, instr.addr);
    }

    stats_.liftedInstructions++;
}

// ════════════════════════════════════════════════════════════════════════════
// VU0 Special Instructions
// ════════════════════════════════════════════════════════════════════════════

void IRLifter::liftVU0_Special(IRFunction& func, uint32_t blockIdx,
                               const GhidraInstruction& instr,
                               const MIPSFields& f) {
    std::string mn = instr.mnemonic;
    if (!mn.empty() && mn[0] == '_') mn = mn.substr(1);

    // Strip dot suffix for special ops
    auto dotPos = mn.find('.');
    std::string base = (dotPos != std::string::npos) ? mn.substr(0, dotPos) : mn;

    uint8_t vfs = f.rd;
    uint8_t vft = f.rt;

    if (base == "vdiv") {
        auto fs = emitVFRead(func, blockIdx, vfs, instr.addr);
        auto ft = emitVFRead(func, blockIdx, vft, instr.addr);
        IRInst inst;
        inst.op = IROp::IR_VU_DIV;
        inst.result = func.allocTypedValue(IRType::F32);
        inst.operands = {fs, ft};
        inst.vuFSF = (f.sa >> 3) & 0x3;  // FSF field
        inst.vuFTF = (f.sa >> 1) & 0x3;  // FTF field
        inst.srcAddress = instr.addr;
        ValueId vid = inst.result.id;
        func.blocks[blockIdx].instructions.push_back(std::move(inst));
        // Write to Q register
        auto w = makeRegWrite(IRReg::vuQ(), vid);
        w.srcAddress = instr.addr;
        func.blocks[blockIdx].instructions.push_back(std::move(w));
    }
    else if (base == "vsqrt") {
        auto ft = emitVFRead(func, blockIdx, vft, instr.addr);
        IRInst inst;
        inst.op = IROp::IR_VU_SQRT;
        inst.result = func.allocTypedValue(IRType::F32);
        inst.operands = {ft};
        inst.vuFTF = (f.sa >> 1) & 0x3;
        inst.srcAddress = instr.addr;
        ValueId vid = inst.result.id;
        func.blocks[blockIdx].instructions.push_back(std::move(inst));
        auto w = makeRegWrite(IRReg::vuQ(), vid);
        w.srcAddress = instr.addr;
        func.blocks[blockIdx].instructions.push_back(std::move(w));
    }
    else if (base == "vrsqrt") {
        auto fs = emitVFRead(func, blockIdx, vfs, instr.addr);
        auto ft = emitVFRead(func, blockIdx, vft, instr.addr);
        IRInst inst;
        inst.op = IROp::IR_VU_RSQRT;
        inst.result = func.allocTypedValue(IRType::F32);
        inst.operands = {fs, ft};
        inst.vuFSF = (f.sa >> 3) & 0x3;
        inst.vuFTF = (f.sa >> 1) & 0x3;
        inst.srcAddress = instr.addr;
        ValueId vid = inst.result.id;
        func.blocks[blockIdx].instructions.push_back(std::move(inst));
        auto w = makeRegWrite(IRReg::vuQ(), vid);
        w.srcAddress = instr.addr;
        func.blocks[blockIdx].instructions.push_back(std::move(w));
    }
    else if (base == "vwaitq") {
        IRInst inst;
        inst.op = IROp::IR_VU_WAITQ;
        inst.srcAddress = instr.addr;
        func.blocks[blockIdx].instructions.push_back(std::move(inst));
    }
    else if (base == "vnop") {
        IRInst inst;
        inst.op = IROp::IR_NOP;
        inst.srcAddress = instr.addr;
        inst.comment = "VU0 NOP";
        func.blocks[blockIdx].instructions.push_back(std::move(inst));
    }
    else if (base == "vclipw") {
        uint8_t destMask = 0xE; // xyz
        auto fs = emitVFRead(func, blockIdx, vfs, instr.addr);
        auto ft = emitVFRead(func, blockIdx, vft, instr.addr);
        IRInst inst;
        inst.op = IROp::IR_VU_CLIP;
        inst.result = func.allocTypedValue(IRType::I32);
        inst.operands = {fs, ft};
        inst.vuDestMask = destMask;
        inst.srcAddress = instr.addr;
        func.blocks[blockIdx].instructions.push_back(std::move(inst));
    }
    else if (base == "vmtir") {
        auto fs = emitVFRead(func, blockIdx, vfs, instr.addr);
        IRInst inst;
        inst.op = IROp::IR_VU_MTIR;
        inst.result = func.allocTypedValue(IRType::I16);
        inst.operands = {fs};
        inst.vuFSF = (f.sa >> 3) & 0x3;
        inst.srcAddress = instr.addr;
        ValueId vid = inst.result.id;
        func.blocks[blockIdx].instructions.push_back(std::move(inst));
        emitVIWrite(func, blockIdx, f.rt, vid, instr.addr);
    }
    else if (base == "vlqd") {
        // Pre-decrement: --VI[is], then load VF[ft] from VU data mem
        auto is = emitVIRead(func, blockIdx, vfs, instr.addr);
        IRInst inst;
        inst.op = IROp::IR_VU_LQD;
        inst.result = func.allocTypedValue(IRType::V128);
        inst.operands = {is};
        inst.vuDestMask = parseDestMask(
            (dotPos != std::string::npos) ? mn.substr(dotPos + 1) : "xyzw");
        inst.srcAddress = instr.addr;
        ValueId vid = inst.result.id;
        func.blocks[blockIdx].instructions.push_back(std::move(inst));
        emitVFWrite(func, blockIdx, vft, vid, instr.addr);
    }
    else if (base == "vlqi") {
        // Post-increment: load VF[ft] from VU data mem, then VI[is]++
        auto is = emitVIRead(func, blockIdx, vfs, instr.addr);
        IRInst inst;
        inst.op = IROp::IR_VU_LQI;
        inst.result = func.allocTypedValue(IRType::V128);
        inst.operands = {is};
        inst.vuDestMask = parseDestMask(
            (dotPos != std::string::npos) ? mn.substr(dotPos + 1) : "xyzw");
        inst.srcAddress = instr.addr;
        ValueId vid = inst.result.id;
        func.blocks[blockIdx].instructions.push_back(std::move(inst));
        emitVFWrite(func, blockIdx, vft, vid, instr.addr);
    }

    stats_.liftedInstructions++;
}

} // namespace ps2recomp
