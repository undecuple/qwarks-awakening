# Changelog

All notable user-facing changes, newest first, in the style of [Keep a Changelog](https://keepachangelog.com/). The
project is a pre-release (early work, not playable yet): see [RELEASE-CRITERIA.md](docs/RELEASE-CRITERIA.md).

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

