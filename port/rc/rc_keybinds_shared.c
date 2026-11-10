/* Shared key bindings (launcher "All games", 2026-11-09): <data folder>/shared/keybinds.ini read through the
 * runtime's own keybinds parser (libs/input/pad_keybinds.h, R&C patch 6011), filtered to the inputs every game has
 * (rc_keybinds.h rc_kb_shared_inputs) and [mouse], and turned into the defaults of the game's profile. C, because
 * pad_keybinds.h is C (its static parser gets its own copy in this file). Host test: port/rc/tests/test_rc_settings.c. */
#include "pad_keybinds.h"
#include "rc_keybinds.h"
#include "rc_keybinds_shared.h"
#include "rc_settings.h"

#include <stdlib.h>
#include <string.h>

/* The shared file's text -> only its [keyboard] shared-input lines and its [mouse] lines (section headers kept). Lines
 * before any header count as [keyboard] (the parser's rule). */
static char* rc_kbs_filter(const char* t, int* nk, int* nm)
{
    const size_t len = strlen(t);
    char* out = (char*)malloc(len + 32);
    size_t o = 0;
    int sec = 1;   /* 0 other, 1 keyboard, 2 mouse */
    *nk = *nm = 0;
    if (!out) return NULL;
    while (*t) {
        const char* e = strchr(t, '\n');
        const size_t full = e ? (size_t)(e - t) + 1 : strlen(t);
        const char* p = t;
        size_t n = full;
        int keep = 0;
        while (n && (*p == ' ' || *p == '\t')) { p++; n--; }
        while (n && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
        if (n && p[0] == '[') {
            sec = pad_kb_ieq(p, n, "[keyboard]") ? 1 : pad_kb_ieq(p, n, "[mouse]") ? 2 : 0;
            keep = 1;
        } else if (n && p[0] != '#' && p[0] != ';') {
            if (sec == 1 && rc_ini_line_key_in(p, n, rc_kb_shared_inputs, RC_KB_N_SHARED_INPUTS)) { keep = 1; (*nk)++; }
            else if (sec == 2 && rc_ini_line_key_in(p, n, rc_kb_shared_mouse, RC_KB_N_SHARED_MOUSE)) { keep = 1; (*nm)++; }
        }
        if (keep) {
            memcpy(out + o, t, full); o += full;
            if (!e) out[o++] = '\n';
        }
        t += full;
    }
    out[o] = 0;
    return out;
}

void rc_kb_shared_apply(const PadKbProfile* base, const char* shared_text, RcKbShared* out)
{
    const PadKbProfile* saved = pad_kb_profile;
    PadKeybinds kb;
    int i, j;
    char* f;
    memset(out, 0, sizeof *out);
    out->prof = *base;
    for (i = 0; i < base->nkeys && i < RC_KBS_MAX_KEYS; i++) out->keys[i] = base->keys[i];
    out->prof.keys = out->keys;
    out->prof.nkeys = i;
    if (!shared_text || !*shared_text) return;
    f = rc_kbs_filter(shared_text, &out->n_keys, &out->n_mouse);
    if (!f) return;
    pad_kb_profile = base;   /* this file's copy of the parser: base's table, and [mouse] is read */
    pad_kb_defaults(&kb);
    out->n_bad = pad_kb_parse(&kb, f, NULL, NULL);
    pad_kb_profile = saved;
    free(f);
    for (j = 0; j < RC_KB_N_SHARED_INPUTS; j++) {
        const int k = pad_kb_input_by_name(rc_kb_shared_inputs[j], strlen(rc_kb_shared_inputs[j]));
        int at = -1;
        if (k < 0) continue;
        for (i = 0; i < out->prof.nkeys; i++)
            if (out->keys[i].in && !strcmp(out->keys[i].in, rc_kb_shared_inputs[j])) at = i;
        if (at < 0) {
            if (out->prof.nkeys >= RC_KBS_MAX_KEYS) continue;
            at = out->prof.nkeys++;
            out->keys[at].in = rc_kb_shared_inputs[j];
        }
        out->keys[at].a = kb.key[k][0];
        out->keys[at].b = kb.key[k][1];
    }
    out->prof.mouse = kb.mouse;
}

int rc_kb_mark_shared(const char* text, char* out, size_t cap)
{
    char* tmp = (char*)malloc(cap);
    int n;
    if (!tmp) return -1;
    n = rc_ini_mark_shared(text, "keyboard", "keyboard", rc_kb_shared_inputs, RC_KB_N_SHARED_INPUTS, tmp, cap);
    if (n >= 0) n = rc_ini_mark_shared(tmp, "mouse", NULL, rc_kb_shared_mouse, RC_KB_N_SHARED_MOUSE, out, cap);
    free(tmp);
    return n;
}
