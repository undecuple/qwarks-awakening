/* Host test (no GPU, no game run): the lifted Bink DC bundle reader 0x007FCCD8 (read_dcs) and the memset16 helper it calls
 * (0x0080FC48) must equal an independent C model of the Bink 1 DC bundle reader on random bit streams.
 *
 * Why (R&C pass 5): the green Ratchet legs come from chroma planes the game's own Bink decoder wrote; pass 24 compared the
 * lifted read_dcs with ffmpeg by eye and called it equal, T1 (run it against an oracle) was never done. This is T1.
 *
 * The test compiles the REAL lifted functions: tests/run_bink_read_dcs.sh cuts the file prologue + func_007FCCD8__lifted +
 * func_0080FC48__lifted out of work/recomp/tods/ppu_recomp_06{0,2}.cpp into a scratch dir (lifted code is derived from the
 * game binary and is never committed) and includes the result through BINK_LIFTED_SUBSET. No game data is read.
 *
 * Model (read off the lifted code, 2026-10-21, ASSUMPTION where noted):
 *   bit reader (guest struct at R): +0 cache, +4 next-word pointer, +8 bits left in cache; words are big-endian 32-bit, bits are
 *   taken LSB first (so the stream is the concatenation, per word, of its bits from bit 0 up); position = (ptr - start) * 8 - bits_left.
 *   bundle B: +0 cur_dec, +4 cur_ptr (end), +8 start width, +0xC has_sign, +0x28 count width, +0x2C capacity bytes, +0x30 data base.
 *   read: nothing if cur_dec != cur_ptr; n = bits(count width); n == 0 or n > capacity/2 or reader ptr >= stream end -> bundle empty
 *   (cur_ptr = base, cur_dec = base + 4); else v = bits(start width - has_sign) [+ sign bit if has_sign and v != 0],
 *   then groups of up to 8: w = bits(4); w == 0: all equal v; else per value d = bits(w), d != 0 -> sign bit, v += d, store int16.
 *   cur_dec = base, cur_ptr = base + 2 * n.
 * Build and run: tests/run_bink_read_dcs.sh (gcc and clang). Exit 0 = PASS. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <sys/mman.h>
#define PPU_VMI_DISABLE 1
#include BINK_LIFTED_SUBSET
#undef vm_read8
#undef vm_read16
#undef vm_read32
#undef vm_read64   /* the out-of-line readers below are the fallbacks the inline ones call */

extern "C" {
uint8_t* vm_base;
int vm_inline_ok = 1;
uint32_t ppu_vm_size = 0;
uint32_t ppu_hle_inject_base = 0xF0000000u;
unsigned char g_ppu_vm_page[65536];
PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*) = 0;
uint8_t vm_read8(uint64_t a) { return vm_base[(uint32_t)a]; }
uint16_t vm_read16(uint64_t a) { uint16_t v; memcpy(&v, vm_base + (uint32_t)a, 2); return __builtin_bswap16(v); }
uint32_t vm_read32(uint64_t a) { uint32_t v; memcpy(&v, vm_base + (uint32_t)a, 4); return __builtin_bswap32(v); }
uint64_t vm_read64(uint64_t a) { uint64_t v; memcpy(&v, vm_base + (uint32_t)a, 8); return __builtin_bswap64(v); }
void vm_write8(uint64_t a, uint8_t v) { vm_base[(uint32_t)a] = v; }
void vm_write16(uint64_t a, uint16_t v) { v = __builtin_bswap16(v); memcpy(vm_base + (uint32_t)a, &v, 2); }
void vm_write32(uint64_t a, uint32_t v) { v = __builtin_bswap32(v); memcpy(vm_base + (uint32_t)a, &v, 4); }
void vm_write64(uint64_t a, uint64_t v) { v = __builtin_bswap64(v); memcpy(vm_base + (uint32_t)a, &v, 8); }
int spu_coh_is_reserved(uint32_t) { return 0; }
void ppu_st_coh(uint32_t ea, const void* src, uint32_t n) { memcpy(vm_base + ea, src, n); }
void ppu_store_note(uint32_t, uint32_t) {}
void ps3_indirect_call(ppu_context*) { fprintf(stderr, "unexpected indirect call\n"); exit(2); }
void ps3_indirect_call_to(ppu_context*, uint32_t) { fprintf(stderr, "unexpected indirect call\n"); exit(2); }
void ps3_hle_call(unsigned, ppu_context*) { fprintf(stderr, "unexpected hle call\n"); exit(2); }
}
void func_0080FC48(ppu_context* c) { func_0080FC48__lifted(c); }

static uint64_t rng_s = 0x9E3779B97F4A7C15ull;
static uint32_t rnd() { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17; return (uint32_t)(rng_s >> 16); }

enum { R = 0x00100000, B = 0x00100100, DATA = 0x00110000, STREAM = 0x00200000, STACK = 0x00F00000 };

struct BitWriter {
    std::vector<uint32_t> w; int used = 0;
    void put(uint32_t v, int n) { for (int i = 0; i < n; i++) { if (used % 32 == 0) w.push_back(0); w.back() |= ((v >> i) & 1u) << (used % 32); used++; } }
};
struct BitReader {
    const uint32_t* w; long pos;
    uint32_t get(int n) { uint32_t v = 0; for (int i = 0; i < n; i++, pos++) v |= ((w[pos / 32] >> (pos % 32)) & 1u) << i; return v; }
};

struct Model { bool empty; int n; std::vector<int16_t> out; long pos; };
static Model model(const uint32_t* w, long pos0, int cw, int sw, int sign, int cap_bytes, long end_bits_limit)
{
    BitReader br{ w, pos0 }; Model m{}; m.empty = false;
    int n = (int)br.get(cw);
    if (n == 0 || n > cap_bytes / 2) { m.empty = true; m.pos = br.pos; return m; }
    (void)end_bits_limit;
    int v;
    if (sign) { v = (int)br.get(sw - 1); if (v && br.get(1)) v = -v; } else v = (int)br.get(sw);
    m.out.push_back((int16_t)v);
    int len = n - 1;
    for (int i = 0; i < len; i += 8) {
        int len2 = len - i < 8 ? len - i : 8, bs = (int)br.get(4);
        for (int j = 0; j < len2; j++) {
            if (bs) { int d = (int)br.get(bs); if (d && br.get(1)) d = -d; v += d; }
            m.out.push_back((int16_t)v);
        }
    }
    m.n = n; m.pos = br.pos; return m;
}

static uint32_t g32(uint32_t ea) { return vm_read32(ea); }
static void s32(uint32_t ea, uint32_t v) { vm_write32(ea, v); }

int main()
{
    vm_base = (uint8_t*)mmap(0, 0x01000000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (vm_base == MAP_FAILED) { perror("mmap"); return 2; }
    long fails = 0, runs = 0, nonempty = 0, bailed = 0;
    for (int it = 0; it < 60000; it++) {
        const int sign = rnd() & 1, sw = 8 + rnd() % 5 /* 8..12 */, cw = 6 + rnd() % 6 /* 6..11 */;
        const int cap_bytes = 0x540, pre = rnd() % 32;
        BitWriter bw;
        for (int i = 0; i < pre; i++) bw.put(rnd() & 1, 1);
        const int mode = rnd() % 8;
        int n = mode == 0 ? 0 : mode == 1 ? 673 + rnd() % 300 : 1 + rnd() % (mode < 4 ? 40 : 672);
        bw.put((uint32_t)n & ((1u << cw) - 1), cw);
        bw.put(rnd(), 32); bw.put(rnd(), 32);   /* first value + noise follow as random bits, groups below are random too */
        const int style = rnd() % 4;   /* 0: random bits, 1: many zero widths, 2: small widths, 3: wide widths */
        for (int g = 0; g < 100 && bw.w.size() < 700; g++) {
            const int wsel = style == 0 ? (int)(rnd() % 16) : style == 1 ? ((rnd() % 3) ? 0 : (int)(rnd() % 16)) : style == 2 ? (int)(rnd() % 4) : 8 + (int)(rnd() % 8);
            bw.put((uint32_t)wsel, 4);
            for (int j = 0; j < 8; j++) { const int d = wsel ? (int)(rnd() & ((1u << wsel) - 1)) : 0; if (wsel) bw.put((uint32_t)d, wsel); if (wsel && d) bw.put(rnd() & 1, 1); }
        }
        while (bw.w.size() < 1500) bw.w.push_back(rnd());
        /* guest memory */
        memset(vm_base + R, 0, 0x100); memset(vm_base + B, 0, 0x100); memset(vm_base + DATA - 0x100, 0xAA, 0x1000);
        for (size_t i = 0; i < bw.w.size(); i++) s32(STREAM + 4 * (uint32_t)i, bw.w[i]);
        const uint32_t w0 = bw.w[0];
        /* reader state at 'pre' bits consumed: cache holds the remaining bits of word 0 */
        const int skip = pre % 32;
        s32(R + 0, skip ? (w0 >> skip) : w0); s32(R + 4, STREAM + 4); s32(R + 8, 32 - skip);
        if (skip == 0 && (rnd() & 1)) { s32(R + 0, 0); s32(R + 4, STREAM); s32(R + 8, 0); }   /* cold reader: nothing cached */
        s32(B + 0, 0); s32(B + 4, 0); s32(B + 8, (uint32_t)sw); s32(B + 0xC, sign ? 0x400 : 0);
        s32(B + 0x28, (uint32_t)cw); s32(B + 0x2C, (uint32_t)cap_bytes); s32(B + 0x30, DATA);
        ppu_context ctx; memset(&ctx, 0, sizeof ctx);
        ctx.gpr[1] = STACK; ctx.gpr[3] = B; ctx.gpr[4] = R; ctx.gpr[5] = STREAM + 4 * 1600; ctx.lr = 0x1234;
        func_007FCCD8__lifted(&ctx);
        runs++;
        const long pos_after = (long)((g32(R + 4) - STREAM) / 4) * 32 - (long)g32(R + 8);
        Model m = model(bw.w.data(), pre, cw, sw, sign, cap_bytes, 0);
        const uint32_t cur = g32(B + 0), end = g32(B + 4);
        bool bad = false; char why[160] = "";
        if (m.empty) {
            bailed++;
            if (!(end == DATA && cur == DATA + 4)) { bad = true; snprintf(why, sizeof why, "bail: cur=%x end=%x", cur, end); }
            else if (pos_after != m.pos) { bad = true; snprintf(why, sizeof why, "bail pos %ld vs %ld", pos_after, m.pos); }
        } else {
            nonempty++;
            if (cur != DATA || end != DATA + 2u * m.n) { bad = true; snprintf(why, sizeof why, "ptrs cur=%x end=%x n=%d", cur, end, m.n); }
            else for (int i = 0; i < m.n && !bad; i++) {
                const int16_t got = (int16_t)vm_read16(DATA + 2u * i);
                if (got != m.out[i]) { bad = true; snprintf(why, sizeof why, "value[%d] got %d want %d (n=%d sign=%d sw=%d)", i, got, m.out[i], m.n, sign, sw); }
            }
            if (!bad && pos_after != m.pos) { bad = true; snprintf(why, sizeof why, "reader pos %ld want %ld", pos_after, m.pos); }
        }
        if (bad) { if (fails < 12) printf("FAIL it=%d: %s\n", it, why); fails++; }
    }
    printf("read_dcs vs model: %ld runs (%ld decoded, %ld empty-bundle), %ld mismatches\n", runs, nonempty, bailed, fails);
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
