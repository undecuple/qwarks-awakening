/* Into the Nexus 1.01: collision-query probe (DIAGNOSTIC, default off; 2026-11-19, prison_transport fall,
 * work/coord/nexus-fall-1119/static-1119.md). No behaviour change: the overrides in nexus101_ppu_overrides.cpp call the
 * lifted bodies unchanged and only read guest memory around them. Plain C so the host test can include it.
 *
 *   RC_COLLPROBE=1            one [collq] summary line per second per caller of the sync-query finaliser func_00591B88
 *                             (n queries, cand0 = broadphase found no candidate, bit4 = 0x4 still set in [0xF516B0]
 *                             after the finaliser, hit = [buf+0x50] != 0).
 *   RC_COLLPROBE=N (N >= 2)   the summaries plus N [collprobe] detail lines, one per player ground probe func_007660E0
 *                             (r3 point, f1, desc r5; result buffer, hit word, record 0xF51700, object slots, masks),
 *                             starting at the first ground probe after RC_COLLPROBE_AFTER seconds (default 0). The first
 *                             detail line, and the first after a 1 s gap in ground probes (a new airborne phase, at most
 *                             8 times), is preceded by the last 128 finalised queries of any caller ([collprobe-pre],
 *                             with the query geometry rec +0x3C..+0x5F: radius, p0, p1 as %g and raw hex; "nonfinite"
 *                             flags a NaN/inf field). The collision world (0xF55880..+0x110, 0xF3FF10..+0x70) is dumped
 *                             as hex words ([collprobe-world]) once at the first finalised query (any caller) and once at
 *                             the first ground probe, both after RC_COLLPROBE_AFTER: a before/after pair.
 *   The [collprobe] lr is the guest return address of the ground probe's caller (the lifted call sets ctx->lr to it before
 *   the call): 0x00028010 = func_00027B88 (bl @0x0002800C), 0x0002C730 = func_0002C608 (bl @0x0002C72C), the only two
 *   static call sites in work/recomp/nexus101; printed with fn=.
 *
 * Facts used (lifted PPU C work/recomp/nexus101, broadphase SPU job 0x00C21400 disassembled at LS 0x100):
 *   - func_005913FC fills the record: u16 +0x0C = type, u16 +0x0A = u16 +0x0E = 0, float +0x3C = max(0.001, radius) (only
 *     when the radius pointer is non-null), +0x40..+0x48 = p0 xyz with +0x4C = 1.0, +0x50..+0x58 = p1 xyz with
 *     +0x5C = 1.0 (each point only when its pointer is non-null, so a null pointer leaves the previous query's value).
 *     The type field read at the finaliser's entry has 0x100 once the bp has PUT +0x00..+0x0F; without it cand is the
 *     PPU's 0, not a broadphase result.
 *   - record 0xF51700 (0xA0 bytes), filled by func_005913FC; the broadphase GETs it (bp 0xD68), runs the per-type routine
 *     (table LS 0xBD80 indexed by u16 +0x0C), counts the items it hands to the narrow phase in LS[0xC780], and PUTs
 *     +0x00..+0x0F back with u16 +0x0A = that count, u16 +0x0C |= 0x100 (done), u16 +0x0E (bp 0x10D8..0x1124).
 *     Count 0: it also PUTs +0x10..+0x1F with byte +0x15 |= 0x80 and drops the narrow claim (bp 0x1308..0x13CC).
 *     Then it clears 0x4 (sync) or 0x44 (async) in [0xF516B0] (bp 0x1130..0x11D8).
 *   - func_00591A98 waits for +0x0C & 0x100; func_00591B88 waits for byte +0x15 & 0x80 unless [0xF55808] != 0, clears
 *     0x180 in [0xF516B0] and returns [0xF51718] (the caller's result buffer); func_007660E0 hit = buf && [buf+0x50].
 *   - func_005974CC(ea): slot = u16 [[0xF5591C] + ((((int32_t)(ea - [0xF55888])) / 128) << 1 & 0x1FFFE)]; slot != 0xFFFF
 *     sets bit (slot & 31) of the word at 0xF40200 + ((slot >> 3) & 0x3FFFC).
 * ASSUMPTION (not read off the code): u16 +0x0A is the number of broadphase candidates (it is the word the bp zeroes
 * before the type routine and tests for zero to take the "nothing to do" path); obj+0x10 is the object position. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CP_REC        0x00F51700u   /* sync query record, 0xA0 bytes */
#define CP_REC_BUF    0x00F51718u   /* result buffer pointer the PPU stores at submit */
#define CP_BPFLAGS    0x00F516B0u   /* line 0x00F51680 + 0x30: broadphase command flags */
#define CP_WORLD      0x00F55880u   /* collision world descriptor */
#define CP_MASK_PEND  0x00F40200u   /* dirty mask set by func_005974CC */
#define CP_MASK_ACC   0x00F40280u
#define CP_MASK_SPU   0x00F3FE00u   /* merged mask the job reads */
#define CP_FRAME      0x00D994A8u   /* collision frame counter (func_00593A5C) */
#define CP_GROUND_LR  0x00766188u   /* func_007660E0's call of func_00591B88 */
#define CP_WORLD_LEN  0x110u
#define CP_BLOCK      0x00F3FF10u   /* world block next to the merged mask (static-1119 hyp 3) */
#define CP_BLOCK_LEN  0x70u
#define CP_RING       128u          /* power of two */
#define CP_GEOM_N     9u            /* rec +0x3C..+0x5F */

static inline uint32_t cp_be32(const uint8_t* m, uint32_t ea)
{ return (uint32_t)m[ea] << 24 | (uint32_t)m[ea + 1] << 16 | (uint32_t)m[ea + 2] << 8 | m[ea + 3]; }
static inline uint32_t cp_be16(const uint8_t* m, uint32_t ea) { return (uint32_t)m[ea] << 8 | m[ea + 1]; }
static inline float cp_f32(const uint8_t* m, uint32_t ea) { uint32_t v = cp_be32(m, ea); float f; memcpy(&f, &v, 4); return f; }

/* func_005974CC's slot for an object pointer: -1 for a null pointer, 0xFFFF = no slot. */
static inline int cp_slot(const uint8_t* m, uint32_t ea)
{
    if (!ea) return -1;
    uint32_t base = cp_be32(m, CP_WORLD + 0x08), table = cp_be32(m, CP_WORLD + 0x9C);
    if (!base || !table) return -2;                              /* world not set up (func_0059A738) */
    int32_t idx = (int32_t)(ea - base) / 128;                    /* srawi 7 + addze: toward zero */
    return (int)cp_be16(m, table + (((uint32_t)idx << 1) & 0x1FFFEu));
}
static inline int cp_slot_bit(const uint8_t* m, uint32_t mask, int slot)
{
    if (slot < 0 || slot == 0xFFFF) return 0;
    return (cp_be32(m, mask + (((uint32_t)slot >> 3) & 0x3FFFCu)) >> (slot & 31)) & 1u;
}
static inline unsigned cp_popcount128(const uint8_t* m, uint32_t ea)
{
    unsigned n = 0;
    for (uint32_t i = 0; i < 128; i++) for (uint8_t b = m[ea + i]; b; b &= (uint8_t)(b - 1)) n++;
    return n;
}

typedef struct { uint32_t lr, n, cand0, bit4, hit; } cp_row;
typedef struct { double t; uint32_t lr, w08, w0C, bp, buf, b50, geom[CP_GEOM_N]; } cp_hist;
typedef struct {
    int on;                    /* 0 off, 1 summaries, 2 summaries + details */
    long detail_left;
    double after_s, last_sum_s;
    cp_row rows[16]; int nrows;
    cp_hist ring[CP_RING]; unsigned ring_n; int ring_dumps; double last_ground_t;
    int world_q, world_g;                                /* world dumped at the first query / first ground probe */
    uint32_t pre_w08, pre_w0C, pre_b15, pre_lr, pre_geom[CP_GEOM_N];   /* record at the finaliser's entry */
    cp_hist last;                                        /* the last finalised query */
    uint32_t g_pt, g_desc, g_lr; double g_f1;            /* ground probe arguments */
    int g_s80, g_s88; uint32_t g_obj, g_pc[3], g_in[2][3];
} cp_state;

static inline void cp_init(cp_state* s, const char* on, const char* after)
{
    memset(s, 0, sizeof *s);
    long v = on ? strtol(on, 0, 0) : 0;
    if (v <= 0) return;
    s->on = v >= 2 ? 2 : 1;
    s->detail_left = v >= 2 ? v : 0;
    s->after_s = after ? strtod(after, 0) : 0.0;
    s->last_sum_s = -1.0;
}

static inline void cp_finish_pre(cp_state* s, const uint8_t* m, uint32_t lr)
{
    s->pre_lr = lr;
    s->pre_w08 = cp_be32(m, CP_REC + 0x08);
    s->pre_w0C = cp_be32(m, CP_REC + 0x0C);
    s->pre_b15 = m[CP_REC + 0x15];
    for (uint32_t i = 0; i < CP_GEOM_N; i++) s->pre_geom[i] = cp_be32(m, CP_REC + 0x3C + 4 * i);
}

/* One-time hex dump of the collision world descriptor and the block at 0xF3FF10. */
static inline void cp_world_dump(const uint8_t* m, FILE* out, double t, const char* why)
{
    fprintf(out, "[collprobe-world] t=%.3f at=%s fc=0x%08X\n", t, why, cp_be32(m, CP_FRAME));
    const uint32_t base[2] = { CP_WORLD, CP_BLOCK }, len[2] = { CP_WORLD_LEN, CP_BLOCK_LEN };
    for (int k = 0; k < 2; k++)
        for (uint32_t o = 0; o < len[k]; o += 0x20) {
            fprintf(out, "[collprobe-world]   %08X:", base[k] + o);
            for (uint32_t i = o; i < o + 0x20 && i < len[k]; i += 4) fprintf(out, " %08X", cp_be32(m, base[k] + i));
            fprintf(out, "\n");
        }
}

/* Guest caller of func_007660E0 from its return address (the two static call sites). */
static inline const char* cp_ground_fn(uint32_t lr)
{
    return lr == 0x00028010u ? "func_00027B88" : lr == 0x0002C730u ? "func_0002C608" : "?";
}

static inline void cp_summary(cp_state* s, FILE* out, double t)
{
    if (s->last_sum_s < 0) { s->last_sum_s = t; return; }
    if (t - s->last_sum_s < 1.0) return;
    for (int i = 0; i < s->nrows; i++) {
        cp_row* r = &s->rows[i];
        if (!r->n) continue;
        fprintf(out, "[collq] t=%.1f lr=0x%08X n=%u cand0=%u bit4=%u hit=%u\n", t, r->lr, r->n, r->cand0, r->bit4, r->hit);
        r->n = r->cand0 = r->bit4 = r->hit = 0;
    }
    s->last_sum_s = t;
}

/* After func_00591B88: ret = its r3 (the result buffer or 0). */
static inline void cp_finish_post(cp_state* s, const uint8_t* m, FILE* out, double t, uint32_t ret)
{
    cp_hist h;
    h.t = t; h.lr = s->pre_lr; h.w08 = s->pre_w08; h.w0C = s->pre_w0C;
    h.bp = cp_be32(m, CP_BPFLAGS); h.buf = ret; h.b50 = ret ? cp_be32(m, ret + 0x50) : 0;
    memcpy(h.geom, s->pre_geom, sizeof h.geom);
    s->last = h;
    s->ring[s->ring_n++ & (CP_RING - 1u)] = h;
    if (s->on >= 2 && !s->world_q && t >= s->after_s) { s->world_q = 1; cp_world_dump(m, out, t, "first-query"); }
    int i = 0;
    while (i < s->nrows && s->rows[i].lr != h.lr) i++;
    if (i == s->nrows && s->nrows < 16) { s->rows[i].lr = h.lr; s->nrows++; }
    if (i < 16) {
        cp_row* r = &s->rows[i];
        r->n++;
        if ((h.w08 & 0xFFFFu) == 0) r->cand0++;          /* u16 +0x0A */
        if (h.bp & 0x4u) r->bit4++;
        if (h.b50) r->hit++;
    }
    cp_summary(s, out, t);
}

static inline void cp_ground_pre(cp_state* s, const uint8_t* m, uint32_t pt, double f1, uint32_t desc, uint32_t lr)
{
    s->g_pt = pt; s->g_f1 = f1; s->g_desc = desc; s->g_lr = lr;
    s->g_obj = desc ? cp_be32(m, desc + 0x10) : 0;
    s->g_s80 = s->g_obj ? cp_slot(m, cp_be32(m, s->g_obj + 0x80)) : -1;
    s->g_s88 = s->g_obj ? cp_slot(m, cp_be32(m, s->g_obj + 0x88)) : -1;
    s->g_pc[0] = cp_popcount128(m, CP_MASK_PEND);
    s->g_pc[1] = cp_popcount128(m, CP_MASK_ACC);
    s->g_pc[2] = cp_popcount128(m, CP_MASK_SPU);
    const uint32_t masks[3] = { CP_MASK_PEND, CP_MASK_ACC, CP_MASK_SPU };
    for (int k = 0; k < 3; k++) { s->g_in[0][k] = cp_slot_bit(m, masks[k], s->g_s80); s->g_in[1][k] = cp_slot_bit(m, masks[k], s->g_s88); }
}

static inline void cp_hist_line(FILE* out, const char* tag, const cp_hist* h)
{
    float g[CP_GEOM_N];
    int bad = 0;
    for (uint32_t i = 0; i < CP_GEOM_N; i++) { memcpy(&g[i], &h->geom[i], 4); if (!isfinite(g[i])) bad = 1; }
    fprintf(out, "[%s] t=%.3f lr=0x%08X cand=%u type=0x%04X e=0x%04X bp=0x%X buf=0x%08X b50=0x%08X r=%g p0=(%g %g %g %g)"
                 " p1=(%g %g %g %g) raw=", tag, h->t, h->lr, h->w08 & 0xFFFFu, h->w0C >> 16, h->w0C & 0xFFFFu, h->bp, h->buf,
            h->b50, (double)g[0], (double)g[1], (double)g[2], (double)g[3], (double)g[4], (double)g[5], (double)g[6],
            (double)g[7], (double)g[8]);
    for (uint32_t i = 0; i < CP_GEOM_N; i++) fprintf(out, i ? ",%08X" : "%08X", h->geom[i]);
    fprintf(out, bad ? " nonfinite\n" : "\n");
}

/* After func_007660E0: hit = its r3. */
static inline void cp_ground_post(cp_state* s, const uint8_t* m, FILE* out, double t, uint32_t hit)
{
    if (s->on < 2 || s->detail_left <= 0 || t < s->after_s) return;
    if (!s->world_g) { s->world_g = 1; cp_world_dump(m, out, t, "first-ground"); }
    /* A new airborne phase (no ground probe for 1 s): first the last 128 finalised queries of any caller (at most 8 times). */
    if ((s->ring_dumps == 0 || t - s->last_ground_t > 1.0) && s->ring_dumps < 8) {
        s->ring_dumps++;
        unsigned n = s->ring_n < CP_RING ? s->ring_n : CP_RING;
        for (unsigned k = s->ring_n - n; k < s->ring_n; k++) cp_hist_line(out, "collprobe-pre", &s->ring[k & (CP_RING - 1u)]);
    }
    s->last_ground_t = t;
    s->detail_left--;
    const cp_hist* h = &s->last;
    const uint32_t p = s->g_pt, o = s->g_obj;
    fprintf(out, "[collprobe] t=%.3f fc=0x%08X lr=0x%08X fn=%s hit=%u cand=%u type=0x%04X e=0x%04X b15=0x%02X bp=0x%X buf=0x%08X"
                 " b50=0x%08X f1=%g desc=0x%08X obj=0x%08X slot80=%d slot88=%d masks=%u/%u/%u in80=%u%u%u in88=%u%u%u\n",
            t, cp_be32(m, CP_FRAME), s->g_lr, cp_ground_fn(s->g_lr), hit, h->w08 & 0xFFFFu, h->w0C >> 16, h->w0C & 0xFFFFu,
            m[CP_REC + 0x15], h->bp,
            h->buf, h->b50, s->g_f1, s->g_desc, o, s->g_s80, s->g_s88, s->g_pc[0], s->g_pc[1], s->g_pc[2],
            s->g_in[0][0], s->g_in[0][1], s->g_in[0][2], s->g_in[1][0], s->g_in[1][1], s->g_in[1][2]);
    if (p) fprintf(out, "[collprobe]   pt=(%g %g %g %g | %g %g %g %g)", cp_f32(m, p), cp_f32(m, p + 4), cp_f32(m, p + 8),
                   cp_f32(m, p + 12), cp_f32(m, p + 16), cp_f32(m, p + 20), cp_f32(m, p + 24), cp_f32(m, p + 28));
    else fprintf(out, "[collprobe]   pt=null");
    if (o) fprintf(out, " obj+10=(%g %g %g)", cp_f32(m, o + 0x10), cp_f32(m, o + 0x14), cp_f32(m, o + 0x18));
    fprintf(out, "\n[collprobe]   rec:");
    for (uint32_t i = 0; i < 0xA0; i += 4) fprintf(out, " %08X", cp_be32(m, CP_REC + i));
    if (h->buf) {
        fprintf(out, "\n[collprobe]   buf+40:");
        for (uint32_t i = 0x40; i < 0x60; i += 4) fprintf(out, " %08X", cp_be32(m, h->buf + i));
    }
    fprintf(out, "\n");
}
