/* Host test of port/rc/rc_cpu_check.c: the decision function with injected CPUID/XCR0 values. */
#include <stdio.h>
#include <string.h>
#include "rc_cpu_check.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL: %s\n", #c); fails++; } } while (0)
int main(void)
{
    const unsigned int all = RC_CPU_SSE41 | RC_CPU_SSE42 | RC_CPU_POPCNT | RC_CPU_AVX | RC_CPU_AVX2 | RC_CPU_FMA |
                             RC_CPU_BMI1 | RC_CPU_BMI2 | RC_CPU_F16C;
    /* leaf 1 ecx: SSE4.1 19, SSE4.2 20, POPCNT 23, OSXSAVE 27, AVX 28, FMA 12, F16C 29; leaf 7 ebx: BMI1 3, AVX2 5, BMI2 8 */
    const unsigned int c1 = (1u << 19) | (1u << 20) | (1u << 23) | (1u << 27) | (1u << 28) | (1u << 12) | (1u << 29);
    const unsigned int b7 = (1u << 3) | (1u << 5) | (1u << 8);
    rc_cpu_id id = { 7, c1, b7, 7 };
    char msg[320], list[96];

    CHECK(rc_cpu_missing(all, &id) == 0);                 /* everything present */
    CHECK(rc_cpu_missing(0, &id) == 0);                   /* baseline build requires nothing */
    { rc_cpu_id z = { 0, 0, 0, 0 }; CHECK(rc_cpu_missing(0, &z) == 0); }

    id.leaf1_ecx = c1 & ~(1u << 19);                      /* one missing: SSE4.1 */
    CHECK(rc_cpu_missing(all, &id) == RC_CPU_SSE41);
    CHECK(rc_cpu_missing(RC_CPU_AVX2, &id) == 0);         /* not required, not reported */
    id.leaf1_ecx = c1;

    id.xcr0 = 0;                                          /* AVX bits set but OS did not enable XSAVE state */
    CHECK(rc_cpu_missing(all, &id) == (RC_CPU_AVX | RC_CPU_AVX2 | RC_CPU_FMA | RC_CPU_F16C));
    id.xcr0 = 3;                                          /* XMM but no YMM state */
    CHECK(rc_cpu_missing(RC_CPU_AVX, &id) == RC_CPU_AVX);
    id.xcr0 = 7;
    id.leaf1_ecx = c1 & ~(1u << 27);                      /* OSXSAVE clear */
    CHECK(rc_cpu_missing(RC_CPU_AVX, &id) == RC_CPU_AVX);
    id.leaf1_ecx = c1;

    id.max_leaf = 1;                                      /* no leaf 7: AVX2/BMI are absent whatever ebx holds */
    CHECK(rc_cpu_missing(all, &id) == (RC_CPU_AVX2 | RC_CPU_BMI1 | RC_CPU_BMI2));

    rc_cpu_list(RC_CPU_SSE41 | RC_CPU_AVX2, list, sizeof list);
    CHECK(strcmp(list, "SSE4.1, AVX2") == 0);
    rc_cpu_message(RC_CPU_SSE41 | RC_CPU_AVX2, RC_CPU_AVX2, msg, sizeof msg);
    CHECK(strcmp(msg, "This build needs a processor with SSE4.1, AVX2. This computer's processor does not have AVX2.") == 0);
    rc_cpu_list(all, list, 8);                            /* truncation stays terminated */
    CHECK(strlen(list) < 8);
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails != 0;
}
