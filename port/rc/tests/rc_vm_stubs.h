/* Host-test stubs for a unity build of one patched ps3recomp library source: the guest memory window and the PPU/SPU hooks
 * ppu_memory.h's inline accessors reference. Include after the source under test. */
#ifndef RC_VM_STUBS_H
#define RC_VM_STUBS_H
#include <assert.h>
#include "runtime/ppu/ppu_memory.h"
#include "ps3emu/guest_call.h"
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
#endif
