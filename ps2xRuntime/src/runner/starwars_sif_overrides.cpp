#include "game_overrides.h"
#include "ps2_syscalls.h"
#include "ps2_runtime.h"
#include <thread>
#include <chrono>
#include <algorithm>

namespace
{
    void StarWarsSifOverrides(PS2Runtime &runtime)
    {
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
            
            // Check if this is the cdvdman client
            // We can read sid from the client tracker in ps2_syscalls (but it's protected).
            // Fast check: we know from tracing client == 0x1B4DC0 for CDVD
            if (clientPtr == 0x1B4DC0) {
                // Read recvBuf / recvSize from stack (O32 convention)
                uint32_t sp = getRegU32(ctx, 29);
                uint32_t recvBuf = runtimePtr->memory().read32(sp + 0x14);
                uint32_t recvSize = runtimePtr->memory().read32(sp + 0x18);

                static bool cored_opened = false;

                if (rpcNum == 0x0) { // sceCdOpen (CORED.BIN)
                    char filename[64] = {0};
                    if (sendBuf) {
                        for(int i=0; i<63; i++) {
                            filename[i] = runtimePtr->memory().read8(sendBuf + 4 + i); // sendBuf might have header
                            if (filename[i] == 0) break;
                        }
                    }
                    if (strstr(filename, "CORED.BIN")) {
                        cored_opened = true;
                    }
                    if (recvBuf && recvSize >= 4) runtimePtr->memory().write32(recvBuf, 1);
                }
                else if (rpcNum == 0x4) { // Read / GetStat
                    // Se stiamo leggendo CORED, restituiamo 1 ma non sporchiamo RAM, 
                    // oppure simuliamo successo continuo.
                    if (recvBuf && recvSize >= 4) runtimePtr->memory().write32(recvBuf, 1);
                }
                else {
                    // 0xFF (Init) or 0x01 (SearchFile)
                    if (recvBuf && recvSize >= 4) runtimePtr->memory().write32(recvBuf, 1);
                }

                // Sblocca il thread dispatcher CDVD che fa WaitSema(3)
                // Salviamo a0, chiamiamo iSignalSema, lo ripristiniamo (anche se non serve)
                __m128i old_a0 = ctx->r[4];
                ctx->r[4] = _mm_set_epi64x(0, 3LL); // 3 = CDVD Semaphore ID
                ps2_syscalls::iSignalSema(rdram, ctx, runtimePtr);
                ctx->r[4] = old_a0;

                setReturnS32(ctx, 0); // Success
                ctx->pc = getRegU32(ctx, 31); // return via ra
                return;
            }

            // Fallback per tutte le altre RPC (IOP, audio, ecc.)
            ps2_syscalls::SifCallRpc(rdram, ctx, runtimePtr);
            ctx->pc = getRegU32(ctx, 31); // return via ra
        };
        runtime.registerFunction(0x00114838, overrideSifCallRpc);

        // _sceSifLoadModule (0x119360)
        static auto overrideSifLoadModule = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtimePtr) {
            ps2_syscalls::SifLoadModule(rdram, ctx, runtimePtr);
            ctx->pc = getRegU32(ctx, 31); // return via ra
        };
        runtime.registerFunction(0x00119360, overrideSifLoadModule);

        // _sceSifInitRpc (0x118b88) - infinite polling loop waiting for client->server ptr to bound
        static auto overrideSifInitRpc = [](uint8_t*, R5900Context* ctx, PS2Runtime*) {
            ctx->pc = getRegU32(ctx, 31); // return via ra
        };
        runtime.registerFunction(0x00118b88, overrideSifInitRpc);
        // Prevent infinite loop waiting for SIF RPC init at 0x114088
        // The HLE completes the operation synchronously without updating the wait flag.
        runtime.memory().write32(0x114088, 0x00000000); // NOP out 'beq v0, zero, 0x114080'

        // sceSifCheckStatRpc (0x114a28) — guest MIPS version reads SIF packet
        // fields in RDRAM which our HLE SifCallRpc never creates.  Since we
        // complete all RPCs synchronously, always report "complete" (return 0).
        static auto overrideSifCheckStatRpc = [](uint8_t*, R5900Context* ctx, PS2Runtime*) {
            setReturnS32(ctx, 0); // 0 = complete, 1 = still in progress
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x00114a28, overrideSifCheckStatRpc);

        // CD I/O "check pending" helper (0x100e60)
        // When param_1==1 (non-blocking check), this polls sceSifCheckStatRpc
        // AND checks DAT_001304b4.  Our HLE completes RPCs synchronously, but
        // doesn't invoke the IOP end-callback that clears DAT_001304b4.
        // Override to always return 0 (complete).
        static auto overrideCdCheckPending = [](uint8_t*, R5900Context* ctx, PS2Runtime*) {
            setReturnS32(ctx, 0); // 0 = I/O complete
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x00100e60, overrideCdCheckPending);

        // SetAlarm callback at 0x100288 — iSignalSema(param_3); SYNC; EI; return
        // This function is NOT in the recompiled lookup table, so SetAlarm()
        // returns KE_ERROR when given this handler.  Register it as a shim.
        static auto overrideAlarmCallback = [](uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
            // The alarm handler is called as handler(alarmId, ticksRemaining, commonArg)
            // commonArg (a2 = reg 6) is the sema ID.  iSignalSema reads from a0 (reg 4).
            uint32_t semaId = getRegU32(ctx, 6);
            // Move a2 -> a0. R5900 sign-extends 32-bit into 64-bit GPR.
            ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(semaId))); 
            ps2_syscalls::iSignalSema(rdram, ctx, rt);
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x00100288, overrideAlarmCallback);

        // Sleep/delay function at 0x1002b0
        // Original: CreateSema → SetAlarm(ticks, 0x100288, semaId) → WaitSema(semaId) → DeleteSema
        // Since SetAlarm may fail if callbacks are unregistered, override the
        // entire sleep function with a host-side sleep.  1 HSYNC tick ≈ 1/(15734 Hz) s.
        static auto overrideSleepDelay = [](uint8_t*, R5900Context* ctx, PS2Runtime*) {
            uint32_t ticks = getRegU32(ctx, 4) & 0xFFFFu;
            // ~63.5 µs per HSYNC tick on NTSC.  Clamp to avoid long stalls.
            auto us = std::max(1u, ticks) * 64u;
            if (us > 500000u) us = 500000u; // cap at 0.5s
            std::this_thread::sleep_for(std::chrono::microseconds(us));
            ctx->pc = getRegU32(ctx, 31);
        };
        runtime.registerFunction(0x001002b0, overrideSleepDelay);
    }
}

// 0 for CRC and entry indicates a match based entirely on the elf name
PS2_REGISTER_GAME_OVERRIDE("Star Wars Episode 3 SIF Fixes", "SLES_531.55", 0, 0, StarWarsSifOverrides)
