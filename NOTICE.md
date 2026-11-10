# Third-party components and credits

| Component | Use | Licence |
|---|---|---|
| [ps3recomp](https://github.com/sp00nznet/ps3recomp) | PPU/SPU recompiler and PS3 runtime (submodule `third_party/ps3recomp`, pinned at v0.18.0, commit 4e8f3ca; our changes are the patches in `port/patches/`) | MIT |
| [RPCS3](https://github.com/RPCS3/rpcs3) | reference for PS3 behaviour and data layouts, read only; no RPCS3 code is included | (GPL-2.0, not used as code) |
| [AMD FidelityFX Super Resolution 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) | the optional FSR 1 upscaler, off by default (`upscale = fsr1` in display.ini, or `RSX_UPSCALE=fsr1`): `ffx_a.h` and `ffx_fsr1.h` unmodified at commit a21ffb8, added to the renderer by patch 2145 (from the Killzone 3 series) under `libs/video/fsr1/` with their licence text; Copyright (c) 2021 Advanced Micro Devices, Inc. | MIT |
| [Russo One](https://github.com/google/fonts/tree/main/ofl/russoone) (Jovanny Lemonad) | the wordmark in `assets/` (banner, social preview); the font itself is not included | SIL OFL 1.1 |
| [Rajdhani](https://github.com/google/fonts/tree/main/ofl/rajdhani) (Indian Type Foundry) | small text in `assets/`; the font itself is not included | SIL OFL 1.1 |
| [Dear ImGui](https://github.com/ocornut/imgui) (Omar Cornut) | the launcher's interface (submodule `third_party/imgui`, v1.93.0) | MIT |
| [SDL3](https://libsdl.org/) (Sam Lantinga and SDL contributors) | window, input, gamepads and PNG reading/writing in the launcher and the games | zlib |
| [Python](https://www.python.org/) (Python Software Foundation) | the official Windows embeddable package 3.13.16, unmodified, in the launcher download's `python\` folder: it runs the launcher's setup engine (`tools/release/catalog.py`); its `LICENSE.txt` (also covering the libraries CPython bundles) is in the download's `licenses\` | PSF-2.0 |
| [llvm-mingw](https://github.com/mstorsjo/llvm-mingw), [CMake](https://cmake.org/), [Ninja](https://ninja-build.org/) | build the launcher download in the release workflow (`.github/workflows/release.yml`); not included in it | Apache-2.0 WITH LLVM-exception (and MinGW-w64's terms), BSD-3-Clause, Apache-2.0 |
| [Adwaita Sans and Adwaita Mono](https://gitlab.gnome.org/GNOME/adwaita-fonts) (The Inter Project Authors, Renzhi Li, Jamie Gravendeel, Florian Müllner) | the launcher's text and headings (Sans) and its paths and versions (Mono), from the GNOME release adwaita-fonts 51.0: Adwaita Mono Regular unmodified; Adwaita Sans Regular, SemiBold and Bold are static instances cut from its variable font by `tools/release/font_instances.py` (a Modified Version under the OFL; no Reserved Font Name); the TTF files ship in `launcher/fonts/` with `OFL-adwaita.txt` | SIL OFL 1.1 |
| [Russo One](https://github.com/google/fonts/tree/main/ofl/russoone) (Jovanny Lemonad) | the launcher's wordmark on its About section; the TTF file ships in `launcher/fonts/` with its licence text | SIL OFL 1.1 |
| [Adwaita Sans and Adwaita Mono](https://gitlab.gnome.org/GNOME/adwaita-fonts) (The Inter Project Authors, Renzhi Li, Jamie Gravendeel, Florian Müllner) | the project page's text and headings (Sans Regular, SemiBold and Bold) and code (Mono Regular): WOFF2 files made from the launcher's Adwaita fonts above by `tools/release/site_fonts.py`, subset to Latin (Modified Versions under the OFL; no Reserved Font Name); they ship in `site/fonts/` with `OFL-adwaita.txt` | SIL OFL 1.1 |
| [IGDB](https://www.igdb.com/) | cover art, only when the player turns "Fetch cover art from the internet" on (not active yet) | their terms |

## Upstream contributors and sister ports

Our ps3recomp patches (`port/patches/`) carry work from other ps3recomp pull requests and forks (MIT) and from the
author's sister ports. Each patch header names its source.

- **Project Helghast** ([undecuple/project-helghast](https://github.com/undecuple/project-helghast), the author's
  Killzone 3 port, MIT): patches `0xxx`-`5xxx`, `9633` and `9933`, carried from that port's patch series, unchanged
  or rebased; and the file logger (`kz3_log.c`, `kz3_log.h`, `kz3_log_redact.h`: levels and tags, crash reports, redaction
  of the user's name), copied into `port/common/log/` with the Ratchet & Clank file names and header block added;
  and its keybinds file and mouse settings, which R&C patch 6011 and `port/rc/rc_settings.*` extend to one settings
  folder and one key table per game. Most of the entries below reach this port through that series.
- **Project Pyrrhus** ([undecuple/project-pyrrhus](https://github.com/undecuple/project-pyrrhus), the author's
  Killzone 2 port, MIT), through the Killzone 3 series: patches 1059 (design of the per-waiter `sys_lwcond` FIFO),
  1139 (console `sys_fs_open` flag values), 1143 (kernel wake events under Wine), 2088 and 2089 (`WPOS` and the clear
  scissor on antialiased targets), 2124 (fragment-program source precision modifiers), 2129 (idle wake at a FIFO
  park), 2141 (pipeline keys tied to the shader translator), 1148 (a condition variable on per-thread events, so
  timed waits stay out of ntdll's futex queue under Wine), 1150 (SPURS task ids passed as arguments), 2142 (a texture
  level too large for the upload ring submits the list first instead of being skipped), 2143 (fragment-program DDX/DDY
  derivatives decompiled instead of dropped), 2144 (a draw that samples its own render target reads it when other
  surfaces share the address), 2152 (the present paths skip a back buffer GetBuffer could not return) and 2154 (the
  draw state memo is invalidated after the helper blits); and, directly, the in-play rule of its patch 6046 (the pointer held in the window while a
  look update took mouse counts in the last 300 ms, free otherwise), used by the R&C cursor rule in `port/rc/main.cpp`.
- **J-Stars Victory VS+ port** (MIT): its patch 0004, carried as patch 1145: the HLE fast path stores the caller's
  TOC in the ABI slot as the full path does.
- **Toro Lets Party port** (same author): [PR #387](https://github.com/sp00nznet/ps3recomp/pull/387), the PPU
  lifter's far jump-table bounds compare, carried until it was merged into ps3recomp (v0.16.0).
- **tekulvw:** [PR #304](https://github.com/sp00nznet/ps3recomp/pull/304),
  [#289](https://github.com/sp00nznet/ps3recomp/pull/289), [#434](https://github.com/sp00nznet/ps3recomp/pull/434),
  [#445](https://github.com/sp00nznet/ps3recomp/pull/445), [#449](https://github.com/sp00nznet/ps3recomp/pull/449),
  [#420](https://github.com/sp00nznet/ps3recomp/pull/420), [#481](https://github.com/sp00nznet/ps3recomp/pull/481)
  and [#484](https://github.com/sp00nznet/ps3recomp/pull/484) (patches 2055, 1061, 1120, 1118, 1119, 1123, 1124,
  1125): framebuffer output sRGB for half exports; the interrupt-enable bit in the SPU machine status (opt-in here);
  POSIX `_setjmp`/`_longjmp` for SPU jumps; one gate for the SPU local-store diagnostics; a bounded EA-to-IO lookup
  behind the SPU put hook; one owner order for every lifted-code lookup; the SPU dispatch cache; and a PUT that
  touches a reserved line copying only the reserved lines under the lock.
- **NoahBPeterson:** [PR #287](https://github.com/sp00nznet/ps3recomp/pull/287),
  [PR #213](https://github.com/sp00nznet/ps3recomp/pull/213) and his fork's commits e68e567, b709a08 and e3ab49d
  (patches 1071, 1134, 1074, 1055, 1056): exact per-element code for SPU extended-range single precision and its
  integer `fi` (#287), the per-SPU SPURS kernel with ready-count admission (#213, `libs/spurs/spurs_kernel.c`), and
  the ideas behind the hashed HLE NID lookup, the striped lock-line lock and the seqlock GETLLAR (fork commits).
- **erikomis:** [PR #333](https://github.com/sp00nznet/ps3recomp/pull/333),
  [#334](https://github.com/sp00nznet/ps3recomp/pull/334), [#338](https://github.com/sp00nznet/ps3recomp/pull/338),
  [#372](https://github.com/sp00nznet/ps3recomp/pull/372), [#489](https://github.com/sp00nznet/ps3recomp/pull/489)
  and [#474](https://github.com/sp00nznet/ps3recomp/pull/474), merged into ps3recomp: `PS3_LANG` system-language
  selection, the row-at-a-time unscaled NV3089 blit, `PS3_GAMEDATA_ROOT`, pad rumble that lasts until it changes, a
  guest stack of its own for each host thread's callbacks, and `MUTEX_WATCH`. Patch 9933 builds on #333, patches
  2080, 2083 and 2140 use #334's row-copy idea, patch 5004 adapts `PS3_GAMEDATA_ROOT` to its trophy store, patch 4010
  holds and renews a rumble request as #372 does, patch 1126 carries #489, and patch 1122 takes #474's idea of naming
  the owner of a lock a thread is stuck on.
- **Kurtisone:** [PR #340](https://github.com/sp00nznet/ps3recomp/pull/340) and
  [#243](https://github.com/sp00nznet/ps3recomp/pull/243), merged into ps3recomp: the atomic update of a task's
  signalled bit against SPU commits, which patch 1067 relies on, and reading a diagnostic switch once instead of on
  every call, which patch 1127 applies to the other hot paths.
- **rigaz29:** [PR #633](https://github.com/sp00nznet/ps3recomp/pull/633) (patch 9633, part of it): the wall clock
  on Windows in seconds since 1970.
- **n0madic:** [PR #485](https://github.com/sp00nznet/ps3recomp/pull/485) (patch 2100): vertex-program branches on
  the transform branch bits, and block starts past instruction 512.
- **JonathanDC64:** fork PRs [#38](https://github.com/JonathanDC64/ps3recomp/pull/38) and
  [#47](https://github.com/JonathanDC64/ps3recomp/pull/47) (from the Demon's Souls port), adapted in patches 1111 and
  2085: PPU stores that stay coherent with SPU lock-line reservations, and colour-mask writes with reserved bits.
- **soyjxck:** [soyjxck/ps3recomp](https://github.com/soyjxck/ps3recomp) (from the Drakengard 3 port): patch 1091's
  inline guest loads follow its per-page access byte (`ppu_vm_page.h`).
- **robbe1912:** [robbe1912/ps3recomp](https://github.com/robbe1912/ps3recomp) commit 0d03c8b5 (patch 2057, from the
  MotorStorm: Monument Valley port): the fragment decoder's third-source absolute modifier.
- **feanor-noldorin:** item E1 of [issue #457](https://github.com/sp00nznet/ps3recomp/issues/457), the report that
  inline-array vertices ignore the frequency divider, which patch 7009 (this port's own) fixes.

Ratchet & Clank and the names of its games and characters are trademarks of Sony Interactive Entertainment. This
project is not affiliated with or endorsed by Sony Interactive Entertainment, Insomniac Games or the studios that
remastered the PS2 games. The names are used only to say which games the port runs; no logos or other marks are used.

The MIT licence in LICENSE covers this port's own source code, tools and documentation only. It grants no rights to
Ratchet & Clank or any other software, data or trademark of Sony Interactive Entertainment or Insomniac Games.

**No game content is distributed.** This repository contains no part of the games: no disc files, no decrypted or
translated game code, no assets and no keys. Players supply their own copy of each game.

## Rights holders

- **No circumvention tools.** This repository ships no decryption or copy-protection circumvention tools, no keys
  and no instructions for decrypting game files. The port only reads a decrypted executable that players make
  themselves from their own copy of the game.
- **Contact.** If you hold rights in material you believe is in this repository, report it privately through this
  repository's *Security* tab (*Report a vulnerability*). Anything identified as infringing is removed while it is
  checked.
- **Formal notices** under the DMCA go to GitHub as described in
  [GitHub's DMCA Takedown Policy](https://docs.github.com/en/site-policy/content-removal-policies/dmca-takedown-policy).

## Reuse and attribution (including AI and automated use)

- **Licence:** the port's own code, tools and documentation are MIT ([LICENSE](LICENSE)), for automated use exactly
  as for any other: copies or substantial portions, including code an AI model reproduces from this repository, must
  include the copyright notice "Copyright (c) 2026 undecuple" and the MIT permission notice.
- **Attribution we ask for:** "Qwark's Awakening by undecuple", with a link to
  <https://github.com/undecuple/qwarks-awakening>.
- **Not covered by any licence here:** the games themselves. Recovered names, numbers and formats are documented for
  interoperability only.
- **Accuracy:** findings are dated and marked as verified or assumed; do not restate an `ASSUMPTION:` as fact.
