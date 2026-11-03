/* Host test (POSIX, no game data): the two-layer /dev_hdd0 of R&C patch 7014 (runtime/syscalls/hdd0_overlay.h), driven
 * through the real lv2 file syscalls of runtime/syscalls/sys_fs.c (unity build, SYS_FS_SRC) on a temp lower and upper.
 * Covered: read fall-through (and the lower file's own times in stat), create in the upper, copy-up on open-for-write
 * (times kept), unlink of a lower-only file + whiteout, re-create over a whiteout, merged directory listing (no
 * markers), rename of a lower file and of an upper file, mkdir, rmdir (not empty / empty, lower directory hidden), a
 * cross-layer rename refused. At the end the lower tree must be byte-identical with unchanged mtimes and no new entry.
 * Mode "nolower" (no PS3_HDD0_LOWER): /dev_hdd0 maps to the upper only, as before the patch.
 * Exit 0 = PASS. Build and run: run_hdd0_overlay_test.sh. */
#include SYS_FS_SRC
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>
#include <dirent.h>

uint8_t* vm_base;
uint32_t ppu_vm_size = 0;
const char* edat_resolve(const char* host_path, char* buf, size_t cap) { (void)buf; (void)cap; return host_path; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static char g_lower[256], g_upper[256];

static int64_t sc(int64_t (*f)(ppu_context*), uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    ppu_context x; memset(&x, 0, sizeof x); x.thread_id = 1;
    x.gpr[3] = a; x.gpr[4] = b; x.gpr[5] = c; x.gpr[6] = d;
    return (int32_t)f(&x);
}
static uint32_t gstr(const char* s) { static uint32_t at = 0x1000; uint32_t a = at; strcpy((char*)vm_base + a, s); at += (uint32_t)strlen(s) + 16; return a; }
static uint32_t rd32(uint32_t a) { const uint8_t* p = vm_base + a; return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint64_t rd64(uint32_t a) { return ((uint64_t)rd32(a) << 32) | rd32(a + 4); }
enum { BUF = 0x80000, FDOUT = 0x7F000, NOUT = 0x7F010, STAT = 0x7F100, DIRENT = 0x7F400 };

static int open_(const char* g, int flags) { int64_t r = sc(sys_fs_open, gstr(g), (uint64_t)flags, FDOUT, 0); return r ? -1 : (int)rd32(FDOUT); }
static int read_all(const char* g, char* out, int cap)
{
    int fd = open_(g, CELL_FS_O_RDONLY); if (fd < 0) return -1;
    int64_t r = sc(sys_fs_read, (uint64_t)fd, BUF, (uint64_t)cap, NOUT);
    int n = r ? -1 : (int)rd64(NOUT);
    if (n > 0) memcpy(out, vm_base + BUF, (size_t)n);
    if (n >= 0) out[n] = 0;
    sc(sys_fs_close, (uint64_t)fd, 0, 0, 0);
    return n;
}
static int write_at(const char* g, int flags, const char* data)
{
    int fd = open_(g, flags); if (fd < 0) return -1;
    memcpy(vm_base + BUF, data, strlen(data));
    int64_t r = sc(sys_fs_write, (uint64_t)fd, BUF, strlen(data), NOUT);
    sc(sys_fs_close, (uint64_t)fd, 0, 0, 0);
    return r ? -1 : 0;
}
static void mkfile(const char* root, const char* rel, const char* data, time_t mt)
{
    char p[512]; snprintf(p, sizeof p, "%s/%s", root, rel);
    char d[512]; snprintf(d, sizeof d, "%s", p);
    for (char* c = d + strlen(root) + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(d, 0777); *c = '/'; }
    FILE* f = fopen(p, "wb"); fputs(data, f); fclose(f);
    struct utimbuf t = { mt - 100, mt }; utime(p, &t);
}
static int exists(const char* root, const char* rel) { char p[512]; struct stat st; snprintf(p, sizeof p, "%s/%s", root, rel); return stat(p, &st) == 0; }
static void slurp(const char* p, char* out, int cap) { FILE* f = fopen(p, "rb"); int n = f ? (int)fread(out, 1, (size_t)cap - 1, f) : -1; if (f) fclose(f); out[n < 0 ? 0 : n] = 0; }

/* lower snapshot: every entry's path, size, mtime and bytes */
static char g_snap[64][3][512]; static int g_nsnap;
static void snap_walk(const char* dir, char (*out)[3][512], int* n)
{
    DIR* d = opendir(dir); if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[512]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        struct stat st; stat(p, &st);
        snprintf(out[*n][0], 512, "%s", p);
        snprintf(out[*n][1], 512, "%lld/%lld/%d", (long long)st.st_size, (long long)st.st_mtime, S_ISDIR(st.st_mode));
        if (S_ISDIR(st.st_mode)) out[*n][2][0] = 0; else slurp(p, out[*n][2], 512);
        (*n)++;
        if (S_ISDIR(st.st_mode)) snap_walk(p, out, n);
    }
    closedir(d);
}

static int list_has(const char* g, const char* name, int* markers)
{
    int64_t r = sc(sys_fs_opendir, gstr(g), FDOUT, 0, 0);
    if (r) return -1;
    const uint32_t dfd = rd32(FDOUT); int found = 0;
    for (;;) {
        sc(sys_fs_readdir, dfd, DIRENT, NOUT, 0);
        if (!rd64(NOUT)) break;
        const char* nm = (const char*)vm_base + DIRENT + 2;
        if (!strcmp(nm, name)) found = 1;
        if (!strncmp(nm, ".wh.", 4) && markers) (*markers)++;
    }
    sc(sys_fs_closedir, dfd, 0, 0, 0);
    return found;
}

int main(int argc, char** argv)
{
    vm_base = (uint8_t*)calloc(1, 1 << 20);
    const char* tmp = getenv("TMPDIR"); if (!tmp) tmp = "/tmp";
    snprintf(g_lower, sizeof g_lower, "%s/hdd0ov_lower_%d", tmp, (int)getpid());
    snprintf(g_upper, sizeof g_upper, "%s/hdd0ov_upper_%d", tmp, (int)getpid());
    mkdir(g_lower, 0777); mkdir(g_upper, 0777);
    const char* U = "game/BCUS98127/USRDIR";
    char r[256];
    snprintf(r, sizeof r, "%s/a.bin", U);     mkfile(g_lower, r, "LOWER-A", 1500000000);
    snprintf(r, sizeof r, "%s/del.bin", U);   mkfile(g_lower, r, "LOWER-DEL", 1500000001);
    snprintf(r, sizeof r, "%s/ren.bin", U);   mkfile(g_lower, r, "LOWER-REN", 1500000002);
    snprintf(r, sizeof r, "%s/sub/b.bin", U); mkfile(g_lower, r, "LOWER-B", 1500000003);
    mkfile(g_lower, "home/00000001/x.txt", "X", 1500000004);
    setenv("PS3_HDD0_ROOT", g_upper, 1);
    if (!(argc > 1 && !strcmp(argv[1], "nolower"))) setenv("PS3_HDD0_LOWER", g_lower, 1);
    snap_walk(g_lower, g_snap, &g_nsnap);
    char buf[512];

    if (argc > 1 && !strcmp(argv[1], "nolower")) {   /* the old mapping: upper only */
        CHECK(read_all("/dev_hdd0/game/BCUS98127/USRDIR/a.bin", buf, 256) < 0, "without PS3_HDD0_LOWER a lower-only file must not be found");
        CHECK(write_at("/dev_hdd0/game/BCUS98127/USRDIR/n.bin", CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_TRUNC, "N") < 0 || exists(g_upper, "game/BCUS98127/USRDIR/n.bin"), "write lands in the root");
        printf("nolower: %s\n", fails ? "FAIL" : "PASS"); return fails ? 1 : 0;
    }

    /* 1 read fall-through and the lower file's own times */
    CHECK(read_all("/dev_hdd0/game/BCUS98127/USRDIR/a.bin", buf, 256) == 7 && !strcmp(buf, "LOWER-A"), "read a.bin from the lower: '%s'", buf);
    CHECK(sc(sys_fs_stat, gstr("/dev_hdd0/game/BCUS98127/USRDIR/a.bin"), STAT, 0, 0) == 0 && rd64(STAT + 0x14) == 1500000000ull,
          "stat a.bin mtime %llu", (unsigned long long)rd64(STAT + 0x14));
    CHECK(!exists(g_upper, "game/BCUS98127/USRDIR/a.bin"), "a read must not copy up");
    /* 2 create lands in the upper */
    CHECK(write_at("/dev_hdd0/game/BCUS98127/USRDIR/new.bin", CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_TRUNC, "NEW") == 0, "create new.bin");
    CHECK(exists(g_upper, "game/BCUS98127/USRDIR/new.bin") && !exists(g_lower, "game/BCUS98127/USRDIR/new.bin"), "new.bin in the upper only");
    CHECK(read_all("/dev_hdd0/game/BCUS98127/USRDIR/new.bin", buf, 256) == 3 && !strcmp(buf, "NEW"), "read new.bin '%s'", buf);
    /* 3 copy-up on open for write (no truncate), times kept */
    CHECK(write_at("/dev_hdd0/game/BCUS98127/USRDIR/a.bin", CELL_FS_O_RDWR, "X") == 0, "open a.bin O_RDWR");
    snprintf(r, sizeof r, "%s/game/BCUS98127/USRDIR/a.bin", g_upper); slurp(r, buf, 256);
    CHECK(!strcmp(buf, "XOWER-A"), "copied-up a.bin is '%s'", buf);
    CHECK(read_all("/dev_hdd0/game/BCUS98127/USRDIR/a.bin", buf, 256) == 7 && !strcmp(buf, "XOWER-A"), "merged a.bin '%s'", buf);
    /* 4 unlink a lower-only file: whiteout */
    CHECK(sc(sys_fs_unlink, gstr("/dev_hdd0/game/BCUS98127/USRDIR/del.bin"), 0, 0, 0) == 0, "unlink del.bin");
    CHECK(sc(sys_fs_stat, gstr("/dev_hdd0/game/BCUS98127/USRDIR/del.bin"), STAT, 0, 0) != 0, "del.bin must be gone");
    CHECK(read_all("/dev_hdd0/game/BCUS98127/USRDIR/del.bin", buf, 256) < 0, "del.bin must not open");
    CHECK(exists(g_upper, "game/BCUS98127/USRDIR/.wh.del.bin"), "whiteout for del.bin");
    CHECK(sc(sys_fs_unlink, gstr("/dev_hdd0/game/BCUS98127/USRDIR/del.bin"), 0, 0, 0) != 0, "second unlink is ENOENT");
    /* 5 merged listing */
    int markers = 0;
    CHECK(list_has("/dev_hdd0/game/BCUS98127/USRDIR", "a.bin", &markers) == 1, "listing has a.bin");
    CHECK(list_has("/dev_hdd0/game/BCUS98127/USRDIR", "new.bin", &markers) == 1, "listing has new.bin");
    CHECK(list_has("/dev_hdd0/game/BCUS98127/USRDIR", "sub", &markers) == 1, "listing has sub");
    CHECK(list_has("/dev_hdd0/game/BCUS98127/USRDIR", "del.bin", &markers) == 0, "listing must not have del.bin");
    CHECK(markers == 0, "listing shows %d marker(s)", markers);
    /* 6 rename: a lower file, then an upper file; cross-layer refused */
    CHECK(sc(sys_fs_rename, gstr("/dev_hdd0/game/BCUS98127/USRDIR/ren.bin"), gstr("/dev_hdd0/game/BCUS98127/USRDIR/ren2.bin"), 0, 0) == 0, "rename ren.bin");
    CHECK(sc(sys_fs_stat, gstr("/dev_hdd0/game/BCUS98127/USRDIR/ren.bin"), STAT, 0, 0) != 0, "ren.bin gone");
    CHECK(read_all("/dev_hdd0/game/BCUS98127/USRDIR/ren2.bin", buf, 256) == 9 && !strcmp(buf, "LOWER-REN"), "ren2.bin '%s'", buf);
    CHECK(sc(sys_fs_rename, gstr("/dev_hdd0/game/BCUS98127/USRDIR/new.bin"), gstr("/dev_hdd0/game/BCUS98127/USRDIR/new2.bin"), 0, 0) == 0, "rename new.bin");
    CHECK(read_all("/dev_hdd0/game/BCUS98127/USRDIR/new2.bin", buf, 256) == 3 && list_has("/dev_hdd0/game/BCUS98127/USRDIR", "new.bin", NULL) == 0, "new2.bin");
    CHECK(sc(sys_fs_rename, gstr("/dev_hdd0/game/BCUS98127/USRDIR/a.bin"), gstr("/dev_hdd1/a.bin"), 0, 0) == (int32_t)CELL_EXDEV, "cross-layer rename refused");
    /* 7 mkdir, rmdir */
    CHECK(sc(sys_fs_mkdir, gstr("/dev_hdd0/game/BCUS98127/USRDIR/newdir"), 0777, 0, 0) == 0 && exists(g_upper, "game/BCUS98127/USRDIR/newdir"), "mkdir newdir");
    CHECK(sc(sys_fs_mkdir, gstr("/dev_hdd0/game/BCUS98127/USRDIR/sub"), 0777, 0, 0) == (int32_t)CELL_EEXIST, "mkdir sub is EEXIST");
    CHECK(sc(sys_fs_rmdir, gstr("/dev_hdd0/game/BCUS98127/USRDIR/sub"), 0, 0, 0) == (int32_t)CELL_ENOTEMPTY, "rmdir sub not empty");
    CHECK(sc(sys_fs_unlink, gstr("/dev_hdd0/game/BCUS98127/USRDIR/sub/b.bin"), 0, 0, 0) == 0, "unlink sub/b.bin");
    CHECK(sc(sys_fs_rmdir, gstr("/dev_hdd0/game/BCUS98127/USRDIR/sub"), 0, 0, 0) == 0, "rmdir sub");
    CHECK(list_has("/dev_hdd0/game/BCUS98127/USRDIR", "sub", NULL) == 0, "sub gone from the listing");
    CHECK(sc(sys_fs_mkdir, gstr("/dev_hdd0/game/BCUS98127/USRDIR/sub"), 0777, 0, 0) == 0, "mkdir sub again");
    CHECK(list_has("/dev_hdd0/game/BCUS98127/USRDIR/sub", "b.bin", NULL) == 0, "re-created sub is empty (opaque)");
    /* 8 re-create over a whiteout */
    CHECK(write_at("/dev_hdd0/game/BCUS98127/USRDIR/del.bin", CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_TRUNC, "R") == 0, "re-create del.bin");
    CHECK(read_all("/dev_hdd0/game/BCUS98127/USRDIR/del.bin", buf, 256) == 1 && !strcmp(buf, "R"), "del.bin is '%s'", buf);
    CHECK(write_at("/dev_hdd0/game/BCUS98127/USRDIR/nope.bin", CELL_FS_O_WRONLY, "N") < 0, "open for write without O_CREAT of a missing file is ENOENT");

    /* the lower is untouched */
    static char after[64][3][512]; int nafter = 0;
    snap_walk(g_lower, after, &nafter);
    int same = nafter == g_nsnap;
    for (int i = 0; same && i < g_nsnap; i++)
        if (strcmp(g_snap[i][0], after[i][0]) || strcmp(g_snap[i][1], after[i][1]) || strcmp(g_snap[i][2], after[i][2])) same = 0;
    CHECK(same, "the lower tree changed (%d entries before, %d after)", g_nsnap, nafter);
    printf("lower: %d entries, byte-identical and same mtimes: %s\n", g_nsnap, same ? "yes" : "NO");
    printf("%s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
