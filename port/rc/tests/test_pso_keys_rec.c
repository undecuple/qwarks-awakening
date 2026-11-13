/* Host test for R&C patch 7051 (libs/video/rsx_pso_keys_rec.h): PSO preload key file records.
 * 1. Flags: a GPU-fetch build (packed 2) is recorded with LD_PL_FLAG_VGPU and reads back as mode 2; mode 0/1 as before.
 * 2. Kill midway: a child appends records with ld_pl_append and reports each one through a pipe; the parent SIGKILLs it
 *    after K reports and reads the file: header valid, at least K whole records, in order, nothing torn.
 * 3. Negative control: the same kill on a writer that keeps one buffered FILE open (the "write at shutdown" shape)
 *    must lose records, or the check in 2 would not catch a writer that buffers.
 * No game data. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>
#include "rsx_pso_keys_rec.h"

typedef struct { uint64_t key; uint32_t flags; uint8_t pad[212]; uint64_t check; } rec_t;   /* 232 bytes like ld_pl_rec */
#define RS_SIZE 120u
#define CACHE_V 7u
#define ID 0x1234567890ABCDEFull

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static rec_t make(uint32_t i)
{
    rec_t r; memset(&r, 0, sizeof r);
    r.key = 0x9E3779B97F4A7C15ull * (i + 1u);
    r.flags = ld_pl_rec_flags(1, (int)(i % 3u), (int)(i & 1u));
    memset(r.pad, (int)(i & 0xFFu), sizeof r.pad);
    r.check = r.key ^ 0xA5A5A5A5A5A5A5A5ull;
    return r;
}

/* Writer child: append N records, write one byte to the pipe after each. buffered=1: one FILE kept open, no flush. */
static void writer(const char* path, int fd, uint32_t n, int buffered)
{
    FILE* bf = NULL;
    if (buffered) {
        bf = fopen(path, "ab");
        const ld_pl_hdr h = ld_pl_hdr_make((uint32_t)sizeof(rec_t), RS_SIZE, CACHE_V, ID);
        fwrite(&h, sizeof h, 1, bf);
    }
    for (uint32_t i = 0; i < n; i++) {
        const rec_t r = make(i);
        if (buffered) fwrite(&r, sizeof r, 1, bf);
        else if (!ld_pl_append(path, &r, (uint32_t)sizeof r, RS_SIZE, CACHE_V, ID)) _exit(3);
        const char b = 1;
        if (write(fd, &b, 1) != 1) _exit(4);
        { const struct timespec ts = { 0, 2000000L }; nanosleep(&ts, NULL); }   /* a session builds PSOs over time; the kill lands between and during appends */
    }
    for (;;) pause();   /* never exits on its own: only the kill ends it (no atexit flush) */
}

/* Returns the number of whole records that check out, -1 on a bad header; *torn = trailing partial bytes. */
static long read_back(const char* path, long* torn)
{
    FILE* f = fopen(path, "rb");
    *torn = 0;
    if (!f) return 0;
    ld_pl_hdr h;
    if (fread(&h, sizeof h, 1, f) != 1) { fclose(f); return 0; }
    if (ld_pl_hdr_check(&h, (uint32_t)sizeof(rec_t), RS_SIZE, CACHE_V, ID) != LD_PL_HDR_OK) { fclose(f); return -1; }
    long n = 0;
    rec_t r;
    size_t got;
    while ((got = fread(&r, 1, sizeof r, f)) == sizeof r) {
        const rec_t e = make((uint32_t)n);
        if (memcmp(&r, &e, sizeof r) != 0) break;
        n++;
    }
    *torn = (long)got;
    fclose(f);
    return n;
}

/* Kill the writer after k reports; returns the whole records found. */
static long kill_after(const char* path, uint32_t k, int buffered, long* torn)
{
    unlink(path);
    int p[2];
    if (pipe(p) != 0) { perror("pipe"); exit(2); }
    const pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid == 0) { close(p[0]); writer(path, p[1], 100000u, buffered); }
    close(p[1]);
    for (uint32_t i = 0; i < k; i++) { char b; if (read(p[0], &b, 1) != 1) break; }
    kill(pid, SIGKILL);
    int st = 0;
    waitpid(pid, &st, 0);
    close(p[0]);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL, "writer did not die by SIGKILL (status %d)", st);
    return read_back(path, torn);
}

int main(void)
{
    /* 1. flags */
    CHECK(ld_pl_rec_packed_mode(ld_pl_rec_flags(1, 2, 0)) == 2, "GPU-fetch build does not read back as mode 2");
    CHECK(ld_pl_rec_flags(1, 2, 0) & LD_PL_FLAG_VGPU, "GPU-fetch build lacks LD_PL_FLAG_VGPU");
    CHECK(ld_pl_rec_packed_mode(ld_pl_rec_flags(1, 1, 1)) == 1, "packed build not mode 1");
    CHECK(ld_pl_rec_packed_mode(ld_pl_rec_flags(1, 0, 1)) == 0, "float4 build not mode 0");
    CHECK(ld_pl_rec_packed_mode(ld_pl_rec_flags(0, 2, 0)) == 0, "no layout must be mode 0");
    CHECK(ld_pl_rec_flags(1, 1, 1) == (LD_PL_FLAG_LAYOUT | LD_PL_FLAG_PACKED | LD_PL_FLAG_DEPTH), "mode 1 flags changed");
    CHECK(ld_pl_rec_flags(1, 0, 0) == LD_PL_FLAG_LAYOUT, "mode 0 flags changed");

    char path[256];
    const char* td = getenv("TMPDIR");
    snprintf(path, sizeof path, "%s/test_pso_keys_rec_%d.bin", td && *td ? td : "/tmp", (int)getpid());

    /* 2. per-key append survives a kill at several points */
    const uint32_t ks[] = { 1u, 7u, 50u, 333u };
    for (unsigned t = 0; t < sizeof ks / sizeof ks[0]; t++) {
        long torn = 0;
        const long n = kill_after(path, ks[t], 0, &torn);
        CHECK(n >= (long)ks[t], "append writer killed after %u keys: %ld whole records on disk", ks[t], n);
        CHECK(n <= (long)ks[t] + 1, "append writer: %ld records after %u reports (at most one more expected)", n, ks[t]);
        CHECK(torn == 0 || n == (long)ks[t], "torn tail of %ld bytes before the last key", torn);
        printf("append writer killed after %u keys: %ld records, %ld torn bytes\n", ks[t], n, torn);
    }

    /* 3. negative control: a buffered writer loses keys on the same kill */
    {
        long torn = 0;
        const long n = kill_after(path, 7u, 1, &torn);
        CHECK(n < 7, "negative control: the buffered writer kept %ld of 7 records, the kill test cannot see loss", n);
        printf("negative control (buffered writer) killed after 7 keys: %ld records (loss expected)\n", n < 0 ? 0 : n);
    }
    unlink(path);
    if (fails) { printf("%d FAILED\n", fails); return 1; }
    printf("test_pso_keys_rec: all checks pass\n");
    return 0;
}
