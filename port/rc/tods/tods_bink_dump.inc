/* Ratchet & Clank Future: Tools of Destruction, R&C pass 4: T5 diagnostic, DEFAULT OFF (RC_BINK_DUMP unset = a flag test
 * and a tail call into the lifted body). Included from tods_ppu_overrides.cpp. Needs the lines of port/rc/tods/ppu_overrides.txt for
 * 00801E88 007FD2C8 007FCCD8 007FC188 007FC550 0080FC48 (the lifter then emits func_<addr>__lifted and PPU_OVERRIDE_<addr>).
 *
 * What it records (R&C rounds 23-24: 0x801E88 = the per-plane Bink decoder, 0x7FD2C8 = bundle/tree init, 0x7FCCD8 = read_dcs,
 * 0x7FC188 / 0x7FC550 = read_colors for version >= / < 'i', 0x80FC48 = memset16). The argument meaning is NOT known for the decoder
 * (ASSUMPTION: r3..r10 + the stack words at entry-sp+0x70.. carry the plane pointers/strides and the input), so everything is logged raw
 * and every word that looks like a guest pointer is followed (a few bytes before and after). The offline step matches the values.
 *
 *   RC_BINK_DUMP=<after_seconds>:<n_calls>      start after that many seconds of process time, trace the next n_calls calls of
 *                                               0x801E88 (one call = one plane, ASSUMPTION: 3 per frame) and then stop
 *   RC_BINK_PLANES=<hex EA>,...                 (optional) plane buffers whose bytes are logged after each traced call; default: the
 *                                               two Y, two Cr(unit 1) and two Cb(unit 2) movie buffers seen in runs 6009 (IO offset
 *                                               + 0x20000000); rows 222 (chroma) / 444 (luma), columns 192 / 384, 64 bytes
 *   RC_BINK_ROW=<chroma row>:<chroma col>       (optional) default 222:192 (the green leg block)
 * Output (under scratch/, relative to the cwd, capped): bink_dump.txt (text, <= 20000 lines) and, pass 5, bink_frame_<call>.bin: the
 * compressed video data of the frame, bytes [r8 - 4, r9) of the first plane call of each frame (call % 3 == 1; <= 8 files, <= 256 KiB each).
 * ASSUMPTION (read off the 2026-10-21 dump, exe 744dc7e2): r8 = the plane's stream pointer, just past its 32-bit size word (Y: size word
 * 0xB380 at r8 - 4, and the U stream starts at r8 - 4 + 0xB380 = the next call's r8), r9 = the end of the frame's video data (the same for the
 * three plane calls of a frame). Pass 4's bink_stream_<call>.bin windows were the wrong bytes (call 1: an OPD table of the ELF, call 4: heap
 * structures) and are gone; the line "[bink] stream" now only logs the reader's word 1 (the stream pointer at init). Read-only for the guest.
 * Offline: port/rc/tests/bink_dump_check.py. */
#include <chrono>
#if defined(PPU_OVERRIDE_00801E88) && defined(PPU_OVERRIDE_007FD2C8) && defined(PPU_OVERRIDE_007FCCD8) && defined(PPU_OVERRIDE_007FC188) && defined(PPU_OVERRIDE_007FC550) && defined(PPU_OVERRIDE_0080FC48)
namespace bink_dump {
static uint32_t be32(uint32_t ea) { uint32_t v; memcpy(&v, vm_base + ea, 4); return __builtin_bswap32(v); }
static bool ea_ok(uint32_t ea) { return ea >= 0x10000u && ea < 0xF0000000u; }
static bool ptr_like(uint32_t v) { return ea_ok(v) && v < 0x60000000u; }

static FILE* g_f; static int g_state;      /* 0 = unread env, 1 = off, 2 = armed, 3 = tracing window open, 4 = done */
static unsigned g_after_s, g_ncalls, g_call, g_traced, g_lines, g_streams;
static bool g_in; static bool g_first_init; static unsigned g_dcs, g_cols, g_ms16;
static unsigned g_row = 222, g_col = 192;
static uint32_t g_planes[16]; static unsigned g_nplanes;
static uint64_t g_t0;

static uint64_t now_ms() { return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static void init()
{
    g_state = 1;
    const char* e = getenv("RC_BINK_DUMP");
    if (!e || !*e) return;
    unsigned a = 0, n = 0;
    if (sscanf(e, "%u:%u", &a, &n) != 2 || !n) { fprintf(stderr, "[bink] RC_BINK_DUMP needs <after_seconds>:<n_calls>, off\n"); return; }
    g_after_s = a; g_ncalls = n; g_t0 = now_ms();
    static const uint32_t def[] = { 0x22909500u, 0x22A5AD00u, 0x229EA500u, 0x22B3BD00u, 0x22A22900u, 0x22B74100u };
    const char* p = getenv("RC_BINK_PLANES");
    if (p && *p) { while (*p && g_nplanes < 16) { g_planes[g_nplanes++] = (uint32_t)strtoul(p, (char**)&p, 16); if (*p == ',') p++; else break; } }
    else for (uint32_t d : def) g_planes[g_nplanes++] = d;
    const char* r = getenv("RC_BINK_ROW"); if (r) sscanf(r, "%u:%u", &g_row, &g_col);
    g_f = fopen("scratch/bink_dump.txt", "a");
    if (!g_f) { fprintf(stderr, "[bink] cannot open scratch/bink_dump.txt, off\n"); return; }
    g_state = 2;
    fprintf(stderr, "[bink] T5 dump armed: after %us, %u calls, %u plane buffers\n", a, n, g_nplanes);
}
/* true while a traced call is running; opens/closes the window on the 0x801E88 entry only */
static bool window_entry()
{
    if (g_state == 0) init();
    if (g_state == 2 && now_ms() - g_t0 >= (uint64_t)g_after_s * 1000) g_state = 3;
    if (g_state == 3 && g_traced >= g_ncalls) { g_state = 4; fprintf(g_f, "[bink] window done after %u calls\n", g_traced); fflush(g_f); }
    return g_state == 3;
}
static void hex(const char* tag, uint32_t ea, unsigned n)
{
    if (g_lines >= 20000 || !ea_ok(ea) || !ea_ok(ea + n)) return;
    g_lines++;
    fprintf(g_f, "%s ea=%08X:", tag, ea);
    for (unsigned i = 0; i < n; i++) fprintf(g_f, "%s%02X", (i & 15) ? "" : " ", vm_base[ea + i]);
    fputc('\n', g_f);
}
static void words(const char* tag, uint32_t ea, unsigned nw)
{
    if (g_lines >= 20000 || !ea_ok(ea)) return;
    g_lines++;
    fprintf(g_f, "%s ea=%08X words:", tag, ea);
    for (unsigned i = 0; i < nw; i++) fprintf(g_f, " %08X", be32(ea + 4 * i));
    fputc('\n', g_f);
}
/* a structure and everything in it that looks like a pointer: 64 bytes before and 64 after it */
static void chase(const char* tag, uint32_t s, unsigned nw)
{
    words(tag, s, nw);
    for (unsigned i = 0; i < nw; i++) {
        const uint32_t v = be32(s + 4 * i);
        if (!ptr_like(v) || (v >= s && v < s + 4 * nw)) continue;
        char t[48]; snprintf(t, sizeof t, "%s+%02X->", tag, 4 * i);
        hex(t, v >= 64 ? v - 64 : v, 128);
    }
}
static void stream_window(uint32_t s, unsigned call)
{
    /* the bit reader at s: +4 = the stream pointer at init (pass 5: pass 4's "lowest pointer" heuristic picked a code table and a heap pointer) */
    fprintf(g_f, "[bink] stream call=%u struct=%08X ptr(+4)=%08X ptr(+C)=%08X\n", call, s, be32(s + 4), be32(s + 0xC));
}
/* the frame's compressed video data: [r8 - 4, r9) at the first plane call of a frame */
static void frame_stream(unsigned call, uint32_t r8, uint32_t r9)
{
    if (call % 3 != 1 || g_streams >= 8) return;
    if (!ptr_like(r8) || !ptr_like(r9) || r9 <= r8 || r9 - r8 > 0x40000u) {
        fprintf(g_f, "[bink] frame-stream call=%u: r8=%08X r9=%08X do not look like a stream range, not written\n", call, r8, r9); return; }
    char fn[64]; snprintf(fn, sizeof fn, "scratch/bink_frame_%03u.bin", call);
    FILE* f = fopen(fn, "wb");
    if (f) { fwrite(vm_base + r8 - 4, 1, r9 - r8 + 4, f); fclose(f); g_streams++; }
    fprintf(g_f, "[bink] frame-stream call=%u start=%08X (size word %08X) end=%08X bytes=%u %s\n", call, r8 - 4, be32(r8 - 4), r9, r9 - r8 + 4, f ? "written" : "NOT written");
}
}   /* namespace bink_dump */

/* @0x00801E88  Bink plane decoder */
void func_00801E88(ppu_context* ctx)
{
    using namespace bink_dump;
    if (!window_entry()) { func_00801E88__lifted(ctx); return; }
    const unsigned call = ++g_call; g_traced++;
    const uint32_t sp = (uint32_t)ctx->gpr[1];
    fprintf(g_f, "[bink] call=%u t=%llums lr=%08X sp=%08X r3..r10 = %llX %llX %llX %llX %llX %llX %llX %llX\n", call,
            (unsigned long long)(now_ms() - g_t0), (uint32_t)ctx->lr, sp,
            (unsigned long long)ctx->gpr[3], (unsigned long long)ctx->gpr[4], (unsigned long long)ctx->gpr[5], (unsigned long long)ctx->gpr[6],
            (unsigned long long)ctx->gpr[7], (unsigned long long)ctx->gpr[8], (unsigned long long)ctx->gpr[9], (unsigned long long)ctx->gpr[10]);
    words("[bink] stack-args", sp + 0x70, 8);
    for (int r = 3; r <= 10; r++) {            /* every pointer-like argument: 64 bytes at it */
        const uint32_t v = (uint32_t)ctx->gpr[r];
        if (ptr_like(v)) { char t[32]; snprintf(t, sizeof t, "[bink] arg r%d", r); hex(t, v, 64); }
    }
    for (int i = 0; i < 8; i++) {
        const uint32_t v = be32(sp + 0x70 + 4 * i);
        if (ptr_like(v)) { char t[32]; snprintf(t, sizeof t, "[bink] stackarg%d", i); chase(t, v, 8); }
    }
    frame_stream(call, (uint32_t)ctx->gpr[8], (uint32_t)ctx->gpr[9]);
    g_in = true; g_first_init = true; g_dcs = g_cols = g_ms16 = 0;
    func_00801E88__lifted(ctx);
    g_in = false;
    fprintf(g_f, "[bink] call=%u exit r3=%llX\n", call, (unsigned long long)ctx->gpr[3]);
    for (unsigned i = 0; i < g_nplanes; i++) {
        const bool luma = i < 2;
        const uint32_t base = g_planes[i], row = luma ? 2 * g_row : g_row, col = luma ? 2 * g_col : g_col, w = luma ? 1280u : 640u;
        char t[48]; snprintf(t, sizeof t, "[bink] plane%u %s row%u col%u", i, luma ? "Y" : (i < 4 ? "U1" : "U2"), row, col);
        hex(t, base + row * w + col, 64);
    }
    fflush(g_f);
}
/* @0x007FD2C8  bundle / tree init: the first call of a traced plane call shows the bit reader (r3) */
void func_007FD2C8(ppu_context* ctx)
{
    using namespace bink_dump;
    if (g_in && g_first_init) {
        g_first_init = false;
        const uint32_t s = (uint32_t)ctx->gpr[3];
        fprintf(g_f, "[bink] init r3..r6 = %llX %llX %llX %llX\n", (unsigned long long)ctx->gpr[3], (unsigned long long)ctx->gpr[4],
                (unsigned long long)ctx->gpr[5], (unsigned long long)ctx->gpr[6]);
        chase("[bink] bitreader", s, 16);
        stream_window(s, g_call);
    }
    func_007FD2C8__lifted(ctx);
}
#define BINK_READER(ADDR, TAG, CNT)                                                                                          \
void func_##ADDR(ppu_context* ctx)                                                                                           \
{                                                                                                                            \
    using namespace bink_dump;                                                                                               \
    if (!g_in || CNT >= 40) { func_##ADDR##__lifted(ctx); return; }                                                          \
    CNT++;                                                                                                                   \
    const uint32_t b = (uint32_t)ctx->gpr[3];                                                                                \
    fprintf(g_f, "[bink] " TAG " #%u in r3..r6 = %llX %llX %llX %llX\n", CNT, (unsigned long long)ctx->gpr[3], (unsigned long long)ctx->gpr[4], \
            (unsigned long long)ctx->gpr[5], (unsigned long long)ctx->gpr[6]);                                               \
    words("[bink] " TAG " bundle-before", b, 16);                                                                            \
    func_##ADDR##__lifted(ctx);                                                                                              \
    chase("[bink] " TAG " bundle-after", b, 16);                                                                             \
}
/* @0x007FCCD8 read_dcs (r3 = DC bundle on the decoder's stack: +0x220 intra, +0xB4 inter) */
BINK_READER(007FCCD8, "read_dcs", g_dcs)
/* @0x007FC188 read_colors, version >= 'i';  @0x007FC550 version < 'i' */
BINK_READER(007FC188, "read_colors_i", g_cols)
BINK_READER(007FC550, "read_colors_old", g_cols)
/* @0x0080FC48 memset16(dest, value16, count): every constant-DC stretch */
void func_0080FC48(ppu_context* ctx)
{
    using namespace bink_dump;
    if (g_in && g_ms16 < 400) { g_ms16++; fprintf(g_f, "[bink] memset16 dest=%08X v=%04X n=%u\n", (uint32_t)ctx->gpr[3], (unsigned)(ctx->gpr[4] & 0xFFFF), (unsigned)(uint32_t)ctx->gpr[5]); }
    func_0080FC48__lifted(ctx);
}
#endif
