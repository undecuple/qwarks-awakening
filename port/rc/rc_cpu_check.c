/* Startup CPU-feature check. The build may be compiled with -msse4.1/-mavx2/... (the macros below); on a CPU without
 * one of them the first such instruction dies silently (the other ToD port's issues 7 and 15). rc_cpu_check() runs at
 * the top of main(): it checks with CPUID (and XGETBV for the AVX class, since the OS must save the YMM state too)
 * exactly the features this build requires, says in plain words which are missing, and exits with code 3.
 * A build that needs nothing beyond x86-64 baseline requires nothing and the check does nothing.
 * The decision (rc_cpu_missing) is pure so the host test can inject CPUID values. */
#include "rc_cpu_check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#define RC_CPU_X86 1
#endif

static const char *const k_names[RC_CPU_COUNT] = { "SSE4.1", "SSE4.2", "POPCNT", "AVX", "AVX2", "FMA", "BMI1", "BMI2", "F16C" };

unsigned int rc_cpu_required(void)
{
    unsigned int r = 0;
#if defined(__SSE4_1__) || defined(RC_CPU_REQUIRE_SSE41)   /* the define: the lifted SPU TUs only (RC_SPU_SSE41) */
    r |= RC_CPU_SSE41;
#endif
#ifdef __SSE4_2__
    r |= RC_CPU_SSE42;
#endif
#ifdef __POPCNT__
    r |= RC_CPU_POPCNT;
#endif
#ifdef __AVX__
    r |= RC_CPU_AVX;
#endif
#ifdef __AVX2__
    r |= RC_CPU_AVX2;
#endif
#ifdef __FMA__
    r |= RC_CPU_FMA;
#endif
#ifdef __BMI__
    r |= RC_CPU_BMI1;
#endif
#ifdef __BMI2__
    r |= RC_CPU_BMI2;
#endif
#ifdef __F16C__
    r |= RC_CPU_F16C;
#endif
    return r;
}

unsigned int rc_cpu_missing(unsigned int req, const rc_cpu_id *id)
{
    unsigned int have = 0, c = id->leaf1_ecx, b = id->leaf7_ebx;
    /* AVX-class features need the CPU bit, OSXSAVE (ecx 27) and XCR0 bits 1+2 (SSE and YMM state) */
    int os_avx = ((c >> 27) & 1u) && (id->xcr0 & 6u) == 6u;
    int leaf7 = id->max_leaf >= 7;
    if ((c >> 19) & 1u) have |= RC_CPU_SSE41;
    if ((c >> 20) & 1u) have |= RC_CPU_SSE42;
    if ((c >> 23) & 1u) have |= RC_CPU_POPCNT;
    if (os_avx && ((c >> 28) & 1u)) have |= RC_CPU_AVX;
    if (os_avx && ((c >> 12) & 1u)) have |= RC_CPU_FMA;
    if (os_avx && ((c >> 29) & 1u)) have |= RC_CPU_F16C;
    if (os_avx && leaf7 && ((b >> 5) & 1u)) have |= RC_CPU_AVX2;
    if (leaf7 && ((b >> 3) & 1u)) have |= RC_CPU_BMI1;
    if (leaf7 && ((b >> 8) & 1u)) have |= RC_CPU_BMI2;
    return req & ~have;
}

void rc_cpu_list(unsigned int mask, char *out, unsigned int cap)
{
    unsigned int i, n = 0;
    if (!cap) return;
    out[0] = 0;
    for (i = 0; i < RC_CPU_COUNT; i++) {
        if (!(mask & (1u << i))) continue;
        n += (unsigned int)snprintf(out + n, n < cap ? cap - n : 0, "%s%s", n ? ", " : "", k_names[i]);
        if (n >= cap) return;
    }
}

void rc_cpu_message(unsigned int req, unsigned int missing, char *out, unsigned int cap)
{
    char need[96], lack[96];
    rc_cpu_list(req, need, sizeof need);
    rc_cpu_list(missing, lack, sizeof lack);
    snprintf(out, cap, "This build needs a processor with %s. This computer's processor does not have %s.", need, lack);
}

#ifdef RC_CPU_X86
static unsigned int read_xcr0(unsigned int osxsave)
{
    unsigned int lo = 0, hi = 0;
    if (osxsave) __asm__ volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(lo), "=d"(hi) : "c"(0));   /* xgetbv, ecx = 0 */
    return lo;
}
#endif

void rc_cpu_check(void)
{
    unsigned int req = rc_cpu_required();
    if (!req) return;
#ifdef RC_CPU_X86
    {
        rc_cpu_id id = { 0, 0, 0, 0 };
        unsigned int a = 0, b = 0, c = 0, d = 0, miss;
        char msg[320];
        id.max_leaf = __get_cpuid_max(0, NULL);
        if (id.max_leaf >= 1) { __cpuid(1, a, b, c, d); id.leaf1_ecx = c; }
        if (id.max_leaf >= 7) { __cpuid_count(7, 0, a, b, c, d); id.leaf7_ebx = b; }
        id.xcr0 = read_xcr0((id.leaf1_ecx >> 27) & 1u);
        miss = rc_cpu_missing(req, &id);
        if (!miss) return;
        rc_cpu_message(req, miss, msg, sizeof msg);
        fprintf(stderr, "[rc] %s\n", msg);
#if defined(_WIN32)
        {
            char path[MAX_PATH] = "Ratchet & Clank", *p;
            if (GetModuleFileNameA(NULL, path, sizeof path)) {
                p = strrchr(path, '\\');
                if (p) memmove(path, p + 1, strlen(p));
            }
            MessageBoxA(NULL, msg, path, MB_OK | MB_ICONERROR | MB_TOPMOST);
        }
#endif
        exit(3);
    }
#endif
}
