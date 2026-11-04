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

/* The folder name for a game id: the id when it is 1..31 characters of [a-z0-9_-] (catalogue ids are), else
 * "_default" (an exe built without a catalogue entry, or a name that is not safe as a folder). */
static inline const char* rc_settings_game_folder(const char* id)
{
    size_t n = id ? strlen(id) : 0, i;
    if (n == 0 || n > 31) return "_default";
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
static inline int rc_settings_dir(char* out, size_t cap, const char* exe_dir, int exe_portable, const char* root_dir,
                                  int root_portable, const char* appdata, const char* product, const char* game_id,
                                  char sep)
{
    const char* g = rc_settings_game_folder(game_id);
    int n;
    if (exe_portable && exe_dir) n = snprintf(out, cap, "%s%c%s", exe_dir, sep, g);
    else if (root_portable && root_dir) n = snprintf(out, cap, "%s%c%s", root_dir, sep, g);
    else if (appdata && *appdata) n = snprintf(out, cap, "%s%c%s%c%s", appdata, sep, product, sep, g);
    else n = snprintf(out, cap, "%s", g);   /* the working folder */
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
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
