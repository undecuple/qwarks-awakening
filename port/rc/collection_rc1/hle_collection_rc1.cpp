/* Ratchet & Clank Collection, Ratchet & Clank 1 (RC1.ppu.elf, disc BCUS98282, no update): HLE for imports the runtime leaves
 * unresolved. Built and run directly on RC1.ppu.elf (the launcher EBOOT's sys_game_process_exitspawn2 start path is not handled).
 * None registered yet (first build, 2026-11-09). The readiness card (2026-11-03) lists as unhandled after patches 7032/7033:
 * cellL10n L10nConvertStr (NID 0x931FF25A, call site func_00551A64), cellFsAioWrite, sys_prx_load_module_by_fd /
 * _on_memcontainer_by_fd (func_00577DEC). They fall back to the runtime's default return today. Add a handler here only after
 * reading its call site and the first boot's log. */
extern "C" void rc_hle_register(void)
{
}
