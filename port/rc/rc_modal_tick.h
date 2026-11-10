/* rc_modal_tick.h -- window stage 1c (2026-11-09): keep the frame clock running while Windows holds the frame thread
 * in a modal loop. Pure state machine (host test port/rc/tests/test_rc_modal_tick.c); the Win32 side is in
 * port/rc/main.cpp (rc_modal_wndproc, rc_modal_pacer).
 *
 * Cause (measured on ToD under Proton 2026-11-09, runs wr1c-before-drag / wr1c-after-drag): the frame thread owns the
 * window and pumps its messages between vblank ticks (frame_clock -> rsx_null_backend_pump_messages ->
 * DispatchMessageA). A title-bar drag, a border resize or the system menu (Alt+Space) runs DefWindowProc's modal
 * move/size or menu loop inside that DispatchMessageA and returns only when the button is released (the menu closed),
 * so vblanks, the FIFO drain (the RSX labels the game fences on) and presents all stopped: 0 flips/s during a 10 s
 * caption drag and a 10 s border drag (normal ~61/s), and the game stood still for as long as the system menu was open.
 *
 * Fix: the loop still dispatches posted messages. Between WM_ENTERSIZEMOVE/WM_ENTERMENULOOP and the matching exit, a
 * pacer thread posts one RC_WM_MODAL_TICK every ~4 ms (the frame loop's own Sleep(4) cadence) and the window procedure
 * runs one frame-clock step for it on the window's thread: vblank ticks off real time, flip, FIFO drain, present. The
 * step never pumps (we are already inside the pump). At most one tick is outstanding: a posted message is retrieved
 * before input, so a pacer that kept posting while a present blocked would starve the drag's mouse moves; with one in
 * flight the loop always reaches the input between two ticks. The window stays on the frame thread (DXGI swap chain,
 * 2095/2126 mode changes and the live-draw walker keep their one owner); a resize during the drag is taken by
 * ld_dm_sync_size at the next present, one ResizeBuffers per presented frame (DXGI's documented pattern). */
#ifndef RC_MODAL_TICK_H
#define RC_MODAL_TICK_H

typedef struct {
    int depth;              /* nested modal loops (Alt+Space menu, then Move from it: menu exit and size/move enter) */
    int posted;             /* a tick message is in the queue */
    unsigned ticks;         /* frame-clock steps run inside the current modal stretch */
    unsigned long long t0;  /* ms at the first enter */
} rc_modal;

/* WM_ENTERSIZEMOVE / WM_ENTERMENULOOP. 1 = the clock has just left the frame loop (start the pacer, log). */
static inline int rc_modal_enter(rc_modal* m, unsigned long long now_ms)
{
    if (m->depth++ > 0) return 0;
    m->ticks = 0;
    m->t0 = now_ms;
    return 1;
}

/* WM_EXITSIZEMOVE / WM_EXITMENULOOP. 1 = back in the frame loop. An exit without an enter (seen from some menu paths)
 * is ignored. */
static inline int rc_modal_exit(rc_modal* m)
{
    if (m->depth <= 0) { m->depth = 0; return 0; }
    return --m->depth == 0;
}

/* Pacer side: 1 = post a tick now (marks it outstanding). */
static inline int rc_modal_want_post(rc_modal* m)
{
    if (m->depth <= 0 || m->posted) return 0;
    m->posted = 1;
    return 1;
}

/* Window side, for each RC_WM_MODAL_TICK: 1 = run one frame-clock step. A tick that arrives after the loop ended (it
 * was posted just before WM_EXITSIZEMOVE and the frame loop's own pump dispatched it) runs nothing. */
static inline int rc_modal_take_tick(rc_modal* m)
{
    m->posted = 0;
    if (m->depth <= 0) return 0;
    m->ticks++;
    return 1;
}

#endif
