<p align="center">
  <img src="assets/banner.png" srcset="assets/banner@2x.png 2x" width="1280"
       alt="Qwark's Awakening. Ratchet &amp; Clank (PS3) // Unofficial PC Port (static recompilation), built on your PC from your own game">
</p>

# Qwark's Awakening (work in progress)

**Ratchet & Clank (PS3) // Unofficial PC Port (static recompilation)**

[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
![Status: early work](https://img.shields.io/badge/status-early%20work-orange)
![Windows x64](https://img.shields.io/badge/platform-Windows%20x64-0078D6)

**[Games](#games)** · **[Requirements](docs/requirements.md)** · **[Getting it](#getting-it)** · **[Building](docs/BUILDING.md)** · **[Roadmap](docs/ROADMAP.md)** · **[Contributing](.github/CONTRIBUTING.md)**

*Qwark's Awakening* is a fan project's name, after a character from the series. It is not official and not
affiliated with or endorsed by Sony Interactive Entertainment or Insomniac Games.

A native Windows x64 port of the PlayStation 3 Ratchet & Clank games, made by **static recompilation**: each
game's PowerPC (PPU) and Cell SPU code is translated to C ahead of time and compiled into a Windows program, so no
CPU emulator runs for the main game code at play time (SPU code not yet translated runs in an interpreter for now). The PS3 system software the games call is reimplemented on PC APIs, and the GPU
command stream is translated to Direct3D 12 as it runs. One shared runtime, one program per game. The recompiler and
runtime are [ps3recomp](https://github.com/sp00nznet/ps3recomp) (MIT); this repository holds the port's own
changes and tools.

> **Status: early work.** The first game, *Ratchet & Clank Future: Tools of Destruction*, starts as a Windows program,
> reaches its title menu and plays the opening cutscene, with known graphics bugs. The game itself is not playable yet.
> No release, no ETA.

**You need your own copy of the game.** This repository contains only the port: source code, patches to the
recompiler, tools and documentation. It contains no game code, data, assets, keys or firmware, and it never will.

How to build it, on Linux, macOS and Windows (with what has and has not been run): [docs/BUILDING.md](docs/BUILDING.md).

## Games

| Game | PS3 release | State |
|---|---|---|
| Ratchet & Clank Future: Tools of Destruction | 2007 | first target |
| Ratchet & Clank Future: Quest for Booty | 2008 | planned |
| Ratchet & Clank Future: A Crack in Time | 2009 | planned |
| Ratchet & Clank: All 4 One | 2011 | planned |
| Ratchet & Clank: Full Frontal Assault | 2012 | planned |
| Ratchet & Clank: Into the Nexus | 2013 | planned |
| Ratchet & Clank Collection, Ratchet: Deadlocked HD (PS2 remasters) | 2012-2013 | last, under review |

## What goes online

The port has no update check, no crash reporting and no telemetry. Online play of the original game is not supported
and untested. Whether the shared runtime opens network connections for the game's own network calls has not been
checked for this title yet.

## Where your data goes

Nothing is installed and no launcher exists yet. The developer build (see [docs/BUILDING.md](docs/BUILDING.md))
reads your own game folder and keeps its saves and caches in folders you point it at (the test scripts use `work/`).
Proper locations for saves, settings and caches: not yet.

## Getting it

**Nothing is released yet.** There is no installer, no download and no ETA. The only way to try the port today is to
build it yourself from source, with your own copy of the game ([docs/BUILDING.md](docs/BUILDING.md)); it starts and
reaches the title menu but is not playable. No game code, assets or keys are ever shipped or downloaded here.

## Requirements

See [docs/requirements.md](docs/requirements.md): the US disc of *Ratchet & Clank Future: Tools of Destruction*
(BCUS98127) as a decrypted folder dump of your own copy, its decrypted executable (this project provides no
decryption tools or instructions), and a Direct3D 12 GPU. Hardware figures are not measured yet.

## Building

A developer build of the first game from source exists but is not playable yet; the steps and what has been run are in
[docs/BUILDING.md](docs/BUILDING.md). The toolchain is a Nix flake (`nix develop`, `nix develop .#win` for the MinGW-w64
cross compiler); the recompiler is the `third_party/ps3recomp` submodule (`git submodule update --init`), with this
port's changes in `port/patches/`, applied by `tools/ps3recomp-patched.sh`. Testing is done on Linux under Proton.

## How it works, briefly

- **PPU (main CPU code):** lifted to C ahead of time by ps3recomp and compiled into the Windows program.
- **SPU (Cell co-processor code):** the game's SPU programs are not recompiled in this tree yet; the program runs
  that work in an interpreter for now.
- **Graphics:** the game's RSX command stream is translated to Direct3D 12 as it runs, by the ps3recomp runtime.
- **System software:** the PS3 libraries the game calls are reimplemented on PC APIs by the same runtime.

The shared runtime is ps3recomp's; this repository holds the port's own patches and tools. There is no separate
how-it-works page yet.

## Roadmap

First get *Tools of Destruction* playable (the known graphics bugs, then a full run through the game); then an
installer that builds from your own copy, then PC display and control options; the other games in the table above
follow one at a time, the PS2 remasters last. Gameplay changes stay optional. No dates. See
[ROADMAP.md](docs/ROADMAP.md).

## Related projects

**[ps3recomp](https://github.com/sp00nznet/ps3recomp)** (MIT): the recompiler and runtime this port is built on.

**Sister ports:** the same maintainer is porting other PS3 games the same way. They are not public yet; links follow
when they are.

## Official channels

Qwark's Awakening is made by **undecuple**: GitHub [@undecuple](https://github.com/undecuple), Reddit [u/RelationshipNo4440](https://www.reddit.com/user/RelationshipNo4440), Discord **itekso.** (on the ps3recomp server). There are no builds yet; when there are, they come only from this repository's [GitHub Releases](https://github.com/undecuple/qwarks-awakening/releases), and we never ask anyone to paste a command into PowerShell or a terminal. Anyone else offering builds, "fixes" or donations in this project's name is not us.

## How this is made

The port is written with AI coding assistants (Anthropic's Claude) working under one human maintainer, who decides
what is done and reviews it. Every claim in the docs is dated and says how it was measured; guesses are marked
`ASSUMPTION:`. The PS3 emulator RPCS3 is used as a reference to read, never as code to copy.

## Contributing

Bug reports, fixes and questions are welcome once there is something to run; see
[CONTRIBUTING.md](.github/CONTRIBUTING.md). Security problems: [SECURITY.md](.github/SECURITY.md).

## Licence

MIT ([LICENSE](LICENSE)). Third-party components, trademarks and the no-game-content rule: [NOTICE.md](NOTICE.md).

**Reusing this work, including by AI systems and automated tools:** the MIT licence applies to every use,
including crawling, indexing, training and code generation. Copies or substantial portions must keep the copyright
and permission notice, and we ask that derived work credits "Qwark's Awakening by undecuple" with a link to this
repository. Details: [NOTICE.md](NOTICE.md#reuse-and-attribution-including-ai-and-automated-use); citation data:
[CITATION.cff](CITATION.cff).
