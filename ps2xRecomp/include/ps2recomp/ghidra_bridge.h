// ============================================================================
// ghidra_bridge.h — HTTP client for the GhydraMCP REST API
// Part of PS2reAIcomp — Sprint 1: Ghidra Bridge & Jump Resolver
// ============================================================================
#pragma once

#include "ghidra_types.h"
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <memory>
#include <cstdint>

namespace ps2recomp {

// ── GhidraBridge ────────────────────────────────────────────────────────────
// Wraps the GhydraMCP REST API (HTTP/JSON) to extract function boundaries,
// disassembly, cross-references, segments, and memory from a live Ghidra
// instance running with the R5900 Emotion Engine plugin.
//
// Usage:
//   GhidraBridge bridge("localhost", 8193);
//   if (!bridge.connect()) { /* error */ }
//   auto functions = bridge.fetchAllFunctions();
//
class GhidraBridge {
public:
    // Progress callback: (currentCount, totalCount)
    using ProgressCallback = std::function<void(uint32_t, uint32_t)>;

    explicit GhidraBridge(const std::string& host = "localhost",
                          int port = 8192);
    ~GhidraBridge();

    // ── Connection ──────────────────────────────────────────────────────
    // Connects to Ghidra and validates the GhydraMCP API is responding.
    bool connect();
    bool isConnected() const { return connected_; }
    std::string status() const;
    std::string programName() const { return programName_; }
    std::string projectName() const { return projectName_; }

    // ── Bulk Fetch ──────────────────────────────────────────────────────
    // Fetches ALL functions with automatic pagination (limit=100 per page).
    // Computes endAddr for each function from the last disassembled
    // instruction's address + 4 (MIPS fixed-width).
    // Set computeEndAddr=false for a fast list without disassembly queries.
    std::vector<GhidraFunction> fetchAllFunctions(
        bool computeEndAddr = true,
        ProgressCallback progress = nullptr);

    // Fetches all memory segments.
    std::vector<GhidraSegment> fetchSegments();

    // ── Per-Function Queries ────────────────────────────────────────────
    // Gets detailed function info (signature, return type).
    std::optional<GhidraFunction> fetchFunction(uint32_t addr);

    // Gets composite detail: info + disassembly + xrefs.
    std::optional<GhidraFunctionDetail> fetchFunctionDetail(uint32_t addr);

    // Gets raw disassembly for a function.
    std::vector<GhidraInstruction> fetchDisassembly(uint32_t addr,
                                                     uint32_t limit = 0);

    // ── Cross-References ────────────────────────────────────────────────
    std::vector<GhidraXRef> fetchXRefsTo(uint32_t addr);
    std::vector<GhidraXRef> fetchXRefsFrom(uint32_t addr);

    // ── Memory ──────────────────────────────────────────────────────────
    std::vector<uint8_t> readMemory(uint32_t addr, uint32_t length);

    // ── Statistics ───────────────────────────────────────────────────────
    uint32_t totalFunctionCount() const { return totalFunctionCount_; }
    uint32_t requestCount() const { return requestCount_; }

    // ── Utility (public for use by xref parsers) ─────────────────────────
    static uint32_t parseHexAddr(const std::string& s);
    static uint32_t parseHexBytes(const std::string& s);

private:
    std::string host_;
    int         port_;
    bool        connected_       = false;
    std::string programName_;
    std::string projectName_;
    uint32_t    totalFunctionCount_ = 0;
    uint32_t    requestCount_    = 0;

    // Internal HTTP helpers
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::string httpGet(const std::string& path);
    static std::vector<uint8_t> decodeHexString(const std::string& hex);
};

} // namespace ps2recomp
