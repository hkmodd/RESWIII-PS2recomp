// ============================================================================
// test_ghidra_bridge.cpp — Integration test for GhidraBridge + JumpResolver
// Part of PS2reAIcomp — Sprint 1
//
// Requires a running Ghidra instance with GhydraMCP on localhost:8193
// (or the port specified by GHIDRA_PORT environment variable).
//
// Usage:
//   test_ghidra_bridge [port]
// ============================================================================

#include "ps2recomp/ghidra_bridge.h"
#include "ps2recomp/jump_resolver.h"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace ps2recomp;

static int failures = 0;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "  FAIL: %s\n", msg); \
            ++failures; \
        } else { \
            std::printf("  PASS: %s\n", msg); \
        } \
    } while(0)

int main(int argc, char* argv[]) {
    // Determine port
    int port = 8193;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    } else {
        const char* envPort = std::getenv("GHIDRA_PORT");
        if (envPort) port = std::atoi(envPort);
    }

    std::printf("=== GhidraBridge Integration Test ===\n");
    std::printf("Connecting to localhost:%d ...\n\n", port);

    // ── Test 1: Connection ──────────────────────────────────────────────
    std::printf("[Test 1] Connection\n");
    GhidraBridge bridge("localhost", port);
    bool connected = bridge.connect();
    CHECK(connected, "connect() succeeds");
    CHECK(bridge.isConnected(), "isConnected() returns true");
    std::printf("  Status: %s\n\n", bridge.status().c_str());

    if (!connected) {
        std::fprintf(stderr, "\nCannot continue without connection. "
                             "Is Ghidra+GhydraMCP running on port %d?\n", port);
        return 1;
    }

    // ── Test 2: Segments ────────────────────────────────────────────────
    std::printf("[Test 2] Segments\n");
    auto segments = bridge.fetchSegments();
    CHECK(!segments.empty(), "fetchSegments() returns non-empty");
    std::printf("  Found %zu segments:\n", segments.size());
    for (auto& seg : segments) {
        std::printf("    %-12s [%08X-%08X] size=%u %s%s%s\n",
                    seg.name.c_str(), seg.startAddr, seg.endAddr, seg.size,
                    seg.readable ? "R" : "-",
                    seg.writable ? "W" : "-",
                    seg.executable ? "X" : "-");
    }
    std::printf("\n");

    // ── Test 3: Function List (fast, no endAddr) ────────────────────────
    std::printf("[Test 3] Function List (fast mode)\n");
    auto funcs = bridge.fetchAllFunctions(false, [](uint32_t cur, uint32_t total) {
        std::printf("  ... fetched %u / %u functions\r", cur, total);
    });
    std::printf("\n");
    CHECK(!funcs.empty(), "fetchAllFunctions() returns non-empty");
    CHECK(funcs.size() > 100, "more than 100 functions found");
    std::printf("  Total functions: %zu\n", funcs.size());
    if (funcs.size() >= 3) {
        std::printf("  First 3:\n");
        for (size_t i = 0; i < 3; ++i) {
            std::printf("    [%08X] %s\n", funcs[i].startAddr,
                        funcs[i].name.c_str());
        }
    }
    std::printf("\n");

    // ── Test 4: Single Function Detail ──────────────────────────────────
    std::printf("[Test 4] Function Detail (entry point)\n");
    // Use the first non-thunk function
    uint32_t testAddr = 0;
    for (auto& f : funcs) {
        if (!f.name.empty() && f.name.find("entry") != std::string::npos) {
            testAddr = f.startAddr;
            break;
        }
    }
    if (testAddr == 0 && !funcs.empty()) testAddr = funcs[0].startAddr;

    auto detail = bridge.fetchFunctionDetail(testAddr);
    CHECK(detail.has_value(), "fetchFunctionDetail() returns a value");
    if (detail) {
        std::printf("  Name:       %s\n", detail->info.name.c_str());
        std::printf("  Signature:  %s\n", detail->info.signature.c_str());
        std::printf("  Range:      [%08X-%08X] (%u bytes)\n",
                    detail->info.startAddr, detail->info.endAddr,
                    detail->info.size());
        std::printf("  Insns:      %u\n", detail->totalInstructions);
        std::printf("  XRefs from: %zu\n", detail->xrefsFrom.size());
        std::printf("  XRefs to:   %zu\n", detail->xrefsTo.size());
        std::printf("  Is thunk:   %s\n", detail->info.isThunk ? "yes" : "no");

        CHECK(detail->totalInstructions > 0, "has disassembly");
        CHECK(detail->info.endAddr > detail->info.startAddr,
              "endAddr > startAddr");

        if (!detail->disasm.empty()) {
            std::printf("  First insn: [%08X] %s %s\n",
                        detail->disasm[0].addr,
                        detail->disasm[0].mnemonic.c_str(),
                        detail->disasm[0].operands.c_str());
        }
    }
    std::printf("\n");

    // ── Test 5: Memory Read ─────────────────────────────────────────────
    std::printf("[Test 5] Memory Read\n");
    auto mem = bridge.readMemory(testAddr, 16);
    CHECK(mem.size() == 16, "readMemory returns 16 bytes");
    if (!mem.empty()) {
        std::printf("  Bytes at %08X:", testAddr);
        for (auto b : mem) std::printf(" %02X", b);
        std::printf("\n");
    }
    std::printf("\n");

    // ── Test 6: Cross-References ────────────────────────────────────────
    std::printf("[Test 6] Cross-References\n");
    auto xrefsTo = bridge.fetchXRefsTo(testAddr);
    std::printf("  XRefs TO %08X: %zu\n", testAddr, xrefsTo.size());
    for (size_t i = 0; i < std::min(xrefsTo.size(), size_t(5)); ++i) {
        std::printf("    from=%08X type=%s insn=\"%s\"\n",
                    xrefsTo[i].fromAddr,
                    GhidraXRef::typeToString(xrefsTo[i].type),
                    xrefsTo[i].fromInstruction.c_str());
    }

    auto xrefsFrom = bridge.fetchXRefsFrom(testAddr);
    std::printf("  XRefs FROM %08X: %zu\n", testAddr, xrefsFrom.size());
    for (size_t i = 0; i < std::min(xrefsFrom.size(), size_t(5)); ++i) {
        std::printf("    to=%08X type=%s sym=\"%s\"\n",
                    xrefsFrom[i].toAddr,
                    GhidraXRef::typeToString(xrefsFrom[i].type),
                    xrefsFrom[i].toSymbol.c_str());
    }
    std::printf("\n");

    // ── Test 7: Jump Resolver (quick scan) ──────────────────────────────
    std::printf("[Test 7] Jump Resolver\n");
    JumpResolver resolver(bridge);
    uint32_t jrScanned = 0;

    // Scan first 20 functions for jump tables
    size_t scanLimit = std::min(funcs.size(), size_t(20));
    for (size_t i = 0; i < scanLimit; ++i) {
        auto fd = bridge.fetchFunctionDetail(funcs[i].startAddr);
        if (!fd) continue;

        auto tables = resolver.resolveFunction(*fd);
        jrScanned += static_cast<uint32_t>(fd->disasm.size());

        for (auto& table : tables) {
            std::printf("  RESOLVED jump table at %08X:\n", table.jrAddr);
            std::printf("    Table addr: %08X, entries: %u, reg: %s\n",
                        table.tableStart, table.entryCount,
                        table.regName.c_str());
            for (size_t j = 0; j < std::min(table.entries.size(), size_t(8)); ++j) {
                std::printf("      case[%u] -> %08X\n",
                            table.entries[j].caseIndex,
                            table.entries[j].targetAddr);
            }
            if (table.entryCount > 8)
                std::printf("      ... (%u more)\n", table.entryCount - 8);
        }
    }
    std::printf("  Scanned %u instructions across %zu functions\n",
                jrScanned, scanLimit);
    std::printf("  Resolved: %u, Failed: %u\n",
                resolver.resolvedCount(), resolver.failedCount());
    std::printf("\n");

    // ── Summary ─────────────────────────────────────────────────────────
    std::printf("=== Summary ===\n");
    std::printf("  Total HTTP requests: %u\n", bridge.requestCount());
    std::printf("  Results: %d failures\n", failures);

    return failures > 0 ? 1 : 0;
}
