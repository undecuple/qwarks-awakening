# Roadmap

What is planned, in rough order. Done, in progress, planned. No dates: the order is firm, the timing is not.
Releases are pre-releases: v0.1.0 was source only, v0.2.0 added a launcher preview for Windows, v0.3.0 adds a launcher that builds the games from your own
copy. The port is not playable yet.

## 1. Ratchet & Clank Future: Tools of Destruction (first target)

| | |
|---|---|
| Done | Starts as a Windows program (tested under Proton on Linux only); a new game reaches the first level, Metropolis, and can be played by hand with a DualShock 4 pad; saving in game works; a saved game flies on to later planets; a scripted new game from an empty install plays about 11 minutes in Metropolis without a crash |
| In progress | The known bugs: picture flicker in one cave, stalls during level loads |
| Planned | Playable start to finish: a full run through the game, saves loaded again after a quit, audio, input |
| Done | A launcher that builds the program on your machine from your own copy of the game, nothing of the game shipped: *Tools of Destruction* and *Ratchet & Clank 2* built from a decrypted EBOOT on Linux, 2026-11-10 (not tried on Windows) |
| Done | One full download with everything the build needs, and a first-start download of the build files that asks first: in v0.3.0, built by the release workflow (not tried on Windows) |
| Planned | Testing on real Windows (not run so far) and on macOS (not run so far) |

## 2. PC options

Started (checked in *Tools of Destruction*): the game window remembers its place and size, and the game keeps running
while the window is moved or resized; display and control settings can be set once for all games, with per-game
exceptions; FSR 1 upscaling is a setting. Planned after the first game is playable: more display settings (resolution, widescreen), rebindable controls in a
menu, gamepad prompts. Gameplay changes stay optional.

## 3. The other games

One at a time, after the first game works. The working order, from a survey of each game's program and files: A Crack
in Time, Quest for Booty, All 4 One, Into the Nexus, Full Frontal Assault, Deadlocked HD, the Collection; it may change as
each one is tried. The main-CPU (PPU) code of all of them is lifted to C with the same tools, and every game builds
into its own program, the Collection's three games each into one (how far each gets: the
table below and [FEATURE-PARITY.md](FEATURE-PARITY.md)).

| Game | PS3 release | State |
|---|---|---|
| Ratchet & Clank Future: Quest for Booty | 2008 | builds; a scripted new game from a fresh install reaches the ship deck, with about 90 seconds of scripted play without errors (one run); not playable |
| Ratchet & Clank Future: A Crack in Time | 2009 | next: builds (1.20 update); a scripted new game reaches its first level (great_clock_a) and plays its opening cutscenes, in 4 of 4 runs; player control there is not confirmed; not playable |
| Ratchet & Clank: All 4 One | 2011 | builds (1.03 update); scripted menus reach the first level, then the game stops; not playable |
| Ratchet & Clank: Full Frontal Assault | 2012 | builds and installs its data (1.05 update); the screen stays black; not playable |
| Ratchet & Clank: Into the Nexus | 2013 | builds (1.01 update); installs its data and shows its intro movie, then the lobby stays black; not playable |
| Ratchet & Clank Collection: Ratchet & Clank 1, 2 and 3 (PS2 remasters) | 2012 | each game builds into its own program; a scripted new game reaches its first level with the player under scripted control; not playable |
| Ratchet: Deadlocked HD (PS2 remaster) | 2013 | last: a different engine; builds; a scripted new game plays its intro, then stops before the first level; not playable|

What works in each game, feature by feature: [FEATURE-PARITY.md](FEATURE-PARITY.md).

## 4. Native Linux build with Vulkan

Planned, not started. Today the Linux way is the Windows program under Proton. The goal is a native Linux program for
every game, from the same ported code, drawing with Vulkan instead of Direct3D 12. Windows with Direct3D 12 stays the
first target, and Proton stays the Linux way until the native build matches it.

## 5. Online play through re-psn

Planned, not started in any game: [ONLINE.md](ONLINE.md). The games with online modes on the console are *All 4 One*,
*Full Frontal Assault* and *Deadlocked HD*; the order is not set. Off by default, one online method at a time, and
nothing connects unless you choose it.

## 6. Co-op for the single-player games (optional)

If it proves possible: an optional co-op mode in the style of *All 4 One* for the single-player games, where a second
player takes a second character. It would be opt-in, a change to the game that is off by default, and is only a plan
until the first prototype shows it can work.

## Not planned

- Distributing the game or any part of it: you always bring your own copy.
