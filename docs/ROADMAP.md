# Roadmap

What is planned, in rough order. Done, in progress, planned. No dates: the order is firm, the timing is not.
Releases are pre-releases: v0.1.0 was source only, v0.2.0 adds a launcher preview for Windows. The port is not playable yet.

## 1. Ratchet & Clank Future: Tools of Destruction (first target)

| | |
|---|---|
| Done | Starts as a Windows program (tested under Proton on Linux only); a new game reaches the first level, Metropolis, and can be played by hand with a DualShock 4 pad; saving in game works; a saved game flies on to later planets |
| In progress | The known bugs: too-strong reflections and a mirrored HUD prompt at Fastoon's spaceport, picture flicker in one cave, stalls during level loads |
| Planned | Playable start to finish: a full run through the game, saves loaded again after a quit, audio, input |
| Planned | A tool that builds the program on your machine from your own copy of the game; nothing of the game is shipped |
| Planned | Testing on real Windows (not run so far) and on macOS (not run so far) |

## 2. PC options

Planned after the first game is playable: display settings (resolution, window modes, widescreen), keyboard and
mouse with rebindable controls, gamepad prompts. Gameplay changes stay optional.

## 3. The other games

One at a time, after the first game works. The working order, from a survey of each game's program and files: A Crack
in Time, Quest for Booty, All 4 One, Into the Nexus, Full Frontal Assault, Deadlocked HD, the Collection; it may change as
each one is tried. The main-CPU (PPU) code of all of them is already lifted to C with the same tools, and every game
except Deadlocked HD and the Collection builds into its own program (how far each gets: the table below and
[FEATURE-PARITY.md](FEATURE-PARITY.md)).

| Game | PS3 release | State |
|---|---|---|
| Ratchet & Clank Future: Quest for Booty | 2008 | builds; installs its data and plays the opening cutscene of a new game; not playable |
| Ratchet & Clank Future: A Crack in Time | 2009 | next: builds, plays its intro and reaches a loading screen (1.20 update), about 17 frames per second; not playable |
| Ratchet & Clank: All 4 One | 2011 | builds and shows its title screen (1.03 update); not playable |
| Ratchet & Clank: Full Frontal Assault | 2012 | builds and installs its data (1.05 update); the screen stays black; not playable |
| Ratchet & Clank: Into the Nexus | 2013 | builds (1.01 update); an earlier build showed its intro, the current one stays black; not playable |
| Ratchet & Clank Collection, Ratchet: Deadlocked HD (PS2 remasters) | 2012-2013 | last: a different engine; PPU code lifted, not built yet |

What works in each game, feature by feature: [FEATURE-PARITY.md](FEATURE-PARITY.md).

## Not planned

- Distributing the game or any part of it: you always bring your own copy.
