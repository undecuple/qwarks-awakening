/* Host test for R&C patch 7068: the INLINE_ARRAY buffer grows on demand. Pushes inline streams of 16 KB, exactly 64 KB,
 * 64 KB + 4, 200 KB and 3 MB through rsx_dispatch_method (BEGIN_END(prim), words, BEGIN_END(0)) and checks the sink's
 * inline_array callback receives every byte, in guest (big-endian) order, with inline_dropped still 0; a following small
 * stream after a large one; the destroy / reuse cycle. Builds against libs/video/rsx_dispatch.c only. */
#include "rsx_dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); fails++; } } while (0)

#define M_BEGIN_END 0x1808u
#define M_INLINE    0x1818u

static struct { u32 calls, bytes; int content_ok, ended_after; u32 ends; } g;

static void on_inline(void* user, const rsx_dispatch* rsx, const u8* data, u32 bytes)
{
    (void)user; (void)rsx;
    g.calls++; g.bytes = bytes; g.content_ok = 1;
    for (u32 i = 0; i < bytes / 4u; i++) {
        const u32 w = 0xA5000000u ^ (i * 2654435761u);
        const u32 got = ((u32)data[4*i] << 24) | ((u32)data[4*i+1] << 16) | ((u32)data[4*i+2] << 8) | data[4*i+3];
        if (got != w) { g.content_ok = 0; break; }
    }
}
static void on_end(void* user, const rsx_dispatch* rsx) { (void)user; (void)rsx; g.ends++; }

static void stream(rsx_dispatch* r, u32 words, const char* name)
{
    char msg[128];
    memset(&g, 0, sizeof g);
    rsx_dispatch_method(r, M_BEGIN_END, 8u /* QUADS */);
    for (u32 i = 0; i < words; i++) rsx_dispatch_method(r, M_INLINE, 0xA5000000u ^ (i * 2654435761u));
    rsx_dispatch_method(r, M_BEGIN_END, 0);
    snprintf(msg, sizeof msg, "%s: the sink got one inline_array call", name);      CHECK(g.calls == 1, msg);
    snprintf(msg, sizeof msg, "%s: all %u bytes delivered (got %u)", name, words * 4u, g.bytes);
    CHECK(g.bytes == words * 4u, msg);
    snprintf(msg, sizeof msg, "%s: bytes are the words, big-endian, in order", name); CHECK(g.content_ok, msg);
    snprintf(msg, sizeof msg, "%s: END followed", name);                              CHECK(g.ends == 1, msg);
    snprintf(msg, sizeof msg, "%s: nothing dropped", name);                           CHECK(r->inline_dropped == 0, msg);
}

int main(void)
{
    rsx_dispatch_sink sink; memset(&sink, 0, sizeof sink);
    sink.inline_array = on_inline; sink.end = on_end;
    rsx_dispatch* r = (rsx_dispatch*)malloc(sizeof *r);   /* heap, uninitialised: init must make it valid */
    rsx_dispatch_init(r, &sink);
    CHECK(r->inline_data == NULL && r->inline_cap == 0, "no buffer before the first inline word");

    stream(r, 4096, "16 KB");
    CHECK(r->inline_cap == RSX_DSP_INLINE_MAX_BYTES, "first allocation is the 64 KB initial capacity");
    stream(r, RSX_DSP_INLINE_MAX_BYTES / 4u, "exactly 64 KB");
    CHECK(r->inline_cap == RSX_DSP_INLINE_MAX_BYTES, "exactly 64 KB does not grow");
    stream(r, RSX_DSP_INLINE_MAX_BYTES / 4u + 1u, "64 KB + 4 (was dropped before the patch)");
    CHECK(r->inline_cap == 2u * RSX_DSP_INLINE_MAX_BYTES, "64 KB + 4 doubles once");
    stream(r, 50000, "200 KB");
    stream(r, 786432, "3 MB");
    stream(r, 100, "400 B after a large stream");
    CHECK(r->inline_cap >= 3u * 1024u * 1024u, "the capacity stays grown");

    /* 1365 vertices of 48 bytes: the A Crack in Time time-bomb bubble's layer size (RACiT PR 21), two layers. */
    stream(r, 2u * 1365u * 48u / 4u, "2 x 1365 vertices x 48 B");

    rsx_dispatch_destroy(r);
    CHECK(r->inline_data == NULL && r->inline_cap == 0 && r->inline_len == 0, "destroy frees and clears");
    rsx_dispatch_destroy(r);   /* twice is fine */
    stream(r, 30000, "120 KB after destroy");

    /* An empty BEGIN/END delivers no inline call, as before. */
    memset(&g, 0, sizeof g);
    rsx_dispatch_method(r, M_BEGIN_END, 8u); rsx_dispatch_method(r, M_BEGIN_END, 0);
    CHECK(g.calls == 0 && g.ends == 1, "an empty primitive delivers no inline stream");

    rsx_dispatch_destroy(r);
    free(r);
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    printf("inline grow test: ok\n");
    return 0;
}
