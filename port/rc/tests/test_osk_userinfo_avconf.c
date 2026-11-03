/* Host test for patches 7022 (cellOskDialogSetKeyLayoutOption), 7023 (cellUserInfoSelectUser_SetList) and 7025
 * (cellVideoOutGetScreenSize): unity build of the three patched sources. Run with tests/run_imports_test.sh. */
#include "libs/misc/cellOskDialog.c"
#include "libs/system/cellUserInfo.c"
#include "libs/misc/cellAvconfExt.c"
#include "rc_vm_stubs.h"
void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*)) { (void)nid; (void)name; (void)fn; }

int main(void)
{
    /* 7022: bit set of 10KEY (1) | FULLKEY (2); other bits are PARAM and leave the stored option alone. */
    assert(ps3_osk_key_layout_option() == 0);
    assert(cellOskDialogSetKeyLayoutOption(1) == CELL_OK && ps3_osk_key_layout_option() == 1);
    assert(cellOskDialogSetKeyLayoutOption(3) == CELL_OK && ps3_osk_key_layout_option() == 3);
    assert(cellOskDialogSetKeyLayoutOption(0) == CELL_OK && ps3_osk_key_layout_option() == 0);
    assert(cellOskDialogSetKeyLayoutOption(2) == CELL_OK);
    assert(cellOskDialogSetKeyLayoutOption(4) == (s32)0x8002B504 && ps3_osk_key_layout_option() == 2);
    assert(cellOskDialogSetKeyLayoutOption(0x8000001u) == (s32)0x8002B504 && ps3_osk_key_layout_option() == 2);

    /* 7023: NULL is PARAM and keeps the previous set; otherwise the guest address is stored. */
    assert(ps3_userinfo_list_set() == 0);
    assert(cellUserInfoSelectUser_SetList(NULL) == CELL_USERINFO_ERROR_PARAM && ps3_userinfo_list_set() == 0);
    assert(cellUserInfoSelectUser_SetList((const CellUserInfoListSet*)(uintptr_t)0x1234) == CELL_OK);
    assert(ps3_userinfo_list_set() == 0x1234);
    assert(cellUserInfoSelectUser_SetList(NULL) == CELL_USERINFO_ERROR_PARAM && ps3_userinfo_list_set() == 0x1234);

    /* 7025: primary only; no EDID size in a window -> VALUE_IS_NOT_SET, the output stays untouched on every error. */
    float size = 42.0f;
    assert(cellVideoOutGetScreenSize(0, &size) == (s32)0x8002B229 && size == 42.0f);
    assert(cellVideoOutGetScreenSize(1, &size) == (s32)0x8002B226 && size == 42.0f);
    assert(cellVideoOutGetScreenSize(0, NULL) == (s32)0x8002B223);
    assert(cellVideoOutGetScreenSize(1, NULL) == (s32)0x8002B226);   /* the output id is checked first */

    puts("OSK key layout, UserInfo SetList and VideoOutGetScreenSize checks passed");
    return 0;
}
