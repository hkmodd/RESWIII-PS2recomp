// ============================================================================
// ghidra_bridge.cpp — HTTP client for the GhydraMCP REST API
// Part of PS2reAIcomp — Sprint 1: Ghidra Bridge & Jump Resolver
// ============================================================================

// cpp-httplib requires these on Windows before inclusion
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#endif

#include "ps2recomp/ghidra_bridge.h"

// Third-party headers (header-only)
#include "third_party/httplib.h"
#include "third_party/json.hpp"

#include <sstream>
#include <iomanip>
#include <algorithm>
#include <stdexcept>
#include <cstdio>

using json = nlohmann::json;

namespace ps2recomp {

// ── PIMPL for httplib::Client ───────────────────────────────────────────────
struct GhidraBridge::Impl {
    std::unique_ptr<httplib::Client> client;
};

// ── Helpers ─────────────────────────────────────────────────────────────────

uint32_t GhidraBridge::parseHexAddr(const std::string& s) {
    if (s.empty()) return 0;
    // Handle "0x" prefix if present
    const char* p = s.c_str();
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        p += 2;
    return static_cast<uint32_t>(std::strtoul(p, nullptr, 16));
}

uint32_t GhidraBridge::parseHexBytes(const std::string& hexStr) {
    // "280C0070" → 0x280C0070 (big-endian as stored in the hex string)
    if (hexStr.size() != 8) return 0;
    return static_cast<uint32_t>(std::strtoul(hexStr.c_str(), nullptr, 16));
}

std::vector<uint8_t> GhidraBridge::decodeHexString(const std::string& hex) {
    std::vector<uint8_t> result;
    result.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        uint8_t byte = static_cast<uint8_t>(
            std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16));
        result.push_back(byte);
    }
    return result;
}

static std::string addrToHex(uint32_t addr) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", addr);
    return std::string(buf);
}

// ── Constructor / Destructor ────────────────────────────────────────────────

GhidraBridge::GhidraBridge(const std::string& host, int port)
    : host_(host), port_(port), impl_(std::make_unique<Impl>())
{
}

GhidraBridge::~GhidraBridge() = default;

// ── HTTP Layer ──────────────────────────────────────────────────────────────

std::string GhidraBridge::httpGet(const std::string& path) {
    if (!impl_->client) {
        throw std::runtime_error("GhidraBridge: not connected");
    }

    ++requestCount_;
    auto res = impl_->client->Get(path);

    if (!res) {
        throw std::runtime_error("GhidraBridge: HTTP request failed for " +
                                 path + " (error: " +
                                 httplib::to_string(res.error()) + ")");
    }

    if (res->status != 200) {
        throw std::runtime_error("GhidraBridge: HTTP " +
                                 std::to_string(res->status) + " for " + path);
    }

    return res->body;
}

// ── Connection ──────────────────────────────────────────────────────────────

bool GhidraBridge::connect() {
    try {
        impl_->client = std::make_unique<httplib::Client>(host_, port_);
        impl_->client->set_connection_timeout(5);   // 5 seconds
        impl_->client->set_read_timeout(30);         // 30 seconds for large responses

        // Hit the root endpoint to validate connection
        auto body = httpGet("/");
        auto j = json::parse(body);

        if (!j.value("success", false)) {
            connected_ = false;
            return false;
        }

        // Extract program/project info from _links
        if (j.contains("_links")) {
            auto& links = j["_links"];
            if (links.contains("project") && links["project"].contains("href")) {
                std::string projHref = links["project"]["href"];
                // "/projects/SLES-53155" → "SLES-53155"
                auto pos = projHref.rfind('/');
                if (pos != std::string::npos)
                    projectName_ = projHref.substr(pos + 1);
            }
        }

        // Extract program name from /program endpoint
        try {
            auto progBody = httpGet("/program");
            auto progJson = json::parse(progBody);
            if (progJson.contains("result") && progJson["result"].contains("name"))
                programName_ = progJson["result"]["name"].get<std::string>();
        } catch (...) {
            // Non-fatal — just means we can't get the program name
        }

        connected_ = true;
        
        try {
            auto segs = fetchSegments();
            uint32_t minAddr = 0xFFFFFFFF;
            uint32_t maxAddr = 0;
            for (const auto& s : segs) {
                if (s.startAddr < minAddr) minAddr = s.startAddr;
                if (s.endAddr > maxAddr) maxAddr = s.endAddr;
            }
            printf("[GHIDRA BRIDGE] Connected to program: %s\n", programName_.c_str());
            if (segs.empty()) {
                printf("[GHIDRA BRIDGE] Total memory range / Segments: 0 segments found.\n");
            } else {
                printf("[GHIDRA BRIDGE] Total memory range / Segments: %zu segments, range 0x%08X - 0x%08X\n", segs.size(), minAddr, maxAddr);
            }
        } catch (...) {
            printf("[GHIDRA BRIDGE] Connected to program: %s\n", programName_.c_str());
            printf("[GHIDRA BRIDGE] Total memory range / Segments: failed to fetch segments.\n");
        }

        return true;
    } catch (const std::exception& e) {
        connected_ = false;
        return false;
    }
}

std::string GhidraBridge::status() const {
    if (!connected_) return "disconnected";
    std::string s = "connected to " + host_ + ":" + std::to_string(port_);
    if (!projectName_.empty()) s += " [" + projectName_ + "]";
    if (!programName_.empty()) s += " program=" + programName_;
    if (totalFunctionCount_ > 0)
        s += " functions=" + std::to_string(totalFunctionCount_);
    s += " requests=" + std::to_string(requestCount_);
    return s;
}

// ── Segments ────────────────────────────────────────────────────────────────

std::vector<GhidraSegment> GhidraBridge::fetchSegments() {
    auto body = httpGet("/segments?limit=100");
    auto j = json::parse(body);

    std::vector<GhidraSegment> result;
    if (!j.contains("result") || !j["result"].is_array()) return result;

    for (auto& seg : j["result"]) {
        GhidraSegment s;
        s.name        = seg.value("name", "");

        // Some segments have non-numeric addresses (e.g. ".comment::00000000")
        std::string startStr = seg.value("start", "");
        std::string endStr   = seg.value("end", "");
        if (startStr.find("::") != std::string::npos) continue; // skip metadata segments

        s.startAddr   = parseHexAddr(startStr);
        s.endAddr     = parseHexAddr(endStr);
        s.size        = seg.value("size", 0u);
        s.readable    = seg.value("readable", false);
        s.writable    = seg.value("writable", false);
        s.executable  = seg.value("executable", false);
        s.initialized = seg.value("initialized", false);

        result.push_back(std::move(s));
    }
    return result;
}

// ── Function Listing (Paginated) ────────────────────────────────────────────

std::vector<GhidraFunction> GhidraBridge::fetchAllFunctions(
    bool computeEndAddr, ProgressCallback progress)
{
    std::vector<GhidraFunction> allFuncs;
    constexpr uint32_t PAGE_SIZE = 100;
    uint32_t offset = 0;
    uint32_t total = 0;

    // Phase 1: Paginated fetch of all function addresses + names
    do {
        std::string path = "/functions?offset=" + std::to_string(offset) +
                           "&limit=" + std::to_string(PAGE_SIZE);
        auto body = httpGet(path);
        auto j = json::parse(body);

        if (!j.value("success", false)) break;

        // Get total count from first page
        if (offset == 0) {
            total = j.value("size", 0u);
            totalFunctionCount_ = total;
            allFuncs.reserve(total);
        }

        if (!j.contains("result") || !j["result"].is_array()) break;
        auto& arr = j["result"];
        if (arr.empty()) break;

        for (auto& fn : arr) {
            GhidraFunction f;
            f.startAddr = parseHexAddr(fn.value("address", ""));
            f.name      = fn.value("name", "");
            allFuncs.push_back(std::move(f));
        }

        offset += PAGE_SIZE;

        if (progress) {
            progress(static_cast<uint32_t>(allFuncs.size()), total);
        }

    } while (offset < total);

    // Phase 2: Optionally compute endAddr from disassembly metadata
    if (computeEndAddr && !allFuncs.empty()) {
        // Sort by address for efficient boundary computation
        std::sort(allFuncs.begin(), allFuncs.end(),
                  [](const GhidraFunction& a, const GhidraFunction& b) {
                      return a.startAddr < b.startAddr;
                  });

        for (size_t i = 0; i < allFuncs.size(); ++i) {
            auto& f = allFuncs[i];

            // Query disassembly with limit=1 to just get totalInstructions
            // Then fetch the LAST instruction to get endAddr
            try {
                std::string addr = addrToHex(f.startAddr);
                std::string path = "/functions/" + addr +
                                   "/disassembly?offset=0&limit=1";
                auto body = httpGet(path);
                auto j = json::parse(body);

                if (j.value("success", false) && j.contains("result")) {
                    auto& res = j["result"];
                    uint32_t totalInsns = res.value("totalInstructions", 0u);

                    if (totalInsns > 0) {
                        // Fetch the last instruction
                        std::string lastPath = "/functions/" + addr +
                                               "/disassembly?offset=" +
                                               std::to_string(totalInsns - 1) +
                                               "&limit=1";
                        auto lastBody = httpGet(lastPath);
                        auto lastJ = json::parse(lastBody);

                        if (lastJ.value("success", false) &&
                            lastJ.contains("result") &&
                            lastJ["result"].contains("instructions"))
                        {
                            auto& insns = lastJ["result"]["instructions"];
                            if (!insns.empty()) {
                                uint32_t lastAddr = parseHexAddr(
                                    insns[0].value("address", ""));
                                f.endAddr = lastAddr + 4; // MIPS fixed-width

                                // Detect thunks: 1-2 instructions, first is 'j'
                                if (totalInsns <= 2) {
                                    // Fetch the first instruction to check
                                    if (res.contains("instructions") &&
                                        !res["instructions"].empty())
                                    {
                                        std::string mn =
                                            res["instructions"][0].value(
                                                "mnemonic", "");
                                        if (mn == "j") f.isThunk = true;
                                    }
                                }
                            }
                        }
                    }

                    // Also grab signature if available
                    if (res.contains("function")) {
                        f.signature = res["function"].value("signature", "");
                    }
                }
            } catch (const std::exception&) {
                // Non-fatal: endAddr stays 0 for this function
                // Will be estimated from next function's startAddr
            }

            if (progress && (i % 25 == 0 || i == allFuncs.size() - 1)) {
                progress(static_cast<uint32_t>(i + 1), total);
            }
        }

        // Phase 3: Fill in any remaining endAddr gaps using next function boundary
        for (size_t i = 0; i < allFuncs.size(); ++i) {
            if (allFuncs[i].endAddr == 0 && i + 1 < allFuncs.size()) {
                allFuncs[i].endAddr = allFuncs[i + 1].startAddr;
            }
        }
    }

    return allFuncs;
}

// ── Single Function ─────────────────────────────────────────────────────────

std::optional<GhidraFunction> GhidraBridge::fetchFunction(uint32_t addr) {
    try {
        std::string path = "/function?address=0x" + addrToHex(addr);
        auto body = httpGet(path);
        auto j = json::parse(body);

        if (!j.value("success", false) || !j.contains("result"))
            return std::nullopt;

        auto& res = j["result"];
        GhidraFunction f;
        f.startAddr  = parseHexAddr(res.value("address", ""));
        f.name       = res.value("name", "");
        f.signature  = res.value("signature", "");
        f.returnType = res.value("returnType", "");

        return f;
    } catch (...) {
        return std::nullopt;
    }
}

// ── Disassembly ─────────────────────────────────────────────────────────────

std::vector<GhidraInstruction> GhidraBridge::fetchDisassembly(
    uint32_t addr, uint32_t limit)
{
    std::vector<GhidraInstruction> result;
    std::string addrStr = addrToHex(addr);
    uint32_t offset = 0;
    uint32_t pageSize = (limit > 0 && limit < 200) ? limit : 200;

    do {
        std::string path = "/functions/" + addrStr +
                           "/disassembly?offset=" + std::to_string(offset) +
                           "&limit=" + std::to_string(pageSize);
        auto body = httpGet(path);
        auto j = json::parse(body);

        if (!j.value("success", false) || !j.contains("result"))
            break;

        auto& res = j["result"];
        if (!res.contains("instructions") || !res["instructions"].is_array())
            break;

        auto& insns = res["instructions"];
        if (insns.empty()) break;

        for (auto& ins : insns) {
            GhidraInstruction gi;
            gi.addr     = parseHexAddr(ins.value("address", ""));
            gi.rawBytes = parseHexBytes(ins.value("bytes", ""));
            gi.mnemonic = ins.value("mnemonic", "");
            gi.operands = ins.value("operands", "");
            result.push_back(std::move(gi));
        }

        uint32_t totalInsns = res.value("totalInstructions", 0u);
        offset += pageSize;

        // Stop if we've fetched enough or reached the end
        if (limit > 0 && result.size() >= limit) break;
        if (offset >= totalInsns) break;

    } while (true);

    // Trim if we fetched more than requested
    if (limit > 0 && result.size() > limit) {
        result.resize(limit);
    }

    return result;
}

// ── Function Detail (Composite) ─────────────────────────────────────────────

std::optional<GhidraFunctionDetail> GhidraBridge::fetchFunctionDetail(
    uint32_t addr)
{
    auto funcOpt = fetchFunction(addr);
    if (!funcOpt) return std::nullopt;

    GhidraFunctionDetail detail;
    detail.info = *funcOpt;

    // Fetch full disassembly
    detail.disasm = fetchDisassembly(addr);
    detail.totalInstructions = static_cast<uint32_t>(detail.disasm.size());

    // Compute endAddr from last instruction
    if (!detail.disasm.empty()) {
        detail.info.endAddr = detail.disasm.back().addr + 4;

        // Detect thunks
        if (detail.disasm.size() <= 2 && detail.disasm[0].isJ()) {
            detail.info.isThunk = true;
        }
    }

    // Fetch cross-references
    detail.xrefsFrom = fetchXRefsFrom(addr);
    detail.xrefsTo   = fetchXRefsTo(addr);

    return detail;
}

// ── Cross-References ────────────────────────────────────────────────────────

static std::vector<GhidraXRef> parseXRefs(const json& j) {
    std::vector<GhidraXRef> result;
    if (!j.value("success", false) || !j.contains("result"))
        return result;

    auto& res = j["result"];
    if (!res.contains("references") || !res["references"].is_array())
        return result;

    for (auto& ref : res["references"]) {
        GhidraXRef x;
        x.fromAddr        = GhidraBridge::parseHexAddr(
                                ref.value("from_addr", ""));
        x.toAddr          = GhidraBridge::parseHexAddr(
                                ref.value("to_addr", ""));
        x.type            = GhidraXRef::parseRefType(
                                ref.value("refType", ""));
        x.fromInstruction = ref.value("from_instruction", "");
        x.toSymbol        = ref.value("to_symbol", "");
        result.push_back(std::move(x));
    }
    return result;
}

std::vector<GhidraXRef> GhidraBridge::fetchXRefsTo(uint32_t addr) {
    std::vector<GhidraXRef> allRefs;
    uint32_t offset = 0;
    constexpr uint32_t PAGE_SIZE = 100;

    do {
        std::string path = "/xrefs?to_addr=" + addrToHex(addr) +
                           "&offset=" + std::to_string(offset) +
                           "&limit=" + std::to_string(PAGE_SIZE);
        auto body = httpGet(path);
        auto j = json::parse(body);

        auto refs = parseXRefs(j);
        if (refs.empty()) break;

        allRefs.insert(allRefs.end(), refs.begin(), refs.end());

        uint32_t total = j.value("size", 0u);
        offset += PAGE_SIZE;
        if (offset >= total) break;
    } while (true);

    return allRefs;
}

std::vector<GhidraXRef> GhidraBridge::fetchXRefsFrom(uint32_t addr) {
    std::vector<GhidraXRef> allRefs;
    uint32_t offset = 0;
    constexpr uint32_t PAGE_SIZE = 100;

    do {
        std::string path = "/xrefs?from_addr=" + addrToHex(addr) +
                           "&offset=" + std::to_string(offset) +
                           "&limit=" + std::to_string(PAGE_SIZE);
        auto body = httpGet(path);
        auto j = json::parse(body);

        auto refs = parseXRefs(j);
        if (refs.empty()) break;

        allRefs.insert(allRefs.end(), refs.begin(), refs.end());

        uint32_t total = j.value("size", 0u);
        offset += PAGE_SIZE;
        if (offset >= total) break;
    } while (true);

    return allRefs;
}

// ── Memory ──────────────────────────────────────────────────────────────────

std::vector<uint8_t> GhidraBridge::readMemory(uint32_t addr, uint32_t length) {
    std::string path = "/memory?address=" + addrToHex(addr) +
                       "&length=" + std::to_string(length);
    auto body = httpGet(path);
    auto j = json::parse(body);

    if (!j.value("success", false) || !j.contains("result"))
        return {};

    auto& res = j["result"];
    std::string hexBytes = res.value("hexBytes", "");
    return decodeHexString(hexBytes);
}

} // namespace ps2recomp
