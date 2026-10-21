/* Ratchet & Clank Future: Tools of Destruction (rc-re-data rounds 11-12, installed 2026-10-20): host body for port/rc/tods/ppu_overrides.txt line 007E3FA0.
 * Goes into port/rc/tods/tods_ppu_overrides.cpp, which the lift's ppu_recomp.h makes room for with PPU_OVERRIDE_007E3FA0.
 *
 * What the original is (read off the binary, @0x007e3fa0, 98 instructions): Edge zlib's "add inflate queue element". It
 *   - checks  compressedSize (r5) <= 0x10000 (@0x7e3fb4), outSize (r7) <= 0x10000 (@0x7e3ffc),
 *     counter (r8) NULL or *counter != 0 (@0x7e4004-0x7e4014), compressedSize <= outSize (warning only, @0x7e4018),
 *     counter 4-aligned (@0x7e4020); each failure prints through 0x826870 / 0x81c618;
 *   - builds a 32-byte element at sp+0x70 (@0x7e402c-0x7e404c): +0x00 eaIn (r4), +0x04 eaOut (r6), +0x08 compressedSize (r5),
 *     +0x0C outSize (r7), +0x10 eaCounter | mode (r8 | caller stack word at sp+0x74), +0x14 eaEventFlag (r9), +0x18 u16 bits (r10);
 *     lwsync; _cellSpursLFQueuePushBody(queue (r3), &element, 3) (@0x7e4060); a non-zero result prints an error.
 *   The 9th argument (mode) is the low word of the caller's stack slot at sp+0x70, i.e. gpr[1]+0x74 at entry.
 * Callers (read off the binary): the manager's submit method @0x616da8 (call @0x616ebc: eaIn = src+2, mode 1 (zlib header skipped by the
 *   caller), counter = this+0x2c00, event flag = this+0x2980, bits = slot u16 @+0x1e) and its wait method @0x6173c8 (call @0x6174cc: the bounce copy,
 *   compressedSize == outSize, mode 0). The consumer is the EDGE inflate SPU task; it would inflate, decrement *counter and set the event-flag bits.
 * Host design = KZ3 option B (Killzone 3 the RE notes 1.5): do not push; inflate on the host, then do what the task does.
 *   READ OFF THE EDGE INFLATE TASK (pass 12, ELF edgezlib_inflate_task.spu.elf at LS 0x3000, lifted as work/spu/tods/lift/elf_008ea200):
 *   (1) mode = bit 0 of the u32 at element +0x10 (counter EA = word & ~1, @LS 0x345c `andi r80,r90,-2`). Bit 0 set -> inflate: the branch
 *       @0x3364 `brhz` goes to 0x36a0 and calls edgeZlibInflateRawData @0x3980, which calls inflateInit2_ with windowBits **-15** (@0x39f0 `il r4,-15`,
 *       version string "1.2.3.0-PS3-SPU-EDGE") = RAW deflate, never a zlib header. Bit 0 clear -> stored copy: @0x3368 `ceq csize,osize`; unequal ->
 *       assert edgezlib_inflate_task.cpp:146 (LS 0xADA0) and error -3; equal -> 16-byte quadword copy loop @0x33d8-0x33f4. There is no zlib-header
 *       mode. The element has no skip fields: only +0x00..+0x18 are loaded (@0x3326-0x3288); bytes +0x1A/+0x1C are never read.
 *   (2) After the output DMA: if the counter EA is non-zero the task calls the atomic decrement @0xA7F0 (GETLLAR/PUTLLC loop) which returns the OLD value;
 *       old == 0 -> assert "work-to-do counter already at zero" (@0x3964); old == 1 (the counter is now 0) -> falls to the event-flag set @0x3498;
 *       old > 1 -> no event-flag set (@0x34b0). With a NULL counter the flag is set directly. So: decrement first, set the bits only when the counter
 *       reached zero (and only if the flag EA is non-zero).
 *   ASSUMPTIONs that remain (not read off the binary): (a) the call at 0x9308 is cellSpursEventFlagSet(flagEA, bits) (its arguments are the flag EA and the
 *   u16 bits; not decoded further); (b) host atomics on the big-endian word are compatible with the lifted lwarx/stwcx. the callers use on the same counter
 *   (@0x616e5c); (c) the HLE cellSpursEventFlagSet (libs/spurs/cellSpurs.c:3041) wakes a PPU thread blocked in cellSpursEventFlagWait on the same flag.
 * Off switch: TODS_EDGE_HOST=0 runs the lifted body (needs _cellSpursLFQueuePushBody and the SPU task). One log line per 1000 requests.
 */
#include "ppu_recomp.h"
#include "ppu_overrides.h"   /* PPU_OVERRIDE_<addr> and the <name>__lifted declarations (patch 3005 keeps them out of ppu_recomp.h) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
extern uint8_t* vm_base;
int stbi_zlib_decode_noheader_buffer(char* obuffer, int olen, const char* ibuffer, int ilen);   /* libs/codec/stb_image.h:535 */
int32_t cellSpursEventFlagSet(void* eventFlagEa, uint16_t bits);                                 /* cellSpurs.c:3041, takes the guest EA */
}

#ifdef PPU_OVERRIDE_007E3FA0
static uint32_t edge_be32(uint32_t ea) { uint32_t v; memcpy(&v, vm_base + ea, 4); return __builtin_bswap32(v); }

static int edge_host_enabled()
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("TODS_EDGE_HOST"); on = !(e && e[0] == '0'); }
    return on;
}

/* @0x007e3fa0  edgeZlibAddInflateQueueElement */
void func_007E3FA0(ppu_context* ctx)
{
    static unsigned long long n_host = 0, n_fallback = 0;
    const uint32_t in = (uint32_t)ctx->gpr[4], csize = (uint32_t)ctx->gpr[5];       /* @0x7e3fe0, @0x7e3fd8 */
    const uint32_t out = (uint32_t)ctx->gpr[6], osize = (uint32_t)ctx->gpr[7];      /* @0x7e3fe4, @0x7e3fe8 */
    const uint32_t ctr = (uint32_t)ctx->gpr[8], evf = (uint32_t)ctx->gpr[9];        /* @0x7e3fec, @0x7e3ff0 */
    const uint16_t bits = (uint16_t)ctx->gpr[10];                                    /* @0x7e3ff4 (sth @0x7e404c) */
    const uint32_t mode = edge_be32((uint32_t)ctx->gpr[1] + 0x74);                   /* @0x7e402c lwz 0x154(r1) in the callee frame */
    static thread_local uint8_t scratch[0x10000];                                    /* outSize <= 0x10000 (@0x7e3ffc) */

    /* Everything the original asserts on goes to the original, which reports it. */
    const bool ok_args = edge_host_enabled() && vm_base && csize <= 0x10000u && osize <= 0x10000u &&           /* @0x7e3fb4, @0x7e3ffc */
                         (ctr == 0 || ((ctr & 3u) == 0 && edge_be32(ctr) != 0)) &&                             /* @0x7e4004-0x7e4014, @0x7e4020 */
                         (uint64_t)in + csize < 0xFFFF0000ull && (uint64_t)out + osize < 0xFFFF0000ull &&
                         (mode == 1 || (mode == 0 && csize == osize));        /* task: bit 0 = inflate; clear = stored copy, sizes must match (LS 0x3368, assert :146) */
    int got = -1;
    if (ok_args) {
        if (mode == 1) got = stbi_zlib_decode_noheader_buffer((char*)scratch, (int)osize, (const char*)(vm_base + in), (int)csize);   /* raw deflate, windowBits -15 (LS 0x39f0) */
        else { memcpy(scratch, vm_base + in, osize); got = (int)osize; }                                           /* stored copy (LS 0x33d8) */
    }
    if (!ok_args || got != (int)osize) {    /* inflate error or a size that differs from the request: the original (SPU task) path */
        if ((++n_fallback & 0x3FF) == 1) fprintf(stderr, "[edge] fallback to the lifted push n=%llu (args_ok=%d inflated=%d expected=%u)\n", n_fallback, (int)ok_args, got, osize);
        func_007E3FA0__lifted(ctx);
        return;
    }
    memcpy(vm_base + out, scratch, osize);                                                                           /* the SPU's output DMA */
    bool set_flag = true;                                                                                            /* the task: LS 0x3498 */
    if (ctr) {                                                                                                       /* the SPU's atomic decrement (LS 0xA7F0): returns the OLD value */
        uint32_t* p = (uint32_t*)(vm_base + ctr);
        uint32_t old = __atomic_load_n(p, __ATOMIC_RELAXED), v;
        for (;;) {
            v = __builtin_bswap32(old);
            if (!v) break;                                                                                           /* never reached: checked above */
            if (__atomic_compare_exchange_n(p, &old, __builtin_bswap32(v - 1), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) break;
        }
        set_flag = (v == 1);                                                                                         /* LS 0x3530 `ceqi r88,r3,1`: the flag is set only when the counter reached 0 */
    }
    if (set_flag && evf && bits) cellSpursEventFlagSet((void*)(uintptr_t)evf, bits);                                 /* LS 0x34a8 brsl 0x9308 (ASSUMPTION (a)); NULL flag allowed (LS 0x349c) */
    if ((++n_host % 1000) == 0) fprintf(stderr, "[edge] host inflate n=%llu fallback=%llu\n", n_host, n_fallback);
    ctx->gpr[3] = 0;                                                                                                 /* callers ignore the result (@0x616ec4) */
}
#endif

#include "tods_bink_dump.inc"   /* rc-runtime pass 4: RC_BINK_DUMP diagnostic (default off) */
