/* ENHANCEMENT: two entries in ToD's pause menu, OPTIONS page, after LOAD:
 *   "QUIT TO MAIN MENU"  leaves the pause menu and asks the game for its front-end state (the state its legal screens hand over to)
 *   "QUIT TO DESKTOP"    the port's clean shutdown, the same path as closing the window (port/rc/main.cpp rc_exit_on_window_close);
 *                        nothing new is saved.
 * Switch: RC_PAUSE_QUIT=1 (default off until it has been tried by hand). RC_PAUSE_QUIT_TITLE=restart makes the title entry restart the exe
 * instead (clean exit + the same command line), the fallback if the in-game return proves unsound.
 * Included by tods_ppu_overrides.cpp before tods_lua_ljmp.inc; the hook runs in the luaD_rawrunprotected override there.
 * Decoded facts and the design: the RE notes section 19 (ToD's pause menu).
 *
 * How the pause menu is built (read off packed/game/global_cached.psarc entry 86, /built/anark/pause/built.dat, an Anark presentation
 * whose behaviours are compiled Lua 5.0 chunks; names and shapes only): the "ListUpdater" behaviour chunk installs methods on the global
 * `self` (the behaviour instance): onInitialize sets MAIN/OPTIONS/EXTRAS/ARENA/SPACE_MENU_INDEX = 1..5 and calls self:SetupMenus(),
 * which builds self.menuData[menu][i] = { text, callback }; ActivateItem shows menuItem[1] through SetLocalizedText (a number = a text
 * tag of the presentation's table, a string = shown as is: LOC.SetText), handleSelect calls menuItem[2](self), getNumListItems is
 * table.getn(menuData[current]). OPTIONS has 6 entries (GAME OPTIONS, CONTROLS, MOVES, HELP, SAVE, LOAD); the main page has 8 and the
 * arena/space pages 9, so the list shows at least 9 rows and two more OPTIONS rows fit.
 *
 * The hook: the presentation loads that chunk through luaL_loadbuffer @0x0066BAC0 -> lua_load @0x00668D58 -> luaD_protectedparser
 * @0x00671DA8 -> luaD_rawrunprotected(L, f_parser, &SParser) (f_parser OPD 0x0087E038, TOC-0x5E68 @0x671DD0). At that call the ZIO
 * (SParser+0) already holds the whole buffer (luaZ_lookahead @0x0068BDA0 filled it: n +0, p +4), so the chunk is recognised by its
 * names (SetupMenus, OPTIONS_MENU_INDEX, request_save_interface). After the parse returns 0 the new function F is on the Lua stack;
 * the hook runs lua_dostring @0x0066C520 on kMaker (pushes `maker`), swaps the two top stack slots, and calls lua_pcall @0x006696C8
 * (L, 1, 1, 0), so maker(F) replaces F with a wrapper: it runs F with F's environment set to its own (the presentation's `self`
 * stays visible), then wraps self.SetupMenus to append the two rows to menuData[OPTIONS_MENU_INDEX]. The rows' callbacks signal the
 * host by compiling a one-line comment (loadstring "--rc_pause_quit:<what>"), which comes through the same parser hook.
 * Game-side Lua natives used: exit_pause_menu (OPD 0x0085A9E8 -> 0x00035188, the game's own "resume"), loadstring (Lua 5.0 base).
 * Title return: the game's state machine (0x0060AF88) switches between four module sets read off the table at 0x1006330C (id 0: 1
 * module; id 1: 2; id 2: 3, adds module 0x1035E1F0; id 3: 5, the in-game set). Requests go through 0x0060AEE8 (stores the id in the
 * pending word 0x100E4F18, TOC-0x6DAC); the legal sequence asks for 2 when it ends (@0x0039717C), start_game asks for 3 (@0x0039B834).
 * ASSUMPTION: id 2 is the front end (its set adds 0x1035E1F0 to the legal set; start_game leaves it for 3), and leaving 3 for 2 unloads
 * the level the way the module exits do for any switch. Run tods-pause-c (2026-11-03): after the write the game reloads metropolis,
 * level_transitions and fastoon_return with no input and is back in the level from the save. It does not stop at the title, so the
 * state-2 path is a reload, not a title return; RC_PAUSE_QUIT_TITLE=restart is the path to the real title (not run yet).
 * Also seen: the OPTIONS slide's box holds 6 rows; rows 7/8 draw overlapping under it and the label shown does not reliably match
 * the focused row (the RE notes s.19). Both keep the switch default off. */
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);   /* ppu_loader.cpp:2158 (also declared in tods_lua_ljmp.inc) */

extern "C" volatile int g_rc_quit_request;   /* port/rc/main.cpp: 1 = quit to desktop (the frame loop's clean exit), 2 = restart */

namespace tods_pause_quit {
static const uint32_t kFParserOpd   = 0x0087E038u;   /* f_parser, read through TOC-0x5E68 @0x00671DD0 (slot 0x008A9FF4) */
static const uint32_t kLuaTocC      = 0x008AFE5Cu;   /* TOC of the Lua block (0x0066xxxx-0x0068xxxx, libc) */
static const uint32_t kPendingState = 0x100E4F18u;   /* game-state request word, [TOC-0x6DAC] in 0x0060AEE8 */
static const uint32_t kFrontEndState = 2u;           /* @0x0039717C (legal sequence done) */

/* maker(f): Lua 5.0 source. f is the freshly parsed ListUpdater chunk (or an error value, returned unchanged). */
static const char kMaker[] =
    "return function(f)\n"
    "  if type(f) ~= 'function' then return f end\n"
    "  return function()\n"
    "    if getfenv and setfenv then setfenv(f, getfenv(1)) end\n"
    "    f()\n"
    "    local s = self\n"
    "    if s ~= nil and s.SetupMenus ~= nil then\n"
    "      local setup = s.SetupMenus\n"
    "      s.SetupMenus = function(t)\n"
    "        setup(t)\n"
    "        local m = t.menuData[t.OPTIONS_MENU_INDEX]\n"
    "        local n = table.getn(m)\n"
    "        m[n + 1] = { 'QUIT TO MAIN MENU', function(u) exit_pause_menu() loadstring('--rc_pause_quit:title') end }\n"
    "        m[n + 2] = { 'QUIT TO DESKTOP', function(u) loadstring('--rc_pause_quit:desktop') end }\n"
    "      end\n"
    "      loadstring('--rc_pause_quit:installed')\n"
    "    end\n"
    "  end\n"
    "end\n";

static int enabled()
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("RC_PAUSE_QUIT"); on = (e && e[0] == '1'); }
    return on;
}

static int title_restart()
{
    static int r = -1;
    if (r < 0) { const char* e = getenv("RC_PAUSE_QUIT_TITLE"); r = (e && !strcmp(e, "restart")); }
    return r;
}

/* Does guest [p, p+n) contain the NUL-terminated name? (Lua 5.0 dumps strings with their NUL.) */
static bool has_name(uint32_t p, uint32_t n, const char* name)
{
    const uint32_t k = (uint32_t)strlen(name) + 1;
    if (n < k) return false;
    for (uint32_t i = 0; i + k <= n; i++) {
        if (vm_read8(p + i) != (uint8_t)name[0]) continue;
        uint32_t j = 1;
        while (j < k && vm_read8(p + i + j) == (uint8_t)name[j]) j++;
        if (j == k) return true;
    }
    return false;
}

enum Kind { kNone = 0, kListUpdater = 1, kSignal = 2 };
static const char kSig[] = "--rc_pause_quit:";

/* Before the parse: what is being compiled? ud = SParser {ZIO* z +0; Mbuffer +4; int bin +0xC}; ZIO {n +0; p +4; ...}. */
static Kind classify(uint32_t f, uint32_t ud, char* what, size_t what_n)
{
    if (f != kFParserOpd || !ud) return kNone;
    const uint32_t z = vm_read32(ud);
    if (!z) return kNone;
    const uint32_t n = vm_read32(z), p = vm_read32(z + 4);
    if (!p || !n || n > (1u << 20)) return kNone;
    if (vm_read8(p) == 0x1B) {                                              /* precompiled: "\033Lua" */
        return (has_name(p, n, "SetupMenus") && has_name(p, n, "OPTIONS_MENU_INDEX") && has_name(p, n, "request_save_interface"))
                   ? kListUpdater : kNone;
    }
    const uint32_t k = (uint32_t)sizeof(kSig) - 1;
    if (n <= k || n > k + 32) return kNone;
    for (uint32_t i = 0; i < k; i++) if (vm_read8(p + i) != (uint8_t)kSig[i]) return kNone;
    size_t o = 0;
    for (uint32_t i = k; i < n && o + 1 < what_n; i++) what[o++] = (char)vm_read8(p + i);
    what[o] = 0;
    return kSignal;
}

static void drain(ppu_context* ctx) { while (g_trampoline_fn) { void (*t)(void*) = g_trampoline_fn; g_trampoline_fn = 0; t(ctx); } }

/* After a successful parse of the ListUpdater chunk: F at L->top - 8. Replace it with maker(F). */
static void wrap(ppu_context* ctx, uint32_t L)
{
    const ppu_context saved = *ctx;
    const uint32_t top0 = vm_read32(L + 0x08);
    const uint32_t f_tt = vm_read32(top0 - 8), f_v = vm_read32(top0 - 4);
    if (f_tt != 6) { fprintf(stderr, "[pause-quit] top of stack is not the parsed function (tt=%u): menu left as is\n", f_tt); return; }
    /* Guest scratch below the caller's frame: the source text, then a frame for the calls. */
    const uint64_t sp0 = ctx->gpr[1];
    const uint32_t len = (uint32_t)sizeof(kMaker);
    const uint32_t src = (uint32_t)((sp0 - 0x100 - len) & ~0xFull);
    const uint64_t sp = (src - 0x200) & ~0xFull;
    for (uint32_t i = 0; i < len; i++) vm_write8(src + i, (uint8_t)kMaker[i]);
    vm_write64(sp, sp0);                                                    /* back chain, as stdu leaves it */
    ctx->gpr[1] = sp; ctx->gpr[2] = kLuaTocC;
    ctx->gpr[3] = L; ctx->gpr[4] = src; ctx->lr = 0x00671E04;              /* lua_dostring(L, src) -> maker on top */
    func_0066C520(ctx); drain(ctx);
    int32_t st = (int32_t)ctx->gpr[3];
    const uint32_t top1 = vm_read32(L + 0x08);
    if (st != 0 || top1 != top0 + 8) {
        fprintf(stderr, "[pause-quit] maker did not compile (status %d, top %+d): menu left as is\n", st, (int)(top1 - top0));
        if (top1 > top0) vm_write32(L + 0x08, top0);                        /* drop what it left; F stays on top */
        *ctx = saved;
        return;
    }
    for (int i = 0; i < 2; i++) {                                           /* lua_insert(L, -2) by hand: [F, maker] -> [maker, F] */
        const uint32_t a = vm_read32(top1 - 16 + 4 * i), b = vm_read32(top1 - 8 + 4 * i);
        vm_write32(top1 - 16 + 4 * i, b); vm_write32(top1 - 8 + 4 * i, a);
    }
    ctx->gpr[1] = sp; ctx->gpr[2] = kLuaTocC;
    ctx->gpr[3] = L; ctx->gpr[4] = 1; ctx->gpr[5] = 1; ctx->gpr[6] = 0; ctx->lr = 0x00671E04;
    func_006696C8(ctx); drain(ctx);                                         /* lua_pcall(L, 1, 1, 0): maker(F) -> wrapper */
    st = (int32_t)ctx->gpr[3];
    const uint32_t top2 = vm_read32(L + 0x08);
    if (st != 0 || top2 != top0 || vm_read32(top2 - 8) != 6) {              /* keep the game's chunk if the wrap failed */
        fprintf(stderr, "[pause-quit] wrapping failed (status %d): the game's own menu is kept\n", st);
        vm_write32(top0 - 8, f_tt); vm_write32(top0 - 4, f_v); vm_write32(L + 0x08, top0);
    } else {
        fprintf(stderr, "[pause-quit] pause-menu list chunk wrapped (L=0x%08X)\n", L);
    }
    *ctx = saved;                                                           /* r3 = 0, the parse status the caller expects */
}

static void on_signal(const char* what)
{
    if (!strcmp(what, "installed")) {
        fprintf(stderr, "[pause-quit] OPTIONS page: QUIT TO MAIN MENU and QUIT TO DESKTOP added\n");
    } else if (!strcmp(what, "desktop")) {
        fprintf(stderr, "[pause-quit] QUIT TO DESKTOP selected: clean shutdown, nothing saved\n");
        g_rc_quit_request = 1;
    } else if (!strcmp(what, "title")) {
        if (title_restart()) {
            fprintf(stderr, "[pause-quit] QUIT TO MAIN MENU selected: restarting the game (RC_PAUSE_QUIT_TITLE=restart)\n");
            g_rc_quit_request = 2;
        } else {
            const uint32_t old = vm_read32(kPendingState);
            vm_write32(kPendingState, kFrontEndState);                      /* what 0x0060AEE8(2) does */
            fprintf(stderr, "[pause-quit] QUIT TO MAIN MENU selected: game state %u requested (pending word was 0x%08X)\n", kFrontEndState, old);
        }
    } else {
        fprintf(stderr, "[pause-quit] unknown signal '%s'\n", what);
    }
}
} // namespace tods_pause_quit

/* Called by the luaD_rawrunprotected override (tods_lua_ljmp.inc) around the lifted body. */
struct TodsPauseQuitCall { int kind; char what[40]; };
static inline void tods_pause_quit_before(ppu_context* ctx, TodsPauseQuitCall* c)
{
    c->kind = tods_pause_quit::kNone; c->what[0] = 0;
    if (!tods_pause_quit::enabled()) return;
    c->kind = tods_pause_quit::classify((uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5], c->what, sizeof c->what);
}
static inline void tods_pause_quit_after(ppu_context* ctx, const TodsPauseQuitCall* c, uint32_t L)
{
    if (c->kind == tods_pause_quit::kNone || (int32_t)ctx->gpr[3] != 0) return;
    if (c->kind == tods_pause_quit::kListUpdater) tods_pause_quit::wrap(ctx, L);
    else tods_pause_quit::on_signal(c->what);
}
