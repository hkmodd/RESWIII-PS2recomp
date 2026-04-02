#include "game_overrides.h"
#include "ps2_syscalls.h"
#include "ps2_runtime.h"

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
    }
}

// 0 for CRC and entry indicates a match based entirely on the elf name
PS2_REGISTER_GAME_OVERRIDE("Star Wars Episode 3 SIF Fixes", "SLES_531.55", 0, 0, StarWarsSifOverrides)
