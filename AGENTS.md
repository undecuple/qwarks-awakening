# Instructions for AI coding agents

Read this before changing anything in this repository. It applies to every automated contributor (coding agents such
as Claude Code, Codex, Copilot and Cursor) and to the people who run them. Human contributors: the same rules are in
[CONTRIBUTING.md](.github/CONTRIBUTING.md).

## The project

Qwark's Awakening is an unofficial PC port of the PlayStation 3 Ratchet & Clank games, made by static recompilation
with [ps3recomp](https://github.com/sp00nznet/ps3recomp): each game's PPU and SPU code is lifted to C, the console's
system libraries are reimplemented, and the RSX graphics run on Direct3D 12. One shared runtime, one program per game.
The repository holds only the port; each player builds from their own copy of the game. Status: [README](README.md),
[feature parity](docs/FEATURE-PARITY.md), [roadmap](docs/ROADMAP.md), [release criteria](docs/RELEASE-CRITERIA.md).

## Repository map

| Path | What it holds |
|---|---|
| `port/rc/` | the program: entry point, per-game glue and HLE overrides, display and settings code, host tests |
| `port/common/` | code shared with the other games of the port |
| `port/patches/` | numbered patches to ps3recomp; each header names its purpose and its source |
| `third_party/ps3recomp/` | the recompiler, a pinned submodule, never edited in place |
| `tools/` | lift, build, sandbox and run scripts; disc and ELF inventory tools; `tools/release/` (setup engine, catalogue, release and site tools, with their tests) |
| `launcher/` | the launcher (SDL3 and Dear ImGui) |
| `data/` | tables the tools read (`data/titles/`: catalogue, per-game SPU tables; `data/theme.json`); numbers and names only |
| `docs/` | public documentation: building, requirements, roadmap, feature parity, release criteria, forking |
| `site/` | the project page |

## Hard rules

- **No game material, ever.** Never commit, attach, paste, request or link to: files from a disc, update or digital
  copy, decrypted or lifted game code, extracted assets, saves, keys or firmware. Numbers, names, structure layouts and
  formulas worked out from the games are fine. Game and work folders (`work/`, `build/`) stay out of git; do not
  weaken `.gitignore`.
- **No circumvention.** No decryption or copy-protection tools, keys, step-by-step procedures or links to them. The
  docs may say that the port needs the decrypted EBOOT made from the player's own copy and that RPCS3 and its
  documentation are the place to start; nothing more.
- **ps3recomp stays pristine.** Never edit the submodule or move its pin as part of a fix. Every change is a new
  numbered patch in `port/patches/`, applied by `tools/ps3recomp-patched.sh`, which fails on a patch that does not
  apply.
- **No GPL code.** RPCS3 is a reference to read for behaviour, never to copy, translate or closely paraphrase.
- **Gameplay stays original by default.** Mark additions `ENHANCEMENT:` (display improvements may default on, gameplay
  changes stay opt-in), values not yet recovered from the game `ASSUMPTION:`, and keep the original address in a
  comment (`// @0x00123abc`) next to recompiled or ported code.
- **Root causes, not symptoms.** No timeouts, clamps, skips or reduced counts that hide a fault.
- **Nothing goes online unless the player chose it.** A new network connection is named in the README's "What goes
  online" paragraph in the same change.

## Licence, attribution and citation

- The port is MIT-licensed ([LICENSE](LICENSE)), as is ps3recomp; other components keep their own terms, listed in
  [NOTICE.md](NOTICE.md).
- A new dependency, bundled tool or font needs a compatible licence and an entry in NOTICE.md in the same change.
- **Credit what you absorb.** Work taken from an upstream pull request, a fork or another port is credited in the
  patch header, in NOTICE.md (one entry per author, with the pull requests and patch numbers) and in the commit
  message. Never remove existing credits.
- **Cite, do not paraphrase silently.** A fact from an external reference names its source next to it; check it
  against the game's code before relying on it.
- Citing the project: [CITATION.cff](CITATION.cff). Reuse terms for AI systems: NOTICE.md, "Reuse and attribution".

## Build and test

The toolchain comes from the Nix flake; full steps are in [docs/BUILDING.md](docs/BUILDING.md).

~~~sh
nix develop .#re     # analysis and lifting
nix develop .#win    # Windows x64 cross build (MinGW-w64)
nix develop          # everything, plus Xvfb and steam-run for runs under Proton
~~~

- **Before every pull request** run what CI runs, which needs no game data and no GPU:
  `nix develop .#ci --command bash tools/ps3recomp-patched.sh` and every `tools/release/test_*.py`.
- **Host tests are self-contained.** A runtime fix comes with a focused test that fails without the fix and passes
  with it. A test that needs game data prints SKIP, not FAIL. When a patch changes what an old test checks, update the
  test in the same change.
- **Anything that reads the game** runs inside `tools/sandbox.sh` (game and repository read-only, only `work/` and
  `build/` writable, no network).
- **Windows runs from Linux** go through `tools/proton-test.sh`; the log lands in `work/proton/<name>/` and a
  screenshot in `work/shots/`. Look at the screenshot before claiming anything about the picture.
- Agents without the game can still work on the tools, on patches with host tests, on the launcher and on the docs;
  say in the pull request that no game was run.

## Docs travel with code

- Update the docs a change affects in the same commit: build steps, requirements, settings, credits.
- Never write that something works unless you ran it, and say how. Frame rates say whether they come from scripted
  input or from hand play.
- Environment switches are documented where they are read, with their default.

## Commits and pull requests

- One topic per commit; short, plain, descriptive messages. Credits go in the message; no tool-generated footers or
  advertising lines.
- The pull request says what was run and what was seen. Use the template in `.github/`.
- Never add personal paths, user names, tokens or other secrets; CI runs a secret scan on the full history.

## Fixes that belong upstream

A patch that is not specific to these games is a candidate for
[ps3recomp](https://github.com/sp00nznet/ps3recomp). Keep it free of game-specific content, with its focused test.
Propose it there with the symptom, cause, fix and test, and follow the maintainer's lead; the patch stays in
`port/patches/` until upstream takes it.

## Security and rights holders

- Report vulnerabilities privately ([SECURITY.md](.github/SECURITY.md)), never in a public issue.
- Rights holders use the same private channel; formal notices follow [NOTICE.md](NOTICE.md). Do not open issues or
  pull requests about such reports.
