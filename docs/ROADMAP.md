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

One at a time, after the first game works. The order is not fixed. The main-CPU (PPU) code of two of them is already
lifted to C with the same tools; neither is linked or running yet.

| Game | PS3 release | State |
|---|---|---|
| Ratchet & Clank Future: Quest for Booty | 2008 | planned |
| Ratchet & Clank Future: A Crack in Time | 2009 | PPU code lifted; not linked or running yet |
| Ratchet & Clank: All 4 One | 2011 | planned |
| Ratchet & Clank: Full Frontal Assault | 2012 | planned |
| Ratchet & Clank: Into the Nexus | 2013 | PPU code lifted; not linked or running yet |
| Ratchet & Clank Collection, Ratchet: Deadlocked HD (PS2 remasters) | 2012-2013 | last, under review |

## Not planned

- Distributing the game or any part of it: you always bring your own copy.
