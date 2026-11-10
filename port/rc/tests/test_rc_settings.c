/* Host test for the per-game settings folder (port/rc/rc_settings.h), the per-game key tables (port/rc/rc_keybinds.h)
 * and R&C patch 6011 (keybinds profile in libs/input/pad_keybinds.h): folder per game id, "_default", the portable
 * rule, the load order (built-in default < file < environment), defaults chosen by id, unknown id -> base table, the
 * [mouse] section, Project Helghast's file unchanged when no profile is set, and the shared folder and shared key
 * bindings (launcher "All games": shared only, a game key wins, no shared file, the first-start file's shared lines
 * marked). run_rc_settings_test.sh builds it. */
#include "pad_keybinds.h"
#include "rc_settings.h"
#include "rc_keybinds.h"
#include "rc_keybinds_shared.c"   /* the shared key bindings (one translation unit with the test) */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int fails = 0, logs = 0;
static char last[256];
#define CHECK(c, ...) do { printf((c) ? "[PASS] " : "[FAIL] "); if (!(c)) fails++; printf(__VA_ARGS__); printf("\n"); } while (0)

static void logger(void* ctx, const char* m) { (void)ctx; logs++; snprintf(last, sizeof last, "%s", m); }
static int key(const PadKeybinds* k, const char* in, int slot) { return k->key[pad_kb_input_by_name(in, strlen(in))][slot]; }

int main(void)
{
    char d[512], v[64];
    static char buf[16384];
    PadKeybinds k, k2;
    int n, bad, from;

    /* 1. folder name per game id */
    CHECK(!strcmp(rc_settings_game_folder("tods"), "tods"), "tods -> tods");
    CHECK(!strcmp(rc_settings_game_folder("rc1"), "rc1"), "rc1 -> rc1");
    CHECK(!strcmp(rc_settings_game_folder(""), "_default"), "empty id -> _default");
    CHECK(!strcmp(rc_settings_game_folder(NULL), "_default"), "no id -> _default");
    CHECK(!strcmp(rc_settings_game_folder("../x"), "_default"), "unsafe id -> _default");
    CHECK(!strcmp(rc_settings_game_folder("TODS"), "_default"), "upper case is not a catalogue id -> _default");

    /* 2. the folder: APPDATA\product\id; portable exe dir; portable root; no APPDATA = working folder */
    n = rc_settings_dir(d, sizeof d, "C:\\G\\build", 0, "C:\\G", 0, "C:\\U\\AppData\\Roaming", "Qwark's Awakening", "tods", '\\');
    CHECK(n > 0 && !strcmp(d, "C:\\U\\AppData\\Roaming\\Qwark's Awakening\\tods"), "appdata folder: %s", d);
    n = rc_settings_dir(d, sizeof d, "C:\\G\\build", 0, "C:\\G", 0, "C:\\U\\AppData\\Roaming", "Qwark's Awakening", "acit", '\\');
    CHECK(n > 0 && !strcmp(d, "C:\\U\\AppData\\Roaming\\Qwark's Awakening\\acit"), "another game, another folder: %s", d);
    n = rc_settings_dir(d, sizeof d, "C:\\G\\build", 0, "C:\\G", 0, "C:\\A", "Qwark's Awakening", "", '\\');
    CHECK(n > 0 && !strcmp(d, "C:\\A\\Qwark's Awakening\\_default"), "no game id: %s", d);
    n = rc_settings_dir(d, sizeof d, "C:\\G\\build", 1, "C:\\G", 1, "C:\\A", "Qwark's Awakening", "tods", '\\');
    CHECK(n > 0 && !strcmp(d, "C:\\G\\build\\tods"), "portable exe folder first: %s", d);
    n = rc_settings_dir(d, sizeof d, "C:\\G\\build", 0, "C:\\G", 1, "C:\\A", "Qwark's Awakening", "tods", '\\');
    CHECK(n > 0 && !strcmp(d, "C:\\G\\tods"), "portable install root: %s", d);
    n = rc_settings_dir(d, sizeof d, NULL, 0, NULL, 0, NULL, "Qwark's Awakening", "tods", '/');
    CHECK(n > 0 && !strcmp(d, "tods"), "no APPDATA: the working folder's tods: %s", d);
    CHECK(rc_settings_dir(d, 8, NULL, 0, NULL, 0, "C:\\A", "Qwark's Awakening", "tods", '\\') == -1, "too long -> -1");

    /* 3. the portable marker (kz3-install.ini; the runtime's rule: exactly "1") */
    CHECK(rc_settings_portable("[install]\nportable = 1\n"), "portable = 1");
    CHECK(rc_settings_portable("[Install]\r\nportable=1 ; comment\r\n"), "portable=1 with CRLF and comment");
    CHECK(!rc_settings_portable("[install]\nportable = 10\n"), "portable = 10 is not portable");
    CHECK(!rc_settings_portable("[other]\nportable = 1\n"), "another section is not portable");
    CHECK(!rc_settings_portable(NULL), "no file is not portable");

    /* 4. ini reader */
    CHECK(rc_ini_get("[a]\nx = 1\n[b]\nX = two words # c\n", "B", "x", v, sizeof v) && !strcmp(v, "two words"), "ini value by section/key: '%s'", v);
    CHECK(!rc_ini_get("[a]\nx = 1\n", "b", "x", v, sizeof v), "missing section");
    CHECK(!rc_ini_get("[a]\n# x = 1\n", "a", "x", v, sizeof v), "commented key");

    /* 5. load order: default < file < environment */
    CHECK(!strcmp(rc_setting_pick("1", NULL, NULL, &from), "1") && from == RC_SET_DEFAULT, "nothing set: default");
    CHECK(!strcmp(rc_setting_pick("1", "0", NULL, &from), "0") && from == RC_SET_FILE, "file over default");
    CHECK(!strcmp(rc_setting_pick("1", "0", "1", &from), "1") && from == RC_SET_ENV, "environment over file");
    CHECK(!strcmp(rc_setting_pick("1", "", "", &from), "1") && from == RC_SET_DEFAULT, "empty values are unset");

    /* 6. settings.ini written with the defaults reads back as the defaults */
    n = rc_settings_format_default(buf, sizeof buf, "Ratchet & Clank Future: Tools of Destruction");
    CHECK(n > 0 && n < (int)sizeof buf && strstr(buf, "[gameplay]") && strstr(buf, "[performance]"), "settings.ini default (%d bytes)", n);
    {
        int i, ok = 1;
        for (i = 0; i < RC_N_SETTINGS; i++) {
            const RcSetting* s = &rc_settings_table[i];
            ok &= rc_ini_get(buf, s->section, s->key, v, sizeof v) && !strcmp(v, s->def) && strstr(buf, s->env) != NULL;
        }
        CHECK(ok, "every setting reads back as its default and names its variable");
    }

    /* 7. defaults chosen by game id */
    CHECK(rc_kb_profile_for("tods") == &rc_kb_profile_tods, "tods -> its own table");
    CHECK(rc_kb_profile_for("acit") == &rc_kb_profile_base, "acit (no table yet) -> base");
    CHECK(rc_kb_profile_for("nope") == &rc_kb_profile_base && rc_kb_profile_for(NULL) == &rc_kb_profile_base, "unknown/no id -> base");
    pad_kb_profile = rc_kb_profile_for("tods");
    pad_kb_defaults(&k);
    CHECK(key(&k, "ls_up", 0) == 0x57 && key(&k, "ls_left", 0) == 0x41 && key(&k, "ls_down", 0) == 0x53 && key(&k, "ls_right", 0) == 0x44, "ToD: WASD move");
    CHECK(key(&k, "cross", 0) == 0x20 && key(&k, "r1", 0) == 0x01 && key(&k, "l1", 0) == 0x02, "ToD: Space jump, Mouse1 R1, Mouse2 L1");
    CHECK(key(&k, "square", 0) == 0x46 && key(&k, "triangle", 0) == 0x51 && key(&k, "r2", 0) == 0xA2, "ToD: F wrench, Q quick select, LeftCtrl crouch");
    CHECK(key(&k, "rs_left", 0) == 0x25 && key(&k, "rs_up", 0) == 0x26 && key(&k, "start", 0) == 0x1B, "ToD: arrows camera, Esc pause");
    CHECK(key(&k, "back", 0) == 0 && key(&k, "back", 1) == 0, "ToD: KZ3's menu-only back unbound");
    CHECK(k.mouse.look == 0 && k.mouse.invert == 0 && k.mouse.sens == 2.5f && k.mouse.zoom == 1.0f, "ToD: mouse look off by default, 2.5, 1.0");
    {   /* no key bound twice to two different pad buttons in the base table */
        int i, j, s, t, dup = 0;
        for (i = 0; i < PKB_N_INPUTS; i++) for (s = 0; s < PKB_SLOTS; s++) if (k.key[i][s])
            for (j = i + 1; j < PKB_N_INPUTS; j++) for (t = 0; t < PKB_SLOTS; t++) if (k.key[j][t] == k.key[i][s]) {
                dup++; printf("  dup %s/%s key 0x%02X\n", pad_kb_inputs[i].name, pad_kb_inputs[j].name, k.key[i][s]); }
        CHECK(dup == 0, "base table: every key does one thing");
    }

    /* 8. the file with the profile: title, uses, [mouse]; round trip; edits */
    n = pad_kb_format(&k, buf, sizeof buf);
    CHECK(n > 0 && n < (int)sizeof buf, "ToD file fits (%d bytes)", n);
    CHECK(strstr(buf, "Tools of Destruction key bindings") && strstr(buf, "# wrench") && strstr(buf, "[mouse]") &&
          strstr(buf, "look             = off") && strstr(buf, "sensitivity      = 2.5"), "ToD file: title, uses, [mouse]");
    CHECK(!strstr(buf, "Standard controller preset") && !strstr(buf, "Keyboard page"), "ToD file: no Project Helghast menu text");
    pad_kb_defaults(&k2);
    memset(&k2.mouse, 0x55, sizeof k2.mouse);
    logs = 0;
    bad = pad_kb_parse(&k2, buf, logger, NULL);
    CHECK(bad == 0 && logs == 0 && memcmp(&k, &k2, offsetof(PadKeybinds, mouse)) == 0 && k2.mouse.look == k.mouse.look &&
          k2.mouse.invert == k.mouse.invert && k2.mouse.sens == k.mouse.sens && k2.mouse.zoom == k.mouse.zoom, "ToD round trip (bad %d, logs %d: %s)", bad, logs, logs ? last : "");
    pad_kb_defaults(&k2);
    logs = 0;
    bad = pad_kb_parse(&k2, "[mouse]\nlook = on\nsensitivity = 4\nzoom_sensitivity=0.5\ninvert_y = yes\n[keyboard]\nr1 = Mouse1, Enter\n", logger, NULL);
    CHECK(bad == 0 && k2.mouse.look == 1 && k2.mouse.sens == 4.0f && k2.mouse.zoom == 0.5f && k2.mouse.invert == 1 &&
          key(&k2, "r1", 1) == 0x0D, "edited [mouse] and keys read");
    pad_kb_defaults(&k2);
    logs = 0;
    bad = pad_kb_parse(&k2, "[mouse]\nlook = maybe\nsensitivity = -3\nspeed = 2\n", logger, NULL);
    CHECK(bad == 3 && k2.mouse.look == 0 && k2.mouse.sens == 2.5f, "bad [mouse] lines logged, defaults kept (%d: %s)", bad, last);

    /* 9. no profile: Project Helghast's table and file as before */
    pad_kb_profile = NULL;
    pad_kb_defaults(&k);
    CHECK(key(&k, "r1", 0) == 0x01 && key(&k, "r1", 1) == 0x4F && key(&k, "rs_up", 0) == 0x49 && key(&k, "back", 0) == 0x1B,
          "no profile: the built-in (KZ3) defaults");
    n = pad_kb_format(&k, buf, sizeof buf);
    CHECK(!strstr(buf, "[mouse]") && strstr(buf, "Standard controller preset") && strstr(buf, PS3RECOMP_PRODUCT_NAME " key bindings"),
          "no profile: the built-in file, no [mouse]");
    logs = 0;
    bad = pad_kb_parse(&k, "[mouse]\nlook = on\n", logger, NULL);
    CHECK(k.mouse.look == 0 && logs >= 1, "no profile: [mouse] not read (%s)", last);

    /* 10. shared folder and shared key bindings (launcher "All games") */
    {
        static RcKbShared sh;
        static char marked[16384], game[16384];
        PadKeybinds kb0;
        const PadKbProfile* base = rc_kb_profile_for("tods");
        CHECK(!strcmp(rc_settings_game_folder("shared"), "_default"), "a game id \"shared\" never takes the shared folder");
        n = rc_settings_shared_dir(d, sizeof d, "C:\\G\\build", 0, "C:\\G", 0, "C:\\U\\AppData\\Roaming", "Qwark's Awakening", '\\');
        CHECK(n > 0 && !strcmp(d, "C:\\U\\AppData\\Roaming\\Qwark's Awakening\\shared"), "shared folder beside the game folders: %s", d);
        n = rc_settings_shared_dir(d, sizeof d, "C:\\G\\build", 0, "C:\\G", 1, "C:\\A", "Qwark's Awakening", '\\');
        CHECK(n > 0 && !strcmp(d, "C:\\G\\shared"), "portable install: %s", d);
        n = rc_settings_shared_dir(d, sizeof d, NULL, 0, NULL, 0, NULL, "Qwark's Awakening", '/');
        CHECK(n > 0 && !strcmp(d, "shared"), "no APPDATA: the working folder's shared: %s", d);
        CHECK(RC_KB_N_SHARED_INPUTS == 10 && RC_KB_N_SHARED_MOUSE == 4, "10 shared inputs, 4 mouse keys");
        {   /* every shared input is an input of the runtime and bound in every game's default table */
            int j, ok = 1;
            static const char* const ids[] = { "tods", "acit", "qfb", "a4o", "ffa", "nexus", "dlhd", "rc1", "rc2", "rc3", NULL };
            for (j = 0; j < RC_KB_N_SHARED_INPUTS; j++) {
                int g;
                if (pad_kb_input_by_name(rc_kb_shared_inputs[j], strlen(rc_kb_shared_inputs[j])) < 0) ok = 0;
                for (g = 0; ids[g]; g++) {
                    const PadKbProfile* p = rc_kb_profile_for(ids[g]);
                    int i, found = 0;
                    for (i = 0; i < p->nkeys; i++) if (!strcmp(p->keys[i].in, rc_kb_shared_inputs[j]) && p->keys[i].a) found = 1;
                    if (!found) ok = 0;
                }
            }
            CHECK(ok, "every shared input exists and is bound in every game's default table");
        }

        /* no shared file: the game's own defaults */
        rc_kb_shared_apply(base, NULL, &sh);
        pad_kb_profile = base; pad_kb_defaults(&kb0);
        pad_kb_profile = &sh.prof; pad_kb_defaults(&k);
        CHECK(!memcmp(kb0.key, k.key, sizeof k.key) && !memcmp(&kb0.mouse, &k.mouse, sizeof k.mouse) && sh.n_keys == 0,
              "no shared file: the profile is the game's own");
        rc_kb_shared_apply(base, "", &sh);
        pad_kb_profile = &sh.prof; pad_kb_defaults(&k);
        CHECK(!memcmp(kb0.key, k.key, sizeof k.key), "empty shared file: the game's own");

        /* shared only: shared inputs and [mouse] taken, everything else in the shared file ignored */
        rc_kb_shared_apply(base, "# All games\n[keyboard]\nkeys = positions\nls_up = Up\ncross = F, Enter\nsquare = G\n"
                                 "start = Pause\n[mouse]\nlook = on\nsensitivity = 4\n[prompts]\nbuttons = ps4\n"
                                 "[display]\nmode = fullscreen\n", &sh);
        pad_kb_profile = &sh.prof; pad_kb_defaults(&k);
        CHECK(sh.n_keys == 3 && sh.n_mouse == 2 && sh.n_bad == 0, "shared lines read: %d keys, %d mouse, %d bad", sh.n_keys, sh.n_mouse, sh.n_bad);
        CHECK(key(&k, "ls_up", 0) == 0x26 && key(&k, "ls_up", 1) == 0 && key(&k, "cross", 0) == 0x46 && key(&k, "cross", 1) == 0x0D &&
              key(&k, "start", 0) == 0x13, "shared inputs: ls_up Up, cross F/Enter, start Pause");
        CHECK(key(&k, "square", 0) == key(&kb0, "square", 0) && key(&k, "square", 1) == key(&kb0, "square", 1),
              "a game-specific input in the shared file is ignored (square keeps the game's default)");
        CHECK(key(&k, "ls_down", 0) == 0x53 && k.mouse.look == 1 && k.mouse.sens == 4.0f && k.mouse.zoom == kb0.mouse.zoom,
              "shared inputs not in the file and the other mouse keys keep the game's defaults");
        CHECK(sh.prof.title == base->title && sh.prof.uses == base->uses, "title and uses stay the game's");
        CHECK(pad_kb_profile == &sh.prof, "the parser's profile is restored after reading the shared file");

        /* the game's own file wins (cellPad: defaults = the profile with the shared values, then the game's file) */
        pad_kb_profile = &sh.prof; pad_kb_defaults(&k);
        bad = pad_kb_parse(&k, "[keyboard]\ncross = Space\n[mouse]\nsensitivity = 1.5\n", NULL, NULL);
        CHECK(bad == 0 && key(&k, "cross", 0) == 0x20 && key(&k, "ls_up", 0) == 0x26 && k.mouse.sens == 1.5f && k.mouse.look == 1,
              "a game key wins (cross Space, sensitivity 1.5), shared values elsewhere (ls_up Up, look on)");

        /* the game's first-start file: written with every default, its shared lines then marked */
        pad_kb_profile = &sh.prof; pad_kb_defaults(&k);
        n = pad_kb_format(&k, game, sizeof game);
        CHECK(n > 0 && n < (int)sizeof game, "first-start file formatted (%d bytes)", n);
        n = rc_kb_mark_shared(game, marked, sizeof marked);
        CHECK(n > 0 && strstr(marked, "\n# (shared) ls_up ") && strstr(marked, "\n# (shared) cross ") && strstr(marked, "\n# (shared) start ") &&
              strstr(marked, "\n# (shared) look ") && strstr(marked, "\n# (shared) sensitivity "), "shared lines marked");
        CHECK(strstr(marked, "\nsquare ") && strstr(marked, "\nkeys     = positions") && strstr(marked, "\nbuttons  = ") &&
              strstr(marked, "\nl1 "), "game-specific lines, keys = positions and [prompts] kept");
        CHECK(strlen(marked) == strlen(game) + 14 * strlen(RC_INI_SHARED_MARK), "only the 14 shared lines changed");
        /* that file + a new shared value: the shared value now applies */
        rc_kb_shared_apply(base, "[keyboard]\nls_up = I\n[mouse]\ninvert_y = on\n", &sh);
        pad_kb_profile = &sh.prof; pad_kb_defaults(&k);
        logs = 0;
        bad = pad_kb_parse(&k, marked, logger, NULL);
        CHECK(bad == 0 && key(&k, "ls_up", 0) == 0x49 && k.mouse.invert == 1 && key(&k, "square", 0) == key(&kb0, "square", 0),
              "a marked game file follows a later shared change (ls_up I, invert on)");
        CHECK(rc_kb_mark_shared(game, marked, 64) == -1, "mark: too small -> -1");
        n = rc_ini_mark_shared("ls_up = W\n[mouse]\nlook = on\n", "keyboard", "keyboard", rc_kb_shared_inputs, RC_KB_N_SHARED_INPUTS, marked, sizeof marked);
        CHECK(n > 0 && !strcmp(marked, "# (shared) ls_up = W\n[mouse]\nlook = on\n"), "mark: lines before any header are [keyboard]: %s", marked);
        pad_kb_profile = base;
    }

    printf("[test_rc_settings] %s (%d)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
