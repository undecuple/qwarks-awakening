/* Dependencies for the focused cellSpurs unity tests.
 *
 * The scripts discard functions unreachable from the test and runtime
 * constructors, then require every remaining reference to resolve normally.
 * Hook storage is supplied by the SPU runtime in a game build.  Map striped
 * locking to the test's whole-line lock: the LFQueue fixture supplies a real
 * mutex, so producer/consumer exclusion is preserved.
 *
 * This does not model the SPU scheduler or validate runtime-wide integration.
 */
#include <stdint.h>
#include "runtime/spu/spu_context.h"

void (*g_spu_line_commit_hook)(uint32_t);
void (*g_spu_put_hook)(uint32_t, uint32_t);

extern void spu_lockline_lock(void);
extern void spu_lockline_unlock(void);

uint64_t spu_ll_mask(uint32_t ea, uint32_t len)
{
    (void)ea;
    return len ? UINT64_MAX : 0;
}

void spu_ll_lock_mask(uint64_t mask)
{
    if (mask) spu_lockline_lock();
}

void spu_ll_unlock_mask(uint64_t mask)
{
    if (mask) spu_lockline_unlock();
}

/* The policy-wrapper test checks that a running interpreter hands off through
 * this exact entry pointer. Executing the dispatcher requires lifted images
 * and is outside this fixture; fail loudly if a future test reaches it. */
void SPU_ABI spu_indirect_branch(spu_context* ctx)
{
    (void)ctx;
    fputs("unexpected SPU dispatcher execution in focused host test\n", stderr);
    abort();
}
