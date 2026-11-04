/* Host test of the logger (port/common/log): driven by port/rc/tests/run_log_test.sh, one process per mode (the logger is
 * a per-process singleton and builds its redaction rules once). No game data. Modes:
 *   basic DIR   rc_log_open + header + env + PARAM.SFO + lines + rc_log_exit(7, ...)
 *   levels DIR  tag/level filtering (the script sets RC_LOG_LEVEL / RC_LOG)
 *   crash DIR   kz3_log_write_crash after some lines (the crash report text and the ring)
 *   noexit DIR  rc_log_open then kz3_log_shutdown() without rc_log_exit (the runtime's sys_process_exit path)
 *   sha FILE    prints the sha256 of FILE (rc_log_sha256_file) for the script to compare with sha256sum
 *   route       prints kz3_log_route() results for a few lines
 */
#include "rc_log_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_sfo(const char* path)
{
    /* a minimal PARAM.SFO: TITLE (utf8) "Test & Game", TITLE_ID "BCUS00000", VERSION "02.00", APP_VER "02.10" */
    const char* keys[4] = { "APP_VER", "TITLE", "TITLE_ID", "VERSION" };
    const char* vals[4] = { "02.10", "Test & Game", "BCUS00000", "02.00" };
    unsigned char b[1024]; memset(b, 0, sizeof b);
    unsigned ktab = 20 + 16 * 4, koff[4], k = 0;
    for (int i = 0; i < 4; i++) { koff[i] = k; strcpy((char*)b + ktab + k, keys[i]); k += (unsigned)strlen(keys[i]) + 1; }
    unsigned dtab = (ktab + k + 3) & ~3u, d = 0;
    memcpy(b, "\0PSF", 4); b[4] = 1; b[5] = 1;
    b[8] = (unsigned char)ktab; b[12] = (unsigned char)dtab; b[16] = 4;
    for (int i = 0; i < 4; i++) {
        unsigned char* e = b + 20 + 16 * i; unsigned l = (unsigned)strlen(vals[i]) + 1;
        e[0] = (unsigned char)koff[i]; e[2] = 0x04; e[3] = 0x02; e[4] = (unsigned char)l; e[8] = 64; e[12] = (unsigned char)d;
        strcpy((char*)b + dtab + d, vals[i]); d += 64;
    }
    FILE* f = fopen(path, "wb"); if (!f) exit(9);
    fwrite(b, 1, dtab + d, f); fclose(f);
}

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    const char* mode = argv[1];
    if (!strcmp(mode, "sha")) {
        char hex[65]; unsigned long long sz;
        if (argc < 3 || rc_log_sha256_file(argv[2], hex, &sz)) return 1;
        printf("%s %llu\n", hex, sz);
        return 0;
    }
    if (!strcmp(mode, "route")) {
        const char* l[] = { "[mfc-list] x", "[hle] unresolved", "[CRASH] boom", "[fps] 60", "plain text", "[spurs-pm] z" };
        for (size_t i = 0; i < sizeof l / sizeof l[0]; i++) {
            char tag[32]; int lv = kz3_log_route(l[i], strlen(l[i]), tag, sizeof tag);
            printf("%s -> %d tag='%s'\n", l[i], lv, tag);
        }
        return 0;
    }
    rc_run_info ri = { "Qwark's Awakening", "9.8.7", "abc123def", "tods", "Test Title: A & B", "BCUS98127", "01.20" };
    if (!strcmp(mode, "basic")) {
        char sfo[600]; snprintf(sfo, sizeof sfo, "%s/PARAM.SFO", argv[2]);
        write_sfo(sfo);
        int rc = rc_log_open(&ri, 0);
        if (rc) { fprintf(stderr, "rc_log_open returned %d\n", rc); return 3; }
        rc_log_note_env();
        rc_log_note_sfo(sfo);
        kz3_log(KZ3_LOG_INFO, "boot", "VFS root: /home/zq_fakeuser_77/games/tods");
        kz3_log(KZ3_LOG_INFO, "boot", "profile C:\\Users\\zq_fakeuser_77\\AppData\\Local\\x and Z:\\home\\zq_fakeuser_77\\y");
        kz3_log(KZ3_LOG_DEBUG, "boot", "debug line (hidden by default)");
        kz3_log_printf("[fps] 59.9 %s\n", "from printf");
        rc_log_exit(7, "test exit");
        return 0;
    }
    if (!strcmp(mode, "noexit")) {
        rc_log_open(&ri, 0);
        kz3_log_shutdown();
        return 0;
    }
    if (!strcmp(mode, "levels")) {
        rc_log_open(&ri, 0);
        const char* tags[] = { "foo", "bar", "baz" };
        for (int t = 0; t < 3; t++)
            for (int l = 0; l < 5; l++) kz3_log(l, tags[t], "lvl%d", l);
        rc_log_exit(0, "levels");
        return 0;
    }
    if (!strcmp(mode, "crash")) {
        rc_log_open(&ri, 0);
        for (int i = 0; i < 300; i++) kz3_log(KZ3_LOG_INFO, "ring", "line %d at /home/zq_fakeuser_77/x", i);
        const char* p = kz3_log_write_crash("unit test fault", "rip=0x1234 user /home/zq_fakeuser_77");
        printf("%s\n", p);
        return p && *p ? 0 : 1;
    }
    return 2;
}
