# Roadmap

What is planned, in rough order. Done, in progress, planned. No dates: the order is firm, the timing is not.
The first release, v0.1.0, is a source-only pre-release, and the port is not playable yet.

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
each one is tried. The main-CPU (PPU) code of four of them is already lifted to C with the same tools, and A Crack in Time
builds into its own program and starts.

| Game | PS3 release | State |
|---|---|---|
| Ratchet & Clank Future: Quest for Booty | 2008 | SPU code lifted; PPU code not yet |
| Ratchet & Clank Future: A Crack in Time | 2009 | next: builds and starts (1.20 update); ends after a few seconds on a black screen |
| Ratchet & Clank: All 4 One | 2011 | PPU code of the 1.03 update lifted; not built yet |
| Ratchet & Clank: Full Frontal Assault | 2012 | PPU code of the 1.05 update lifted; not built yet |
| Ratchet & Clank: Into the Nexus | 2013 | PPU code of the 1.01 update lifted; not built yet |
| Ratchet & Clank Collection, Ratchet: Deadlocked HD (PS2 remasters) | 2012-2013 | last: a different engine; surveyed, nothing lifted yet |

## Not planned

- Distributing the game or any part of it: you always bring your own copy.
