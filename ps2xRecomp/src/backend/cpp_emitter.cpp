// ============================================================================
// cpp_emitter.cpp — Backend emitter for translating IR to C++ code
// Part of PS2reAIcomp
// ============================================================================

#include "cpp_emitter.h"
#include <bit>
#include <cmath>

namespace ps2recomp {

using namespace ir;

CppEmitter::CppEmitter() = default;
CppEmitter::~CppEmitter() = default;

std::string CppEmitter::emitFunction(const IRFunction& func) {
    std::ostringstream out;

    out << "// Emitted C++ backend for " << func.name << "\n";
    out << "extern \"C\" void ps2_" << func.name << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {\n";
    out << "    // TODO: Define context and basic arguments\n\n";
    // Setup block dispatch for indirect intra-function jumps or just basic blocks if needed
    // For now we just dump basic blocks linearly.

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
                out << "ctx->" << getRegName(inst.reg);
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
                out << "ctx->" << getRegName(inst.reg) << " = " << getValueName(inst.operands[0]);
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
                out << "ctx->pc = " << getValueName(inst.operands[0]) << "; // function CALL (stub)";
            } else {
                out << "ctx->pc = 0x" << std::hex << inst.branchTarget << std::dec << "; // function CALL (stub)";
            }
            break;
        case IROp::IR_RETURN:
            out << "return";
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
        case IROp::IR_PADDB:
            out << "_mm_add_epi8(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")";
            break;
        case IROp::IR_PEXTUW:
            out << "MMI_PEXTUW(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")";
            break;
        case IROp::IR_PCPYLD:
            out << "MMI_PCPYLD(" << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")";
            break;
        case IROp::IR_PEXTLH:
            out << "_mm_unpacklo_epi16(" << getValueName(inst.operands[1]) << ", " << getValueName(inst.operands[0]) << ")"; // rt, rs
            break;
        case IROp::IR_PEXTUH:
            out << "_mm_unpackhi_epi16(" << getValueName(inst.operands[1]) << ", " << getValueName(inst.operands[0]) << ")";
            break;
        case IROp::IR_PEXTLB:
            out << "_mm_unpacklo_epi8(" << getValueName(inst.operands[1]) << ", " << getValueName(inst.operands[0]) << ")";
            break;
        case IROp::IR_PEXTUB:
            out << "_mm_unpackhi_epi8(" << getValueName(inst.operands[1]) << ", " << getValueName(inst.operands[0]) << ")";
            break;
        case IROp::IR_PPACB:
            out << "MMI_PPACB(ctx, " << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")";
            break;
        case IROp::IR_PPACW:
            out << "MMI_PPACW(ctx, " << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")";
            break;
        case IROp::IR_PSLLW:
            out << "_mm_sll_epi32(" << getValueName(inst.operands[0]) << ", _mm_cvtsi32_si128(" << getValueName(inst.operands[1]) << "))";
            break;
        case IROp::IR_PSRLW:
            out << "_mm_srl_epi32(" << getValueName(inst.operands[0]) << ", _mm_cvtsi32_si128(" << getValueName(inst.operands[1]) << "))";
            break;
        case IROp::IR_PSRAW:
            out << "_mm_sra_epi32(" << getValueName(inst.operands[0]) << ", _mm_cvtsi32_si128(" << getValueName(inst.operands[1]) << "))";
            break;
        case IROp::IR_SYSCALL:
            out << "runtime->handleSyscall(rdram, ctx)";
            break;
        case IROp::IR_BREAK:
            out << "runtime->SignalException(ctx, EXCEPTION_BREAKPOINT); return";
            break;
        case IROp::IR_EI:
            out << "ctx->cop0_status |= 0x10000 /* ei */";
            break;
        case IROp::IR_DI:
            out << "ctx->cop0_status &= ~0x10000 /* di */";
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

std::string CppEmitter::getRegName(const IRReg& reg) const {
    switch (reg.kind) {
        case IRRegKind::GPR: return "r[" + std::to_string(reg.index) + "]";
        case IRRegKind::FPR: return "f[" + std::to_string(reg.index) + "]";
        case IRRegKind::HI:  return reg.index == 0 ? "hi" : "hi1";
        case IRRegKind::LO:  return reg.index == 0 ? "lo" : "lo1";
        case IRRegKind::SA:  return "sa";
        case IRRegKind::FPU_CC: return "fcr31";
        case IRRegKind::FPU_ACC: return "fpu_acc"; // R5900Context needs to handle this? Or maybe we can just say fpu_acc, let's fall back to f[0] if it's missing but ideally we don't hit this yet
        case IRRegKind::COP0: { // Standard cop0 mapped fields
            switch (reg.index) {
                case 12: return "cop0_status";
                case 13: return "cop0_cause";
                case 14: return "cop0_epc";
                default: return "cop0_index"; // Dummy fallback
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
        case IRType::F32: return "float";
        default: return "uint32_t";
    }
}

} // namespace ps2recomp
