/* The shared key bindings applied to a game's keybinds profile (rc_keybinds_shared.c; rules in rc_settings.h and
 * rc_keybinds.h rc_kb_shared_inputs). Pure C, callable from C++. */
#ifndef RC_KEYBINDS_SHARED_H
#define RC_KEYBINDS_SHARED_H

#include <stddef.h>
#include "pad_kb_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC_KBS_MAX_KEYS 64
typedef struct {
    PadKbProfile prof;                    /* the game's profile with the shared values as its defaults */
    PadKbDefKey keys[RC_KBS_MAX_KEYS];    /* prof.keys points here */
    int n_keys;                           /* shared [keyboard] lines read */
    int n_mouse;                          /* shared [mouse] lines read */
    int n_bad;                            /* parts of those lines the runtime's parser rejected */
} RcKbShared;

/* base + the shared file's text (NULL or "" = no shared file: out->prof is base's table) -> out. Only the shared
 * inputs of [keyboard] and the [mouse] keys are taken; every other line of the shared file is ignored. The game's own
 * keybinds.ini is then read over these defaults by cellPad (defaults, then the file: a key in the game's file wins). */
void rc_kb_shared_apply(const PadKbProfile* base, const char* shared_text, RcKbShared* out);

/* A game's keybinds.ini text -> out with its shared lines ([keyboard] shared inputs, [mouse] keys) marked
 * "# (shared) " so they fall back to the shared file. For the file the game writes on its first start. Returns the
 * length or -1 (out too small). */
int rc_kb_mark_shared(const char* text, char* out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
