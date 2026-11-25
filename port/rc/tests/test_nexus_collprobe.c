/* Host test of port/rc/nexus101/nexus101_collprobe.inc (Nexus 1.01 collision-query probe, RC_COLLPROBE, diagnostic).
 *   nix develop .#re --command port/rc/tests/run_nexus_collprobe_test.sh */
#include "../nexus101/nexus101_collprobe.inc"

static int fails;
#define CHECK(c) do { if (c) printf("[PASS] %s\n", #c); else { printf("[FAIL] %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

static uint8_t* mem;
static void w32(uint32_t ea, uint32_t v) { mem[ea] = (uint8_t)(v >> 24); mem[ea + 1] = (uint8_t)(v >> 16); mem[ea + 2] = (uint8_t)(v >> 8); mem[ea + 3] = (uint8_t)v; }
static void w16(uint32_t ea, uint32_t v) { mem[ea] = (uint8_t)(v >> 8); mem[ea + 1] = (uint8_t)v; }
static void wf(uint32_t ea, float f) { uint32_t v; memcpy(&v, &f, 4); w32(ea, v); }

static char buf[1 << 16];
static FILE* out_open(void) { return fmemopen(buf, sizeof buf, "w"); }
static void out_close(FILE* f) { fclose(f); }

int main(void)
{
    mem = (uint8_t*)calloc(1, 0x01000000u);
    if (!mem) return 2;
    cp_state s;

    /* Gate. */
    cp_init(&s, NULL, NULL);      CHECK(s.on == 0);
    cp_init(&s, "0", NULL);       CHECK(s.on == 0);
    cp_init(&s, "1", NULL);       CHECK(s.on == 1 && s.detail_left == 0);
    cp_init(&s, "500", "400.5");  CHECK(s.on == 2 && s.detail_left == 500 && s.after_s > 400.4 && s.after_s < 400.6);

    /* func_005974CC's slot: world not set up, then a pool at 0x00100000 with its table at 0x00200000. */
    CHECK(cp_slot(mem, 0x00100185u) == -2);
    w32(CP_WORLD + 0x08, 0x00100000u);
    w32(CP_WORLD + 0x9C, 0x00200000u);
    w16(0x00200000u + 3 * 2, 0x0025);
    w16(0x00200000u + 4 * 2, 0xFFFF);
    w16(0x00200000u + 0x1FFFE, 0x0007);                  /* idx -1 wraps like rlwinm 1,15,30 */
    CHECK(cp_slot(mem, 0) == -1);
    CHECK(cp_slot(mem, 0x00100000u + 3 * 128 + 5) == 0x25);
    CHECK(cp_slot(mem, 0x00100000u + 4 * 128) == 0xFFFF);
    CHECK(cp_slot(mem, 0x00100000u - 200) == 0x07);       /* (-200)/128 = -1 toward zero */
    CHECK(cp_slot(mem, 0x00100000u - 100) == 0x00);       /* (-100)/128 = 0 */

    /* Dirty-mask bit: slot 0x25 = bit 5 of the word at +4. */
    w32(CP_MASK_PEND + 4, 0x20u);
    CHECK(cp_slot_bit(mem, CP_MASK_PEND, 0x25) == 1);
    CHECK(cp_slot_bit(mem, CP_MASK_PEND, 0x24) == 0);
    CHECK(cp_slot_bit(mem, CP_MASK_PEND, 0xFFFF) == 0);
    CHECK(cp_popcount128(mem, CP_MASK_PEND) == 1);

    /* Finaliser: a no-candidate query that left 0x4, then a hit with candidates; summary after 1 s. */
    cp_init(&s, "1", NULL);
    FILE* f = out_open();
    w32(CP_REC + 0x08, 0x12340000u);                     /* u16 +0x0A = 0 candidates */
    w32(CP_REC + 0x0C, 0x01040002u);
    w32(CP_BPFLAGS, 0x4u);
    w32(0x00300000u + 0x50, 0);
    cp_finish_pre(&s, mem, CP_GROUND_LR);
    cp_finish_post(&s, mem, f, 10.0, 0x00300000u);
    w32(CP_REC + 0x08, 0x00000003u);
    w32(CP_BPFLAGS, 0x0u);
    w32(0x00310000u + 0x50, 0x00450000u);
    cp_finish_pre(&s, mem, CP_GROUND_LR);
    cp_finish_post(&s, mem, f, 10.4, 0x00310000u);
    cp_finish_pre(&s, mem, 0x0076628Cu);
    cp_finish_post(&s, mem, f, 11.2, 0);
    out_close(f);
    CHECK(strstr(buf, "[collq] t=11.2 lr=0x00766188 n=2 cand0=1 bit4=1 hit=1\n") != NULL);
    CHECK(strstr(buf, "[collq] t=11.2 lr=0x0076628C n=1 cand0=0 bit4=0 hit=0\n") != NULL);
    CHECK(s.rows[0].n == 0 && s.ring_n == 3);
    CHECK(strstr(buf, "[collprobe") == NULL);            /* mode 1: no details, no world dump */

    /* Ground probe details: gated by RC_COLLPROBE_AFTER, ring first, then one line per probe until the count runs out. */
    cp_init(&s, "2", "100");
    uint32_t desc = 0x00400000u, obj = 0x00410000u, pt = 0x00420000u;
    w32(desc + 0x00, 0x00310000u);
    w32(desc + 0x10, obj);
    w32(obj + 0x80, 0x00100000u + 3 * 128);
    w32(obj + 0x88, 0);
    wf(obj + 0x10, 1.5f); wf(pt, -2.0f);
    /* Query geometry as func_005913FC stores it: radius, p0 (w 1.0), p1 (w 1.0). */
    wf(CP_REC + 0x3C, 0.25f);
    wf(CP_REC + 0x40, 10.0f); wf(CP_REC + 0x44, 102.5f); wf(CP_REC + 0x48, -3.0f); wf(CP_REC + 0x4C, 1.0f);
    wf(CP_REC + 0x50, 10.5f); wf(CP_REC + 0x54, 102.5f); wf(CP_REC + 0x58, -3.0f); wf(CP_REC + 0x5C, 1.0f);
    w32(CP_WORLD + 0x10C, 0xCAFEF00Du);
    w32(CP_BLOCK + 0x6C, 0x0B10C0DEu);
    f = out_open();
    cp_finish_pre(&s, mem, 0x00765F8Cu);
    cp_finish_post(&s, mem, f, 99.0, 0);
    cp_ground_pre(&s, mem, pt, 0.25, desc, 0x00028010u);
    cp_finish_pre(&s, mem, CP_GROUND_LR);
    cp_finish_post(&s, mem, f, 99.5, 0x00310000u);
    cp_ground_post(&s, mem, f, 99.5, 1);                  /* before RC_COLLPROBE_AFTER */
    { uint32_t nan = 0x7FC00000u; w32(CP_REC + 0x50, nan); }   /* this query's p1.x is NaN */
    cp_ground_pre(&s, mem, pt, 0.25, desc, 0x00028010u);
    cp_finish_pre(&s, mem, CP_GROUND_LR);
    cp_finish_post(&s, mem, f, 100.5, 0x00310000u);
    cp_ground_post(&s, mem, f, 100.5, 1);
    cp_ground_post(&s, mem, f, 100.6, 1);
    cp_ground_post(&s, mem, f, 100.7, 1);                 /* count spent: nothing */
    out_close(f);
    CHECK(strstr(buf, "[collprobe] t=99.500") == NULL);
    CHECK(strstr(buf, "[collprobe-pre] t=99.000 lr=0x00765F8C cand=3 type=0x0104 e=0x0002 bp=0x0 buf=0x00000000 b50=0x00000000"
                      " r=0.25 p0=(10 102.5 -3 1) p1=(10.5 102.5 -3 1) raw=3E800000,41200000,42CD0000,C0400000,3F800000,"
                      "41280000,42CD0000,C0400000,3F800000\n") != NULL);
    CHECK(strstr(buf, "p1=(nan 102.5 -3 1) raw=3E800000,41200000,42CD0000,C0400000,3F800000,7FC00000,42CD0000,C0400000,"
                      "3F800000 nonfinite\n") != NULL);
    /* World dumped once at the first query after RC_COLLPROBE_AFTER (100.5) and once at the first ground probe. */
    CHECK(strstr(buf, "[collprobe-world] t=100.500 at=first-query") != NULL);
    CHECK(strstr(buf, "[collprobe-world] t=100.500 at=first-ground") != NULL);
    CHECK(strstr(buf, "[collprobe-world]   00F55980: 00000000 00000000 00000000 CAFEF00D\n") != NULL);   /* 0x110: last row 4 words */
    CHECK(strstr(buf, "[collprobe-world]   00F3FF70: 00000000 00000000 00000000 0B10C0DE\n") != NULL);
    { int w = 0; for (const char* q = buf; (q = strstr(q, " at=")) != NULL; q++) w++; CHECK(w == 2); }
    { int w = 0; for (const char* q = buf; (q = strstr(q, "[collprobe-world]   ")) != NULL; q++) w++; CHECK(w == 2 * (9 + 4)); }
    CHECK(strstr(buf, "[collprobe-world] t=100.500 at=first-ground") < strstr(buf, "[collprobe-pre] t=99.000"));
    CHECK(strstr(buf, "[collprobe-pre] t=99.000 lr=0x00765F8C cand=3") != NULL);
    CHECK(strstr(buf, "[collprobe-pre] t=100.500 lr=0x00766188") != NULL);
    CHECK(strstr(buf, "lr=0x00028010 fn=func_00027B88 hit=1 cand=3 type=0x0104 e=0x0002") != NULL);
    CHECK(strstr(buf, "b50=0x00450000 f1=0.25 desc=0x00400000 obj=0x00410000 slot80=37 slot88=-1 masks=1/0/0 in80=100 in88=000") != NULL);
    CHECK(strstr(buf, "pt=(-2 0 0 0 | 0 0 0 0) obj+10=(1.5 0 0)") != NULL);
    CHECK(strstr(buf, "rec: 00000000 00000000 00000003 01040002") != NULL);
    CHECK(strstr(buf, "buf+40: 00000000 00000000 00000000 00000000 00450000") != NULL);
    { int lines = 0; for (const char* q = buf; (q = strstr(q, "[collprobe] t=")) != NULL; q++) lines++; CHECK(lines == 2); }
    CHECK(s.detail_left == 0);

    /* The ring is dumped again at a new airborne phase (1 s without a ground probe), not between close probes. */
    cp_init(&s, "5", NULL);
    f = out_open();
    for (int k = 0; k < 3; k++) {
        static const double ts[3] = { 200.0, 200.1, 202.0 };
        cp_ground_pre(&s, mem, pt, 0.25, desc, 0x00028010u);
        cp_finish_pre(&s, mem, CP_GROUND_LR);
        cp_finish_post(&s, mem, f, ts[k], 0x00310000u);
        cp_ground_post(&s, mem, f, ts[k], 0);
    }
    out_close(f);
    CHECK(s.ring_dumps == 2);
    { int pre = 0; for (const char* q = buf; (q = strstr(q, "[collprobe-pre]")) != NULL; q++) pre++; CHECK(pre == 1 + 3); }
    CHECK(strstr(buf, "fn=func_00027B88") != NULL);
    { int w = 0; for (const char* q = buf; (q = strstr(q, " at=")) != NULL; q++) w++; CHECK(w == 2); }   /* after 0 */

    /* Ring of 128: 200 queries plus the ground probe's own; the dump holds the last 128 (t = 300.073 .. 300.200), oldest first. */
    cp_init(&s, "2", NULL);
    f = out_open();
    for (int k = 0; k < 200; k++) { cp_finish_pre(&s, mem, 0x0076628Cu); cp_finish_post(&s, mem, f, 300.0 + k * 0.001, 0); }
    cp_ground_pre(&s, mem, pt, 0.25, desc, 0x0002C730u);
    cp_finish_pre(&s, mem, CP_GROUND_LR);
    cp_finish_post(&s, mem, f, 300.2, 0x00310000u);
    cp_ground_post(&s, mem, f, 300.2, 0);
    out_close(f);
    { int pre = 0; for (const char* q = buf; (q = strstr(q, "[collprobe-pre]")) != NULL; q++) pre++; CHECK(pre == 128); }
    CHECK(strstr(buf, "[collprobe-pre] t=300.073 ") != NULL && strstr(buf, "[collprobe-pre] t=300.072 ") == NULL);
    CHECK(strstr(buf, "[collprobe-pre] t=300.073 ") < strstr(buf, "[collprobe-pre] t=300.200 "));
    CHECK(strstr(buf, "lr=0x0002C730 fn=func_0002C608 hit=0") != NULL);
    CHECK(strstr(buf, "at=first-query") < strstr(buf, "at=first-ground"));

    /* Negative control: RC_COLLPROBE unset leaves the probe off (the overrides then only call the lifted bodies). */
    cp_init(&s, NULL, NULL);
    CHECK(s.on == 0 && s.ring_n == 0 && !s.world_q && !s.world_g);

    free(mem);
    printf(fails ? "[FAIL] %d check(s)\n" : "[PASS] all\n", fails);
    return fails ? 1 : 0;
}
