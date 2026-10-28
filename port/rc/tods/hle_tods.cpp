/* Ratchet & Clank Future: Tools of Destruction (BCUS98127): HLE for imports the runtime leaves unresolved.
 * None registered yet. ps3recomp v0.15.0 + port/patches handles 112 of ToD's 118 imports (survey pass 2, plus patch
 * 6000); the rest fake CELL_OK and log once. Add a handler here only after reading its call site. */
extern "C" void rc_hle_register(void)
{
}
