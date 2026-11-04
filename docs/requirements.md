# Requirements

What you need to build and run the port of *Ratchet & Clank Future: Tools of Destruction*. Kept current as the port
changes. The game starts and can be played by hand from its first level, with known bugs; it has not been played
through and is not playable yet. Every test run so far was the Windows program under Proton on Linux. Nothing below is
a promise of a playable game.

## What you need from your own copy of the game

| Item | Required | Details |
|---|---|---|
| **Ratchet & Clank Future: Tools of Destruction disc, US release** | yes | Title ID **BCUS98127** (disc version, no update), as a decrypted folder that holds `PS3_GAME/`. Other regions and IDs are untested. You make your own dump; the port ships none of the game. |
| **Decrypted executable** | yes | The game's `EBOOT.BIN` is encrypted; the port needs `EBOOT.elf`, its decrypted form, made from your own copy. This project gives no decryption tools or instructions; [RPCS3](https://rpcs3.net) and its documentation are the place to start for dumping questions. |
| **PS3 system firmware** | no | Not needed; the PS3 system libraries are reimplemented by the runtime. |
| **PlayStation Network account** | no | Online play is not supported. |

## Hardware

| | Needed | Basis |
|---|---|---|
| **OS** | To build: Linux with Nix (flakes enabled). To run: the Windows program under Proton on Linux is the only tested way | Windows itself and macOS: not run (see [BUILDING.md](BUILDING.md)). A native Linux build is not planned yet |
| **GPU** | Direct3D 12 support | Tested on one setup only: NVIDIA GeForce RTX 4060, driver 595.112.07, under GE-Proton11-7 (2026-11-03; earlier runs 2026-10-22 with driver 595.104.2). Which other GPUs work is not established |
| **Controller** | A gamepad | Tested with a DualShock 4, read through SDL3. Keyboard and mouse: not checked on this game |
| **RAM** | 16 GB or more to build | Each lifted source chunk can take 1 to 2 GB to compile; the build script picks the number of parallel jobs from free memory. Memory needed to play: not measured |
| **Disk** | The game dump plus several GB for the lifted sources and build | Exact figures: not measured |
| **CPU** | x86-64 | Figures for playing: not measured |

**Frame rate.** One hand-play session on the setup in the GPU row averaged about 37 frames per second over 147 seconds
on the planet Fastoon, fights included. Scripted runs: not measured on that build. Minimum and recommended hardware for
playing are not measured; they will be listed here once the game is playable.

## Distribution

Releases are pre-releases on this repository's
[GitHub Releases](https://github.com/undecuple/qwarks-awakening/releases) page; since v0.2.0 each one has a launcher
preview for Windows that does not build or start a game yet. You build the program on your machine
from your own copy of the game ([BUILDING.md](BUILDING.md)); no game code is shipped or downloaded. There is no
installer yet.
