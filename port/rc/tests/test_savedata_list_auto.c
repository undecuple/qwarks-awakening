/* Host test for patch 7020 (cellSaveDataListAutoSave and the User*List* entry points): unity build of the patched
 * libs/system/cellSaveData.c, driven through the ctx handlers exactly as the HLE dispatcher calls them (arguments in
 * r3..r10, the ninth/tenth on the guest stack at SP+112/SP+120). Run with tests/run_imports_test.sh. */
#include "libs/system/cellSaveData.c"
#include <assert.h>
uint8_t* vm_base;
uint32_t ppu_vm_size;
int g_resv_store_active;
uint32_t g_ww_lo, g_ww_hi;
void ppu_resv_break_store(uint64_t ea) { (void)ea; }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a; (void)v; (void)w; }
int spu_coh_is_reserved(uint32_t a) { (void)a; return 0; }
void spu_coh_notify_write(uint32_t a) { (void)a; }
void spu_lockline_lock(void) {}
void spu_lockline_unlock(void) {}
ps3_guest_caller_fn g_ps3_guest_caller;

static uint32_t reg_nid[16]; static void (*reg_fn[16])(ppu_context*); static int reg_n;
void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*))
{ (void)name; if (reg_n < 16) { reg_fn[reg_n] = fn; reg_nid[reg_n++] = nid; } }
static void (*registered_fn(uint32_t nid))(ppu_context*)
{ for (int i = 0; i < reg_n; i++) if (reg_nid[i] == nid) return reg_fn[i]; return 0; }
static int registered(uint32_t nid) { for (int i = 0; i < reg_n; i++) if (reg_nid[i] == nid) return 1; return 0; }

static unsigned fixed_calls, last_userdata;
static void guest_callback(u32 opd, u64 cb, u64 get, u64 set, u64 a, u64 b, u64 c, u64 d, u64 e)
{
    (void)get; (void)set; (void)a; (void)b; (void)c; (void)d; (void)e;
    assert(opd == 0x800);               /* only the fixed callback is reached: it finishes the sequence */
    fixed_calls++;
    last_userdata = vm_read32(cb + 16); /* cbResult->userdata */
    vm_write32(cb, 1);                  /* CELL_SAVEDATA_CBRESULT_OK_LAST */
}

static void call(void (*h)(ppu_context*), ppu_context* ctx) { h(ctx); }

int main(void)
{
    char root[] = "/tmp/rc-savedata-list-XXXXXX";
    assert(mkdtemp(root)); strcpy(s_save_root, root);
    vm_base = calloc(1, 0x200000); assert(vm_base);
    assert(cellSaveData_set_scratch_region(0x100000, 0x20000) == CELL_OK);
    strcpy((char*)vm_base + 0x300, "TEST");
    vm_write32(0x108, 0x300); vm_write32(0x200, 1);   /* setList.dirNamePrefix, setBuf.dirListMax */
    g_ps3_guest_caller = guest_callback;
    ppu_context ctx = {0};
    ctx.gpr[1] = 0x400;
    cellSaveData_register_ctx_handlers();   /* the five new NIDs (SHA-1 of the names) are bound */
    assert(registered(0x4DD03A4Eu) && registered(0x0E091C36u) && registered(0x248BD1D8u) &&
           registered(0x0F03CFB0u) && registered(0x39DD8425u));

    /* patch 7028: cellSaveDataListAutoLoad (0x21425307) is a ctx handler, so the game's userdata (ninth argument, guest
     * stack) reaches the callbacks instead of NULL. Same register layout as ListAutoSave. */
    assert(registered_fn(0x21425307u) == ps3_savedata_list_auto_load);
    ctx.gpr[3] = 0; ctx.gpr[4] = 0; ctx.gpr[5] = 0x100; ctx.gpr[6] = 0x200; ctx.gpr[7] = 0x800; ctx.gpr[8] = 0x900;
    ctx.gpr[9] = 0; ctx.gpr[10] = 0;
    vm_write64(0x400 + 112, 0xFACE);
    call(registered_fn(0x21425307u), &ctx);
    assert((s32)ctx.gpr[3] == CELL_OK && fixed_calls == 1 && last_userdata == 0xFACE);
    ctx.gpr[4] = 3;                                    /* errDialog out of range */
    call(registered_fn(0x21425307u), &ctx);
    assert((s32)ctx.gpr[3] == CELL_SAVEDATA_ERROR_PARAM && fixed_calls == 1);
    fixed_calls = 0;

    /* cellSaveDataListAutoSave (9 args): r3 version, r4 errDialog, r5 setList, r6 setBuf, r7 funcFixed, r8 funcStat,
     * r9 funcFile, r10 container, userdata on the stack. */
    ctx.gpr[3] = 0; ctx.gpr[4] = 0; ctx.gpr[5] = 0x100; ctx.gpr[6] = 0x200; ctx.gpr[7] = 0x800; ctx.gpr[8] = 0x900;
    vm_write64(0x400 + 112, 0xCAFE);
    call(hle_cellSaveDataListAutoSave, &ctx);
    assert((s32)ctx.gpr[3] == CELL_OK && fixed_calls == 1 && last_userdata == 0xCAFE);
    ctx.gpr[4] = 3;
    call(hle_cellSaveDataListAutoSave, &ctx);
    assert((s32)ctx.gpr[3] == CELL_SAVEDATA_ERROR_PARAM && fixed_calls == 1);

    /* cellSaveDataUserListAutoSave / UserListAutoLoad (10 args): r4 userId, r5 errDialog, r6 setList, r7 setBuf,
     * r8 funcFixed, r9 funcStat, r10 funcFile, stack: container, userdata. */
    ctx.gpr[3] = 0; ctx.gpr[4] = 1; ctx.gpr[5] = 0; ctx.gpr[6] = 0x100; ctx.gpr[7] = 0x200; ctx.gpr[8] = 0x800;
    ctx.gpr[9] = 0x900; ctx.gpr[10] = 0;
    vm_write64(0x400 + 112, 0); vm_write64(0x400 + 120, 0xBEEF);
    call(hle_cellSaveDataUserListAutoSave, &ctx);
    assert((s32)ctx.gpr[3] == CELL_OK && fixed_calls == 2 && last_userdata == 0xBEEF);
    ctx.gpr[4] = 1;
    call(hle_cellSaveDataUserListAutoLoad, &ctx);
    assert((s32)ctx.gpr[3] == CELL_OK && fixed_calls == 3 && last_userdata == 0xBEEF);
    ctx.gpr[4] = 0;                                    /* CELL_SYSUTIL_USERID_CURRENT */
    call(hle_cellSaveDataUserListAutoLoad, &ctx);
    assert((s32)ctx.gpr[3] == CELL_OK && fixed_calls == 4);
    ctx.gpr[4] = 7;                                    /* a user that is not signed in */
    call(hle_cellSaveDataUserListAutoSave, &ctx);
    assert((s32)ctx.gpr[3] == CELL_SAVEDATA_ERROR_NOUSER && fixed_calls == 4);
    call(hle_cellSaveDataUserListAutoLoad, &ctx);
    assert((s32)ctx.gpr[3] == CELL_SAVEDATA_ERROR_NOUSER && fixed_calls == 4);
    ctx.gpr[4] = 1; ctx.gpr[5] = 3;                    /* errDialog out of range */
    call(hle_cellSaveDataUserListAutoSave, &ctx);
    assert((s32)ctx.gpr[3] == CELL_SAVEDATA_ERROR_PARAM && fixed_calls == 4);

    /* cellSaveDataUserListSave / UserListLoad (9 args): the user check comes first; a missing funcList is PARAM. */
    ctx.gpr[3] = 0; ctx.gpr[4] = 7; ctx.gpr[5] = 0x100; ctx.gpr[6] = 0x200; ctx.gpr[7] = 0x800; ctx.gpr[8] = 0x900;
    ctx.gpr[9] = 0; ctx.gpr[10] = 0;
    call(hle_cellSaveDataUserListSave, &ctx);
    assert((s32)ctx.gpr[3] == CELL_SAVEDATA_ERROR_NOUSER);
    call(hle_cellSaveDataUserListLoad, &ctx);
    assert((s32)ctx.gpr[3] == CELL_SAVEDATA_ERROR_NOUSER);
    ctx.gpr[4] = 1; ctx.gpr[7] = 0;
    call(hle_cellSaveDataUserListSave, &ctx);
    assert((s32)ctx.gpr[3] == CELL_SAVEDATA_ERROR_PARAM);
    call(hle_cellSaveDataUserListLoad, &ctx);
    assert((s32)ctx.gpr[3] == CELL_SAVEDATA_ERROR_PARAM);

    assert(rmdir(root) == 0); free(vm_base);
    puts("savedata ListAutoSave / User*List* handler checks passed");
    return 0;
}
