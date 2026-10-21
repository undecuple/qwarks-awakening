/* Host test (no GPU, no game run): the lifted Bink IDCT put 0x0080D5B8 (via wrapper 0x0080D988, intra) and IDCT add 0x0080DDD8
 * (inter) must equal a floating-point orthonormal 8x8 inverse DCT within +-2 on random blocks, including NEGATIVE coefficients
 * and negative DC (inter blocks carry signed DCs) and the column DC-only shortcut.
 *
 * Why (rc-runtime pass 5): the green Ratchet legs are chroma planes ~40 levels (a DC of ~320) away from their surroundings; the IDCT
 * add is the only place a signed DC meets the plane. Pass 24 compared the lifted IDCT with ffmpeg by eye (T4 never run).
 *
 * Model (read off the lifted code, ASSUMPTION where noted): block = int16[64], index = row * 8 + col (row = vertical frequency);
 * dequantised coefficient d = (int16(c) * Q[q][i]) >> 11 (arithmetic), Q = int32 table of 16 * 64 entries; put reads Q at
 * [r2 - 0x3108] + 0x1000, add at [r2 - 0x3108] + 0 (q << 8 bytes per set); pixel = round(IDCT_ortho(d / (aan[u] aan[v])) / 32) modulo 256: the lifted put and add both store the LOW BYTE, there is no clamp in either
 * (put measured: a block that overshoots -7 stores 249; ffmpeg's put clamps, ASSUMPTION from memory, unchecked; the original's behaviour is unknown, the
 * lifted code is a straight translation). The generator below keeps most blocks inside 0..255; overshooting ones are compared modulo 256; add: pixel =
 * clamp(src + that). Q here is synthetic (the test never reads the game's tables). Signatures: put wrapper f(dst, stride, block, q);
 * add f(dst, stride, block, q, src, src_stride).
 * Run: tests/run_bink_tests.sh idct. Exit 0 = PASS. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <sys/mman.h>
#define PPU_VMI_DISABLE 1
#include BINK_LIFTED_SUBSET
#undef vm_read8
#undef vm_read16
#undef vm_read32
#undef vm_read64

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

static uint64_t rng_s = 0x2545F4914F6CDD1Dull;
static uint32_t rnd() { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17; return (uint32_t)(rng_s >> 16); }
enum { TOC = 0x00080000, QBASE = 0x00090000, BLOCK = 0x000A0000, DST = 0x000B0000, SRC = 0x000C0000, STACK = 0x00F00000 };

static void run(ppu_context& c, void (*fn)(ppu_context*))
{
    c.lr = 0x1234; fn(&c);
    while (g_trampoline_fn) { void (*t)(void*) = g_trampoline_fn; g_trampoline_fn = 0; t(&c); }
}

int main()
{
    vm_base = (uint8_t*)mmap(0, 0x01000000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (vm_base == MAP_FAILED) { perror("mmap"); return 2; }
    vm_write32(TOC - 0x3108, QBASE);
    static int32_t Qin[16][64], Qintra[16][64];
    for (int q = 0; q < 16; q++) for (int i = 0; i < 64; i++) { Qin[q][i] = 2048 + 40 * q + i; Qintra[q][i] = 2048 + 24 * q - i; }
    /* With RC_EBOOT_ELF=<decrypted EBOOT.elf> the REAL quantiser tables are read at run time (rodata 0x10046200 inter = [TOC 0x8afe5c - 0x3108], 0x10047200 intra = +0x1000;
     * file offset = 0x8a0000 + (va - 0x10000000), ASSUMPTION: the segment layout of the ToD ELF) and the AC blocks are compared too: the
     * transform is not orthonormal, the per-frequency (AAN-style) scale lives in these tables, so a float model is only valid with them.
     * Without the ELF only DC-only blocks are compared (synthetic tables). Nothing from the ELF is stored. */
    bool real_q = false;
    if (const char* elf = getenv("RC_EBOOT_ELF")) {
        if (FILE* f = fopen(elf, "rb")) {
            static uint8_t raw[0x2000];
            if (fseek(f, 0x8a0000 + 0x46200, SEEK_SET) == 0 && fread(raw, 1, sizeof raw, f) == sizeof raw) {
                for (int q = 0; q < 16; q++) for (int i = 0; i < 64; i++) {
                    const uint8_t* a = raw + q * 256 + i * 4; const uint8_t* b = raw + 0x1000 + q * 256 + i * 4;
                    Qin[q][i] = (int32_t)((a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3]);
                    Qintra[q][i] = (int32_t)((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
                }
                real_q = true;
            }
            fclose(f);
        }
        if (!real_q) { fprintf(stderr, "RC_EBOOT_ELF set but unreadable\n"); return 2; }
    }
    printf("quantiser tables: %s\n", real_q ? "real (from RC_EBOOT_ELF)" : "synthetic, DC-only blocks compared");
    for (int q = 0; q < 16; q++) for (int i = 0; i < 64; i++) {
        vm_write32(QBASE + q * 256 + i * 4, (uint32_t)Qin[q][i]);
        vm_write32(QBASE + 0x1000 + q * 256 + i * 4, (uint32_t)Qintra[q][i]);
    }
    double cs[8][8], al[8], aan[8];
    for (int k = 0; k < 8; k++) aan[k] = k ? sqrt(2.0) * cos(k * M_PI / 16.0) : 1.0;   /* AAN scale folded into the real tables: Q = step * aan[u] * aan[v] */
    for (int k = 0; k < 8; k++) { al[k] = k ? 0.5 : sqrt(0.125); for (int x = 0; x < 8; x++) cs[k][x] = cos((2 * x + 1) * k * M_PI / 16.0); }
    long runs[2] = {0, 0}, fails[2] = {0, 0}, fstyle[2][5] = {}; int worst[2] = {0, 0};
    const char* nm[2] = { "put", "add" };
    for (int it = 0; it < 40000; it++) {
        const int add = it & 1, q = rnd() % 16, stride = 8 + (rnd() % 4) * 8, sstride = 8 + (rnd() % 4) * 8;
        int16_t blk[64]; memset(blk, 0, sizeof blk);
        const int style = rnd() % 5;
        /* DC: put -> a pixel level 40..215, add -> a signed residual of -60..+60 levels (negative ones included); real tables: the
         * stored value is the level divided by the table entry (the encoder's quantiser), synthetic tables: the level scaled directly. */
        const int32_t Qdc = add ? Qin[q][0] : Qintra[q][0];
        const int level = add ? (int)(rnd() % 121) - 60 : 40 + (int)(rnd() % 176);
        blk[0] = (int16_t)lround(level * 256.0 * 2048.0 / Qdc);
        if (style == 0 && add) blk[0] = (int16_t)(-llabs(blk[0]));   /* negative DC only */
        const int nac = (style == 1 || !real_q) ? 0 : style == 2 ? 3 : style == 3 ? 8 : 12;     /* style 1: DC-only block */
        for (int k = 0; k < nac; k++) {
            const int i = 1 + rnd() % 63, Q = add ? Qin[q][i] : Qintra[q][i];
            const int amp = (int)(400.0 * 2048.0 / Q) > 0 ? (int)(400.0 * 2048.0 / Q) : 1;   /* |dequantised| <= ~400: ~3 levels per coefficient */
            blk[i] = (int16_t)((int)(rnd() % (2 * amp + 1)) - amp);
        }
        for (int i = 0; i < 64; i++) vm_write16(BLOCK + 2 * i, (uint16_t)blk[i]);
        uint8_t srcpx[8][8];
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { srcpx[y][x] = (uint8_t)rnd(); vm_base[SRC + y * sstride + x] = srcpx[y][x]; }
        memset(vm_base + DST, 0x5A, 8 * stride + 8);
        ppu_context c; memset(&c, 0, sizeof c);
        c.gpr[1] = STACK; c.gpr[2] = TOC; c.gpr[3] = DST; c.gpr[4] = (uint64_t)stride; c.gpr[5] = BLOCK; c.gpr[6] = (uint64_t)q;
        if (add) { c.gpr[7] = SRC; c.gpr[8] = (uint64_t)sstride; run(c, func_0080DDD8); } else run(c, func_0080D988);
        double d[8][8];
        for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) {
            const int32_t Q = add ? Qin[q][v * 8 + u] : Qintra[q][v * 8 + u];
            d[v][u] = (double)(((int64_t)blk[v * 8 + u] * Q) >> 11) / (real_q ? aan[u] * aan[v] : 1.0);
        }
        bool bad = false; int maxerr = 0; char why[160] = "";
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
            double s = 0;
            for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) s += al[u] * al[v] * d[v][u] * cs[u][x] * cs[v][y];
            s /= 32.0;
            const int want = ((add ? srcpx[y][x] : 0) + (int)lround(s)) & 255, got = vm_base[DST + y * stride + x];
            int e = abs(got - want); if (e > 128) e = 256 - e;   /* both stores keep the low byte (no clamp anywhere in the lifted put/add; see header) */
            if (e > maxerr) maxerr = e;
            if (e > 2 && !bad) { bad = true; snprintf(why, sizeof why, "px(%d,%d) got %d want %d (dc %d style %d q %d)", x, y, got, want, blk[0], style, q); }
        }
        runs[add]++; if (maxerr > worst[add]) worst[add] = maxerr;
        if (bad) { fstyle[add][style]++; if (fails[add] < 6) printf("FAIL %s it=%d: %s\n", nm[add], it, why); fails[add]++; }
    }
    for (int k = 0; k < 2; k++) printf("idct %s vs float model: %ld blocks, %ld outside +-2, worst error %d\n", nm[k], runs[k], fails[k], worst[k]);
    for (int k = 0; k < 2; k++) printf("  %s failures by style (0 neg dc, 1 dc only, 2 3ac, 3 12ac, 4 40ac): %ld %ld %ld %ld %ld\n", nm[k], fstyle[k][0], fstyle[k][1], fstyle[k][2], fstyle[k][3], fstyle[k][4]);
    const int ok = !fails[0] && !fails[1];
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
