#include <stdint.h>
#include <stdbool.h>
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

// Emitted C++ backend for entry
static inline __m128i to_m128i(__m128i v) { return v; }
static inline __m128i to_m128i(uint64_t v) { return _mm_cvtsi64_si128(static_cast<int64_t>(v)); }
static inline __m128i to_m128i(uint32_t v) { return _mm_cvtsi64_si128(static_cast<int64_t>(v)); }
static inline __m128i to_m128i(int32_t v) { return _mm_cvtsi64_si128(static_cast<int64_t>(v)); }
extern "C" void entry(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    // TODO: Define context and basic arguments

bb_001001E0:
    // addiu sp,sp,-0x40
    uint32_t v0 = GPR_U32(ctx, 29);
    uint32_t v1 = 0xffffffffffffffc0;
    uint32_t v2 = v0 + v1;
    SET_GPR_U32(ctx, 29, v2);
    // sd ra,0x20(sp)
    uint32_t v3 = GPR_U32(ctx, 29);
    uint32_t v4 = 0x20;
    uint32_t v5 = v3 + v4;
    uint32_t v6 = GPR_U32(ctx, 31);
    WRITE64(v5, v6);
    // sq s1,0x10(sp)
    uint32_t v7 = GPR_U32(ctx, 29);
    uint32_t v8 = 0x10;
    uint32_t v9 = v7 + v8;
    uint32_t v10 = GPR_U32(ctx, 17);
    WRITE128(v9, to_m128i(v10));
    // sq s0,0x0(sp)
    uint32_t v11 = GPR_U32(ctx, 29);
    uint32_t v12 = GPR_U32(ctx, 16);
    WRITE128(v11, to_m128i(v12));
    // addiu a0,sp,0x3c
    uint32_t v13 = GPR_U32(ctx, 29);
    uint32_t v14 = 0x3c;
    uint32_t v15 = v13 + v14;
    SET_GPR_U32(ctx, 4, v15);
    // jal 0x00100250
    uint32_t v16 = 0x1001fc;
    SET_GPR_U32(ctx, 31, v16);
    // _nop
    ctx->pc = 0x100250; // function CALL (stub);

bb_001001FC:
    // li a0,0x1
    uint32_t v17 = 0x0;
    uint32_t v18 = 0x1;
    uint32_t v19 = v17 + v18;
    SET_GPR_U32(ctx, 4, v19);
    // jal 0x003c6c10
    uint32_t v20 = 0x100208;
    SET_GPR_U32(ctx, 31, v20);
    // _nop
    ctx->pc = 0x3c6c10; // function CALL (stub);

bb_00100208:
    // move s1,v0
    uint32_t v21 = GPR_U32(ctx, 2);
    uint32_t v22 = 0x0;
    uint32_t v23 = v21 + v22;
    SET_GPR_U32(ctx, 17, v23);
    // move a0,s1
    uint32_t v24 = GPR_U32(ctx, 17);
    uint32_t v25 = 0x0;
    uint32_t v26 = v24 + v25;
    SET_GPR_U32(ctx, 4, v26);
    // jal 0x003c6b90
    uint32_t v27 = 0x100218;
    SET_GPR_U32(ctx, 31, v27);
    // _nop
    ctx->pc = 0x3c6b90; // function CALL (stub);

bb_00100218:
    // li a0,0x3
    uint32_t v28 = 0x0;
    uint32_t v29 = 0x3;
    uint32_t v30 = v28 + v29;
    SET_GPR_U32(ctx, 4, v30);
    // jal 0x003c6bd0
    uint32_t v31 = 0x100224;
    SET_GPR_U32(ctx, 31, v31);
    // _nop
    ctx->pc = 0x3c6bd0; // function CALL (stub);

bb_00100224:
    // move s0,v0
    uint32_t v32 = GPR_U32(ctx, 2);
    uint32_t v33 = 0x0;
    uint32_t v34 = v32 + v33;
    SET_GPR_U32(ctx, 16, v34);
    // move a0,s0
    uint32_t v35 = GPR_U32(ctx, 16);
    uint32_t v36 = 0x0;
    uint32_t v37 = v35 + v36;
    SET_GPR_U32(ctx, 4, v37);
    // jal 0x003c6ad0
    uint32_t v38 = 0x100234;
    SET_GPR_U32(ctx, 31, v38);
    // _nop
    ctx->pc = 0x3c6ad0; // function CALL (stub);

bb_00100234:
    // ld ra,0x20(sp)
    uint32_t v39 = GPR_U32(ctx, 29);
    uint32_t v40 = 0x20;
    uint32_t v41 = v39 + v40;
    uint64_t v42 = READ64(v41);
    SET_GPR_U64(ctx, 31, v42);
    // lq s1,0x10(sp)
    uint32_t v43 = GPR_U32(ctx, 29);
    uint32_t v44 = 0x10;
    uint32_t v45 = v43 + v44;
    __m128i v46 = READ128(v45);
    SET_GPR_VEC(ctx, 17, v46);
    // lq s0,0x0(sp)
    uint32_t v47 = GPR_U32(ctx, 29);
    __m128i v48 = READ128(v47);
    SET_GPR_VEC(ctx, 16, v48);
    // addiu sp,sp,0x40
    uint32_t v49 = GPR_U32(ctx, 29);
    uint32_t v50 = 0x40;
    uint32_t v51 = v49 + v50;
    SET_GPR_U32(ctx, 29, v51);
    // jr ra
    uint32_t v52 = GPR_U32(ctx, 31);
    // _nop
    return;

}

