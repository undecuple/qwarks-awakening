/* Host test of port/rc/rc_modal_tick.h (window stage 1c): the modal-loop clock's state machine.
 *   nix develop .#re --command port/rc/tests/run_rc_modal_tick_test.sh */
#include <stdio.h>
#include <string.h>
#include "rc_modal_tick.h"

static int fails;
#define CHECK(c) do { if (c) printf("[PASS] %s\n", #c); else { printf("[FAIL] %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

int main(void)
{
    rc_modal m;
    memset(&m, 0, sizeof m);

    /* Outside a modal loop the pacer posts nothing and a stray tick runs nothing. */
    CHECK(rc_modal_want_post(&m) == 0);
    CHECK(rc_modal_take_tick(&m) == 0);

    /* Title-bar drag: enter, ticks while held, exit. */
    CHECK(rc_modal_enter(&m, 1000) == 1);
    CHECK(m.t0 == 1000 && m.ticks == 0);
    CHECK(rc_modal_want_post(&m) == 1);
    CHECK(rc_modal_want_post(&m) == 0);          /* one outstanding: the loop reaches input between two ticks */
    CHECK(rc_modal_take_tick(&m) == 1);
    CHECK(rc_modal_want_post(&m) == 1);          /* handled -> the next one may go */
    CHECK(rc_modal_take_tick(&m) == 1);
    CHECK(m.ticks == 2);
    CHECK(rc_modal_exit(&m) == 1);
    CHECK(rc_modal_want_post(&m) == 0);

    /* A tick posted just before the exit and dispatched by the frame loop's own pump: no step, flag cleared. */
    memset(&m, 0, sizeof m);
    rc_modal_enter(&m, 5);
    CHECK(rc_modal_want_post(&m) == 1);
    rc_modal_exit(&m);
    CHECK(rc_modal_take_tick(&m) == 0 && m.posted == 0);
    CHECK(m.ticks == 0);

    /* Alt+Space then Move: menu loop enter, size/move enter (nested), menu exit, size/move exit. One stretch. */
    memset(&m, 0, sizeof m);
    CHECK(rc_modal_enter(&m, 10) == 1);
    CHECK(rc_modal_enter(&m, 20) == 0 && m.t0 == 10);
    CHECK(rc_modal_exit(&m) == 0);
    CHECK(rc_modal_want_post(&m) == 1 && rc_modal_take_tick(&m) == 1);
    CHECK(rc_modal_exit(&m) == 1);
    CHECK(m.ticks == 1);

    /* Menu exits after the menu closed, then Move enters: two stretches, each counted from 0. */
    memset(&m, 0, sizeof m);
    rc_modal_enter(&m, 1); rc_modal_want_post(&m); rc_modal_take_tick(&m);
    CHECK(rc_modal_exit(&m) == 1 && m.ticks == 1);
    CHECK(rc_modal_enter(&m, 2) == 1 && m.ticks == 0 && m.t0 == 2);
    CHECK(rc_modal_exit(&m) == 1);

    /* An exit with no enter (seen from some menu paths) is ignored and does not go negative. */
    memset(&m, 0, sizeof m);
    CHECK(rc_modal_exit(&m) == 0 && m.depth == 0);
    CHECK(rc_modal_enter(&m, 3) == 1);
    CHECK(rc_modal_exit(&m) == 1 && m.depth == 0);

    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
