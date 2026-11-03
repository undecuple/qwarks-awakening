/* Host test (POSIX, no game data): the R&C patch 7014 overlay through the cellFs HLE (runtime/ppu/ppu_fs.cpp, unity build
 * FS_SRC), the path ToD's own /dev_hdd0 reads take (global_cached.psarc on hdd0). Lower-only file: cellFsOpen read-only
 * and cellFsStat answer from the lower (its mtime); cellFsOpen O_RDWR copies it up; cellFsOpen O_CREAT creates in the
 * upper; cellFsMkdir creates in the upper; cellFsOpendir + cellFsReaddir and cellFsGetDirectoryEntries list both layers
 * once each. The lower stays byte-identical. Exit 0 = PASS. */
#include FS_SRC
#include <utime.h>
#include <sys/stat.h>

extern "C" {
uint8_t* vm_base;
uint32_t ppu_vm_size = 0;
ps3_guest_caller_fn g_ps3_guest_caller;
void ppu_guest_caller(char* out, size_t n) { snprintf(out, n, "test"); }
void ps3_hle_register_ctx(uint32_t, const char*, void (*)(ppu_context*)) {}
void ps3_vfs_ps3game_fallback(char*, size_t) {}
void ydkj_host_bt(const char*) {}
void vm_write32(uint64_t a, uint32_t v) { v = __builtin_bswap32(v); memcpy(vm_base + (uint32_t)a, &v, 4); }
void vm_write64(uint64_t a, uint64_t v) { v = __builtin_bswap64(v); memcpy(vm_base + (uint32_t)a, &v, 8); }
}
const char* edat_resolve(const char* host_path, char*, size_t) { return host_path; }
static uint64_t rd64(uint32_t a) { uint64_t v; memcpy(&v, vm_base + a, 8); return __builtin_bswap64(v); }
static uint32_t rd32(uint32_t a) { uint32_t v; memcpy(&v, vm_base + a, 4); return __builtin_bswap32(v); }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static void put(const char* s) { strcpy((char*)vm_base + 0x100, s); }
static void mk(const char* p, const char* d, time_t mt) { FILE* f = fopen(p, "wb"); fputs(d, f); fclose(f); struct utimbuf t = { mt, mt }; utime(p, &t); }
static void slurp(const char* p, char* o) { FILE* f = fopen(p, "rb"); int n = f ? (int)fread(o, 1, 255, f) : 0; if (f) fclose(f); o[n] = 0; }

int main()
{
    vm_base = (uint8_t*)calloc(1, 1 << 20);
    const char* tmp = getenv("TMPDIR"); if (!tmp) tmp = "/tmp";
    char lo[256], up[256], p[512];
    snprintf(lo, sizeof lo, "%s/cfl_%d", tmp, (int)getpid()); snprintf(up, sizeof up, "%s/cfu_%d", tmp, (int)getpid());
    mkdir(lo, 0777); mkdir(up, 0777);
    snprintf(p, sizeof p, "%s/game", lo); mkdir(p, 0777);
    snprintf(p, sizeof p, "%s/game/g.psarc", lo); mk(p, "PSAR-LOWER", 1600000000);
    setenv("PS3_HDD0_ROOT", up, 1); setenv("PS3_HDD0_LOWER", lo, 1);
    ppu_context c;
    /* stat and read from the lower */
    memset(&c, 0, sizeof c); put("/dev_hdd0/game/g.psarc"); c.gpr[3] = 0x100; c.gpr[4] = 0x200; cellFsStat(&c);
    CHECK(c.gpr[3] == 0 && rd64(0x200 + 0x14) == 1600000000ull && rd64(0x200 + 0x24) == 10, "stat from the lower");
    memset(&c, 0, sizeof c); c.gpr[3] = 0x100; c.gpr[4] = 0; c.gpr[5] = 0x300; cellFsOpen(&c);
    const uint32_t fd = rd32(0x300);
    CHECK(c.gpr[3] == 0, "open read-only");
    memset(&c, 0, sizeof c); c.gpr[3] = fd; c.gpr[4] = 0x400; c.gpr[5] = 4; c.gpr[6] = 0x320; cellFsRead(&c);
    CHECK(!memcmp(vm_base + 0x400, "PSAR", 4), "read the lower bytes");
    memset(&c, 0, sizeof c); c.gpr[3] = fd; cellFsClose(&c);
    snprintf(p, sizeof p, "%s/game/g.psarc", up); struct stat st;
    CHECK(stat(p, &st) != 0, "a read must not copy up");
    /* open for write: copy-up */
    memset(&c, 0, sizeof c); c.gpr[3] = 0x100; c.gpr[4] = CELL_FS_O_RDWR; c.gpr[5] = 0x300; cellFsOpen(&c);
    CHECK(c.gpr[3] == 0 && stat(p, &st) == 0 && st.st_mtime == 1600000000, "O_RDWR copies up with the times");
    memset(&c, 0, sizeof c); c.gpr[3] = rd32(0x300); cellFsClose(&c);
    /* create, mkdir */
    memset(&c, 0, sizeof c); put("/dev_hdd0/game/new.dat"); c.gpr[3] = 0x100; c.gpr[4] = CELL_FS_O_WRONLY | CELL_FS_O_CREAT; c.gpr[5] = 0x300; cellFsOpen(&c);
    snprintf(p, sizeof p, "%s/game/new.dat", up);
    CHECK(c.gpr[3] == 0 && stat(p, &st) == 0, "O_CREAT in the upper");
    memset(&c, 0, sizeof c); c.gpr[3] = rd32(0x300); cellFsClose(&c);
    memset(&c, 0, sizeof c); put("/dev_hdd0/game/d1/d2"); c.gpr[3] = 0x100; cellFsMkdir(&c);
    snprintf(p, sizeof p, "%s/game/d1/d2", up);
    CHECK(stat(p, &st) == 0, "mkdir -p in the upper");
    /* listings */
    snprintf(p, sizeof p, "%s/game/only_lower.txt", lo); mk(p, "L", 1600000001);
    memset(&c, 0, sizeof c); put("/dev_hdd0/game"); c.gpr[3] = 0x100; c.gpr[4] = 0x300; cellFsOpendir(&c);
    const uint32_t dfd = rd32(0x300); int n_g = 0, n_l = 0, n_new = 0, n_all = 0;
    for (;;) {
        memset(&c, 0, sizeof c); c.gpr[3] = dfd; c.gpr[4] = 0x500; c.gpr[5] = 0x320; cellFsReaddir(&c);
        if (!rd64(0x320)) break;
        const char* nm = (const char*)vm_base + 0x502; n_all++;
        if (!strcmp(nm, "g.psarc")) n_g++; if (!strcmp(nm, "only_lower.txt")) n_l++; if (!strcmp(nm, "new.dat")) n_new++;
    }
    memset(&c, 0, sizeof c); c.gpr[3] = dfd; cellFsClosedir(&c);
    CHECK(n_g == 1 && n_l == 1 && n_new == 1, "readdir merged: g.psarc %d, only_lower %d, new %d (entries %d)", n_g, n_l, n_new, n_all);
    memset(&c, 0, sizeof c); c.gpr[3] = 0x100; c.gpr[4] = 0x300; cellFsOpendir(&c);
    const uint32_t dfd2 = rd32(0x300);
    memset(&c, 0, sizeof c); c.gpr[3] = dfd2; c.gpr[4] = 0x10000; c.gpr[5] = 312 * 16; c.gpr[6] = 0x330; cellFsGetDirectoryEntries(&c);
    const uint32_t cnt = rd32(0x330); int saw_l = 0;
    for (uint32_t i = 0; i < cnt; i++) {
        const uint32_t e = 0x10000 + i * 312;
        if (!memcmp(vm_base + e + 54, "only_lower.txt", 14)) { saw_l = 1; CHECK(rd64(e + 0x14) == 1600000001ull, "entry mtime from the lower"); }
    }
    memset(&c, 0, sizeof c); c.gpr[3] = dfd2; cellFsClosedir(&c);
    CHECK(c.gpr[3] == 0 && saw_l, "GetDirectoryEntries lists the lower-only file (%u entries)", cnt);
    char b[256]; snprintf(p, sizeof p, "%s/game/g.psarc", lo); slurp(p, b); stat(p, &st);
    CHECK(!strcmp(b, "PSAR-LOWER") && st.st_mtime == 1600000000, "the lower file is unchanged");
    snprintf(p, sizeof p, "%s/game/new.dat", lo); CHECK(stat(p, &st) != 0, "nothing created in the lower");
    printf("cellFs overlay: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
