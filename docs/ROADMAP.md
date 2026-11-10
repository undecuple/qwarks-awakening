# Roadmap

What is planned, in rough order. Done, in progress, planned. No dates: the order is firm, the timing is not.
Releases are pre-releases: v0.1.0 was source only, v0.2.0 adds a launcher preview for Windows. The port is not playable yet.

## 1. Ratchet & Clank Future: Tools of Destruction (first target)

| | |
|---|---|
| Done | Starts as a Windows program (tested under Proton on Linux only); a new game reaches the first level, Metropolis, and can be played by hand with a DualShock 4 pad; saving in game works; a saved game flies on to later planets; a scripted new game from an empty install plays about 11 minutes in Metropolis without a crash |
| In progress | The known bugs: washed-out glow, picture flicker in one cave, stalls during level loads |
| Planned | Playable start to finish: a full run through the game, saves loaded again after a quit, audio, input |
| Planned | A tool that builds the program on your machine from your own copy of the game; nothing of the game is shipped |
| Planned | Testing on real Windows (not run so far) and on macOS (not run so far) |

## 2. PC options

Started (checked in *Tools of Destruction*): the game window remembers its place and size, and the game keeps running
while the window is moved or resized; display and control settings can be set once for all games, with per-game
exceptions; FSR 1 upscaling is a setting. Planned after the first game is playable: more display settings (resolution, widescreen), rebindable controls in a
menu, gamepad prompts. Gameplay changes stay optional.

## 3. The other games

One at a time, after the first game works. The working order, from a survey of each game's program and files: A Crack
in Time, Quest for Booty, All 4 One, Into the Nexus, Full Frontal Assault, Deadlocked HD, the Collection; it may change as
each one is tried. The main-CPU (PPU) code of all of them is already lifted to C with the same tools, and every game
except Deadlocked HD builds into its own program, the Collection's three games each into one (how far each gets: the
table below and [FEATURE-PARITY.md](FEATURE-PARITY.md)).

| Game | PS3 release | State |
|---|---|---|
| Ratchet & Clank Future: Quest for Booty | 2008 | builds; a scripted new game from a fresh install reaches the ship deck, with about 90 seconds of scripted play without errors (one run); not playable |
| Ratchet & Clank Future: A Crack in Time | 2009 | next: builds (1.20 update); a scripted new game reaches its first save, then the screen goes black in most runs; not playable |
| Ratchet & Clank: All 4 One | 2011 | builds (1.03 update); scripted menus reach the first level, then the game stops; not playable |
| Ratchet & Clank: Full Frontal Assault | 2012 | builds and installs its data (1.05 update); the screen stays black; not playable |
| Ratchet & Clank: Into the Nexus | 2013 | builds (1.01 update); installs its data and shows its intro movie, then the lobby stays black; not playable |
| Ratchet & Clank Collection: Ratchet & Clank 1, 2 and 3 (PS2 remasters) | 2012 | each game builds into its own program; a scripted new game reaches its first level with the player under scripted control; not playable |
| Ratchet: Deadlocked HD (PS2 remaster) | 2013 | last: a different engine; PPU code lifted, not built yet |

What works in each game, feature by feature: [FEATURE-PARITY.md](FEATURE-PARITY.md).

## Not planned

- Distributing the game or any part of it: you always bring your own copy.
