/* rc_trace_read.cpp -- diagnostic guest-memory access tracer (default off).
 *
 * RC_TRACE_READ=<ea>,<len>   (hex or decimal; the range must sit inside one 4 KB host page)
 * RC_TRACE_LIST=<head_ea>[,<off>,<len>]
 *                            a guest linked list of {u32 item, u32 next} cells starting at the u32 at head_ea; every item's
 *                            [item+off, item+off+len) is traced too (default off 0x14, len 0x10: the four axis words of
 *                            an input context, ToD's head for pad 0 is 0x10724D9C, the RE notes s.18). The list is
 *                            re-walked every 500 ms; up to 15 ranges in all.
 *
 * Finds which lifted functions read or write a few bytes of guest memory, e.g. the pad manager's right-stick floats
 * (the RE notes s.18) to locate ToD's follow-camera input. The host page of each range is made PAGE_GUARD; a
 * vectored handler records each access inside a range with its host RIP and three unwound callers, then single-steps
 * the faulting instruction (trap flag) and re-arms the guard. Accesses elsewhere on the page are re-armed silently.
 * A poll thread arms pages once the game has committed them, re-arms one if a protect call dropped the guard, and
 * prints each new access site ("[trace-read] new ...") plus a count summary every 30 s. RIPs map to func_XXXXXXXX with
 * `nm` on the exe (the image base is printed). Diagnostic only, never default on: every access to a traced page costs
 * two exceptions. */
#ifdef _WIN32
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t* vm_base;

namespace {

struct Site {
    volatile LONG64 key;     /* 0 = free, -1 = being filled */
    uintptr_t rip[4];        /* faulting RIP, then three callers */
    volatile LONG count;
    uint32_t ea;
    uint8_t  write;
    DWORD    tid;
    volatile LONG printed;
};

struct Range { uint32_t ea, len; uintptr_t page; };

constexpr int kSites = 4096, kRanges = 16;
Site      g_sites[kSites];
Range     g_ranges[kRanges];
volatile LONG g_nranges;
uint32_t  g_list_head, g_list_off = 0x14, g_list_len = 0x10;
DWORD     g_poll_tid;
volatile LONG g_dropped, g_hits, g_steps, g_polls;   /* guard hits on traced pages, re-arms by single step / by the poll */
thread_local uintptr_t t_rearm;                       /* page to re-arm after the single step, 0 = none */

void arm(uintptr_t page)
{
    DWORD old;
    VirtualProtect((void*)page, 0x1000, PAGE_READWRITE | PAGE_GUARD, &old);
}

bool traced_page(uintptr_t page)
{
    const LONG n = g_nranges;
    for (LONG i = 0; i < n; i++) if (g_ranges[i].page == page) return true;
    return false;
}

void unwind(const CONTEXT* in, uintptr_t out[4])
{
    CONTEXT c = *in;
    out[0] = c.Rip;
    for (int i = 1; i < 4; i++) {
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &base, NULL);
        if (!fn) {                               /* leaf: the return address is at [rsp] */
            if (!c.Rsp) { out[i] = 0; continue; }
            c.Rip = *(DWORD64*)c.Rsp; c.Rsp += 8;
        } else {
            PVOID hd = NULL; DWORD64 fr = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, fn, &c, &hd, &fr, NULL);
        }
        out[i] = c.Rip;
    }
}

void record(const EXCEPTION_RECORD* er, const CONTEXT* ctx, uint32_t ea)
{
    uintptr_t rip[4];
    unwind(ctx, rip);
    LONG64 key = (LONG64)((rip[0] * 0x9E3779B97F4A7C15ull ^ rip[1] * 31 ^ rip[2] ^ (uint64_t)ea << 17) & 0x7FFFFFFFFFFFFFFFull);
    if (key <= 0) key = 1;
    unsigned h = (unsigned)((uint64_t)key >> 7) % kSites;
    for (int n = 0; n < kSites; n++, h = (h + 1) % kSites) {
        Site& s = g_sites[h];
        LONG64 k = s.key;
        if (k == key) { InterlockedIncrement(&s.count); return; }
        if (k == 0) {
            if (InterlockedCompareExchange64(&s.key, -1, 0) != 0) { n--; continue; }   /* re-check this slot */
            for (int i = 0; i < 4; i++) s.rip[i] = rip[i];
            s.ea    = ea;
            s.write = (uint8_t)er->ExceptionInformation[0];
            s.tid   = GetCurrentThreadId();
            s.count = 1;
            MemoryBarrier();
            s.key = key;
            return;
        }
        if (k == -1) { n--; continue; }          /* being filled: wait for it */
    }
    InterlockedIncrement(&g_dropped);
}

LONG WINAPI veh(EXCEPTION_POINTERS* ep)
{
    const EXCEPTION_RECORD* er = ep->ExceptionRecord;
    if (er->ExceptionCode == STATUS_GUARD_PAGE_VIOLATION && er->NumberParameters >= 2) {
        const uintptr_t a = er->ExceptionInformation[1], page = a & ~(uintptr_t)0xFFF;
        if (!vm_base || !traced_page(page)) return EXCEPTION_CONTINUE_SEARCH;
        InterlockedIncrement(&g_hits);
        const uint32_t ea = (uint32_t)(a - (uintptr_t)vm_base);
        if (GetCurrentThreadId() != g_poll_tid) {
            const LONG n = g_nranges;
            for (LONG i = 0; i < n; i++)
                if (ea >= g_ranges[i].ea && ea - g_ranges[i].ea < g_ranges[i].len) { record(er, ep->ContextRecord, ea); break; }
        }
        ep->ContextRecord->EFlags |= 0x100;     /* step over the access, then re-arm */
        t_rearm = page;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (er->ExceptionCode == STATUS_SINGLE_STEP && t_rearm) {
        const uintptr_t page = t_rearm;
        t_rearm = 0;
        InterlockedIncrement(&g_steps);
        ep->ContextRecord->EFlags &= ~0x100u;
        arm(page);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

bool add_range(uint32_t ea, uint32_t len, const char* why)
{
    const uintptr_t lo = (uintptr_t)vm_base + ea;
    if (len == 0 || ((lo & 0xFFF) + len) > 0x1000) {
        printf("[trace-read] %s 0x%08X,%u: the range must be 1..n bytes inside one 4 KB page\n", why, ea, len);
        return false;
    }
    const LONG n = g_nranges;
    for (LONG i = 0; i < n; i++) if (g_ranges[i].ea == ea && g_ranges[i].len == len) return true;
    if (n >= kRanges) { printf("[trace-read] %s 0x%08X: range table full\n", why, ea); return false; }
    g_ranges[n] = Range{ea, len, lo & ~(uintptr_t)0xFFF};
    MemoryBarrier();
    g_nranges = n + 1;
    printf("[trace-read] range %ld: ea=0x%08X len=%u (%s)\n", (long)n, ea, len, why);
    return true;
}

/* Guest u32 (big-endian) read by the poll thread; its own guard hits are not recorded. 0 when not committed. */
uint32_t be32(uint32_t ea)
{
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(vm_base + ea, &mi, sizeof mi) || mi.State != MEM_COMMIT) return 0;
    uint32_t v;
    memcpy(&v, vm_base + ea, 4);
    return __builtin_bswap32(v);
}

void walk_list(void)
{
    uint32_t cell = be32(g_list_head);
    for (int n = 0; cell && n < 32; n++) {
        const uint32_t item = be32(cell), next = be32(cell + 4);
        if (item) {
            const uint32_t map = be32(item);
            const uint32_t axes = map ? be32(map + 0x40) : 0;   /* the context's axis channel bytes (ToD input map +0x40) */
            char why[96];
            snprintf(why, sizeof why, "list item 0x%08X map 0x%08X axes %08X", item, map, axes);
            const LONG before = g_nranges;
            add_range(item + g_list_off, g_list_len, why);
            (void)before;
        }
        cell = next;
    }
}

void print_site(const char* tag, const Site& s, uintptr_t img)
{
    printf("[trace-read] %s n=%ld ea=0x%08X %s tid=%lu rip=+0x%llX callers=+0x%llX +0x%llX +0x%llX\n", tag,
           (long)s.count, s.ea, s.write ? "W" : "R", (unsigned long)s.tid,
           (unsigned long long)(s.rip[0] - img), (unsigned long long)(s.rip[1] - img),
           (unsigned long long)(s.rip[2] - img), (unsigned long long)(s.rip[3] - img));
}

DWORD WINAPI poll(LPVOID)
{
    g_poll_tid = GetCurrentThreadId();
    const uintptr_t img = (uintptr_t)GetModuleHandleW(NULL);
    printf("[trace-read] armed, image base 0x%llX (rip=+offset from it)\n", (unsigned long long)img);
    DWORD t0 = GetTickCount(), last = t0, period = 500, walked = 0;
    for (;;) {
        Sleep(period);
        if (period > 1 && g_hits > 50 && g_steps == 0) {   /* the trap flag never came back */
            period = 1;
            printf("[trace-read] no single-step re-arm after %ld hits: re-arming from the poll every 1 ms\n", (long)g_hits);
        }
        DWORD now = GetTickCount();
        if (g_list_head && now - walked >= 500) { walked = now; walk_list(); }
        const LONG n = g_nranges;
        for (LONG i = 0; i < n; i++) {
            MEMORY_BASIC_INFORMATION mi;
            if (VirtualQuery((void*)g_ranges[i].page, &mi, sizeof mi) && mi.State == MEM_COMMIT && !(mi.Protect & PAGE_GUARD)) {
                arm(g_ranges[i].page); InterlockedIncrement(&g_polls);
            }
        }
        for (int i = 0; i < kSites; i++) {
            Site& s = g_sites[i];
            if (s.key > 0 && !s.printed) { s.printed = 1; print_site("new", s, img); }
        }
        if (now - last >= 30000) {
            last = now;
            printf("[trace-read] t=%lus summary: page hits %ld, re-armed by step %ld / by poll %ld, dropped %ld\n",
                   (unsigned long)((now - t0) / 1000), (long)g_hits, (long)g_steps, (long)g_polls, (long)g_dropped);
            for (int i = 0; i < kSites; i++)
                if (g_sites[i].key > 0) print_site("sum", g_sites[i], img);
        }
    }
    return 0;
}

}  // namespace

void rc_trace_read_start(void)
{
    const char* v = getenv("RC_TRACE_READ");
    const char* l = getenv("RC_TRACE_LIST");
    if ((!v || !*v) && (!l || !*l)) return;
    if (!vm_base) return;
    if (v && *v) {
        char* end = NULL;
        unsigned long ea = strtoul(v, &end, 0);
        unsigned long len = (end && *end == ',') ? strtoul(end + 1, NULL, 0) : 4;
        add_range((uint32_t)ea, (uint32_t)len, "RC_TRACE_READ");
    }
    if (l && *l) {
        char* end = NULL;
        g_list_head = (uint32_t)strtoul(l, &end, 0);
        if (end && *end == ',') {
            g_list_off = (uint32_t)strtoul(end + 1, &end, 0);
            if (end && *end == ',') g_list_len = (uint32_t)strtoul(end + 1, NULL, 0);
        }
        printf("[trace-read] RC_TRACE_LIST head 0x%08X, item range +0x%X len %u\n", g_list_head, g_list_off, g_list_len);
    }
    AddVectoredExceptionHandler(1, veh);
    CreateThread(NULL, 0, poll, NULL, 0, NULL);
}
#else
void rc_trace_read_start(void) {}
#endif
