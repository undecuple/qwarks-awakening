# Changelog

All notable user-facing changes, newest first, in the style of [Keep a Changelog](https://keepachangelog.com/). The
project is a pre-release (early work, not playable yet): see [RELEASE-CRITERIA.md](docs/RELEASE-CRITERIA.md).

## [0.3.0] - 2026-11-13 (pre-release)

Since 0.2.1 the launcher builds games, *Deadlocked HD* builds and boots, every other game was run again with scripted
input, the window and display settings are new, and the stall while a level's graphics pipelines are built is
shorter. Each game's line gives its playability state in this release (the states are defined in
[RELEASE-CRITERIA.md](docs/RELEASE-CRITERIA.md)); no game was played through by hand. Known issues of this release:
[docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).
Patch numbers name this repository's own fixes on top of ps3recomp, the files in
[`port/patches/`](port/patches/); they are not ps3recomp's numbering. Frame rates are frames presented to the screen
per second, in every game's line.

### [All games]
- Window placement: each game's window remembers its place and size, and the game keeps drawing and running while the
  window is dragged or resized. Checked in scripted runs of *Tools of Destruction*, 2026-11-10 (Windows program under
  Proton on Linux).
- FSR 1 upscaling as a setting: `upscale = fsr1` (with `upscale_sharpness` 0 to 2) in `display.ini`; off by default.
  Checked in a scripted run of *Tools of Destruction*, 2026-11-10.
- Shared settings: display and control keys in `shared/display.ini` and `shared/keybinds.ini` apply to every game, and a
  key in a game's own file wins. Tested on the host only, not yet in a game run.
- Fixed a lost fence in the runtime's graphics fence queue (patch 7042): the queue published a slot before its
  value had landed, so a game could wait for a fence that never arrived and the renderer timed out. This was why
  *Ratchet & Clank 1* froze after its intro. It is a general fix and is not specific to that game.
- Fixed false downloadable-content ownership: a game's check for a file that is not installed now answers "not
  installed", as the console does, instead of "licensed" (patch 7046). This removed the unlock of *A Crack in
  Time*'s pre-order content (a scripted run, 2026-11-10). The check reads the file's type, so a folder, pipe or socket
  with the file's name also counts as not installed (host test). *All 4 One* and *Full Frontal Assault* make the same
  call and get the new answer; what it changes in those two games was not checked.
- Shorter stalls while a level's graphics pipelines are built (patches 7050, 7051, 7052, 7054 and 7061): the
  renderer builds the pipeline a draw is waiting for first, starts building pipelines of draws it sees coming, sizes
  its build threads from the PC's processor count, and stores every pipeline it built in its cache file, so the next
  start can build them before they are needed. Measured on *Tools of Destruction* only, scripted, from a new game with
  an empty shader cache: the longest stall, at Metropolis's first frame, was about 26 to 32 s without these changes (single
  runs of earlier builds, 2026-11-10/11) and 12.6 and 15.8 s with them (two runs of a build of this release's code, 2026-11-12). With the cache
  of an earlier start it was 1.3 s (one run, 2026-11-11, before the changes). At exit the renderer now waits for
  builds still running before it frees the pipeline store (found in code review; the exit path was checked in one
  scripted run). Shorter stalls remain after the level starts.
- Two voice-chat event functions that games call were missing and now answer as on the console (patch 7048;
  host test only, no game run with it).
- Host tests in CI: every push and pull request runs the runtime's host tests against the patched recompiler tree (no
  game data and no GPU needed; a skipped test counts as a failure) and the release tools' tests.
- Playability badges in the README: one per game, each a dated, measured run, with the six states defined in
  [RELEASE-CRITERIA.md](docs/RELEASE-CRITERIA.md). The badges are written from the game catalogue, so they cannot drift
  from it.
- [AGENTS.md](AGENTS.md): rules for AI coding agents that work on this repository.
- New guide: [docs/ONLINE.md](docs/ONLINE.md), on online play (it does not work yet in any game) and how the port will
  connect to re-psn.

### [ToD] *Tools of Destruction*: in game
- Fixed the washed-out glow: a texture's gamma setting that decodes the alpha channel was ignored, so the environment
  cube's alpha stayed undecoded and glow came out too strong (patch 7049). The mean brightness of a
  Metropolis frame fell from 118 to 80 (one scripted run, 2026-11-10), and a 16-minute scripted run after the change
  behaved as before.
- A scripted new game from an empty install installs its data, plays the intro and reaches Metropolis; the player then
  moves, jumps, swings the wrench and fires for about 11 minutes. Two runs of 16 minutes each, 2026-11-10; no crash.
  Not played through, not played by hand in this release.
- Each level started directly, as a test: the game's own developer start option opens a level without the story
  before it. Started that way with no input and left for 300 seconds, 17 of the game's 19 level ids (Metropolis,
  Cobalia, Stratus City, Fastoon, the first space combat, the Imperial Fight Festival, Apogee Space Station, the pirate
  base, the second space combat, Rykan V, Sargasso, Iris, the third space combat, Kerchu City, Slag's fleet, the
  Cragmite ruins and the return to Fastoon) loaded, drew frames to the end of the run and did not crash, at about 12 to
  59 frames per second over the last two minutes, 11 of the 17 under 30 (scripted, 2026-11-12 and 2026-11-13). Zordoom
  Prison and Meridian City drew their first frames and then stopped drawing (see
  [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md)). This shows that those levels load and draw; it does not show that
  they can be played, and the game was not played through.

### [QfB] *Quest for Booty*: in game
- A scripted new game from a fresh install installs its data, plays the intro and reaches the ship deck, with about 90
  seconds of scripted play without errors (one run, 2026-11-10).

### [ACiT] *A Crack in Time*: boots
- A scripted new game (1.20 update) now gets past its first save and reaches its first level, `great_clock_a`, where
  the opening cutscenes play: 4 of 4 scripted runs, 2026-11-10 (patch 7053; the save dialog now takes the
  time the console's takes, which the game's menu waits for). In one run Clank is on screen in the level's corridor
  under scripted stick input; player control is not confirmed and the game is not playable.
- The false unlock of pre-order content is gone (see [All games]).

### [A4O] *All 4 One*: boots
- Scripted menus reach the first level (Luminopolis), shown with a white 3D view. The game then stops drawing: its
  sound job list does not finish. Not fixed.

### [FFA] *Full Frontal Assault*: boots
- Builds and installs its data; the screen stays black (a black frame is the game's own output). Not fixed.

### [Nexus] *Into the Nexus*: boots
- Builds, installs its data and shows its intro movie; the lobby stays black. Not fixed.

### [R&C 1], [R&C 2], [R&C 3] *Ratchet & Clank Collection*: in game
- Each game builds into its own program and a scripted new game reaches its first level with Ratchet under scripted
  control (2026-11-10, one build of each): *Ratchet & Clank 1* reaches Veldin (about three minutes, about 8 frames per
  second); *Ratchet & Clank 2* reaches Aranos (about a minute at 41 to 65 frames per second); *Ratchet & Clank 3*
  reaches Veldin (about three minutes at about 23 frames per second). No crash. These are scripted figures.

### [DLHD] *Ratchet: Deadlocked HD*: builds, boots to its main menu
- Builds for the first time and, from an installed copy of the game, shows its warning screen and main menu at 62 to
  98 frames per second (one scripted run); a scripted new game then plays its in-engine intro and stops before the
  first level (2026-11-10). Not playable.

### [Launcher]
- The launcher builds games: Build, Rebuild, Play and Cancel per game, with the build's stage and progress, the last
  build's result and the game's log. The first-run engine finds your copy and the EBOOT, lifts the game and compiles
  it on your PC. Checked on Linux, 2026-11-10: *Tools of Destruction* and *Ratchet & Clank 2* built from a decrypted
  EBOOT; the programs equal those of the manual build except for build paths. Not tried on Windows.
- Every game the launcher can build offers Build, with a note on how far that game gets in this release.
- Release tags per part: the launcher's release, `launcher-v0.3.0`, carries the downloads; each game with an entry
  above has its own release with notes only (`tods-v0.3.0`, `qfb-v0.3.0`, `acit-v0.3.0`, `a4o-v0.3.0`, `ffa-v0.3.0`,
  `nexus-v0.3.0`, `rc1-v0.3.0`, `rc2-v0.3.0`, `rc3-v0.3.0`, `dlhd-v0.3.0`). Earlier releases were tagged `v0.1.0` to
  `v0.2.1`.
- Two downloads per launcher release, with a SHA-256 file for each and a `SHA256SUMS` list: the bare launcher
  (`Qwarks-Awakening-<version>-launcher-windows-x64.zip`), which asks before it fetches the build files on your first
  Build, and the full build zip (`Qwarks-Awakening-<version>-build-windows-x64.zip`), which holds the same files for an
  offline build. A third file, `Qwarks-Awakening-<version>-launcher-symbols.zip`, holds the launcher's own debug
  information; the launcher fetches it only when you press its button in Settings.
- A game built from an older release shows "Rebuild" only when its build inputs changed; saves and settings are never
  touched.
- Online settings (method, server and online name) in the launcher. They only write a settings file; no game reads it
  yet.

### Test setup
- The runs of 2026-11-12 and 2026-11-13 named above used the Windows program under GE-Proton11-7 on Linux, an NVIDIA
  GeForce RTX 4060 with driver 595.112.07 and an Intel Core i7-12700KF, and a build of this release's source (the same
  code apart from the version number). No run was made on Windows itself.

## [0.2.1] - 2026-11-04 (pre-release)

### Added
- [docs/FEATURE-PARITY.md](docs/FEATURE-PARITY.md): for each game, the console version's features and whether each
  works in the port, with an estimate per game.
- *Tools of Destruction*: mouse look, off by default (`[mouse] look = on` in the game's `keybinds.ini`). Moving the
  mouse turns the camera left and right and tilts it up and down within the game's own limits; the pointer is hidden in
  play and shown in menus. Checked in scripted runs only; not tried by hand yet.

### Changed
- *Tools of Destruction*: too-strong glow and reflections are fixed. A texture no longer reads from the picture being
  drawn (this also removes the HUD prompt mirrored into the scene at Fastoon's spaceport), and reflection maps are
  read with the right layout (the white sheen on floors and pillars is gone). Checked in scripted runs; not tried by
  hand yet.
- The later games run their SPU job code translated instead of interpreted, and an SPU task scheduler that is not
  translated yet no longer keeps processor cores busy while it waits. Scripted runs, 2026-11-04, frames per second
  over the same part of each run: *A Crack in Time* about 3 to 17, now past its intro to a loading screen; *Into the
  Nexus* about 2 to 28, but its screen is black; *All 4 One* about 3 to 13, title screen.
- *Quest for Booty* is built for the first time. On a first start it installs its game data; a new game writes its
  save and plays the prologue's opening cutscene (scripted run). The game-data check now tells a game whether its data
  is new, and the web-browser functions it calls at start answer as on the console.
- *All 4 One* and *Full Frontal Assault* are built for the first time: *All 4 One* shows its title screen; *Full
  Frontal Assault* installs its data, but its screen stays black.
- *Tools of Destruction* was checked with these changes on 2026-11-04, five minutes each under GE-Proton11-7 on an
  NVIDIA GeForce RTX 4060 (driver 595.112.07), scripted: a first start on an empty drive installs the game data and
  shows "Setup complete"; a new game reaches Metropolis and goes on to Cobalia; a saved game loads into Fastoon. None
  of the runs crashed.

## [0.2.0] - 2026-11-03 (pre-release)

### Added
- A log file per game: each game writes `logs/<game>/` beside its program, with a header (port version, game and
  title ID, Windows or Wine/Proton version, CPU, memory, settings in use) and the reason the game ended; a crash also
  leaves a crash report and a minidump there. Your user name and home folder are removed from
  them. Checked with *Tools of Destruction* under Proton on 2026-11-03: a five-minute run and a forced crash.
- A launcher preview in `launcher/` (SDL3 and Dear ImGui): it lists all eight games with their state, shows which of your
  files it found, and has screens for choosing the folders by hand (seen in screenshots on Linux; not tried on Windows
  yet). It does not build or start a game yet.
- The launcher preview as a Windows x64 download: the release page carries
  `Qwarks-Awakening-0.2.0-launcher-windows-x64.zip`, built from this repository by the release workflow on GitHub. It
  holds the launcher, SDL3, its fonts, the file-finding tool with the Python that runs it (nothing else to install)
  and the licences; no game files and no game program. On the first start it looks in RPCS3's usual folders and
  writes its game list.
- Offline behaviour for the network and PlayStation Network functions the later games import (HTTP, SSL, network
  sockets and status, PSN sign-in, friends, scores, matching, store): they answer as a console with no network
  connection does. Host-tested; no game has run with them yet.
- Issue forms ask which game a report is about.
- Keyboard controls for *Tools of Destruction*, with default keys for every pad button, played by hand under Proton on
  2026-11-03. Mouse look is in the code but off by default until it has been tested.
- Settings per game: each game keeps its own `keybinds.ini`, `display.ini` and `settings.ini` in its own folder, so
  keys and display choices for one game do not change another.
- *Tools of Destruction*: QUIT TO DESKTOP and QUIT TO MAIN MENU entries at the end of the pause menu's OPTIONS page,
  off unless `RC_PAUSE_QUIT=1` is set. Quitting to the desktop works in a scripted run. Known problems, which are why it
  is off: the two entries are drawn below the menu's box and the label shown can name the other entry, and QUIT TO MAIN
  MENU loads the last save again instead of stopping at the title screen.

### Changed
- *A Crack in Time* (1.20 update) now starts and shows its intro: Qwark's cinematic is drawn on screen. It runs very
  slowly (about one frame per second) and is not playable. Three changes got it there: a lock-free queue push between
  the main processor and the SPU tasks now wakes the waiting task; an error raised inside the game's Lua scripts now
  returns to the script's error handler instead of ending the game after seven seconds; and an SPU task scheduler
  that the game ships as SPU code, and that is not translated yet, now runs in the runtime's SPU interpreter.
- *Into the Nexus* (1.01 update) is built for the first time and shows its intro, the Polaris News broadcast with
  subtitles, at the same slow speed; not playable.
- *Tools of Destruction* was checked with these changes: a new game and a saved game, five minutes each, without a
  crash.
- The PPU code of *Quest for Booty*, *Ratchet: Deadlocked HD* and the *Ratchet & Clank Collection*'s programs is
  lifted, and *Quest for Booty*'s SPU programs are found and translated; none of these is built yet.

## [0.1.0] - 2026-11-03 (pre-release)

### Added
- First public pre-release, source code only: the port of *Ratchet & Clank Future: Tools of Destruction* (US disc,
  BCUS98127) by static recompilation on ps3recomp v0.18.0, with this port's patches in `port/patches/`, built on Linux
  with Nix from your own copy of the game ([docs/BUILDING.md](docs/BUILDING.md)). No game files and no game program
  are included.
- *Tools of Destruction* starts as a Windows program and can be played by hand with a controller: a new game starts in
  its first level, Metropolis, and from a saved game the ship flies on to later planets such as Cobalia. Checked under
  GE-Proton11-7 on Linux with an NVIDIA GeForce RTX 4060 (driver 595.104.2) on 2026-10-22; hand play in
  Fastoon averaged about 37 frames per second over one 147-second session; scripted frame rate not measured.
  The source as released (ps3recomp v0.18.0) was checked on 2026-11-03 under GE-Proton11-7 with the same GPU (driver
  595.112.07): a new game reaches Metropolis and a saved game loads into Fastoon, five minutes each without a crash.
- A DualShock 4 pad (through SDL3), Alt+Enter fullscreen and in-game saving work under Proton.
- The PPU code of *A Crack in Time* and *Into the Nexus* is lifted with the same tools; neither game is linked or
  running yet.

