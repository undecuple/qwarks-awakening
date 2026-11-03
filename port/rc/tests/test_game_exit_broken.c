/* Host test for patch 7024 (cellGameDataExitBroken): unity build of the patched libs/system/cellGame.c. The call must log the
 * directory (a guest address, or NULL), ask the host to quit once per call with its reason, and return CELL_OK.
 * Run with tests/run_imports_test.sh. */
#include "libs/system/cellGame.c"
#include "rc_vm_stubs.h"
#include <stdlib.h>

const char* ppu_vfs_root = ".";
static int quits; static char quit_why[64];
void ps3_host_quit(const char* why) { quits++; snprintf(quit_why, sizeof quit_why, "%s", why); }
void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*)) { (void)nid; (void)name; (void)fn; }

int main(void)
{
    vm_base = calloc(1, 0x1000); assert(vm_base);
    strcpy((char*)vm_base + 0x100, "BCUS98148GAME");
    assert(cellGameDataExitBroken((const char*)(uintptr_t)0x100) == CELL_OK);
    assert(quits == 1 && !strcmp(quit_why, "cellGameDataExitBroken"));
    assert(cellGameDataExitBroken(NULL) == CELL_OK && quits == 2);   /* NULL dirName: no crash, still quits */
    puts("cellGameDataExitBroken checks passed");
    return 0;
}
