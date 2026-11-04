/* R&C pass 5: guest setjmp/longjmp for ToD's Lua 5.0.2 error handling (port/rc/tods/ppu_overrides.txt lines 00671CA8, 00826B34).
 * Included at the end of tods_ppu_overrides.cpp; port/rc/tests/test_lua_ljmp.cpp includes it too (with the real lifted functions).
 *
 * The defect (NEW GAME exit, tods-ng-us / tods-ng-7002c): the lifted libc longjmp @0x00826B34 restores the guest registers from the
 * jmp_buf and then RETURNS to its lifted C caller, because a lifted function cannot leave its host frame. Lua's luaD_throw @0x00671D38
 * (`if (L->errorJmp) { L->errorJmp->status = errcode; longjmp(L->errorJmp->b, 1); } else { G(L)->panic(L); exit(EXIT_FAILURE); }`)
 * therefore falls through into its own "no handler" branch with r3 = 1 (longjmp's value) taken as L: reads 1+0x10 = 0x11 (G(L)),
 * 0x28 (G->panic), 0x0/0x4 (the NULL OPD), calls NULL, then exit(1) @0x00671D9C -> libc exit -> sys_process_exit(1).
 * Every Lua error raised inside a protected call (lua_pcall, lua_load, lua_resume) kills the process; on the console it is caught.
 *
 * Where the guest uses setjmp (read off the lifted code, 2026-10-21): ONE call site, @0x00671CE0 `bl 0x0059E630` (TOC stub -> setjmp
 * @0x00826A10) in luaD_rawrunprotected @0x00671CA8. longjmp (stub 0x0059DE10 -> @0x00826B34) is called from luaD_throw @0x00671D6C and two
 * inlined LUA_ERRERR throws @0x00673018 (func_00672CB8) and @0x00673734 (func_006733A0). Lua names: the Lua 5.0.2 sources
 * ("$Lua: Lua 5.0.2 Copyright ..." @0x1002ABB0); the layout matches 5.0: L->top +0x08, G(L) +0x10, errorJmp +0x50, errfunc +0x54,
 * G->panic +0x28, struct lua_longjmp {previous +0; jmp_buf b +4; status +0x224}, TObject 8 bytes {tt, value}.
 *
 * luaD_rawrunprotected @0x00671CA8 (lifted, 0x671CA8..0x671D34):
 *   stdu r1,-0x2B0(r1); lj = r1+0x70; lj.status (r1+0x294) = 0; lj.previous = L->errorJmp; L->errorJmp = &lj;
 *   L at r1+0x2A0, f at r1+0x2A4, ud at r1+0x2A8, lr at r1+0x2C0; r3 = &lj.b = r1+0x74; bl setjmp            (@0x671CE0)
 *   0x671CE4: ld r2,0x28(r1); r9 = f; cmpwi r3,0; r4 = ud; r3 = L; bne 0x671D14; (call f via its OPD)        (@0x671CE4-0x671D10)
 *   0x671D14: L->errorJmp = lj.previous; r3 = (s32)lj.status; lr = saved; addi r1,r1,0x2B0; blr              (@0x671D14-0x671D34)
 *
 * The fix: a host frame at the setjmp site. The override takes a host setjmp before it runs the lifted body and records it under the
 * guest jmp_buf address the body will pass to setjmp ((r1_in - 0x2B0 + 0x74 + 0xF) & ~0xF, the alignment setjmp/longjmp apply @0x826A10).
 * The longjmp override runs the lifted longjmp (guest registers restored, r3 = val ? val : 1) and then host-longjmps to the record of
 * that guest jmp_buf, so control lands in the override, which runs the post-setjmp tail of the original (0x671CE4 with r3 != 0, i.e.
 * the 0x671D14 epilogue). Records live on a per-thread stack; a normal return pops its own record, a landing drops the records above it.
 * ps3_indirect_call's per-thread depth counters (dispatch depth, vcall sp) are saved at the setjmp and restored at the landing, as the
 * runtime asks of anything that leaves guest frames without returning (ppu_loader.cpp: ps3_ppu_dispatch_depth_slot).
 * A longjmp to a jmp_buf with no record keeps the old behaviour (returns) and logs it.
 * Off switch: TODS_LUA_LJMP=0 (both overrides call the lifted bodies only). No gameplay change: this is what the console does.
 * Log: `[lua] caught error status=N (...) msg='...'` for the first 64 caught errors, then every 1000th (the message is the TString at
 * L->top-1 for status 1 (ERRRUN) and 3 (ERRSYNTAX); 4 = ERRMEM, 5 = ERRERR push nothing).
 * Pass 6: with 0x006670C8 overridden too (the game's "execute" loader: compile a string with chunk name "execute" (TOC-0x6070 ->
 * 0x10014730) and pcall it; Lua's `execute(str)` is 0x00099BD0, registered under the name "execute" @0x0088FADC), each caught error also
 * prints `[lua]   in execute() chunk src@0x...: <text>` = the string handed to the loader. RC_LUA_SRC=1 (diagnostic, default off) logs
 * every execute() chunk (first 4000) as `[lua] execute src@...`.
 * Upstream candidate: the general form is a lifter option that emits this host frame at every call of a function
 * marked as setjmp; this file is the one-site version for ToD. */
#if defined(PPU_OVERRIDE_00671CA8) && defined(PPU_OVERRIDE_00826B34)
#include <setjmp.h>

extern "C" int* ps3_ppu_dispatch_depth_slot(void);   /* ppu_loader.cpp:1484 */
extern "C" int* ps3_ppu_vcall_sp_slot(void);         /* ppu_loader.cpp:1485 */
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);   /* ppu_loader.cpp:2158 */

namespace tods_lua_ljmp {
struct Rec { uint32_t jb; jmp_buf* host; };
enum { kMax = 256 };
static PPU_THREAD_LOCAL Rec s_rec[kMax];
static PPU_THREAD_LOCAL int s_n = 0;
static unsigned long long s_caught = 0, s_unmatched = 0;   /* log rate only (racy counts are fine) */
static PPU_THREAD_LOCAL uint32_t s_src = 0;     /* guest string of the innermost execute() chunk running on this thread (0x006670C8), 0 = none */

static void print_src(const char* tag, uint32_t src)   /* the chunk text, escaped, up to 1024 bytes */
{
    char buf[1024 * 4 + 1]; size_t o = 0; uint32_t i = 0;
    for (; i < 1024; i++) {
        const uint8_t c = vm_read8(src + i);
        if (!c) break;
        if (c == '\n') { buf[o++] = '\\'; buf[o++] = 'n'; }
        else if (c == '\t') { buf[o++] = '\\'; buf[o++] = 't'; }
        else if (c >= 0x20 && c < 0x7F && c != '\\') buf[o++] = (char)c;
        else o += (size_t)snprintf(buf + o, 5, "\\x%02X", c);
    }
    buf[o] = 0;
    fprintf(stderr, "[lua] %s src@0x%08X%s: %s\n", tag, src, (i == 1024 && vm_read8(src + i)) ? " (cut at 1024)" : "", buf);
}

static int enabled()
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("TODS_LUA_LJMP"); on = !(e && e[0] == '0'); }
    return on;
}

static void log_caught(uint32_t L, int32_t status)
{
    const unsigned long long n = ++s_caught;
    if (n > 64 && n % 1000) return;
    static const char* const names[] = { "OK", "ERRRUN", "ERRFILE", "ERRSYNTAX", "ERRMEM", "ERRERR" };
    char msg[161] = "";
    if (status == 1 || status == 3) {                                      /* luaD_seterrorobj @0x671E58: message at L->top - 1 */
        const uint32_t top = vm_read32(L + 0x08);
        if (top >= 8 && vm_read32(top - 8) == 4) {                          /* tt == LUA_TSTRING (5.0: 4), @0x671EC8 stores 4 */
            const uint32_t ts = vm_read32(top - 4);                         /* TString: len +0x0C, chars +0x10 (5.0 layout, ASSUMPTION) */
            uint32_t len = ts ? vm_read32(ts + 0x0C) : 0;
            if (len > 160) len = 160;
            for (uint32_t i = 0; i < len; i++) { const uint8_t c = vm_read8(ts + 0x10 + i); msg[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '.'; }
            msg[len] = 0;
        }
    }
    fprintf(stderr, "[lua] caught error #%llu status=%d (%s) L=0x%08X msg='%s'\n", n, status,
            (status >= 0 && status <= 5) ? names[status] : "?", L, msg);
    if (s_src) print_src("  in execute() chunk", s_src);   /* the innermost execute() string; ERRSYNTAX = this text failed to compile */
}
} // namespace tods_lua_ljmp

/* @0x00671CA8  luaD_rawrunprotected(L, f, ud) (Lua 5.0.2 name) */
void func_00671CA8(ppu_context* ctx)
{
    using namespace tods_lua_ljmp;
#ifdef TODS_PAUSE_QUIT_HOOK
    TodsPauseQuitCall pq;                                                   /* tods_pause_quit.inc: the pause menu's two extra rows */
    tods_pause_quit_before(ctx, &pq);
    const uint32_t pq_L = (uint32_t)ctx->gpr[3];
#endif
    if (!enabled() || s_n >= kMax) {
        func_00671CA8__lifted(ctx);
#ifdef TODS_PAUSE_QUIT_HOOK
        tods_pause_quit_after(ctx, &pq, pq_L);
#endif
        return;
    }
    const uint64_t frame = ctx->gpr[1] - 0x2B0;                             /* stdu r1,-0x2B0(r1) @0x671CB0 */
    const uint32_t jb = (uint32_t)((frame + 0x74 + 0xF) & ~0xFull);         /* &lj.b @0x671CD0, aligned as in setjmp @0x826A10 */
    jmp_buf host;
    const int depth0 = *ps3_ppu_dispatch_depth_slot(), vsp0 = *ps3_ppu_vcall_sp_slot();
    const uint32_t src0 = s_src;                                            /* execute() frames skipped by a landing */
    const int me = s_n;
    s_rec[me].jb = jb; s_rec[me].host = &host; s_n = me + 1;
    if (setjmp(host) == 0) {
        func_00671CA8__lifted(ctx);                                         /* status 0 path (or an error caught by a deeper frame) */
        s_n = me;
#ifdef TODS_PAUSE_QUIT_HOOK
        tods_pause_quit_after(ctx, &pq, pq_L);                              /* only on a normal return; a landing is an error */
#endif
        return;
    }
    /* Landed from longjmp: the guest registers are the ones setjmp saved, r3 = longjmp's value (never 0). */
    s_n = me;
    *ps3_ppu_dispatch_depth_slot() = depth0; *ps3_ppu_vcall_sp_slot() = vsp0;
    s_src = src0;
    if ((uint32_t)ctx->gpr[1] != (uint32_t)frame)
        fprintf(stderr, "[lua] longjmp landing: r1=0x%08X, expected 0x%08X\n", (uint32_t)ctx->gpr[1], (uint32_t)frame);
    ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);                            /* @0x671CE4 */
    ctx->gpr[9] = vm_read32(ctx->gpr[1] + 0x2A4);                           /* @0x671CE8 */
    { int64_t a = (int32_t)ctx->gpr[3]; uint32_t cr_val = ((a < 0) ? 8 : (a > 0) ? 4 : 2) | ((ctx->xer >> 31) & 1u);
      ctx->cr = (ctx->cr & ~0xFu) | cr_val; }                               /* cmpwi cr0,r3,0 @0x671CEC (the lifter keeps CR field n at bits 4n) */
    ctx->gpr[4] = vm_read32(ctx->gpr[1] + 0x2A8);                           /* @0x671CF0 */
    ctx->gpr[3] = vm_read32(ctx->gpr[1] + 0x2A0);                           /* @0x671CF4; bne -> 0x671D14 */
    ctx->gpr[8] = vm_read32(ctx->gpr[1] + 0x2A0);                           /* @0x671D14 L */
    ctx->gpr[10] = vm_read32(ctx->gpr[1] + 0x70);                           /* lj.previous */
    ctx->gpr[3] = (int64_t)(int32_t)vm_read32(ctx->gpr[1] + 0x294);         /* lj.status */
    vm_write32(ctx->gpr[8] + 0x50, (uint32_t)ctx->gpr[10]);                 /* L->errorJmp = lj.previous */
    ctx->gpr[7] = vm_read64(ctx->gpr[1] + 0x2C0);
    ctx->gpr[1] = ctx->gpr[1] + 0x2B0;
    ctx->lr = ctx->gpr[7];                                                  /* blr @0x671D34 */
    log_caught((uint32_t)ctx->gpr[8], (int32_t)ctx->gpr[3]);
}

/* @0x00826B34  longjmp(env, val) (libc) */
void func_00826B34(ppu_context* ctx)
{
    using namespace tods_lua_ljmp;
    const uint32_t jb = (uint32_t)((ctx->gpr[3] + 0xF) & ~0xFull);          /* addi r3,r3,15; rldicr r3,r3,0,59 @0x826B34 */
    func_00826B34__lifted(ctx);                                             /* guest registers from the buffer, r3 = val ? val : 1 */
    if (!enabled()) return;
    for (int i = s_n - 1; i >= 0; i--) {
        if (s_rec[i].jb != jb) continue;
        s_n = i + 1;                                                        /* frames above the target are gone */
        g_trampoline_fn = 0;
        longjmp(*s_rec[i].host, 1);
    }
    if (++s_unmatched <= 16)
        fprintf(stderr, "[lua] longjmp to jmp_buf 0x%08X with no host frame (records=%d) -- returns as before\n", jb, s_n);
}

#ifdef PPU_OVERRIDE_006670C8
/* @0x006670C8  the game's execute loader (this = r3, chunk text = r4): lua_load(L = this+4, reader @OPD 0x0087DA08, {text, done},
 * "execute") @0x0066711C, status to a callback, on 0 the call @0x00667168, result to the callback; returns !status. Wrapped only to know
 * the text while it runs (nesting kept per thread). */
void func_006670C8(ppu_context* ctx)
{
    using namespace tods_lua_ljmp;
    static int all = -1;
    static unsigned long long n_all = 0;
    if (all < 0) { const char* e = getenv("RC_LUA_SRC"); all = (e && e[0] == '1'); }
    const uint32_t src = (uint32_t)ctx->gpr[4], outer = s_src;
    if (all && ++n_all <= 4000) print_src("execute", src);
    s_src = src;
    func_006670C8__lifted(ctx);
    s_src = outer;
}
#endif
#endif

