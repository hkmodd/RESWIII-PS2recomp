// ============================================================================
// cpp_emitter.h — Backend emitter for translating IR to C++ code
// Part of PS2reAIcomp
// ============================================================================

#ifndef PS2RECOMP_CPP_EMITTER_H
#define PS2RECOMP_CPP_EMITTER_H

#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>
#include "ps2recomp/ir.h"

namespace ps2recomp {

class CppEmitter {
public:
    CppEmitter();
    ~CppEmitter();

    // Emits backend C++ code equivalent to the given IRFunction
    std::string emitFunction(const ir::IRFunction& func);

private:
    void emitBasicBlockHeader(std::ostringstream& out, const ir::IRBasicBlock& bb);
    void emitInstruction(std::ostringstream& out, const ir::IRInst& inst);

    // Helpers for specific IR opcodes
    std::string getOpString(ir::IROp op) const;
    std::string getRegName(const ir::IRReg& reg) const;
    std::string getValueName(ir::ValueId id) const;
    std::string getCType(ir::IRType type) const;

    std::unordered_map<ir::ValueId, ir::IRType> valueTypes_;
};

} // namespace ps2recomp

#endif // PS2RECOMP_CPP_EMITTER_H
