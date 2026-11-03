/* Host test for patch 7027 (cellSysutilAvc2 state machine: loaded -> joined -> streaming, no peers, no device): unity build of
 * the patched libs/network/cellSysutilAvc2.c. Run with tests/run_imports_test.sh. */
#include "libs/network/cellSysutilAvc2.c"
#include "rc_vm_stubs.h"
#include <stdlib.h>

#define NOT_INIT ((s32)0x8002B703)
#define INVALID  ((s32)0x8002B705)
#define NOT_ACT  ((s32)0x8002B708)

static unsigned events, last_event;
s32 cellSysutilQueueGuestCallbackArgs(u32 cb, const u64 args[8]) { (void)cb; events++; last_event = (unsigned)args[0]; return 0; }

int main(void)
{
    vm_base = calloc(1, 0x1000); assert(vm_base);

    /* Before Load: everything that needs the library says NOT_INITIALIZED (Unload and SetAttribute included). */
    assert(cellSysutilAvc2StartStreaming() == NOT_INIT && cellSysutilAvc2StopStreaming() == NOT_INIT);
    assert(cellSysutilAvc2SetStreamingTarget(0x100) == NOT_INIT);
    assert(cellSysutilAvc2SetPlayerVoiceMuting(1, 1) == NOT_INIT);
    assert(cellSysutilAvc2GetPlayerVoiceMuting(1, 0x100) == NOT_INIT);
    assert(cellSysutilAvc2GetPlayerInfo(1, 0x100) == NOT_INIT);
    assert(cellSysutilAvc2SetAttribute(0x100) == NOT_INIT);
    assert(cellSysutilAvc2Unload() == NOT_INIT);

    /* InitParam: NULL is INVALID_ARGUMENT; else only the version field (first u16) is written. */
    vm_write32(0x200, 0xAAAAAAAAu);
    assert(cellSysutilAvc2InitParam(100, 0) == INVALID);
    assert(cellSysutilAvc2InitParam(110, 0x200) == CELL_OK);
    assert(vm_read16(0x200) == 110 && vm_read16(0x202) == 0xAAAA);

    /* Loaded but not in a chat: the stream/player calls are NOT_ACTIVE; SetAttribute works (NULL is INVALID). */
    assert(events == 0);
    cellSysutilAvc2LoadAsync(0, 0, 0x5000, 0x6000, 0);   /* (ctxId, container, callback, userdata, initParam) */
    assert(events == 1 && last_event == AVC2_EVENT_LOAD_SUCCEEDED);
    assert(cellSysutilAvc2StartStreaming() == NOT_ACT && cellSysutilAvc2StopStreaming() == NOT_ACT);
    assert(cellSysutilAvc2SetStreamingTarget(0x100) == NOT_ACT);
    assert(cellSysutilAvc2GetPlayerInfo(1, 0x100) == NOT_ACT);
    assert(cellSysutilAvc2SetAttribute(0x100) == CELL_OK && cellSysutilAvc2SetAttribute(0) == INVALID);

    /* Joined: streaming calls succeed, a NULL target is INVALID, every player id is unknown (INVALID), outputs untouched. */
    assert(cellSysutilAvc2JoinChatRequest(7) == CELL_OK && last_event == AVC2_EVENT_JOIN_SUCCEEDED);
    assert(cellSysutilAvc2StartStreaming() == CELL_OK && cellSysutilAvc2StopStreaming() == CELL_OK);
    assert(cellSysutilAvc2SetStreamingTarget(0x100) == CELL_OK && cellSysutilAvc2SetStreamingTarget(0) == INVALID);
    vm_write32(0x300, 0xBBBBBBBBu);
    assert(cellSysutilAvc2SetPlayerVoiceMuting(1, 1) == INVALID);
    assert(cellSysutilAvc2GetPlayerVoiceMuting(1, 0x300) == INVALID && vm_read32(0x300) == 0xBBBBBBBBu);
    assert(cellSysutilAvc2GetPlayerInfo(1, 0x300) == INVALID && vm_read32(0x300) == 0xBBBBBBBBu);

    /* Leave drops the streaming state: calls are NOT_ACTIVE again. A rejoin starts clean. */
    assert(cellSysutilAvc2StartStreaming() == CELL_OK);
    assert(cellSysutilAvc2LeaveChatRequest() == CELL_OK && last_event == AVC2_EVENT_LEAVE_SUCCEEDED);
    assert(cellSysutilAvc2StopStreaming() == NOT_ACT && cellSysutilAvc2GetPlayerInfo(1, 0x300) == NOT_ACT);
    assert(cellSysutilAvc2JoinChatRequest(7) == CELL_OK && cellSysutilAvc2StopStreaming() == CELL_OK);

    /* Unload (synchronous) clears everything and raises no event; a second one is NOT_INITIALIZED. */
    unsigned before = events;
    assert(cellSysutilAvc2Unload() == CELL_OK && events == before);
    assert(cellSysutilAvc2Unload() == NOT_INIT && cellSysutilAvc2StartStreaming() == NOT_INIT);
    assert(cellSysutilAvc2GetPlayerInfo(1, 0x300) == NOT_INIT);

    puts("Avc2 state machine checks passed");
    return 0;
}
