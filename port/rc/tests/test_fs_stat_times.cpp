/* Host test (no game data): cellFsStat / cellFsFstat report the host file's atime/mtime/ctime (was R&C patch
 * 7011; upstream since the v0.18.0 rebase).
 * Unity build of the runtime's runtime/ppu/ppu_fs.cpp (FS_SRC) with stubs for its few externals; guest memory is a
 * calloc'd block. A temp file under PS3_HDD1_ROOT gets mtime 1234567890 (utime); /dev_hdd1/t.bin is then stat'ed,
 * opened and fstat'ed. lv2 (RPCS3 sys_fs_stat / sys_fs_fstat): times come from the file. The base wrote 0.
 * CellFsStat layout: mode@0 uid@4 gid@8 atime@0x0C mtime@0x14 ctime@0x1C size@0x24 blksize@0x2C (big-endian). */
#include FS_SRC
#include <utime.h>

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
const char* edat_resolve(const char* host_path, char*, size_t) { return host_path; }   /* not an EDAT: the file itself */

static uint64_t rd64(uint32_t a) { uint64_t v; memcpy(&v, vm_base + a, 8); return __builtin_bswap64(v); }
static uint32_t rd32(uint32_t a) { uint32_t v; memcpy(&v, vm_base + a, 4); return __builtin_bswap32(v); }

int main()
{
    vm_base = (uint8_t*)calloc(1, 1 << 20);
    char dir[] = "/tmp/fsstatXXXXXX";
    if (!mkdtemp(dir)) return 2;
    setenv("PS3_HDD1_ROOT", dir, 1);
    char hp[256]; snprintf(hp, sizeof hp, "%s/t.bin", dir);
    FILE* f = fopen(hp, "wb"); fputs("0123456789", f); fclose(f);
    struct utimbuf ut = { 1234567000, 1234567890 }; utime(hp, &ut);
    strcpy((char*)vm_base + 0x100, "/dev_hdd1/t.bin");
    int fails = 0;
    ppu_context c; memset(&c, 0, sizeof c);
    c.gpr[3] = 0x100; c.gpr[4] = 0x200;
    cellFsStat(&c);
    const uint64_t smt = rd64(0x200 + 0x14), sat = rd64(0x200 + 0x0C), ssz = rd64(0x200 + 0x24);
    printf("cellFsStat : rc=0x%llX mtime=%llu atime=%llu size=%llu\n", (unsigned long long)c.gpr[3],
           (unsigned long long)smt, (unsigned long long)sat, (unsigned long long)ssz);
    if (c.gpr[3] != 0 || smt != 1234567890ull || sat != 1234567000ull || ssz != 10) fails++;
    memset(&c, 0, sizeof c); c.gpr[3] = 0x100; c.gpr[4] = 0; c.gpr[5] = 0x300;   /* flags O_RDONLY, fd out */
    cellFsOpen(&c);
    const uint32_t fd = rd32(0x300);
    memset(&c, 0, sizeof c); c.gpr[3] = fd; c.gpr[4] = 0x400;
    cellFsFstat(&c);
    const uint64_t fmt = rd64(0x400 + 0x14), fsz = rd64(0x400 + 0x24);
    printf("cellFsFstat: fd=%u rc=0x%llX mtime=%llu size=%llu\n", fd, (unsigned long long)c.gpr[3],
           (unsigned long long)fmt, (unsigned long long)fsz);
    if (c.gpr[3] != 0 || fmt != 1234567890ull || fsz != 10) fails++;
    remove(hp); rmdir(dir);
    printf("%s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
