# Release criteria

Qwark's Awakening has no release date. Versions below 1.0.0 are previews: **v0.x = preview**, published as GitHub
pre-releases, each saying what is missing. **v1.0.0 = Ratchet & Clank Future: Tools of Destruction playable start to
finish at the game's own frame rate.** The first release that calls itself **playable** waits for every point of the
second list. Starting the program, or reaching a menu, does not count. This list, not a date, is what a release is
measured against.

## Playability states

The badges in the README show one of six states per game. Each is a measured run, dated in the catalogue, made with
the Windows program under Proton on Linux. A state is only claimed when a run reached it, and a game keeps the
highest state that was reached on the build in question.

| State | Meaning |
|---|---|
| Not built | the game's program is not built yet |
| Boots | the program starts and runs, but does not reach a menu or the first level in a usable form (black or white screen, or it stops) |
| Menus | the menus draw and take input, but a new game does not reach its first level |
| In game | a new game reaches a level and the player character moves, reached with scripted input (a script presses the pad's buttons at set times); not played through by hand |
| Playable | played from start to end by hand, with the known bugs listed |
| Completed | played to 100 % by hand |

## What v0.1.0 is

The first preview. It is **source code only**: no installer and no ready-made game program, because the program is
generated from the player's own copy of the game ([BUILDING.md](BUILDING.md)). It is early work and **not playable**:
*Tools of Destruction* starts and can be played by hand from its first level, with the known bugs listed in the
README, and has not been played through. The other games are not running.

## What v0.2.0 adds

A launcher preview for Windows, built by the release workflow from this repository and attached to the release with
its SHA-256: it lists the games and the files it finds, with Python included for its scanner. It does not build or
start a game yet. Still no installer and no game program.

## Gates for a v0.x tag (preview)

1. **CI green** on the commit that is tagged: every patch in `port/patches/` applies to the pinned ps3recomp, and the
   secret scan of the full history passes.
2. **Built and run from the tagged source:** the steps in [BUILDING.md](BUILDING.md) run on Linux, and the resulting
   program run under Proton reaches *Tools of Destruction*'s first level from a new game. The release notes name the
   build, the Proton version and the GPU and driver it ran on.
3. **Known issues listed** in the README, on the build that is tagged.
4. **One version string** in the exe's version block (`port/rc/CMakeLists.txt`), the README and `CITATION.cff`,
   checked to agree before the tag.
5. **Release notes from the changelog:** the tag's section of [CHANGELOG.md](../CHANGELOG.md); a release never goes
   out with generic text.
6. **SHA256SUMS** published beside any uploaded asset; the launcher download is built by the release workflow from the
   tagged commit, never uploaded by hand, and started once on Windows or under Proton before the draft is published.
7. **Tags are never moved**; a fix is a new tag (v0.1.1), a feature a new minor version (v0.2.0).
8. **Published as a pre-release** while the version is 0.x.

## Release criteria for v1.0.0 (playable)

1. **Played start to finish by a person:** *Tools of Destruction* on one save, by hand, with a dated playthrough log
   (planet, time, build, bugs met) published beside the release.
2. **A picture first:** the game's frames are drawn correctly on every planet and compared with the PS3 emulator
   RPCS3 on the same scene.
3. **Automated coverage:** scripted runs that load every planet on the release build, longer runs logging stalls and
   frame rate, a save loaded again after a quit, and the boot check.
4. **Speed:** the game's own frame rate in levels on the recommended hardware (see [requirements.md](requirements.md)),
   with steady frame pacing (1 % lows and frame-time deviation measured next to the average). Scripted and hand-play
   numbers are given separately in the release notes, each with its hardware and build.
5. **No known crash or soft-lock** on the path through the game; everything else in the published known-issues list.
6. **Tested where it is meant to run:** Windows 10/11 itself, not only Proton on Linux, each setup in the README's
   compatibility table with a named build.
7. **Getting the game files explained:** the build needs the files of your own copy of the game: the US disc
   (BCUS98127) as a folder (the folder that holds `PS3_GAME/`) and the decrypted `EBOOT.elf` of that copy. The tool
   that builds the program checks the serial and refuses anything else with a message saying which file is wrong.
   Where to start for making those files: the PS3 emulator RPCS3 and its documentation. This project gives no
   decryption procedure or keys, and does not link to or name where to get game files.
8. **Accurate label:** the README and release notes say what is translated, what is reimplemented and what is native,
   and how AI was used (README, "How AI was used").
9. **Maintenance shown, not promised:** a changelog, GitHub releases, and an issue template that asks for GPU, driver
   and the newest log file.
10. **No emulator tells:** release builds expose none of the runtime's debug overlays, hotkeys or diagnostic switches
    by default; only the port's own settings, styled like the game.

The numbers and goals here are targets for 1.0.0, not a statement about today: the README's status line and
compatibility table say what works now.
