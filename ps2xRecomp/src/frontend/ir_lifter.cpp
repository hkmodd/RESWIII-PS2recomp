// ============================================================================
// ir_lifter.cpp — MIPS R5900 → IR Lifter (Sprint 2)
// Part of PS2reAIcomp
// ============================================================================
#include "ps2recomp/ir_lifter.h"
#include <algorithm>
#include <cctype>
#include <cassert>
#include <cstdio>
#include <sstream>
#include <iostream>

namespace ps2recomp {

using namespace ir;

// ═══════════════════════════════════════════════════════════════════════════
// Construction / Dispatch table
// ═══════════════════════════════════════════════════════════════════════════

IRLifter::IRLifter() { initDispatchTable(); }
IRLifter::~IRLifter() = default;

void IRLifter::initDispatchTable() {
    // Integer ALU — R-type
    dispatchTable_["add"]    = &IRLifter::liftADD;
    dispatchTable_["addu"]   = &IRLifter::liftADDU;
    dispatchTable_["sub"]    = &IRLifter::liftSUB;
    dispatchTable_["subu"]   = &IRLifter::liftSUBU;
    dispatchTable_["dadd"]   = &IRLifter::liftDADD;
    dispatchTable_["daddu"]  = &IRLifter::liftDADDU;
    dispatchTable_["dsubu"]  = &IRLifter::liftDSUBU;
    // Integer ALU — I-type
    dispatchTable_["addi"]   = &IRLifter::liftADDI;
    dispatchTable_["addiu"]  = &IRLifter::liftADDIU;
    dispatchTable_["daddi"]  = &IRLifter::liftDADDI;
    dispatchTable_["daddiu"] = &IRLifter::liftDADDIU;
    dispatchTable_["move"]   = &IRLifter::liftMOVE;
    dispatchTable_["dmove"]  = &IRLifter::liftDMOVE;
    dispatchTable_["li"]     = &IRLifter::liftLI;
    // Logic
    dispatchTable_["and"]    = &IRLifter::liftAND;
    dispatchTable_["andi"]   = &IRLifter::liftANDI;
    dispatchTable_["or"]     = &IRLifter::liftOR;
    dispatchTable_["ori"]    = &IRLifter::liftORI;
    dispatchTable_["xor"]    = &IRLifter::liftXOR;
    dispatchTable_["xori"]   = &IRLifter::liftXORI;
    dispatchTable_["nor"]    = &IRLifter::liftNOR;
    // Shifts
    dispatchTable_["sll"]    = &IRLifter::liftSLL;
    dispatchTable_["srl"]    = &IRLifter::liftSRL;
    dispatchTable_["sra"]    = &IRLifter::liftSRA;
    dispatchTable_["sllv"]   = &IRLifter::liftSLLV;
    dispatchTable_["srlv"]   = &IRLifter::liftSRLV;
    dispatchTable_["srav"]   = &IRLifter::liftSRAV;
    dispatchTable_["dsll"]   = &IRLifter::liftDSLL;
    dispatchTable_["dsrl"]   = &IRLifter::liftDSRL;
    dispatchTable_["dsra"]   = &IRLifter::liftDSRA;
    dispatchTable_["dsll32"] = &IRLifter::liftDSLL32;
    dispatchTable_["dsrl32"] = &IRLifter::liftDSRL32;
    dispatchTable_["dsra32"] = &IRLifter::liftDSRA32;
    dispatchTable_["dsllv"]  = &IRLifter::liftDSLLV;
    dispatchTable_["dsrlv"]  = &IRLifter::liftDSRLV;
    dispatchTable_["dsrav"]  = &IRLifter::liftDSRAV;
    // Set-on-less-than
    dispatchTable_["slt"]    = &IRLifter::liftSLT;
    dispatchTable_["sltu"]   = &IRLifter::liftSLTU;
    dispatchTable_["slti"]   = &IRLifter::liftSLTI;
    dispatchTable_["sltiu"]  = &IRLifter::liftSLTIU;
    // LUI
    dispatchTable_["lui"]    = &IRLifter::liftLUI;
    // Multiply / Divide
    dispatchTable_["mult"]   = &IRLifter::liftMULT;
    dispatchTable_["multu"]  = &IRLifter::liftMULTU;
    dispatchTable_["div"]    = &IRLifter::liftDIV;
    dispatchTable_["divu"]   = &IRLifter::liftDIVU;
    dispatchTable_["mfhi"]   = &IRLifter::liftMFHI;
    dispatchTable_["mflo"]   = &IRLifter::liftMFLO;
    dispatchTable_["mthi"]   = &IRLifter::liftMTHI;
    dispatchTable_["mtlo"]   = &IRLifter::liftMTLO;
    // Memory loads
    dispatchTable_["lb"]     = &IRLifter::liftLB;
    dispatchTable_["lbu"]    = &IRLifter::liftLBU;
    dispatchTable_["lh"]     = &IRLifter::liftLH;
    dispatchTable_["lhu"]    = &IRLifter::liftLHU;
    dispatchTable_["lw"]     = &IRLifter::liftLW;
    dispatchTable_["lwu"]    = &IRLifter::liftLWU;
    dispatchTable_["ld"]     = &IRLifter::liftLD;
    dispatchTable_["lq"]     = &IRLifter::liftLQ;
    dispatchTable_["lwl"]    = &IRLifter::liftLWL;
    dispatchTable_["lwr"]    = &IRLifter::liftLWR;
    dispatchTable_["ldl"]    = &IRLifter::liftLDL;
    dispatchTable_["ldr"]    = &IRLifter::liftLDR;
    // Memory stores
    dispatchTable_["sb"]     = &IRLifter::liftSB;
    dispatchTable_["sh"]     = &IRLifter::liftSH;
    dispatchTable_["sw"]     = &IRLifter::liftSW;
    dispatchTable_["sd"]     = &IRLifter::liftSD;
    dispatchTable_["sq"]     = &IRLifter::liftSQ;
    dispatchTable_["swl"]    = &IRLifter::liftSWL;
    dispatchTable_["swr"]    = &IRLifter::liftSWR;
    dispatchTable_["sdl"]    = &IRLifter::liftSDL;
    dispatchTable_["sdr"]    = &IRLifter::liftSDR;
    // Branches
    dispatchTable_["beq"]    = &IRLifter::liftBEQ;
    dispatchTable_["bne"]    = &IRLifter::liftBNE;
    dispatchTable_["bgez"]   = &IRLifter::liftBGEZ;
    dispatchTable_["bgtz"]   = &IRLifter::liftBGTZ;
    dispatchTable_["blez"]   = &IRLifter::liftBLEZ;
    dispatchTable_["bltz"]   = &IRLifter::liftBLTZ;
    dispatchTable_["beql"]   = &IRLifter::liftBEQL;
    dispatchTable_["bnel"]   = &IRLifter::liftBNEL;
    dispatchTable_["bgezl"]  = &IRLifter::liftBGEZL;
    dispatchTable_["bgtzl"]  = &IRLifter::liftBGTZL;
    dispatchTable_["blezl"]  = &IRLifter::liftBLEZL;
    dispatchTable_["bltzl"]  = &IRLifter::liftBLTZL;
    dispatchTable_["beqz"]   = &IRLifter::liftBEQ;
    dispatchTable_["bnez"]   = &IRLifter::liftBNE;
    // Jumps / Calls
    dispatchTable_["j"]      = &IRLifter::liftJ;
    dispatchTable_["jal"]    = &IRLifter::liftJAL;
    dispatchTable_["jr"]     = &IRLifter::liftJR;
    dispatchTable_["jalr"]   = &IRLifter::liftJALR;
    // Conditional move
    dispatchTable_["movz"]   = &IRLifter::liftMOVZ;
    dispatchTable_["movn"]   = &IRLifter::liftMOVN;
    // System
    dispatchTable_["syscall"]= &IRLifter::liftSYSCALL;
    dispatchTable_["break"]  = &IRLifter::liftBREAK;
    dispatchTable_["sync"]   = &IRLifter::liftSYNC;
    dispatchTable_["ei"]     = &IRLifter::liftEI;
    dispatchTable_["di"]     = &IRLifter::liftDI;
    dispatchTable_["nop"]    = &IRLifter::liftNOP;
    // Pseudo / Synthetic
    dispatchTable_["move"]   = &IRLifter::liftMOVE;
    dispatchTable_["clear"]  = &IRLifter::liftCLEAR;
    dispatchTable_["li"]     = &IRLifter::liftLI;
    dispatchTable_["_clear"] = &IRLifter::liftCLEAR;
    dispatchTable_["negu"]   = &IRLifter::liftNEGU;
    dispatchTable_["b"]      = &IRLifter::liftB;
    // FPU
    dispatchTable_["add.s"]  = &IRLifter::liftADD_S;
    dispatchTable_["adda.s"] = &IRLifter::liftADDA_S;
    dispatchTable_["sub.s"]  = &IRLifter::liftSUB_S;
    dispatchTable_["mul.s"]  = &IRLifter::liftMUL_S;
    dispatchTable_["div.s"]  = &IRLifter::liftDIV_S;
    dispatchTable_["mov.s"]  = &IRLifter::liftMOV_S;
    dispatchTable_["neg.s"]  = &IRLifter::liftNEG_S;
    dispatchTable_["abs.s"]  = &IRLifter::liftABS_S;
    dispatchTable_["sqrt.s"] = &IRLifter::liftSQRT_S;
    dispatchTable_["mfc1"]   = &IRLifter::liftMFC1;
    dispatchTable_["mtc1"]   = &IRLifter::liftMTC1;
    dispatchTable_["cvt.s.w"]= &IRLifter::liftCVT_S_W;
    dispatchTable_["cvt.w.s"]= &IRLifter::liftCVT_W_S;
    dispatchTable_["lwc1"]   = &IRLifter::liftLWC1;
    dispatchTable_["swc1"]   = &IRLifter::liftSWC1;
    dispatchTable_["c.eq.s"] = &IRLifter::liftC_EQ_S;
    dispatchTable_["c.lt.s"] = &IRLifter::liftC_LT_S;
    dispatchTable_["c.le.s"] = &IRLifter::liftC_LE_S;
    dispatchTable_["bc1t"]   = &IRLifter::liftBC1T;
    dispatchTable_["bc1f"]   = &IRLifter::liftBC1F;
    dispatchTable_["bc1tl"]  = &IRLifter::liftBC1TL;
    dispatchTable_["bc1fl"]  = &IRLifter::liftBC1FL;

    // ── Phase 1: FPU Extended (PS2-specific) ────────────────────────────
    dispatchTable_["madd.s"]  = &IRLifter::liftMADD_S;
    dispatchTable_["madd.S"]  = &IRLifter::liftMADD_S;
    dispatchTable_["_madd.S"] = &IRLifter::liftMADD_S;
    dispatchTable_["msub.s"]  = &IRLifter::liftMSUB_S;
    dispatchTable_["msub.S"]  = &IRLifter::liftMSUB_S;
    dispatchTable_["_msub.S"] = &IRLifter::liftMSUB_S;
    dispatchTable_["max.s"]   = &IRLifter::liftMAX_S;
    dispatchTable_["max.S"]   = &IRLifter::liftMAX_S;
    dispatchTable_["_max.S"]  = &IRLifter::liftMAX_S;
    dispatchTable_["min.s"]   = &IRLifter::liftMIN_S;
    dispatchTable_["min.S"]   = &IRLifter::liftMIN_S;
    dispatchTable_["mula.s"]  = &IRLifter::liftMULA_S;
    dispatchTable_["mula.S"]  = &IRLifter::liftMULA_S;
    dispatchTable_["suba.s"]  = &IRLifter::liftSUBA_S;
    dispatchTable_["suba.S"]  = &IRLifter::liftSUBA_S;

    // ── Phase 2: COP0 / System / Misc ───────────────────────────────────
    dispatchTable_["mfc0"]    = &IRLifter::liftMFC0;
    dispatchTable_["mtc0"]    = &IRLifter::liftMTC0;
    dispatchTable_["bc0f"]    = &IRLifter::liftBC0F;
    dispatchTable_["cache"]   = &IRLifter::liftCACHE;
    dispatchTable_["tlbwi"]   = &IRLifter::liftTLBWI;
    dispatchTable_["mtsab"]   = &IRLifter::liftMTSAB;
    dispatchTable_["moveq"]   = &IRLifter::liftMOVEQ;

    // ── Phase 3: Pipeline-1 HI/LO ───────────────────────────────────────
    dispatchTable_["mult1"]   = &IRLifter::liftMULT1;
    dispatchTable_["div1"]    = &IRLifter::liftDIV1;
    dispatchTable_["mfhi1"]   = &IRLifter::liftMFHI1;
    dispatchTable_["mflo1"]   = &IRLifter::liftMFLO1;
    dispatchTable_["madd"]    = &IRLifter::liftMADD;

    // ── Phase 4: MMI 128-bit SIMD ───────────────────────────────────────
    dispatchTable_["paddh"]   = &IRLifter::liftPADDH;
    dispatchTable_["paddw"]   = &IRLifter::liftPADDW;
    dispatchTable_["psubb"]   = &IRLifter::liftPSUBB;
    dispatchTable_["psubw"]   = &IRLifter::liftPSUBW;
    dispatchTable_["psubh"]   = &IRLifter::liftPSUBH;
    dispatchTable_["pmaxh"]   = &IRLifter::liftPMAXH;
    dispatchTable_["pminh"]   = &IRLifter::liftPMINH;
    dispatchTable_["pand"]    = &IRLifter::liftPAND;
    dispatchTable_["por"]     = &IRLifter::liftPOR;
    dispatchTable_["pxor"]    = &IRLifter::liftPXOR;
    dispatchTable_["pnor"]    = &IRLifter::liftPNOR;
    dispatchTable_["pceqb"]   = &IRLifter::liftPCEQB;
    dispatchTable_["pceqh"]   = &IRLifter::liftPCEQH;
    dispatchTable_["pceqw"]   = &IRLifter::liftPCEQW;
    dispatchTable_["pcgth"]   = &IRLifter::liftPCGTH;
    dispatchTable_["pextlb"]  = &IRLifter::liftPEXTLB;
    dispatchTable_["pextlw"]  = &IRLifter::liftPEXTLW;
    dispatchTable_["pextub"]  = &IRLifter::liftPEXTUB;
    dispatchTable_["pextuw"]  = &IRLifter::liftPEXTUW;
    dispatchTable_["pcpyld"]  = &IRLifter::liftPCPYLD;
    dispatchTable_["pcpyud"]  = &IRLifter::liftPCPYUD;
    dispatchTable_["pinteh"]  = &IRLifter::liftPINTEH;
    dispatchTable_["ppacb"]   = &IRLifter::liftPPACB;
    dispatchTable_["ppacw"]   = &IRLifter::liftPPACW;
    dispatchTable_["psllh"]   = &IRLifter::liftPSLLH;
    dispatchTable_["psllw"]   = &IRLifter::liftPSLLW;
    dispatchTable_["psrah"]   = &IRLifter::liftPSRAH;
    dispatchTable_["psrlh"]   = &IRLifter::liftPSRLH;
    dispatchTable_["psrlw"]   = &IRLifter::liftPSRLW;
    dispatchTable_["pcpyh"]   = &IRLifter::liftPCPYH;
    dispatchTable_["pexch"]   = &IRLifter::liftPEXCH;
    dispatchTable_["pexcw"]   = &IRLifter::liftPEXCW;
    dispatchTable_["pexeh"]   = &IRLifter::liftPEXEH;
    dispatchTable_["pexew"]   = &IRLifter::liftPEXEW;
    dispatchTable_["plzcw"]   = &IRLifter::liftPLZCW;
    dispatchTable_["prevh"]   = &IRLifter::liftPREVH;
    dispatchTable_["prot3w"]  = &IRLifter::liftPROT3W;
    dispatchTable_["qfsrv"]   = &IRLifter::liftQFSRV;
    dispatchTable_["pmfhl.lh"]= &IRLifter::liftPMFHL;
    dispatchTable_["pmfhl.lw"]= &IRLifter::liftPMFHL;
    dispatchTable_["pmfhl.uw"]= &IRLifter::liftPMFHL;
    dispatchTable_["pmthi"]   = &IRLifter::liftPMTHI;
    dispatchTable_["pmtlo"]   = &IRLifter::liftPMTLO;

    // ── Phase 5: VU0 COP2 Transfer ──────────────────────────────────────
    dispatchTable_["qmfc2"]   = &IRLifter::liftVU0_Transfer;
    dispatchTable_["_qmfc2"]  = &IRLifter::liftVU0_Transfer;
    dispatchTable_["qmtc2"]   = &IRLifter::liftVU0_Transfer;
    dispatchTable_["_qmtc2"]  = &IRLifter::liftVU0_Transfer;
    dispatchTable_["cfc2"]    = &IRLifter::liftVU0_Transfer;
    dispatchTable_["ctc2"]    = &IRLifter::liftVU0_Transfer;
    dispatchTable_["lqc2"]    = &IRLifter::liftVU0_Transfer;
    dispatchTable_["sqc2"]    = &IRLifter::liftVU0_Transfer;
    dispatchTable_["_sqc2"]   = &IRLifter::liftVU0_Transfer;

    // ── Phase 5: VU0 Special ────────────────────────────────────────────
    dispatchTable_["vdiv"]    = &IRLifter::liftVU0_Special;
    dispatchTable_["vsqrt"]   = &IRLifter::liftVU0_Special;
    dispatchTable_["vrsqrt"]  = &IRLifter::liftVU0_Special;
    dispatchTable_["vwaitq"]  = &IRLifter::liftVU0_Special;
    dispatchTable_["vnop"]    = &IRLifter::liftVU0_Special;
    dispatchTable_["vclipw.xyz"]  = &IRLifter::liftVU0_Special;
    dispatchTable_["vmtir"]   = &IRLifter::liftVU0_Special;
    dispatchTable_["vlqd.xyzw"]   = &IRLifter::liftVU0_Special;
    dispatchTable_["vlqi.xyzw"]   = &IRLifter::liftVU0_Special;

    // ── Phase 5: VU0 Generic Arithmetic (all v* mnemonics) ──────────────
    // VADD variants
    dispatchTable_["vadd.x"]       = &IRLifter::liftVU0_Generic;
    dispatchTable_["vadd.xyz"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vadd.xyzw"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vadda.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vadda.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vadda.xzw"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddaw.x"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddax.xzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vadday.x"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vadday.x"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddq.w"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddq.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddq.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddw.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddw.y"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddw.z"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddx.w"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddx.xy"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddx.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddx.xyzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddx.y"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddx.z"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddy.w"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddy.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddz.w"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddz.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddz.xy"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vaddz.xyz"]    = &IRLifter::liftVU0_Generic;
    // VABS
    dispatchTable_["vabs.xyzw"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vabs.xz"]      = &IRLifter::liftVU0_Generic;
    // VFTOI / VITOF
    dispatchTable_["vftoi0.xyzw"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vftoi0.z"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vitof0.xy"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vitof0.xyz"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vitof0.xyzw"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vitof0.xz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vitof12.xy"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vitof15.xyzw"] = &IRLifter::liftVU0_Generic;
    // VMADD variants
    dispatchTable_["vmadd.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmadd.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmadd.xyzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmadd.xzw"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmadda.x"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmadda.xyzw"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmadda.xzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddaw.x"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddaw.xyz"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddaw.xyzw"] = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddax.x"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddax.xyz"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddax.xzw"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmadday.w"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmadday.x"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmadday.xy"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmadday.xyz"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmadday.xyzw"] = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddaz.x"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddaz.xyz"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddaz.xyzw"] = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddaz.xzw"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddw.x"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddw.xyz"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddw.xyzw"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vmaddw.xyzw"] = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddx.xyz"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddx.xyzw"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddy.xyz"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddz.w"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddz.x"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddz.xy"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddz.xyz"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaddz.xyzw"]  = &IRLifter::liftVU0_Generic;
    // VMAX / VMINI
    dispatchTable_["vmax.w"]       = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmax.xyz"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaxw.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaxw.xyzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vmaxw.xyzw"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmaxx.xyzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmini.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmini.xyzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vminibcw.x"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vminibcw.xyzw"]= &IRLifter::liftVU0_Generic;
    dispatchTable_["vminibcx.xyw"] = &IRLifter::liftVU0_Generic;
    dispatchTable_["vminibcx.xzw"] = &IRLifter::liftVU0_Generic;
    dispatchTable_["vminibcx.yzw"] = &IRLifter::liftVU0_Generic;
    // VMOVE / VMR32
    dispatchTable_["vmove.w"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmove.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmove.xy"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmove.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmove.xyzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmove.y"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmove.z"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmr32.w"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vmr32.w"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmr32.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmr32.xyzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vmr32.xyzw"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmr32.z"]      = &IRLifter::liftVU0_Generic;
    // VMSUB variants
    dispatchTable_["vmsub.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vmsub.x"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmsuba.x"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmsubax.x"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmsubax.xyz"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmsubay.x"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmsubay.xyzw"] = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmsubaz.x"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmsubaz.xyzw"] = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmsubw.x"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmsubx.xyz"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmsubx.xyzw"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmsubx.xzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmsuby.z"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmsubz.x"]     = &IRLifter::liftVU0_Generic;
    // VMUL variants
    dispatchTable_["vmul.x"]       = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vmul.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmul.xyz"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmul.xyzw"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmul.xzw"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmula.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmula.xyzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulaw.xyz"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulaw.xyzw"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulax.w"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulax.xy"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulax.xyz"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulax.xyzw"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vmulax.xyzw"] = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulax.z"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulay.xyzw"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulq.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vmulq.x"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulq.xy"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulq.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulq.xyzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulq.zw"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulw.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulw.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulw.xyzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulw.xzw"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulx.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulx.xy"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulx.xyw"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulx.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulx.xyzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulx.xzw"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulx.yzw"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulx.z"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulx.zw"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmuly.w"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmuly.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vmuly.z"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmuly.xy"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmuly.xyzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmuly.xzw"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulz.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulz.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vmulz.xyzw"]   = &IRLifter::liftVU0_Generic;
    // VOPMULA / VOPMSUB
    dispatchTable_["vopmula.xyz"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["vopmsub.xyz"]  = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vopmsub.xyz"] = &IRLifter::liftVU0_Generic;
    // VSUB variants
    dispatchTable_["vsub.w"]       = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsub.x"]       = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsub.xyz"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vsub.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsub.xyzw"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsuba.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vsuba.x"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsuba.xyz"]    = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsubaw.x"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsubaw.xyz"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsubw.w"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsubw.x"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["_vsubw.x"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsubw.y"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsubw.z"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsubx.w"]      = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsubx.xyzw"]   = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsubx.xz"]     = &IRLifter::liftVU0_Generic;
    dispatchTable_["vsuby.xyzw"]   = &IRLifter::liftVU0_Generic;
}

// ═══════════════════════════════════════════════════════════════════════════
// MIPS field extraction
// ═══════════════════════════════════════════════════════════════════════════

IRLifter::MIPSFields IRLifter::decodeFields(uint32_t raw) {
    MIPSFields f;
    f.opcode   = static_cast<uint8_t>((raw >> 26) & 0x3F);
    f.rs       = static_cast<uint8_t>((raw >> 21) & 0x1F);
    f.rt       = static_cast<uint8_t>((raw >> 16) & 0x1F);
    f.rd       = static_cast<uint8_t>((raw >> 11) & 0x1F);
    f.sa       = static_cast<uint8_t>((raw >>  6) & 0x1F);
    f.func     = static_cast<uint8_t>( raw        & 0x3F);
    f.imm16    = static_cast<uint16_t>(raw & 0xFFFF);
    f.simm16   = static_cast<int16_t>(f.imm16);
    f.target26 = raw & 0x03FFFFFF;
    f.fmt      = f.rs;   // COP1: format field shares rs position
    f.ft       = f.rt;   // COP1: ft shares rt position
    f.fs       = f.rd;   // COP1: fs shares rd position
    f.fd       = f.sa;   // COP1: fd shares sa position
    return f;
}

// ═══════════════════════════════════════════════════════════════════════════
// Pass 1: Block boundary discovery
// ═══════════════════════════════════════════════════════════════════════════

std::unordered_set<uint32_t> IRLifter::findBlockBoundaries(
        const std::vector<GhidraInstruction>& disasm) const {
    std::unordered_set<uint32_t> starts;
    if (disasm.empty()) return starts;

    // First instruction always starts a block
    starts.insert(disasm[0].addr);

    for (size_t i = 0; i < disasm.size(); ++i) {
        const auto& instr = disasm[i];
        const auto  f = decodeFields(instr.rawBytes);

        bool isBranch = false;
        uint32_t target = 0;

        // Check for branch instructions (I-type with PC-relative offset)
        if (instr.mnemonic == "beq"  || instr.mnemonic == "bne"  ||
            instr.mnemonic == "beqz" || instr.mnemonic == "bnez" ||
            instr.mnemonic == "bgez" || instr.mnemonic == "bgtz" ||
            instr.mnemonic == "blez" || instr.mnemonic == "bltz" ||
            instr.mnemonic == "beql" || instr.mnemonic == "bnel" ||
            instr.mnemonic == "bgezal" || instr.mnemonic == "bltzal" ||
            instr.mnemonic == "bc1t" || instr.mnemonic == "bc1f" ||
            instr.mnemonic == "bc1tl"|| instr.mnemonic == "bc1fl") {
            isBranch = true;
            target = computeBranchTarget(instr.addr, f.simm16);
        }
        // J / JAL
        else if (instr.mnemonic == "j") {
            isBranch = true;
            target = computeJumpTarget(instr.addr, f.target26);
        }
        // JR is a terminator but target is dynamic — just mark fall-through
        else if (instr.mnemonic == "jr" || instr.mnemonic == "jalr" ||
                 instr.mnemonic == "jal") {
            isBranch = true;
            target = 0; // dynamic or call — no intra-function target
        }

        if (isBranch) {
            // Branch target starts a new block
            if (target != 0) starts.insert(target);
            // Instruction after delay slot starts a new block
            if (i + 2 < disasm.size()) {
                starts.insert(disasm[i + 2].addr);
            }
        }
    }
    return starts;
}

// ═══════════════════════════════════════════════════════════════════════════
// Main lifting entry point
// ═══════════════════════════════════════════════════════════════════════════

std::optional<IRFunction> IRLifter::liftFunction(
        const GhidraFunction& funcInfo,
        const std::vector<GhidraInstruction>& disasm,
        const std::vector<ResolvedJumpTable>* resolvedJumps,
        ProgressCallback progress) {

    if (disasm.empty()) return std::nullopt;

    resetStats();
    addrToBlockIndex_.clear();
    currentResolvedJumps_ = resolvedJumps;
    currentFuncStart_ = funcInfo.startAddr;
    // Compute the TRUE function end from the actual disassembly, not Ghidra's
    // potentially too-tight endAddr. Ghidra sometimes reports endAddr that
    // doesn't cover all basic blocks, causing internal branches to be
    // misclassified as "external" tail-calls.
    {
        uint32_t maxAddr = funcInfo.endAddr;
        for (const auto& inst : disasm) {
            if (inst.addr + 4 > maxAddr) {
                maxAddr = inst.addr + 4;
            }
        }
        currentFuncEnd_ = maxAddr;
    }

    IRFunction func;
    func.name = funcInfo.name;
    func.mipsEntryAddr = funcInfo.startAddr;
    func.mipsEndAddr = currentFuncEnd_;

    stats_.totalInstructions = static_cast<uint32_t>(disasm.size());
    std::cout << "\nLifting Function: " << func.name << "\n";

    // Pass 1: find block boundaries
    auto blockStarts = findBlockBoundaries(disasm);

    // Sort block starts to create blocks in address order
    std::vector<uint32_t> sortedStarts(blockStarts.begin(), blockStarts.end());
    std::sort(sortedStarts.begin(), sortedStarts.end());

    // Pre-create all blocks so we can reference them by index
    for (auto addr : sortedStarts) {
        char label[32];
        std::snprintf(label, sizeof(label), "bb_%08X", addr);
        auto& bb = func.addBlock(label);
        bb.mipsStartAddr = addr;
        addrToBlockIndex_[addr] = bb.index;
    }
    stats_.basicBlocks = static_cast<uint32_t>(func.blocks.size());

    // Pass 2: emit IR per block
    uint32_t currentBlockIdx = 0;
    if (!func.blocks.empty()) {
        currentBlockIdx = addrToBlockIndex_.count(disasm[0].addr)
            ? addrToBlockIndex_[disasm[0].addr] : 0;
    }

    currentDisasm_ = &disasm;
    skipInstructionIndices_.clear();

    for (uint32_t i = 0; i < disasm.size(); ++i) {
        if (skipInstructionIndices_.count(i)) {
            continue;
        }

        currentInstrIndex_ = i;
        const auto& instr = disasm[i];

        // Switch to new block if this address starts one
        auto it = addrToBlockIndex_.find(instr.addr);
        if (it != addrToBlockIndex_.end()) {
            currentBlockIdx = it->second;
        }

        auto* bb = func.getBlock(currentBlockIdx);
        if (!bb) {
            std::cout << "ERROR: Invalid block idx " << currentBlockIdx << "\n";
            continue;
        }

        if (i < 5 || i > disasm.size() - 5) {
            std::cout << "Lifting inst: " << instr.mnemonic << " at " << std::hex << instr.addr << " with bb idx: " << std::dec << currentBlockIdx << "\n";
        }

        bb->mipsEndAddr = instr.addr + 4;

        // Decode raw fields
        auto fields = decodeFields(instr.rawBytes);

        // Emit source comment
        if (emitComments_) {
            std::string text = instr.mnemonic;
            if (!instr.operands.empty()) text += " " + instr.operands;
            emitComment(func, currentBlockIdx, text, instr.addr);
        }

        std::string mnemonic = instr.mnemonic;
        for (auto& c : mnemonic) {
            c = std::tolower(static_cast<unsigned char>(c));
        }
        if (!mnemonic.empty() && mnemonic[0] == '_') {
            mnemonic = mnemonic.substr(1);
        }

        // Skip NOPs (SLL $zero, $zero, 0)
        if (instr.isNop() || mnemonic == "nop") {
            stats_.skippedNops++;
            stats_.liftedInstructions++;
        } else {
            // Dispatch to handler
            auto handler = dispatchTable_.find(mnemonic);
            if (handler != dispatchTable_.end()) {
                (this->*(handler->second))(func, currentBlockIdx, instr, fields);
                stats_.liftedInstructions++;
            } else {
                liftUnhandled(func, currentBlockIdx, instr, fields);
                stats_.unhandledMnemonics++;
            }
        }

        if (progress) progress(i, stats_.totalInstructions);
    }

    // Wire up fall-through edges between consecutive blocks
    for (uint32_t b = 0; b + 1 < func.blocks.size(); ++b) {
        auto& blk = func.blocks[b];
        if (blk.instructions.empty() || !blk.instructions.back().isTerminator()) {
            blk.successors.push_back(b + 1);
            func.blocks[b + 1].predecessors.push_back(b);
        }
    }

    return func;
}

// ═══════════════════════════════════════════════════════════════════════════
// Register read/write helpers
// ═══════════════════════════════════════════════════════════════════════════

ValueId IRLifter::emitGPRRead(IRFunction& func, uint32_t blockIdx,
                               uint8_t regIdx, uint32_t srcAddr) {
    // $zero is always 0
    if (regIdx == 0) return emitConst32(func, blockIdx, 0);

    auto inst = makeRegRead(func, IRType::I32, IRReg::gpr(regIdx));
    inst.srcAddress = srcAddr;
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    return vid;
}

ValueId IRLifter::emitGPRRead(IRFunction& func, uint32_t blockIdx,
                               uint8_t regIdx, uint32_t srcAddr, IRType type) {
    // $zero is always 0
    if (regIdx == 0) {
        auto c = makeConst(func, type, 0);
        ValueId cid = c.result.id;
        func.blocks[blockIdx].instructions.push_back(std::move(c));
        return cid;
    }

    auto inst = makeRegRead(func, type, IRReg::gpr(regIdx));
    inst.srcAddress = srcAddr;
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    return vid;
}

ValueId IRLifter::emitFPRRead(IRFunction& func, uint32_t blockIdx,
                               uint8_t regIdx, uint32_t srcAddr) {
    auto inst = makeRegRead(func, IRType::F32, IRReg::fpr(regIdx));
    inst.srcAddress = srcAddr;
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    return vid;
}

void IRLifter::emitGPRWrite(IRFunction& func, uint32_t blockIdx,
                             uint8_t regIdx, ValueId value, uint32_t srcAddr) {
    if (regIdx == 0) return;  // writes to $zero are dropped
    auto inst = makeRegWrite(IRReg::gpr(regIdx), value);
    inst.srcAddress = srcAddr;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
}

void IRLifter::emitFPRWrite(IRFunction& func, uint32_t blockIdx,
                             uint8_t regIdx, ValueId value, uint32_t srcAddr) {
    auto inst = makeRegWrite(IRReg::fpr(regIdx), value);
    inst.srcAddress = srcAddr;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
}

ValueId IRLifter::emitConst32(IRFunction& func, uint32_t blockIdx, int32_t value) {
    auto inst = makeConst(func, IRType::I32, static_cast<int64_t>(value));
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    return vid;
}

ValueId IRLifter::emitConstU32(IRFunction& func, uint32_t blockIdx, uint32_t value) {
    auto inst = makeConstU(func, IRType::I32, static_cast<uint64_t>(value));
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    return vid;
}

ValueId IRLifter::emitConst64(IRFunction& func, uint32_t blockIdx, int64_t value) {
    auto inst = makeConst(func, IRType::I64, value);
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    return vid;
}

// ═══════════════════════════════════════════════════════════════════════════
// Comment emission
// ═══════════════════════════════════════════════════════════════════════════

void IRLifter::emitComment(IRFunction& func, uint32_t blockIdx, const std::string& text,
                            uint32_t srcAddr) {
    IRInst inst;
    inst.op = IROp::IR_COMMENT;
    inst.comment = text;
    inst.srcAddress = srcAddr;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
}

// ═══════════════════════════════════════════════════════════════════════════
// Block management
// ═══════════════════════════════════════════════════════════════════════════

uint32_t IRLifter::getOrCreateBlock(IRFunction& func, uint32_t addr) {
    auto it = addrToBlockIndex_.find(addr);
    if (it != addrToBlockIndex_.end()) return it->second;
    char label[32];
    std::snprintf(label, sizeof(label), "bb_%08X", addr);
    auto& bb = func.addBlock(label);
    bb.mipsStartAddr = addr;
    addrToBlockIndex_[addr] = bb.index;
    return bb.index;
}

// ═══════════════════════════════════════════════════════════════════════════
// Address computation helpers
// ═══════════════════════════════════════════════════════════════════════════

uint32_t IRLifter::computeBranchTarget(uint32_t pc, int16_t offset) const {
    // MIPS: target = PC + 4 + (sign_ext(offset) << 2)
    return pc + 4 + (static_cast<int32_t>(offset) << 2);
}

uint32_t IRLifter::computeJumpTarget(uint32_t pc, uint32_t target26) const {
    // MIPS: target = (PC+4)[31:28] | (target26 << 2)
    return ((pc + 4) & 0xF0000000) | (target26 << 2);
}

ValueId IRLifter::emitAddrCalc(IRFunction& func, uint32_t blockIdx,
                                uint8_t baseReg, int16_t offset,
                                uint32_t srcAddr) {
    auto base = emitGPRRead(func, blockIdx, baseReg, srcAddr);
    if (offset == 0) return base;
    auto off = emitConst32(func, blockIdx, static_cast<int32_t>(offset));
    auto inst = makeBinaryOp(func, IROp::IR_ADD, IRType::I32, base, off, srcAddr);
    ValueId vid = inst.result.id;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
    return vid;
}

// ═══════════════════════════════════════════════════════════════════════════
// Unhandled instruction fallback
// ═══════════════════════════════════════════════════════════════════════════

void IRLifter::liftUnhandled(IRFunction& func, uint32_t blockIdx,
                              const GhidraInstruction& instr,
                              const MIPSFields&) {
    std::cout << "WARNING: UNHANDLED MNEMONIC: '" << instr.mnemonic << "' at PC 0x" << std::hex << instr.addr << std::dec << "\n";
    IRInst inst;
    inst.op = IROp::IR_NOP;
    inst.srcAddress = instr.addr;
    inst.srcRaw = instr.rawBytes;
    inst.comment = "[UNHANDLED] " + instr.mnemonic + " " + instr.operands;
    func.blocks[blockIdx].instructions.push_back(std::move(inst));
}

void IRLifter::inlineDelaySlot(ir::IRFunction& func, uint32_t blockIdx, bool isLikely, std::optional<ir::ValueId> condId) {
    if (currentInstrIndex_ + 1 >= currentDisasm_->size()) return;
    
    const GhidraInstruction& delaySlotInst = (*currentDisasm_)[currentInstrIndex_ + 1];
    MIPSFields f = decodeFields(delaySlotInst.rawBytes);
    
    std::string mnemonic = delaySlotInst.mnemonic;
    for (auto& c : mnemonic) {
        c = std::tolower(static_cast<unsigned char>(c));
    }
    if (!mnemonic.empty() && mnemonic[0] == '_') {
        mnemonic = mnemonic.substr(1);
    }
    
    if (isLikely && condId) {
        ir::IRInst markIfLikely;
        markIfLikely.op = ir::IROp::IR_IF_LIKELY;
        markIfLikely.operands.push_back(*condId);
        func.blocks[blockIdx].instructions.push_back(std::move(markIfLikely));
    }
    
    // Dispatch delay slot instruction
    auto handlerIt = dispatchTable_.find(mnemonic);
    if (handlerIt != dispatchTable_.end()) {
        (this->*(handlerIt->second))(func, blockIdx, delaySlotInst, f);
    } else {
        if (mnemonic == "nop") {
            // NOPs are safe to skip inside delay slots too
        } else {
            liftUnhandled(func, blockIdx, delaySlotInst, f);
        }
    }
    
    if (isLikely) {
        ir::IRInst markEndLikely;
        markEndLikely.op = ir::IROp::IR_END_LIKELY;
        func.blocks[blockIdx].instructions.push_back(std::move(markEndLikely));
    }
    
    // Check if we should skip the delay slot in main processing loop
    bool isBlockBoundary = (addrToBlockIndex_.find(delaySlotInst.addr) != addrToBlockIndex_.end());
    if (!isBlockBoundary) {
        skipInstructionIndices_.insert(currentInstrIndex_ + 1);
    }
}

void IRLifter::emitTerminator(IRFunction& func, uint32_t blockIdx, IRInst termInst, bool isLikely, bool hasFallthrough) {
    std::optional<ir::ValueId> condId = std::nullopt;
    if (isLikely && termInst.op == IROp::IR_BRANCH && !termInst.operands.empty()) {
        condId = termInst.operands[0];
    }
    
    inlineDelaySlot(func, blockIdx, isLikely, condId);
    func.blocks[blockIdx].instructions.push_back(std::move(termInst));

    if (hasFallthrough && currentInstrIndex_ + 2 < currentDisasm_->size()) {
        uint32_t fallAddr = (*currentDisasm_)[currentInstrIndex_ + 2].addr;
        IRInst jmp;
        jmp.op = IROp::IR_JUMP;
        jmp.branchTarget = getOrCreateBlock(func, fallAddr);
        jmp.srcAddress = termInst.srcAddress;
        func.blocks[blockIdx].instructions.push_back(std::move(jmp));
        
        func.blocks[blockIdx].successors.push_back(jmp.branchTarget);
        func.blocks[jmp.branchTarget].predecessors.push_back(blockIdx);
    }
}

} // namespace ps2recomp
