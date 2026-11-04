# Changelog

All notable user-facing changes, newest first, in the style of [Keep a Changelog](https://keepachangelog.com/). The
project is a pre-release (early work, not playable yet): see [RELEASE-CRITERIA.md](docs/RELEASE-CRITERIA.md).

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

