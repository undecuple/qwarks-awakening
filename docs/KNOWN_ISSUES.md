# Known issues

Known problems of the current release, as measured. No game is playable from start to finish yet; see
[RELEASE-CRITERIA.md](RELEASE-CRITERIA.md) for what the states mean.

How the runs were made:

| | |
|---|---|
| Where | The Windows program under Proton on Linux, in every run named here |
| Hardware (2026-11-12 and 2026-11-13) | NVIDIA GeForce RTX 4060, driver 595.112.07, Intel Core i7-12700KF, GE-Proton11-7 |
| Scripted run | A script presses the pad's buttons at set times; the level runs below had no input at all |
| Hand play | A person with a pad; named as such |
| Frame rate | Frames presented to the screen per second |

## Installing and starting

### "Windows protected your PC"

The setup and the programs are not signed, so Windows SmartScreen may stop them on
the first start. Choose "More info", then "Run anyway".

### Antivirus warnings

Some antivirus programs flag new programs that have no signature. Compare the file with
`SHA256SUMS` on the release page. If it matches, restore the file or allow it.

### Not tested on Windows

The installer and the programs are built for Windows 10 and 11. Every run so far was
under Proton on Linux, the launcher's included.

## All games

### Sound not checked

The test runs have sound output turned off. Music, effects and voices have not been checked
in any game.

### No in-game settings menu yet

Settings are in the `.ini` files described in the README.

## *Tools of Destruction*

### Not played through

It can be played by hand from its first level (an earlier build, 2026-10-22). A scripted
new game plays about 11 minutes in Metropolis without a crash (2026-11-10). Nobody has played it to the end, so
later levels are untested as play.

### Frame rate below the 60 the game's display mode asks for

The game's code sets a display mode that shows up to
60 frames per second. In scripted runs with no input, the port started directly in each of 17 levels
(2026-11-12 and 2026-11-13, 300 seconds each). Over the last two minutes it showed about 12 to 59 frames per
second, counted each second, depending on the level. 11 of the 17 were under 30. Hand play of an earlier build ran
at about 37 frames per second in Fastoon (2026-10-22).

### Stalls at level loads

The first time a level loads, it stops while graphics pipelines are built. Metropolis's
first frame came after 12.6 and 15.8 s in two scripted runs with an empty shader cache (2026-11-12). The game's own
graphics-stall notice can show during them. Shorter pauses follow after the level starts. Three scripted runs of an
earlier build (2026-11-11) had three each: eight of them 3.2 to 3.7 s and one 8.4 s. A second start with the cache
of the first is much shorter (1.3 s in one run).

### Two levels stop drawing

The game's developer level option started Zordoom Prison and Meridian City directly
(scripted, no input, 2026-11-13). Both load and draw their first frames. Then the picture stops changing for the
rest of the run, while the game waits for a frame to be shown. The run logs point to the way the port runs the
game's SPU (co-processor) programs. A fix is in development; it is not yet tried on these two levels and not in this
release.

### Two script errors in the log

The game's scripts report two caught errors when its settings tables are read,
and again at each respawn. Both come from the game's data. The first is an empty row in one of its tables, which
gives the same error on the console. The second names a setting the game does not have; it is believed to fail the
same way on the console (not proven). Neither stops the game.

### Flicker in one cave

The picture flickers inside one cave on Fastoon.

### Pause menu quit entries

The quit entries of the pause menu are off unless `RC_PAUSE_QUIT=1`. They are drawn
below the menu's box, and QUIT TO MAIN MENU loads the last save again.

## Other games

None of the other games is playable yet. Each one's furthest point and where it stops are in the README's status
table and in [CHANGELOG.md](../CHANGELOG.md).
