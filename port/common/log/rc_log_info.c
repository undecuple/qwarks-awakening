/* Ratchet & Clank port: run header and end block over the logger. See rc_log_info.h. Portable C11 (MinGW-w64, Linux). */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE 1
#endif
#include "rc_log_info.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <pthread.h>
#  include <sys/utsname.h>
#  include <time.h>
#  include <unistd.h>
extern char** environ;
#endif
#if defined(__x86_64__) || defined(__i386__)
#  include <cpuid.h>
#endif

#define TAG "run"

/* ------------------------------------------------------------------------------------------------ time */
static uint64_t mono_ms(void)
{
#ifdef _WIN32
    static LARGE_INTEGER f; LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e3 / (double)f.QuadPart);
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}
static void sleep_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&ts, NULL);
#endif
}

/* ------------------------------------------------------------------------------------------------ SHA-256 */
typedef struct { uint32_t h[8]; uint64_t n; unsigned char buf[64]; size_t fill; } sha_t;
static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(sha_t* s, const unsigned char* p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h;
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3), s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha_init(sha_t* s)
{
    static const uint32_t i0[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    memcpy(s->h, i0, sizeof i0); s->n = 0; s->fill = 0;
}
static void sha_update(sha_t* s, const unsigned char* p, size_t n)
{
    s->n += n;
    while (n) {
        if (!s->fill && n >= 64) { sha_block(s, p); p += 64; n -= 64; continue; }
        size_t c = 64 - s->fill; if (c > n) c = n;
        memcpy(s->buf + s->fill, p, c); s->fill += c; p += c; n -= c;
        if (s->fill == 64) { sha_block(s, s->buf); s->fill = 0; }
    }
}
static void sha_final(sha_t* s, char hex[65])
{
    uint64_t bits = s->n * 8; unsigned char pad = 0x80, z = 0, len[8];
    sha_update(s, &pad, 1);
    while (s->fill != 56) sha_update(s, &z, 1);
    for (int i = 0; i < 8; i++) len[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha_update(s, len, 8);
    for (int i = 0; i < 8; i++) snprintf(hex + 8 * i, 9, "%08x", s->h[i]);
}

int rc_log_sha256_file(const char* path, char hex64[65], unsigned long long* size_out)
{
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    static const size_t CH = 1u << 20;
    unsigned char* buf = (unsigned char*)malloc(CH);
    if (!buf) { fclose(f); return -1; }
    sha_t s; sha_init(&s);
    size_t n; unsigned long long total = 0;
    while ((n = fread(buf, 1, CH, f)) > 0) { sha_update(&s, buf, n); total += n; }
    int bad = ferror(f);
    free(buf); fclose(f);
    if (bad) return -1;
    sha_final(&s, hex64);
    if (size_out) *size_out = total;
    return 0;
}

/* ------------------------------------------------------------------------------------------------ exe path */
static int exe_path(char* out, size_t cap)
{
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);   /* ANSI: the logger opens files with the ANSI API too */
    if (!n || n >= cap) return -1;
    return 0;
#else
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n <= 0) return -1;
    out[n] = 0;
    return 0;
#endif
}

int rc_log_exe_dir(char* out, size_t cap)
{
    if (exe_path(out, cap)) return -1;
    size_t n = strlen(out);
    while (n && out[n - 1] != '\\' && out[n - 1] != '/') n--;
    if (n > 1) out[n - 1] = 0; else return -1;
    return 0;
}

static const char* base_name(const char* p)
{
    const char* b = p;
    for (; *p; p++) if (*p == '\\' || *p == '/') b = p + 1;
    return b;
}

/* ------------------------------------------------------------------------------------------------ state */
static char             g_exe[520];
static uint64_t         g_t0_ms;
static atomic_int       g_exit_set;
static int              g_exit_code;
static char             g_exit_reason[160];
static atomic_int       g_hash_done = 1;      /* 1 = nothing running */

/* ------------------------------------------------------------------------------------------------ hash thread */
#ifdef _WIN32
static DWORD WINAPI hash_main(LPVOID u)
#else
static void* hash_main(void* u)
#endif
{
    (void)u;
    char hex[65]; unsigned long long sz = 0;
    uint64_t t = mono_ms();
    if (!rc_log_sha256_file(g_exe, hex, &sz))
        kz3_log(KZ3_LOG_INFO, TAG, "exe %s: sha256 %.16s (%llu bytes, hashed in %llu ms)", base_name(g_exe), hex, sz,
                (unsigned long long)(mono_ms() - t));
    else
        kz3_log(KZ3_LOG_WARN, TAG, "exe %s: sha256 unavailable (could not read the file)", base_name(g_exe));
    atomic_store(&g_hash_done, 1);
    return 0;
}

static void start_hash(void)
{
    atomic_store(&g_hash_done, 0);
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, hash_main, NULL, 0, NULL);
    if (h) { CloseHandle(h); return; }
#else
    pthread_t th;
    if (!pthread_create(&th, NULL, hash_main, NULL)) { pthread_detach(th); return; }
#endif
    hash_main(NULL);   /* no thread: hash here */
}

/* ------------------------------------------------------------------------------------------------ system info */
static void cpu_name(char* out, size_t cap)
{
    out[0] = 0;
#if defined(__x86_64__) || defined(__i386__)
    unsigned a[4] = { 0 };
    if (__get_cpuid(0x80000000u, &a[0], &a[1], &a[2], &a[3]) && a[0] >= 0x80000004u) {
        char b[49] = { 0 };
        for (unsigned i = 0; i < 3; i++) {
            unsigned r[4];
            __get_cpuid(0x80000002u + i, &r[0], &r[1], &r[2], &r[3]);
            memcpy(b + 16 * i, r, 16);
        }
        const char* s = b; while (*s == ' ') s++;
        snprintf(out, cap, "%s", s);
    }
#endif
#ifndef _WIN32
    if (!out[0]) {
        FILE* f = fopen("/proc/cpuinfo", "r"); char line[256];
        while (f && fgets(line, sizeof line, f))
            if (!strncmp(line, "model name", 10) && strchr(line, ':')) {
                const char* v = strchr(line, ':') + 1; while (*v == ' ') v++;
                snprintf(out, cap, "%s", v); size_t n = strlen(out);
                while (n && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = 0;
                break;
            }
        if (f) fclose(f);
    }
#endif
    if (!out[0]) snprintf(out, cap, "unknown");
}

static void log_system(void)
{
    char cpu[128]; cpu_name(cpu, sizeof cpu);
#ifdef _WIN32
    typedef LONG (WINAPI* rtl_ver_fn)(OSVERSIONINFOW*);
    typedef const char* (CDECL* wine_ver_fn)(void);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    rtl_ver_fn rv = nt ? (rtl_ver_fn)(void*)GetProcAddress(nt, "RtlGetVersion") : NULL;
    wine_ver_fn wv = nt ? (wine_ver_fn)(void*)GetProcAddress(nt, "wine_get_version") : NULL;
    OSVERSIONINFOW vi; memset(&vi, 0, sizeof vi); vi.dwOSVersionInfoSize = sizeof vi;
    if (rv && !rv(&vi))
        kz3_log(KZ3_LOG_INFO, TAG, "os: Windows %lu.%lu build %lu%s%s", (unsigned long)vi.dwMajorVersion,
                (unsigned long)vi.dwMinorVersion, (unsigned long)vi.dwBuildNumber, wv ? ", Wine/Proton " : "", wv ? wv() : "");
    else
        kz3_log(KZ3_LOG_INFO, TAG, "os: Windows (version unavailable)%s%s", wv ? ", Wine/Proton " : "", wv ? wv() : "");
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    MEMORYSTATUSEX ms; memset(&ms, 0, sizeof ms); ms.dwLength = sizeof ms; GlobalMemoryStatusEx(&ms);
    kz3_log(KZ3_LOG_INFO, TAG, "cpu: %s, %lu threads", cpu, (unsigned long)si.dwNumberOfProcessors);
    kz3_log(KZ3_LOG_INFO, TAG, "ram: %llu MB total, %llu MB free", (unsigned long long)(ms.ullTotalPhys >> 20),
            (unsigned long long)(ms.ullAvailPhys >> 20));
#else
    struct utsname u; if (uname(&u)) { strcpy(u.sysname, "Unix"); u.release[0] = 0; }
    kz3_log(KZ3_LOG_INFO, TAG, "os: %s %s (host test build, not Windows)", u.sysname, u.release);
    long np = sysconf(_SC_NPROCESSORS_ONLN), pg = sysconf(_SC_PHYS_PAGES), ps = sysconf(_SC_PAGESIZE);
    kz3_log(KZ3_LOG_INFO, TAG, "cpu: %s, %ld threads", cpu, np);
    kz3_log(KZ3_LOG_INFO, TAG, "ram: %llu MB total", (unsigned long long)((unsigned long long)pg * (unsigned long long)ps >> 20));
#endif
}

/* ------------------------------------------------------------------------------------------------ end block */
static void end_block(void)
{
    uint64_t ms = mono_ms() - g_t0_ms;
    for (int i = 0; i < 100 && !atomic_load(&g_hash_done); i++) sleep_ms(50);   /* the hash line first, 5 s at most */
    if (!atomic_load(&g_hash_done)) kz3_log(KZ3_LOG_WARN, TAG, "exe sha256 not finished at exit");
    if (atomic_load(&g_exit_set))
        kz3_log(KZ3_LOG_INFO, TAG, "end: exit code %d (%s), run time %llu.%03llu s", g_exit_code, g_exit_reason,
                (unsigned long long)(ms / 1000), (unsigned long long)(ms % 1000));
    else
        kz3_log(KZ3_LOG_INFO, TAG, "end: the runtime ended the process (guest sys_process_exit); exit status not recorded here, run time %llu.%03llu s",
                (unsigned long long)(ms / 1000), (unsigned long long)(ms % 1000));
}

void rc_log_exit(int code, const char* reason)
{
    int z = 0;
    if (atomic_compare_exchange_strong(&g_exit_set, &z, 1)) {
        g_exit_code = code;
        snprintf(g_exit_reason, sizeof g_exit_reason, "%s", reason ? reason : "");
    }
    kz3_log_shutdown();
}

/* ------------------------------------------------------------------------------------------------ open + header */
int rc_log_open(const rc_run_info* ri, unsigned flags)
{
    static char dir[520];
    g_t0_ms = mono_ms();
    kz3_log_set_game(ri && ri->game_id ? ri->game_id : "");
    const char* d = getenv("RC_LOG_DIR");
    if (d && *d) snprintf(dir, sizeof dir, "%s", d);
    else if (rc_log_exe_dir(dir, sizeof dir)) snprintf(dir, sizeof dir, ".");
    int rc = kz3_log_init(dir, flags);
    kz3_log_set_thread_label("main");
    kz3_log_set_end_hook(end_block);
    if (!ri) return rc;
    kz3_log(KZ3_LOG_INFO, TAG, "=== %s %s%s%s%s ===", ri->product ? ri->product : "Qwark's Awakening", ri->version ? ri->version : "?",
            ri->commit && *ri->commit ? " (commit " : "", ri->commit && *ri->commit ? ri->commit : "",
            ri->commit && *ri->commit ? ")" : "");
    kz3_log(KZ3_LOG_INFO, TAG, "game: %s, %s, title ID %s", ri->game_id ? ri->game_id : "?", ri->title ? ri->title : "?",
            ri->title_id ? ri->title_id : "?");
    kz3_log(KZ3_LOG_INFO, TAG, "update version expected: %s; game version: see the PARAM.SFO line once the boot code reads it",
            ri->update_version && *ri->update_version ? ri->update_version : "none");
    if (!exe_path(g_exe, sizeof g_exe)) start_hash();
    else kz3_log(KZ3_LOG_WARN, TAG, "exe path unavailable: no sha256");
    log_system();
    return rc;
}

void rc_log_note_env(void)
{
    static const char* const pre[] = { "RC_", "PS3_", "KZ3_", "RSX_", "GCM_", "LD_" };
#ifdef _WIN32
    char** env = _environ;
#else
    char** env = environ;
#endif
    int n = 0;
    for (; env && *env; env++) {
        const char* e = *env;
        int hit = 0;
        for (size_t i = 0; i < sizeof pre / sizeof pre[0]; i++) if (!strncmp(e, pre[i], strlen(pre[i]))) hit = 1;
        if (!hit || !strncmp(e, "LD_LIBRARY_PATH=", 16) || !strncmp(e, "LD_PRELOAD=", 11)) continue;
        if (!strncmp(e, "PS3_RIF_KEY=", 12)) { kz3_log(KZ3_LOG_INFO, TAG, "env PS3_RIF_KEY=<redacted>"); n++; continue; }   /* licence key */
        kz3_log(KZ3_LOG_INFO, TAG, "env %.300s", e);
        n++;
    }
    if (!n) kz3_log(KZ3_LOG_INFO, TAG, "env: no RC_/PS3_/KZ3_/RSX_/GCM_/LD_ variable set");
}

/* PARAM.SFO: "\0PSF", u32 version, u32 key table, u32 data table, u32 entries; entries of 16 bytes
 * (u16 key offset, u16 format, u32 length, u32 max length, u32 data offset), little endian. */
void rc_log_note_sfo(const char* path)
{
    FILE* f = path ? fopen(path, "rb") : NULL;
    if (!f) { kz3_log(KZ3_LOG_WARN, TAG, "PARAM.SFO unreadable: game version unknown"); return; }
    static unsigned char b[16384];
    size_t n = fread(b, 1, sizeof b - 1, f); fclose(f);
    b[n] = 0;
    #define U16(o) ((unsigned)b[o] | (unsigned)b[(o)+1] << 8)
    #define U32(o) ((uint32_t)b[o] | (uint32_t)b[(o)+1] << 8 | (uint32_t)b[(o)+2] << 16 | (uint32_t)b[(o)+3] << 24)
    if (n < 20 || memcmp(b, "\0PSF", 4)) { kz3_log(KZ3_LOG_WARN, TAG, "PARAM.SFO has no PSF header"); return; }
    uint32_t kt = U32(8), dt = U32(12), cnt = U32(16);
    static const char* const want[] = { "TITLE_ID", "VERSION", "APP_VER", "TITLE" };
    char out[512]; size_t o = 0; out[0] = 0;
    for (size_t w = 0; w < 4; w++) {
        for (uint32_t i = 0; i < cnt && 20 + 16 * (size_t)(i + 1) <= n; i++) {
            size_t e = 20 + 16 * (size_t)i;
            uint32_t ko = kt + U16(e), len = U32(e + 4), dof = dt + U32(e + 12);
            if (ko >= n || dof >= n || strcmp((const char*)b + ko, want[w])) continue;
            char val[160]; size_t vl = len < sizeof val - 1 ? len : sizeof val - 1;
            if (dof + vl > n) vl = n - dof;
            memcpy(val, b + dof, vl); val[vl] = 0;
            o += (size_t)snprintf(out + o, sizeof out - o, "%s%s=%s", o ? ", " : "", want[w], val);
            if (o >= sizeof out) o = sizeof out - 1;
            break;
        }
    }
    kz3_log(KZ3_LOG_INFO, TAG, "PARAM.SFO: %s", out[0] ? out : "(no known fields)");
}
