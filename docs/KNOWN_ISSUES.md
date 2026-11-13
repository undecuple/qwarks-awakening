# Known issues

Known problems of the current release, as measured. Every run named here is the Windows program under Proton on Linux;
the runs of 2026-11-12 and 2026-11-13 used GE-Proton11-7, an NVIDIA GeForce RTX 4060 with driver 595.112.07 and an
Intel Core i7-12700KF. In a scripted run a script presses the pad's buttons at set times (the level runs below had no
input at all); hand play is named as such. Frame rates are frames presented to the screen per second. No game is
playable from start to finish yet: see [RELEASE-CRITERIA.md](RELEASE-CRITERIA.md) for what the states mean.

## All games

- **Not tested on Windows.** The programs build for Windows 10/11, but every run so far was under Proton on Linux; the
  launcher was also checked on Linux only.
- **Sound not checked.** The test runs have sound output turned off, so music, effects and voices have not been
  checked in any game.
- No in-game settings menu yet: settings are in the `.ini` files described in the README.

## *Tools of Destruction*

- **Not played through.** It can be played by hand from its first level (an earlier build, 2026-10-22) and a scripted
  new game plays about 11 minutes in Metropolis without a crash (2026-11-10), but nobody has played it to the end, so
  later levels are untested as play.
- **Frame rate below the 60 the game's display mode asks for.** The game's code sets a display mode that shows up to
  60 frames per second. Started directly in each of 17 levels with no input (scripted, 2026-11-12 and 2026-11-13, 300
  seconds each), the port showed about 12 to 59 frames per second (counted each second) over the last two minutes,
  depending on the level; 11 of the 17 were under 30. Hand play of an earlier build ran at about 37 frames per second
  in Fastoon (2026-10-22).
- **Stalls at level loads.** The first time a level loads, it stops while graphics pipelines are built (12.6 and
  15.8 s at Metropolis's first frame in two scripted runs with an empty shader cache, 2026-11-12); the game's own
  graphics-stall notice can show during them. Shorter pauses follow after the level starts: three in each of three
  scripted runs of an earlier build (2026-11-11), eight of them 3.2 to 3.7 s and one 8.4 s. A second start with the
  cache of the first is much shorter (1.3 s in one run).
- **Two levels stop drawing.** Started directly with the game's developer level option (scripted, no input,
  2026-11-13), Zordoom Prison and Meridian City load and draw their first frames, then the picture stops changing for
  the rest of the run while the game waits for a frame to be shown. The run logs point to the way the port runs the
  game's SPU (co-processor) programs; a fix is in development, not yet tried on these two levels, and not in this
  release.
- **Two script errors in the log.** The game's scripts report two caught errors when its settings tables are read, and
  again at each respawn. Both come from the game's data: the first is an empty row in one of its tables, which gives
  the same error on the console; the second names a setting the game does not have, which is believed to fail the
  same way on the console (not proven). Neither stops the game.
- The picture flickers inside one cave on Fastoon.
- The quit entries of the pause menu (off unless `RC_PAUSE_QUIT=1`) are drawn below the menu's box, and QUIT TO MAIN
  MENU loads the last save again.

## Other games

None of the other games is playable yet. Each one's furthest point and where it stops are in the README's status
table and in [CHANGELOG.md](../CHANGELOG.md).
