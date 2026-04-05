#include "game_overrides.h"
#include "ps2_syscalls.h"
#include "ps2_runtime.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <fstream>

namespace
{

    extern "C" void ps2_FUN_0067c890_67c890(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime);

    void StarWarsSifOverrides(PS2Runtime &runtime)
    {
        // ────────────────────────────────────────────────────
        //  PRELOAD COREC.BIN AND CORED.BIN
        // ────────────────────────────────────────────────────
        /*
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
            
            loadModule("corec.bin", 0x001c2680);
            loadModule("cored.bin", 0x009bc400);
        }
        */
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

            // Check if this is the cdvdman client
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

            // Fix for custom SIF RPC sid=6 (deadlock / main thread crash)
            // It uses statically allocated clientPtr = 0x9a3c20 or 0x9a3d40
            if (clientPtr == 0x9a3c20 || clientPtr == 0x9a3d40) {
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
                    std::cerr << "[CRT] Entering main — initializing Metrowerks vtable (FUN_0067c890)..." << std::endl;

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
                    // Area above game heap (0x1F00000) is safe & unused.
                    constexpr uint32_t SAFE_VTABLE = 0x1F00000;
                    constexpr uint32_t VTABLE_SIZE = 0x120; // enough for all slots

                    uint32_t vtableBase = rt->memory().read32(0x1af860);
                    if (vtableBase && vtableBase >= 0x1afa00 && vtableBase < 0xa9f580) {
                        // Vtable is in BSS — copy it out
                        for (uint32_t off = 0; off < VTABLE_SIZE; off += 4) {
                            uint32_t val = rt->memory().read32(vtableBase + off);
                            rt->memory().write32(SAFE_VTABLE + off, val);
                        }
                        // Redirect the pointer to the safe copy
                        rt->memory().write32(0x1af860, SAFE_VTABLE);

                        uint32_t slotDc = rt->memory().read32(SAFE_VTABLE + 0xdc);
                        uint32_t slot54 = rt->memory().read32(SAFE_VTABLE + 0x54);
                        std::cerr << "[CRT] VTable RELOCATED from 0x" << std::hex << vtableBase
                                  << " to 0x" << SAFE_VTABLE
                                  << " [+0xdc]=0x" << slotDc
                                  << " [+0x54]=0x" << slot54
                                  << std::dec << std::endl;
                    } else {
                        uint32_t slotDc = vtableBase ? rt->memory().read32(vtableBase + 0xdc) : 0;
                        std::cerr << "[CRT] VTable at 0x" << std::hex << vtableBase
                                  << " [+0xdc]=0x" << slotDc
                                  << std::dec << " (not in BSS, skipping relocation)" << std::endl;
                    }
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

        std::cerr << "[SW3] All overrides registered. Bulk allocator patch applied." << std::endl;
        std::cerr << "[SW3] Heap config: base=0x" << std::hex
                  << runtime.guestHeapBase() << " limit=0x8000000"
                  << std::dec << std::endl;
    }
}

// 0 for CRC and entry indicates a match based entirely on the elf name
PS2_REGISTER_GAME_OVERRIDE("Star Wars Episode 3 SIF Fixes", "SLES_531.55", 0, 0, StarWarsSifOverrides)
