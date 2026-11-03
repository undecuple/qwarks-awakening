# Copying or forking this project

This page is for people who fork or reuse this repository. It lists what carries this project's name, address or
settings, so your copy does not point at ours or claim to be ours, and what you must keep. Paths are relative to the
repository root.

**Licence.** The port's own code, tools and documentation are MIT ([LICENSE](../LICENSE)): you may use, copy, modify,
merge, publish, distribute, sublicense and sell copies. Every copy or substantial portion must keep the copyright line
("Copyright (c) 2026 undecuple") and the MIT permission notice. [NOTICE.md](../NOTICE.md) also asks, but the licence
does not require, that derived work credit "Qwark's Awakening by undecuple" with a link to this repository. The
licence covers nothing of the Ratchet & Clank games themselves.

## 1. Names and branding

"Qwark's Awakening" is this fan project's name. Use your own name for a fork, and say it is unofficial and not
affiliated with Sony Interactive Entertainment or Insomniac Games, as the README and NOTICE.md do.

| What | Where |
|---|---|
| Title, tag line and prose | `README.md`, `docs/*.md`, `.github/CONTRIBUTING.md`, `.github/SECURITY.md` (written by hand, not generated) |
| Banner and social preview | `assets/banner.png`, `assets/banner@2x.png`, `assets/social-preview.png` (images carrying the project name; the generator is not part of this tree, so replace them) |
| Directory entry for recomp.fyi | `.recomp.json` |
| Citation | `CITATION.cff` |
| Game window title | `port/rc/main.cpp` reads the `PS3_TITLE` environment variable and falls back to `ps3recomp` |

Some patches in `port/patches/` come from the Killzone 3 port (Project Helghast) and still use that project's name
in settings-file names and headers (for example patches 2037 and 2095). Search the patches for the name if your fork
should not write files under it.

## 2. Identity and remotes

This project's owner name and repository address appear as plain text in several files. Search for `undecuple` and
`qwarks-awakening` and replace each occurrence you do not mean to keep (the copyright line and the credits in
NOTICE.md stay):

- `README.md`, `docs/BUILDING.md` (clone command), `NOTICE.md`, `CITATION.cff` (`authors`, `repository-code`, `url`),
  `.recomp.json` (`maintainers`, `links`).
- `.github/CODEOWNERS`, `.github/FUNDING.yml` (delete or replace it), `.github/ISSUE_TEMPLATE/config.yml`.

Set your own git identity (`git config user.name`, `user.email`) before your first commit, and review your first
commits for personal paths and host names. A private address in a pushed commit stays readable in the history.

## 3. Versions and releases

- Tags are `vMAJOR.MINOR.PATCH`, with no suffixes (no `-alpha`, `-rc`): a fix bumps the last number, a feature the
  middle one.
- Every release stays a GitHub pre-release until `v1.0.0`.
- A tag is never moved or reused; if a release is wrong, publish the next version.
- Use the same version string everywhere it appears: the tag, the exe's version block (`tools/build-rc.sh` takes it
  from the tag through `RC_RELEASE_TAG`) and `CITATION.cff`.
- Publish a SHA-256 checksum file next to every release asset.

## 4. CI and GitHub settings

| What | Notes |
|---|---|
| `.github/workflows/ci.yml` | every patch in `port/patches/` applies to the pinned ps3recomp, and a Gitleaks secret scan of the full history; needs no secrets. The program itself cannot be built in CI: its lifted code comes from the player's own EBOOT |
| Action versions | actions are pinned by commit SHA and updated by hand, as are `flake.lock` and the ps3recomp submodule (no Dependabot version updates) |
| Issue forms | `.github/ISSUE_TEMPLATE/`; they assume labels you create yourself |

For these workflows and your own:

- Pin every third-party action to a full commit SHA, not a tag.
- Give the workflow token the least permission it needs (`permissions: contents: read` at the top, widen per job).
- Require approval before Actions run for first-time contributors, and never run workflows from forks with secrets.
- Never commit tokens, keys or credentials, and never print them in logs.
- Add a ruleset on the default branch (no force push, no deletion) and on `v*` tags (no update, no delete).
- Turn on private vulnerability reporting, since `.github/SECURITY.md` points reporters to it.

## 5. The ps3recomp submodule and the patch layout

- ps3recomp (MIT) is a git submodule at `third_party/ps3recomp`, pinned in `.gitmodules` and by commit. It stays
  unmodified.
- This port's changes to it are the patches in `port/patches/NNNN-name.patch`. `tools/ps3recomp-patched.sh` copies
  the pinned ps3recomp to a work folder (`RC_PATCHED_DIR`) and applies every patch in order; the lift and the build
  use that copy.
- Number blocks: `0xxx`-`5xxx`, `9633` and `9933` are carried from the Killzone 3 port's series; `6xxx` and `7xxx`
  are this port's own.
- Each patch header names its source when the change comes from someone else (a ps3recomp pull request, a fork commit
  or another port). Keep those lines when you carry a patch, and keep the matching credit in NOTICE.md.
- If you move to another ps3recomp release, update the pin, check that every patch still applies (CI does this), and
  update the version named in `NOTICE.md`.

## 6. Paths and environment in the scripts

Check these before you run the scripts on another machine.

| Item | Where | Default or note |
|---|---|---|
| `PROTON_DIR` (`--proton DIR`) | `tools/proton-test.sh`, `tools/proton-inner.sh` | a GE-Proton folder in Steam's `compatibilitytools.d` under the user's home; change it for your system |
| `RC_PATCHED_DIR` | `tools/ps3recomp-patched.sh`, `tools/lift.sh` | where the patched ps3recomp copy is made |
| `RC_RECOMP_DIR`, `RC_ANALYSIS_DIR`, `RC_LIFT_CHUNKS` | `tools/lift.sh` | where the lifted sources and the lift report go, and how the source is split |
| `RC_BUILD_JOBS_MAX`, `RC_BUILD_JOB_MB`, `RC_CCACHE`, `RC_SPU`, `RC_RELEASE_TAG` | `tools/build-rc.sh` | tuning (memory per compile job), the SPU build switch and the version tag |
| `RC_LOCK_DIR`, `RC_HELD_LOCKS`, `RC_BUILD_SLOTS` | `tools/lift.sh`, `tools/build-rc.sh` | lock files for shared build slots; on a machine that runs nothing else you can ignore them (`lift.sh` then prints a harmless warning) |
| `PS3_VFS_ROOT`, `PS3_TITLE` and the `RC_*` run switches | `port/rc/main.cpp` | read with `getenv` where they are used; documented in the source |

`tools/sandbox.sh` runs commands in a bubblewrap sandbox that makes the game folder and the repository read-only,
allows writes only under `work/` and `build/`, and allows no network; adapt its mounts if your layout differs.
`tools/safe_rm.sh` only deletes inside `work/` and `build/` of the repository; keep that limit.

## 7. What the user supplies, and what is never committed

Nothing from the games is in this repository or in a release. Each user brings their own copy of the US disc of
*Ratchet & Clank Future: Tools of Destruction* (BCUS98127) as a decrypted folder dump (the folder that holds
`PS3_GAME/`) and the decrypted executable, `EBOOT.elf`, made from that copy. This project gives no tools or
instructions for making it; [RPCS3](https://rpcs3.net) and its documentation are the place to start. The full list is
in [requirements.md](requirements.md).

Keep it that way in a fork. Never commit or publish:

- game files: disc files, archives, extracted assets, movies, saves;
- the EBOOT in any form, encrypted or decrypted, and any other decrypted binary;
- anything generated from the game's code: the lifted C sources and the built game program (`work/` and `build/`
  are ignored by git for this reason);
- keys or licence files of any kind, and links to them;
- a decryption procedure, or links to game files or to places that offer them.

## 8. Licences and credits to keep

- `LICENSE` (MIT): keep the notice with any copied code; add your own line, do not remove the existing one.
- `NOTICE.md`: the ps3recomp and RPCS3 lines, the font credits for `assets/`, the trademark statement, and the
  section "Upstream contributors and sister ports", which credits every outside source our patches carry. When you
  carry a patch with code from a new pull request, fork or author, add or extend that author's entry in the same
  commit.
- `port/patches/`: keep each patch's source line in its header.
- RPCS3 (GPL-2.0) is a reference to read; no RPCS3 code is included. Keep it that way, or your fork's licence changes.

## 9. What the public tree leaves out

The public tree holds the port's code and patches, the build, lift (PPU and SPU), stage and test tools, the release helpers in
`tools/release/stage_sdl3.py` (it puts SDL3.dll next to the built program) and these docs. It leaves out working notes, run and measurement tools, and all game data. Some
comments in the code and patches cite internal numbering (rounds, steps, run names); they are labels, not links. If a
script refers to a file you cannot find, treat it as optional first and check the script before assuming the tree is
incomplete.
