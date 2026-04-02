// ============================================================================
// cpp_emitter.cpp — Backend emitter for translating IR to C++ code
// Part of PS2reAIcomp
// ============================================================================

#include "cpp_emitter.h"
#include <bit>
#include <cmath>
#include <iostream>

namespace ps2recomp {

using namespace ir;

CppEmitter::CppEmitter() = default;
CppEmitter::~CppEmitter() = default;

std::string CppEmitter::emitFunction(const IRFunction& func) {
    std::ostringstream out;

    out << "// Emitted C++ backend for " << func.name << "\n";
    out << "extern \"C\" void ps2_" << func.name << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {\n";

    // ── Phase 3: Re-entry switch ──────────────────────────────────────────
    // For every IR_CALL in this function, compute continuation_addr = call.srcAddress + 8
    // and find the basic block whose mipsStartAddr matches. Emit a switch(ctx->pc)
    // so the dispatcher can re-enter at the correct continuation point.
    struct ReentryCase {
        uint32_t continuationAddr; // JAL address + 8
        uint32_t bbIndex;          // target basic block index
    };
    std::vector<ReentryCase> reentryCases;

    // Build address→block index map
    std::unordered_map<uint32_t, uint32_t> addrToBBIndex;
    for (const auto& bb : func.blocks) {
        if (bb.mipsStartAddr != 0) {
            addrToBBIndex[bb.mipsStartAddr] = bb.index;
        }
    }

    // Scan all blocks to build reentry switch. Add all basic blocks with a known
    // MIPS address as valid re-entry points. This allows resuming not just after 
    // CALLs, but also indirect jumps/switches that were unresolved statically and 
    // fell back to the dispatcher.
    for (const auto& bb : func.blocks) {
        if (bb.mipsStartAddr != 0) {
            // Deduplicate: don't emit the same case twice
            bool duplicate = false;
            for (const auto& rc : reentryCases) {
                if (rc.continuationAddr == bb.mipsStartAddr) { duplicate = true; break; }
            }
            if (!duplicate) {
                reentryCases.push_back({bb.mipsStartAddr, bb.index});
            }
        }
    }

    // Emit the re-entry switch if there are any call continuations
    if (!reentryCases.empty()) {
        out << "    // Re-entry dispatch (continuation after calls)\n";
        out << "    switch (ctx->pc) {\n";
        for (const auto& rc : reentryCases) {
            out << "        case 0x" << std::hex << rc.continuationAddr << std::dec
                << ": goto bb_" << rc.bbIndex << ";\n";
        }
        out << "        default: break; // normal entry at bb_0\n";
        out << "    }\n\n";
    }

    for (const auto& bb : func.blocks) {
        emitBasicBlockHeader(out, bb);
        
        for (const auto& inst : bb.instructions) {
            emitInstruction(out, inst);
        }
        out << "}\n";
    }

    out << "}\n";
    return out.str();
}

void CppEmitter::emitBasicBlockHeader(std::ostringstream& out, const IRBasicBlock& bb) {
    out << "bb_" << bb.index << ": {\n";
    if (!bb.label.empty()) {
        out << "    // " << bb.label << "\n";
    }
}

void CppEmitter::emitInstruction(std::ostringstream& out, const IRInst& inst) {
    out << "    ";
    
    if (inst.hasResult()) {
        valueTypes_[inst.result.id] = inst.result.type;
        out << "[[maybe_unused]] " << getCType(inst.result.type) << " " << getValueName(inst.result.id) << " = ";
    }

    switch (inst.op) {
        case IROp::IR_COMMENT:
            out << "// " << inst.comment << "\n";
            return; // We don't want the generic end-of-instruction semicolon for comments
        case IROp::IR_REG_READ:
            if (inst.reg.kind == IRRegKind::GPR) {
                if (inst.result.type == IRType::I128) {
                    out << "GET_GPR_VEC(ctx, " << (int)inst.reg.index << ")";
                } else if (inst.result.type == IRType::I64) {
                    out << "GPR_U64(ctx, " << (int)inst.reg.index << ")";
                } else {
                    out << "GPR_U32(ctx, " << (int)inst.reg.index << ")";
                }
            } else {
                // Non-GPR register read: bridge type domains
                auto regNative = getRegNativeType(inst.reg);
                auto resultType = inst.result.type;
                if (regNative == resultType) {
                    // Types match — direct read
                    out << "ctx->" << getRegName(inst.reg);
                } else if (resultType == IRType::I128 && (regNative == IRType::I64)) {
                    // uint64_t → __m128i (zero-extended)
                    out << "_mm_cvtsi64_si128((int64_t)(ctx->" << getRegName(inst.reg) << "))";
                } else if (resultType == IRType::I64 && (regNative == IRType::I128)) {
                    // __m128i → uint64_t (extract low 64)
                    out << "(uint64_t)_mm_cvtsi128_si64(ctx->" << getRegName(inst.reg) << ")";
                } else if (resultType == IRType::I128 && regNative == IRType::V128) {
                    // __m128 → __m128i
                    out << "_mm_castps_si128(ctx->" << getRegName(inst.reg) << ")";
                } else if (resultType == IRType::V128 && regNative == IRType::I128) {
                    // __m128i → __m128
                    out << "_mm_castsi128_ps(ctx->" << getRegName(inst.reg) << ")";
                } else if (resultType == IRType::I128 && regNative == IRType::I32) {
                    // uint32_t → __m128i (zero-extended)
                    out << "_mm_cvtsi32_si128((int32_t)(ctx->" << getRegName(inst.reg) << "))";
                } else if (resultType == IRType::I32 && (regNative == IRType::I64)) {
                    // uint64_t → uint32_t (truncate)
                    out << "(uint32_t)(ctx->" << getRegName(inst.reg) << ")";
                } else {
                    // Fallback — direct (may warn but won't crash)
                    out << "ctx->" << getRegName(inst.reg);
                }
            }
            break;
        case IROp::IR_REG_WRITE:
            if (inst.reg.kind == IRRegKind::GPR) {
                ir::IRType opType = ir::IRType::I32;
                if (!inst.operands.empty()) {
                    auto it = valueTypes_.find(inst.operands[0]);
                    if (it != valueTypes_.end()) opType = it->second;
                }
                if (opType == ir::IRType::I128) {
                    out << "SET_GPR_VEC(ctx, " << (int)inst.reg.index << ", " << getValueName(inst.operands[0]) << ")";
                } else if (opType == ir::IRType::I64) {
                    out << "SET_GPR_U64(ctx, " << (int)inst.reg.index << ", " << getValueName(inst.operands[0]) << ")";
                } else {
                    out << "SET_GPR_U32(ctx, " << (int)inst.reg.index << ", " << getValueName(inst.operands[0]) << ")";
                }
            } else {
                // Non-GPR register write: bridge type domains
                auto regNative = getRegNativeType(inst.reg);
                ir::IRType opType = ir::IRType::I32;
                if (!inst.operands.empty()) {
                    auto it = valueTypes_.find(inst.operands[0]);
                    if (it != valueTypes_.end()) opType = it->second;
                }
                if (regNative == opType) {
                    // Types match — direct write
                    out << "ctx->" << getRegName(inst.reg) << " = " << getValueName(inst.operands[0]);
                } else if (regNative == IRType::I64 && opType == IRType::I128) {
                    // __m128i → uint64_t (extract low 64)
                    out << "ctx->" << getRegName(inst.reg) << " = (uint64_t)_mm_cvtsi128_si64(" << getValueName(inst.operands[0]) << ")";
                } else if (regNative == IRType::I128 && opType == IRType::I64) {
                    // uint64_t → __m128i (zero-extend)
                    out << "ctx->" << getRegName(inst.reg) << " = _mm_cvtsi64_si128((int64_t)" << getValueName(inst.operands[0]) << ")";
                } else if (regNative == IRType::V128 && opType == IRType::I128) {
                    // __m128i → __m128
                    out << "ctx->" << getRegName(inst.reg) << " = _mm_castsi128_ps(" << getValueName(inst.operands[0]) << ")";
                } else if (regNative == IRType::I128 && opType == IRType::V128) {
                    // __m128 → __m128i
                    out << "ctx->" << getRegName(inst.reg) << " = _mm_castps_si128(" << getValueName(inst.operands[0]) << ")";
                } else if (regNative == IRType::I32 && opType == IRType::I128) {
                    // __m128i → uint32_t (extract low 32)
                    out << "ctx->" << getRegName(inst.reg) << " = (uint32_t)_mm_cvtsi128_si32(" << getValueName(inst.operands[0]) << ")";
                } else if (regNative == IRType::I32 && opType == IRType::I64) {
                    // uint64_t → uint32_t (truncate)
                    out << "ctx->" << getRegName(inst.reg) << " = (uint32_t)(" << getValueName(inst.operands[0]) << ")";
                } else {
                    // Fallback — direct assign
                    out << "ctx->" << getRegName(inst.reg) << " = " << getValueName(inst.operands[0]);
                }
            }
            break;
        case IROp::IR_ADD:
            out << getValueName(inst.operands[0]) << " + " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_SUB:
            out << getValueName(inst.operands[0]) << " - " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_AND:
            out << getValueName(inst.operands[0]) << " & " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_OR:
            out << getValueName(inst.operands[0]) << " | " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_XOR:
            out << getValueName(inst.operands[0]) << " ^ " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_SLL:
            out << getValueName(inst.operands[0]) << " << " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_SRL:
            out << getValueName(inst.operands[0]) << " >> " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_SRA:
            // Cast to signed to ensure arithmetic shift
            out << "((int32_t)" << getValueName(inst.operands[0]) << ") >> " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_SHL:
            out << getValueName(inst.operands[0]) << " << " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_LSHR:
            out << getValueName(inst.operands[0]) << " >> " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_ASHR:
            out << "((int32_t)" << getValueName(inst.operands[0]) << ") >> " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_MUL:
            out << getValueName(inst.operands[0]) << " * " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_DIV:
            out << getValueName(inst.operands[0]) << " / " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_MOD:
            out << getValueName(inst.operands[0]) << " % " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_DIVU:
            out << getValueName(inst.operands[0]) << " / " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_MODU:
            out << getValueName(inst.operands[0]) << " % " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_LUI:
            out << "((" << getValueName(inst.operands[0]) << ") << 16)";
            break;
        case IROp::IR_BITCAST: {
            std::string t = "int32_t";
            if (inst.result.type == IRType::F32) t = "float";
            else if (inst.result.type == IRType::I64) t = "int64_t";
            out << "std::bit_cast<" << t << ">(" << getValueName(inst.operands[0]) << ")";
            break;
        }
        case IROp::IR_ZEXT:
            out << "(" << getCType(inst.result.type) << ")" << getValueName(inst.operands[0]);
            break;
        case IROp::IR_TRUNC:
            out << "(" << getCType(inst.result.type) << ")" << getValueName(inst.operands[0]);
            break;
        case IROp::IR_SEXT:
            // Need to sign extend from the operand's type
            {
                std::string opType = "int32_t";
                auto it = valueTypes_.find(inst.operands[0]);
                if (it != valueTypes_.end()) {
                    if (it->second == IRType::I8) opType = "int8_t";
                    else if (it->second == IRType::I16) opType = "int16_t";
                    else if (it->second == IRType::I32) opType = "int32_t";
                }
                out << "(" << getCType(inst.result.type) << ")(int64_t)(" << opType << ")" << getValueName(inst.operands[0]);
            }
            break;
        case IROp::IR_EQ:
            out << getValueName(inst.operands[0]) << " == " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_NE:
            out << getValueName(inst.operands[0]) << " != " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_SLT:
            out << "((int32_t)" << getValueName(inst.operands[0]) << " < (int32_t)" << getValueName(inst.operands[1]) << " ? 1 : 0)";
            break;
        case IROp::IR_SLTU:
            out << "(" << getValueName(inst.operands[0]) << " < " << getValueName(inst.operands[1]) << " ? 1 : 0)";
            break;
        case IROp::IR_SLE:
            out << "((int32_t)" << getValueName(inst.operands[0]) << " <= (int32_t)" << getValueName(inst.operands[1]) << " ? 1 : 0)";
            break;
        case IROp::IR_SGT:
            out << "((int32_t)" << getValueName(inst.operands[0]) << " > (int32_t)" << getValueName(inst.operands[1]) << " ? 1 : 0)";
            break;
        case IROp::IR_SGE:
            out << "((int32_t)" << getValueName(inst.operands[0]) << " >= (int32_t)" << getValueName(inst.operands[1]) << " ? 1 : 0)";
            break;
        case IROp::IR_BRANCH:
            if (inst.operands.empty()) {
                out << "goto bb_" << inst.branchTarget;
            } else {
                out << "if (" << getValueName(inst.operands[0]) << ") goto bb_" << inst.branchTarget;
            }
            break;
        case IROp::IR_JUMP:
            out << "goto bb_" << inst.branchTarget;
            break;
        case IROp::IR_JUMP_INDIRECT:
            out << "ctx->pc = " << getValueName(inst.operands[0]) << "; return; /* Unresolved Indirect Jump Fallback */";
            break;
        case IROp::IR_SWITCH:
            out << "switch (" << getValueName(inst.operands[0]) << ") {\n";
            for (size_t i = 0; i < inst.switchTargets.size(); ++i) {
                out << "        case 0x" << std::hex << inst.switchValues[i] << std::dec 
                    << ": goto bb_" << inst.switchTargets[i] << ";\n";
            }
            out << "        default: ctx->pc = " << getValueName(inst.operands[0]) << "; return; // Bounds limit / Fallback\n";
            out << "    }";
            break;
        case IROp::IR_CALL:
            if (!inst.operands.empty()) {
                out << "ctx->pc = " << getValueName(inst.operands[0]) << "; return; // function CALL \u2192 dispatcher";
            } else {
                out << "ctx->pc = 0x" << std::hex << inst.branchTarget << std::dec << "; return; // function CALL \u2192 dispatcher";
            }
            break;
        case IROp::IR_RETURN:
            if (!inst.operands.empty()) {
                out << "ctx->pc = " << getValueName(inst.operands[0]) << "; return";
            } else {
                out << "ctx->pc = GPR_U32(ctx, 31); return // WARNING: IR_RETURN with no operand";
            }
            break;
        case IROp::IR_IF_LIKELY:
            out << "if (" << getValueName(inst.operands[0]) << ") {";
            break;
        case IROp::IR_END_LIKELY:
            out << "}";
            break;
        case IROp::IR_CONST:
            if (inst.result.type == IRType::F32) {
                out << inst.constData.immFloat << "f";
            } else if (inst.result.type == IRType::I128) {
                if (inst.constData.immUnsigned == 0) {
                    out << "_mm_setzero_si128()";
                } else {
                    out << "_mm_set_epi64x(0, 0x" << std::hex << inst.constData.immUnsigned << std::dec << "ULL)";
                }
            } else {
                out << "(" << getCType(inst.result.type) << ")0x" << std::hex << inst.constData.immUnsigned << std::dec << "ULL";
            }
            break;
        case IROp::IR_LOAD: {
            std::string macroPrefix = "READ";
            switch(inst.memType) {
                case IRType::I8: macroPrefix += "8"; break;
                case IRType::I16: macroPrefix += "16"; break;
                case IRType::I32: macroPrefix += "32"; break;
                case IRType::I64: macroPrefix += "64"; break;
                case IRType::I128: macroPrefix += "128"; break;
                case IRType::F32: macroPrefix += "32"; break;
                default: macroPrefix += "32"; break;
            }
            std::string readExpr = macroPrefix + "(" + getValueName(inst.operands[0]) + ")";
            if (inst.memSigned) {
                if (inst.memType == IRType::I8) readExpr = "(int32_t)(int8_t)" + readExpr;
                else if (inst.memType == IRType::I16) readExpr = "(int32_t)(int16_t)" + readExpr;
                else if (inst.memType == IRType::I32) readExpr = "(int32_t)" + readExpr;
                else if (inst.memType == IRType::I64) readExpr = "(int64_t)" + readExpr;
            }
            out << readExpr;
            break;
        }
        case IROp::IR_STORE: {
            std::string macroPrefix = "WRITE";
            switch(inst.memType) {
                case IRType::I8: macroPrefix += "8"; break;
                case IRType::I16: macroPrefix += "16"; break;
                case IRType::I32: macroPrefix += "32"; break;
                case IRType::I64: macroPrefix += "64"; break;
                case IRType::I128: macroPrefix += "128"; break;
                case IRType::F32: macroPrefix += "32"; break;
                default: macroPrefix += "32"; break;
            }
            std::string valStr = getValueName(inst.operands[1]);
            if (inst.memType == IRType::I128) {
                valStr = "to_m128i(" + valStr + ")";
            }
            out << macroPrefix << "(" << getValueName(inst.operands[0]) << ", " << valStr << ")";
            break;
        }
        case IROp::IR_LOAD_LEFT: {
            // operands[0] = addr (unaligned), operands[1] = old rt value
            std::string addrV = getValueName(inst.operands[0]);
            std::string rtV   = getValueName(inst.operands[1]);
            if (inst.memType == IRType::I64) {
                // LDL: Load Doubleword Left
                out << "([&]() -> uint64_t { "
                    << "uint32_t addr_ = " << addrV << "; "
                    << "uint32_t aligned_ = addr_ & ~7u; "
                    << "uint32_t offset_ = addr_ & 7u; "
                    << "uint64_t mem_ = READ64(aligned_); "
                    << "uint32_t shift_ = (7u - offset_) << 3; "
                    << "uint64_t keepMask_ = (shift_ == 0) ? 0ull : ((1ull << shift_) - 1ull); "
                    << "return (" << rtV << " & keepMask_) | (mem_ << shift_); "
                    << "})()";
            } else {
                // LWL: Load Word Left
                out << "([&]() -> uint32_t { "
                    << "uint32_t addr_ = " << addrV << "; "
                    << "uint32_t aligned_ = addr_ & ~3u; "
                    << "uint32_t offset_ = addr_ & 3u; "
                    << "uint32_t mem_ = READ32(aligned_); "
                    << "uint32_t shift_ = (3u - offset_) << 3; "
                    << "uint32_t keepMask_ = (shift_ == 0) ? 0u : ((1u << shift_) - 1u); "
                    << "return (int32_t)((" << rtV << " & keepMask_) | (mem_ << shift_)); "
                    << "})()";
            }
            break;
        }
        case IROp::IR_LOAD_RIGHT: {
            // operands[0] = addr (unaligned), operands[1] = old rt value
            std::string addrV = getValueName(inst.operands[0]);
            std::string rtV   = getValueName(inst.operands[1]);
            if (inst.memType == IRType::I64) {
                // LDR: Load Doubleword Right
                out << "([&]() -> uint64_t { "
                    << "uint32_t addr_ = " << addrV << "; "
                    << "uint32_t aligned_ = addr_ & ~7u; "
                    << "uint32_t offset_ = addr_ & 7u; "
                    << "uint64_t mem_ = READ64(aligned_); "
                    << "uint32_t shift_ = offset_ << 3; "
                    << "uint64_t keepMask_ = (offset_ == 0) ? 0ull : (0xFFFFFFFFFFFFFFFFull << ((8u - offset_) << 3)); "
                    << "return (" << rtV << " & keepMask_) | (mem_ >> shift_); "
                    << "})()";
            } else {
                // LWR: Load Word Right
                out << "([&]() -> uint32_t { "
                    << "uint32_t addr_ = " << addrV << "; "
                    << "uint32_t aligned_ = addr_ & ~3u; "
                    << "uint32_t offset_ = addr_ & 3u; "
                    << "uint32_t mem_ = READ32(aligned_); "
                    << "uint32_t shift_ = offset_ << 3; "
                    << "uint32_t keepMask_ = (offset_ == 0) ? 0u : (0xFFFFFFFFu << ((4u - offset_) << 3)); "
                    << "uint32_t merged32_ = (" << rtV << " & keepMask_) | (mem_ >> shift_); "
                    << "return merged32_; "
                    << "})()";
            }
            break;
        }
        case IROp::IR_STORE_LEFT: {
            // operands[0] = addr (unaligned), operands[1] = rt value
            std::string addrV = getValueName(inst.operands[0]);
            std::string valV  = getValueName(inst.operands[1]);
            if (inst.memType == IRType::I64) {
                // SDL: Store Doubleword Left
                out << "{ uint32_t addr_ = " << addrV << "; "
                    << "uint32_t aligned_ = addr_ & ~7u; "
                    << "uint32_t offset_ = addr_ & 7u; "
                    << "uint32_t shift_ = (7u - offset_) << 3; "
                    << "uint64_t mask_ = 0xFFFFFFFFFFFFFFFFull >> shift_; "
                    << "uint64_t old_ = READ64(aligned_); "
                    << "uint64_t val_ = " << valV << "; "
                    << "uint64_t new_ = (old_ & ~mask_) | ((val_ >> shift_) & mask_); "
                    << "WRITE64(aligned_, new_); }";
            } else {
                // SWL: Store Word Left
                out << "{ uint32_t addr_ = " << addrV << "; "
                    << "uint32_t aligned_ = addr_ & ~3u; "
                    << "uint32_t offset_ = addr_ & 3u; "
                    << "uint32_t shift_ = (3u - offset_) << 3; "
                    << "uint32_t mask_ = 0xFFFFFFFFu >> shift_; "
                    << "uint32_t old_ = READ32(aligned_); "
                    << "uint32_t val_ = " << valV << "; "
                    << "uint32_t new_ = (old_ & ~mask_) | ((val_ >> shift_) & mask_); "
                    << "WRITE32(aligned_, new_); }";
            }
            break;
        }
        case IROp::IR_STORE_RIGHT: {
            // operands[0] = addr (unaligned), operands[1] = rt value
            std::string addrV = getValueName(inst.operands[0]);
            std::string valV  = getValueName(inst.operands[1]);
            if (inst.memType == IRType::I64) {
                // SDR: Store Doubleword Right
                out << "{ uint32_t addr_ = " << addrV << "; "
                    << "uint32_t aligned_ = addr_ & ~7u; "
                    << "uint32_t offset_ = addr_ & 7u; "
                    << "uint32_t shift_ = offset_ << 3; "
                    << "uint64_t mask_ = 0xFFFFFFFFFFFFFFFFull << shift_; "
                    << "uint64_t old_ = READ64(aligned_); "
                    << "uint64_t val_ = " << valV << "; "
                    << "uint64_t new_ = (old_ & ~mask_) | ((val_ << shift_) & mask_); "
                    << "WRITE64(aligned_, new_); }";
            } else {
                // SWR: Store Word Right
                out << "{ uint32_t addr_ = " << addrV << "; "
                    << "uint32_t aligned_ = addr_ & ~3u; "
                    << "uint32_t offset_ = addr_ & 3u; "
                    << "uint32_t shift_ = offset_ << 3; "
                    << "uint32_t mask_ = 0xFFFFFFFFu << shift_; "
                    << "uint32_t old_ = READ32(aligned_); "
                    << "uint32_t val_ = " << valV << "; "
                    << "uint32_t new_ = (old_ & ~mask_) | ((val_ << shift_) & mask_); "
                    << "WRITE32(aligned_, new_); }";
            }
            break;
        }
        case IROp::IR_NOP:
            if (!inst.comment.empty() && inst.comment.find("[UNHANDLED]") == 0) {
                out << "runtime->SignalException(ctx, EXCEPTION_UNKNOWN_INSTRUCTION); return; // " << inst.comment;
            } else {
                out << "// NOP";
            }
            break;
        case IROp::IR_FADD:
        case IROp::IR_FADDA:
            out << getValueName(inst.operands[0]) << " + " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_FSUB:
            out << getValueName(inst.operands[0]) << " - " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_FMUL:
            out << getValueName(inst.operands[0]) << " * " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_FDIV:
            out << getValueName(inst.operands[0]) << " / " << getValueName(inst.operands[1]);
            break;
        case IROp::IR_FABS:
            out << "std::abs(" << getValueName(inst.operands[0]) << ")";
            break;
        case IROp::IR_FNEG:
            out << "-(" << getValueName(inst.operands[0]) << ")";
            break;
        case IROp::IR_FMOV:
            out << getValueName(inst.operands[0]);
            break;
        case IROp::IR_FSQRT:
            out << "std::sqrt(" << getValueName(inst.operands[0]) << ")";
            break;
        case IROp::IR_CVT_S_W:
            out << "(float)((int32_t)" << getValueName(inst.operands[0]) << ")";
            break;
        case IROp::IR_TRUNC_W_S:
            out << "(int32_t)(" << getValueName(inst.operands[0]) << ")";
            break;
        case IROp::IR_FCMP_EQ:
            out << "(" << getValueName(inst.operands[0]) << " == " << getValueName(inst.operands[1]) << " ? 1 : 0)";
            break;
        case IROp::IR_FCMP_LT:
            out << "(" << getValueName(inst.operands[0]) << " < " << getValueName(inst.operands[1]) << " ? 1 : 0)";
            break;
        case IROp::IR_FCMP_LE:
            out << "(" << getValueName(inst.operands[0]) << " <= " << getValueName(inst.operands[1]) << " ? 1 : 0)";
            break;
        // ── FPU Extended (PS2) ──────────────────────────────────────────
        case IROp::IR_FMADD:
            // acc + (fs * ft)  — operands: [acc, fs, ft]
            out << "(" << getValueName(inst.operands[0]) << " + ("
                << getValueName(inst.operands[1]) << " * "
                << getValueName(inst.operands[2]) << "))";
            break;
        case IROp::IR_FMSUB:
            // acc - (fs * ft)
            out << "(" << getValueName(inst.operands[0]) << " - ("
                << getValueName(inst.operands[1]) << " * "
                << getValueName(inst.operands[2]) << "))";
            break;
        case IROp::IR_FMAX:
            out << "std::fmax(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")";
            break;
        case IROp::IR_FMIN:
            out << "std::fmin(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")";
            break;
        case IROp::IR_FMULA:
            // fs * ft  (result stored into accumulator by lifter via reg-write)
            out << "(" << getValueName(inst.operands[0]) << " * " << getValueName(inst.operands[1]) << ")";
            break;
        case IROp::IR_FSUBA:
            // fs - ft  (result stored into accumulator by lifter via reg-write)
            out << "(" << getValueName(inst.operands[0]) << " - " << getValueName(inst.operands[1]) << ")";
            break;
        // ── Conditional Select ──────────────────────────────────────────
        case IROp::IR_SELECT:
            // cond ? operands[1] : operands[2]
            out << "(" << getValueName(inst.operands[0]) << " ? "
                << getValueName(inst.operands[1]) << " : "
                << getValueName(inst.operands[2]) << ")";
            break;
        case IROp::IR_PADDB:
            out << "_mm_add_epi8(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")";
            break;
        case IROp::IR_PEXTLH:
            out << "_mm_unpacklo_epi16(" << getValueName(inst.operands[1]) << ", " << getValueName(inst.operands[0]) << ")"; // rt, rs
            break;
        case IROp::IR_PEXTUH:
            out << "_mm_unpackhi_epi16(" << getValueName(inst.operands[1]) << ", " << getValueName(inst.operands[0]) << ")";
            break;
        case IROp::IR_PSRLW:
            out << "_mm_srl_epi32(" << getValueName(inst.operands[0]) << ", _mm_cvtsi32_si128(" << getValueName(inst.operands[1]) << "))";
            break;
        case IROp::IR_PSRAW:
            out << "_mm_sra_epi32(" << getValueName(inst.operands[0]) << ", _mm_cvtsi32_si128(" << getValueName(inst.operands[1]) << "))";
            break;

        // ── Phase 4: MMI SIMD ───────────────────────────────────────────────
        case IROp::IR_PADDH: out << "_mm_add_epi16(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PADDW: out << "_mm_add_epi32(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PSUBB: out << "_mm_sub_epi8(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PSUBH: out << "_mm_sub_epi16(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PSUBW: out << "_mm_sub_epi32(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        
        case IROp::IR_PMAXH: out << "_mm_max_epi16(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PMINH: out << "_mm_min_epi16(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        
        case IROp::IR_PAND:  out << "_mm_and_si128(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_POR:   out << "_mm_or_si128(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PXOR:  out << "_mm_xor_si128(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PNOR:  out << "_mm_andnot_si128(_mm_or_si128(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << "), _mm_set1_epi32(-1))"; break;
        
        case IROp::IR_PCEQB: out << "_mm_cmpeq_epi8(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PCEQH: out << "_mm_cmpeq_epi16(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PCEQW: out << "_mm_cmpeq_epi32(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PCGTH: out << "_mm_cmpgt_epi16(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        
        case IROp::IR_PEXTLB: out << "_mm_unpacklo_epi8(" << getValueName(inst.operands[1]) << ", " << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_PEXTLW: out << "_mm_unpacklo_epi32(" << getValueName(inst.operands[1]) << ", " << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_PEXTUB: out << "_mm_unpackhi_epi8(" << getValueName(inst.operands[1]) << ", " << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_PEXTUW: out << "_mm_unpackhi_epi32(" << getValueName(inst.operands[1]) << ", " << getValueName(inst.operands[0]) << ")"; break;
        
        case IROp::IR_PCPYLD: out << "PS2_PCPYLD(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PCPYUD: out << "PS2_PCPYUD(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PCPYH:  out << "PS2_PCPYH(" << getValueName(inst.operands[0]) << ")"; break;
        
        case IROp::IR_PINTEH: out << "PS2_PINTEH(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PPACB:  out << "PS2_PPACB(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        case IROp::IR_PPACW:  out << "PS2_PPACW(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")"; break;
        
        case IROp::IR_PSLLH: out << "_mm_sll_epi16(" << getValueName(inst.operands[0]) << ", _mm_cvtsi32_si128(" << getValueName(inst.operands[1]) << "))"; break;
        case IROp::IR_PSLLW: out << "_mm_sll_epi32(" << getValueName(inst.operands[0]) << ", _mm_cvtsi32_si128(" << getValueName(inst.operands[1]) << "))"; break;
        case IROp::IR_PSRAH: out << "_mm_sra_epi16(" << getValueName(inst.operands[0]) << ", _mm_cvtsi32_si128(" << getValueName(inst.operands[1]) << "))"; break;
        case IROp::IR_PSRLH: out << "_mm_srl_epi16(" << getValueName(inst.operands[0]) << ", _mm_cvtsi32_si128(" << getValueName(inst.operands[1]) << "))"; break;
        
        case IROp::IR_PEXCH: out << "PS2_PEXCH(" << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_PEXCW: out << "PS2_PEXCW(" << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_PEXEH: out << "PS2_PEXEH(" << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_PEXEW: out << "PS2_PEXEW(" << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_PLZCW: out << "PS2_PLZCW(" << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_PREVH: out << "PS2_PREVH(" << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_PROT3W:out << "PS2_PROT3W(" << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_QFSRV: out << "PS2_QFSRV(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ", " << getValueName(inst.operands[2]) << ")"; break;
        
        case IROp::IR_PMFHL: out << "PS2_PMFHL(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ", " << getValueName(inst.operands[2]) << ")"; break;


        // ── Phase 5: VU0 Arithmetic ─────────────────────────────────────────
        case IROp::IR_VU_ADD:
            out << "VU0_ADD(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << (int)inst.vuDestMask << ", " << (int)inst.vuBroadcast << ")";
            break;
        case IROp::IR_VU_SUB:
            out << "VU0_SUB(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << (int)inst.vuDestMask << ", " << (int)inst.vuBroadcast << ")";
            break;
        case IROp::IR_VU_MUL:
            out << "VU0_MUL(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << (int)inst.vuDestMask << ", " << (int)inst.vuBroadcast << ")";
            break;
        case IROp::IR_VU_MADD:
            out << "VU0_MADD(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << getValueName(inst.operands[2])
                << ", " << (int)inst.vuDestMask << ", " << (int)inst.vuBroadcast << ")";
            break;
        case IROp::IR_VU_MSUB:
            out << "VU0_MSUB(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << getValueName(inst.operands[2])
                << ", " << (int)inst.vuDestMask << ", " << (int)inst.vuBroadcast << ")";
            break;
        case IROp::IR_VU_MAX:
            out << "VU0_MAX(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << (int)inst.vuDestMask << ", " << (int)inst.vuBroadcast << ")";
            break;
        case IROp::IR_VU_MINI:
            out << "VU0_MINI(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << (int)inst.vuDestMask << ", " << (int)inst.vuBroadcast << ")";
            break;
        case IROp::IR_VU_ABS:
            out << "VU0_ABS(" << getValueName(inst.operands[0])
                << ", " << (int)inst.vuDestMask << ")";
            break;
        case IROp::IR_VU_OPMULA:
            out << "VU0_OPMULA(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")";
            break;
        case IROp::IR_VU_OPMSUB:
            out << "VU0_OPMSUB(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << getValueName(inst.operands[2]) << ")";
            break;
        // VU0 Accumulator-writing
        case IROp::IR_VU_ADDA:
            out << "VU0_ADDA(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << (int)inst.vuDestMask << ", " << (int)inst.vuBroadcast << ")";
            break;
        case IROp::IR_VU_SUBA:
            out << "VU0_SUBA(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << (int)inst.vuDestMask << ", " << (int)inst.vuBroadcast << ")";
            break;
        case IROp::IR_VU_MULA:
            out << "VU0_MULA(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << (int)inst.vuDestMask << ", " << (int)inst.vuBroadcast << ")";
            break;
        case IROp::IR_VU_MADDA:
            out << "VU0_MADDA(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << getValueName(inst.operands[2])
                << ", " << (int)inst.vuDestMask << ", " << (int)inst.vuBroadcast << ")";
            break;
        case IROp::IR_VU_MSUBA:
            out << "VU0_MSUBA(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << getValueName(inst.operands[2])
                << ", " << (int)inst.vuDestMask << ", " << (int)inst.vuBroadcast << ")";
            break;
        // VU0 Conversion
        case IROp::IR_VU_ITOF0:  out << "VU0_ITOF0("  << getValueName(inst.operands[0]) << ", " << (int)inst.vuDestMask << ")"; break;
        case IROp::IR_VU_ITOF4:  out << "VU0_ITOF4("  << getValueName(inst.operands[0]) << ", " << (int)inst.vuDestMask << ")"; break;
        case IROp::IR_VU_ITOF12: out << "VU0_ITOF12(" << getValueName(inst.operands[0]) << ", " << (int)inst.vuDestMask << ")"; break;
        case IROp::IR_VU_ITOF15: out << "VU0_ITOF15(" << getValueName(inst.operands[0]) << ", " << (int)inst.vuDestMask << ")"; break;
        case IROp::IR_VU_FTOI0:  out << "VU0_FTOI0("  << getValueName(inst.operands[0]) << ", " << (int)inst.vuDestMask << ")"; break;
        case IROp::IR_VU_FTOI4:  out << "VU0_FTOI4("  << getValueName(inst.operands[0]) << ", " << (int)inst.vuDestMask << ")"; break;
        case IROp::IR_VU_FTOI12: out << "VU0_FTOI12(" << getValueName(inst.operands[0]) << ", " << (int)inst.vuDestMask << ")"; break;
        case IROp::IR_VU_FTOI15: out << "VU0_FTOI15(" << getValueName(inst.operands[0]) << ", " << (int)inst.vuDestMask << ")"; break;
        // VU0 Data Movement
        case IROp::IR_VU_MOVE:
            out << "VU0_MOVE(" << getValueName(inst.operands[0]) << ", " << (int)inst.vuDestMask << ")";
            break;
        case IROp::IR_VU_MR32:
            out << "VU0_MR32(" << getValueName(inst.operands[0]) << ", " << (int)inst.vuDestMask << ")";
            break;
        // VU0 Division / Sqrt
        case IROp::IR_VU_DIV:
            out << "VU0_DIV(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << (int)inst.vuFSF << ", " << (int)inst.vuFTF << ")";
            break;
        case IROp::IR_VU_SQRT:
            out << "VU0_SQRT(" << getValueName(inst.operands[0]) << ", " << (int)inst.vuFTF << ")";
            break;
        case IROp::IR_VU_RSQRT:
            out << "VU0_RSQRT(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1])
                << ", " << (int)inst.vuFSF << ", " << (int)inst.vuFTF << ")";
            break;
        case IROp::IR_VU_WAITQ:
            out << "/* VWAITQ: nop on recompiled */";
            break;
        // VU0 Clip
        case IROp::IR_VU_CLIP:
            out << "VU0_CLIP(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")";
            break;
        // VU0 Transfer
        case IROp::IR_VU_QMFC2: out << "_mm_castps_si128(" << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_VU_QMTC2: out << "_mm_castsi128_ps(" << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_VU_CFC2:  out << "(uint32_t)(" << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_VU_CTC2:  out << "(uint16_t)(" << getValueName(inst.operands[0]) << ")"; break;
        case IROp::IR_VU_MTIR:  out << "VU0_MTIR(" << getValueName(inst.operands[0]) << ", " << (int)inst.vuFSF << ")"; break;
        // VU0 Load/Store
        case IROp::IR_LOAD_VF:
            out << "VU0_LOAD_VF(rdram, " << getValueName(inst.operands[0]) << ")";
            break;
        case IROp::IR_STORE_VF:
            out << "VU0_STORE_VF(rdram, " << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")";
            break;
        case IROp::IR_VU_LQI:
            out << "VU0_LQI(ctx, " << getValueName(inst.operands[0]) << ", " << (int)inst.vuDestMask << ")";
            break;
        case IROp::IR_VU_LQD:
            out << "VU0_LQD(ctx, " << getValueName(inst.operands[0]) << ", " << (int)inst.vuDestMask << ")";
            break;

        case IROp::IR_SYSCALL:
            out << "runtime->handleSyscall(rdram, ctx)";
            break;
        case IROp::IR_BREAK:
            out << "runtime->SignalException(ctx, EXCEPTION_BREAKPOINT); return";
            break;
        case IROp::IR_EI:
            out << "ctx->cop0_status |= 0x1 /* ei */";
            break;
        case IROp::IR_DI:
            out << "ctx->cop0_status &= ~0x1 /* di */";
            break;
        default:
            if (inst.result.type != IRType::Void) {
                out << "([&]() -> " << getCType(inst.result.type) << " { runtime->SignalException(ctx, EXCEPTION_UNKNOWN_INSTRUCTION); std::abort(); return " << getCType(inst.result.type) << "{}; })() /* UNHANDLED OPCODE: " << getOpString(inst.op) << " */";
            } else {
                out << "runtime->SignalException(ctx, EXCEPTION_UNKNOWN_INSTRUCTION); return; // UNHANDLED OPCODE: " << getOpString(inst.op);
            }
            break;
    }

    out << ";\n";
}

std::string CppEmitter::getOpString(IROp op) const {
    return std::string(irOpName(op));
}

IRType CppEmitter::getRegNativeType(const IRReg& reg) const {
    switch (reg.kind) {
        case IRRegKind::GPR:     return IRType::I128; // 128-bit GPR registers
        case IRRegKind::FPR:     return IRType::F32;  // float f[32]
        case IRRegKind::VF:      return IRType::V128; // __m128 vu0_vf[32]
        case IRRegKind::VI:      return IRType::I16;  // uint16_t vi[16]
        case IRRegKind::HI:      return IRType::I64;  // uint64_t hi, hi1
        case IRRegKind::LO:      return IRType::I64;  // uint64_t lo, lo1
        case IRRegKind::SA:      return IRType::I32;  // uint32_t sa
        case IRRegKind::FPU_CC:  return IRType::I32;  // uint32_t fcr31
        case IRRegKind::FPU_ACC: return IRType::F32;  // float fpu_acc
        case IRRegKind::VU_ACC:  return IRType::V128; // __m128 vu0_acc
        case IRRegKind::VU_Q:    return IRType::F32;  // float vu0_q
        case IRRegKind::VU_P:    return IRType::F32;  // float vu0_p
        case IRRegKind::VU_I:    return IRType::F32;  // float vu0_i
        case IRRegKind::VU_R:    return IRType::F32;  // float vu0_r
        case IRRegKind::COP0:    return IRType::I32;  // uint32_t cop0_*
        case IRRegKind::PC:      return IRType::I32;  // uint32_t pc
        default:                 return IRType::I32;
    }
}

std::string CppEmitter::getRegName(const IRReg& reg) const {
    switch (reg.kind) {
        case IRRegKind::GPR: return "r[" + std::to_string(reg.index) + "]";
        case IRRegKind::FPR: return "f[" + std::to_string(reg.index) + "]";
        case IRRegKind::VF:  return "vu0_vf[" + std::to_string(reg.index) + "]";
        case IRRegKind::VI:  return "vi[" + std::to_string(reg.index) + "]";
        case IRRegKind::HI:  return reg.index == 0 ? "hi" : "hi1";
        case IRRegKind::LO:  return reg.index == 0 ? "lo" : "lo1";
        case IRRegKind::SA:  return "sa";
        case IRRegKind::FPU_CC: return "fcr31";
        case IRRegKind::FPU_ACC: return "fpu_acc";
        case IRRegKind::VU_ACC:  return "vu0_acc";
        case IRRegKind::VU_Q:    return "vu0_q";
        case IRRegKind::VU_P:    return "vu0_p";
        case IRRegKind::VU_I:    return "vu0_i";
        case IRRegKind::VU_R:    return "vu0_r";
        case IRRegKind::COP0: {
            switch (reg.index) {
                case 12: return "cop0_status";
                case 13: return "cop0_cause";
                case 14: return "cop0_epc";
                default: return "cop0_index";
            }
        }
        case IRRegKind::PC: return "pc";
        default: return "UNKNOWN_REG_" + std::to_string(static_cast<int>(reg.kind));
    }
}

std::string CppEmitter::getValueName(ValueId id) const {
    return "v" + std::to_string(id);
}

std::string CppEmitter::getCType(IRType type) const {
    switch (type) {
        case IRType::I1: return "bool";
        case IRType::I8: return "uint8_t";
        case IRType::I16: return "uint16_t";
        case IRType::I32: return "uint32_t";
        case IRType::I64: return "uint64_t";
        case IRType::I128: return "__m128i";
        case IRType::V128: return "__m128";
        case IRType::F32: return "float";
        default: return "uint32_t";
    }
}

} // namespace ps2recomp
