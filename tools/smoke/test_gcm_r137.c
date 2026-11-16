/* Host test for KZ3 patch 2147 (RSX pass 137): the [perf-flush] line with the ring-wrap counts (gcm_perf_flush_line).
 * Reuses the stubs of the toolkit's test_gcm_legacy_state_filter.c (its main is renamed away), no game data, no GPU.
 * Built by tools/smoke/test_gcm_r137.sh with -DLEGACY_TEST_C=<path of that file>. */
#ifdef _WIN32
#include <stdlib.h>
static int rc_test_setenv(const char* name, const char* value, int overwrite)
{
    return !overwrite && getenv(name) ? 0 : _putenv_s(name, value);
}
static int rc_test_unsetenv(const char* name) { return _putenv_s(name, ""); }
#define setenv rc_test_setenv
#define unsetenv rc_test_unsetenv
#endif
#define main legacy_state_filter_main
#include LEGACY_TEST_C
#undef main
#ifdef _WIN32
#undef setenv
#undef unsetenv
#endif

int g_kz3_wacct;
void (*g_kz3_wtrace)(const char*, int, uint32_t, uint64_t, uint64_t);
volatile u32 g_rsx_game_frame_marks;
const unsigned char* g_spu_coh_bits;
uint64_t kz3_wacct_now(void) { return 0; }
void kz3_wacct_note(int kind, uint32_t site, uint64_t ns) { (void)kind; (void)site; (void)ns; }
void kz3_wacct_trace(int kind, uint32_t site, uint64_t t0, uint64_t t1)
{ (void)kind; (void)site; (void)t0; (void)t1; }
void kz3_wacct_flip(void) {}
const char* ppu_thread_name_for_os_tid(unsigned long os_tid) { (void)os_tid; return ""; }
void rsx_draw_engine_set_display_buffer(u32 id, u32 location, u32 offset, u32 pitch, u32 width, u32 height)
{ (void)id; (void)location; (void)offset; (void)pitch; (void)width; (void)height; }
void rsx_live_draw_debug_cond_render_stats(u64 out[6]) { memset(out, 0, 6 * sizeof *out); }
void rsx_live_draw_debug_report_readback_stats(u64 out[6]) { memset(out, 0, 6 * sizeof *out); }
void rsx_live_draw_note_guest_write_pages(u32 location, u32 offset, u32 size)
{ (void)location; (void)offset; (void)size; }
void rsx_live_draw_note_spu_read(u32 location, u32 offset, u32 size)
{ (void)location; (void)offset; (void)size; }
void rsx_live_draw_perf_dump(void) {}
void rsx_live_draw_set_write_source(u32 source) { (void)source; }
unsigned long long rsx_perf_draws(void) { return 0; }
unsigned long long rsx_perf_fifo_flips(void) { return 0; }

static int has(const char* s, const char* sub) { return strstr(s, sub) != NULL; }

int main(void)
{
    long long d[RSX_PERF_COUNT];
    memset(d, 0, sizeof d);
    d[RSX_PERF_W_FLUSH_N0 + 2] = 3;    d[RSX_PERF_W_FLUSH_T0 + 2] = 3000;   /* vring */
    d[RSX_PERF_W_FLUSH_N0 + 9] = 5;    d[RSX_PERF_W_FLUSH_T0 + 9] = 1000;   /* descring */
    d[RSX_PERF_W_FLUSH_N0 + 11] = 2;   /* upring */
    d[RSX_PERF_W_WRAP_SRV] = 4; d[RSX_PERF_W_WRAP_SMP] = 1;
    char line[512];
    gcm_perf_flush_line(line, sizeof line, 160.0, d, 0.001);
    puts(line);
    check(strncmp(line, "[perf-flush] t=160s tex-up 0.0ms |", 34) == 0, "the line starts as before");
    check(has(line, " vring 3/3.0ms") && has(line, " descring 5/1.0ms"), "per-reason flush counts and ms are kept");
    check(has(line, " | wraps srv 4 smp 1 up 2 vtx 3 cb 0 pscb 0"), "wraps segment: srv/smp counters, upring, vring, cbring, pscbring");
    /* a zero period still prints the segment */
    memset(d, 0, sizeof d);
    gcm_perf_flush_line(line, sizeof line, 1.0, d, 1.0);
    check(has(line, " | wraps srv 0 smp 0 up 0 vtx 0 cb 0 pscb 0"), "an idle period prints zeros");
    /* a short buffer is cut, always NUL-terminated and never overrun */
    char small[80 + 8]; memset(small, 0x5A, sizeof small);
    d[RSX_PERF_W_WRAP_SRV] = 123456789;
    gcm_perf_flush_line(small, 80, 1.0, d, 1.0);
    check(strlen(small) < 80 && (unsigned char)small[80] == 0x5A && (unsigned char)small[87] == 0x5A, "a short buffer is not overrun");
    printf("[test_gcm_r137] %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
