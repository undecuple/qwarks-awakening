/* Per-game keyboard and mouse defaults (R&C patch 6011's profile, libs/input/pad_kb_profile.h): one table per game
 * id of data/titles/catalog.json, a shared Ratchet & Clank third-person base for the games without their own table.
 * ENHANCEMENT (the console has no keyboard): every binding is a choice of this port. The player's file
 * (<data folder>/<game id>/keybinds.ini, rc_settings.h) starts from these and wins over them.
 *
 * Pad button -> action, Tools of Destruction (sources registered in docs/sources.md):
 *   left stick move, right stick camera, Cross jump: the series' standard (no source needed to bind them);
 *   Square wrench, Triangle Quick Select (L1/R1 page through it), R2 crouch [src:tod-faq-gamerev];
 *   everything else is ASSUMPTION until read in the game's own controls page or code (the RE notes s.1).
 * Key codes: Windows virtual keys of US key positions (pad_keybinds.h pad_kb_keys: Mouse1 0x01, Mouse2 0x02,
 * Mouse3 0x04, Space 0x20, Enter 0x0D, ...). Pure C, no platform header (host test port/rc/tests/test_rc_settings.c). */
#ifndef RC_KEYBINDS_H
#define RC_KEYBINDS_H

#include "pad_kb_profile.h"
#include <string.h>

/* The shared base: third-person action layout. WASD move, arrows/numpad turn the camera (the mouse when mouse look
 * is on), Space jump, LMB fire, RMB strafe/aim, F wrench, Q/Tab quick select, Ctrl/C crouch, Esc pause. */
static const PadKbDefKey rc_kb_base_keys[] = {
    { "up", 0x31, 0 }, { "down", 0x32, 0 }, { "left", 0x33, 0 }, { "right", 0x34, 0 },   /* 1 2 3 4 */
    { "cross", 0x20, 0x0D },       /* Space, Enter */
    { "circle", 0x45, 0x08 },      /* E, Backspace */
    { "square", 0x46, 0x04 },      /* F, Mouse3 */
    { "triangle", 0x51, 0x09 },    /* Q, Tab */
    { "l1", 0x02, 0 },             /* Mouse2 */
    { "r1", 0x01, 0 },             /* Mouse1 */
    { "l2", 0xA0, 0 },             /* LeftShift */
    { "r2", 0xA2, 0x43 },          /* LeftCtrl, C */
    { "l3", 0x58, 0 },             /* X */
    { "r3", 0x56, 0 },             /* V */
    { "start", 0x1B, 0x50 },       /* Escape, P */
    { "select", 0x4D, 0 },         /* M */
    { "back", 0, 0 },              /* KZ3's menu-only Back needs KZ3's menu hook: unbound here */
    { "ls_up", 0x57, 0 }, { "ls_down", 0x53, 0 }, { "ls_left", 0x41, 0 }, { "ls_right", 0x44, 0 },   /* W S A D */
    { "rs_up", 0x26, 0x68 }, { "rs_down", 0x28, 0x62 }, { "rs_left", 0x25, 0x64 }, { "rs_right", 0x27, 0x66 },
};

static const PadKbUse rc_kb_base_uses[] = {
    { "up", "d-pad up (ASSUMPTION)" }, { "down", "d-pad down (ASSUMPTION)" },
    { "left", "d-pad left (ASSUMPTION)" }, { "right", "d-pad right (ASSUMPTION)" },
    { "cross", "jump; menu accept" },
    { "circle", "circle (ASSUMPTION: action; menu back)" },
    { "square", "wrench" },
    { "triangle", "Quick Select (hold)" },
    { "l1", "L1 (ASSUMPTION: strafe/aim; pages Quick Select)" },
    { "r1", "R1 (ASSUMPTION: fire weapon; pages Quick Select)" },
    { "l2", "L2 (ASSUMPTION)" },
    { "r2", "crouch" },
    { "l3", "L3 (ASSUMPTION)" },
    { "r3", "R3 (ASSUMPTION: first-person look)" },
    { "start", "pause menu" },
    { "select", "select (ASSUMPTION: map)" },
    { "back", "not used by this game" },
    { "ls_up", "move forward (left stick up)" }, { "ls_down", "move back" },
    { "ls_left", "move left" }, { "ls_right", "move right" },
    { "rs_up", "camera up (right stick up)" }, { "rs_down", "camera down" },
    { "rs_left", "camera left" }, { "rs_right", "camera right" },
};

#define RC_KB_N(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Shared key bindings (launcher "All games", 2026-11-09): the inputs whose action is the same in every game, read
 * from <data folder>/shared/keybinds.ini before the game's own file (rc_settings.h). Checked against each game's default
 * table (2026-11-09): every game id uses rc_kb_base_keys (ToD through its own profile, every other id through
 * rc_kb_profile_base), so the input names line up by construction; the actions behind them:
 *   ls_up/ls_down/ls_left/ls_right  move (left stick)        ToD: series standard; other games: ASSUMPTION (same)
 *   rs_up/rs_down/rs_left/rs_right  camera (right stick)     ToD: series standard; other games: ASSUMPTION (same)
 *   cross                           jump; menu accept        ToD [src:tod-faq-gamerev]; other games: ASSUMPTION
 *   start                           pause menu               ToD; other games: ASSUMPTION (the PS3 pause button)
 * Not shared: the d-pad, circle, square, triangle, L1/R1/L2/R2, L3/R3, select (weapons, gadgets and maps differ per
 * game, and most of their ToD uses are themselves ASSUMPTION), [prompts] and [pad] (the profile carries no prompts
 * default, and the pad layout remaps game actions). [mouse] (look, sensitivity, zoom_sensitivity, invert_y) is shared. */
static const char* const rc_kb_shared_inputs[] = {
    "ls_up", "ls_down", "ls_left", "ls_right", "rs_up", "rs_down", "rs_left", "rs_right", "cross", "start",
};
static const char* const rc_kb_shared_mouse[] = { "look", "sensitivity", "zoom_sensitivity", "invert_y" };
#define RC_KB_N_SHARED_INPUTS RC_KB_N(rc_kb_shared_inputs)
#define RC_KB_N_SHARED_MOUSE RC_KB_N(rc_kb_shared_mouse)

/* Mouse look default OFF until it has been tried by hand (RC_MOUSE_LOOK=1 or keybinds.ini [mouse] look = on). Sensitivity
 * 2.5 x 0.022 deg/count and zoom 1.0 are Project Helghast's defaults (pad_mouse.h; ASSUMPTION for a third-person
 * camera). */
#define RC_KB_MOUSE_DEFAULT { 0, 0, 2.5f, 1.0f }

/* Tools of Destruction: the base table as it is (its sourced actions are the base's). */
static const PadKbProfile rc_kb_profile_tods = {
    "Ratchet & Clank Future: Tools of Destruction",
    rc_kb_base_keys, RC_KB_N(rc_kb_base_keys), rc_kb_base_uses, RC_KB_N(rc_kb_base_uses), RC_KB_MOUSE_DEFAULT };

/* Every other game (and an exe without a game id): the base, untested on those games (ASSUMPTION). */
static const PadKbProfile rc_kb_profile_base = {
    "Qwark's Awakening",
    rc_kb_base_keys, RC_KB_N(rc_kb_base_keys), rc_kb_base_uses, RC_KB_N(rc_kb_base_uses), RC_KB_MOUSE_DEFAULT };

/* The profile of a game id (catalogue id: tods, acit, qfb, a4o, ffa, nexus, dlhd, rc1, rc2, rc3); unknown or NULL ->
 * the base. A game gets its own entry here when its controls differ from the base. */
static inline const PadKbProfile* rc_kb_profile_for(const char* game_id)
{
    if (game_id && !strcmp(game_id, "tods")) return &rc_kb_profile_tods;
    return &rc_kb_profile_base;
}

#endif
