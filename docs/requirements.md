# Requirements

What you need to build and run the port of *Ratchet & Clank Future: Tools of Destruction*. Kept current as the port
changes. The port starts, reaches the title menu and plays the opening cutscene, with known graphics bugs; it is not
playable yet, so there are no hardware figures measured for play. Nothing below is a promise of a playable game.

## What you need from your own copy of the game

| Item | Required | Details |
|---|---|---|
| **Ratchet & Clank Future: Tools of Destruction disc, US release** | yes | Title ID **BCUS98127**, as a decrypted folder that holds `PS3_GAME/`. Other regions and IDs are untested. You make your own dump; the port ships none of the game. |
| **Decrypted executable** | yes | The game's `EBOOT.BIN` is encrypted; the port needs `EBOOT.elf`, its decrypted form, made from your own copy. This project gives no decryption tools or instructions. |
| **PS3 system firmware** | no | Not needed; the PS3 system libraries are reimplemented by the runtime. |
| **PlayStation Network account** | no | Online play is not supported. |

## Hardware

| | Needed | Basis |
|---|---|---|
| **OS** | To build: Linux with Nix (flakes enabled). To run: the Windows program under Proton on Linux is the only tested way | Windows and macOS: not run (see [BUILDING.md](BUILDING.md)). A native Linux build is not planned yet |
| **GPU** | Direct3D 12 support | Only tested under Proton on Linux; which GPUs work is not established |
| **RAM** | 16 GB or more to build | Each lifted source chunk can take 1 to 2 GB to compile; the build script picks the number of parallel jobs from free memory. Memory needed to play: not measured |
| **Disk** | The game dump plus several GB for the lifted sources and build | Exact figures: not measured |
| **CPU** | x86-64 | Figures for playing: not measured |

Playing figures (frame rate, minimum and recommended hardware) will be listed here once the game is playable.

## Distribution

Nothing is released. The intended model is source code plus a tool that builds the program on your machine from your
own copy of the game; no game code is shipped or downloaded. Details when there is something to release.
