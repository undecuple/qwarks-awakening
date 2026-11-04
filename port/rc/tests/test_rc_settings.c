/* Host test for the per-game settings folder (port/rc/rc_settings.h), the per-game key tables (port/rc/rc_keybinds.h)
 * and R&C patch 6011 (keybinds profile in libs/input/pad_keybinds.h): folder per game id, "_default", the portable
 * rule, the load order (built-in default < file < environment), defaults chosen by id, unknown id -> base table, the
 * [mouse] section, and Project Helghast's file unchanged when no profile is set. run_rc_settings_test.sh builds it. */
#include "pad_keybinds.h"
#include "rc_settings.h"
#include "rc_keybinds.h"

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

    printf("[test_rc_settings] %s (%d)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
