#ifndef PS2RECOMP_IR_H
#define PS2RECOMP_IR_H

// =============================================================================
// PS2Recomp Intermediate Representation (IR) — Sprint 0.1
// =============================================================================
//
// This header defines the SSA-form IR used to lift R5900 MIPS instructions
// into a target-independent representation before lowering to C++ or x86-64.
//
// Design principles:
//   1. SEMANTIC opcodes — not 1:1 copies of MIPS encoding. ADD/ADDU both map
//      to IR_ADD because the IR handles width/signedness through IRType.
//   2. SSA form — every IRValue is defined exactly once. PHI nodes merge
//      values at basic block join points.
//   3. R5900-aware — 128-bit GPRs, dual HI/LO pairs, FPU accumulator,
//      VU0 macro vector registers are first-class citizens.
//   4. Zero coupling to existing pipeline — this file can be included
//      without modifying any existing recompiler code.
//
// Architecture reference: EE Core Instruction Set (R5900), VU0 Macro Mode
// =============================================================================

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <memory>
#include <cassert>

namespace ps2recomp {
namespace ir {

// =============================================================================
// Forward declarations
// =============================================================================
struct IRValue;
struct IRInst;
struct IRBasicBlock;
struct IRFunction;

// Unique ID for SSA values (monotonically increasing per function)
using ValueId = uint32_t;
constexpr ValueId INVALID_VALUE_ID = UINT32_MAX;

// =============================================================================
// IRType — Value widths in the IR
// =============================================================================
// The R5900 has an unusual register file: GPRs are 128-bit, FPRs are 32-bit
// single-precision, VU0 VF regs are 128-bit (4x float32). We need explicit
// width tracking to generate correct C++ casts and AVX intrinsics.
// =============================================================================
enum class IRType : uint8_t {
    Void = 0,   // No value (used for stores, branches, etc.)
    I1,         // Boolean (condition codes, comparisons)
    I8,         // Byte (LB/SB)
    I16,        // Halfword (LH/SH, VU0 integer regs)
    I32,        // Word (most ALU ops, FPU as integer view)
    I64,        // Doubleword (DADD, LD/SD)
    I128,       // Quadword (LQ/SQ, MMI parallel ops, full GPR)
    F32,        // Single-precision float (FPU, VU0 component)
    V128,       // 4x F32 vector (VU0 VF registers)
};

/// Returns the byte size of an IRType
inline uint32_t irTypeSizeBytes(IRType t) {
    switch (t) {
        case IRType::Void: return 0;
        case IRType::I1:   return 1;
        case IRType::I8:   return 1;
        case IRType::I16:  return 2;
        case IRType::I32:  return 4;
        case IRType::I64:  return 8;
        case IRType::I128: return 16;
        case IRType::F32:  return 4;
        case IRType::V128: return 16;
    }
    return 0;
}

/// Returns a human-readable name for an IRType
inline const char* irTypeName(IRType t) {
    switch (t) {
        case IRType::Void: return "void";
        case IRType::I1:   return "i1";
        case IRType::I8:   return "i8";
        case IRType::I16:  return "i16";
        case IRType::I32:  return "i32";
        case IRType::I64:  return "i64";
        case IRType::I128: return "i128";
        case IRType::F32:  return "f32";
        case IRType::V128: return "v128";
    }
    return "?";
}

/// Returns the bit width of an IRType
inline uint32_t irTypeBits(IRType t) {
    return irTypeSizeBytes(t) * 8;
}

// =============================================================================
// IRReg — R5900 Register File Mapping
// =============================================================================
// Encodes which physical register an IR value refers to. This is used for:
//   - Loading the initial value of a register at function entry
//   - Storing back to a register before a call or return
//   - Register allocation hints in the backend
//
// Encoding: kind (high 8 bits) | index (low 8 bits)
// =============================================================================
enum class IRRegKind : uint8_t {
    GPR = 0,    // General Purpose Register (r0-r31, 128-bit on R5900)
    FPR,        // Floating Point Register (f0-f31, 32-bit single)
    VF,         // VU0 Vector Float Register (vf0-vf31, 128-bit = 4x f32)
    VI,         // VU0 Vector Integer Register (vi0-vi15, 16-bit)
    HI,         // HI register (multiply/divide result high, index: 0=HI, 1=HI1)
    LO,         // LO register (multiply/divide result low, index: 0=LO, 1=LO1)
    SA,         // Shift Amount register (PS2-specific)
    FPU_ACC,    // FPU Accumulator (ADDA.S, MULA.S, etc.)
    VU_ACC,     // VU0 Accumulator (VADDA, VMULA, etc.)
    FPU_CC,     // FPU Condition Code (C.xx.S result)
    COP0,       // COP0 control registers (Status, Cause, EPC, etc.)
    VU_Q,       // VU0 Q register (division result)
    VU_P,       // VU0 P register (EFU result)
    VU_I,       // VU0 I register (immediate broadcast)
    VU_R,       // VU0 R register (random number)
    PC,         // Program Counter (for branch targets)
    MEM,        // Memory (pseudo-register for load/store targets)
};

struct IRReg {
    IRRegKind kind;
    uint8_t   index;    // Register number within kind

    IRReg() : kind(IRRegKind::GPR), index(0) {}
    IRReg(IRRegKind k, uint8_t i) : kind(k), index(i) {}

    bool operator==(const IRReg& o) const { return kind == o.kind && index == o.index; }
    bool operator!=(const IRReg& o) const { return !(*this == o); }

    // Convenience factories
    static IRReg gpr(uint8_t n)  { return {IRRegKind::GPR, n}; }
    static IRReg fpr(uint8_t n)  { return {IRRegKind::FPR, n}; }
    static IRReg vf(uint8_t n)   { return {IRRegKind::VF, n}; }
    static IRReg vi(uint8_t n)   { return {IRRegKind::VI, n}; }
    static IRReg hi(uint8_t n=0) { return {IRRegKind::HI, n}; }   // n=0: HI, n=1: HI1
    static IRReg lo(uint8_t n=0) { return {IRRegKind::LO, n}; }   // n=0: LO, n=1: LO1
    static IRReg sa()            { return {IRRegKind::SA, 0}; }
    static IRReg fpuAcc()        { return {IRRegKind::FPU_ACC, 0}; }
    static IRReg vuAcc()         { return {IRRegKind::VU_ACC, 0}; }
    static IRReg fpuCC()         { return {IRRegKind::FPU_CC, 0}; }
    static IRReg cop0(uint8_t n) { return {IRRegKind::COP0, n}; }
    static IRReg vuQ()           { return {IRRegKind::VU_Q, 0}; }
    static IRReg vuP()           { return {IRRegKind::VU_P, 0}; }
    static IRReg vuI()           { return {IRRegKind::VU_I, 0}; }
    static IRReg vuR()           { return {IRRegKind::VU_R, 0}; }
    static IRReg pc()            { return {IRRegKind::PC, 0}; }

    /// Predicate helpers
    bool isGPR()  const { return kind == IRRegKind::GPR; }
    bool isFPR()  const { return kind == IRRegKind::FPR; }
    bool isVF()   const { return kind == IRRegKind::VF; }
    bool isVI()   const { return kind == IRRegKind::VI; }
};

/// Returns a human-readable name for a register
inline const char* irRegName(IRReg reg) {
    // MIPS GPR ABI names
    static const char* gprNames[32] = {
        "$zero","$at","$v0","$v1","$a0","$a1","$a2","$a3",
        "$t0","$t1","$t2","$t3","$t4","$t5","$t6","$t7",
        "$s0","$s1","$s2","$s3","$s4","$s5","$s6","$s7",
        "$t8","$t9","$k0","$k1","$gp","$sp","$fp","$ra"
    };
    // Thread-local static for dynamic names
    thread_local char buf[16];
    switch (reg.kind) {
        case IRRegKind::GPR:
            if (reg.index < 32) return gprNames[reg.index];
            break;
        case IRRegKind::FPR:
            std::snprintf(buf, sizeof(buf), "$f%u", reg.index);
            return buf;
        case IRRegKind::VF:
            std::snprintf(buf, sizeof(buf), "vf%u", reg.index);
            return buf;
        case IRRegKind::VI:
            std::snprintf(buf, sizeof(buf), "vi%u", reg.index);
            return buf;
        case IRRegKind::HI:
            return reg.index == 0 ? "hi" : "hi1";
        case IRRegKind::LO:
            return reg.index == 0 ? "lo" : "lo1";
        case IRRegKind::SA:       return "sa";
        case IRRegKind::FPU_ACC:  return "fpu_acc";
        case IRRegKind::VU_ACC:   return "vu_acc";
        case IRRegKind::FPU_CC:   return "fpu_cc";
        case IRRegKind::COP0:
            std::snprintf(buf, sizeof(buf), "cop0_%u", reg.index);
            return buf;
        case IRRegKind::VU_Q:     return "vu_q";
        case IRRegKind::VU_P:     return "vu_p";
        case IRRegKind::VU_I:     return "vu_i";
        case IRRegKind::VU_R:     return "vu_r";
        case IRRegKind::PC:       return "pc";
        case IRRegKind::MEM:      return "mem";
    }
    return "?reg?";
}
// =============================================================================
// IROp — Semantic IR Opcodes
// =============================================================================
// These are NOT 1:1 MIPS opcodes. Multiple MIPS instructions can map to the
// same IROp (e.g., ADD and ADDU both become IR_ADD with appropriate width).
// The IR is designed to be lowered to C++ or directly to x86-64.
//
// Naming convention: IR_<CATEGORY>_<OPERATION>
// Categories:
//   - Arithmetic, Logic, Shift, Compare, Branch, Memory, Move
//   - Mul/Div (separate because R5900 uses HI/LO pairs)
//   - FPU (single-precision float operations)
//   - MMI (128-bit parallel SIMD operations, PS2-specific)
//   - VU0 (VU0 macro mode vector operations)
//   - COP0 (system control)
//   - SSA (PHI, LABEL — control flow merge points)
//   - Meta (NOP, BREAK, SYSCALL, COMMENT — non-computational)
// =============================================================================
enum class IROp : uint16_t {
    // =========================================================================
    // Meta / SSA Infrastructure
    // =========================================================================
    IR_NOP = 0,         // No operation (placeholder)
    IR_PHI,             // SSA PHI node: merges values from predecessor blocks
    IR_LABEL,           // Basic block label (target of branches)
    IR_COMMENT,         // Human-readable annotation (debug info, source address)

    // =========================================================================
    // Constants & Immediates
    // =========================================================================
    IR_CONST,           // Constant value (immediate embedded in IR)
    IR_UNDEF,           // Undefined value (uninitialized register reads)

    // =========================================================================
    // Register Access (load/store to/from physical R5900 regs)
    // =========================================================================
    IR_REG_READ,        // Read from a physical register → SSA value
    IR_REG_WRITE,       // Write an SSA value → physical register

    // =========================================================================
    // Integer Arithmetic
    // =========================================================================
    // Width determined by IRType (I32 for word ops, I64 for doubleword)
    IR_ADD,             // Addition (ADD, ADDU, ADDI, ADDIU, DADD, DADDU, DADDI, DADDIU)
    IR_SUB,             // Subtraction (SUB, SUBU, DSUB, DSUBU)
    IR_NEG,             // Negate (pseudo: SUB from zero)

    // Sign/zero extension
    IR_SEXT,            // Sign-extend (e.g., I32 → I64)
    IR_ZEXT,            // Zero-extend (e.g., I8 → I32 for LBU)
    IR_TRUNC,           // Truncate (e.g., I64 → I32)
    IR_BITCAST,         // Bitwise cast between types of same size

    // =========================================================================
    // Integer Logic
    // =========================================================================
    IR_AND,             // Bitwise AND (AND, ANDI)
    IR_OR,              // Bitwise OR  (OR, ORI)
    IR_XOR,             // Bitwise XOR (XOR, XORI)
    IR_NOR,             // Bitwise NOR (NOR)
    IR_NOT,             // Bitwise NOT (pseudo: NOR with zero)

    // =========================================================================
    // Shifts
    // =========================================================================
    IR_SLL,             // Shift Left Logical  (SLL, SLLV, DSLL, DSLLV, DSLL32)
    IR_SRL,             // Shift Right Logical (SRL, SRLV, DSRL, DSRLV, DSRL32)
    IR_SRA,             // Shift Right Arithmetic (SRA, SRAV, DSRA, DSRAV, DSRA32)
    IR_SHL,             // Shift Left
    IR_LSHR,            // Logical Shift Right
    IR_ASHR,            // Arithmetic Shift Right

    // =========================================================================
    // Comparisons (produce I1 boolean result)
    // =========================================================================
    IR_SLT,             // Set on Less Than signed   (SLT, SLTI)
    IR_SLTU,            // Set on Less Than unsigned  (SLTU, SLTIU)
    IR_SLE,             // Set on Less Equal
    IR_SGT,             // Set on Greater Than
    IR_SGE,             // Set on Greater Equal
    IR_EQ,              // Equality compare           (for BEQ/BNE)
    IR_NE,              // Not-equal compare

    // =========================================================================
    // Conditional Move
    // =========================================================================
    IR_SELECT,          // Ternary select: (cond, true_val, false_val) → result
                        // Maps MOVZ (select if zero) and MOVN (select if nonzero)

    // =========================================================================
    // Multiply / Divide — R5900 writes to HI/LO register pairs
    // =========================================================================
    IR_MUL,             // Multiply (result to HI:LO)        — MULT, MULTU
    IR_DIV,             // Divide (quotient→LO, remainder→HI) — DIV, DIVU
    IR_MOD,             // Modulo (signed)
    IR_DIVU,            // Divide (unsigned)
    IR_MODU,            // Modulo (unsigned)
    IR_MADD,            // Multiply-Add to HI:LO              — MADD, MADDU (MMI)
    IR_MSUB,            // Multiply-Sub from HI:LO            — MSUB, MSUBU (MMI)

    // Pipeline 1 variants (PS2-specific: writes to HI1/LO1)
    IR_MUL1,            // MULT1, MULTU1
    IR_DIV1,            // DIV1, DIVU1
    IR_MADD1,           // MADD1, MADDU1
    // (MSUB1 not in standard R5900 ISA but reserved for completeness)

    // Extract from HI/LO (these become IR_REG_READ in practice, but
    // kept as explicit ops for clearer semantics during lowering)
    IR_MFHI,            // Move From HI  (MFHI)
    IR_MFLO,            // Move From LO  (MFLO)
    IR_MTHI,            // Move To HI    (MTHI)
    IR_MTLO,            // Move To LO    (MTLO)
    IR_MFHI1,           // Move From HI1 (PS2: MFHI1)
    IR_MFLO1,           // Move From LO1 (PS2: MFLO1)
    IR_MTHI1,           // Move To HI1   (PS2: MTHI1)
    IR_MTLO1,           // Move To LO1   (PS2: MTLO1)

    // SA register (PS2-specific shift amount register)
    IR_MFSA,            // Move From SA
    IR_MTSA,            // Move To SA
    IR_MTSAB,           // Move To SA Byte  (MTSAB)
    IR_MTSAH,           // Move To SA Halfword (MTSAH)

    // =========================================================================
    // Memory Operations
    // =========================================================================
    IR_LOAD,            // Memory load  (LB, LBU, LH, LHU, LW, LWU, LD, LQ)
    IR_STORE,           // Memory store (SB, SH, SW, SD, SQ)
    IR_LOAD_LEFT,       // Unaligned load left  (LWL, LDL)
    IR_LOAD_RIGHT,      // Unaligned load right (LWR, LDR)
    IR_STORE_LEFT,      // Unaligned store left  (SWL, SDL)
    IR_STORE_RIGHT,     // Unaligned store right (SWR, SDR)

    // Coprocessor memory (FPU/VU0)
    IR_LOAD_FPR,        // Load to FPR (LWC1)
    IR_STORE_FPR,       // Store from FPR (SWC1)
    IR_LOAD_VF,         // Load to VU0 VF reg (LQC2)
    IR_STORE_VF,        // Store from VU0 VF reg (SQC2)

    // Atomics / Sync (rarely used but must be handled)
    IR_SYNC,            // Memory barrier (SYNC)
    IR_CACHE,           // Cache operation (CACHE) — typically NOP in recomp
    IR_PREF,            // Prefetch (PREF) — typically NOP in recomp

    // =========================================================================
    // Control Flow
    // =========================================================================
    IR_BRANCH,          // Conditional branch: (cond, target_bb)
    IR_JUMP,            // Unconditional jump to known target: (target_bb)
    IR_JUMP_INDIRECT,   // Indirect jump via register: (addr_value)
                        // JR (non-$ra) fallbacks
    IR_SWITCH,          // Switch statement for jump tables.
                        // operands[0]: index register
                        // switchTargets: vector of target basic block indices
                        // switchValues: vector of match values
    IR_CALL,            // Function call: (target_addr, args...)
                        // JAL, JALR
    IR_RETURN,          // Function return: JR $ra
    IR_SYSCALL,         // System call (SYSCALL) — will map to HLE stubs
    IR_BREAK,           // Breakpoint (BREAK) — typically trap handler
    IR_TRAP,            // Conditional trap (TGE, TLT, TEQ, TNE, etc.)
    IR_ERET,            // Return from exception (ERET)

    IR_IF_LIKELY,       // Start of likely-branch delay slot conditional
    IR_END_LIKELY,      // End of likely-branch delay slot conditional

    // =========================================================================
    // Load Upper Immediate (special because it's a common pattern)
    // =========================================================================
    IR_LUI,             // Load Upper Immediate — kept separate for
                        // LUI+ORI/ADDIU constant reconstruction patterns

    // =========================================================================
    // Count Leading Zeros/Ones (PS2-specific via MMI)
    // =========================================================================
    IR_CLZ,             // Count Leading Zeros (PLZCW — counts per word)

    // =========================================================================
    // FPU — Single-Precision Float (COP1)
    // =========================================================================
    // PS2 FPU divergences from IEEE 754:
    //   - No denormals (flush to zero)
    //   - No NaN propagation (results clamped to ±MAX)
    //   - Rounding: always round-toward-zero (truncate)
    //   - No overflow exception (clamp to ±MAX instead)
    //   - RSQRT accuracy: ~23-bit mantissa (hardware approx)
    // The backend must emit ps2_fadd(), ps2_fmul() wrappers, NOT raw +/-.
    // =========================================================================
    IR_FADD,            // Float Add        (ADD.S)
    IR_FSUB,            // Float Subtract   (SUB.S)
    IR_FMUL,            // Float Multiply   (MUL.S)
    IR_FDIV,            // Float Divide     (DIV.S)
    IR_FSQRT,           // Float Sqrt       (SQRT.S)
    IR_FRSQRT,          // Float Recip Sqrt (RSQRT.S) — hardware approx
    IR_FABS,            // Float Abs        (ABS.S)
    IR_FNEG,            // Float Negate     (NEG.S)
    IR_FMOV,            // Float Move       (MOV.S)
    IR_FMIN,            // Float Min        (MIN.S) — PS2-specific
    IR_FMAX,            // Float Max        (MAX.S) — PS2-specific

    // FPU Accumulator operations (PS2-specific: ACC register)
    IR_FADDA,           // Add to Accumulator       (ADDA.S)
    IR_FSUBA,           // Subtract from Accumulator (SUBA.S)
    IR_FMULA,           // Multiply to Accumulator  (MULA.S)
    IR_FMADD,           // Multiply-Add             (MADD.S)
    IR_FMSUB,           // Multiply-Subtract        (MSUB.S)
    IR_FMADDA,          // Multiply-Add to Acc      (MADDA.S)
    IR_FMSUBA,          // Multiply-Sub from Acc    (MSUBA.S)

    // FPU Conversion
    IR_CVT_S_W,         // Convert Word → Float     (CVT.S.W)
    IR_CVT_W_S,         // Convert Float → Word     (CVT.W.S)
    IR_TRUNC_W_S,       // Truncate Float → Word    (TRUNC.W.S)
    IR_ROUND_W_S,       // Round Float → Word       (ROUND.W.S, but PS2 always truncates)
    IR_CEIL_W_S,        // Ceil Float → Word        (CEIL.W.S)
    IR_FLOOR_W_S,       // Floor Float → Word       (FLOOR.W.S)

    // FPU Comparison (sets FPU condition code → IR_FPU_CC)
    IR_FCMP_EQ,         // C.EQ.S  — Equal
    IR_FCMP_LT,         // C.LT.S  — Less Than
    IR_FCMP_LE,         // C.LE.S  — Less or Equal
    IR_FCMP_F,          // C.F.S   — Always False
    // (Unordered variants map to the same ops since PS2 has no NaN)

    // FPU Branches (use the FPU CC value from IR_FCMP_*)
    IR_FBRANCH_TRUE,    // BC1T  — Branch if FPU condition true
    IR_FBRANCH_FALSE,   // BC1F  — Branch if FPU condition false

    // FPU ↔ GPR Moves
    IR_MFC1,            // Move From COP1 (FPR → GPR)
    IR_MTC1,            // Move To COP1   (GPR → FPR)
    IR_CFC1,            // Move From COP1 Control
    IR_CTC1,            // Move To COP1 Control

    // =========================================================================
    // COP0 — System Control
    // =========================================================================
    IR_MFC0,            // Move From COP0
    IR_MTC0,            // Move To COP0
    IR_EI,              // Enable Interrupts
    IR_DI,              // Disable Interrupts
    IR_TLBR,            // TLB Read
    IR_TLBWI,           // TLB Write Indexed
    IR_TLBWR,           // TLB Write Random
    IR_TLBP,            // TLB Probe

    // =========================================================================
    // MMI — 128-bit Parallel SIMD Operations (PS2-specific, OPCODE 0x1C)
    // =========================================================================
    // These operate on 128-bit GPRs as SIMD lanes:
    //   - W: 4x 32-bit words
    //   - H: 8x 16-bit halfwords
    //   - B: 16x 8-bit bytes
    //
    // Backend maps these to SSE/AVX intrinsics.
    // =========================================================================

    // --- Parallel Add/Sub ---
    IR_PADDW,           // Parallel Add Word (4x i32)
    IR_PADDH,           // Parallel Add Halfword (8x i16)
    IR_PADDB,           // Parallel Add Byte (16x i8)
    IR_PSUBW,           // Parallel Sub Word
    IR_PSUBH,           // Parallel Sub Halfword
    IR_PSUBB,           // Parallel Sub Byte

    // --- Parallel Add/Sub Saturated ---
    IR_PADDSW,          // Parallel Add Signed Saturated Word
    IR_PADDSH,          // Parallel Add Signed Saturated Halfword
    IR_PADDSB,          // Parallel Add Signed Saturated Byte
    IR_PSUBSW,          // Parallel Sub Signed Saturated Word
    IR_PSUBSH,          // Parallel Sub Signed Saturated Halfword
    IR_PSUBSB,          // Parallel Sub Signed Saturated Byte

    // --- Parallel Add/Sub Unsigned ---
    IR_PADDUW,          // Parallel Add Unsigned Word
    IR_PADDUH,          // Parallel Add Unsigned Halfword
    IR_PADDUB,          // Parallel Add Unsigned Byte
    IR_PSUBUW,          // Parallel Sub Unsigned Word
    IR_PSUBUH,          // Parallel Sub Unsigned Halfword
    IR_PSUBUB,          // Parallel Sub Unsigned Byte

    // --- Parallel Compare ---
    IR_PCGTW,           // Parallel Compare Greater Than Word
    IR_PCGTH,           // Parallel Compare Greater Than Halfword
    IR_PCGTB,           // Parallel Compare Greater Than Byte
    IR_PCEQW,           // Parallel Compare Equal Word
    IR_PCEQH,           // Parallel Compare Equal Halfword
    IR_PCEQB,           // Parallel Compare Equal Byte

    // --- Parallel Min/Max ---
    IR_PMAXW,           // Parallel Max Word
    IR_PMAXH,           // Parallel Max Halfword
    IR_PMINW,           // Parallel Min Word
    IR_PMINH,           // Parallel Min Halfword

    // --- Parallel Absolute ---
    IR_PABSW,           // Parallel Absolute Word
    IR_PABSH,           // Parallel Absolute Halfword

    // --- Parallel Add/Sub Bytes-Halfwords ---
    IR_PADSBH,          // Parallel Add/Sub Bytes-Halfwords

    // --- Parallel Shift ---
    IR_PSLLW,           // Parallel Shift Left Logical Word
    IR_PSLLH,           // Parallel Shift Left Logical Halfword
    IR_PSRLW,           // Parallel Shift Right Logical Word
    IR_PSRLH,           // Parallel Shift Right Logical Halfword
    IR_PSRAW,           // Parallel Shift Right Arithmetic Word
    IR_PSRAH,           // Parallel Shift Right Arithmetic Halfword
    IR_PSLLVW,          // Parallel Shift Left Logical Variable Word
    IR_PSRLVW,          // Parallel Shift Right Logical Variable Word
    IR_PSRAVW,          // Parallel Shift Right Arithmetic Variable Word

    // --- Parallel Multiply ---
    IR_PMULTW,          // Parallel Multiply Word
    IR_PMULTUW,         // Parallel Multiply Unsigned Word
    IR_PMULTH,          // Parallel Multiply Halfword
    IR_PMADDW,          // Parallel Multiply-Add Word
    IR_PMADDUW,         // Parallel Multiply-Add Unsigned Word
    IR_PMADDH,          // Parallel Multiply-Add Halfword
    IR_PHMADH,          // Parallel Horizontal Multiply-Add Halfword
    IR_PMSUBW,          // Parallel Multiply-Sub Word
    IR_PMSUBH,          // Parallel Multiply-Sub Halfword
    IR_PHMSBH,          // Parallel Horizontal Multiply-Sub-Halfword
    IR_PDIVW,           // Parallel Divide Word
    IR_PDIVUW,          // Parallel Divide Unsigned Word
    IR_PDIVBW,          // Parallel Divide Broadcast Word

    // --- Parallel Logic ---
    IR_PAND,            // Parallel AND (128-bit)
    IR_POR,             // Parallel OR  (128-bit)
    IR_PXOR,            // Parallel XOR (128-bit)
    IR_PNOR,            // Parallel NOR (128-bit)

    // --- Parallel Extend / Pack / Copy ---
    IR_PEXTLW,          // Parallel Extend Lower Word
    IR_PEXTLH,          // Parallel Extend Lower Halfword
    IR_PEXTLB,          // Parallel Extend Lower Byte
    IR_PEXTUW,          // Parallel Extend Upper Word
    IR_PEXTUH,          // Parallel Extend Upper Halfword
    IR_PEXTUB,          // Parallel Extend Upper Byte
    IR_PPACW,           // Parallel Pack Word
    IR_PPACH,           // Parallel Pack Halfword
    IR_PPACB,           // Parallel Pack Byte
    IR_PEXT5,           // Parallel Extend 5-bit
    IR_PPAC5,           // Parallel Pack 5-bit

    // --- Parallel Interleave / Exchange / Copy ---
    IR_PINTH,           // Parallel Interleave Halfword
    IR_PINTEH,          // Parallel Interleave Even Halfword
    IR_PCPYLD,          // Parallel Copy Lower Doubleword
    IR_PCPYUD,          // Parallel Copy Upper Doubleword
    IR_PCPYH,           // Parallel Copy Halfword
    IR_PEXEH,           // Parallel Exchange Even Halfword
    IR_PEXEW,           // Parallel Exchange Even Word
    IR_PEXCH,           // Parallel Exchange Center Halfword
    IR_PEXCW,           // Parallel Exchange Center Word
    IR_PREVH,           // Parallel Reverse Halfword
    IR_PROT3W,          // Parallel Rotate 3 Words
    IR_PLZCW,           // Parallel Leading Zero Count Word

    // --- Parallel HI/LO ---
    IR_PMFHI,           // Parallel Move From HI
    IR_PMFLO,           // Parallel Move From LO
    IR_PMTHI,           // Parallel Move To HI
    IR_PMTLO,           // Parallel Move To LO
    IR_PMFHL,           // Parallel Move From HI/LO (with sub-function)
    IR_PMTHL,           // Parallel Move To HI/LO (with sub-function)

    // --- Quadword Funnel Shift ---
    IR_QFSRV,          // Quadword Funnel Shift Right Variable

    // =========================================================================
    // VU0 Macro Mode — Vector Float Operations (COP2)
    // =========================================================================
    // VU0 macro instructions execute on the VU0 coprocessor via COP2 encoding.
    // They operate on VF registers (128-bit = 4x float32) with field masking
    // (xyzw destination mask). The field mask is stored in IRInst::vuDestMask.
    //
    // Broadcast variants (VADDx, VADDy, etc.) are unified into IR_VU_ADD
    // with the broadcast field stored in IRInst::vuBroadcast.
    // =========================================================================

    // --- VU0 Arithmetic ---
    IR_VU_ADD,          // Vector Add (VADD, VADDx/y/z/w, VADDq, VADDi)
    IR_VU_SUB,          // Vector Sub (VSUB, VSUBx/y/z/w, VSUBq, VSUBi)
    IR_VU_MUL,          // Vector Mul (VMUL, VMULx/y/z/w, VMULq, VMULi)
    IR_VU_MADD,         // Vector Multiply-Add (VMADD + variants)
    IR_VU_MSUB,         // Vector Multiply-Sub (VMSUB + variants)
    IR_VU_MAX,          // Vector Max (VMAX, VMAXx/y/z/w, VMAXi)
    IR_VU_MINI,         // Vector Min (VMINI, VMINIx/y/z/w, VMINIi)
    IR_VU_ABS,          // Vector Absolute (VABS)
    IR_VU_OPMULA,       // Vector Outer Product Mul-Acc (VOPMULA)
    IR_VU_OPMSUB,       // Vector Outer Product Mul-Sub (VOPMSUB)

    // --- VU0 Accumulator ---
    IR_VU_ADDA,         // Vector Add to Acc (VADDA + variants)
    IR_VU_SUBA,         // Vector Sub from Acc (VSUBA + variants)
    IR_VU_MULA,         // Vector Mul to Acc (VMULA + variants)
    IR_VU_MADDA,        // Vector Mul-Add to Acc (VMADDA + variants)
    IR_VU_MSUBA,        // Vector Mul-Sub from Acc (VMSUBA + variants)

    // --- VU0 Conversion ---
    IR_VU_ITOF0,        // Int → Float (no shift)
    IR_VU_ITOF4,        // Int → Float (>> 4)
    IR_VU_ITOF12,       // Int → Float (>> 12)
    IR_VU_ITOF15,       // Int → Float (>> 15)
    IR_VU_FTOI0,        // Float → Int (no shift)
    IR_VU_FTOI4,        // Float → Int (<< 4)
    IR_VU_FTOI12,       // Float → Int (<< 12)
    IR_VU_FTOI15,       // Float → Int (<< 15)

    // --- VU0 Division Unit ---
    IR_VU_DIV,          // Vector Divide (VDIV) → Q reg
    IR_VU_SQRT,         // Vector Sqrt (VSQRT) → Q reg
    IR_VU_RSQRT,        // Vector Reciprocal Sqrt (VRSQRT) → Q reg
    IR_VU_WAITQ,        // Wait for Q register (VWAITQ)

    // --- VU0 Register Move ---
    IR_VU_MOVE,         // Vector Move (VMOVE)
    IR_VU_MR32,         // Vector Move Rotate32 (VMR32)
    IR_VU_CLIP,         // Clipping test (VCLIPw)

    // --- VU0 Integer ---
    IR_VU_IADD,         // Integer Add (VIADD)
    IR_VU_ISUB,         // Integer Sub (VISUB)
    IR_VU_IADDI,        // Integer Add Immediate (VIADDI)
    IR_VU_IAND,         // Integer AND (VIAND)
    IR_VU_IOR,          // Integer OR  (VIOR)

    // --- VU0 Load/Store (from VU data memory) ---
    IR_VU_LQI,          // Load Quad Increment (VLQI)
    IR_VU_SQI,          // Store Quad Increment (VSQI)
    IR_VU_LQD,          // Load Quad Decrement (VLQD)
    IR_VU_SQD,          // Store Quad Decrement (VSQD)
    IR_VU_ILWR,         // Integer Load Word (VILWR)
    IR_VU_ISWR,         // Integer Store Word (VISWR)

    // --- VU0 ↔ EE Register Moves ---
    IR_VU_QMFC2,        // Move Quadword from COP2 (VF → GPR)
    IR_VU_QMTC2,        // Move Quadword to COP2 (GPR → VF)
    IR_VU_CFC2,         // Move from COP2 Control
    IR_VU_CTC2,         // Move to COP2 Control
    IR_VU_MFIR,         // Move From VU Integer Register
    IR_VU_MTIR,         // Move To VU Integer Register

    // --- VU0 Random Number ---
    IR_VU_RNEXT,        // Next Random Number (VRNEXT) → R reg
    IR_VU_RGET,         // Get Random (VRGET) → VF
    IR_VU_RINIT,        // Init Random (VRINIT)
    IR_VU_RXOR,         // XOR Random (VRXOR)

    // --- VU0 Microprogram Execution ---
    IR_VU_CALLMS,       // Call VU0 Microprogram (VCALLMS)
    IR_VU_CALLMSR,      // Call VU0 Microprogram via Register (VCALLMSR)

    // --- VU0 Branches ---
    IR_VU_BRANCH_TRUE,  // BC2T  — Branch if VU0 condition true
    IR_VU_BRANCH_FALSE, // BC2F  — Branch if VU0 condition false

    // =========================================================================
    // Sentinel — must be last
    // =========================================================================
    IR_OP_COUNT
};

/// Returns a human-readable name for an IROp
inline const char* irOpName(IROp op) {
    switch (op) {
        case IROp::IR_NOP:          return "NOP";
        case IROp::IR_PHI:          return "PHI";
        case IROp::IR_LABEL:        return "LABEL";
        case IROp::IR_COMMENT:      return "COMMENT";
        case IROp::IR_CONST:        return "CONST";
        case IROp::IR_UNDEF:        return "UNDEF";
        case IROp::IR_REG_READ:     return "REG_READ";
        case IROp::IR_REG_WRITE:    return "REG_WRITE";
        case IROp::IR_ADD:          return "ADD";
        case IROp::IR_SUB:          return "SUB";
        case IROp::IR_NEG:          return "NEG";
        case IROp::IR_SEXT:         return "SEXT";
        case IROp::IR_ZEXT:         return "ZEXT";
        case IROp::IR_TRUNC:        return "TRUNC";
        case IROp::IR_BITCAST:      return "BITCAST";
        case IROp::IR_AND:          return "AND";
        case IROp::IR_OR:           return "OR";
        case IROp::IR_XOR:          return "XOR";
        case IROp::IR_NOR:          return "NOR";
        case IROp::IR_NOT:          return "NOT";
        case IROp::IR_SLL:          return "SLL";
        case IROp::IR_SRL:          return "SRL";
        case IROp::IR_SRA:          return "SRA";
        case IROp::IR_SHL:          return "SHL";
        case IROp::IR_LSHR:         return "LSHR";
        case IROp::IR_ASHR:         return "ASHR";
        case IROp::IR_SLT:          return "SLT";
        case IROp::IR_SLTU:         return "SLTU";
        case IROp::IR_SLE:          return "SLE";
        case IROp::IR_SGT:          return "SGT";
        case IROp::IR_SGE:          return "SGE";
        case IROp::IR_EQ:           return "EQ";
        case IROp::IR_NE:           return "NE";
        case IROp::IR_SELECT:       return "SELECT";
        case IROp::IR_MUL:          return "MUL";
        case IROp::IR_DIV:          return "DIV";
        case IROp::IR_MOD:          return "MOD";
        case IROp::IR_DIVU:         return "DIVU";
        case IROp::IR_MODU:         return "MODU";
        case IROp::IR_MADD:         return "MADD";
        case IROp::IR_MSUB:         return "MSUB";
        case IROp::IR_MUL1:         return "MUL1";
        case IROp::IR_DIV1:         return "DIV1";
        case IROp::IR_MADD1:        return "MADD1";
        case IROp::IR_MFHI:         return "MFHI";
        case IROp::IR_MFLO:         return "MFLO";
        case IROp::IR_MTHI:         return "MTHI";
        case IROp::IR_MTLO:         return "MTLO";
        case IROp::IR_MFHI1:        return "MFHI1";
        case IROp::IR_MFLO1:        return "MFLO1";
        case IROp::IR_MTHI1:        return "MTHI1";
        case IROp::IR_MTLO1:        return "MTLO1";
        case IROp::IR_MFSA:         return "MFSA";
        case IROp::IR_MTSA:         return "MTSA";
        case IROp::IR_MTSAB:        return "MTSAB";
        case IROp::IR_MTSAH:        return "MTSAH";
        case IROp::IR_LOAD:         return "LOAD";
        case IROp::IR_STORE:        return "STORE";
        case IROp::IR_LOAD_LEFT:    return "LOAD_LEFT";
        case IROp::IR_LOAD_RIGHT:   return "LOAD_RIGHT";
        case IROp::IR_STORE_LEFT:   return "STORE_LEFT";
        case IROp::IR_STORE_RIGHT:  return "STORE_RIGHT";
        case IROp::IR_LOAD_FPR:     return "LOAD_FPR";
        case IROp::IR_STORE_FPR:    return "STORE_FPR";
        case IROp::IR_LOAD_VF:      return "LOAD_VF";
        case IROp::IR_STORE_VF:     return "STORE_VF";
        case IROp::IR_SYNC:         return "SYNC";
        case IROp::IR_CACHE:        return "CACHE";
        case IROp::IR_PREF:         return "PREF";
        case IROp::IR_BRANCH:       return "BRANCH";
        case IROp::IR_JUMP:         return "JUMP";
        case IROp::IR_JUMP_INDIRECT: return "JUMP_INDIRECT";
        case IROp::IR_SWITCH:       return "SWITCH";
        case IROp::IR_CALL:         return "CALL";
        case IROp::IR_RETURN:       return "RETURN";
        case IROp::IR_SYSCALL:      return "SYSCALL";
        case IROp::IR_BREAK:        return "BREAK";
        case IROp::IR_TRAP:         return "TRAP";
        case IROp::IR_ERET:         return "ERET";
        case IROp::IR_IF_LIKELY:    return "IF_LIKELY";
        case IROp::IR_END_LIKELY:   return "END_LIKELY";
        case IROp::IR_LUI:          return "LUI";
        case IROp::IR_CLZ:          return "CLZ";
        case IROp::IR_FADD:         return "FADD";
        case IROp::IR_FSUB:         return "FSUB";
        case IROp::IR_FMUL:         return "FMUL";
        case IROp::IR_FDIV:         return "FDIV";
        case IROp::IR_FSQRT:        return "FSQRT";
        case IROp::IR_FRSQRT:       return "FRSQRT";
        case IROp::IR_FABS:         return "FABS";
        case IROp::IR_FNEG:         return "FNEG";
        case IROp::IR_FMOV:         return "FMOV";
        case IROp::IR_FMIN:         return "FMIN";
        case IROp::IR_FMAX:         return "FMAX";
        case IROp::IR_FADDA:        return "FADDA";
        case IROp::IR_FSUBA:        return "FSUBA";
        case IROp::IR_FMULA:        return "FMULA";
        case IROp::IR_FMADD:        return "FMADD";
        case IROp::IR_FMSUB:        return "FMSUB";
        case IROp::IR_FMADDA:       return "FMADDA";
        case IROp::IR_FMSUBA:       return "FMSUBA";
        case IROp::IR_CVT_S_W:      return "CVT_S_W";
        case IROp::IR_CVT_W_S:      return "CVT_W_S";
        case IROp::IR_TRUNC_W_S:    return "TRUNC_W_S";
        case IROp::IR_ROUND_W_S:    return "ROUND_W_S";
        case IROp::IR_CEIL_W_S:     return "CEIL_W_S";
        case IROp::IR_FLOOR_W_S:    return "FLOOR_W_S";
        case IROp::IR_FCMP_EQ:      return "FCMP_EQ";
        case IROp::IR_FCMP_LT:      return "FCMP_LT";
        case IROp::IR_FCMP_LE:      return "FCMP_LE";
        case IROp::IR_FCMP_F:       return "FCMP_F";
        case IROp::IR_FBRANCH_TRUE: return "FBRANCH_TRUE";
        case IROp::IR_FBRANCH_FALSE: return "FBRANCH_FALSE";
        case IROp::IR_MFC1:         return "MFC1";
        case IROp::IR_MTC1:         return "MTC1";
        case IROp::IR_CFC1:         return "CFC1";
        case IROp::IR_CTC1:         return "CTC1";
        case IROp::IR_MFC0:         return "MFC0";
        case IROp::IR_MTC0:         return "MTC0";
        case IROp::IR_EI:           return "EI";
        case IROp::IR_DI:           return "DI";
        case IROp::IR_TLBR:         return "TLBR";
        case IROp::IR_TLBWI:        return "TLBWI";
        case IROp::IR_TLBWR:        return "TLBWR";
        case IROp::IR_TLBP:         return "TLBP";
        case IROp::IR_PADDW:        return "PADDW";
        case IROp::IR_PADDH:        return "PADDH";
        case IROp::IR_PADDB:        return "PADDB";
        case IROp::IR_PSUBW:        return "PSUBW";
        case IROp::IR_PSUBH:        return "PSUBH";
        case IROp::IR_PSUBB:        return "PSUBB";
        case IROp::IR_PADDSW:       return "PADDSW";
        case IROp::IR_PADDSH:       return "PADDSH";
        case IROp::IR_PADDSB:       return "PADDSB";
        case IROp::IR_PSUBSW:       return "PSUBSW";
        case IROp::IR_PSUBSH:       return "PSUBSH";
        case IROp::IR_PSUBSB:       return "PSUBSB";
        case IROp::IR_PADDUW:       return "PADDUW";
        case IROp::IR_PADDUH:       return "PADDUH";
        case IROp::IR_PADDUB:       return "PADDUB";
        case IROp::IR_PSUBUW:       return "PSUBUW";
        case IROp::IR_PSUBUH:       return "PSUBUH";
        case IROp::IR_PSUBUB:       return "PSUBUB";
        case IROp::IR_PCGTW:        return "PCGTW";
        case IROp::IR_PCGTH:        return "PCGTH";
        case IROp::IR_PCGTB:        return "PCGTB";
        case IROp::IR_PCEQW:        return "PCEQW";
        case IROp::IR_PCEQH:        return "PCEQH";
        case IROp::IR_PCEQB:        return "PCEQB";
        case IROp::IR_PMAXW:        return "PMAXW";
        case IROp::IR_PMAXH:        return "PMAXH";
        case IROp::IR_PMINW:        return "PMINW";
        case IROp::IR_PMINH:        return "PMINH";
        case IROp::IR_PABSW:        return "PABSW";
        case IROp::IR_PABSH:        return "PABSH";
        case IROp::IR_PADSBH:       return "PADSBH";
        case IROp::IR_PSLLW:        return "PSLLW";
        case IROp::IR_PSLLH:        return "PSLLH";
        case IROp::IR_PSRLW:        return "PSRLW";
        case IROp::IR_PSRLH:        return "PSRLH";
        case IROp::IR_PSRAW:        return "PSRAW";
        case IROp::IR_PSRAH:        return "PSRAH";
        case IROp::IR_PSLLVW:       return "PSLLVW";
        case IROp::IR_PSRLVW:       return "PSRLVW";
        case IROp::IR_PSRAVW:       return "PSRAVW";
        case IROp::IR_PMULTW:       return "PMULTW";
        case IROp::IR_PMULTUW:      return "PMULTUW";
        case IROp::IR_PMULTH:       return "PMULTH";
        case IROp::IR_PMADDW:       return "PMADDW";
        case IROp::IR_PMADDUW:      return "PMADDUW";
        case IROp::IR_PMADDH:       return "PMADDH";
        case IROp::IR_PHMADH:       return "PHMADH";
        case IROp::IR_PMSUBW:       return "PMSUBW";
        case IROp::IR_PMSUBH:       return "PMSUBH";
        case IROp::IR_PHMSBH:       return "PHMSBH";
        case IROp::IR_PDIVW:        return "PDIVW";
        case IROp::IR_PDIVUW:       return "PDIVUW";
        case IROp::IR_PDIVBW:       return "PDIVBW";
        case IROp::IR_PAND:         return "PAND";
        case IROp::IR_POR:          return "POR";
        case IROp::IR_PXOR:         return "PXOR";
        case IROp::IR_PNOR:         return "PNOR";
        case IROp::IR_PEXTLW:       return "PEXTLW";
        case IROp::IR_PEXTLH:       return "PEXTLH";
        case IROp::IR_PEXTLB:       return "PEXTLB";
        case IROp::IR_PEXTUW:       return "PEXTUW";
        case IROp::IR_PEXTUH:       return "PEXTUH";
        case IROp::IR_PEXTUB:       return "PEXTUB";
        case IROp::IR_PPACW:        return "PPACW";
        case IROp::IR_PPACH:        return "PPACH";
        case IROp::IR_PPACB:        return "PPACB";
        case IROp::IR_PEXT5:        return "PEXT5";
        case IROp::IR_PPAC5:        return "PPAC5";
        case IROp::IR_PINTH:        return "PINTH";
        case IROp::IR_PINTEH:       return "PINTEH";
        case IROp::IR_PCPYLD:       return "PCPYLD";
        case IROp::IR_PCPYUD:       return "PCPYUD";
        case IROp::IR_PCPYH:        return "PCPYH";
        case IROp::IR_PEXEH:        return "PEXEH";
        case IROp::IR_PEXEW:        return "PEXEW";
        case IROp::IR_PEXCH:        return "PEXCH";
        case IROp::IR_PEXCW:        return "PEXCW";
        case IROp::IR_PREVH:        return "PREVH";
        case IROp::IR_PROT3W:       return "PROT3W";
        case IROp::IR_PLZCW:        return "PLZCW";
        case IROp::IR_PMFHI:        return "PMFHI";
        case IROp::IR_PMFLO:        return "PMFLO";
        case IROp::IR_PMTHI:        return "PMTHI";
        case IROp::IR_PMTLO:        return "PMTLO";
        case IROp::IR_PMFHL:        return "PMFHL";
        case IROp::IR_PMTHL:        return "PMTHL";
        case IROp::IR_QFSRV:        return "QFSRV";
        case IROp::IR_VU_ADD:        return "VU_ADD";
        case IROp::IR_VU_SUB:        return "VU_SUB";
        case IROp::IR_VU_MUL:        return "VU_MUL";
        case IROp::IR_VU_MADD:       return "VU_MADD";
        case IROp::IR_VU_MSUB:       return "VU_MSUB";
        case IROp::IR_VU_MAX:        return "VU_MAX";
        case IROp::IR_VU_MINI:       return "VU_MINI";
        case IROp::IR_VU_ABS:        return "VU_ABS";
        case IROp::IR_VU_OPMULA:     return "VU_OPMULA";
        case IROp::IR_VU_OPMSUB:     return "VU_OPMSUB";
        case IROp::IR_VU_ADDA:       return "VU_ADDA";
        case IROp::IR_VU_SUBA:       return "VU_SUBA";
        case IROp::IR_VU_MULA:       return "VU_MULA";
        case IROp::IR_VU_MADDA:      return "VU_MADDA";
        case IROp::IR_VU_MSUBA:      return "VU_MSUBA";
        case IROp::IR_VU_ITOF0:      return "VU_ITOF0";
        case IROp::IR_VU_ITOF4:      return "VU_ITOF4";
        case IROp::IR_VU_ITOF12:     return "VU_ITOF12";
        case IROp::IR_VU_ITOF15:     return "VU_ITOF15";
        case IROp::IR_VU_FTOI0:      return "VU_FTOI0";
        case IROp::IR_VU_FTOI4:      return "VU_FTOI4";
        case IROp::IR_VU_FTOI12:     return "VU_FTOI12";
        case IROp::IR_VU_FTOI15:     return "VU_FTOI15";
        case IROp::IR_VU_DIV:        return "VU_DIV";
        case IROp::IR_VU_SQRT:       return "VU_SQRT";
        case IROp::IR_VU_RSQRT:      return "VU_RSQRT";
        case IROp::IR_VU_WAITQ:      return "VU_WAITQ";
        case IROp::IR_VU_MOVE:       return "VU_MOVE";
        case IROp::IR_VU_MR32:       return "VU_MR32";
        case IROp::IR_VU_CLIP:       return "VU_CLIP";
        case IROp::IR_VU_IADD:       return "VU_IADD";
        case IROp::IR_VU_ISUB:       return "VU_ISUB";
        case IROp::IR_VU_IADDI:      return "VU_IADDI";
        case IROp::IR_VU_IAND:       return "VU_IAND";
        case IROp::IR_VU_IOR:        return "VU_IOR";
        case IROp::IR_VU_LQI:        return "VU_LQI";
        case IROp::IR_VU_SQI:        return "VU_SQI";
        case IROp::IR_VU_LQD:        return "VU_LQD";
        case IROp::IR_VU_SQD:        return "VU_SQD";
        case IROp::IR_VU_ILWR:       return "VU_ILWR";
        case IROp::IR_VU_ISWR:       return "VU_ISWR";
        case IROp::IR_VU_QMFC2:      return "VU_QMFC2";
        case IROp::IR_VU_QMTC2:      return "VU_QMTC2";
        case IROp::IR_VU_CFC2:       return "VU_CFC2";
        case IROp::IR_VU_CTC2:       return "VU_CTC2";
        case IROp::IR_VU_MFIR:       return "VU_MFIR";
        case IROp::IR_VU_MTIR:       return "VU_MTIR";
        case IROp::IR_VU_RNEXT:      return "VU_RNEXT";
        case IROp::IR_VU_RGET:       return "VU_RGET";
        case IROp::IR_VU_RINIT:      return "VU_RINIT";
        case IROp::IR_VU_RXOR:       return "VU_RXOR";
        case IROp::IR_VU_CALLMS:     return "VU_CALLMS";
        case IROp::IR_VU_CALLMSR:    return "VU_CALLMSR";
        case IROp::IR_VU_BRANCH_TRUE:  return "VU_BRANCH_TRUE";
        case IROp::IR_VU_BRANCH_FALSE: return "VU_BRANCH_FALSE";
        case IROp::IR_OP_COUNT:        return "OP_COUNT";
    }
    return "UNKNOWN";
}

// =============================================================================
// VU0 Broadcast / Destination Field
// =============================================================================
// VU0 instructions have a 4-bit destination mask (xyzw) and some instructions
// broadcast a single component of FT across all lanes.
// =============================================================================
enum class VUBroadcast : uint8_t {
    None = 0,   // No broadcast (use full VF register)
    X,          // Broadcast .x component
    Y,          // Broadcast .y component
    Z,          // Broadcast .z component
    W,          // Broadcast .w component
    Q,          // Broadcast Q register
    I,          // Broadcast I register (immediate)
};

// =============================================================================
// IRValue — SSA Value
// =============================================================================
// Every computed result in the IR is an IRValue with a unique ValueId.
// Values are defined exactly once (SSA property) and can be used by
// multiple instructions.
// =============================================================================
struct IRValue {
    ValueId   id;       // Unique SSA identifier
    IRType    type;      // Width/format of this value
    IRInst*   def;       // Instruction that defines this value (null for params)

    IRValue() : id(INVALID_VALUE_ID), type(IRType::Void), def(nullptr) {}
    IRValue(ValueId vid, IRType vtype) : id(vid), type(vtype), def(nullptr) {}

    bool isValid() const { return id != INVALID_VALUE_ID; }
};

// =============================================================================
// IRInst — IR Instruction
// =============================================================================
// An IR instruction consists of an opcode, an optional result value, and
// a list of operand value IDs. Additional fields carry MIPS-specific
// metadata (source address, register info, VU field masks).
// =============================================================================
struct IRInst {
    IROp                    op;             // Semantic opcode
    IRValue                 result;         // SSA value produced (Void if none)
    std::vector<ValueId>    operands;       // SSA value IDs consumed

    // --- Source tracking ---
    uint32_t                srcAddress;     // Original MIPS PC (for debug/comments)
    uint32_t                srcRaw;         // Raw MIPS instruction word

    // --- Register metadata (for IR_REG_READ / IR_REG_WRITE) ---
    IRReg                   reg;            // Which physical register

    // --- Constant data (for IR_CONST) ---
    union {
        int64_t             immSigned;      // Signed constant
        uint64_t            immUnsigned;    // Unsigned constant
        float               immFloat;       // Float constant
    } constData;

    // --- VU0-specific metadata ---
    uint8_t                 vuDestMask;     // xyzw destination mask (4 bits)
    VUBroadcast             vuBroadcast;    // Broadcast mode for VU ops
    uint8_t                 vuFSF;          // FS field select (2 bits)
    uint8_t                 vuFTF;          // FT field select (2 bits)

    // --- Memory metadata (for IR_LOAD / IR_STORE) ---
    IRType                  memType;        // Width of memory access
    bool                    memSigned;      // Sign-extend on load?

    // --- Branch metadata ---
    uint32_t                branchTarget;   // Target BB index or MIPS address
    bool                    branchLikely;   // MIPS "likely" branch (annul delay slot if not taken)

    // --- Switch metadata (for IR_SWITCH) ---
    std::vector<uint32_t>   switchTargets;  // Target BB indices for switch cases
    std::vector<uint32_t>   switchValues;   // Match values for each switch case

    // --- PHI metadata ---
    // For IR_PHI: operands[2*i] = value from predecessor, operands[2*i+1] is unused
    // phiPredecessors[i] = index of predecessor basic block
    std::vector<uint32_t>   phiPredecessors;

    // --- Debug annotation ---
    std::string             comment;        // Human-readable note

    IRInst()
        : op(IROp::IR_NOP)
        , srcAddress(0)
        , srcRaw(0)
        , vuDestMask(0xF)      // Default: write all xyzw
        , vuBroadcast(VUBroadcast::None)
        , vuFSF(0)
        , vuFTF(0)
        , memType(IRType::Void)
        , memSigned(false)
        , branchTarget(0)
        , branchLikely(false)
    {
        constData.immUnsigned = 0;
    }

    bool hasResult() const { return result.type != IRType::Void; }
    bool isTerminator() const {
        return op == IROp::IR_BRANCH || op == IROp::IR_JUMP ||
               op == IROp::IR_JUMP_INDIRECT || op == IROp::IR_RETURN ||
               op == IROp::IR_CALL || op == IROp::IR_SYSCALL ||
               op == IROp::IR_ERET || op == IROp::IR_FBRANCH_TRUE ||
               op == IROp::IR_FBRANCH_FALSE || op == IROp::IR_VU_BRANCH_TRUE ||
               op == IROp::IR_VU_BRANCH_FALSE;
    }
    bool isPhi() const { return op == IROp::IR_PHI; }
    bool isBranch() const {
        return op == IROp::IR_BRANCH || op == IROp::IR_FBRANCH_TRUE ||
               op == IROp::IR_FBRANCH_FALSE || op == IROp::IR_VU_BRANCH_TRUE ||
               op == IROp::IR_VU_BRANCH_FALSE;
    }
};

// =============================================================================
// IRBasicBlock — A sequence of IR instructions with single entry, single exit
// =============================================================================
// Basic blocks end with exactly one terminator instruction (branch, jump,
// return). PHI nodes, if any, must be at the beginning of the block.
// =============================================================================
struct IRBasicBlock {
    uint32_t                index;           // Block index within the function
    std::string             label;           // Human-readable label (e.g., "bb_0x001000")
    uint32_t                mipsStartAddr;   // First MIPS address in this block
    uint32_t                mipsEndAddr;     // Last MIPS address in this block

    std::vector<IRInst>     instructions;    // IR instructions in order
    std::vector<uint32_t>   predecessors;    // Indices of predecessor blocks
    std::vector<uint32_t>   successors;      // Indices of successor blocks

    IRBasicBlock() : index(0), mipsStartAddr(0), mipsEndAddr(0) {}

    bool empty() const { return instructions.empty(); }

    /// Get the terminator instruction (last instruction, must be a terminator)
    const IRInst* terminator() const {
        if (instructions.empty()) return nullptr;
        const auto& last = instructions.back();
        return last.isTerminator() ? &last : nullptr;
    }

    /// Check if this block has any PHI nodes
    bool hasPhiNodes() const {
        return !instructions.empty() && instructions.front().isPhi();
    }
};

// =============================================================================
// IRFunction — Complete IR representation of a recompiled function
// =============================================================================
struct IRFunction {
    std::string                     name;           // Function name
    uint32_t                        mipsEntryAddr;  // Original MIPS entry point
    uint32_t                        mipsEndAddr;    // Original MIPS end address

    std::vector<IRBasicBlock>       blocks;         // Basic blocks in CFG order
    ValueId                         nextValueId;    // Counter for SSA value IDs

    IRFunction() : mipsEntryAddr(0), mipsEndAddr(0), nextValueId(0) {}

    /// Allocate a new SSA value ID
    ValueId allocValue() { return nextValueId++; }

    /// Allocate a new SSA value with a type
    IRValue allocTypedValue(IRType type) {
        return IRValue(allocValue(), type);
    }

    /// Add a new basic block and return a reference to it
    IRBasicBlock& addBlock(const std::string& blockLabel = "") {
        IRBasicBlock bb;
        bb.index = static_cast<uint32_t>(blocks.size());
        bb.label = blockLabel.empty()
            ? ("bb_" + std::to_string(bb.index))
            : blockLabel;
        blocks.push_back(std::move(bb));
        return blocks.back();
    }

    /// Find a block by index (bounds-checked)
    IRBasicBlock* getBlock(uint32_t idx) {
        return idx < blocks.size() ? &blocks[idx] : nullptr;
    }
    const IRBasicBlock* getBlock(uint32_t idx) const {
        return idx < blocks.size() ? &blocks[idx] : nullptr;
    }

    /// Get total instruction count across all blocks
    size_t totalInstructions() const {
        size_t count = 0;
        for (const auto& bb : blocks) count += bb.instructions.size();
        return count;
    }
};

// =============================================================================
// Builder Helpers — Convenience functions for constructing IR
// =============================================================================

/// Create a constant integer IR instruction
inline IRInst makeConst(IRFunction& func, IRType type, int64_t value) {
    IRInst inst;
    inst.op = IROp::IR_CONST;
    inst.result = func.allocTypedValue(type);
    inst.constData.immSigned = value;
    return inst;
}

/// Create a constant unsigned IR instruction
inline IRInst makeConstU(IRFunction& func, IRType type, uint64_t value) {
    IRInst inst;
    inst.op = IROp::IR_CONST;
    inst.result = func.allocTypedValue(type);
    inst.constData.immUnsigned = value;
    return inst;
}

/// Create a constant float IR instruction
inline IRInst makeConstF(IRFunction& func, float value) {
    IRInst inst;
    inst.op = IROp::IR_CONST;
    inst.result = func.allocTypedValue(IRType::F32);
    inst.constData.immFloat = value;
    return inst;
}

/// Create a register read IR instruction
inline IRInst makeRegRead(IRFunction& func, IRType type, IRReg reg) {
    IRInst inst;
    inst.op = IROp::IR_REG_READ;
    inst.result = func.allocTypedValue(type);
    inst.reg = reg;
    return inst;
}

/// Create a register write IR instruction
inline IRInst makeRegWrite(IRReg reg, ValueId value) {
    IRInst inst;
    inst.op = IROp::IR_REG_WRITE;
    inst.reg = reg;
    inst.operands.push_back(value);
    return inst;
}

/// Create a binary arithmetic/logic IR instruction
inline IRInst makeBinaryOp(IRFunction& func, IROp op, IRType resultType,
                           ValueId lhs, ValueId rhs, uint32_t srcAddr = 0) {
    IRInst inst;
    inst.op = op;
    inst.result = func.allocTypedValue(resultType);
    inst.operands = {lhs, rhs};
    inst.srcAddress = srcAddr;
    return inst;
}

/// Create a unary IR instruction
inline IRInst makeUnaryOp(IRFunction& func, IROp op, IRType resultType,
                          ValueId operand, uint32_t srcAddr = 0) {
    IRInst inst;
    inst.op = op;
    inst.result = func.allocTypedValue(resultType);
    inst.operands = {operand};
    inst.srcAddress = srcAddr;
    return inst;
}

/// Create a memory load IR instruction
inline IRInst makeLoad(IRFunction& func, IRType loadType, ValueId addr,
                       bool signExtend, uint32_t srcAddr = 0) {
    IRInst inst;
    inst.op = IROp::IR_LOAD;
    inst.result = func.allocTypedValue(loadType);
    inst.operands = {addr};
    inst.memType = loadType;
    inst.memSigned = signExtend;
    inst.srcAddress = srcAddr;
    return inst;
}

/// Create a memory store IR instruction
inline IRInst makeStore(IRType storeType, ValueId addr, ValueId value,
                        uint32_t srcAddr = 0) {
    IRInst inst;
    inst.op = IROp::IR_STORE;
    inst.operands = {addr, value};
    inst.memType = storeType;
    inst.srcAddress = srcAddr;
    return inst;
}

/// Create a conditional branch IR instruction
inline IRInst makeBranch(ValueId condition, uint32_t targetBlock,
                         bool likely = false, uint32_t srcAddr = 0) {
    IRInst inst;
    inst.op = IROp::IR_BRANCH;
    inst.operands = {condition};
    inst.branchTarget = targetBlock;
    inst.branchLikely = likely;
    inst.srcAddress = srcAddr;
    return inst;
}

/// Create an unconditional jump IR instruction
inline IRInst makeJump(uint32_t targetBlock, uint32_t srcAddr = 0) {
    IRInst inst;
    inst.op = IROp::IR_JUMP;
    inst.branchTarget = targetBlock;
    inst.srcAddress = srcAddr;
    return inst;
}

/// Create a return IR instruction
inline IRInst makeReturn(uint32_t srcAddr = 0) {
    IRInst inst;
    inst.op = IROp::IR_RETURN;
    inst.srcAddress = srcAddr;
    return inst;
}

/// Create a function call IR instruction
inline IRInst makeCall(IRFunction& func, uint32_t targetAddr,
                       uint32_t srcAddr = 0) {
    IRInst inst;
    inst.op = IROp::IR_CALL;
    inst.branchTarget = targetAddr;
    inst.srcAddress = srcAddr;
    // Return value is I64 (v0:v1 pair convention, but typically I32 widened)
    inst.result = func.allocTypedValue(IRType::Void);
    return inst;
}

/// Create a PHI node
inline IRInst makePhi(IRFunction& func, IRType type) {
    IRInst inst;
    inst.op = IROp::IR_PHI;
    inst.result = func.allocTypedValue(type);
    return inst;
}

/// Add an incoming edge to a PHI node
inline void phiAddIncoming(IRInst& phi, ValueId value, uint32_t predBlock) {
    assert(phi.op == IROp::IR_PHI);
    phi.operands.push_back(value);
    phi.phiPredecessors.push_back(predBlock);
}

} // namespace ir
} // namespace ps2recomp

#endif // PS2RECOMP_IR_H
