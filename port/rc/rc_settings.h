/* Per-game settings folder and the settings load order (pure helpers; Windows glue in rc_settings.cpp).
 *
 * Every R&C game exe keeps its own settings in <data folder>/<game id>/ (2026-11-03: games have different controls,
 * display and gameplay choices, but share one product folder):
 *   <data folder> = the install folder when kz3-install.ini (the runtime's portable marker, next to the exe or one
 *                   level up) says [install] portable = 1 (same rule as cellPad.c pad_kb_find_path), else
 *                   %APPDATA%\Qwark's Awakening (PS3RECOMP_PRODUCT_NAME), else the working folder;
 *   <game id>     = the catalogue id the log header uses (RC_LOG_GAME_ID: tods, acit, qfb, a4o, ffa, nexus, dlhd, rc1,
 *                   rc2, rc3); empty or unusable as a folder name -> "_default".
 * Files there: keybinds.ini (keys, pad layout, prompts, [mouse]; cellPad, R&C patch 6011), display.ini (window mode,
 * scaling, filter, vsync; KZ3 2095/2103), settings.ini (rc_settings_table below). The launcher's per-game settings
 * screen edits these files later (docs/launcher-plan.md "Per game: settings").
 * Shared files (launcher "All games", 2026-11-09; launcher-plan.md "Shared display and control settings"):
 *   <data folder>/shared/display.ini and <data folder>/shared/keybinds.ini, written only by the launcher's engine
 *   (tools/release/catalog.py set-setting --shared). A game reads the shared file, then its own; a key in the game's
 *   own file wins. Only the keys every game has are read from them: display.ini mode, size, scaling, filter, vsync
 *   (rc_display_ini.h; window_*, maximize and hud_169 stay per game) and keybinds.ini's shared inputs and [mouse]
 *   (rc_keybinds.h rc_kb_shared_inputs). The game never writes a shared file. Why a "shared" subfolder and not
 *   <data folder>/display.ini: before 2026-11-03 the runtime kept one keybinds.ini (full table) and display.ini at that
 *   root; reading those as shared would silently pin every key of every game. "shared" is not a catalogue id
 *   (rc_settings_game_folder maps a game id "shared" to "_default").
 * Machine-wide (not per game): nothing is written yet. The GPU adapter choice is the only machine-wide setting the
 * runtime has (KZ3 2101/2134: chosen at start, environment only); it would go to <data folder>/machine.ini.
 *
 * Load order of every setting: the built-in default, then the file, then the environment (wins, for one run).
 * Host test: port/rc/tests/test_rc_settings.c. */
#ifndef RC_SETTINGS_H
#define RC_SETTINGS_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define RC_SETTINGS_SHARED_FOLDER "shared"   /* <data folder>/shared: the files of every game (launcher "All games") */

/* The folder name for a game id: the id when it is 1..31 characters of [a-z0-9_-] (catalogue ids are), else
 * "_default" (an exe built without a catalogue entry, or a name that is not safe as a folder). */
static inline const char* rc_settings_game_folder(const char* id)
{
    size_t n = id ? strlen(id) : 0, i;
    if (n == 0 || n > 31 || !strcmp(id, RC_SETTINGS_SHARED_FOLDER)) return "_default";
    for (i = 0; i < n; i++) {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return "_default";
    }
    return id;
}

/* The value of `key` in `[section]` of an ini text (sections and keys compare without case; '#' and ';' start a
 * comment; blanks around the value trimmed). 1 = found (out holds it, cut to cap - 1), 0 = not there. */
static inline int rc_ini_get(const char* text, const char* section, const char* key, char* out, size_t cap)
{
    const char* p = text;
    const size_t sl = strlen(section), kl = strlen(key);
    int in = 0;
    if (!text || !cap) return 0;
    while (*p) {
        const char* e = p;
        const char *a, *b, *c;
        while (*e && *e != '\n') e++;
        for (c = p; c < e && *c != '#' && *c != ';'; c++) {}
        a = p; b = c;
        while (a < b && (*a == ' ' || *a == '\t' || *a == '\r')) a++;
        while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r')) b--;
        if (a < b && *a == '[') {
            const char* r = (const char*)memchr(a, ']', (size_t)(b - a));
            size_t i;
            in = r && (size_t)(r - a - 1) == sl;
            for (i = 0; in && i < sl; i++) in = ((a[1 + i] | 0x20) == (section[i] | 0x20));
        } else if (in && a < b) {
            const char* q = (const char*)memchr(a, '=', (size_t)(b - a));
            const char* ke = q;
            if (q) {
                size_t i;
                int same;
                while (ke > a && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
                same = (size_t)(ke - a) == kl;
                for (i = 0; same && i < kl; i++) same = ((a[i] | 0x20) == (key[i] | 0x20));
                if (same) {
                    const char* v = q + 1;
                    size_t n;
                    while (v < b && (*v == ' ' || *v == '\t')) v++;
                    n = (size_t)(b - v);
                    if (n >= cap) n = cap - 1;
                    memcpy(out, v, n);
                    out[n] = 0;
                    return 1;
                }
            }
        }
        p = *e ? e + 1 : e;
    }
    return 0;
}

/* kz3-install.ini text: [install] portable = 1 exactly (the runtime's and the launcher's rule). */
static inline int rc_settings_portable(const char* install_ini)
{
    char v[8];
    return install_ini && rc_ini_get(install_ini, "install", "portable", v, sizeof v) && !strcmp(v, "1");
}

/* The per-game folder (no trailing separator) from what the host found: exe_dir / root_dir with their portable flags,
 * appdata (NULL or "" = none), the product name, the game id and the separator ('\\' on Windows). Returns the
 * length, or -1 when it does not fit. */
static inline int rc_settings_dir_named(char* out, size_t cap, const char* exe_dir, int exe_portable,
                                        const char* root_dir, int root_portable, const char* appdata, const char* product,
                                        const char* g, char sep)
{
    int n;
    if (exe_portable && exe_dir) n = snprintf(out, cap, "%s%c%s", exe_dir, sep, g);
    else if (root_portable && root_dir) n = snprintf(out, cap, "%s%c%s", root_dir, sep, g);
    else if (appdata && *appdata) n = snprintf(out, cap, "%s%c%s%c%s", appdata, sep, product, sep, g);
    else n = snprintf(out, cap, "%s", g);   /* the working folder */
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}
static inline int rc_settings_dir(char* out, size_t cap, const char* exe_dir, int exe_portable, const char* root_dir,
                                  int root_portable, const char* appdata, const char* product, const char* game_id,
                                  char sep)
{
    return rc_settings_dir_named(out, cap, exe_dir, exe_portable, root_dir, root_portable, appdata, product,
                                 rc_settings_game_folder(game_id), sep);
}
/* The shared folder <data folder>/shared, same rule as the per-game one. */
static inline int rc_settings_shared_dir(char* out, size_t cap, const char* exe_dir, int exe_portable,
                                         const char* root_dir, int root_portable, const char* appdata,
                                         const char* product, char sep)
{
    return rc_settings_dir_named(out, cap, exe_dir, exe_portable, root_dir, root_portable, appdata, product,
                                 RC_SETTINGS_SHARED_FOLDER, sep);
}

/* The mark a line gets when it is left to the shared file: "# (shared) key = value". The engine's --unset writes it
 * and set-setting reuses its place; the game writes it into a keybinds.ini it creates (rc_ini_mark_shared). */
#define RC_INI_SHARED_MARK "# (shared) "

/* Is the trimmed line p[0..n) "key = ..." for one of keys[] (no case)? */
static inline int rc_ini_line_key_in(const char* p, size_t n, const char* const* keys, int nkeys)
{
    int k;
    for (k = 0; k < nkeys; k++) {
        const size_t kl = strlen(keys[k]);
        size_t i;
        int same = n > kl;
        for (i = 0; same && i < kl; i++) same = ((p[i] | 0x20) == (keys[k][i] | 0x20));
        if (!same) continue;
        while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
        if (i < n && p[i] == '=') return 1;
    }
    return 0;
}

/* text -> out with every "key = ..." line of `section` whose key is in keys[] turned into RC_INI_SHARED_MARK + the line
 * (so the shared file or the default decides it); every other line kept byte for byte. A file with no section header
 * before its first line counts as being in `first_section` (NULL = none). Returns the length, -1 when out is too small. */
static inline int rc_ini_mark_shared(const char* text, const char* section, const char* first_section,
                                     const char* const* keys, int nkeys, char* out, size_t cap)
{
    const char* t = text;
    size_t o = 0;
    const size_t sl = strlen(section), ml = strlen(RC_INI_SHARED_MARK);
    int in = first_section && !strcmp(first_section, section);
    if (!cap) return -1;
    while (t && *t) {
        const char* e = strchr(t, '\n');
        const size_t full = e ? (size_t)(e - t) + 1 : strlen(t);
        const char* p = t;
        size_t n = full;
        while (n && (*p == ' ' || *p == '\t')) { p++; n--; }
        while (n && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
        if (n && p[0] == '[') {
            size_t i;
            in = n == sl + 2 && p[n - 1] == ']';
            for (i = 0; in && i < sl; i++) in = ((p[1 + i] | 0x20) == (section[i] | 0x20));
        } else if (in && n && p[0] != '#' && p[0] != ';' && rc_ini_line_key_in(p, n, keys, nkeys)) {
            if (o + ml >= cap) return -1;
            memcpy(out + o, RC_INI_SHARED_MARK, ml); o += ml;
            if (o + (full - (size_t)(p - t)) >= cap) return -1;
            memcpy(out + o, p, full - (size_t)(p - t)); o += full - (size_t)(p - t);
            t = e ? e + 1 : NULL;
            continue;
        }
        if (o + full >= cap) return -1;
        memcpy(out + o, t, full); o += full;
        t = e ? e + 1 : NULL;
    }
    out[o] = 0;
    return (int)o;
}

/* RC_* names of the runtime's switches that read KZ3_* names (pad_mouse.c, cellPad.c, rsx_window_size.h,
 * rsx_null_backend.c): rc_settings_init copies RC_X into KZ3_X before anything reads it, so the runtime's own parser
 * handles both. The RC_ name wins when both are set. */
static const char* const rc_settings_env_alias[][2] = {
    {"RC_MOUSE_LOOK", "KZ3_MOUSE_LOOK"},   {"RC_MOUSE_SENS", "KZ3_MOUSE_SENS"},
    {"RC_MOUSE_ZOOM_SENS", "KZ3_MOUSE_ZOOM_SENS"}, {"RC_MOUSE_INVERT", "KZ3_MOUSE_INVERT"},
    {"RC_MOUSE_YAW", "KZ3_MOUSE_YAW"},     {"RC_MOUSE_SCRIPT", "KZ3_MOUSE_SCRIPT"},
    {"RC_MOUSE_FEED", "KZ3_MOUSE_FEED"},   {"RC_MOUSE_LOG", "KZ3_MOUSE_LOG"},
    {"RC_CURSOR_HIDE", "KZ3_CURSOR_HIDE"}, {"RC_KEYBINDS", "KZ3_KEYBINDS"},
    {"RC_WINDOW_SIZE", "KZ3_WINDOW_SIZE"}, {"RC_DISPLAY", "KZ3_DISPLAY"},   /* window resize stage 1a (2026-11-09) */
    {"RC_DISPLAY_SCALING", "KZ3_DISPLAY_SCALING"}, {"RC_DISPLAY_FILTER", "KZ3_DISPLAY_FILTER"},
    {"RC_HUD_169", "KZ3_HUD_169"},         {"RC_TEST_DISPLAY", "KZ3_TEST_DISPLAY"},
};
/* The KZ3_* name an RC_* switch stands for, or NULL. */
static inline const char* rc_settings_alias_of(const char* rc_name)
{
    size_t i;
    for (i = 0; rc_name && i < sizeof rc_settings_env_alias / sizeof rc_settings_env_alias[0]; i++)
        if (!strcmp(rc_settings_env_alias[i][0], rc_name)) return rc_settings_env_alias[i][1];
    return NULL;
}

/* One setting: built-in default, then the file, then the environment. Returns the value and says where it came from. */
enum { RC_SET_DEFAULT = 0, RC_SET_FILE = 1, RC_SET_ENV = 2 };
static inline const char* rc_setting_pick(const char* def, const char* file_val, const char* env_val, int* from)
{
    if (env_val && *env_val) { if (from) *from = RC_SET_ENV; return env_val; }
    if (file_val && *file_val) { if (from) *from = RC_SET_FILE; return file_val; }
    if (from) *from = RC_SET_DEFAULT;
    return def;
}
static const char* const rc_set_from_names[3] = { "default", "file", "environment" };

/* settings.ini: the per-game switches that are not keys or display. Each maps to the variable the code reads; the
 * file's value is put into the environment when that variable is not set already (so the environment wins).
 *   [gameplay]  opt-in gameplay changes (ENHANCEMENT, default off). None exist yet: the section is written empty with
 *               its rule, so a later change adds its line here and nowhere else.
 *   [performance] ENHANCEMENTs with identical output (default on; main.cpp rc_env has the same defaults). */
typedef struct { const char* section; const char* key; const char* env; const char* def; const char* what; } RcSetting;
static const RcSetting rc_settings_table[] = {
    { "performance", "vtx_gpu", "LD_VTX_GPU", "1",
      "1 = the GPU decodes vertices (KZ3 2076; same picture, faster), 0 = the CPU path" },
    { "performance", "vtx_gpu_index", "LD_VTX_GPU_INDEX", "1",
      "1 = indexed draws read the game's index buffer directly (R&C 7013), 0 = the CPU path" },
};
#define RC_N_SETTINGS ((int)(sizeof rc_settings_table / sizeof rc_settings_table[0]))

/* The settings.ini written on the first start (the defaults, commented). Returns the length (snprintf style). */
static inline int rc_settings_format_default(char* buf, size_t cap, const char* game_title)
{
    size_t o = 0;
    int i;
    const char* sec = "";
#define RCS_PUT(...) do { int w_ = snprintf(buf + (o < cap ? o : cap), o < cap ? cap - o : 0, __VA_ARGS__); \
                          if (w_ > 0) o += (size_t)w_; } while (0)
    RCS_PUT("# %s: game settings of this game only (keys: keybinds.ini, window: display.ini, both in this folder).\n"
            "# Written with the defaults on the first start; read at each start. An environment variable of the same\n"
            "# setting (named after each line) wins for one run. Delete this file to get the defaults back.\n"
            "\n[gameplay]\n"
            "# Changes to how the game plays. Each is an addition of this port and off by default; none exist yet.\n",
            game_title ? game_title : "Qwark's Awakening");
    sec = "gameplay";
    for (i = 0; i < RC_N_SETTINGS; i++) {
        const RcSetting* s = &rc_settings_table[i];
        if (strcmp(sec, s->section)) { RCS_PUT("\n[%s]\n", s->section); sec = s->section; }
        RCS_PUT("# %s (%s)\n%s = %s\n", s->what, s->env, s->key, s->def);
    }
#undef RCS_PUT
    return (int)o;
}

#endif
