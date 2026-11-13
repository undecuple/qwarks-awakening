/* Ratchet & Clank port host (one exe per game, port/rc/<game>/ for game glue). From the the game port's
 * port/tlp/main.cpp at its a46f947 (itself from KZ2 bffa40a and KZ3 78bf574 port/kz3/main.cpp, based on ps3recomp
 * templates/project, MIT licence: third_party/ps3recomp/LICENSE): the game-independent boot. RC_* switches are RC_*
 * here; the game's display-buffer scan (its own guest addresses) is left out. rc_log.h wraps KZ3's 
 * logger (port/common/log). Game-specific code goes in port/rc/<game>/, never here. */
#include "ppu_recomp.h"

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"
#include "pad_mouse.h"                   /* Project Helghast's mouse (libs/input): the stage 3 cursor rule */
#include "rc_mouse.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <string>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <sys/stat.h>
#include "rc_log.h"   /* the logger (port/common/log, KZ3's logger): file log, crash files, header/end block */
#include "rc_log_ident.h"   /* generated: game id/title/title ID/version/commit for the log header */
void rc_settings_init(const char* game_id);   /* rc_settings.cpp: the per-game settings folder */
void rc_trace_read_start(void);              /* rc_trace_read.cpp: RC_TRACE_READ=<ea>,<len> access tracer (diagnostic) */
std::string rc_settings_path(const char* name);
std::string rc_settings_shared_path(const char* name);   /* <data folder>/shared/<name> (launcher "All games") */
extern "C" void edat_set_cache_dir(const char* dir);   /* libs/filesystem/edat.c, patch 5002 */

#ifdef _WIN32
/* timeBeginPeriod: <windows.h> arrives with WIN32_LEAN_AND_MEAN set, which
 * excludes the multimedia timer API, so it is asked for by name -- and after
 * windows.h, since timeapi.h uses UINT. No shim exists off Windows because
 * there is nothing to shim: the 15.6 ms default timer granularity this works
 * around is a Windows problem. */
#include <timeapi.h>
#endif

/* ---------------------------------------------------------------------------
 * What the scaffold gives this file
 * -----------------------------------------------------------------------*/
extern "C" {
uint32_t ppu_load_elf(const char* path);      /* ELF -> guest RAM, returns entry OPD */
void     ppu_recomp_register(void);           /* generated: lifted table -> address map */
void     ppu_hle_init(void);                  /* firmware import NID -> HLE handler */
void     ppu_sysprx_register(void);           /* boot-critical CRT (sys_initialize_tls, ...) */
void     ppu_fs_register(void);               /* cellFs over the real game directory */
void     lv2_init_syscalls(void);             /* the lv2 syscall table */
void     cellSaveData_register_ctx_handlers(void); /* 9-argument User*Auto{Load,Save} (patch 3001) */
void     cellGame_init_from_paramsfo(const char* sfo_path); /* title id etc. from PARAM.SFO */
const char* cellGame_get_hdd_title_id(void);   /* the title id when PARAM.SFO says CATEGORY "HG", else NULL */
extern uint32_t ppu_hle_inject_base;           /* guest EA of the runtime's GCM window (labels, control) */
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */

/* The status the guest handed to sys_process_exit. A title that exits that way
 * never returns through ppu_run, so this is only read on the path where the
 * entry function unwound instead. */
extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

/* Host-provided symbols the runtime and the HLE libraries link against. */
uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;                  /* ppu_loader.cpp: the OOB guard */

/* Guest-callback dispatch. g_ps3_guest_caller is the hook the HLE runtime
 * calls back into recompiled code through -- cellSysutil events and the GCM
 * vblank/flip handlers. ppu_guest_call does the OPD -> dispatch. */
typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

/* The GCM half of the frame clock. */
void     cellGcmTickVBlank(void);
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
unsigned cellGcm_flip_request_count(void);
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* D3D12 on Windows, Metal on Apple, and the null backend's headless software
 * path anywhere else -- the same selection runtime/host/host_posix.c and the
 * toolkit's boot harness make. All three expose the same three entry points,
 * so the frame clock below is backend-agnostic. */
#if defined(_WIN32)
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
#  define rsx_backend_init    rsx_d3d12_backend_init
#  define rsx_backend_present rsx_d3d12_backend_present
#  define rsx_backend_pump    rsx_d3d12_backend_pump_messages
#  define RSX_BACKEND_NAME    "D3D12"
#elif defined(__APPLE__)
extern "C" int  rsx_metal_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_metal_backend_present(void);
extern "C" int  rsx_metal_backend_pump_messages(void);
#  define rsx_backend_init    rsx_metal_backend_init
#  define rsx_backend_present rsx_metal_backend_present
#  define rsx_backend_pump    rsx_metal_backend_pump_messages
#  define RSX_BACKEND_NAME    "Metal"
#else
extern "C" int  rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_null_backend_present(void);
extern "C" int  rsx_null_backend_pump_messages(void);
#  define rsx_backend_init    rsx_null_backend_init
#  define rsx_backend_present rsx_null_backend_present
#  define rsx_backend_pump    rsx_null_backend_pump_messages
#  define RSX_BACKEND_NAME    "null (headless software)"
#endif

/* ---------------------------------------------------------------------------
 * Configuration
 * -----------------------------------------------------------------------*/
/* The flat VM must span every region the PS3 memory map uses, not just the
 * game image: sys_ppu_thread_create puts thread stacks at 0xD0000000, so a
 * smaller arena makes every spawned thread's stack access out of bounds. */
#define VM_SIZE       0x100010000ull   /* the full 32-bit guest space + a 64K guard */
#define STACK_TOP     0x0FF00000u      /* main-thread stack, below the 0x10000000 segment */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

/* ---------------------------------------------------------------------------
 * Frame clock
 * -----------------------------------------------------------------------*/
/* On real hardware the RSX raises a vblank interrupt about 60 times a second
 * and that is what drives the game's frame loop. With no RSX it is synthesized
 * here: a host thread calls cellGcmTickVBlank/TickFlip, which invoke the
 * handlers the guest registered, and drains the GCM FIFO. Without this the
 * title initialises, registers its handlers and then waits forever.
 *
 * The ticks are driven off real elapsed time rather than off how long present()
 * took. A hidden or occluded window makes present block hard, and pacing the
 * ticks behind it paces the whole game behind it. */
static volatile LONG g_frames_presented = 0;

/* Frames handed to the backend at a guest FLIP boundary -- one per frame the
 * guest actually finished. The presents made before the guest's first flip, so
 * a fresh window is not left blank through a long boot, carry no guest frame
 * and are deliberately not counted. */
extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

/* ps3recomp's second D3D12 path, the live-draw engine (libs/video/rsx_live_draw.c: per-surface depth, MRT,
 * mip chains), opt-in with RSX_LIVE_DRAW=1 for the renderer probe. The null backend opens the window and the
 * engine presents into it; unset, the rsx_d3d12_backend path below is unchanged. Bring-up follows the toolkit's
 * runtime/ppu/tests/boot_main.cpp. */
extern "C" int      rsx_live_draw_enabled(void);
extern "C" int      rsx_live_draw_init(void* hwnd, uint32_t w, uint32_t h,
                                       const uint8_t* (*guest_ptr)(void*, uint32_t, uint32_t, uint32_t), void* user);
extern "C" void     rsx_live_draw_present(uint32_t buffer_id);
extern "C" void     rsx_live_draw_shutdown(void);
extern "C" int      rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" int      rsx_null_backend_pump_messages(void);
extern "C" void*    rsx_null_backend_get_hwnd(void);
extern "C" void     rsx_null_backend_suppress_present(int on);
extern "C" uint32_t cellGcmResolveLocated(int local, uint32_t offset);
extern "C" uint32_t cellGcmResolveIO(uint32_t offset);
extern "C" unsigned cellGcmGetCurrentDisplayBufferId(void);
static int s_rsx_live = 0;

/* (location, offset) -> host memory: location 0 = RSX local memory, 1 = main memory through the IO table. */
static const uint8_t* rsx_live_guest_ptr(void*, uint32_t location, uint32_t offset, uint32_t min_bytes)
{
    if (!vm_base) return nullptr;
    uint32_t ea;
    if (location == 0) ea = cellGcmResolveLocated(1, offset);
    else { ea = cellGcmResolveIO(offset); if (!ea) ea = cellGcmResolveLocated(0, offset); }
    if (!ea || ea == 0xFFFFFFFFu || (uint64_t)ea + min_bytes > 0x100000000ull) return nullptr;
    return (const uint8_t*)vm_base + ea;
}

/* Display modes (KZ3 patch 2095, carried; ENHANCEMENT): windowed / borderless / fullscreen (DXGI exclusive), Alt+Enter
 * toggles windowed and the last fullscreen-type mode. The choice is kept in display.ini in the game's own settings
 * folder (rc_settings.h; per game since 2026-11-03, before that beside one shared keybinds.ini) ([display] mode/scaling/filter), read here before the window opens and written by the window's thread when the mode
 * changes. KZ3_DISPLAY / KZ3_DISPLAY_SCALING / KZ3_DISPLAY_FILTER (names from the patch) override it for one run.
 * Same code as KZ3's port/kz3/main.cpp display block. Window resize stage 1a (2026-11-09): display.ini is
 * read-modify-write, the window placement (2126) is restored and saved, R&C display defaults (rc_display_ini.h). */
#include "rsx_display_mode.h"
#include "rc_display_ini.h"
#ifdef _WIN32
static bool rc_display_ini(wchar_t* out, size_t n)   /* <per-game folder>\display.ini (rc_settings.cpp) */
{
    const std::string p = rc_settings_path("display.ini");
    return MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, out, (int)n) > 0;
}
extern "C" void rsx_display_configure(int saved_mode, int saved_scaling, int saved_filter, int saved_vsync,
                                      void (*save)(int mode, int scaling, int filter, int vsync));   /* rsx_null_backend.c */

/* KZ3 patch 2126 (carried): the window remembered in display.ini (rsx_window_place.h). The runtime restores it at
 * creation (rsx_wp_resolve: the saved monitor when it is still there with the same rectangle, else the primary one,
 * centred; always clamped into that monitor's work area, so an off-screen position never comes back) and calls
 * rc_display_save at every move/resize end, maximize change and close. KZ3_WINDOW_SIZE / RC_WINDOW_SIZE or
 * KZ3_DISPLAY / RC_DISPLAY rule one run: then the placement is neither restored nor saved. */
extern "C" void rsx_display_configure_window(const rsx_wp* saved, int maximize_borderless, int (*close_ask)(int src),
                                             void (*focus)(int focused, int minimized));   /* rsx_null_backend.c */
extern "C" int  rsx_display_get_window(rsx_wp* out);
extern "C" int  rsx_display_get_maximize(void);
extern "C" void rsx_live_draw_set_hud169(int on);   /* rsx_live_draw.c (KZ3 patch 2113) */
static rc_dini_read_t s_dini;                      /* what display.ini held at start (the shared file's keys filled in) */
static rc_dini_shared_t s_dini_shared;             /* what <data folder>/shared/display.ini held at start */
static SRWLOCK s_dini_lock = SRWLOCK_INIT;

static char* rc_display_read_text(const wchar_t* p, size_t* len)   /* the whole file, NUL-terminated; NULL = none */
{
    FILE* f = _wfopen(p, L"rb");
    if (!f) return nullptr;
    char* t = nullptr;
    size_t n = 0, cap = 0;
    for (;;) {
        if (cap - n < 4096) {
            if (cap >= (1u << 20)) break;   /* a display.ini over 1 MiB is not ours: read no further */
            const size_t nc = cap ? cap * 2 : 8192;
            char* g = (char*)realloc(t, nc);
            if (!g) break;
            t = g; cap = nc;
        }
        const size_t k = fread(t + n, 1, cap - n - 1, f);
        n += k;
        if (!k) break;
    }
    fclose(f);
    if (t) t[n] = 0;
    if (len) *len = n;
    return t;
}

/* Read-modify-write (rc_dini_merge): mode/scaling/filter/vsync, maximize and the window_* keys are replaced in place,
 * every other line is kept. Written to display.ini.tmp, then moved over the file. Called on the window's thread. */
static void rc_display_save(int mode, int scaling, int filter, int vsync)   /* vsync: KZ3 patch 2103 */
{
    wchar_t p[1100], tp[1110];
    if (!rc_display_ini(p, 1100)) return;
    AcquireSRWLockExclusive(&s_dini_lock);
    rsx_wp w;
    if (!rsx_display_get_window(&w)) {
        memset(&w, 0, sizeof w);
        if (s_dini.wp.valid && !s_dini.from_size) w = s_dini.wp;
    }
    const int mx = s_dini.maximize >= 0 ? s_dini.maximize : rsx_display_get_maximize();
    rc_dini_kv kv[16];
    int nkv = rc_dini_owned(kv, mode, scaling, filter, vsync, mx, &w);
    size_t ol = 0;
    char* old = rc_display_read_text(p, &ol);
    nkv = rc_dini_drop_inherited(kv, nkv, old, &s_dini_shared);   /* an unchanged shared value stays shared */
    const size_t cap = ol + 4096;
    char* t = (char*)malloc(cap);
    const int n = t ? rc_dini_merge(old, kv, nkv, t, cap) : -1;
    bool ok = false;
    if (n >= 0) {
        swprintf(tp, 1110, L"%ls.tmp", p);
        FILE* f = _wfopen(tp, L"wb");
        ok = f && fwrite(t, 1, (size_t)n, f) == (size_t)n;
        if (f) ok = (fclose(f) == 0) && ok;
        ok = ok && MoveFileExW(tp, p, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok) _wremove(tp);
    }
    free(t);
    free(old);
    ReleaseSRWLockExclusive(&s_dini_lock);
    printf("[display] %ls: mode %s, scaling %s, filter %s, vsync %s, window %s %s\n", p, rsx_dm_name(mode),
           rsx_dm_scaling_name(scaling), rsx_dm_filter_name(filter), rsx_dm_vsync_name(vsync),
           w.valid ? "placement" : "(none)", ok ? "saved (other keys kept)" : "NOT saved");
    fflush(stdout);
}

static void rc_display_load(void)
{
    wchar_t p[1100];
    memset(&s_dini, 0, sizeof s_dini);
    s_dini.mode = s_dini.scaling = s_dini.filter = s_dini.vsync = s_dini.maximize = s_dini.hud169 = -1;
    rc_dini_read_shared(nullptr, &s_dini_shared);
    char* shared = nullptr;
    {
        const std::string sp = rc_settings_shared_path("display.ini");
        wchar_t sw[1100];
        if (MultiByteToWideChar(CP_UTF8, 0, sp.c_str(), -1, sw, 1100) > 0 && (shared = rc_display_read_text(sw, nullptr))) {
            rc_dini_read_shared(shared, &s_dini_shared);
            printf("[display] %ls (shared): mode %s, scaling %s, filter %s, vsync %s, size %dx%d\n", sw,
                   s_dini_shared.mode >= 0 ? rsx_dm_name(s_dini_shared.mode) : "(none)",
                   s_dini_shared.scaling >= 0 ? rsx_dm_scaling_name(s_dini_shared.scaling) : "(none)",
                   s_dini_shared.filter >= 0 ? rsx_dm_filter_name(s_dini_shared.filter) : "(none)",
                   s_dini_shared.vsync >= 0 ? rsx_dm_vsync_name(s_dini_shared.vsync) : "(none)", s_dini_shared.w, s_dini_shared.h);
        }
    }
    if (rc_display_ini(p, 1100)) {
        char* t = rc_display_read_text(p, nullptr);
        if (t || shared) {
            const int from = rc_dini_read_layered(t, shared, &s_dini, &s_dini_shared);
            free(t);
            if (from) printf("[display] from the shared file:%s%s%s%s%s%s%s\n", from & RC_DINI_SH_MODE ? " mode" : "",
                             from & RC_DINI_SH_SIZE ? " size" : "", from & RC_DINI_SH_SCALING ? " scaling" : "",
                             from & RC_DINI_SH_FILTER ? " filter" : "", from & RC_DINI_SH_VSYNC ? " vsync" : "",
                             from & RC_DINI_SH_UPSCALE ? " upscale" : "", from & RC_DINI_SH_SHARPNESS ? " upscale_sharpness" : "");
            printf("[display] %ls: mode %s, scaling %s, filter %s, vsync %s, maximize %s, hud_169 %s, window %s\n", p,
                   s_dini.mode >= 0 ? rsx_dm_name(s_dini.mode) : "(none)",
                   s_dini.scaling >= 0 ? rsx_dm_scaling_name(s_dini.scaling) : "(none)",
                   s_dini.filter >= 0 ? rsx_dm_filter_name(s_dini.filter) : "(none)",
                   s_dini.vsync >= 0 ? rsx_dm_vsync_name(s_dini.vsync) : "(none)",
                   s_dini.maximize < 0 ? "(none)" : s_dini.maximize ? "borderless" : "window",
                   s_dini.hud169 < 0 ? "(none)" : s_dini.hud169 ? "on" : "off",
                   !s_dini.wp.valid ? "(none)" : s_dini.from_size ? "size = (first start)" : "saved");
        }
    }
    free(shared);
    /* ENHANCEMENT (R&C display defaults, rc_display_ini.h): Fit and no 2D squeeze when display.ini has no key. */
    const int scaling = rc_dini_scaling_or_default(s_dini.scaling);
    const int h169 = rc_dini_hud169_or_default(s_dini.hud169);
    rsx_live_draw_set_hud169(h169);   /* KZ3_HUD_169 / RC_HUD_169 win for one run (rsx_live_draw.c) */
    if (s_dini.from_size)
        printf("[window] display.ini size = %dx%d: the first start's client size (centred, shrunk to the work area)\n",
               s_dini.wp.w, s_dini.wp.h);
    printf("[display] scaling %s%s, 2D layer kept 16:9 %s%s\n", rsx_dm_scaling_name(scaling),
           s_dini.scaling < 0 ? " (R&C default)" : "", h169 ? "on" : "off", s_dini.hud169 < 0 ? " (R&C default)" : "");
    {   /* window stage 1d (2026-11-09): display.ini upscale / upscale_sharpness -> KZ3 2145's environment switches */
        char kv[2][48];
        const char* eu = getenv("RSX_UPSCALE");
        const char* es = getenv("RSX_UPSCALE_SHARPNESS");
        const int n = rc_dini_upscale_env(&s_dini, eu && *eu, es && *es, kv);
        for (int i = 0; i < n; i++) _putenv(kv[i]);
        if (s_dini.upscale >= 0 || (eu && *eu))
            printf("[display] upscale %s%s\n", eu && *eu ? eu : s_dini.upscale == RC_DINI_UPSCALE_FSR1 ? "fsr1" : "none",
                   eu && *eu ? " (RSX_UPSCALE wins for this run)" : " (display.ini)");
    }
    rsx_display_configure_window(&s_dini.wp, s_dini.maximize, nullptr, nullptr);
    rsx_display_configure(s_dini.mode, scaling, s_dini.filter, s_dini.vsync, rc_display_save);
}
#endif

static int rc_rsx_init(uint32_t w, uint32_t h, const char* title)
{
#ifdef _WIN32
    rc_display_load();   /* KZ3 patch 2095 */
    if (rsx_live_draw_enabled()) {
        if (rsx_null_backend_init(w, h, title) == 0 &&
            rsx_live_draw_init(rsx_null_backend_get_hwnd(), w, h, rsx_live_guest_ptr, nullptr) == 0) {
            s_rsx_live = 1;
            rsx_null_backend_suppress_present(1);
            fprintf(stderr, "[rsx] live-draw engine up (RSX_LIVE_DRAW=1)\n");
            return 0;
        }
        fprintf(stderr, "[rsx] live-draw init FAILED -- falling back to the D3D12 backend\n");
    }
#endif
    return rsx_backend_init(w, h, title);
}
static void rc_rsx_present(void)
{
    if (s_rsx_live) rsx_live_draw_present(cellGcmGetCurrentDisplayBufferId());
    else            rsx_backend_present();
}
static int rc_rsx_pump(void) { return s_rsx_live ? rsx_null_backend_pump_messages() : rsx_backend_pump(); }

/* Window closed (X button, Alt+F4, a scripted WM_CLOSE such as tools/release/shaders.py's): end the
 * process cleanly from the frame thread, which owns the window and the live-draw walker. Live-draw shutdown first
 * (GPU drain, PSO preload workers joined, pso_library.bin serialized), then the log is flushed and closed, then the
 * process ends with code 0. TerminateProcess rather than exit(): guest PPU/SPU threads are still running and may
 * hold CRT or loader locks, which exit()'s handlers and DLL detach would wait on. A watchdog ends the process with
 * code 3 if the shutdown itself hangs (RC_CLOSE_WATCHDOG=<s>, default 15). Before this the frame loop only
 * stopped presenting and <game>.exe kept running without a window. */
static DWORD WINAPI rc_close_watchdog(LPVOID)
{
    const char* e = getenv("RC_CLOSE_WATCHDOG");
    const int s = (e && atoi(e) > 0) ? atoi(e) : 15;
    Sleep((DWORD)s * 1000u);
    RC_LOGE("exit", "window-close shutdown did not finish in %d s: terminating (exit code 3)", s);
    rc_log_exit(3, "window closed, but the shutdown hung: watchdog terminated the process");
    fflush(NULL);
    TerminateProcess(GetCurrentProcess(), 3);
    return 0;
}
/* Quit request from the game's own UI (port/rc/tods/tods_pause_quit.inc, ENHANCEMENT, RC_PAUSE_QUIT=1): 1 = quit to desktop,
 * 2 = restart (the title entry with RC_PAUSE_QUIT_TITLE=restart). The frame loop takes it like a window close. */
extern "C" { volatile int g_rc_quit_request = 0; }

static void rc_exit_on_window_close(void)
{
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    const int req = g_rc_quit_request;
    RC_LOGI("exit", "%s: shutting down (live draw %s)", req == 1 ? "quit to desktop from the game's menu" : req == 2 ?
            "restart from the game's menu" : "window closed", s_rsx_live ? "on" : "off");
    HANDLE wd = CreateThread(NULL, 0, rc_close_watchdog, NULL, 0, NULL);
    (void)wd;
    if (s_rsx_live) rsx_live_draw_shutdown();
    QueryPerformanceCounter(&t1);
    RC_LOGI("exit", "clean exit after %.0f ms (exit code 0)", (double)(t1.QuadPart - t0.QuadPart) * 1e3 / (double)f.QuadPart);
#ifdef _WIN32
    if (req == 2) {   /* the same exe and command line, started after this one's shutdown work (log still open for the line) */
        STARTUPINFOW si; PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof si); si.cb = sizeof si; memset(&pi, 0, sizeof pi);
        if (CreateProcessW(NULL, GetCommandLineW(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            RC_LOGI("exit", "restart: new process %lu started", (unsigned long)pi.dwProcessId);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        } else {
            RC_LOGE("exit", "restart: CreateProcess failed (error %lu); exiting only", (unsigned long)GetLastError());
        }
    }
#endif
    rc_log_exit(0, req == 1 ? "quit to desktop from the game's menu, clean shutdown" : req == 2 ? "restart from the game's menu"
                            : "window closed by the player, clean shutdown");
    fflush(NULL);
    TerminateProcess(GetCurrentProcess(), 0);
}

/* RC_TEST_CLOSE=<s>: after s seconds, post WM_CLOSE to every top-level window of this process, as
 * tools/release/shaders.py's close_politely does from outside (Proton test of the clean exit). */
static BOOL CALLBACK rc_test_close_enum(HWND h, LPARAM n)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) { PostMessageW(h, WM_CLOSE, 0, 0); ++*(int*)n; }
    return TRUE;
}
static DWORD WINAPI rc_test_close(LPVOID)
{
    const char* e = getenv("RC_TEST_CLOSE");
    Sleep((DWORD)(atoi(e) * 1000));
    rc_log_set_thread_label("test");
    int n = 0;
    EnumWindows(rc_test_close_enum, (LPARAM)&n);
    RC_LOGI("test", "RC_TEST_CLOSE=%s: WM_CLOSE posted to %d window(s)", e, n);
    return 0;
}

/* The player's error box for a fatal error or a crash (from KZ3 299f1c83 port/kz3/main.cpp kz3_show_error, with the
 * plain system box only: KZ3's own notice window is not carried). Test runs (RC_LOG_STDOUT=1, hidden display) and
 * RC_NO_ERROR_BOX=1 never block on a dialog; RC_ERROR_NOTICE=1 forces it on. */
static int rc_error_box_wanted(void)
{
    const char* e = getenv("RC_NO_ERROR_BOX");
    if (e && *e && *e != '0') return 0;
    e = getenv("RC_ERROR_NOTICE");
    if (e && *e == '1') return 1;
    e = getenv("RC_LOG_STDOUT");
    return !(e && *e == '1');
}

static void rc_show_error(const char* what, const char* crash_file)
{
    if (!rc_error_box_wanted()) return;
    char msg[1600];
    snprintf(msg, sizeof msg, "Qwark's Awakening stopped: %s\n\nLog: %s\nCrash report: %s", what, kz3_log_file_path(),
             crash_file && *crash_file ? crash_file : "(none)");
    MessageBoxA(NULL, msg, "Qwark's Awakening", MB_OK | MB_ICONERROR | MB_TOPMOST);
}

static int rc_fatal_box(const char* message, const char* crash_file, void*)
{
    rc_show_error(message, crash_file);
    return 0;   /* stop the process (exit code KZ3_LOG_FATAL_EXIT) */
}

/* Called by ppu_crash_report (patch 2030) after the crash file and the minidump are written. */
extern "C" void kz3_crash_notify(const char* crash_file)
{
    rc_show_error("unexpected error (unhandled exception)", crash_file);
    rc_log_exit(-1, "crash (unhandled exception)");
}

/* Test triggers for the fatal paths (from KZ3's KZ3_TEST_FATAL / KZ3_TEST_CRASH): RC_TEST_FATAL=<s> calls
 * kz3_log_fatal after s seconds, RC_TEST_CRASH=<s> makes a host access violation (the unhandled-exception path). */
static DWORD WINAPI rc_test_fault(LPVOID p)
{
    const int crash = (int)(intptr_t)p;
    const char* e = getenv(crash ? "RC_TEST_CRASH" : "RC_TEST_FATAL");
    Sleep((DWORD)(atoi(e) * 1000));
    rc_log_set_thread_label("test");
    if (!crash) { kz3_log_fatal("test", "RC_TEST_FATAL=%s: forced fatal error", e); return 0; }
    RC_LOGE("test", "RC_TEST_CRASH=%s: forcing a host access violation", e);
    *(volatile int*)(uintptr_t)8 = 1;
    return 0;
}

/* Stage 3 cursor rule (ENHANCEMENT, only with mouse look on; adapted from Killzone 2 PC patch 6046's in-play rule): a game's
 * camera hook (ToD: port/rc/tods/tods_mouse_look.inc) stamps g_rc_mouse_take_ms (QueryPerformanceCounter ms) at each camera
 * update; while one ran within RC_MOUSE_PLAY_MS the pointer is hidden and held in the window (pad_cursor_decide with menu = 0),
 * otherwise (menus, pause, movies, loading, a game without a hook) it is shown and free (menu = 1). With mouse look off nothing
 * is called and the cursor keeps Project Helghast's default (hidden over the client area, never clipped). */
extern "C" { volatile unsigned long long g_rc_mouse_take_ms = 0; }
static void rc_mouse_cursor_tick(void)
{
    static int on = -1;
    if (on < 0) on = pad_mouse_enabled();
    if (!on) return;
    LARGE_INTEGER qf, qc;
    QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&qc);
    const unsigned long long now_ms = (unsigned long long)((double)qc.QuadPart * 1000.0 / (double)qf.QuadPart);
    pad_mouse_menu_set(!rc_mouse_in_play(now_ms, g_rc_mouse_take_ms));
}

static void present_guest_frame(void)
{
    rc_rsx_present();
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    InterlockedIncrement(&g_frames_presented);
}

/* Frame-clock state, shared by the frame loop and the modal-loop tick below (both on the frame thread). */
static int       s_fc_rsx_ok;
static unsigned  s_fc_last_flip;
static ULONGLONG s_fc_next_tick;

/* vblank/flip ticks off real time, presenting a pending flip before each drain. */
static void rc_frame_ticks(ULONGLONG now)
{
    int fired = 0;
    while ((long long)(now - s_fc_next_tick) >= 0 && fired < 240) {
        cellGcmTickVBlank();
        cellGcmTickFlip();
        /* Present a pending flip BEFORE draining any further. The flip
         * fires at a get==put frame boundary on the guest thread, so the
         * batch held right now is exactly the completed frame; presenting
         * after the drain races the guest's next-frame writes and shows a
         * mixed one. */
        if (s_fc_rsx_ok && cellGcm_take_flip_pending()) {
            present_guest_frame();
            s_fc_last_flip = cellGcm_flip_request_count();
        }
        /* Drain the FIFO every tick. This is what writes the RSX sync-fence
         * labels the game's per-frame logic blocks on, so it has to keep
         * advancing at 60 Hz even while present() throttles. */
        if (s_fc_rsx_ok) cellGcm_rsx_process_fifo();
        s_fc_next_tick += 16;             /* ~60 Hz */
        fired++;
    }
    if (fired >= 240) s_fc_next_tick = now;   /* fell too far behind -- resync */
}

/* Drain at the outer cadence too, not only on the 16 ms tick: titles
 * fence every render pass on a label the drain writes, and one wave of
 * those at 16 ms apiece paces the guest into single-figure frame rates.
 * The real RSX writes them in microseconds. */
static void rc_frame_drain(void)
{
    if (cellGcm_take_flip_pending()) {
        present_guest_frame();
        s_fc_last_flip = cellGcm_flip_request_count();
    }
    cellGcm_rsx_process_fifo();
}

/* Present on a guest flip. A present on a fixed clock can catch the
 * drain mid-frame and flash a partial one. Before the first flip
 * present freely, so the window is not blank during boot. */
static void rc_frame_present_on_flip(void)
{
    unsigned fc = cellGcm_flip_request_count();
    if (fc != s_fc_last_flip) {
        /* Consume the pending flip too, or the next pass presents the same
         * flip again (3,904 presents for 2,483 flips). */
        cellGcm_take_flip_pending();
        present_guest_frame();
        s_fc_last_flip = fc;
    } else if (fc == 0) {
        rc_rsx_present();
    }
}

/* Window stage 1c (rc_modal_tick.h has the cause and the rules): while DefWindowProc's modal move/size or menu loop
 * holds the frame thread inside the pump, the loop's own dispatch runs the frame clock. Not an ENHANCEMENT: on the PS3
 * nothing stops the vblank interrupt, so this keeps the console's behaviour on a host whose window can be dragged. */
#include "rc_modal_tick.h"
#define RC_WM_MODAL_TICK (WM_APP + 0x1C0)
static rc_modal  s_modal;
static SRWLOCK   s_modal_lock = SRWLOCK_INIT;
static HANDLE    s_modal_evt;
static HWND      s_modal_hwnd;
static WNDPROC   s_modal_prev;
static unsigned  s_modal_flips0;
static int       s_modal_in_step;

static unsigned long long rc_ms_now(void)
{
    LARGE_INTEGER qf, qc;
    QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&qc);
    return (unsigned long long)((double)qc.QuadPart * 1000.0 / (double)qf.QuadPart);
}

/* Posts one tick every ~4 ms while a modal loop runs, never a second one before the first was handled. */
static DWORD WINAPI rc_modal_pacer(LPVOID)
{
    rc_log_set_thread_label("modal");
    for (;;) {
        WaitForSingleObject(s_modal_evt, INFINITE);
        for (;;) {
            AcquireSRWLockExclusive(&s_modal_lock);
            const int in = s_modal.depth > 0, post = rc_modal_want_post(&s_modal);
            ReleaseSRWLockExclusive(&s_modal_lock);
            if (!in) break;
            if (post && !PostMessageA(s_modal_hwnd, RC_WM_MODAL_TICK, 0, 0)) {
                AcquireSRWLockExclusive(&s_modal_lock);
                s_modal.posted = 0;
                ReleaseSRWLockExclusive(&s_modal_lock);
            }
            Sleep(4);
        }
    }
    return 0;
}

static LRESULT CALLBACK rc_modal_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == RC_WM_MODAL_TICK) {
        AcquireSRWLockExclusive(&s_modal_lock);
        const int run = rc_modal_take_tick(&s_modal);
        ReleaseSRWLockExclusive(&s_modal_lock);
        if (run && !s_modal_in_step) {   /* the step never pumps; the guard only covers a nested loop we do not expect */
            s_modal_in_step = 1;
            rc_frame_ticks(GetTickCount64());
            if (s_fc_rsx_ok) { rc_frame_drain(); rc_frame_present_on_flip(); }
            s_modal_in_step = 0;
        }
        return 0;
    }
    if (msg == WM_SYSCOMMAND) {   /* the commands that start a modal loop, for the log (a drag test reads them) */
        const unsigned sc = (unsigned)(wp & 0xFFF0);
        if (sc == SC_MOVE || sc == SC_SIZE || sc == SC_KEYMENU || sc == SC_MOUSEMENU) {
            printf("[window] WM_SYSCOMMAND %s (0x%04x)\n", sc == SC_MOVE ? "SC_MOVE" : sc == SC_SIZE ? "SC_SIZE" :
                   sc == SC_KEYMENU ? "SC_KEYMENU" : "SC_MOUSEMENU", (unsigned)wp);
            fflush(stdout);
        }
    }
    if (msg == WM_ENTERSIZEMOVE || msg == WM_ENTERMENULOOP) {
        AcquireSRWLockExclusive(&s_modal_lock);
        const int first = rc_modal_enter(&s_modal, rc_ms_now());
        ReleaseSRWLockExclusive(&s_modal_lock);
        if (first) {
            s_modal_flips0 = cellGcm_flip_request_count();
            printf("[window] modal %s loop entered at flip %u: the frame clock runs from the window's message loop\n",
                   msg == WM_ENTERSIZEMOVE ? "move/size" : "menu", s_modal_flips0);
            fflush(stdout);
            SetEvent(s_modal_evt);
        }
    } else if (msg == WM_EXITSIZEMOVE || msg == WM_EXITMENULOOP) {
        AcquireSRWLockExclusive(&s_modal_lock);
        const int last = rc_modal_exit(&s_modal);
        const unsigned ticks = s_modal.ticks;
        const unsigned long long t0 = s_modal.t0;
        ReleaseSRWLockExclusive(&s_modal_lock);
        if (last) {
            const unsigned long long ms = rc_ms_now() - t0;
            const unsigned flips = cellGcm_flip_request_count() - s_modal_flips0;
            printf("[window] modal loop left after %llu ms: %u clock steps, %u guest flips inside (%.1f/s)\n", ms, ticks,
                   flips, ms ? flips * 1000.0 / (double)ms : 0.0);
            fflush(stdout);
        }
    }
    return CallWindowProcA(s_modal_prev, hwnd, msg, wp, lp);
}

/* Subclass the game window (the runtime's null backend window, live-draw path) on the frame thread that created it. */
static void rc_modal_install(HWND hwnd)
{
    if (!hwnd) return;
    s_modal_evt = CreateEventA(NULL, FALSE, FALSE, NULL);
    s_modal_hwnd = hwnd;
    s_modal_prev = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)rc_modal_wndproc);
    if (!s_modal_evt || !s_modal_prev || !CreateThread(NULL, 64u * 1024, rc_modal_pacer, NULL, 0, NULL)) {
        if (s_modal_prev) SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)s_modal_prev);
        fprintf(stderr, "[window] modal-loop clock not installed (error %lu): a drag stops the game until released\n",
                (unsigned long)GetLastError());
        return;
    }
    printf("[window] modal-loop clock on: dragging or resizing the window keeps the game running\n");
}

static DWORD WINAPI frame_clock(LPVOID)
{
    rc_log_set_thread_label("frame");
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "ps3recomp";

    s_fc_rsx_ok = (rc_rsx_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0);
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            s_fc_rsx_ok ? "OK -- window open" : "FAILED");
    if (s_fc_rsx_ok && s_rsx_live) rc_modal_install((HWND)rsx_null_backend_get_hwnd());

    s_fc_next_tick = GetTickCount64();

    for (;;) {
        Sleep(4);
        ULONGLONG now = GetTickCount64();
        rc_mouse_cursor_tick();
        rc_frame_ticks(now);
        if (s_fc_rsx_ok) {
            rc_frame_drain();
            if (rc_rsx_pump() != 0 || g_rc_quit_request) {
                rc_exit_on_window_close();   /* does not return */
                s_fc_rsx_ok = 0;
                continue;
            }
            rc_frame_present_on_flip();
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * VFS root
 * -----------------------------------------------------------------------*/
/* Derive the directory holding PS3_GAME from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* strip EBOOT.elf, USRDIR and PS3_GAME */
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* An HDD game (an installed PSN title, CATEGORY "HG": Deadlocked HD) runs from /dev_hdd0/game/<id> on the console, which
 * holds its PARAM.SFO and USRDIR. Two things follow, both only when not already set in the environment:
 *  - argv[0] is /dev_hdd0/game/<id>/USRDIR/EBOOT.BIN (the runtime's default is the disc path, ppu_loader.cpp PS3_BOOT_PATH);
 *  - /dev_hdd0/game/<id> must read the game folder: when the VFS root is .../game/<id> (the installed layout, as RPCS3's
 *    dev_hdd0 keeps it), the folder holding game/ becomes the read-only lower layer of /dev_hdd0 (PS3_HDD0_LOWER, patch 7014),
 *    so the game's writes still go to the writable PS3_HDD0_ROOT. Any other folder name: a warning, the reads will miss. */
static void set_env_default(const char* k, const char* v)
{
    if (getenv(k) && *getenv(k)) return;
#ifdef _WIN32
    char kv[1300];
    snprintf(kv, sizeof kv, "%s=%s", k, v);
    _putenv(kv);
#else
    setenv(k, v, 0);
#endif
}

/* A licence-type-2 EDAT (Deadlocked HD's rcquatro.edat, read by the game's own licence check at boot) is under the owner's
 * RIF key; the runtime takes it from $PS3_RIF_KEY (edat.c). RC_RIF_KEY_FILE=PATH (opt-in) names a file holding the 16 raw
 * bytes: it is read here, after the log header listed the environment, and handed to edat.c through PS3_RIF_KEY in this
 * process only, so the key never appears on a command line or in a log (the logger also redacts PS3_RIF_KEY). */
static void rc_rif_key_from_file(void)
{
    const char* path = getenv("RC_RIF_KEY_FILE");
    if (!path || !*path) return;
    unsigned char k[16];
    FILE* f = fopen(path, "rb");
    size_t n = f ? fread(k, 1, sizeof k, f) : 0;
    if (f) fclose(f);
    if (n != sizeof k) { printf("[boot] RC_RIF_KEY_FILE: not 16 bytes, ignored\n"); return; }
    char hex[33];
    for (int i = 0; i < 16; i++) snprintf(hex + 2 * i, 3, "%02x", k[i]);
#ifdef _WIN32
    char kv[64];
    snprintf(kv, sizeof kv, "PS3_RIF_KEY=%s", hex);
    _putenv(kv);
    memset(kv, 0, sizeof kv);
#else
    setenv("PS3_RIF_KEY", hex, 1);
#endif
    memset(hex, 0, sizeof hex); memset(k, 0, sizeof k);
    printf("[boot] RIF key read from RC_RIF_KEY_FILE (16 bytes)\n");
}

static void rc_hdd_game_layout(void)
{
    const char* id = cellGame_get_hdd_title_id();
    if (!id || !*id) return;
    char boot[160];
    snprintf(boot, sizeof boot, "/dev_hdd0/game/%s/USRDIR/EBOOT.BIN", id);
    set_env_default("PS3_BOOT_PATH", boot);
    char root[1100], tail[64];
    snprintf(root, sizeof root, "%s", ppu_vfs_root);
    for (char* p = root; *p; p++) if (*p == '\\') *p = '/';
    for (size_t n = strlen(root); n > 1 && root[n - 1] == '/'; n--) root[n - 1] = 0;
    snprintf(tail, sizeof tail, "/game/%s", id);
    const size_t rl = strlen(root), tl = strlen(tail);
    if (rl > tl && strcmp(root + rl - tl, tail) == 0) {
        root[rl - tl] = 0;
        set_env_default("PS3_HDD0_LOWER", root);
    } else if (!getenv("PS3_HDD0_LOWER")) {
        printf("[boot] WARNING: HDD game %s: the game folder %s is not named game/%s, so /dev_hdd0/game/%s will not find it "
               "(set PS3_HDD0_LOWER to the folder holding game/%s)\n", id, ppu_vfs_root, id, id, id);
    }
    printf("[boot] HDD game %s: argv[0] %s, /dev_hdd0 lower layer %s\n", id, getenv("PS3_BOOT_PATH"),
           getenv("PS3_HDD0_LOWER") ? getenv("PS3_HDD0_LOWER") : "(none)");
}

/* ---------------------------------------------------------------------------
 * Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
/* Demand-paging for the flat VM: reserve the whole 4 GB guest space, which
 * costs no commit, and commit each 64 KB page on first access. Every 32-bit
 * guest offset is then valid, so a garbage guest pointer reads zero instead of
 * killing the process. Faults outside the arena fall through untouched. */
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool alloc_guest_vm(void)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, vm_commit_veh);
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#else
    /* No vectored exception handlers off Windows, so the arena is committed up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It stops below the 0xE0000000 mark, so ppu_vm_size arms the
     * loader's bounds check for what is left. */
    vm_base = (uint8_t*)calloc(1, 0xE0000000u);
    ppu_vm_size = 0xE0000000u;
#endif
    return vm_base != nullptr;
}

/* ---------------------------------------------------------------------------
 * Boot diagnostics (opt-in, environment)
 * -----------------------------------------------------------------------*/
/* RC_WATCHDOG="s1,s2,...": at each of those seconds after start, print which
 * HLE call every guest thread is inside and each thread's guest lr. A thread
 * parked in sys_lwmutex_lock / sys_lwcond_wait is invisible to the sampler
 * (it shows as ntdll); this names it. Taken from the toolkit's boot_main.cpp
 * watchdog, minus the thread suspension. RC_PEEK="EA[+off],..." adds guest
 * words to each report: "0x0159493C+8" prints the u32 at EA and the u32 at
 * (that value + 8), i.e. one pointer hop. PS3_SAMPLE=<ms> starts the toolkit's
 * guest-PC sampler as well. */
extern "C" const char* g_hle_inflight[];      /* ppu_hle.cpp; 64 entries */
extern "C" uint32_t    g_sc_inflight[];       /* ppu_loader.cpp: the lv2 syscall each guest thread is inside, 0 = none */
extern "C" int         lv2_sync_describe_thread(unsigned tid, char* buf, unsigned n);   /* sys_mutex.c, patch 7006 */
extern "C" uint32_t    g_last_hle_nid;
extern "C" const char* g_last_hle_name;
extern "C" void        ppu_report_guest_lrs(void);
extern void            ps3_sampler_start(void);

static DWORD WINAPI rc_watchdog(LPVOID p)
{
    const char* spec = (const char*)p;
    ULONGLONG t0 = GetTickCount64();
    for (const char* s = spec; *s; ) {
        unsigned at = (unsigned)strtoul(s, (char**)&s, 10);
        while (*s == ',' || *s == ' ') s++;
        ULONGLONG due = t0 + (ULONGLONG)at * 1000;
        ULONGLONG now = GetTickCount64();
        if (due > now) Sleep((DWORD)(due - now));
        fprintf(stderr, "[WATCHDOG] t=%us; last HLE call = 0x%08X (%s)\n",
                at, g_last_hle_nid, g_last_hle_name ? g_last_hle_name : "");
        for (unsigned t = 0; t < 64u; t++)
            if (g_hle_inflight[t]) {
                fprintf(stderr, "[WATCHDOG]   guest-tid %-3u in %s", t, g_hle_inflight[t]);
                /* (KZ2 printed the calling context here with its patch 6001; not in this series.) */
                fprintf(stderr, "\n");
            }
        /* R&C pass 10: a thread parked in an lv2 syscall (sys_cond_wait 107, sys_mutex_lock 102,
         * sys_semaphore_wait 92, sys_event_queue_receive 130, sys_event_flag_wait 85, sys_lwmutex_lock 97,
         * sys_lwcond_queue_wait 113, sys_timer_usleep 141, ...) is not in g_hle_inflight; name the syscall.
         * Its r3 in the guest-lr line below is usually the object id. */
        for (unsigned t = 0; t < 64u; t++)
            if (g_sc_inflight[t]) {
                fprintf(stderr, "[WATCHDOG]   guest-tid %-3u in lv2 syscall %u\n", t, g_sc_inflight[t]);
                /* R&C pass 11 (patch 7006): for sys_mutex_lock / sys_cond_wait, the mutex and its owner. */
                char sb[384];
                if ((g_sc_inflight[t] == 102 || g_sc_inflight[t] == 107) && lv2_sync_describe_thread(t, sb, sizeof sb))
                    fprintf(stderr, "[WATCHDOG]     tid %u sync: %s\n", t, sb);
            }
        ppu_report_guest_lrs();
        if (const char* pk = getenv("RC_PEEK")) {
            for (const char* q = pk; *q; ) {
                uint32_t ea = (uint32_t)strtoul(q, (char**)&q, 0), off = 0; int hop = 0;
                if (*q == '+') { off = (uint32_t)strtoul(q + 1, (char**)&q, 0); hop = 1; }
                while (*q == ',' || *q == ' ') q++;
                uint32_t v = __builtin_bswap32(*(uint32_t*)(vm_base + ea));
                if (hop) fprintf(stderr, "[WATCHDOG]   peek [0x%08X]=0x%08X  [0x%08X+0x%X]=0x%08X\n", ea, v, v, off,
                                 __builtin_bswap32(*(uint32_t*)(vm_base + (uint32_t)(v + off))));
                else     fprintf(stderr, "[WATCHDOG]   peek [0x%08X]=0x%08X\n", ea, v);
            }
        }
        /* RC_STACK="SP[,SP...]": walk the guest back chain from a parked thread's sp (taken from an earlier
         * watchdog line; a thread parked in an HLE wait keeps its sp). PPC64 ABI: [S] = 64-bit back chain,
         * the callee saves its return address at [caller frame + 16]; so each hop prints the u32 at S'+20. */
        if (const char* st = getenv("RC_STACK")) {
            for (const char* q = st; *q; ) {
                uint32_t sp = (uint32_t)strtoul(q, (char**)&q, 0);
                while (*q == ',' || *q == ' ') q++;
                fprintf(stderr, "[WATCHDOG]   stack sp=0x%08X:", sp);
                for (int i = 0; i < 24 && sp && sp < 0xF0000000u; i++) {
                    uint32_t nx = __builtin_bswap32(*(uint32_t*)(vm_base + sp + 4));
                    if (!nx || nx <= sp) break;
                    fprintf(stderr, " 0x%08X", __builtin_bswap32(*(uint32_t*)(vm_base + nx + 20)));
                    sp = nx;
                }
                fprintf(stderr, "\n");
            }
        }
        fflush(stderr);
    }
    return 0;
}


extern "C" void rc_hle_register(void);   /* port/rc/<game>/hle_<game>.cpp */

static void start_diagnostics(void)
{
    const char* wd = getenv("RC_WATCHDOG");
    if (wd && *wd) CreateThread(NULL, 0, rc_watchdog, (LPVOID)wd, 0, NULL);
    const char* tc = getenv("RC_TEST_CLOSE");   /* scripted window close for test runs */
    if (tc && *tc) CreateThread(NULL, 0, rc_test_close, NULL, 0, NULL);
    const char* tf = getenv("RC_TEST_FATAL");   /* forced fatal error / crash, for the log and crash-file tests */
    if (tf && *tf) CreateThread(NULL, 0, rc_test_fault, (LPVOID)0, 0, NULL);
    const char* tx = getenv("RC_TEST_CRASH");
    if (tx && *tx) CreateThread(NULL, 0, rc_test_fault, (LPVOID)1, 0, NULL);
    rc_trace_read_start();   /* no-op unless RC_TRACE_READ is set */
#ifdef _WIN32
#endif
#ifdef _WIN32
    ps3_sampler_start();     /* no-op unless PS3_SAMPLE is set */
#endif
}

/* ---------------------------------------------------------------------------
 * Main
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}


int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: %s <PPU ELF>\n", argv[0]);
        return 2;
    }

    {   /* the log: <exe folder>/logs/<game id>/ (RC_LOG_DIR overrides); the header block is written here */
        rc_run_info ri = {};
        ri.product = "Qwark's Awakening";   /* tools/release/names.py PRODUCT */
        ri.version = RC_LOG_PORT_VERSION; ri.commit = RC_LOG_GIT_COMMIT;
        ri.game_id = RC_LOG_GAME_ID; ri.title = RC_LOG_GAME_TITLE; ri.title_id = RC_LOG_TITLE_ID;
        ri.update_version = RC_LOG_UPDATE_VERSION;
        rc_log_open(&ri, 0);
        kz3_log_set_fatal_hook(rc_fatal_box, NULL);   /* fatal errors reach the player, never swallowed */
    }

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    /* 1 ms timer resolution (the default ~15.6 ms granularity throttles every shorter guest wait). */
    timeBeginPeriod(1);
    setvbuf(stdout, NULL, _IONBF, 0);
#endif

    printf("=== the game (ps3recomp) ===\n");

    /* Per-game settings folder <data folder>/<game id>/ (rc_settings.h): keybinds.ini (the game's default keys and the
     * [mouse] section, R&C patch 6011), display.ini, settings.ini. Before the env defaults below: a settings.ini value
     * is put into the environment when the variable is unset, so environment > file > built-in default. */
    rc_settings_init(RC_LOG_GAME_ID);

    /* Runtime defaults (an explicit environment setting still wins), the general part of KZ3's list:
     *  PS3_CAMERA=0     no PlayStation Eye; the HLE reports one otherwise.
     *  RSX_LIVE_DRAW=1  the live-draw engine renders (KZ3 ); RSX_LIVE_DRAW=0 selects the plain D3D12 backend.
     * KZ3's SPURS_TASKATTR_DESC=1 (patch 1005, KZ3's Bink task control block as the taskset argument) is left out:
     * ASSUMPTION until the game's SPURS task setup is mapped.
     * (LD_LOCAL_WRITEBACK=post was set here, copied from KZ3's list: nothing in this tree reads it -- KZ3's patch 6008 and
     * its rc_ppu_overrides.cpp are not carried -- so it was dropped, R&C pass 9. Local-memory colour targets are
     * written back only when an NV0039 copy reads them (rsx_live_draw_writeback_range); main-memory ones at sync points.)
     *  GCM_PARK_WAIT_MS=86400000  the FIFO walker waits at a JUMP-to-self park until the title patches it, as the RSX
     *                   does (KZ3 patch 1007's wait; its default is 30000 ms, then it resyncs get to put). In a level the
     *                   RSX parks every frame at the post stage's park (IO 0x01A8424C..0x01A8484C, EA 0x41D8424C..,
     *                   in the frame block) until the SPU job Gfx.Post.Unlock overwrites it (a 4-byte DMA put); one
     *                   post chain took more than 30 s on our slow SPU side, the resync skipped the frame tail with the
     *                   render fence release (NV406E label 0x4B, waited for at 0x48AE8) and the main thread waited
     *                   forever.
     *  LD_VTX_GPU=1, LD_VTX_GPU_INDEX=1  ENHANCEMENT (performance, identical output): the GPU decodes vertices (KZ3 2076)
     *                   and indexed triangle draws use the guest index buffer directly (R&C 7013). ToD Metropolis:
     *                   LD_VTX_GPU_VERIFY found 0 differing draws in 3.27M (tods-ng-r1); flips 8.6 -> 10.7 (p1/p2),
     *                   12.3 -> 13.7 (s0/s1) with the same picture. Set either to 0 for the CPU path. */
    static const char* const rc_env[][2] = { {"PS3_CAMERA", "0"}, {"RSX_LIVE_DRAW", "1"},
                                              {"GCM_PARK_WAIT_MS", "86400000"},
                                              {"LD_VTX_GPU", "1"}, {"LD_VTX_GPU_INDEX", "1"} };
    for (const auto& e : rc_env) {
        if (getenv(e[0])) continue;
#ifdef _WIN32
        char kv[64]; snprintf(kv, sizeof kv, "%s=%s", e[0], e[1]); _putenv(kv);
#else
        setenv(e[0], e[1], 0);
#endif
    }
    rc_log_note_env();   /* the switches in force, defaults included */

    if (!alloc_guest_vm()) {
        RC_LOGE("boot", "could not allocate the guest address space");
        rc_log_exit(1, "could not allocate the guest address space");
        return 1;
    }

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        RC_LOGE("boot", "could not load %s", argv[1]);
        rc_log_exit(1, "could not load the EBOOT");
        return 1;
    }

    /* The runtime's GCM window defaults to 0x20000000, where KZ3 maps its own RSX main memory; KZ3 moved it to
     * 0x7F000000. Same default here until the game's cellGcmMapMainMemory ranges are known (ASSUMPTION: 0x7F000000 is free
     * in the game too). RC_INJECT_BASE=<hex> overrides. */
    {
        const char* e = getenv("RC_INJECT_BASE");
        ppu_hle_inject_base = e ? (uint32_t)strtoul(e, nullptr, 16) : 0x7F000000u;
    }
    derive_vfs_root(argv[1]);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);

    /* Decrypted SDAT/EDAT plaintext goes to a writable cache (patch 5002), never beside the disc files, which are
     * read-only in an install and in the sandbox. Default <working folder>/cache/edat, KZ3's layout (its cache/edat
     * under the working folder; KZ3 picks that folder with kz3.ini data_dir, the game has no ini yet: ASSUMPTION the
     * working folder is the exe's folder or a data folder the launcher chose). $PS3_EDAT_CACHE still wins. Before this,
     * runs had to set PS3_EDAT_CACHE by hand (open bug, 2026-10-19). */
    if (!getenv("PS3_EDAT_CACHE") || !*getenv("PS3_EDAT_CACHE")) {
        static char edc[1100];
        char cwd[1024];
        if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, ".");
        snprintf(edc, sizeof edc, "%s/cache", cwd);
#ifdef _WIN32
        _mkdir(edc);
#else
        mkdir(edc, 0755);
#endif
        strncat(edc, "/edat", sizeof edc - strlen(edc) - 1);
#ifdef _WIN32
        _mkdir(edc);
#else
        mkdir(edc, 0755);
#endif
        edat_set_cache_dir(edc);
        printf("[boot] EDAT cache: %s\n", edc);
    }
    /* /dev_hdd0 and /dev_hdd1 default to <working folder>/gamedata/dev_hdd0 and /dev_hdd1, next to the save data the
     * runtime already keeps in gamedata/dev_hdd0/home. Without them ToD's language-data install (cellGameDataCheckCreate2,
     * then writes to /dev_hdd0/game/BCUS98127/USRDIR, 417 MB) went to the read-only disc folder and the game showed "Game
     * data has become corrupted", and its FIOS cache (/dev_hdd1/cache/BCUS98127) stayed off (2026-10-20). Same
     * working-folder ASSUMPTION as the EDAT cache. $PS3_HDD0_ROOT / $PS3_HDD1_ROOT still win. */
    for (const char* v : {"PS3_HDD0_ROOT", "PS3_HDD1_ROOT"}) {
        if (getenv(v) && *getenv(v)) continue;
        char cwd[1024], dir[1100], kv[1200];
        if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, ".");
        snprintf(dir, sizeof dir, "%s/gamedata", cwd);
#ifdef _WIN32
        _mkdir(dir);
#else
        mkdir(dir, 0755);
#endif
        snprintf(dir, sizeof dir, "%s/gamedata/%s", cwd, v[7] == '0' ? "dev_hdd0" : "dev_hdd1");
#ifdef _WIN32
        _mkdir(dir);
        snprintf(kv, sizeof kv, "%s=%s", v, dir); _putenv(kv);
#else
        mkdir(dir, 0755);
        setenv(v, dir, 0); (void)kv;
#endif
        printf("[boot] %s: %s\n", v, dir);
    }

    ppu_recomp_register();   /* lifted function table -> address map */
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    lv2_init_syscalls();     /* the lv2 syscall table */
    rc_hle_register();      /* imports the runtime leaves unresolved (port/rc/<game>/hle_<game>.cpp) */
    cellSaveData_register_ctx_handlers();   /* 9-argument User*Auto{Load,Save} (patch 3001) */
    {   /* title id from PARAM.SFO (BCAS20087) instead of the runtime default: save directory and SysCache id */
        char sfo[1100];
        struct stat sst;
        snprintf(sfo, sizeof sfo, "%s/PS3_GAME/PARAM.SFO", ppu_vfs_root);
        /* An installed PSN game (Deadlocked HD, CATEGORY "HG") has no PS3_GAME level: its folder, /dev_hdd0/game/<id> on the
         * console, holds PARAM.SFO and USRDIR. Given as the VFS root, its own PARAM.SFO is the one. */
        if (stat(sfo, &sst) != 0) {
            char alt[1100];
            snprintf(alt, sizeof alt, "%s/PARAM.SFO", ppu_vfs_root);
            if (stat(alt, &sst) == 0) snprintf(sfo, sizeof sfo, "%s", alt);
        }
        cellGame_init_from_paramsfo(sfo);
        rc_log_note_sfo(sfo);   /* game version / update version as the game's own PARAM.SFO says */
        rc_hdd_game_layout();   /* an HDD game boots from /dev_hdd0/game/<id> */
    }
    rc_rif_key_from_file();   /* RC_RIF_KEY_FILE: the owner's RIF key for a licence-type-2 EDAT, never logged */

    g_ps3_guest_caller = harness_guest_caller;
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);
    start_diagnostics();

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n", entry, STACK_TOP);
    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);
    const int code = g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
    rc_log_exit(code, g_sys_process_exit_called ? "the guest called sys_process_exit" : "ppu_run returned (entry function unwound)");
    return code;
}
