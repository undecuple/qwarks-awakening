/* Per-game settings folder (rc_settings.h has the rules): the folder, keybinds.ini's place and profile (R&C patch 6011),
 * the [mouse] section into the runtime's mouse switches, and settings.ini into the environment. Called once from
 * main() (with RC_LOG_GAME_ID) before anything reads a setting (the first pad poll, the window, the env defaults). */
#include "rc_settings.h"
#include "rc_keybinds.h"
#include "rc_keybinds_shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#ifndef PS3RECOMP_PRODUCT_NAME
#define PS3RECOMP_PRODUCT_NAME "Qwark's Awakening"
#endif

static std::string s_dir;          /* UTF-8, no trailing separator */
static std::string s_shared_dir;   /* <data folder>/shared (launcher "All games"; never written by the game) */
static RcKbShared s_kbs;           /* the game's keybinds profile with the shared file's values as its defaults */

static void rc_set_env(const char* k, const char* v)
{
#ifdef _WIN32
    std::string kv = std::string(k) + "=" + v;
    _putenv(kv.c_str());
#else
    setenv(k, v, 1);
#endif
}
static const char* rc_env(const char* k)
{
    const char* v = getenv(k);
    return v && *v ? v : nullptr;
}

#ifdef _WIN32
static std::wstring rc_w(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? (size_t)n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
static std::string rc_u8(const wchar_t* w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? (size_t)n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
#endif

static char* rc_read_file(const std::string& path)
{
#ifdef _WIN32
    FILE* f = _wfopen(rc_w(path).c_str(), L"rb");
#else
    FILE* f = fopen(path.c_str(), "rb");
#endif
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (1 << 20)) { fclose(f); return nullptr; }
    char* b = (char*)malloc((size_t)n + 1);
    if (b) b[fread(b, 1, (size_t)n, f)] = 0;
    fclose(f);
    return b;
}
static bool rc_write_file(const std::string& path, const char* t, size_t n)
{
#ifdef _WIN32
    FILE* f = _wfopen(rc_w(path).c_str(), L"wb");
#else
    FILE* f = fopen(path.c_str(), "wb");
#endif
    bool ok = f && fwrite(t, 1, n, f) == n;
    if (f) ok = (fclose(f) == 0) && ok;
    return ok;
}
static void rc_mkdir(const std::string& d)
{
#ifdef _WIN32
    CreateDirectoryW(rc_w(d).c_str(), nullptr);   /* exists already = fine */
#else
    mkdir(d.c_str(), 0755);
#endif
}

static bool rc_dir_portable(const std::string& dir)
{
    char* t = rc_read_file(dir + "/kz3-install.ini");
    const bool r = t && rc_settings_portable(t);
    free(t);
    return r;
}

std::string rc_settings_path(const char* name)
{
#ifdef _WIN32
    return s_dir + "\\" + name;
#else
    return s_dir + "/" + name;
#endif
}

std::string rc_settings_shared_path(const char* name)
{
#ifdef _WIN32
    return s_shared_dir + "\\" + name;
#else
    return s_shared_dir + "/" + name;
#endif
}

void rc_settings_init(const char* game_id)
{
    const char* game = rc_settings_game_folder(game_id);
    for (const auto& a : rc_settings_env_alias)   /* rc_settings.h */
        if (const char* v = rc_env(a[0])) rc_set_env(a[1], v);

    /* 1. the folder */
    char d[2048], sd[2048];
#ifdef _WIN32
    std::string exe_dir, root_dir, appdata;
    {
        wchar_t w[1024];
        DWORD n = GetModuleFileNameW(nullptr, w, 1024);
        if (n > 0 && n < 1024) {
            if (wchar_t* sl = wcsrchr(w, L'\\')) { *sl = 0; exe_dir = rc_u8(w); }
            if (wchar_t* sl = wcsrchr(w, L'\\')) { *sl = 0; root_dir = rc_u8(w); }
        }
        n = GetEnvironmentVariableW(L"APPDATA", w, 1024);
        if (n > 0 && n < 1000) appdata = rc_u8(w);
    }
    const int ep = !exe_dir.empty() && rc_dir_portable(exe_dir);
    const int rp = !ep && !root_dir.empty() && rc_dir_portable(root_dir);
    if (rc_settings_dir(d, sizeof d, exe_dir.c_str(), ep, root_dir.c_str(), rp, appdata.c_str(), PS3RECOMP_PRODUCT_NAME,
                        game_id, '\\') < 0)
        snprintf(d, sizeof d, "%s", game);
    if (rc_settings_shared_dir(sd, sizeof sd, exe_dir.c_str(), ep, root_dir.c_str(), rp, appdata.c_str(),
                               PS3RECOMP_PRODUCT_NAME, '\\') < 0)
        snprintf(sd, sizeof sd, "%s", RC_SETTINGS_SHARED_FOLDER);
    if (!ep && !rp && !appdata.empty()) rc_mkdir(appdata + "\\" + PS3RECOMP_PRODUCT_NAME);
#else
    if (rc_settings_dir(d, sizeof d, nullptr, 0, nullptr, 0, nullptr, PS3RECOMP_PRODUCT_NAME, game_id, '/') < 0)
        snprintf(d, sizeof d, "%s", game);
    if (rc_settings_shared_dir(sd, sizeof sd, nullptr, 0, nullptr, 0, nullptr, PS3RECOMP_PRODUCT_NAME, '/') < 0)
        snprintf(sd, sizeof sd, "%s", RC_SETTINGS_SHARED_FOLDER);
#endif
    s_dir = d;
    s_shared_dir = sd;   /* read only: the launcher's engine writes it */
    rc_mkdir(s_dir);
    printf("[settings] per-game folder (%s): %s; shared folder: %s\n", game, s_dir.c_str(), s_shared_dir.c_str());

    /* 2. keybinds.ini: its place (RC_KEYBINDS / KZ3_KEYBINDS win) and the game's default table, with the shared file's
     * values (shared inputs and [mouse] only, rc_keybinds.h) as its defaults: the game's own file is read over them. */
    const bool kb_env = rc_env("KZ3_KEYBINDS") != nullptr;
    if (!kb_env) rc_set_env("KZ3_KEYBINDS", rc_settings_path("keybinds.ini").c_str());
    {
        char* st = rc_read_file(rc_settings_shared_path("keybinds.ini"));
        rc_kb_shared_apply(rc_kb_profile_for(game_id), st, &s_kbs);
        printf("[settings] %s: %s (%d key line(s), %d mouse line(s), %d problem part(s) ignored)\n",
               rc_settings_shared_path("keybinds.ini").c_str(), st ? "read" : "not found, the game's defaults",
               s_kbs.n_keys, s_kbs.n_mouse, s_kbs.n_bad);
        free(st);
    }
    const std::string kp = rc_settings_path("keybinds.ini");
    char* kb_before = kb_env ? nullptr : rc_read_file(kp);
    const bool kb_new = !kb_env && !kb_before;
    free(kb_before);
#ifdef _WIN32
    cellPad_keybinds_profile(&s_kbs.prof);
    PadKbMouse m;
    const bool have_m = cellPad_keybinds_mouse(&m) != 0;   /* reads (or writes) keybinds.ini now */
#else
    PadKbMouse m = s_kbs.prof.mouse;
    const bool have_m = true;
#endif
    if (kb_new) {   /* written just now with every default: leave its shared lines to the shared file */
        if (char* t = rc_read_file(kp)) {
            const size_t cap = strlen(t) * 2 + 4096;
            char* o = (char*)malloc(cap);
            const int n = o ? rc_kb_mark_shared(t, o, cap) : -1;
            const bool ok = n > 0 && rc_write_file(kp, o, (size_t)n);
            printf("[settings] %s: shared lines marked \"# (shared)\" (they follow the shared file): %s\n", kp.c_str(),
                   ok ? "ok" : "NOT written");
            free(o);
            free(t);
        }
    }
    /* 3. [mouse] -> the runtime's switches: built-in default < keybinds.ini < environment. Mouse look is always set
     * explicitly: the runtime's own default is on (KZ3), ours is the profile's (off until the hand test). */
    if (have_m) {
        char look[4], sens[32], zoom[32], inv[4];
        const PadKbMouse& def = s_kbs.prof.mouse;
        snprintf(look, sizeof look, "%d", m.look >= 0 ? m.look : def.look > 0);
        snprintf(sens, sizeof sens, "%g", (double)(m.sens > 0 ? m.sens : def.sens));
        snprintf(zoom, sizeof zoom, "%g", (double)(m.zoom > 0 ? m.zoom : def.zoom));
        snprintf(inv, sizeof inv, "%d", m.invert >= 0 ? m.invert : def.invert > 0);
        const char* const kv[][2] = { {"KZ3_MOUSE_LOOK", look}, {"KZ3_MOUSE_SENS", sens}, {"KZ3_MOUSE_ZOOM_SENS", zoom},
                                      {"KZ3_MOUSE_INVERT", inv} };
        for (const auto& e : kv) {
            int from;
            const char* v = rc_setting_pick(e[1], e[1], rc_env(e[0]), &from);
            if (from != RC_SET_ENV) rc_set_env(e[0], v);
            printf("[settings] %s = %s (%s)\n", e[0], v, from == RC_SET_ENV ? "environment" : "keybinds.ini [mouse], the shared file or default");
        }
    }

    /* 4. settings.ini */
    const std::string sp = rc_settings_path("settings.ini");
    char* t = rc_read_file(sp);
    if (!t) {
        char buf[4096];
        const int n = rc_settings_format_default(buf, sizeof buf, rc_kb_profile_for(game_id)->title);
        const bool ok = n > 0 && n < (int)sizeof buf && rc_write_file(sp, buf, (size_t)n);
        printf("[settings] %s: %s\n", sp.c_str(), ok ? "not found, written with the defaults" : "not found and not writable, defaults used");
    }
    for (int i = 0; i < RC_N_SETTINGS; i++) {
        const RcSetting& s = rc_settings_table[i];
        char fv[64];
        int from;
        const char* v = rc_setting_pick(s.def, t && rc_ini_get(t, s.section, s.key, fv, sizeof fv) ? fv : nullptr,
                                        rc_env(s.env), &from);
        if (from != RC_SET_ENV) rc_set_env(s.env, v);
        printf("[settings] [%s] %s = %s (%s, %s)\n", s.section, s.key, v, s.env, rc_set_from_names[from]);
    }
    free(t);
    fflush(stdout);
}
