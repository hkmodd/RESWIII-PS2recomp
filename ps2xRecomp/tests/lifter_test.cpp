// ============================================================================
// lifter_test.cpp — IR Lifter Smoke Test
// Connects to Ghidra, extracts a function, lifts to IR, prints the IR tree
// ============================================================================
#include <cstdio>
#include <cstdlib>
#include <string>
#include "ps2recomp/ghidra_bridge.h"
#include "ps2recomp/ir_lifter.h"
#include "ps2recomp/ir.h"
#include "../src/backend/cpp_emitter.h"

using namespace ps2recomp;
using namespace ps2recomp::ir;

// ── Simple IR Printer ───────────────────────────────────────────────────────

static void printIRFunction(const IRFunction& func) {
    printf("========================================\n");
    printf("IR Function: %s\n", func.name.c_str());
    printf("MIPS Range: 0x%08X - 0x%08X\n", func.mipsEntryAddr, func.mipsEndAddr);
    printf("Blocks: %zu  |  SSA Values: %u\n",
           func.blocks.size(), func.nextValueId);
    printf("========================================\n\n");

    for (size_t bi = 0; bi < func.blocks.size(); ++bi) {
        const auto& bb = func.blocks[bi];
        printf("--- %s (MIPS 0x%08X - 0x%08X) ---\n",
               bb.label.c_str(), bb.mipsStartAddr, bb.mipsEndAddr);

        // Print predecessors/successors
        if (!bb.predecessors.empty()) {
            printf("  preds: ");
            for (auto p : bb.predecessors) printf("bb_%u ", p);
            printf("\n");
        }
        if (!bb.successors.empty()) {
            printf("  succs: ");
            for (auto s : bb.successors) printf("bb_%u ", s);
            printf("\n");
        }

        for (const auto& inst : bb.instructions) {
            // Format: v<id> : <type> = <OP> <operands...>  ; comment @ 0xADDR
            if (inst.hasResult()) {
                printf("  v%-4u : %-4s = %-12s",
                       inst.result.id,
                       irTypeName(inst.result.type),
                       irOpName(inst.op));
            } else {
                printf("  %-13s %-12s", "", irOpName(inst.op));
            }

            // Print register info
            if (inst.op == IROp::IR_REG_READ || inst.op == IROp::IR_REG_WRITE) {
                printf(" %s", irRegName(inst.reg));
            }

            // Print operands
            for (size_t i = 0; i < inst.operands.size(); ++i) {
                printf(" v%u", inst.operands[i]);
            }

            // Print constant data
            if (inst.op == IROp::IR_CONST) {
                if (inst.result.type == IRType::F32) {
                    printf(" = %f", inst.constData.immFloat);
                } else {
                    printf(" = 0x%llX", static_cast<unsigned long long>(inst.constData.immUnsigned));
                }
            }

            // Print branch target
            if (inst.branchTarget != 0 && inst.isBranch()) {
                printf(" -> bb_%u", inst.branchTarget);
            }
            if (inst.op == IROp::IR_CALL) {
                printf(" -> 0x%08X", inst.branchTarget);
            }

            // Print comment and source address
            if (!inst.comment.empty()) {
                printf("  ; %s", inst.comment.c_str());
            }
            if (inst.srcAddress != 0) {
                printf("  @ 0x%08X", inst.srcAddress);
            }

            printf("\n");
        }
        printf("\n");
    }
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Default: use Ghidra on localhost:8192, lift entry function
    std::string host = "localhost";
    int port = 8192;
    uint32_t targetAddr = 0x00100008; // Typical PS2 entry point

    if (argc >= 2) {
        // Allow overriding target address: lifter_test 0x001234
        targetAddr = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 16));
    }
    if (argc >= 3) {
        port = std::atoi(argv[2]);
    }

    printf("[LIFTER TEST] Connecting to Ghidra at %s:%d...\n", host.c_str(), port);

    GhidraBridge bridge(host, port);
    if (!bridge.connect()) {
        fprintf(stderr, "[ERROR] Cannot connect to Ghidra at %s:%d\n", host.c_str(), port);
        fprintf(stderr, "Make sure GhydraMCP is running with the target binary open.\n");
        return 1;
    }
    printf("[OK] Connected to Ghidra. Program: %s\n", bridge.programName().c_str());

    // Fetch function details from Ghidra (composite: info + disassembly + xrefs)
    printf("[LIFTER TEST] Fetching function at 0x%08X...\n", targetAddr);
    auto detail = bridge.fetchFunctionDetail(targetAddr);

    if (!detail.has_value()) {
        fprintf(stderr, "[ERROR] No function found at 0x%08X\n", targetAddr);
        return 1;
    }

    printf("[OK] Function: %s (0x%08X - 0x%08X), %zu instructions\n",
           detail->info.name.c_str(),
           detail->info.startAddr,
           detail->info.endAddr,
           detail->disasm.size());

    if (detail->disasm.empty()) {
        fprintf(stderr, "[ERROR] No disassembly returned from Ghidra\n");
        return 1;
    }

    // Lift to IR
    printf("[LIFTER TEST] Lifting to IR...\n");

    IRLifter lifter;
    lifter.setEmitComments(true);  // Include source disassembly as comments
    lifter.setFoldDelaySlots(true); // Automatically fold delay slots before branches!
    auto irFuncOpt = lifter.liftFunction(detail->info, detail->disasm);

    if (!irFuncOpt.has_value()) {
        fprintf(stderr, "[ERROR] liftFunction returned nullopt (empty disassembly?)\n");
        return 1;
    }

    auto& irFunc = irFuncOpt.value();
    printf("[OK] Lifting complete.\n\n");

    // Print stats
    const auto& st = lifter.stats();
    printf("=== Lifter Statistics ===\n");
    printf("  Total MIPS instructions: %u\n", st.totalInstructions);
    printf("  Lifted to IR:            %u\n", st.liftedInstructions);
    printf("  Skipped NOPs:            %u\n", st.skippedNops);
    printf("  Unhandled mnemonics:     %u\n", st.unhandledMnemonics);
    printf("  Basic blocks:            %u\n", st.basicBlocks);
    printf("  Branches lifted:         %u\n", st.branchesLifted);
    printf("  Calls lifted:            %u\n", st.callsLifted);
    printf("  Delay slots folded:      %u\n", st.delaySlotsFolded);
    printf("  Coverage:                %.1f%%\n", st.coveragePercent());
    printf("=========================\n\n");

    // Print the full IR tree
    printIRFunction(irFunc);

    // Sprint 3 - Phase 2: Smoke test CppEmitter
    printf("[LIFTER TEST] Emitting C++ Code...\n");
    CppEmitter emitter;
    std::string cppCode = emitter.emitFunction(irFunc);
    
    printf("========================================\n");
    printf("C++ Output:\n");
    printf("========================================\n");
    printf("%s\n", cppCode.c_str());

    printf("[LIFTER TEST] Done.\n");
    return 0;
}
