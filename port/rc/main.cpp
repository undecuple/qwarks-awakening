/* Ratchet & Clank port host (one exe per game, port/rc/<game>/ for game glue). From the the game port's
 * port/tlp/main.cpp at its a46f947 (itself from KZ2 bffa40a and KZ3 78bf574 port/kz3/main.cpp, based on ps3recomp
 * templates/project, MIT licence: third_party/ps3recomp/LICENSE): the game-independent boot. RC_* switches are RC_*
 * here; the game's display-buffer scan (its own guest addresses) is left out. rc_log.h is a stdio stand-in for KZ3's 
 * logger. Game-specific code goes in port/rc/<game>/, never here. */
#include "ppu_recomp.h"

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"

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
#include "rc_log.h"   /* stdio stand-in for KZ3's logger */
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
 * toggles windowed and the last fullscreen-type mode. The choice is kept in display.ini beside keybinds.ini
 * ([display] mode/scaling/filter), read here before the window opens and written by the window's thread when the mode
 * changes. KZ3_DISPLAY / KZ3_DISPLAY_SCALING / KZ3_DISPLAY_FILTER (names from the patch) override it for one run.
 * Same code as KZ3's port/kz3/main.cpp display block. */
#include "rsx_display_mode.h"
#ifdef _WIN32
extern "C" int  pad_kb_sibling_path(const wchar_t* name, wchar_t* out, size_t n);   /* cellPad.c (patch 2095) */
extern "C" void rsx_display_configure(int saved_mode, int saved_scaling, int saved_filter,
                                      void (*save)(int mode, int scaling, int filter));   /* rsx_null_backend.c */

static void rc_display_save(int mode, int scaling, int filter)
{
    wchar_t p[1100];
    char t[512];
    const int n = rsx_dm_ini_write(t, sizeof t, mode, scaling, filter);
    if (!pad_kb_sibling_path(L"display.ini", p, 1100)) return;
    FILE* f = _wfopen(p, L"wb");
    const bool ok = f && fwrite(t, 1, (size_t)n, f) == (size_t)n;
    if (f) fclose(f);
    printf("[display] %ls: mode %s, scaling %s, filter %s %s\n", p, rsx_dm_name(mode), rsx_dm_scaling_name(scaling),
           rsx_dm_filter_name(filter), ok ? "saved" : "NOT saved");
    fflush(stdout);
}

static void rc_display_load(void)
{
    wchar_t p[1100];
    char t[2048];
    int mode = -1, scaling = -1, filter = -1;
    if (pad_kb_sibling_path(L"display.ini", p, 1100)) {
        FILE* f = _wfopen(p, L"rb");
        if (f) {
            const size_t n = fread(t, 1, sizeof t - 1, f);
            fclose(f);
            t[n] = 0;
            mode = rsx_dm_ini_parse(t);
            scaling = rsx_dm_ini_scaling(t);
            filter = rsx_dm_ini_filter(t);
            printf("[display] %ls: mode %s, scaling %s, filter %s\n", p, mode >= 0 ? rsx_dm_name(mode) : "(none)",
                   scaling >= 0 ? rsx_dm_scaling_name(scaling) : "(none)", filter >= 0 ? rsx_dm_filter_name(filter) : "(none)");
        }
    }
    rsx_display_configure(mode, scaling, filter, rc_display_save);
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
    rc_log_shutdown();
    fflush(NULL);
    TerminateProcess(GetCurrentProcess(), 3);
    return 0;
}
static void rc_exit_on_window_close(void)
{
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    RC_LOGI("exit", "window closed: shutting down (live draw %s)", s_rsx_live ? "on" : "off");
    HANDLE wd = CreateThread(NULL, 0, rc_close_watchdog, NULL, 0, NULL);
    (void)wd;
    if (s_rsx_live) rsx_live_draw_shutdown();
    QueryPerformanceCounter(&t1);
    RC_LOGI("exit", "clean exit after %.0f ms (exit code 0)", (double)(t1.QuadPart - t0.QuadPart) * 1e3 / (double)f.QuadPart);
    rc_log_shutdown();
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

static void present_guest_frame(void)
{
    rc_rsx_present();
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    InterlockedIncrement(&g_frames_presented);
}

static DWORD WINAPI frame_clock(LPVOID)
{
    rc_log_set_thread_label("frame");
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "ps3recomp";

    int rsx_ok = (rc_rsx_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0);
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK -- window open" : "FAILED");

    unsigned  last_flip = 0;
    ULONGLONG next_tick = GetTickCount64();

    for (;;) {
        Sleep(4);
        ULONGLONG now = GetTickCount64();

        int fired = 0;
        while ((long long)(now - next_tick) >= 0 && fired < 240) {
            cellGcmTickVBlank();
            cellGcmTickFlip();
            /* Present a pending flip BEFORE draining any further. The flip
             * fires at a get==put frame boundary on the guest thread, so the
             * batch held right now is exactly the completed frame; presenting
             * after the drain races the guest's next-frame writes and shows a
             * mixed one. */
            if (rsx_ok && cellGcm_take_flip_pending()) {
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            /* Drain the FIFO every tick. This is what writes the RSX sync-fence
             * labels the game's per-frame logic blocks on, so it has to keep
             * advancing at 60 Hz even while present() throttles. */
            if (rsx_ok) cellGcm_rsx_process_fifo();
            next_tick += 16;             /* ~60 Hz */
            fired++;
        }
        if (fired >= 240) next_tick = now;   /* fell too far behind -- resync */

        /* Drain at the outer cadence too, not only on the 16 ms tick: titles
         * fence every render pass on a label the drain writes, and one wave of
         * those at 16 ms apiece paces the guest into single-figure frame rates.
         * The real RSX writes them in microseconds. */
        if (rsx_ok) {
            if (cellGcm_take_flip_pending()) {
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            cellGcm_rsx_process_fifo();

            if (rc_rsx_pump() != 0) {
                rc_exit_on_window_close();   /* does not return */
                rsx_ok = 0;
                continue;
            }
            /* Present on a guest flip. A present on a fixed clock can catch the
             * drain mid-frame and flash a partial one. Before the first flip
             * present freely, so the window is not blank during boot. */
            unsigned fc = cellGcm_flip_request_count();
            if (fc != last_flip) {
                /* Consume the pending flip too, or the next pass presents the same
                 * flip again (3,904 presents for 2,483 flips). */
                cellGcm_take_flip_pending();
                present_guest_frame();
                last_flip = fc;
            } else if (fc == 0) {
                rc_rsx_present();
            }
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
        /* rc-runtime pass 10: a thread parked in an lv2 syscall (sys_cond_wait 107, sys_mutex_lock 102,
         * sys_semaphore_wait 92, sys_event_queue_receive 130, sys_event_flag_wait 85, sys_lwmutex_lock 97,
         * sys_lwcond_queue_wait 113, sys_timer_usleep 141, ...) is not in g_hle_inflight; name the syscall.
         * Its r3 in the guest-lr line below is usually the object id. */
        for (unsigned t = 0; t < 64u; t++)
            if (g_sc_inflight[t]) {
                fprintf(stderr, "[WATCHDOG]   guest-tid %-3u in lv2 syscall %u\n", t, g_sc_inflight[t]);
                /* rc-runtime pass 11 (patch 7006): for sys_mutex_lock / sys_cond_wait, the mutex and its owner. */
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

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    /* 1 ms timer resolution (the default ~15.6 ms granularity throttles every shorter guest wait). */
    timeBeginPeriod(1);
    setvbuf(stdout, NULL, _IONBF, 0);
#endif

    printf("=== the game (ps3recomp) ===\n");

    /* Runtime defaults (an explicit environment setting still wins), the general part of KZ3's list:
     *  PS3_CAMERA=0     no PlayStation Eye; the HLE reports one otherwise.
     *  RSX_LIVE_DRAW=1  the live-draw engine renders (KZ3 ); RSX_LIVE_DRAW=0 selects the plain D3D12 backend.
     * KZ3's SPURS_TASKATTR_DESC=1 (patch 1005, KZ3's Bink task control block as the taskset argument) is left out:
     * ASSUMPTION until the game's SPURS task setup is mapped.
     * (LD_LOCAL_WRITEBACK=post was set here, copied from KZ3's list: nothing in this tree reads it -- KZ3's patch 6008 and
     * its rc_ppu_overrides.cpp are not carried -- so it was dropped, rc-runtime pass 9. Local-memory colour targets are
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

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the guest address space\n");
        return 1;
    }

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        fprintf(stderr, "ERROR: could not load %s\n", argv[1]);
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
        snprintf(sfo, sizeof sfo, "%s/PS3_GAME/PARAM.SFO", ppu_vfs_root);
        cellGame_init_from_paramsfo(sfo);
    }

    g_ps3_guest_caller = harness_guest_caller;
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);
    start_diagnostics();

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n", entry, STACK_TOP);
    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);
    rc_log_shutdown();
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
