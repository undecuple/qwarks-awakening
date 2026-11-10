/* display.ini of an R&C game (pure helpers; the Windows glue is port/rc/main.cpp rc_display_load / rc_display_save).
 *
 * File: <data folder>/<game id>/display.ini (rc_settings.h), section [display]. Keys:
 *   mode, scaling, filter, vsync          KZ3 2095/2103 (rsx_display_mode.h parsers); written by the port
 *   maximize, window_x, window_y, window_width, window_height, window_maximized, window_monitor, window_monitor_rect
 *                                         KZ3 2126 (rsx_window_place.h); written by the port
 *   size = WxH | 720p | 900p | 1080p | 1440p | 2160p | 4k
 *                                         R&C: the client size of the first start (no window_* keys yet); read only
 *   hud_169 = on | off                    KZ3 2113 (the 2D layer squeezed back to 16:9); read only, R&C default off
 *   upscale = none | fsr1                 KZ3 2145's upscaler (RSX_UPSCALE) as a setting; read only, default none
 *   upscale_sharpness = 0..2              its RCAS sharpness in stops (RSX_UPSCALE_SHARPNESS; 0 sharpest, default 0.2)
 *                                         R&C 2026-11-09 (window stage 1d): KZ3 2145 reads only the environment, so the
 *                                         port sets RSX_UPSCALE / RSX_UPSCALE_SHARPNESS from these keys before the
 *                                         renderer starts; a variable already set wins for one run (rc_dini_upscale_env)
 * Saving is read-modify-write (rc_dini_merge): the keys the port owns are replaced in place, missing ones are added
 * at the end of the [display] section with their comment, and every other line (comments, other keys, other sections)
 * is kept byte for byte. A file without a [display] section keeps its text and gets one appended.
 *
 * Shared file (launcher "All games", 2026-11-09): <data folder>/shared/display.ini (rc_settings.h) is read first,
 * then this game's file; a key in the game's file wins. Shared keys: mode, size, scaling, filter, vsync, upscale,
 * upscale_sharpness (RC_DINI_SHARED_KEYS); window_*, maximize and hud_169 are read from the game's file only. A save writes only the
 * game's file, and leaves a shared key out of it while the game's file has no line for it and the value is still the
 * shared one (rc_dini_drop_inherited): a change made in the game is kept for that game, an unchanged value keeps
 * following the shared file.
 *
 * R&C display defaults (ENHANCEMENT, display only, changeable in display.ini):
 *   - scaling = fit: the 1280x720 picture keeps its aspect, black bars when the window is not 16:9 (the runtime's
 *     default is stretch, which assumes KZ3's view-aspect camera hook; R&C has none, so stretch distorts);
 *   - hud_169 = off: KZ3's 2D squeeze (tuned on KZ3's HUD draws) is not applied; ToD's HUD stays with its picture.
 * Host test: port/rc/tests/test_rc_display.c (run_rc_display_test.sh). */
#ifndef RC_DISPLAY_INI_H
#define RC_DISPLAY_INI_H

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "rsx_display_mode.h"   /* rsx_dm_* names, parsers, rsx_dm_ieq */
#include "rsx_window_place.h"   /* rsx_wp, rsx_wp_ini_read, rsx_dm_ini_maximize */
#include "rsx_window_size.h"    /* rsx_win_parse_size (KZ3_WINDOW_SIZE's parser) */

#define RC_DINI_SCALING_DEFAULT RSX_DM_FIT   /* ENHANCEMENT (R&C display default; runtime default is stretch) */
#define RC_DINI_HUD169_DEFAULT  0            /* ENHANCEMENT (R&C display default; runtime default is on) */

/* One key the port owns: its value now and the comment written above it when it has to be added. */
typedef struct {
    const char* key;
    char val[64];
    const char* comment;   /* "# ...\n" lines or NULL */
} rc_dini_kv;

/* The header of a new file. */
#define RC_DINI_HEAD                                                                                                  \
    "# Display settings of this game (Alt+Enter or F11 toggles windowed / fullscreen; the maximize button goes\n"    \
    "# borderless). Lines you add here are kept when the game saves.\n"

/* Is the trimmed line p[0..n) "key = ..." (key without case, blanks before '=')? Same rule as rsx_dm_ini_value. */
static inline int rc_dini_is_key(const char* p, size_t n, const char* key)
{
    const size_t kl = strlen(key);
    size_t i = kl;
    if (n <= kl || !rsx_dm_ieq(p, kl, key)) return 0;
    while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
    return i < n && p[i] == '=';
}

/* Fill kv[] (12 entries) with the owned keys. wp may be invalid: then the window_* keys are not owned this time (an
 * existing file keeps whatever it has). Returns the number of entries used. */
static inline int rc_dini_owned(rc_dini_kv* kv, int mode, int scaling, int filter, int vsync, int maximize_borderless,
                                const rsx_wp* wp)
{
    int n = 0;
#define RC_DINI_KV(k, c, ...) do { kv[n].key = (k); kv[n].comment = (c); snprintf(kv[n].val, sizeof kv[n].val, __VA_ARGS__); n++; } while (0)
    RC_DINI_KV("mode", "# mode = windowed | borderless | fullscreen (RC_DISPLAY / KZ3_DISPLAY win for one run)\n", "%s", rsx_dm_name(mode));
    RC_DINI_KV("scaling", "# scaling = fit (picture keeps its shape, black bars) | stretch (fills the window)\n", "%s",
               rsx_dm_scaling_name(scaling));
    RC_DINI_KV("filter", "# filter = bilinear | nearest (nearest with fit: whole-number scale)\n", "%s", rsx_dm_filter_name(filter));
    RC_DINI_KV("vsync", "# vsync = on | off\n", "%s", rsx_dm_vsync_name(vsync));
    RC_DINI_KV("maximize", "# maximize = borderless | window: what the maximize button does\n", "%s",
               maximize_borderless ? "borderless" : "window");
    if (wp && wp->valid) {
        RC_DINI_KV("window_x",
                   "# window_* = the window as it was last left (client size, outer top-left, monitor and its rectangle).\n"
                   "# A missing or changed monitor puts it on the primary one, centred; it is always kept on screen.\n"
                   "# size = WxH (or 720p, 1080p, ...) is the size of the first start, before these exist.\n",
                   "%d", wp->x);
        RC_DINI_KV("window_y", NULL, "%d", wp->y);
        RC_DINI_KV("window_width", NULL, "%d", wp->w);
        RC_DINI_KV("window_height", NULL, "%d", wp->h);
        RC_DINI_KV("window_maximized", NULL, "%d", wp->maximized ? 1 : 0);
        RC_DINI_KV("window_monitor", NULL, "%s", wp->mon);
        RC_DINI_KV("window_monitor_rect", NULL, "%d,%d,%d,%d", wp->ml, wp->mt, wp->mr, wp->mb);
    }
#undef RC_DINI_KV
    return n;
}

/* old (may be NULL or empty) + the owned keys -> out. Returns the length, or -1 when out is too small. */
static inline int rc_dini_merge(const char* old, const rc_dini_kv* kv, int nkv, char* out, size_t cap)
{
    size_t o = 0;
    int done[32] = { 0 }, i, in_sec = 0, sec_seen = 0, added = 0;
    const char* t = old;
#define RC_DINI_PUT(s, len) do { size_t l_ = (len); if (o + l_ >= cap) return -1; memcpy(out + o, (s), l_); o += l_; } while (0)
#define RC_DINI_ADD_MISSING()                                                                                          \
    do {                                                                                                             \
        for (i = 0; i < nkv; i++) {                                                                                  \
            if (done[i]) continue;                                                                                   \
            if (kv[i].comment) RC_DINI_PUT(kv[i].comment, strlen(kv[i].comment));                                    \
            RC_DINI_PUT(kv[i].key, strlen(kv[i].key));                                                               \
            RC_DINI_PUT(" = ", 3);                                                                                   \
            RC_DINI_PUT(kv[i].val, strlen(kv[i].val));                                                               \
            RC_DINI_PUT("\n", 1);                                                                                    \
            done[i] = 1;                                                                                             \
        }                                                                                                            \
        added = 1;                                                                                                   \
    } while (0)
    if (nkv > 32) return -1;
    if (!old || !*old) RC_DINI_PUT(RC_DINI_HEAD, strlen(RC_DINI_HEAD));
    while (t && *t) {
        const char* e = strchr(t, '\n');
        const size_t full = e ? (size_t)(e - t) + 1 : strlen(t);
        const char* p = t;
        size_t n = e ? (size_t)(e - t) : strlen(t);
        int copied = 0;
        while (n && (*p == ' ' || *p == '\t')) { p++; n--; }
        while (n && (p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
        if (n && p[0] == '[') {
            if (in_sec && !added) RC_DINI_ADD_MISSING();   /* end of the first [display] section */
            in_sec = rsx_dm_ieq(p, n, "[display]");
            if (in_sec) sec_seen = 1;
            RC_DINI_PUT(t, full);
            copied = 1;
        } else if (in_sec) {
            int k = -1;
            for (i = 0; i < nkv && k < 0; i++) if (rc_dini_is_key(p, n, kv[i].key)) k = i;
            if (k < 0) { RC_DINI_PUT(t, full); copied = 1; }
            else if (!done[k]) {                            /* the first one is the one the readers see */
                RC_DINI_PUT(t, (size_t)(p - t));            /* its indentation */
                RC_DINI_PUT(kv[k].key, strlen(kv[k].key));
                RC_DINI_PUT(" = ", 3);
                RC_DINI_PUT(kv[k].val, strlen(kv[k].val));
                RC_DINI_PUT(e && e > t && e[-1] == '\r' ? "\r\n" : "\n", e && e > t && e[-1] == '\r' ? 2u : 1u);
                done[k] = 1;
            }                                               /* later duplicates of an owned key: dropped */
        } else { RC_DINI_PUT(t, full); copied = 1; }
        if (!e && copied) RC_DINI_PUT("\n", 1);   /* a kept last line without a newline */
        t = e ? e + 1 : NULL;
    }
    if (!sec_seen) RC_DINI_PUT("[display]\n", 10);
    if (!added) RC_DINI_ADD_MISSING();
    out[o] = 0;
#undef RC_DINI_ADD_MISSING
#undef RC_DINI_PUT
    return (int)o;
}

/* What the port reads at start. -1 = the key is missing (the caller applies the R&C default). */
typedef struct {
    int mode, scaling, filter, vsync;
    int maximize;   /* 1 borderless, 0 window, -1 missing */
    int hud169;     /* 1 on, 0 off, -1 missing */
    int upscale;    /* RC_DINI_UPSCALE_NONE / _FSR1, -1 missing or not a known value */
    float sharpness;/* upscale_sharpness, < 0 missing (clamped to 0..2 by the runtime's parser) */
    rsx_wp wp;      /* window_*; or, with none saved, size = WxH as a first-start client size (wp.mon empty) */
    int from_size;  /* wp came from size = */
} rc_dini_read_t;

/* upscale = none | off | fsr1 | fsr (the spellings rsx_upscale_parse takes for the two backends 2145 implements). */
enum { RC_DINI_UPSCALE_NONE = 0, RC_DINI_UPSCALE_FSR1 = 1 };
static inline int rc_dini_upscale(const char* t)
{
    char v[32];
    const char* p = v;
    size_t n;
    if (!rsx_dm_ini_value(t, "upscale", v, sizeof v)) return -1;
    while (*p == ' ' || *p == '\t') p++;
    n = strlen(p);
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r')) n--;
    if (rsx_dm_ieq(p, n, "none") || rsx_dm_ieq(p, n, "off") || rsx_dm_ieq(p, n, "0")) return RC_DINI_UPSCALE_NONE;
    if (rsx_dm_ieq(p, n, "fsr1") || rsx_dm_ieq(p, n, "fsr")) return RC_DINI_UPSCALE_FSR1;
    return -1;
}
static inline float rc_dini_sharpness(const char* t)
{
    char v[32], *end = NULL;
    double d;
    if (!rsx_dm_ini_value(t, "upscale_sharpness", v, sizeof v)) return -1.0f;
    d = strtod(v, &end);
    if (end == v || !(d == d) || d < 0.0) return -1.0f;   /* not a number, NaN or negative: missing */
    return d > 2.0 ? 2.0f : (float)d;
}

static inline void rc_dini_read(const char* t, rc_dini_read_t* r)
{
    char v[64];
    memset(r, 0, sizeof *r);
    r->mode = rsx_dm_ini_parse(t);
    r->scaling = rsx_dm_ini_scaling(t);
    r->filter = rsx_dm_ini_filter(t);
    r->vsync = rsx_dm_ini_vsync(t);
    r->maximize = rsx_dm_ini_maximize(t);
    r->hud169 = rsx_dm_ini_hud169(t);
    r->upscale = rc_dini_upscale(t);
    r->sharpness = rc_dini_sharpness(t);
    if (!rsx_wp_ini_read(t, &r->wp) && rsx_wp_value(t, "size", v, sizeof v)) {
        size_t n = strlen(v);
        int w = 0, h = 0;
        while (n && (v[n - 1] == ' ' || v[n - 1] == '\t' || v[n - 1] == '\r')) v[--n] = 0;
        if (rsx_win_parse_size(v, &w, &h)) {
            memset(&r->wp, 0, sizeof r->wp);
            r->wp.valid = 1;
            r->wp.w = w; r->wp.h = h;   /* no monitor: rsx_wp_resolve centres it on the primary one, shrunk to fit */
            r->from_size = 1;
        }
    }
}

/* ---- shared display.ini (launcher "All games") ---------------------------------------------------------------------- */
static const char* const RC_DINI_SHARED_KEYS[] = { "mode", "size", "scaling", "filter", "vsync", "upscale", "upscale_sharpness" };
#define RC_DINI_N_SHARED ((int)(sizeof RC_DINI_SHARED_KEYS / sizeof RC_DINI_SHARED_KEYS[0]))
enum { RC_DINI_SH_MODE = 1, RC_DINI_SH_SIZE = 2, RC_DINI_SH_SCALING = 4, RC_DINI_SH_FILTER = 8, RC_DINI_SH_VSYNC = 16,
       RC_DINI_SH_UPSCALE = 32, RC_DINI_SH_SHARPNESS = 64 };

/* What the shared file holds (-1 = no key; w/h 0 = no size). Any other key in it (window_*, maximize, hud_169) is
 * not read. */
typedef struct { int mode, scaling, filter, vsync, w, h, upscale; float sharpness; } rc_dini_shared_t;

static inline void rc_dini_read_shared(const char* t, rc_dini_shared_t* s)
{
    char v[64];
    s->mode = s->scaling = s->filter = s->vsync = s->upscale = -1;
    s->w = s->h = 0;
    s->sharpness = -1.0f;
    if (!t) return;
    s->upscale = rc_dini_upscale(t);
    s->sharpness = rc_dini_sharpness(t);
    s->mode = rsx_dm_ini_parse(t);
    s->scaling = rsx_dm_ini_scaling(t);
    s->filter = rsx_dm_ini_filter(t);
    s->vsync = rsx_dm_ini_vsync(t);
    if (rsx_wp_value(t, "size", v, sizeof v)) {
        size_t n = strlen(v);
        int w = 0, h = 0;
        while (n && (v[n - 1] == ' ' || v[n - 1] == '\t' || v[n - 1] == '\r')) v[--n] = 0;
        if (rsx_win_parse_size(v, &w, &h)) { s->w = w; s->h = h; }
    }
}

/* The game's file read into r (rc_dini_read), then every shared key it lacks taken from s. Returns the RC_DINI_SH_*
 * bits of the values that came from the shared file. The size is taken only when the game's file has neither a saved
 * placement (window_*) nor its own size =. */
static inline int rc_dini_apply_shared(rc_dini_read_t* r, const rc_dini_shared_t* s)
{
    int from = 0;
    if (r->mode < 0 && s->mode >= 0) { r->mode = s->mode; from |= RC_DINI_SH_MODE; }
    if (r->scaling < 0 && s->scaling >= 0) { r->scaling = s->scaling; from |= RC_DINI_SH_SCALING; }
    if (r->filter < 0 && s->filter >= 0) { r->filter = s->filter; from |= RC_DINI_SH_FILTER; }
    if (r->vsync < 0 && s->vsync >= 0) { r->vsync = s->vsync; from |= RC_DINI_SH_VSYNC; }
    if (r->upscale < 0 && s->upscale >= 0) { r->upscale = s->upscale; from |= RC_DINI_SH_UPSCALE; }
    if (r->sharpness < 0 && s->sharpness >= 0) { r->sharpness = s->sharpness; from |= RC_DINI_SH_SHARPNESS; }
    if (!r->wp.valid && s->w > 0 && s->h > 0) {
        memset(&r->wp, 0, sizeof r->wp);
        r->wp.valid = 1; r->wp.w = s->w; r->wp.h = s->h;
        r->from_size = 1;
        from |= RC_DINI_SH_SIZE;
    }
    return from;
}

/* Both files in one call: game text (NULL = none), shared text (NULL = none). */
static inline int rc_dini_read_layered(const char* game, const char* shared, rc_dini_read_t* r, rc_dini_shared_t* s)
{
    rc_dini_read(game ? game : "", r);
    rc_dini_read_shared(shared, s);
    return rc_dini_apply_shared(r, s);
}

/* Before a save: drop from kv[] each shared key (mode, scaling, filter, vsync) that the game's file (game, NULL = none)
 * has no line for and whose value is the shared file's. Returns the new count. */
static inline int rc_dini_drop_inherited(rc_dini_kv* kv, int n, const char* game, const rc_dini_shared_t* s)
{
    int i, o = 0;
    for (i = 0; i < n; i++) {
        const char* sv = NULL;
        char v[64];
        if (!strcmp(kv[i].key, "mode") && s->mode >= 0) sv = rsx_dm_name(s->mode);
        else if (!strcmp(kv[i].key, "scaling") && s->scaling >= 0) sv = rsx_dm_scaling_name(s->scaling);
        else if (!strcmp(kv[i].key, "filter") && s->filter >= 0) sv = rsx_dm_filter_name(s->filter);
        else if (!strcmp(kv[i].key, "vsync") && s->vsync >= 0) sv = rsx_dm_vsync_name(s->vsync);
        if (sv && !strcmp(sv, kv[i].val) && !(game && rsx_dm_ini_value(game, kv[i].key, v, sizeof v))) continue;
        if (o != i) kv[o] = kv[i];
        o++;
    }
    return o;
}

/* The environment assignments that carry upscale / upscale_sharpness to KZ3 2145 (it reads RSX_UPSCALE and
 * RSX_UPSCALE_SHARPNESS once, when the first present needs the upscaler). have_up / have_sh: the variable is already
 * set (it wins for one run, as the other KZ3_/RC_ switches do). Writes up to two "NAME=value" strings; returns how many. */
static inline int rc_dini_upscale_env(const rc_dini_read_t* r, int have_up, int have_sh, char out[2][48])
{
    int n = 0;
    if (!have_up && r->upscale >= 0)
        snprintf(out[n++], 48, "RSX_UPSCALE=%s", r->upscale == RC_DINI_UPSCALE_FSR1 ? "fsr1" : "none");
    if (!have_sh && r->sharpness >= 0)
        snprintf(out[n++], 48, "RSX_UPSCALE_SHARPNESS=%.3g", (double)r->sharpness);
    return n;
}

/* The scaling and 2D squeeze an R&C run uses when display.ini has no key. */
static inline int rc_dini_scaling_or_default(int v) { return v >= 0 ? v : RC_DINI_SCALING_DEFAULT; }
static inline int rc_dini_hud169_or_default(int v) { return v >= 0 ? v : RC_DINI_HUD169_DEFAULT; }

#endif
