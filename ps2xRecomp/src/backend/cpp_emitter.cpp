// ============================================================================
// cpp_emitter.cpp — Backend emitter for translating IR to C++ code
// Part of PS2reAIcomp
// ============================================================================

#include "cpp_emitter.h"

namespace ps2recomp {

using namespace ir;

CppEmitter::CppEmitter() = default;
CppEmitter::~CppEmitter() = default;

std::string CppEmitter::emitFunction(const IRFunction& func) {
    std::ostringstream out;

    out << "// Emitted C++ backend for " << func.name << "\n";
    out << "extern \"C\" void " << func.name << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {\n";
    out << "    // TODO: Define context and basic arguments\n\n";
    // Setup block dispatch for indirect intra-function jumps or just basic blocks if needed
    // For now we just dump basic blocks linearly.

    for (const auto& bb : func.blocks) {
        emitBasicBlockHeader(out, bb);
        
        for (const auto& inst : bb.instructions) {
            emitInstruction(out, inst);
        }
        out << "\n";
    }

    out << "}\n";
    return out.str();
}

void CppEmitter::emitBasicBlockHeader(std::ostringstream& out, const IRBasicBlock& bb) {
    if (!bb.label.empty()) {
        out << bb.label << ":\n";
    } else {
        out << "bb_" << bb.index << "_0x" << std::hex << bb.mipsStartAddr << std::dec << ":\n";
    }
}

void CppEmitter::emitInstruction(std::ostringstream& out, const IRInst& inst) {
    out << "    ";
    
    if (inst.hasResult()) {
        out << getCType(inst.result.type) << " " << getValueName(inst.result.id) << " = ";
    }

    switch (inst.op) {
        case IROp::IR_COMMENT:
            out << "// " << inst.comment << "\n";
            return; // We don't want the generic end-of-instruction semicolon for comments
        case IROp::IR_REG_READ:
            if (inst.reg.kind == IRRegKind::GPR) {
                out << (inst.result.type == IRType::I64 ? "GPR_U64(ctx, " : "GPR_U32(ctx, ") << (int)inst.reg.index << ")";
            } else {
                out << "ctx->" << getRegName(inst.reg);
            }
            break;
        case IROp::IR_REG_WRITE:
            if (inst.reg.kind == IRRegKind::GPR) {
                out << "SET_GPR_U64(ctx, " << (int)inst.reg.index << ", " << getValueName(inst.operands[0]) << ")";
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
        case IROp::IR_LUI:
            out << "((" << getValueName(inst.operands[0]) << ") << 16)";
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
        case IROp::IR_CALL:
            if (!inst.operands.empty()) {
                out << "ctx->pc = " << getValueName(inst.operands[0]) << "; // function CALL (stub)";
            } else {
                out << "ctx->pc = 0x" << std::hex << inst.branchTarget << std::dec << "; // function CALL (stub)";
            }
            break;
        case IROp::IR_RETURN:
            out << "ctx->pc = " << getValueName(inst.operands[0]) << "; return";
            break;
        case IROp::IR_CONST:
            if (inst.result.type == IRType::F32) {
                out << inst.constData.immFloat << "f";
            } else {
                out << "0x" << std::hex << inst.constData.immUnsigned << std::dec;
            }
            break;
        case IROp::IR_LOAD: {
            std::string macroPrefix = "MEM_READ";
            switch(inst.memType) {
                case IRType::I8: macroPrefix += "8"; break;
                case IRType::I16: macroPrefix += "16"; break;
                case IRType::I32: macroPrefix += "32"; break;
                case IRType::I64: macroPrefix += "64"; break;
                case IRType::I128: macroPrefix += "128"; break;
                case IRType::F32: macroPrefix += "32"; break;
                default: macroPrefix += "32"; break;
            }
            std::string readExpr = macroPrefix + "(rdram, " + getValueName(inst.operands[0]) + ")";
            if (inst.memSigned) {
                if (inst.memType == IRType::I8) readExpr = "(int32_t)(int8_t)" + readExpr;
                else if (inst.memType == IRType::I16) readExpr = "(int32_t)(int16_t)" + readExpr;
                else if (inst.memType == IRType::I32) readExpr = "(int32_t)" + readExpr;
                else readExpr = "(int64_t)" + readExpr;
            }
            out << readExpr;
            break;
        }
        case IROp::IR_STORE: {
            std::string macroPrefix = "MEM_WRITE";
            switch(inst.memType) {
                case IRType::I8: macroPrefix += "8"; break;
                case IRType::I16: macroPrefix += "16"; break;
                case IRType::I32: macroPrefix += "32"; break;
                case IRType::I64: macroPrefix += "64"; break;
                case IRType::I128: macroPrefix += "128"; break;
                case IRType::F32: macroPrefix += "32"; break;
                default: macroPrefix += "32"; break;
            }
            out << macroPrefix << "(rdram, " << getValueName(inst.operands[0]) << ", " << getValueName(inst.operands[1]) << ")";
            break;
        }
        case IROp::IR_NOP:
            out << "// NOP";
            break;
        default:
            out << "// TODO: Unimplemented instruction (" << getOpString(inst.op) << ")";
            break;
    }

    out << ";\n";
}

std::string CppEmitter::getOpString(IROp op) const {
    return std::string(irOpName(op));
}

std::string CppEmitter::getRegName(const IRReg& reg) const {
    switch (reg.kind) {
        case IRRegKind::GPR: return "GPR[" + std::to_string(reg.index) + "]";
        case IRRegKind::FPR: return "FPR[" + std::to_string(reg.index) + "]";
        default: return "UNKNOWN_REG";
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
        case IRType::F32: return "float";
        default: return "uint32_t";
    }
}

} // namespace ps2recomp
