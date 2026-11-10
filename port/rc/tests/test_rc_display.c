/* Host test for window resize stage 1a (port/rc/rc_display_ini.h, rc_settings.h aliases): display.ini read-modify-write
 * (owned keys replaced in place, every other line kept, missing keys added in [display], no [display] section,
 * duplicates, CRLF, no final newline, a too-small buffer), the size = first-start key, the R&C display defaults
 * (Fit, hud_169 off), RC_WINDOW_SIZE -> KZ3_WINDOW_SIZE with the same parser, and the off-screen check of the restored
 * placement (KZ3 2126 rsx_wp_resolve), and the shared display.ini (launcher "All games": shared only, a game key wins, no
 * shared file, a save writes only the game's file and keeps foreign keys). run_rc_display_test.sh builds it. */
#include "rc_display_ini.h"
#include "rc_settings.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c, ...) do { printf((c) ? "[PASS] " : "[FAIL] "); if (!(c)) fails++; printf(__VA_ARGS__); printf("\n"); } while (0)

static int count(const char* h, const char* n)
{
    int k = 0;
    for (const char* p = strstr(h, n); p; p = strstr(p + 1, n)) k++;
    return k;
}

int main(void)
{
    static char out[8192];
    rc_dini_kv kv[16];
    rsx_wp wp;
    rc_dini_read_t r;
    int n, nk;

    memset(&wp, 0, sizeof wp);
    wp.valid = 1; wp.x = 100; wp.y = 80; wp.w = 1600; wp.h = 900; wp.maximized = 0;
    snprintf(wp.mon, sizeof wp.mon, "%s", "\\\\.\\DISPLAY1");
    wp.ml = 0; wp.mt = 0; wp.mr = 1920; wp.mb = 1080;
    nk = rc_dini_owned(kv, RSX_DM_WINDOWED, RSX_DM_FIT, RSX_DM_BILINEAR, RSX_DM_VSYNC_ON, RSX_MAX_BORDERLESS, &wp);
    CHECK(nk == 12, "owned keys with a placement: %d (12)", nk);

    /* 1. a new file: header, [display], every key; it reads back */
    n = rc_dini_merge(NULL, kv, nk, out, sizeof out);
    CHECK(n > 0 && !strncmp(out, "# Display settings", 18) && strstr(out, "[display]\n"), "new file: header and section");
    rc_dini_read(out, &r);
    CHECK(r.mode == RSX_DM_WINDOWED && r.scaling == RSX_DM_FIT && r.filter == RSX_DM_BILINEAR && r.vsync == RSX_DM_VSYNC_ON &&
          r.maximize == RSX_MAX_BORDERLESS, "new file reads back mode/scaling/filter/vsync/maximize");
    CHECK(r.wp.valid && !r.from_size && r.wp.x == 100 && r.wp.y == 80 && r.wp.w == 1600 && r.wp.h == 900 &&
          !strcmp(r.wp.mon, "\\\\.\\DISPLAY1") && r.wp.mr == 1920 && r.wp.mb == 1080, "new file reads back the placement");

    /* 2. an old file (the pre-1a writer's shape) with hand-written keys, comments and another section */
    {
        const char* old =
            "# my notes\n"
            "[display]\n"
            "mode = fullscreen\n"
            "  scaling=stretch   \n"
            "# keep this comment\n"
            "hud_169 = on\n"
            "filter = nearest\n"
            "vsync = off\n"
            "size = 1920x1080\n"
            "mode = borderless\n"
            "custom_key = 42\n"
            "\n"
            "[other]\n"
            "mode = keep me\n"
            "window_x = 7\n";
        n = rc_dini_merge(old, kv, nk, out, sizeof out);
        CHECK(n > 0, "merge into an old file: %d bytes", n);
        const char* want = "# my notes\n[display]\nmode = windowed\n  scaling = fit\n# keep this comment\nhud_169 = on\n"
                           "filter = bilinear\nvsync = on\nsize = 1920x1080\ncustom_key = 42\n";
        CHECK(!strncmp(out, want, strlen(want)),
              "owned keys replaced in place (indentation kept), the rest kept in order");
        CHECK(count(out, "mode = ") == 2 && strstr(out, "[other]\nmode = keep me\nwindow_x = 7\n"),
              "a duplicate owned key in [display] dropped; [other] untouched");
        CHECK(strstr(out, "maximize = borderless\n") && strstr(out, "window_width = 1600\n") &&
              strstr(out, "window_monitor_rect = 0,0,1920,1080\n"), "missing keys added");
        CHECK(strstr(out, "maximize = borderless\n") < strstr(out, "[other]") &&
              strstr(out, "window_monitor_rect") < strstr(out, "[other]"), "missing keys added inside [display], before [other]");
        rc_dini_read(out, &r);
        CHECK(r.hud169 == 1 && r.wp.valid && !r.from_size && r.wp.x == 100, "reads back: hud_169 kept, placement from window_*");
        /* idempotent */
        {
            static char again[8192];
            const int m = rc_dini_merge(out, kv, nk, again, sizeof again);
            CHECK(m == n && !strcmp(again, out), "a second save changes nothing");
        }
    }

    /* 3. no [display] section: the text is kept and a section appended; CRLF and no final newline */
    {
        n = rc_dini_merge("[audio]\nvolume = 3", kv, nk, out, sizeof out);
        CHECK(n > 0 && !strncmp(out, "[audio]\nvolume = 3\n[display]\n", 29), "no [display]: text kept, section appended");
        rc_dini_read(out, &r);
        CHECK(r.mode == RSX_DM_WINDOWED && r.wp.valid, "appended section reads back");
        n = rc_dini_merge("[display]\r\nmode = fullscreen\r\nfoo = 1\r\nvsync = off", kv, nk, out, sizeof out);
        CHECK(n > 0 && strstr(out, "mode = windowed\r\nfoo = 1\r\n") && strstr(out, "vsync = on\n") &&
              !strstr(out, "vsync = on\n\n"), "CRLF line kept as CRLF; replaced last line without newline gets one newline");
    }

    /* 4. invalid placement: window_* lines of the file are left alone */
    {
        rsx_wp none;
        memset(&none, 0, sizeof none);
        nk = rc_dini_owned(kv, RSX_DM_BORDERLESS, RSX_DM_STRETCH, RSX_DM_NEAREST, RSX_DM_VSYNC_OFF, RSX_MAX_WINDOW, &none);
        CHECK(nk == 5, "owned keys without a placement: %d (5)", nk);
        n = rc_dini_merge("[display]\nwindow_x = 5\nwindow_x = 6\n", kv, nk, out, sizeof out);
        CHECK(strstr(out, "window_x = 5\nwindow_x = 6\n") && strstr(out, "maximize = window\n") && strstr(out, "scaling = stretch\n"),
              "window_* not owned without a placement: kept as they are");
    }

    /* 5. a too-small buffer */
    n = rc_dini_merge("[display]\nmode = windowed\n", kv, nk, out, 20);
    CHECK(n == -1, "too small an output buffer: -1");

    /* 6. size = first-start size; window_* win over it; bad sizes ignored */
    rc_dini_read("[display]\nsize = 1600x900\n", &r);
    CHECK(r.wp.valid && r.from_size && r.wp.w == 1600 && r.wp.h == 900 && !r.wp.mon[0], "size = 1600x900 -> first-start client size");
    rc_dini_read("[display]\nsize = 1080p \r\n", &r);
    CHECK(r.wp.valid && r.from_size && r.wp.w == 1920 && r.wp.h == 1080, "size = 1080p (named, trailing blanks/CR)");
    rc_dini_read("[display]\nsize = 1600x900\nwindow_x = 10\nwindow_y = 20\nwindow_width = 800\nwindow_height = 600\n", &r);
    CHECK(r.wp.valid && !r.from_size && r.wp.w == 800, "window_* win over size =");
    rc_dini_read("[display]\nsize = 100x50\n", &r);
    CHECK(!r.wp.valid, "size under 160x90 ignored");
    rc_dini_read("[display]\nsize = huge\n", &r);
    CHECK(!r.wp.valid, "unreadable size ignored");

    /* 7. R&C display defaults (ENHANCEMENT): Fit, hud_169 off; a file's choice wins */
    rc_dini_read("[display]\nmode = windowed\n", &r);
    CHECK(r.scaling == -1 && rc_dini_scaling_or_default(r.scaling) == RSX_DM_FIT, "no scaling key -> fit");
    CHECK(r.hud169 == -1 && rc_dini_hud169_or_default(r.hud169) == 0, "no hud_169 key -> off");
    rc_dini_read("[display]\nscaling = stretch\nhud_169 = on\n", &r);
    CHECK(rc_dini_scaling_or_default(r.scaling) == RSX_DM_STRETCH && rc_dini_hud169_or_default(r.hud169) == 1,
          "scaling = stretch, hud_169 = on kept");

    /* 8. RC_WINDOW_SIZE is KZ3_WINDOW_SIZE, read by the same parser */
    {
        int w = 0, h = 0;
        CHECK(rc_settings_alias_of("RC_WINDOW_SIZE") && !strcmp(rc_settings_alias_of("RC_WINDOW_SIZE"), "KZ3_WINDOW_SIZE"),
              "RC_WINDOW_SIZE -> KZ3_WINDOW_SIZE");
        CHECK(rc_settings_alias_of("RC_MOUSE_LOOK") && !rc_settings_alias_of("RC_NOPE"), "older aliases kept, unknown -> NULL");
        CHECK(rsx_win_parse_size("1600x900", &w, &h) && w == 1600 && h == 900, "parser: 1600x900");
        CHECK(rsx_win_parse_size("1440p", &w, &h) && w == 2560 && h == 1440, "parser: 1440p");
        CHECK(!rsx_win_parse_size("1600*900", &w, &h), "parser: 1600*900 refused");
    }

    /* 9. off-screen placements are not restored as they are (2126 rsx_wp_resolve, what the runtime calls at creation) */
    {
        rsx_wp_mon m[2] = {
            { "\\\\.\\DISPLAY1", 0, 0, 1920, 1080, 0, 0, 1920, 1040, 1 },
            { "\\\\.\\DISPLAY2", 1920, 0, 3840, 1080, 1920, 0, 3840, 1080, 0 },
        };
        int o[4], fb, k;
        const int fw = 16, fh = 39;
        rsx_wp s = wp;   /* 1600x900 at 100,80 on DISPLAY1 */
        k = rsx_wp_resolve(&s, m, 2, fw, fh, 320, 180, o, &fb);
        CHECK(k == 0 && !fb && o[0] == 100 && o[1] == 80 && o[2] == 1600 && o[3] == 900, "on-screen placement kept");
        s.x = -5000; s.y = 4000;
        k = rsx_wp_resolve(&s, m, 2, fw, fh, 320, 180, o, &fb);
        CHECK(k == 0 && !fb && o[0] >= 0 && o[1] >= 0 && o[0] + o[2] + fw <= 1920 && o[1] + o[3] + fh <= 1040,
              "far off-screen position clamped into the work area (%d,%d)", o[0], o[1]);
        s = wp; snprintf(s.mon, sizeof s.mon, "%s", "\\\\.\\DISPLAY3"); s.x = 5000;
        k = rsx_wp_resolve(&s, m, 2, fw, fh, 320, 180, o, &fb);
        CHECK(k == 0 && fb && o[0] == (1920 - (1600 + fw)) / 2 && o[1] == (1040 - (900 + fh)) / 2,
              "monitor gone: primary, centred (%d,%d)", o[0], o[1]);
        s = wp; s.x = 2000; s.mon[0] = 0;
        k = rsx_wp_resolve(&s, m, 2, fw, fh, 320, 180, o, &fb);
        CHECK(k == 0 && fb, "no monitor name (size = or a hand edit): primary, centred");
        s = wp; s.w = 3000; s.h = 2000;
        k = rsx_wp_resolve(&s, m, 2, fw, fh, 320, 180, o, &fb);
        CHECK(o[2] + fw <= 1920 && o[3] + fh <= 1040 && o[2] * 2000 / 3000 - o[3] <= 1 && o[2] * 2000 / 3000 - o[3] >= -1,
              "too large: shrunk to the work area keeping the aspect (%dx%d)", o[2], o[3]);
        s = wp; s.mr = 2560;   /* same name, monitor rectangle changed (resolution change) */
        k = rsx_wp_resolve(&s, m, 2, fw, fh, 320, 180, o, &fb);
        CHECK(k == 0 && fb, "monitor rectangle changed: primary, centred");
    }

    /* 10. shared display.ini (launcher "All games"): shared only, a game key wins, no shared file, saves */
    {
        static const char* shared = "# All games\n[display]\nmode = borderless\nsize = 1080p\nscaling = stretch\nvsync = off\n"
                                    "window_x = 5\nwindow_y = 5\nwindow_width = 640\nwindow_height = 480\nmaximize = window\nhud_169 = on\n";
        static const char* game_full = "[display]\nmode = fullscreen\nwindow_x = 100\nwindow_y = 80\nwindow_width = 1600\n"
                                       "window_height = 900\nwindow_maximized = 0\nwindow_monitor = \\\\.\\DISPLAY1\n"
                                       "window_monitor_rect = 0,0,1920,1080\n";
        static char save[8192];
        rc_dini_shared_t sh;
        rc_dini_read_t g0;
        int from;
        CHECK(RC_DINI_N_SHARED == 7 && !strcmp(RC_DINI_SHARED_KEYS[0], "mode"), "7 shared display keys");
        /* shared only (no game file) */
        from = rc_dini_read_layered(NULL, shared, &r, &sh);
        CHECK(r.mode == RSX_DM_BORDERLESS && r.scaling == RSX_DM_STRETCH && r.vsync == RSX_DM_VSYNC_OFF && r.filter < 0,
              "shared only: mode, scaling, vsync from the shared file, filter unset");
        CHECK(r.wp.valid && r.from_size && r.wp.w == 1920 && r.wp.h == 1080 && !r.wp.mon[0],
              "shared only: size = 1080p is the first-start size (%dx%d), the shared window_* are not read", r.wp.w, r.wp.h);
        CHECK(r.maximize < 0 && r.hud169 < 0, "shared only: maximize and hud_169 are per game (not read from the shared file)");
        CHECK(from == (RC_DINI_SH_MODE | RC_DINI_SH_SIZE | RC_DINI_SH_SCALING | RC_DINI_SH_VSYNC), "shared only: from bits %d", from);
        /* the game's own keys win */
        from = rc_dini_read_layered(game_full, shared, &r, &sh);
        CHECK(r.mode == RSX_DM_FULLSCREEN && r.scaling == RSX_DM_STRETCH && r.vsync == RSX_DM_VSYNC_OFF,
              "game key wins (mode fullscreen), the rest from the shared file");
        CHECK(r.wp.valid && !r.from_size && r.wp.w == 1600 && r.wp.x == 100 && !strcmp(r.wp.mon, "\\\\.\\DISPLAY1"),
              "a saved placement wins over the shared size");
        CHECK(from == (RC_DINI_SH_SCALING | RC_DINI_SH_VSYNC), "game key wins: from bits %d", from);
        from = rc_dini_read_layered("[display]\nsize = 720p\n", shared, &r, &sh);
        CHECK(r.from_size && r.wp.w == 1280 && !(from & RC_DINI_SH_SIZE), "the game's own size = wins over the shared one");
        /* no shared file: exactly the game's file */
        from = rc_dini_read_layered(game_full, NULL, &r, &sh);
        rc_dini_read(game_full, &g0);
        CHECK(from == 0 && sh.mode < 0 && sh.w == 0 && !memcmp(&r, &g0, sizeof r), "no shared file: the game's file as before");
        from = rc_dini_read_layered(NULL, NULL, &r, &sh);
        CHECK(from == 0 && r.mode < 0 && r.scaling < 0 && !r.wp.valid, "no file at all: every key missing (the defaults apply)");
        /* a save writes only the game's file: an unchanged shared value stays out of it, a change or an own line is kept,
         * foreign keys and comments are kept */
        {
            static const char* game_old = "# mine\n[display]\nfoo = 1\n# (shared) mode = windowed\nvsync = off\n[other]\nx = 1\n";
            const char* shared_copy = shared;
            rc_dini_read_shared(shared, &sh);
            nk = rc_dini_owned(kv, RSX_DM_BORDERLESS, RSX_DM_FIT, RSX_DM_BILINEAR, RSX_DM_VSYNC_OFF, RSX_MAX_BORDERLESS, &wp);
            nk = rc_dini_drop_inherited(kv, nk, game_old, &sh);
            CHECK(nk == 11, "mode (= shared, no game line) dropped; scaling (changed) and vsync (game line) kept: %d (11)", nk);
            n = rc_dini_merge(game_old, kv, nk, save, sizeof save);
            CHECK(n > 0 && !strstr(save, "\nmode = ") && strstr(save, "scaling = fit") && strstr(save, "vsync = off") &&
                  count(save, "vsync") == 1, "save: no mode line, scaling added, vsync kept in place");
            CHECK(strstr(save, "# mine\n") && strstr(save, "foo = 1\n") && strstr(save, "# (shared) mode = windowed\n") &&
                  strstr(save, "[other]\nx = 1\n"), "save: foreign keys, comments and the shared mark kept");
            rc_dini_read_layered(save, shared, &r, &sh);
            CHECK(r.mode == RSX_DM_BORDERLESS && r.scaling == RSX_DM_FIT, "save reads back: mode still follows the shared file, scaling the game's");
            CHECK(shared_copy == shared && strstr(shared, "mode = borderless"), "the shared text is never written");
            rc_dini_read_shared(NULL, &sh);
            nk = rc_dini_owned(kv, RSX_DM_WINDOWED, RSX_DM_FIT, RSX_DM_BILINEAR, RSX_DM_VSYNC_ON, RSX_MAX_BORDERLESS, &wp);
            CHECK(rc_dini_drop_inherited(kv, nk, NULL, &sh) == 12, "no shared file: every owned key is written (as before)");
        }
    }

    /* 11. upscale / upscale_sharpness (window stage 1d): KZ3 2145's RSX_UPSCALE as display.ini keys */
    {
        char env[2][48];
        rc_dini_shared_t sh;
        rc_dini_read("[display]\nmode = windowed\n", &r);
        CHECK(r.upscale < 0 && r.sharpness < 0, "no upscale keys: both missing");
        CHECK(rc_dini_upscale_env(&r, 0, 0, env) == 0, "no keys: no environment change (2145 stays off)");
        rc_dini_read("[display]\nupscale_sharpness = 0.5\nupscale =  FSR1 \r\n", &r);
        CHECK(r.upscale == RC_DINI_UPSCALE_FSR1 && r.sharpness > 0.49f && r.sharpness < 0.51f,
              "upscale = FSR1 (case, blanks, CRLF), sharpness 0.5; the longer key does not shadow upscale");
        CHECK(rc_dini_upscale_env(&r, 0, 0, env) == 2 && !strcmp(env[0], "RSX_UPSCALE=fsr1") &&
              !strcmp(env[1], "RSX_UPSCALE_SHARPNESS=0.5"), "both keys -> RSX_UPSCALE=fsr1, RSX_UPSCALE_SHARPNESS=0.5 (%s, %s)", env[0], env[1]);
        CHECK(rc_dini_upscale_env(&r, 1, 0, env) == 1 && !strcmp(env[0], "RSX_UPSCALE_SHARPNESS=0.5"),
              "RSX_UPSCALE already set: it wins for the run, only the sharpness is set");
        rc_dini_read("[display]\nupscale = off\nupscale_sharpness = 9\n", &r);
        CHECK(r.upscale == RC_DINI_UPSCALE_NONE && r.sharpness == 2.0f, "off = none; sharpness clamped to 2");
        rc_dini_upscale_env(&r, 0, 0, env);
        CHECK(!strcmp(env[0], "RSX_UPSCALE=none"), "upscale = off -> RSX_UPSCALE=none");
        rc_dini_read("[display]\nupscale = dlss\nupscale_sharpness = soft\n[other]\nupscale = fsr1\n", &r);
        CHECK(r.upscale < 0 && r.sharpness < 0, "unknown backend and a non-number are missing; another section is not read");
        rc_dini_read("[display]\nupscale_sharpness = 0\n", &r);
        CHECK(r.sharpness == 0.0f, "sharpness 0 (the sharpest) is a value, not missing");
        /* shared file: player-wide; the game's own line wins */
        CHECK(rc_dini_read_layered("[display]\nmode = borderless\n", "[display]\nupscale = fsr1\nupscale_sharpness = 1\n", &r, &sh) ==
                  (RC_DINI_SH_UPSCALE | RC_DINI_SH_SHARPNESS) && r.upscale == RC_DINI_UPSCALE_FSR1 && r.sharpness == 1.0f,
              "upscale and sharpness come from the shared file");
        CHECK(rc_dini_read_layered("[display]\nupscale = none\n", "[display]\nupscale = fsr1\n", &r, &sh) == 0 &&
                  r.upscale == RC_DINI_UPSCALE_NONE, "the game's upscale = none wins over the shared fsr1");
        /* a save keeps the hand-written keys (they are not owned: the game never changes them) */
        {
            static char save2[4096];
            static const char* old2 = "[display]\nupscale = fsr1\nupscale_sharpness = 0.3\n";
            nk = rc_dini_owned(kv, RSX_DM_WINDOWED, RSX_DM_FIT, RSX_DM_BILINEAR, RSX_DM_VSYNC_ON, RSX_MAX_BORDERLESS, &wp);
            n = rc_dini_merge(old2, kv, nk, save2, sizeof save2);
            CHECK(n > 0 && strstr(save2, "upscale = fsr1\nupscale_sharpness = 0.3\n"), "a save keeps upscale and upscale_sharpness");
        }
    }

    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
