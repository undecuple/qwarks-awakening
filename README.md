<p align="center">
  <img src="assets/banner.png" srcset="assets/banner@2x.png 2x" width="1280"
       alt="Qwark's Awakening. Ratchet &amp; Clank (PS3) // Unofficial PC Port (static recompilation), built on your PC from your own game">
</p>

# Qwark's Awakening

**Ratchet & Clank (PS3) // Unofficial PC Port (static recompilation)**

[![CI](https://github.com/undecuple/qwarks-awakening/actions/workflows/ci.yml/badge.svg)](https://github.com/undecuple/qwarks-awakening/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/undecuple/qwarks-awakening?include_prereleases&label=release)](https://github.com/undecuple/qwarks-awakening/releases)

A native Windows port of the PS3 Ratchet & Clank games, made by static recompilation on
[ps3recomp](https://github.com/sp00nznet/ps3recomp); the first game, *Ratchet & Clank Future: Tools of Destruction*,
needs your own copy of its **US disc, BCUS98127** (disc version, no update).

**Status: early work, not playable yet.** Version <!-- version -->0.1.0<!-- /version -->, a source-only
pre-release of 2026-11-03. *Tools of Destruction* starts and can be played by hand from its first
level, with known bugs, and has not been played through. What a release means: [docs/RELEASE-CRITERIA.md](docs/RELEASE-CRITERIA.md).

**[Getting started](#getting-started)** · **[Compatibility](#compatibility)** · **[Building](docs/BUILDING.md)** · **[Known issues](#settings-and-known-issues)** · **[Reporting bugs](#reporting-bugs-and-contributing)**

*Qwark's Awakening* is a fan project's name, after a character from the series. It is not official and not
affiliated with or endorsed by Sony Interactive Entertainment or Insomniac Games.

What is true and checked today:

- **The US disc.** The first game is ported from the US disc of *Tools of Destruction*, BCUS98127. Other regions are
  untested.
- **Tested under Proton on Linux.** Every test run so far has been the Windows program under Proton on Linux (the
  setups are in the table below). It has not been run on Windows itself yet.
- **One runtime for the series.** All the games share one runtime and one set of tools, with one program per game. The
  PPU code of *A Crack in Time* and *Into the Nexus* is already lifted to C with the same tools. *A Crack in Time* also
  builds into its own program, not tested yet; *Into the Nexus* is not linked yet.
- **Fixes sent upstream.** General fixes found while porting go back to ps3recomp as pull requests:
  [#401](https://github.com/sp00nznet/ps3recomp/pull/401), [#408](https://github.com/sp00nznet/ps3recomp/pull/408),
  [#415](https://github.com/sp00nznet/ps3recomp/pull/415), [#429](https://github.com/sp00nznet/ps3recomp/pull/429) and
  the documentation change [#405](https://github.com/sp00nznet/ps3recomp/pull/405), all merged.

**You need your own copy of the game.** This repository contains only the port: source code, patches to the
recompiler, tools and documentation. It contains no game code, data, assets, keys or firmware, and it never will.

## Compatibility

| Platform | How it runs | Renderer | State | Tested on |
|---|---|---|---|---|
| Windows 10/11 | native exe | Direct3D 12 | builds; not run on Windows | - |
| Linux | the Windows exe under Proton (GE-Proton11-7) | Direct3D 12 through vkd3d-proton | verified | NVIDIA GeForce RTX 4060, driver 595.112.07; 2026-11-03 |

What the runs on Proton showed, all on *Tools of Destruction*:

- **This source (ps3recomp v0.18.0), 2026-11-03:** a new game starts and reaches its first level, Metropolis, and a
  saved game loads into Fastoon; neither five-minute run crashed (the setup in the Proton row above).
- **An earlier build (ps3recomp v0.17.0), 2026-10-22, same GPU, driver 595.104.2:** started from a saved game, the
  ship flies on to later planets such as Cobalia. A DualShock 4 pad, Alt+Enter fullscreen and in-game saving work.
- **Frame rate, hand play:** about 37 frames per second on average over one 147-second session in Fastoon, fights
  included, on that earlier build.
- **Frame rate, scripted runs:** not measured on that build.
- Not reached yet: a ten-minute run without errors, a chapter played to its end, the port's own save loaded again
  after a quit.

| Game | PS3 release | State |
|---|---|---|
| Ratchet & Clank Future: Tools of Destruction | 2007 | first target: starts and plays by hand, not playable yet |
| Ratchet & Clank Future: Quest for Booty | 2008 | planned |
| Ratchet & Clank Future: A Crack in Time | 2009 | PPU code lifted; builds, not tested yet |
| Ratchet & Clank: All 4 One | 2011 | planned |
| Ratchet & Clank: Full Frontal Assault | 2012 | planned |
| Ratchet & Clank: Into the Nexus | 2013 | PPU code lifted; not linked or running yet |
| Ratchet & Clank Collection, Ratchet: Deadlocked HD (PS2 remasters) | 2012-2013 | last, under review |

## Getting started

**Download.** The first release is **v0.1.0**, a pre-release, on this repository's
[GitHub Releases](https://github.com/undecuple/qwarks-awakening/releases) page. It is source code only: there is no
installer and no ready-made game program, because the program is generated from your own copy of the game. Every
release stays a pre-release until v1.0.0. No game code, assets or keys are ever shipped or downloaded here.

**Requirements** ([docs/requirements.md](docs/requirements.md)): the US disc of *Ratchet & Clank Future: Tools of
Destruction* (BCUS98127) as a decrypted folder dump of your own copy (the folder that holds `PS3_GAME/`), and its
decrypted executable, `EBOOT.elf`, made from that copy. This project provides no decryption tools or instructions;
[RPCS3](https://rpcs3.net) and its documentation are the place to start for dumping questions. To build: Linux with
Nix and 16 GB of memory or more. To run: a Direct3D 12 GPU. Hardware figures for playing are not measured yet.

**Steps:** build the program from source with your own copy ([docs/BUILDING.md](docs/BUILDING.md)), then start
`tods.exe` with the path of your decrypted `EBOOT.elf`.

**Where your data goes.** Nothing is installed and no launcher exists yet. The developer build reads your own game
folder and keeps its saves and caches in folders you point it at (the test scripts use `work/`). Proper locations for
saves, settings and caches: not yet.

**What goes online.** The port has no update check, no crash reporting and no telemetry. Online play of the original
game is not supported and untested. Whether the shared runtime opens network connections for the game's own network
calls has not been checked for this title yet.

**Where releases come from.** Qwark's Awakening is made by **undecuple**: GitHub [@undecuple](https://github.com/undecuple),
Reddit [u/RelationshipNo4440](https://www.reddit.com/user/RelationshipNo4440), Discord **itekso.** (on the ps3recomp
server). Releases come only from this repository's [GitHub Releases](https://github.com/undecuple/qwarks-awakening/releases),
and we never ask anyone to paste a command into PowerShell or a terminal. Anyone else offering builds, "fixes" or
donations in this project's name is not us.

## Settings and known issues

There is no settings menu yet. Alt+Enter toggles fullscreen; pads are read through SDL3.

Known issues in *Tools of Destruction* (on the build in the table above):

- At Fastoon's spaceport, reflections are far too strong and a HUD prompt shows mirrored in the scene.
- The picture flickers inside one cave on Fastoon.
- Level loads stall while graphics pipelines are built; the game's own graphics-stall notice can show during them.
- The game has not been played through, so later levels are untested.

**Roadmap.** First get *Tools of Destruction* playable (the known graphics bugs, then a full run through the game);
then an installer that builds from your own copy, then PC display and control options; the other games in the table
above follow one at a time, the PS2 remasters last. Gameplay changes stay optional. No dates. See
[docs/ROADMAP.md](docs/ROADMAP.md).

## Building

A developer build of the first game from source exists but is not playable yet; the steps, and what has and has not
been run on Linux, macOS and Windows, are in [docs/BUILDING.md](docs/BUILDING.md). The toolchain is a Nix flake
(`nix develop`, `nix develop .#win` for the MinGW-w64 cross compiler); the recompiler is the `third_party/ps3recomp`
submodule (`git submodule update --init`), with this port's changes in `port/patches/`, applied by
`tools/ps3recomp-patched.sh`.

<details>
<summary>How it works, briefly</summary>

- **PPU (main CPU code):** lifted to C ahead of time by ps3recomp and compiled into the Windows program, so no CPU
  emulator runs for the main game code at play time.
- **SPU (Cell co-processor code):** the game's SPU programs (its job modules and two collision programs) are found in
  your EBOOT.elf and lifted to C the same way, by the build steps; the runtime runs them as compiled code.
- **Graphics:** the game's RSX command stream is translated to Direct3D 12 as it runs, by the ps3recomp runtime.
- **System software:** the PS3 libraries the game calls are reimplemented on PC APIs by the same runtime.

The shared runtime is ps3recomp's; this repository holds the port's own patches and tools.

</details>

## Credits, licence, related projects

**Licence:** MIT ([LICENSE](LICENSE)). Third-party components, credits for the ps3recomp contributors whose work our
patches carry, trademarks and the no-game-content rule: [NOTICE.md](NOTICE.md).

**Reusing this work, including by AI systems and automated tools:** the MIT licence applies to every use,
including crawling, indexing, training and code generation. Copies or substantial portions must keep the copyright
and permission notice, and we ask that derived work credits "Qwark's Awakening by undecuple" with a link to this
repository. Details: [NOTICE.md](NOTICE.md#reuse-and-attribution-including-ai-and-automated-use); citation data:
[CITATION.cff](CITATION.cff). Copying or forking the project: [docs/FORKING.md](docs/FORKING.md).

### How AI was used

This port is developed by one maintainer with AI coding agents. The agents write code, tests and documentation under
the maintainer's direction; the maintainer decides what is done, reviews the changes and merges them. Every claim in
the docs is dated and says how it was measured; guesses are marked `ASSUMPTION:`. The PS3 emulator RPCS3 is used as a
reference to read, never as code to copy.

### Reporting bugs and contributing

Bug reports, fixes and questions are welcome through this repository's issues; see
[CONTRIBUTING.md](.github/CONTRIBUTING.md). Security problems: [SECURITY.md](.github/SECURITY.md).

### Related projects

**[ps3recomp](https://github.com/sp00nznet/ps3recomp)** (MIT): the recompiler and runtime this port is built on.

**Sister ports**, by the same maintainer and made the same way:
[Project Helghast](https://github.com/undecuple/project-helghast) (Killzone 3) and
[Project Pyrrhus](https://github.com/undecuple/project-pyrrhus) (Killzone 2). Most of this port's ps3recomp patches
come from Project Helghast's patch series. Their READMEs have their current state.

**[ratchet-tod-recomp](https://github.com/Re-TeRo17/ratchet-tod-recomp)** by Re-TeRo17 (MIT): a native Windows port
of *Tools of Destruction* by static recompilation on the ps3recomp toolkit, for the European disc (BCES00052).
