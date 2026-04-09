#include "game_overrides.h"
#include "ps2_syscalls.h"
#include "ps2_runtime.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <string>
#include <cctype>

namespace
{

    extern "C" void ps2_FUN_0067c890_67c890(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime);

    void StarWarsSifOverrides(PS2Runtime &runtime)
    {
        // ────────────────────────────────────────────────────
        //  PRELOAD COREC.BIN AND CORED.BIN
        // ────────────────────────────────────────────────────
        {
            auto loadModule = [&](const std::string& name, uint32_t addr) {
                auto path = PS2Runtime::getIoPaths().elfDirectory / name;
                std::ifstream file(path, std::ios::binary | std::ios::ate);
                if (file.is_open()) {
                    std::streamsize size = file.tellg();
                    file.seekg(0, std::ios::beg);
                    std::vector<char> buffer(size);
                    if (file.read(buffer.data(), size)) {
                        for (size_t i = 0; i < size; ++i) {
                            runtime.memory().write8(addr + i, buffer[i]);
                        }
                        std::cerr << "[PRELOAD] Loaded " << name << " (" << size << " bytes) to 0x" << std::hex << addr << std::dec << std::endl;
                    }
                } else {
                    std::cerr << "[PRELOAD] Error opening " << path << std::endl;
                }
            };
            
            // Standard loading
            loadModule("corec.bin", 0x001c2680);
            loadModule("cored.bin", 0x009bc400);

            // GHOST MAPPINGS for unrelocated memory accesses
            // The static recompiler left `lui v1, 0xa7` for corec.bin which compiles to 0xA70000.
            // 0xA70000 corresponds to a base address of 0x5C2680.
            loadModule("corec.bin", 0x005c2680);
            
            // Assuming cored.bin static base is also offset by exactly 0x400000...
            loadModule("cored.bin", 0x009bc400 + 0x400000);
        }
        // ────────────────────────────────────────────────────
        //  FIX #1: Bulk asset-buffer allocator (FUN_0012e810)
        //
        //  On real PS2 (32MB RAM): the game requests malloc(64MB),
        //  fails, then loops subtracting 1KB until it fits (~21MB).
        //  In our runtime, the 64MB request may succeed (since we
        //  have 128MB RDRAM) OR it may cause pathological dlmalloc
        //  behavior. We cap at 22MB to match real PS2 behavior.
        //
        //  Original pseudocode:
        //    if (first_time) {
        //        temp = malloc(0x2800);   // 10KB scratch
        //        first_time = false;
        //        s0 = 0x4000000;          // start at 64MB
        //        buf = malloc(s0);
        //        while (buf == 0) { s0 -= 0x400; buf = malloc(s0); }
        //        global_buf_start = buf;
        //        global_buf_end   = buf + s0;
        //        if (temp) free(temp);
        //    }
        //    *param_1 = global_buf_start;
        //    *param_2 = global_buf_end;
        // ────────────────────────────────────────────────────
        static auto overrideBulkAlloc = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
            // a0 = param_1 (pointer to receive buf_start)
            // a1 = param_2 (pointer to receive buf_end)
            uint32_t param1Addr = getRegU32(ctx, 4); // s3 in original
            uint32_t param2Addr = getRegU32(ctx, 5); // s2 in original

            // Addresses from Ghidra decompilation:
            // DAT_001af51c = first_time flag (at gp-0x7FD4 where gp=0x1b74f0: 0x1b74f0-0x7fd4=0x1af51c)
            // iRam001afa48 = global_buf_start (at gp-0x7AA8: 0x1b74f0-0x7aa8=0x1afa48)
            // iRam001afa4c = global_buf_end   (at gp-0x7AA4: 0x1b74f0-0x7aa4=0x1afa4c)
            const uint32_t flagAddr     = 0x1af51c;
            const uint32_t bufStartAddr = 0x1afa48;
            const uint32_t bufEndAddr   = 0x1afa4c;

            uint8_t firstTime = rt->memory().read8(flagAddr);

            if (firstTime != 0) {
                // Clear the flag
                rt->memory().write8(flagAddr, 0);

                // Allocate the bulk buffer via the game's own malloc (FUN_001054c8)
                // We call it by dispatching to 0x1054c8 with the size in a0.
                // BUT calling recompiled functions from an override is tricky.
                //
                // Instead, use guestMalloc from the runtime:
                uint32_t desiredSize = 0x01600000u; // ~22MB (PS2 real: ~21.3MB)
                uint32_t bufAddr = rt->guestMalloc(desiredSize, 16);

                if (bufAddr == 0) {
                    // Try smaller sizes
                    for (desiredSize = 0x01400000u; desiredSize >= 0x100000u; desiredSize -= 0x100000u) {
                        bufAddr = rt->guestMalloc(desiredSize, 16);
                        if (bufAddr != 0) break;
                    }
                }

                if (bufAddr != 0) {
                    rt->memory().write32(bufStartAddr, bufAddr);
                    rt->memory().write32(bufEndAddr, bufAddr + desiredSize);
                    std::cerr << "[BULK] Allocated " << (desiredSize / (1024*1024)) << "MB at 0x"
                              << std::hex << bufAddr << " end=0x" << (bufAddr + desiredSize)
                              << std::dec << std::endl;
                } else {
                    // Even 1MB failed — give it a static region in high RDRAM
                    uint32_t fallbackBase = 0x4000000u; // 64MB mark (well within 128MB RDRAM)
                    uint32_t fallbackSize = 0x01600000u; // 22MB
                    rt->memory().write32(bufStartAddr, fallbackBase);
                    rt->memory().write32(bufEndAddr, fallbackBase + fallbackSize);
                    std::cerr << "[BULK] FALLBACK: Using fixed region at 0x"
                              << std::hex << fallbackBase << " size=0x" << fallbackSize
                              << std::dec << std::endl;
                }
            }

            // Output: *param_1 = buf_start, *param_2 = buf_end
            uint32_t bufStart = rt->memory().read32(bufStartAddr);
            uint32_t bufEnd   = rt->memory().read32(bufEndAddr);
            rt->memory().write32(param1Addr, bufStart);
            rt->memory().write32(param2Addr, bufEnd);

            std::cerr << "[BULK] Result: start=0x" << std::hex << bufStart
                      << " end=0x" << bufEnd << " size=0x" << (bufEnd - bufStart)
                      << std::dec << " (" << ((bufEnd - bufStart) / (1024*1024)) << "MB)" << std::endl;

            // Also call the debug print functions (FUN_0012e7c0) that the original does:
            // FUN_0012e7c0(0x136aa5, *param_1) and FUN_0012e7c0(0x136abd, *param_2)
            // Skip these as they're just debug prints.

            char* defaultDev = (char*)(rdram + 0x92a690);
            if (defaultDev) {
                std::strcpy(defaultDev, "cdrom");
            }

            ctx->pc = getRegU32(ctx, 31); // return via ra
        };
        runtime.registerFunction(0x0012e810, overrideBulkAlloc);

        // Custom CDVD overrides for StarWars file loading (replaces SID=6 calls)
        static FILE* s_fake_cdvd_file = nullptr;
        static uint32_t s_fake_file_size = 0;

        // FUN_001e25a0 - Async Open
        static auto hookCdvdOpen = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            uint32_t filenameAddr = getRegU32(ctx, 4); // a0
            char* filename = (char*)(rdram + filenameAddr);
            
            std::string hostPath = "E:\\Programmi VARI\\PROGETTI\\RESWIII\\ISO extracted\\ENGINE\\";
            
            std::cerr << "[CDVD:HLE] hookCdvdOpen called with filename: '" << filename << "'" << std::endl;
            
            if (strstr(filename, "_0.PK2") || strstr(filename, "_0.pk2") || strstr(filename, "_0.Pk2")) {
                hostPath += "PS2PAK_0.pk2";
            } else if (strstr(filename, "_1.PK2") || strstr(filename, "_1.pk2") || strstr(filename, "_1.Pk2")) {
                hostPath += "PS2PAK_1.pk2";
            } else if (strstr(filename, "PS2P")) {
                hostPath += "PS2PAK.HSH";
            } else {
                const char* base = strrchr(filename, '\\');
                if (base) hostPath += (base + 1);
                else hostPath += filename;
            }

            std::cerr << "[CDVD:HLE] fake open mapped to: " << hostPath << std::endl;
            if (s_fake_cdvd_file) { fclose(s_fake_cdvd_file); }
            s_fake_cdvd_file = fopen(hostPath.c_str(), "rb");
            if (s_fake_cdvd_file) {
                fseek(s_fake_cdvd_file, 0, SEEK_END);
                s_fake_file_size = ftell(s_fake_cdvd_file);
                fseek(s_fake_cdvd_file, 0, SEEK_SET);
                setReturnS32(ctx, 1); // fake handle 1
            } else {
                std::cerr << "[CDVD:HLE] failed to open!" << std::endl;
                setReturnS32(ctx, 0); // 0 = failed
            }
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x001e25a0, hookCdvdOpen);

        // FUN_001e3618 - Get file size
        static auto hookCdvdGetSize = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            setReturnS32(ctx, s_fake_file_size);
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x001e3618, hookCdvdGetSize);

        // FUN_001e2db8 - Read file (a0=handle, a1=count_sectors, a2=buffer)
        static auto hookCdvdReadSectors = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            uint32_t handle = getRegU32(ctx, 4);      // a0
            uint32_t count = getRegU32(ctx, 5);       // a1
            uint32_t buffer_addr = getRegU32(ctx, 6); // a2
            if (s_fake_cdvd_file) {
                std::cerr << "[CDVD:HLE] Read requested: handle=" << handle 
                          << ", sectors=" << count 
                          << ", dst=" << std::hex << buffer_addr << std::dec << std::endl;
                
                size_t read_bytes = fread(rdram + buffer_addr, 1, count * 2048, s_fake_cdvd_file);
                std::cerr << "[CDVD:HLE] Read " << read_bytes << " bytes." << std::endl;
                
                // Dump first 64 bytes
                std::cerr << "[CDVD:HLE] Content preview: ";
                for (int i = 0; i < 64 && i < read_bytes; i++) {
                    char c = (char)rdram[buffer_addr + i];
                    if (c >= 32 && c <= 126) std::cerr << c;
                    else std::cerr << "\\x" << std::hex << (int)(uint8_t)c << std::dec;
                }
                std::cerr << std::endl;
                setReturnS32(ctx, count); // Return read sectors (crashed when returning 1)
            } else {
                setReturnS32(ctx, 0); // read failed
            }
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x001e2db8, hookCdvdReadSectors);

        // FUN_001e3358 - Poll Status
        static auto hookCdvdPollStatus = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            setReturnS32(ctx, 3); // 3 = COMPLETED (success)
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x001e3358, hookCdvdPollStatus);

        // FUN_001e2968 - Close
        static auto hookCdvdClose = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            if (s_fake_cdvd_file) {
                fclose(s_fake_cdvd_file);
                s_fake_cdvd_file = nullptr;
            }
            setReturnS32(ctx, 1); // success
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x001e2968, hookCdvdClose);

        // ────────────────────────────────────────────────────
        //  FIX #CDVD-FS: HLE override for sceCdSearchFile (FUN_00100920)
        //
        //  WHY: The game's CDVD filesystem uses SID 0x80000597 via
        //  sceSifCallRpc(0x1b0e80, 0, ...). Our SifCallRpc override only
        //  handled clientPtr=0x1B4DC0 (SID 0x4) and 0x9a3c20/3d40 (SID 0x6).
        //  The 0x80000597 calls fell through to the generic RPC handler which
        //  returns success but writes NO data. This caused FUN_001f4d78 to
        //  "succeed" SearchFile with an empty result, so FILELIST.DIR was never
        //  loaded, leaving the level pointer null and rendering black frames.
        //
        //  FIX: Intercept FUN_00100920 BEFORE it calls sceSifCallRpc.
        //  We look up the requested file in our extracted ISO directory and
        //  return a fake LBA + real file size in the sceCdlFILE result struct.
        // ────────────────────────────────────────────────────
        struct CdFileEntry {
            std::string hostPath;
            uint32_t size;
            uint32_t fakeLba;
        };
        static std::unordered_map<std::string, CdFileEntry> s_cdFileTable;
        static uint32_t s_nextFakeLba = 1000; // start fake LBAs at sector 1000

        // Build the file lookup table from extracted ISO files
        {
            const std::string isoRoot = "E:\\Programmi VARI\\PROGETTI\\RESWIII\\ISO extracted\\";
            struct { const char* cdPath; const char* hostFile; } fileMap[] = {
                { "\\FILELIST.DIR",            "FILELIST.DIR" },
                { "\\ENGINE\\PS2PAK.HSH",      "ENGINE\\ps2pak.hsh" },
                { "\\ENGINE\\PS2PAK_0.PK2",    "ENGINE\\ps2pak_0.pk2" },
                { "\\ENGINE\\PS2PAK_1.PK2",    "ENGINE\\ps2pak_1.pk2" },
                // The game searches for truncated names with ;1
                { "\\ENGINE\\_0.PK2;1",        "ENGINE\\ps2pak_0.pk2" },
                { "\\ENGINE\\_1.PK2;1",        "ENGINE\\ps2pak_1.pk2" },
                { "\\ENGINE\\PS2PAK.HSH;1",    "ENGINE\\ps2pak.hsh" },
                { "\\FILELIST.DIR;1",          "FILELIST.DIR" },
                { "\\ENGINE\\LEGALENG.PSI",     "ENGINE\\legaleng.psi" },
                { "\\ENGINE\\LEGALFRE.PSI",     "ENGINE\\LegalFre.psi" },
                { "\\ENGINE\\LEGALGER.PSI",     "ENGINE\\LegalGer.psi" },
                { "\\ENGINE\\LEGALITA.PSI",     "ENGINE\\LegalIta.psi" },
                { "\\ENGINE\\LEGALSPA.PSI",     "ENGINE\\LegalSpa.psi" },
                { "\\ENGINE\\LEGALDUT.PSI",     "ENGINE\\LegalDut.psi" },
                { "\\ENGINE\\LEGALPOR.PSI",     "ENGINE\\LegalPor.psi" },
                { "\\ENGINE\\LEGALJAP.PSI",     "ENGINE\\LegalJap.psi" },
            };
            for (auto& entry : fileMap) {
                std::string fullPath = isoRoot + entry.hostFile;
                FILE* f = fopen(fullPath.c_str(), "rb");
                uint32_t sz = 0;
                if (f) {
                    fseek(f, 0, SEEK_END);
                    sz = (uint32_t)ftell(f);
                    fclose(f);
                }
                // Normalize: convert to uppercase for matching
                std::string key = entry.cdPath;
                for (auto& c : key) c = toupper((unsigned char)c);
                s_cdFileTable[key] = { fullPath, sz, s_nextFakeLba };
                s_nextFakeLba += (sz + 2047) / 2048 + 1; // advance by file size in sectors
                std::cerr << "[CDVD:FS] Registered: " << key << " -> " << fullPath 
                          << " (" << sz << " bytes, LBA=" << (s_nextFakeLba - (sz + 2047) / 2048 - 1) << ")" << std::endl;
            }
        }

        // Map from fake LBA -> host file entry for streaming reads
        static auto findFileByLba = [](uint32_t lba) -> CdFileEntry* {
            for (auto& [key, entry] : s_cdFileTable) {
                if (entry.fakeLba == lba) return &entry;
            }
            return nullptr;
        };

        // Track the last SearchFile result for streaming reads
        static CdFileEntry* s_lastSearchedFile = nullptr;

        // FUN_00100920: sceCdSearchFile(result_struct*, filename, disk_type)
        // Returns: 1=found, 0=not found
        // result_struct is sceCdlFILE: { uint32_t lsn, uint32_t size, char name[32], uint8_t date[8] }
        static auto hookCdSearchFile = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            uint32_t resultAddr = getRegU32(ctx, 4);   // a0 = result struct
            uint32_t filenameAddr = getRegU32(ctx, 5); // a1 = filename
            // a2 = disk_type (ignored)

            char filename[256] = {0};
            for (int i = 0; i < 255; i++) {
                filename[i] = (char)rdram[filenameAddr + i];
                if (filename[i] == 0) break;
            }

            // Normalize to uppercase
            std::string key = filename;
            for (auto& c : key) c = toupper((unsigned char)c);

            uint32_t ra = getRegU32(ctx, 31);
            std::cerr << "[CDVD:SearchFile] Looking up: \"" << key << "\" (caller RA: 0x" << std::hex << ra << std::dec << ")" << std::endl;

            auto it = s_cdFileTable.find(key);
            if (it != s_cdFileTable.end()) {
                CdFileEntry& entry = it->second;
                s_lastSearchedFile = &entry;

                // Write sceCdlFILE struct to result
                // Offset 0: LSN (uint32)
                runtimePtr->memory().write32(resultAddr + 0, entry.fakeLba);
                // Offset 4: file size (uint32)
                runtimePtr->memory().write32(resultAddr + 4, entry.size);
                // Offset 8-23: filename (16 bytes, zero-padded)
                for (int i = 0; i < 16; i++) {
                    uint8_t c = (i < (int)key.size()) ? (uint8_t)key[i] : 0;
                    runtimePtr->memory().write8(resultAddr + 8 + i, c);
                }
                // Offset 24-31: date (8 bytes, zeroed)
                for (int i = 0; i < 8; i++) {
                    runtimePtr->memory().write8(resultAddr + 24 + i, 0);
                }

                std::cerr << "[CDVD:SearchFile] FOUND: LBA=" << entry.fakeLba
                          << " size=" << entry.size << " path=" << entry.hostPath << std::endl;

                // Signal the CDVD semaphore (DAT_001304a8, value at 0x1304a8)
                uint32_t semaId = runtimePtr->memory().read32(0x1304a8);
                if (semaId != 0) {
                    __m128i old_a0 = ctx->r[4];
                    ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(semaId)));
                    ps2_syscalls::iSignalSema(rdram, ctx, runtimePtr);
                    ctx->r[4] = old_a0;
                }

                setReturnS32(ctx, 1); // 1 = found
            } else {
                std::cerr << "[CDVD:SearchFile] NOT FOUND: " << key << std::endl;
                s_lastSearchedFile = nullptr;
                setReturnS32(ctx, 0); // 0 = not found
            }

            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x00100920, hookCdSearchFile);

        // ────────────────────────────────────────────────────
        //  FIX #CDVD-ST: HLE override for sceCdStRead (FUN_00101870)
        //
        //  WHY: The streaming read uses SID 0x80000595 via
        //  sceSifCallRpc(0x131650, 1, 1, ...) which was not handled.
        //  The RPC fell through and never delivered any data.
        //
        //  FIX: Intercept FUN_00101870. Read the file data from host FS
        //  directly into guest memory. Set completion flags so the polling
        //  loop (FUN_00100e60) sees the read as complete.
        //
        //  Params: a0=LSN, a1=num_sectors, a2=dest_buffer, a3=mode_struct
        // ────────────────────────────────────────────────────
        static auto hookCdStRead = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            uint32_t lsn = getRegU32(ctx, 4);          // a0 = LSN (from SearchFile)
            uint32_t numSectors = getRegU32(ctx, 5);   // a1 = number of sectors
            uint32_t destBuf = getRegU32(ctx, 6);      // a2 = destination buffer address
            uint32_t modeAddr = getRegU32(ctx, 7);     // a3 = pointer to mode struct

            // Read mode byte [2] to determine sector size
            uint8_t secType = (modeAddr) ? runtimePtr->memory().read8(modeAddr + 2) : 0;
            uint32_t sectorSize;
            switch (secType) {
                case 1: sectorSize = 0x918; break;  // CD-ROM mode 1: 2328 bytes 
                case 2: sectorSize = 0x924; break;  // CD-ROM mode 2: 2340 bytes
                default: sectorSize = 2048; break;  // DVD: 2048 bytes
            }

            uint32_t totalBytes = numSectors * sectorSize;

            std::cerr << "[CDVD:StRead] LSN=" << lsn << " sectors=" << numSectors
                      << " dest=0x" << std::hex << destBuf << std::dec
                      << " secType=" << (int)secType << " sectorSize=" << sectorSize
                      << " total=" << totalBytes << std::endl;

            // Find the file by LBA (from previous SearchFile)
            CdFileEntry* entry = findFileByLba(lsn);
            if (!entry && s_lastSearchedFile) {
                // Fallback: use the last searched file
                entry = s_lastSearchedFile;
                std::cerr << "[CDVD:StRead] LBA mismatch, using last searched: " << entry->hostPath << std::endl;
            }

            if (entry) {
                FILE* f = fopen(entry->hostPath.c_str(), "rb");
                if (f) {
                    // Read up to totalBytes (but no more than file size)
                    uint32_t readSize = std::min(totalBytes, entry->size);
                    
                    // Zero the buffer first
                    for (uint32_t i = 0; i < totalBytes; i++) {
                        runtimePtr->memory().write8(destBuf + i, 0);
                    }
                    
                    // Read file content
                    std::vector<uint8_t> buf(readSize);
                    size_t bytesRead = fread(buf.data(), 1, readSize, f);
                    fclose(f);

                    // Copy into guest memory
                    for (size_t i = 0; i < bytesRead; i++) {
                        runtimePtr->memory().write8(destBuf + (uint32_t)i, buf[i]);
                    }

                    std::cerr << "[CDVD:StRead] Read " << bytesRead << "/" << readSize 
                              << " bytes from " << entry->hostPath << std::endl;

                    // Dump first 64 bytes for debugging
                    std::cerr << "[CDVD:StRead] Preview: ";
                    for (size_t i = 0; i < 64 && i < bytesRead; i++) {
                        char c = (char)buf[i];
                        if (c >= 32 && c <= 126) std::cerr << c;
                        else std::cerr << "\\x" << std::hex << (int)(uint8_t)c << std::dec;
                    }
                    std::cerr << std::endl;
                } else {
                    std::cerr << "[CDVD:StRead] ERROR: Could not open " << entry->hostPath << std::endl;
                }
            } else {
                std::cerr << "[CDVD:StRead] ERROR: No file mapping for LBA " << lsn << std::endl;
            }

            // Signal completion: clear the streaming-pending flags
            runtimePtr->memory().write32(0x1304b4, 0); // DAT_001304b4 = 0 (not busy)
            runtimePtr->memory().write32(0x1304d8, 0); // DAT_001304d8 = 0 (not pending)

            // Signal the CDVD semaphore
            uint32_t semaId = runtimePtr->memory().read32(0x1304a8);
            if (semaId != 0) {
                __m128i old_a0 = ctx->r[4];
                ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(semaId)));
                ps2_syscalls::iSignalSema(rdram, ctx, runtimePtr);
                ctx->r[4] = old_a0;
            }

            setReturnS32(ctx, 1); // 1 = success
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x00101870, hookCdStRead);

        // ────────────────────────────────────────────────────
        //  FIX #CDVD-INIT: HLE override for sceCdStInit (FUN_001eb5f0)
        //  Just return success — no real IOP streaming module to init.
        static auto hookCdStInit = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            std::cerr << "[CDVD:StInit] Stub — returning success" << std::endl;
            setReturnS32(ctx, 1);
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x001eb5f0, hookCdStInit);

        // Override 0x1f0398 (VFS Get Default Device)
        static auto vfsGetDefaultDevice = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            uint32_t destAddr = getRegU32(ctx, 4); // a0
            char* dest = (char*)(rdram + destAddr);
            if (dest) {
                std::strcpy(dest, "cdrom");
                std::cerr << "[VFS Override] Forced default device to 'cdrom'\n";
            }
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x001f0398, vfsGetDefaultDevice);

        // ────────────────────────────────────────────────────
        //  Existing overrides (unchanged)
        // ────────────────────────────────────────────────────

        // sceSifInit (0x113858) - SIF initialization stub
        static auto overrideSifInitNoop = [](uint8_t*, R5900Context* ctx, PS2Runtime*) {
            ctx->pc = getRegU32(ctx, 31); // return via ra
        };
        runtime.registerFunction(0x00113858, overrideSifInitNoop);

        // sceSifAllocIopHeap (FUN_001186c0 / thunk_FUN_001186c0 at 0x12ec40)
        // WHY: The game allocates persistent IOP buffers and aborts if it gets NULL.
        // In our HLE runtime, no IOP exists — return a fake IOP-side address.
        static uint32_t s_fake_iop_heap_ptr = 0x00100000;  // Start in IOP memory range
        static auto overrideSifAllocIopHeap = [](uint8_t*, R5900Context* ctx, PS2Runtime*) {
            uint32_t size = getRegU32(ctx, 4); // a0 = requested size
            uint32_t result = s_fake_iop_heap_ptr;
            s_fake_iop_heap_ptr += (size + 0x3F) & ~0x3Fu; // align to 64 bytes
            std::cerr << "[HLE:SifAllocIopHeap] size=0x" << std::hex << size
                      << " -> 0x" << result << std::dec << std::endl;
            setReturnS32(ctx, result); // return fake IOP pointer
            ctx->pc = getRegU32(ctx, 31); // return via ra
        };
        runtime.registerFunction(0x001186c0, overrideSifAllocIopHeap);
        runtime.registerFunction(0x0012ec40, overrideSifAllocIopHeap); // thunk

        // sceSifCallRpc (0x114838)
        static auto overrideSifCallRpc = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            uint32_t clientPtr = getRegU32(ctx, 4); // a0
            uint32_t rpcNum    = getRegU32(ctx, 5); // a1
            uint32_t mode      = getRegU32(ctx, 6); // a2 
            uint32_t sendBuf   = getRegU32(ctx, 7); // a3

            // t_SifRpcClientData has rpc_id at offset 4
            uint32_t sid = runtimePtr->memory().read32(clientPtr + 4);

            // Log ALL SifCallRpc calls for diagnostics
            uint32_t sp = getRegU32(ctx, 29);
            uint32_t recvBuf  = runtimePtr->memory().read32(sp + 0x14);
            uint32_t recvSize = runtimePtr->memory().read32(sp + 0x18);
            std::cerr << "[SifCallRpc Override] client=0x" << std::hex << clientPtr
                      << " sid=0x" << sid
                      << " rpcNum=0x" << rpcNum
                      << " mode=0x" << mode
                      << " sendBuf=0x" << sendBuf
                      << " recvBuf=0x" << recvBuf
                      << " recvSize=0x" << recvSize
                      << " endFunc=0x" << getRegU32(ctx, 11) << " / " << runtimePtr->memory().read32(sp + 0x1C)
                      << std::dec << std::endl;

            // Dump a bit of sendBuf if present to help identify unknown RPCs
            if (sendBuf) {
                std::cerr << "          sendBuf data: ";
                for (int i = 0; i < 64; ++i) {
                    uint8_t b = runtimePtr->memory().read8(sendBuf + i);
                    std::cerr << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
                }
                std::cerr << "\n          sendBuf ascii: \"";
                for (int i = 0; i < 64; ++i) {
                    char c = (char)runtimePtr->memory().read8(sendBuf + i);
                    if (c >= 32 && c <= 126) std::cerr << c;
                    else std::cerr << '.';
                }
                std::cerr << "\"\n";
            }

            char* defaultDev = (char*)(rdram + 0x92a690);
            if (defaultDev) {
                std::cerr << "[SifCallRpc Override] Default VFS device: \"" << defaultDev << "\"\n";
            }

            // Check if this is the cdvdman client
            // WHY: SID=0x4 uses clientPtr=0x1B4DC0. SID=0x80000003 is IOP Heap (not CDVD).
            // We match on clientPtr to avoid intercepting IOP heap calls.
            if (clientPtr == 0x1B4DC0) {
                static bool cored_opened = false;

                if (rpcNum == 0x0) { // sceCdOpen
                    char filename[64] = {0};
                    if (sendBuf) {
                        for(int i=0; i<63; i++) {
                            filename[i] = runtimePtr->memory().read8(sendBuf + 4 + i);
                            if (filename[i] == 0) break;
                        }
                    }
                    std::cerr << "[CDVD:Open] file=\"" << filename << "\"" << std::endl;
                    if (strstr(filename, "CORED.BIN")) {
                        cored_opened = true;
                    }
                    if (recvBuf && recvSize >= 4) runtimePtr->memory().write32(recvBuf, 1);
                }
                else if (rpcNum == 0x1) { // sceCdSearchFile
                    char filename[64] = {0};
                    if (sendBuf) {
                        for(int i=0; i<63; i++) {
                            filename[i] = runtimePtr->memory().read8(sendBuf + i);
                            if (filename[i] == 0) break;
                        }
                    }
                    std::cerr << "[CDVD:SearchFile] file=\"" << filename << "\"" << std::endl;
                    if (recvBuf && recvSize >= 4) runtimePtr->memory().write32(recvBuf, 1);
                }
                else if (rpcNum == 0x4) { // Read / GetStat
                    std::cerr << "[CDVD:Read] sendBuf=0x" << std::hex << sendBuf << std::dec << std::endl;
                    if (recvBuf && recvSize >= 4) runtimePtr->memory().write32(recvBuf, 0);
                }
                else {
                    std::cerr << "[CDVD:RPC] rpcNum=0x" << std::hex << rpcNum << std::dec << std::endl;
                    if (recvBuf && recvSize >= 4) runtimePtr->memory().write32(recvBuf, 0);
                }

                // Signal CDVD semaphore
                __m128i old_a0 = ctx->r[4];
                ctx->r[4] = _mm_set_epi64x(0, 3LL);
                ps2_syscalls::iSignalSema(rdram, ctx, runtimePtr);
                ctx->r[4] = old_a0;

                setReturnS32(ctx, 0);
                ctx->pc = getRegU32(ctx, 31);
                return;
            }

            // Fix for custom SIF RPC sid=6 (deadlock / main thread crash)
            // It uses statically allocated clientPtr = 0x9a3c20 or 0x9a3d40
            if (clientPtr == 0x9a3c20 || clientPtr == 0x9a3d40 || sid == 0x40) {
                uint32_t sp = getRegU32(ctx, 29);
                // SN Systems ABI for >4 args uses t0-t3 for args 5-8, and stack starts at sp+32 for arg 9
                uint32_t recvBuf = getRegU32(ctx, 9);   // t1
                uint32_t recvSize = getRegU32(ctx, 10); // t2
                uint32_t endFunc = getRegU32(ctx, 11);  // t3
                uint32_t endParam = runtimePtr->memory().read32(sp + 32);
                
                std::cerr << "[StarWars:SifCallRpc] Override for SID " << sid << " executed" << std::endl;

                // 1) Write zeroes to recvBuf
                // By returning all zeroes, the parsing function (0x75e5f0) calculates 0 items
                // instead of parsing garbage or causing bad pointers.
                if (recvBuf && recvSize > 0) {
                    for (uint32_t i = 0; i < recvSize; i++) {
                        runtimePtr->memory().write8(recvBuf + i, 0);
                    }
                    std::cerr << "[StarWars:SifCallRpc] Cleared " << recvSize << " bytes at 0x" << std::hex << recvBuf << std::dec << std::endl;
                }

                // 2) Provide a semaphore signal to avoid deadlock
                __m128i old_a0 = ctx->r[4];
                ctx->r[4] = _mm_set_epi64x(0, 3LL); // Fake CDVD/SIF semaphore ID=3
                ps2_syscalls::iSignalSema(rdram, ctx, runtimePtr);
                ctx->r[4] = old_a0;

                // 3) Execute the end function!
                if (endFunc != 0) {
                    std::cerr << "[StarWars:SifCallRpc] Calling endFunc 0x" << std::hex << endFunc << " arg 0x" << endParam << std::dec << std::endl;
                    // Prepare arguments for endFunc(endParam)
                    ctx->r[4] = _mm_set_epi64x(0, endParam); // a0 = end_param
                    setReturnS32(ctx, 0);                    // sceSifCallRpc returns 0
                    ctx->pc = endFunc;                       // jump to endFunc synchronously
                    return;
                } else {
                    setReturnS32(ctx, 0);
                    ctx->pc = getRegU32(ctx, 31);
                    return;
                }
            }

            // Fallback for all other RPCs
            ps2_syscalls::SifCallRpc(rdram, ctx, runtimePtr);
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x00114838, overrideSifCallRpc);

        // _sceSifLoadModule (0x119360)
        static auto overrideSifLoadModule = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            ps2_syscalls::SifLoadModule(rdram, ctx, runtimePtr);
            ctx->pc = getRegU32(ctx, 31); // return via ra
        };
        runtime.registerFunction(0x00119360, overrideSifLoadModule);

        // _sceSifInitRpc (0x118b88)
        static auto overrideSifInitRpc = [](uint8_t*, R5900Context* ctx, PS2Runtime*) {
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x00118b88, overrideSifInitRpc);
        runtime.memory().write32(0x114088, 0x00000000); // NOP out polling loop

        // sceSifCheckStatRpc (0x114a28)
        static auto overrideSifCheckStatRpc = [](uint8_t*, R5900Context* ctx, PS2Runtime*) {
            setReturnS32(ctx, 0);
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x00114a28, overrideSifCheckStatRpc);

        // CD I/O "check pending" helper (0x100e60)
        static auto overrideCdCheckPending = [](uint8_t*, R5900Context* ctx, PS2Runtime*) {
            setReturnS32(ctx, 0);
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x00100e60, overrideCdCheckPending);

        // SetAlarm callback at 0x100288
        static auto overrideAlarmCallback = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
            uint32_t semaId = getRegU32(ctx, 6);
            ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(semaId))); 
            ps2_syscalls::iSignalSema(rdram, ctx, rt);
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x00100288, overrideAlarmCallback);

        // Sleep/delay function at 0x1002b0
        static auto overrideSleepDelay = [](uint8_t*, R5900Context* ctx, PS2Runtime*) {
            uint32_t ticks = getRegU32(ctx, 4) & 0xFFFFu;
            auto us = std::max(1u, ticks) * 64u;
            if (us > 500000u) us = 500000u;
            std::this_thread::sleep_for(std::chrono::microseconds(us));
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x001002b0, overrideSleepDelay);

        // ────────────────────────────────────────────────────
        //  FIX #CRT: Delayed Metrowerks CRT Constructor Init
        //
        //  The game's _start routine (0x100008) clears the .bss section
        //  (0x1afa00 - 0xa9f580), which WIPE OUT the CRT vtable at 0x9a0ab0.
        //  If we run FUN_0067c890 during static hook application, it gets
        //  zeroed out almost immediately by the engine.
        //
        //  Instead, we hook the `main` function (FUN_00129d40) which is
        //  called precisely AFTER the BSS clearance. We run the initialization
        //  once right as `main` begins, guaranteeing a 100% stable vtable.
        // ────────────────────────────────────────────────────
        static PS2Runtime::RecompiledFunction s_orig_main = nullptr;
        s_orig_main = runtime.lookupFunction(0x00129d40);

        if (s_orig_main) {
              static auto wrapMain = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                  static bool s_main_entered = false;
                  if (!s_main_entered) {
                      s_main_entered = true;
                      std::cerr << "[CRT] Entering main â€” initializing Metrowerks vtable (FUN_0067c890)..." << std::endl;

                      // FIX: The static recompiler missed MWo3 relocations for corec.bin
                      // It hardcoded jump table addresses like 0xA76400. We patch the specific table here.
                      uint32_t jmptab[] = {
                          0x67f9e0, 0x67f9f0, 0x67fa10, 0x67fa20, 0x67fa40, 
                          0x67fa00, 0x67fa30, 0x67fa70, 0x67fa50, 0x67fa60
                      };
                      for(int i=0; i<10; i++) {
                          rt->memory().write32(0xa76400 + i*4, jmptab[i]);
                      }


                    // Call the CRT vtable initializer NOW, after BSS clear.
                    // This populates all 50+ function pointer slots in the vtable at 0x9a0ab0.
                    // _start (0x100008) zeroes BSS first, so we MUST populate AFTER that.
                    R5900Context tempCtx = *ctx;
                    tempCtx.pc = 0x67c890;
                    // ra = return immediately (we don't care, we just need the side-effects)
                    tempCtx.r[31] = _mm_set_epi64x(0, 0);  // ra = 0 (won't be used)
                    ps2_FUN_0067c890_67c890(rdram, &tempCtx, rt);

                    // Copy the vtable to a safe area outside BSS
                    // BSS range: 0x1afa00 - 0xa9f580 (resets to zero on start).
                    // The kernel memory area (0x10000) is safe & unused by the game.
                    constexpr uint32_t SAFE_VTABLE = 0x00010000;
                    constexpr uint32_t VTABLE_SIZE = 0x120; // 288 bytes = 72 u32 entries

                    // GOLDEN VTABLE: exact copy from PCSX2 memory at 0x9a0ab0 (72 words).
                    // FUN_0067c890 only populates SOME slots; PCSX2 has the complete picture.
                    static const uint32_t kGoldenVtable[72] = {
                        0x00010000, 0x00000000, 0x20647453, 0x746e6928,  // +0x00: header
                        0x616e7265, 0x0000296c, 0x00000000, 0x00000000,  // +0x10
                        0x00000000, 0x00000000, 0x00000148, 0x00000000,  // +0x20
                        0x00000001, 0x01fe0280, 0x0067df40, 0x0067df30,  // +0x30: first fn ptrs
                        0x0067def0, 0x00000000, 0x003c6c50, 0x0067ddc0,  // +0x40
                        0x0067dd10, 0x00129720, 0x0067d680, 0x0067d670,  // +0x50
                        0x00129680, 0x00129580, 0x0067d5c0, 0x00000000,  // +0x60
                        0x0067dd00, 0x0067dcf0, 0x0067d870, 0x0067d7b0,  // +0x70
                        0x0067db60, 0x0067e050, 0x0067dfa0, 0x0067da00,  // +0x80: [+0x84]=0x67e050 (CRITICAL)
                        0x0067d960, 0x0067dcc0, 0x0067dcb0, 0x0067d6f0,  // +0x90
                        0x0067d6d0, 0x0067d790, 0x0067d7a0, 0x0067d780,  // +0xA0
                        0x0067d750, 0x0067d710, 0x0067c880, 0x0067c870,  // +0xB0
                        0x0067c860, 0x0067d770, 0x0067d760, 0x0067dce0,  // +0xC0
                        0x0067dcd0, 0x0067d6b0, 0x0067d6a0, 0x00250d50,  // +0xD0: [+0xdc]=0x250d50
                        0x00250d30, 0x00250c90, 0x00000000, 0x00250df0,  // +0xE0
                        0x00250c10, 0x002508a0, 0x00000000, 0x00000000,  // +0xF0
                        0x0067d5b0, 0x0067d5a0, 0x0067d560, 0x0067d550,  // +0x100
                        0x00000000, 0xffffffff, 0x00000001, 0x001b0564,  // +0x110
                    };

                    // Write the golden vtable to the safe area
                    for (uint32_t i = 0; i < 72; i++) {
                        rt->memory().write32(SAFE_VTABLE + i * 4, kGoldenVtable[i]);
                    }

                    // Also overlay any non-zero values from the CRT init
                    // (in case the recompiled CRT wrote different addresses)
                    uint32_t vtableBase = rt->memory().read32(0x1af860);
                    if (vtableBase && vtableBase >= 0x1afa00 && vtableBase < 0xa9f580) {
                        for (uint32_t off = 0; off < VTABLE_SIZE; off += 4) {
                            uint32_t val = rt->memory().read32(vtableBase + off);
                            if (val != 0) {
                                rt->memory().write32(SAFE_VTABLE + off, val);
                            }
                        }
                    }

                    // Redirect the pointer to the safe copy
                    rt->memory().write32(0x1af860, SAFE_VTABLE);

                    uint32_t slotDc = rt->memory().read32(SAFE_VTABLE + 0xdc);
                    uint32_t slot84 = rt->memory().read32(SAFE_VTABLE + 0x84);
                    uint32_t slot54 = rt->memory().read32(SAFE_VTABLE + 0x54);
                    std::cerr << "[CRT] VTable WRITTEN (golden+CRT overlay) at 0x" << std::hex << SAFE_VTABLE
                              << " [+0x84]=0x" << slot84
                              << " [+0xdc]=0x" << slotDc
                              << " [+0x54]=0x" << slot54
                              << std::dec << std::endl;
                }
                s_orig_main(rdram, ctx, rt);
            };
            runtime.registerFunction(0x00129d40, wrapMain);
            std::cerr << "[DIAG] Hooked main (0x129d40)." << std::endl;
        }

        // ────────────────────────────────────────────────────
        //  FIX #CRT2: Diagnostic wrapper for FUN_0012d660 (sprintf)
        // ────────────────────────────────────────────────────
        static PS2Runtime::RecompiledFunction s_orig_12d660 = nullptr;
        s_orig_12d660 = runtime.lookupFunction(0x12d660);

        if (s_orig_12d660) {
            static auto wrapSprintf = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                static int callCount = 0;
                callCount++;

                uint32_t gp = getRegU32(ctx, 28);
                uint32_t vtableAddr = gp - 0x7c90;
                uint32_t vtableBase = rt->memory().read32(vtableAddr);
                uint32_t slotDc = vtableBase ? rt->memory().read32(vtableBase + 0xdc) : 0;

                // Auto-repair: if something ovewrote vtable slots, rebuild them
                if (slotDc == 0 && vtableBase != 0) {
                    std::cerr << "[CRT:fix] Slot +0xdc is ZERO at call #" << callCount
                              << " — re-running FUN_0067c890" << std::dec << std::endl;
                    R5900Context tempCtx = *ctx;
                    tempCtx.pc = 0x67c890;
                    ps2_FUN_0067c890_67c890(rdram, &tempCtx, rt);
                    slotDc = rt->memory().read32(vtableBase + 0xdc);
                    std::cerr << "[CRT:fix] After re-init: [+0xdc]=0x" << std::hex << slotDc << std::dec << std::endl;
                }

                // Log only the first 3 calls
                if (callCount <= 3) {
                    std::cerr << "[DIAG:12d660] #" << callCount
                              << " gp=0x" << std::hex << gp
                              << " vtableBase=0x" << vtableBase
                              << " pc=0x" << ctx->pc
                              << " [base+0xdc]=0x" << slotDc
                              << std::dec << std::endl;
                }

                // Call original recompiled function
                s_orig_12d660(rdram, ctx, rt);
            };
            runtime.registerFunction(0x12d660, wrapSprintf);
        }

        // ────────────────────────────────────────────────────
        //  FIX #CRT3: Register Metrowerks CRT vtable label stubs
        //
        //  The CRT vtable at 0x9a0ab0 contains ~31 entries that point
        //  to mid-function labels (LAB_*) which Ghidra identified as
        //  labels, not standalone functions. The recompiler therefore
        //  never generated code for them, causing pc-zero crashes
        //  when the engine tries to jalr into them.
        //
        //  Most of these are I/O stubs (putchar, write, flush, etc.)
        //  that can safely return 0 in our HLE environment.
        // ────────────────────────────────────────────────────
        {
            static auto crtNoop = [](uint8_t*, R5900Context* ctx, PS2Runtime*) {
                // Return 0 in v0 (register 2)
                ctx->r[2] = _mm_set_epi64x(0, 0);
            };

            // All LAB_ entries from FUN_0067c890 vtable init
            const uint32_t crtLabels[] = {
                0x67d690, 0x67d680, 0x67d670, 0x67d660,  // putchar-like stubs
                0x67d5c0,                                   // close stub
                0x67dd00, 0x67dcf0,                         // read stubs
                0x67d6f0, 0x67d6d0,                         // write stubs
                0x67dcb0, 0x67dcc0,                         // seek stubs
                0x67d7a0, 0x67d790, 0x67d780,               // buffer stubs
                0x67d750, 0x67d710,                         // misc I/O
                0x67dce0, 0x67dcd0,                         // position stubs
                0x67d770, 0x67d760,                         // size stubs
                0x67c880, 0x67c870, 0x67c860,               // init stubs
                0x250d30,                                   // format label
                0x2508a0,                                   // format helper
                0x67d6a0, 0x67d6b0,                         // error stubs
                0x67d5b0, 0x67d5a0, 0x67d550,               // exit/cleanup
                0x67df30,                                   // alloc stub
            };

            int count = 0;
            for (uint32_t addr : crtLabels) {
                if (!runtime.lookupFunction(addr)) {
                    runtime.registerFunction(addr, crtNoop);
                    count++;
                }
            }
            std::cerr << "[CRT] Registered " << count << " CRT label stubs as no-op" << std::endl;
        }

        // ────────────────────────────────────────────────────
        //  IPU/DMA Sync flag override (0x12f4e0)
        //  The game spins on `lbu v1, -0x7a94(gp)` which translates to 0x1afa5c.
        //  This flag is normally set to 1 by an interrupt handler upon DMA completion.
        //  Since we HLE the DMA and don't fire hardware interrupts, it stays 0 forever.
        //  We intercept the loop and immediately return to unblock the engine.
        // ────────────────────────────────────────────────────
        static auto overrideDmaSyncLoop = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
            uint32_t gp = getRegU32(ctx, 28);
            // Write 1 to the polled flag just in case the engine checks it again later
            rt->memory().write8(gp - 0x7A94, 1);
            // Return to caller
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x0012f4e0, overrideDmaSyncLoop);

        // ────────────────────────────────────────────────────
        //  Enhance EndOfHeap logging: uncap the log limit
        //  (The EndOfHeap syscall in ps2_syscalls_system.inl
        //   already logs, but only first 10 calls. We'll add
        //   a warning log here about the patch.)
        // ────────────────────────────────────────────────────
        // ────────────────────────────────────────────────────
        //  FIX #OVL1: Safe vtable dispatch for FUN_00803350
        //
        //  The overlay game-loop tick at 0x803350 does:
        //    1. FUN_006c87d0() — scene init
        //    2. FUN_00888ed0() — ref-count bump
        //    3. FUN_0080ae30() — entity update
        //    4. (*vtable[+0xfc])(obj, arg) — CRASHES if vtable not populated
        //    5. FUN_0080aac0(0)
        //    6. (*vtable[+0xc4])(obj, 1)
        //
        //  The vtable may be zero if the object at piRam001b07b4 hasn't been
        //  fully constructed yet (e.g., scene constructor in overlay).
        //  We wrap and skip null vtable calls to keep the engine alive.
        // ────────────────────────────────────────────────────
        {
            static PS2Runtime::RecompiledFunction s_orig_803350 = nullptr;
            s_orig_803350 = runtime.lookupFunction(0x803350);

            if (s_orig_803350) {
                static auto wrapGameTick = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                    static int tickCount = 0;
                    tickCount++;

                    // Read the object pointer: piRam001b07b4 = *(gp - 0x6d3c)
                    uint32_t gp = getRegU32(ctx, 28);
                    uint32_t objPtr = rt->memory().read32(gp - 0x6d3c);
                    uint32_t vtable = objPtr ? rt->memory().read32(objPtr) : 0;
                    uint32_t slotFc = vtable ? rt->memory().read32(vtable + 0xfc) : 0;
                    uint32_t slotC4 = vtable ? rt->memory().read32(vtable + 0xc4) : 0;

                    if (tickCount <= 3 || (slotFc == 0 && vtable != 0)) {
                        std::cerr << "[OVL:803350] tick #" << tickCount
                                  << " obj=0x" << std::hex << objPtr
                                  << " vtable=0x" << vtable
                                  << " [+0xfc]=0x" << slotFc
                                  << " [+0xc4]=0x" << slotC4
                                  << std::dec << std::endl;
                    }

                    if (slotFc == 0 || slotC4 == 0) {
                        // vtable not ready — call the sub-functions but skip vtable calls
                        auto fn1 = rt->lookupFunction(0x6c87d0);
                        auto fn2 = rt->lookupFunction(0x888ed0);
                        auto fn3 = rt->lookupFunction(0x80ae30);
                        auto fn5 = rt->lookupFunction(0x80aac0);

                        R5900Context sub = *ctx;
                        if (fn1) { sub.pc = 0x6c87d0; fn1(rdram, &sub, rt); }
                        sub = *ctx;
                        if (fn2) { sub.pc = 0x888ed0; fn2(rdram, &sub, rt); }
                        sub = *ctx;
                        if (fn3) { sub.pc = 0x80ae30; fn3(rdram, &sub, rt); }

                        // Skip vtable[+0xfc] call — it would crash
                        if (tickCount <= 3)
                            std::cerr << "[OVL:803350] Skipped vtable calls (slots not ready)" << std::endl;

                        sub = *ctx;
                        if (fn5) {
                            sub.pc = 0x80aac0;
                            sub.r[4] = _mm_set_epi64x(0, 0);  // a0 = 0
                            fn5(rdram, &sub, rt);
                        }
                        // Skip vtable[+0xc4] call too
                        ctx->pc = getRegU32(ctx, 31);  // return
                    } else {
                        // vtable is valid — call original
                        s_orig_803350(rdram, ctx, rt);
                    }
                };
                runtime.registerFunction(0x803350, wrapGameTick);
                std::cerr << "[OVL] Hooked FUN_00803350 (game tick)" << std::endl;
            }
        }

        // ────────────────────────────────────────────────────
        //  FIX #ABORT: Safe abort handler (FUN_00129580)
        //
        //  The game's internal ABORT function at 0x129580 formats
        //  an error message then calls a display callback via:
        //    *(*(*(iRam001b061c + 0x18) + 0x144) + 0x20)
        //  That display vtable contains sentinel 0x12345 when not
        //  initialized, causing a bad-pc crash.
        //
        //  We hook this to:
        //  1. Log the abort error code (param in $a0)
        //  2. Call FUN_00113780 (the printf) so cerr shows the message
        //  3. Skip the display vtable call entirely
        //  4. Return 0 (same as original)
        // ────────────────────────────────────────────────────
        {
            static PS2Runtime::RecompiledFunction s_orig_abort = nullptr;
            s_orig_abort = runtime.lookupFunction(0x129580);

            auto safeAbort = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                const uint32_t errorCode = getRegU32(ctx, 4);
                static int abortCount = 0;
                ++abortCount;

                // Try to read the error string from guest memory
                // The param in a0 might be a guest pointer to a string
                std::string errorStr = "<unknown>";
                if (errorCode > 0x100000 && errorCode < 0x10000000) {
                    // Looks like a valid guest address — read null-terminated string
                    char buf[256] = {0};
                    for (int i = 0; i < 255; ++i) {
                        char c = static_cast<char>(rdram[errorCode + i]);
                        if (c == 0) break;
                        buf[i] = c;
                    }
                    errorStr = buf;
                }

                std::cerr << "[SW3:ABORT] #" << abortCount
                          << " a0=0x" << std::hex << errorCode
                          << " ra=0x" << getRegU32(ctx, 31)
                          << " pc=0x" << ctx->pc
                          << std::dec;
                if (!errorStr.empty() && errorStr != "<unknown>") {
                    std::cerr << " msg=\"" << errorStr << "\"";
                }
                std::cerr << std::endl;

                // Return 0 — skip the display vtable call that would crash
                setReturnU32(ctx, 0);
                ctx->pc = getRegU32(ctx, 31);  // return to caller
            };
            runtime.registerFunction(0x129580, safeAbort);
            std::cerr << "[SW3] Hooked FUN_00129580 (abort handler)" << std::endl;
        }

        // ────────────────────────────────────────────────────
        //  FIX FUN_0067f9b0 (broken jump table block execution)
        // ────────────────────────────────────────────────────
        {
            auto wrap_67f9b0 = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                auto setRegU32 = [](R5900Context* c, int reg, uint32_t val) {
                    c->r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(val)));
                };
                auto setRegU64 = [](R5900Context* c, int reg, uint64_t val) {
                    c->r[reg] = _mm_set_epi64x(val >> 32, val & 0xFFFFFFFF);
                };
                auto getRegU64 = [](R5900Context* c, int reg) -> uint64_t {
                    return static_cast<uint64_t>(_mm_extract_epi64(c->r[reg], 0));
                };

                // Prologue
                uint32_t sp = getRegU32(ctx, 29) - 0x20;
                setRegU32(ctx, 29, sp);
                uint32_t a0 = getRegU32(ctx, 4);
                
                // Delay slot of the condition branch (beq at, zero, 0x67FA80)
                rt->memory().write64(sp, getRegU64(ctx, 31));

                if (a0 >= 10) {
                    // bb_2: 0x67FA80
                    setRegU32(ctx, 4, sp + 0x1c);
                    setRegU32(ctx, 31, 0x67fa90);
                    setRegU32(ctx, 5, 0xa765ad);
                    ctx->pc = 0x12dab0;
                    return;
                } else {
                    uint32_t jmptab[10] = {0x67f9e0, 0x67f9f0, 0x67fa10, 0x67fa20, 0x67fa40, 
                                           0x67fa00, 0x67fa30, 0x67fa70, 0x67fa50, 0x67fa60};
                    uint32_t target_addr = jmptab[a0];
                    
                    uint32_t jump_instr = rt->memory().read32(target_addr + 8);
                    uint32_t offset = jump_instr & 0xFFFF;
                    
                    setRegU32(ctx, 2, 0xa70000 + offset); // v0

                    // Epilogue (0x67FAB8)
                    uint64_t ra = rt->memory().read64(sp);
                    setRegU64(ctx, 31, ra);
                    setRegU32(ctx, 29, sp + 0x20);
                    
                    ctx->pc = (uint32_t)ra;
                    return;
                }
            };
            runtime.registerFunction(0x67f9b0, wrap_67f9b0);
            std::cerr << "[SW3] Hooked FUN_0067f9b0 (broken switch) to un-break loop." << std::endl;
        }

        // ────────────────────────────────────────────────────
        //  FIX: Generic MIPS micro-thunk interpreter
        //
        //  The game binary contains hundreds of tiny vtable accessor
        //  functions that are just: jr ra; <delay-slot-instr>
        //  The static recompiler never emitted code for them, so
        //  when a vtable call dispatches to one of these addresses,
        //  lookupFunction fails → defaultFunction → pc-zero → dead thread.
        //
        //  Solution: scan guest memory for the pattern 0x03E00008 (jr ra)
        //  and register a generic interpreter that:
        //  1. Reads the delay-slot instruction (pc + 4)
        //  2. Interprets it (addiu, lw, move, ori, etc.)
        //  3. Sets pc = ra to return
        // ────────────────────────────────────────────────────
        {
            // The thunk interpreter lambda: reads and executes one delay-slot instruction
            static auto thunkInterpreter = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                uint32_t pc = ctx->pc;
                uint32_t instr = rt->memory().read32(pc + 4); // delay slot

                uint8_t  op    = (instr >> 26) & 0x3F;
                uint8_t  rs    = (instr >> 21) & 0x1F;
                uint8_t  rt_f  = (instr >> 16) & 0x1F;
                uint8_t  rd    = (instr >> 11) & 0x1F;
                uint8_t  sa    = (instr >>  6) & 0x1F;
                uint8_t  func  = instr & 0x3F;
                int16_t  imm   = static_cast<int16_t>(instr & 0xFFFF);
                uint16_t uimm  = instr & 0xFFFF;

                auto getReg = [](R5900Context* c, int r) -> uint32_t {
                    return static_cast<uint32_t>(_mm_extract_epi32(c->r[r], 0));
                };
                auto setReg = [](R5900Context* c, int r, uint32_t val) {
                    if (r == 0) return; // $zero is immutable
                    c->r[r] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(val)));
                };

                switch (op) {
                    case 0x00: { // SPECIAL
                        switch (func) {
                            case 0x21: // addu rd, rs, rt
                                setReg(ctx, rd, getReg(ctx, rs) + getReg(ctx, rt_f));
                                break;
                            case 0x25: // or rd, rs, rt  (also: move rd, rs when rt=0)
                                setReg(ctx, rd, getReg(ctx, rs) | getReg(ctx, rt_f));
                                break;
                            case 0x2D: // daddu rd, rs, rt (treat as addu for 32-bit)
                                setReg(ctx, rd, getReg(ctx, rs) + getReg(ctx, rt_f));
                                break;
                            case 0x00: // sll rd, rt, sa (nop when rd=rt=sa=0)
                                setReg(ctx, rd, getReg(ctx, rt_f) << sa);
                                break;
                            case 0x02: // srl rd, rt, sa
                                setReg(ctx, rd, getReg(ctx, rt_f) >> sa);
                                break;
                            case 0x03: // sra rd, rt, sa
                                setReg(ctx, rd, static_cast<uint32_t>(static_cast<int32_t>(getReg(ctx, rt_f)) >> sa));
                                break;
                            case 0x23: // subu rd, rs, rt
                                setReg(ctx, rd, getReg(ctx, rs) - getReg(ctx, rt_f));
                                break;
                            case 0x24: // and rd, rs, rt
                                setReg(ctx, rd, getReg(ctx, rs) & getReg(ctx, rt_f));
                                break;
                            case 0x27: // nor rd, rs, rt
                                setReg(ctx, rd, ~(getReg(ctx, rs) | getReg(ctx, rt_f)));
                                break;
                            case 0x2A: // slt rd, rs, rt
                                setReg(ctx, rd, (static_cast<int32_t>(getReg(ctx, rs)) < static_cast<int32_t>(getReg(ctx, rt_f))) ? 1 : 0);
                                break;
                            case 0x2B: // sltu rd, rs, rt
                                setReg(ctx, rd, (getReg(ctx, rs) < getReg(ctx, rt_f)) ? 1 : 0);
                                break;
                            default:
                                // Unknown SPECIAL — just nop it and log
                                static int s_unknownSpecial = 0;
                                if (s_unknownSpecial++ < 10)
                                    std::cerr << "[thunk:unknown-special] pc=0x" << std::hex << pc
                                              << " func=0x" << (int)func << std::dec << std::endl;
                                break;
                        }
                        break;
                    }
                    case 0x09: // addiu rt, rs, imm
                        setReg(ctx, rt_f, getReg(ctx, rs) + static_cast<uint32_t>(static_cast<int32_t>(imm)));
                        break;
                    case 0x0D: // ori rt, rs, uimm
                        setReg(ctx, rt_f, getReg(ctx, rs) | uimm);
                        break;
                    case 0x0C: // andi rt, rs, uimm
                        setReg(ctx, rt_f, getReg(ctx, rs) & uimm);
                        break;
                    case 0x0F: // lui rt, imm
                        setReg(ctx, rt_f, static_cast<uint32_t>(uimm) << 16);
                        break;
                    case 0x23: // lw rt, offset(rs)
                        setReg(ctx, rt_f, rt->memory().read32(getReg(ctx, rs) + static_cast<uint32_t>(static_cast<int32_t>(imm))));
                        break;
                    case 0x21: // lh rt, offset(rs)
                        setReg(ctx, rt_f, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(rt->memory().read16(getReg(ctx, rs) + static_cast<uint32_t>(static_cast<int32_t>(imm)))))));
                        break;
                    case 0x25: // lhu rt, offset(rs)
                        setReg(ctx, rt_f, static_cast<uint32_t>(rt->memory().read16(getReg(ctx, rs) + static_cast<uint32_t>(static_cast<int32_t>(imm)))));
                        break;
                    case 0x20: // lb rt, offset(rs)
                        setReg(ctx, rt_f, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(rt->memory().read8(getReg(ctx, rs) + static_cast<uint32_t>(static_cast<int32_t>(imm)))))));
                        break;
                    case 0x24: // lbu rt, offset(rs)
                        setReg(ctx, rt_f, static_cast<uint32_t>(rt->memory().read8(getReg(ctx, rs) + static_cast<uint32_t>(static_cast<int32_t>(imm)))));
                        break;
                    case 0x2B: // sw rt, offset(rs)
                        rt->memory().write32(getReg(ctx, rs) + static_cast<uint32_t>(static_cast<int32_t>(imm)), getReg(ctx, rt_f));
                        break;
                    case 0x29: // sh rt, offset(rs)
                        rt->memory().write16(getReg(ctx, rs) + static_cast<uint32_t>(static_cast<int32_t>(imm)), static_cast<uint16_t>(getReg(ctx, rt_f)));
                        break;
                    case 0x28: // sb rt, offset(rs)
                        rt->memory().write8(getReg(ctx, rs) + static_cast<uint32_t>(static_cast<int32_t>(imm)), static_cast<uint8_t>(getReg(ctx, rt_f)));
                        break;
                    case 0x0A: // slti rt, rs, imm
                        setReg(ctx, rt_f, (static_cast<int32_t>(getReg(ctx, rs)) < static_cast<int32_t>(imm)) ? 1 : 0);
                        break;
                    case 0x0B: // sltiu rt, rs, imm
                        setReg(ctx, rt_f, (getReg(ctx, rs) < static_cast<uint32_t>(static_cast<int32_t>(imm))) ? 1 : 0);
                        break;
                    default:
                        if (instr != 0x00000000) { // not a nop
                            static int s_unknownOp = 0;
                            if (s_unknownOp++ < 10)
                                std::cerr << "[thunk:unknown-op] pc=0x" << std::hex << pc
                                          << " op=0x" << (int)op << " instr=0x" << instr
                                          << std::dec << std::endl;
                        }
                        break;
                }

                // Return via ra
                ctx->pc = getReg(ctx, 31);
            };

            // Scan guest memory for jr-ra thunk patterns and register them
            // jr ra = 0x03E00008
            constexpr uint32_t JR_RA = 0x03E00008;
            int thunkCount = 0;

            // Scan the entire code section for jr-ra thunks
            // We look for addresses where:
            //   [addr+0] = jr ra (0x03E00008)
            //   [addr+4] = some delay-slot instruction
            //   This is NOT inside a larger function (addr is 16-byte aligned for vtable thunks,
            //   or at minimum the instruction before is padding/nop)
            for (uint32_t addr = 0x100000; addr < 0xA00000; addr += 4) {
                uint32_t w0 = runtime.memory().read32(addr);
                if (w0 != JR_RA) continue;

                // Check if this is the START of a tiny function, not a jr ra in the middle of one.
                // Heuristic: the previous instruction (addr-4) is either:
                //   - a nop (0x00000000)
                //   - another jr ra epilogue's padding
                //   - addr is 16-byte aligned (vtable thunk)
                // AND the function is NOT already registered
                bool isThunkStart = false;

                if ((addr & 0xF) == 0) {
                    // 16-byte aligned — likely vtable thunk
                    isThunkStart = true;
                } else if (addr >= 4) {
                    uint32_t prev = runtime.memory().read32(addr - 4);
                    if (prev == 0x00000000) { // preceded by nop/padding
                        isThunkStart = true;
                    }
                }

                if (isThunkStart && !runtime.hasFunction(addr)) {
                    runtime.registerFunction(addr, thunkInterpreter);
                    ++thunkCount;
                }
            }

            std::cerr << "[SW3] Registered " << thunkCount << " MIPS thunk interpreters (jr-ra + delay-slot)." << std::endl;
        }

        // ────────────────────────────────────────────────────
        //  DIAG: Hook FUN_0012daf0 (factory caller wrapper)
        //  Logs a0 (output pointer) BEFORE calling factory,
        //  and the factory return v0 AFTER the call completes.
        // ────────────────────────────────────────────────────
        {
            static PS2Runtime::RecompiledFunction s_orig_12daf0 = nullptr;
            s_orig_12daf0 = runtime.lookupFunction(0x12daf0);
            if (s_orig_12daf0) {
                static auto wrap12daf0 = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                    static int s_callCount = 0;
                    ++s_callCount;
                    uint32_t a0_in = getRegU32(ctx, 4);  // output slot pointer
                    uint32_t ra_in = getRegU32(ctx, 31); // return address

                    if (s_callCount <= 20) {
                        std::cerr << "[DIAG:12daf0] PRE call#" << s_callCount
                                  << " a0(outSlot)=0x" << std::hex << a0_in
                                  << " ra=0x" << ra_in
                                  << std::dec << std::endl;
                    }
                    // Call the original function
                    s_orig_12daf0(rdram, ctx, rt);
                    // After return, v0 should contain the output pointer
                    uint32_t v0_out = getRegU32(ctx, 2);
                    // Also read what was written to the output slot
                    uint32_t slotVal = rt->memory().read32(a0_in);
                    
                    uint32_t gp = getRegU32(ctx, 28);
                    uint32_t vtableBase = rt->memory().read32(gp - 0x7c90);
                    uint32_t vtableEntry = rt->memory().read32(vtableBase + 0xdc);

                    if (s_callCount <= 20) {
                        std::cerr << "[DIAG:12daf0] POST call#" << s_callCount
                                  << " v0=0x" << std::hex << v0_out
                                  << " *outSlot=0x" << slotVal
                                  << " gp=0x" << gp
                                  << " vtableBase=0x" << vtableBase
                                  << " vtable[0xdc]=0x" << vtableEntry
                                  << std::dec << std::endl;
                    }
                };
                runtime.registerFunction(0x12daf0, wrap12daf0);
                std::cerr << "[SW3] Hooked FUN_0012daf0 (factory caller) for diagnostics" << std::endl;
            }
        }

        // ────────────────────────────────────────────────────
        //  DIAG: Hook FUN_00250d50 (singleton factory)
        //  Logs what the factory receives and returns.
        // ────────────────────────────────────────────────────
        {
            static PS2Runtime::RecompiledFunction s_orig_250d50 = nullptr;
            s_orig_250d50 = runtime.lookupFunction(0x250d50);
            if (s_orig_250d50) {
                static auto wrap250d50 = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                    static int s_callCount = 0;
                    ++s_callCount;
                    uint32_t a0_in = getRegU32(ctx, 4);  // hash string ptr
                    uint32_t gp = getRegU32(ctx, 28);

                    if (s_callCount <= 20) {
                        // Read the hash string
                        char hashStr[64] = {0};
                        for (int i = 0; i < 63; i++) {
                            uint8_t c = rt->memory().read8(a0_in + i);
                            if (c == 0) break;
                            hashStr[i] = (char)c;
                        }
                        std::cerr << "[DIAG:250d50] PRE call#" << s_callCount
                                  << " a0(hash)=0x" << std::hex << a0_in
                                  << " str=\"" << hashStr << "\""
                                  << " gp=0x" << gp
                                  << std::dec << std::endl;
                    }
                    // Call the original factory
                    s_orig_250d50(rdram, ctx, rt);
                    uint32_t v0_out = getRegU32(ctx, 2);
                    if (s_callCount <= 20) {
                        std::cerr << "[DIAG:250d50] POST call#" << s_callCount
                                  << " v0(singleton)=0x" << std::hex << v0_out
                                  << std::dec << std::endl;
                    }
                };
                runtime.registerFunction(0x250d50, wrap250d50);
                std::cerr << "[SW3] Hooked FUN_00250d50 (singleton factory) for diagnostics" << std::endl;
            }
        }

        // ────────────────────────────────────────────────────
        //  DIAG: Hook FUN_0024ff40 (component vtable iterator)
        //  This function iterates over an array of component objects and
        //  calls virtual methods. If any component's vtable has a NULL entry
        //  at +0x70, the jalr t9 crashes (pc-zero).
        //  We wrap it to log the component array state before executing.
        // ────────────────────────────────────────────────────
        {
            static PS2Runtime::RecompiledFunction s_orig_24ff40 = nullptr;
            s_orig_24ff40 = runtime.lookupFunction(0x24ff40);
            if (s_orig_24ff40) {
                static auto wrapComponentIter = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                    static int s_callCount = 0;
                    ++s_callCount;
                    // a0 = this pointer (s5 in the assembly = param_1)
                    uint32_t thisPtr = getRegU32(ctx, 4);
                    // a1 = param_2 (s4/target)
                    // a2 = param_3 (s3/filter)
                    if (thisPtr && s_callCount <= 20) {
                        // Read component count at +0x14 and component array ptr at +0x18
                        uint32_t compCount = rt->memory().read32(thisPtr + 0x14);
                        uint32_t compArray = rt->memory().read32(thisPtr + 0x18);
                        // Read vtable ptr at +0x0 of this object
                        uint32_t objVtable = rt->memory().read32(thisPtr);
                        // Read the row-count method at +0x50 (used to determine iteration count)
                        uint32_t vtSlot50 = objVtable ? rt->memory().read32(objVtable + 0x50) : 0;
                        // Read the short at +0x6 to check the branch at 0x24ff80
                        uint16_t flag6 = rt->memory().read16(thisPtr + 6);

                        std::cerr << "[DIAG:24ff40] call#" << s_callCount
                                  << " this=0x" << std::hex << thisPtr
                                  << " vtable=0x" << objVtable
                                  << " +0x50=0x" << vtSlot50
                                  << " flag6=" << std::dec << flag6
                                  << " compCount=" << compCount
                                  << " compArray=0x" << std::hex << compArray
                                  << std::dec << std::endl;

                        // Dump each component's vtable pointers
                        for (uint32_t i = 0; i < compCount && i < 8; i++) {
                            uint32_t compPtrSlot = compArray + i * 4;
                            uint32_t compPtr = rt->memory().read32(compPtrSlot);
                            if (compPtr) {
                                uint32_t compVtable = rt->memory().read32(compPtr);
                                uint32_t vt70 = compVtable ? rt->memory().read32(compVtable + 0x70) : 0xDEAD0070;
                                uint32_t vt10 = compVtable ? rt->memory().read32(compVtable + 0x10) : 0xDEAD0010;
                                std::cerr << "  comp[" << i << "] ptr=0x" << std::hex << compPtr
                                          << " vtable=0x" << compVtable
                                          << " +0x70=0x" << vt70
                                          << " +0x10=0x" << vt10
                                          << std::dec << std::endl;
                            } else {
                                std::cerr << "  comp[" << i << "] ptr=NULL!" << std::endl;
                            }
                        }
                    }
                    // Call the original function
                    s_orig_24ff40(rdram, ctx, rt);
                };
                runtime.registerFunction(0x24ff40, wrapComponentIter);
                std::cerr << "[SW3] Hooked FUN_0024ff40 (component vtable iterator)" << std::endl;
            }
        }

        // ────────────────────────────────────────────────────
        //  FIX: FULL OVERRIDE of FUN_0075a100 — SIF RPC audio transfer
        //
        //  This function manages SIF RPC transfers to the IOP for
        //  audio (SPU2). It has TWO internal stall loops:
        //    bb_8  (0x75a180): do {} while (*(gp-0x6ca4) == 1)
        //    bb_16→17 cycle: sceSifCallRpc → sceSifCheckStatRpc loop
        //
        //  Without a real IOP, BOTH loops spin forever.
        //  Clearing the flag before calling original doesn't help
        //  because the function re-enters the SIF RPC call cycle.
        //
        //  Fix: SKIP the entire function. Return v0=0 (success).
        //  Audio will be silent but the engine will proceed.
        // ────────────────────────────────────────────────────
        {
            static auto stub_sif_audio = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                // v0 = 0 (success / no error)
                ctx->r[2] = _mm_set_epi64x(0, 0);
                // Return to caller
                ctx->pc = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[31], 0));

                static int s_logCount = 0;
                if (s_logCount++ < 5) {
                    std::cerr << "[SW3] FUN_0075a100 FULL OVERRIDE → return 0 (audio SIF skip)"
                              << std::endl;
                }
            };
            runtime.registerFunction(0x75a100, stub_sif_audio);
            std::cerr << "[SW3] FULL OVERRIDE FUN_0075a100 (SIF RPC audio) → skip + return 0." << std::endl;

            // Also override the thunk entry at 0x75a5a0 with same stub
            runtime.registerFunction(0x75a5a0, stub_sif_audio);
            std::cerr << "[SW3] FULL OVERRIDE thunk 0x75a5a0 → same stub." << std::endl;
        }

        // ────────────────────────────────────────────────────
        //  FIX: Dispatch 0x250050 (branch delay slot in FUN_0024ff40)
        //  The recompiled FUN_0024ff40 has a switch dispatch that does NOT
        //  include 0x250050 as a case (it's a delay slot of bnez at 0x25004c).
        //  When the dispatcher arrives here, the default case re-executes
        //  the function from bb_0 with stale registers → pc-zero crash.
        //
        //  Fix: execute the delay slot instruction (addiu s6,s6,4) manually,
        //  then redirect to 0x250054 which IS in the switch case.
        // ────────────────────────────────────────────────────
        {
            static PS2Runtime::RecompiledFunction s_orig_24ff40_fn = nullptr;
            s_orig_24ff40_fn = runtime.lookupFunction(0x24ff40);
            if (s_orig_24ff40_fn) {
                static auto fixDelaySlot250050 = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                    // Execute the delay slot: addiu s6, s6, 4
                    uint32_t s6 = getRegU32(ctx, 22);
                    ctx->r[22] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(s6 + 4)));
                    // Redirect to the next valid entry point
                    ctx->pc = 0x250054;
                    // Re-dispatch to the original function with the corrected PC
                    s_orig_24ff40_fn(rdram, ctx, rt);
                };
                runtime.registerFunction(0x250050, fixDelaySlot250050);
                std::cerr << "[SW3] Registered delay-slot fix for 0x250050 -> 0x250054" << std::endl;
            }
        }


        // ────────────────────────────────────────────────────
        //  FIX: Override FUN_00114a28 — sceSifCheckStatRpc
        //
        //  This function checks if a SIF RPC transfer is in progress:
        //    - returns 1 if busy (transfer still running)
        //    - returns 0 if done (transfer complete)
        //
        //  Without a real IOP, the busy flag is never cleared.
        //  The caller at 0x75a21c keeps looping on a "busy" result.
        //
        //  Fix: Always return 0 ("transfer complete").
        //  This also breaks the gp-0x6ca4 busy-wait at 0x75a180
        //  since the function flow no longer hangs.
        // ────────────────────────────────────────────────────
        {
            static auto stub_sceSifCheckStatRpc = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                // v0 = 0 (transfer complete)
                ctx->r[2] = _mm_set_epi64x(0, 0);
                // Return to caller
                ctx->pc = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[31], 0));

                static int s_logCount = 0;
                if (s_logCount++ < 3) {
                    std::cerr << "[SW3] sceSifCheckStatRpc(0x114a28) → 0 (always done)" << std::endl;
                }
            };
            runtime.registerFunction(0x114a28, stub_sceSifCheckStatRpc);
            std::cerr << "[SW3] Overrode FUN_00114a28 (sceSifCheckStatRpc) → always 0." << std::endl;
        }

        // ────────────────────────────────────────────────────
        //  FIX: FULL OVERRIDE of FUN_003c6c10 + FUN_003c6bd0
        //  (Resource Manager memory allocators)
        //
        //  These functions take a0 = size_bytes and return v0 = pointer.
        //  Internally they traverse pool chains via 0x3c6030 (82-block search)
        //  and eventually call printf/debug logging.  Without a fully
        //  initialized pool manager the search chain stalls forever.
        //
        //  Fix: bypass the entire chain, use guestMalloc directly.
        // ────────────────────────────────────────────────────
        {
            static auto stub_resMgrAlloc = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                uint32_t size = getRegU32(ctx, 4);  // a0 = requested size
                if (size == 0) size = 16;            // minimum allocation
                uint32_t ptr = rt->guestMalloc(size, 16);
                setReturnS32(ctx, (int32_t)ptr);  // v0 = allocated pointer (or 0)

                static int s_logCount = 0;
                s_logCount++;
                if (s_logCount <= 50 || (s_logCount % 500) == 0) {
                    std::cerr << "[SW3] resMgrAlloc #" << s_logCount
                              << ": size=0x" << std::hex << size
                              << " -> ptr=0x" << ptr
                              << " ra=0x" << getRegU32(ctx, 31)
                              << std::dec << std::endl;
                }
                if (s_logCount == 10000) {
                    std::cerr << "[SW3] WARNING: resMgrAlloc called 10000 times! Possible infinite loop!" << std::endl;
                }

                // Return to caller
                ctx->pc = getRegU32(ctx, 31);
            };
            runtime.registerFunction(0x3c6c10, stub_resMgrAlloc);
            std::cerr << "[SW3] FULL OVERRIDE FUN_003c6c10 (resMgr alloc) → guestMalloc." << std::endl;

            runtime.registerFunction(0x3c6bd0, stub_resMgrAlloc);
            std::cerr << "[SW3] FULL OVERRIDE FUN_003c6bd0 (resMgr alloc variant) → guestMalloc." << std::endl;
        }

        // ────────────────────────────────────────────────────
        // DIAGNOSTIC OVERRIDE: 0x6599f0
        // The runtime keeps getting stuck at PC 0x6599f0.
        // We override it manually to trace.
        // ────────────────────────────────────────────────────
        {
            static auto stub_0x6599f0 = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                static int hits = 0;
                if (++hits <= 10) {
                    std::cerr << "[SW3] Reached 0x6599f0 override! Hit #" << hits << std::endl;
                }
                
                auto setRegLocal = [](R5900Context* c, int r, uint32_t v) {
                    if (r != 0) c->r[r] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(v)));
                };

                // Replicate bb_1 logic:
                // move a0, v0
                setRegLocal(ctx, 4, getRegU32(ctx, 2));
                // a1 = 0x65BD00
                setRegLocal(ctx, 5, 0x65BD00);
                // a2 = 0x65BCB0
                setRegLocal(ctx, 6, 0x65BCB0);
                // t0 = s0
                setRegLocal(ctx, 8, getRegU32(ctx, 16));
                // v1 = 0xbe3
                setRegLocal(ctx, 3, 0xbe3);
                // a3 = 0x24
                setRegLocal(ctx, 7, 0x24);
                
                // jal 0x0012fe80
                setRegLocal(ctx, 31, 0x659a10);
                ctx->pc = 0x12fe80;
            };
            runtime.registerFunction(0x6599f0, stub_0x6599f0);
            std::cerr << "[SW3] DIAGNOSTIC OVERRIDE for 0x6599f0 registered." << std::endl;
        }
        // ────────────────────────────────────────────────────
        //  FUN_0072e5d0 — Resource Manager constructor
        //
        //  This is a massive 99-bb function that initializes the entire
        //  resource manager subsystem including multiple sub-allocations.
        //  The recompiled C++ uses a giant switch for re-entry dispatch,
        //  but the switch has a "default: break → fall-through to bb_0"
        //  that causes infinite re-dispatch when returning from sub-calls.
        //
        //  Fix: bypass the entire constructor, allocate + zero-fill
        //  the critical sub-structures, and return param_1.
        // ────────────────────────────────────────────────────
        {
            static auto stub_resMgrCtor = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
                uint32_t param1 = getRegU32(ctx, 4);  // a0 = this ptr
                uint32_t param2 = getRegU32(ctx, 5);  // a1
                uint32_t param3 = getRegU32(ctx, 6);  // a2

                // Allocate 0xc8-byte sub-structure (piVar1 in Ghidra)
                uint32_t subStruct1 = rt->guestMalloc(0xc8, 16);
                if (subStruct1) {
                    // Zero-fill
                    uint32_t phys = subStruct1 & 0x1FFFFFFu;
                    if (phys + 0xc8 <= 0x2000000u)
                        std::memset(rdram + phys, 0, 0xc8);
                }

                // Allocate 0xc98-byte sub-structure (for FUN_0067fc50 init)
                uint32_t subStruct2 = rt->guestMalloc(0xc98, 16);
                if (subStruct2) {
                    uint32_t phys = subStruct2 & 0x1FFFFFFu;
                    if (phys + 0xc98 <= 0x2000000u)
                        std::memset(rdram + phys, 0, 0xc98);
                }

                // Allocate 0x178-byte sub-structure (for FUN_0012daf0 calls)
                uint32_t subStruct3 = rt->guestMalloc(0x178, 16);
                if (subStruct3) {
                    uint32_t phys = subStruct3 & 0x1FFFFFFu;
                    if (phys + 0x178 <= 0x2000000u)
                        std::memset(rdram + phys, 0, 0x178);
                }

                // Allocate 0x1b0-byte sub-structure (for FUN_0072f750 etc.)
                uint32_t subStruct4 = rt->guestMalloc(0x1b0, 16);
                if (subStruct4) {
                    uint32_t phys = subStruct4 & 0x1FFFFFFu;
                    if (phys + 0x1b0 <= 0x2000000u)
                        std::memset(rdram + phys, 0, 0x1b0);
                }

                // Helper lambdas for direct RDRAM access
                auto w32 = [rdram](uint32_t addr, uint32_t val) {
                    uint32_t off = addr & PS2_RAM_MASK;
                    std::memcpy(rdram + off, &val, 4);
                };
                auto r32 = [rdram](uint32_t addr) -> uint32_t {
                    uint32_t off = addr & PS2_RAM_MASK;
                    uint32_t val;
                    std::memcpy(&val, rdram + off, 4);
                    return val;
                };
                auto w8 = [rdram](uint32_t addr, uint8_t val) {
                    rdram[addr & PS2_RAM_MASK] = val;
                };

                // Write critical pointers into param1 struct
                w32(param1 + 0x00, 0x18f7b0);   // vtable → DAT_0018f7b0
                w8(param1 + 0x04, 1);            // enabled byte
                w32(param1 + 0x08, param2);      // param2
                w32(param1 + 0x0c, param3);      // param3
                w32(param1 + 0x60, 1);           // param1[0x18] = 1

                // Store global pointer: piRam001b0bb4 = subStruct1
                uint32_t gp = getRegU32(ctx, 28);
                uint32_t globalAddr = gp + (uint32_t)0xffffffffffff96c4ULL; // gp - 0x693c
                w32(globalAddr, subStruct1);

                // Also: *(DAT_001af860 + 0x34) = subStruct1
                uint32_t dat1af860 = r32(0x1af860);
                if (dat1af860) {
                    w32(dat1af860 + 0x34, subStruct1);
                }

                // subStruct1 linkage
                if (subStruct1) {
                    w32(subStruct1 + 0x34, param1);    // back-pointer to param1
                    w32(subStruct1 + 0x20, subStruct2); // [8] = 0xc98 buffer
                    w32(subStruct1 + 0x00, subStruct3); // [0] = 0x178 buffer
                    w32(subStruct1 + 0x04, subStruct4); // [1] = 0x1b0 buffer
                }

                // Display defaults
                w32(param1 + 0x48, 0x280);  // width = 640
                w32(param1 + 0x4c, 0x1e0);  // height = 480

                // Return param1 in v0
                setReturnS32(ctx, (int32_t)param1);

                static int s_logCount = 0;
                if (s_logCount++ < 5) {
                    std::cerr << "[SW3] resMgrCtor override: param1=0x" << std::hex << param1
                              << " sub1=0x" << subStruct1
                              << " sub2=0x" << subStruct2
                              << " sub3=0x" << subStruct3
                              << " sub4=0x" << subStruct4
                              << std::dec << std::endl;
                }

                // Return to caller
                ctx->pc = getRegU32(ctx, 31);
            };
            runtime.registerFunction(0x72e5d0, stub_resMgrCtor);
            std::cerr << "[SW3] FULL OVERRIDE FUN_0072e5d0 (resMgr constructor)." << std::endl;
        }

        std::cerr << "[SW3] All overrides registered. Bulk allocator patch applied." << std::endl;
        std::cerr << "[SW3] Heap config: base=0x" << std::hex
                  << runtime.guestHeapBase() << " limit=0x8000000"
                  << std::dec << std::endl;
    }
}

// 0 for CRC and entry indicates a match based entirely on the elf name
PS2_REGISTER_GAME_OVERRIDE("Star Wars Episode 3 SIF Fixes", "SLES_531.55", 0, 0, StarWarsSifOverrides)
