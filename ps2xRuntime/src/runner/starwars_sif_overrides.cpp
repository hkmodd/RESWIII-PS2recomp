#include "game_overrides.h"
#include "ps2_syscalls.h"
#include "ps2_runtime.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <iostream>

namespace
{


    void StarWarsSifOverrides(PS2Runtime &runtime)
    {
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

            ctx->pc = getRegU32(ctx, 31); // return via ra
        };
        runtime.registerFunction(0x0012e810, overrideBulkAlloc);


        // ────────────────────────────────────────────────────
        //  Existing overrides (unchanged)
        // ────────────────────────────────────────────────────

        // sceSifInit (0x113858) - SIF initialization stub
        static auto overrideSifInitNoop = [](uint8_t*, R5900Context* ctx, PS2Runtime*) {
            ctx->pc = getRegU32(ctx, 31); // return via ra
        };
        runtime.registerFunction(0x00113858, overrideSifInitNoop);

        // sceSifBindRpc (0x114668)
        static auto overrideSifBindRpc = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            ps2_syscalls::SifBindRpc(rdram, ctx, runtimePtr);
            ctx->pc = getRegU32(ctx, 31); // return via ra
        };
        runtime.registerFunction(0x00114668, overrideSifBindRpc);

        // sceSifCallRpc (0x114838)
        static auto overrideSifCallRpc = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            uint32_t clientPtr = getRegU32(ctx, 4); // a0
            uint32_t rpcNum    = getRegU32(ctx, 5); // a1
            uint32_t mode      = getRegU32(ctx, 6); // a2 
            uint32_t sendBuf   = getRegU32(ctx, 7); // a3

            // Log ALL SifCallRpc calls for diagnostics
            static int rpcLogCount = 0;
            if (rpcLogCount < 50) {
                uint32_t sp = getRegU32(ctx, 29);
                uint32_t recvBuf  = runtimePtr->memory().read32(sp + 0x14);
                uint32_t recvSize = runtimePtr->memory().read32(sp + 0x18);
                std::cerr << "[SifCallRpc] client=0x" << std::hex << clientPtr
                          << " rpcNum=0x" << rpcNum
                          << " mode=0x" << mode
                          << " sendBuf=0x" << sendBuf
                          << " recvBuf=0x" << recvBuf
                          << " recvSize=0x" << recvSize
                          << std::dec << std::endl;
                rpcLogCount++;
            }

            // Check if this is the cdvdman client
            if (clientPtr == 0x1B4DC0) {
                uint32_t sp = getRegU32(ctx, 29);
                uint32_t recvBuf = runtimePtr->memory().read32(sp + 0x14);
                uint32_t recvSize = runtimePtr->memory().read32(sp + 0x18);

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
                    if (recvBuf && recvSize >= 4) runtimePtr->memory().write32(recvBuf, 1);
                }
                else {
                    std::cerr << "[CDVD:RPC] rpcNum=0x" << std::hex << rpcNum << std::dec << std::endl;
                    if (recvBuf && recvSize >= 4) runtimePtr->memory().write32(recvBuf, 1);
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
        //  Enhance EndOfHeap logging: uncap the log limit
        //  (The EndOfHeap syscall in ps2_syscalls_system.inl
        //   already logs, but only first 10 calls. We'll add
        //   a warning log here about the patch.)
        // ────────────────────────────────────────────────────
        std::cerr << "[SW3] All overrides registered. Bulk allocator patch applied." << std::endl;
        std::cerr << "[SW3] Heap config: base=0x" << std::hex
                  << runtime.guestHeapBase() << " limit=0x8000000"
                  << std::dec << std::endl;
    }
}

// 0 for CRC and entry indicates a match based entirely on the elf name
PS2_REGISTER_GAME_OVERRIDE("Star Wars Episode 3 SIF Fixes", "SLES_531.55", 0, 0, StarWarsSifOverrides)
