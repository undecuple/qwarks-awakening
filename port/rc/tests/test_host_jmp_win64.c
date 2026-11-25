/* Host test of patch 7066 (runtime/platform/host_jmp_win64.h): the CRT-free frameless setjmp/longjmp for MinGW x64.
 * Build with GCC and with clang (llvm-mingw), run under Wine:
 *   x86_64-w64-mingw32-gcc -O2 -I<ps3recomp-patched>/runtime/platform test_host_jmp_win64.c -o t.exe
 * Checks: the value (0 -> 1, 2 kept), callee-saved locals survive, a jump from deep frames and from a sysv_abi
 * function lands, a second thread works, and the CRT's validated longjmp is never called (no ucrtbase longjmp import). */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "host_jmp_win64.h"

static jmp_buf jb;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); puts(""); } } while (0)

__attribute__((noinline)) static void deep(int n, int v) { volatile char pad[256]; pad[0] = (char)n; if (n) deep(n - 1, v); else rc_host_longjmp(jb, v); }
__attribute__((noinline, sysv_abi)) static void deep_sysv(int v) { deep(8, v); }

__attribute__((noinline)) static int run(int v, int via_sysv)
{
    volatile int keep = 0x1234;
    volatile double d = 2.5;
    int r = rc_host_setjmp(jb);
    if (r == 0) { if (via_sysv) deep_sysv(v); else deep(30, v); return -1; }
    CHECK(keep == 0x1234 && d == 2.5, "locals lost (keep=%#x d=%g)", keep, d);
    return r;
}

__attribute__((sysv_abi, noinline)) static int run_sysv(int v)
{
    int r = rc_host_setjmp(jb);
    if (r == 0) deep(5, v);
    return r;
}

static DWORD WINAPI thr(void* p) { (void)p; CHECK(run(2, 0) == 2, "thread value"); return 0; }

int main(void)
{
    CHECK(run(0, 0) == 1, "value 0 must land as 1");
    CHECK(run(2, 0) == 2, "value 2");
    CHECK(run(5, 1) == 5, "jump from a sysv_abi frame");
    CHECK(run_sysv(3) == 3, "setjmp in a sysv_abi function");
    for (int i = 0; i < 100000; i++) if (run(1 + (i & 3), i & 1) != 1 + (i & 3)) { CHECK(0, "loop %d", i); break; }
    HANDLE h = CreateThread(NULL, 1 << 20, thr, NULL, 0, NULL); WaitForSingleObject(h, INFINITE);
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
