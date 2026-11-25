/* Startup CPU-feature check (port/rc/rc_cpu_check.c): refuses to run on a CPU that lacks an x86 feature this build was
 * compiled to require, instead of dying silently on the first such instruction. */
#ifndef RC_CPU_CHECK_H
#define RC_CPU_CHECK_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    RC_CPU_SSE41 = 1u << 0, RC_CPU_SSE42 = 1u << 1, RC_CPU_POPCNT = 1u << 2, RC_CPU_AVX = 1u << 3,
    RC_CPU_AVX2 = 1u << 4, RC_CPU_FMA = 1u << 5, RC_CPU_BMI1 = 1u << 6, RC_CPU_BMI2 = 1u << 7,
    RC_CPU_F16C = 1u << 8,
    RC_CPU_COUNT = 9
};

/* What the CPU reports: cpuid leaf 1 ecx, leaf 7 (subleaf 0) ebx, the highest basic leaf, and XCR0 (0 when the OS
 * does not enable XSAVE). Filled by the real CPU at startup; the host test injects values. */
typedef struct rc_cpu_id {
    unsigned int max_leaf, leaf1_ecx, leaf7_ebx, xcr0;
} rc_cpu_id;

unsigned int rc_cpu_required(void);                                     /* mask from this build's compile-time macros */
unsigned int rc_cpu_missing(unsigned int required, const rc_cpu_id *id); /* the required features the CPU lacks */
void rc_cpu_list(unsigned int mask, char *out, unsigned int cap);        /* "SSE4.1, AVX2" */
void rc_cpu_message(unsigned int required, unsigned int missing, char *out, unsigned int cap);
void rc_cpu_check(void);   /* call first thing in main(): returns, or reports and exit(3) */

#ifdef __cplusplus
}
#endif
#endif
