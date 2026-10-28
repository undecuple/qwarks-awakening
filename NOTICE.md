# Third-party components and credits

| Component | Use | Licence |
|---|---|---|
| [ps3recomp](https://github.com/sp00nznet/ps3recomp) | PPU/SPU recompiler and PS3 runtime (submodule `third_party/ps3recomp`; our changes are the patches in `port/patches/`) | MIT |
| [RPCS3](https://github.com/RPCS3/rpcs3) | reference for PS3 behaviour and data layouts, read only; no RPCS3 code is included | (GPL-2.0, not used as code) |
| [Russo One](https://github.com/google/fonts/tree/main/ofl/russoone) (Jovanny Lemonad) | the wordmark in `assets/` (banner, social preview); the font itself is not included | SIL OFL 1.1 |
| [Rajdhani](https://github.com/google/fonts/tree/main/ofl/rajdhani) (Indian Type Foundry) | small text in `assets/`; the font itself is not included | SIL OFL 1.1 |

## Upstream contributors and sister ports

Our ps3recomp patches (`port/patches/`) carry work from other ps3recomp pull requests and forks (MIT) and from the
author's sister ports. Each patch header names its source.

- **Killzone 3 port** (the author's private port of Killzone 3, MIT): patches `0xxx`-`5xxx` and `9933`, carried
  from that port's patch series, unchanged or rebased.
- **Toro Lets Party port** (same author): [PR #387](https://github.com/sp00nznet/ps3recomp/pull/387) (patch 9387),
  the PPU lifter's far jump-table bounds compare.
- **NoahBPeterson:** [PR #287](https://github.com/sp00nznet/ps3recomp/pull/287),
  [PR #213](https://github.com/sp00nznet/ps3recomp/pull/213) and his fork's commits e68e567 / b709a08 (patches 1071,
  1074, 1055, 1056): SPU extended-range single precision (exact per-element code, #287), the per-SPU SPURS kernel with
  ready-count admission (#213, `libs/spurs/spurs_kernel.c`), and the ideas behind the hashed HLE NID lookup and the
  striped lock-line lock with seqlock GETLLAR (fork commits).
- **JonathanDC64:** fork PRs [#38](https://github.com/JonathanDC64/ps3recomp/pull/38) and
  [#47](https://github.com/JonathanDC64/ps3recomp/pull/47) (from the Demon's Souls port), adapted in patches 1111 and
  2085: PPU stores that stay coherent with SPU lock-line reservations, and colour-mask writes with reserved bits.
- **erikomis:** [PR #333](https://github.com/sp00nznet/ps3recomp/pull/333),
  [#334](https://github.com/sp00nznet/ps3recomp/pull/334), [#338](https://github.com/sp00nznet/ps3recomp/pull/338)
  and [#372](https://github.com/sp00nznet/ps3recomp/pull/372), merged into ps3recomp: `PS3_LANG` system-language
  selection, the row-at-a-time unscaled NV3089 blit, `PS3_GAMEDATA_ROOT` and pad rumble that lasts until it changes.
  Patch 9933 builds on #333, patches 2080/2083 use #334's row-copy idea, patch 5004 adapts `PS3_GAMEDATA_ROOT` to its
  trophy store, and patch 4010 holds and renews a rumble request as #372 does.
- **Kurtisone:** [PR #340](https://github.com/sp00nznet/ps3recomp/pull/340), merged into ps3recomp: the atomic update
  of a task's signalled bit against SPU commits, which patch 1067 relies on.
- **tekulvw:** [PR #304](https://github.com/sp00nznet/ps3recomp/pull/304) (patch 2055): framebuffer output sRGB for
  half exports.
- **soyjxck:** [soyjxck/ps3recomp](https://github.com/soyjxck/ps3recomp) (from the Drakengard 3 port): patch 1091's
  inline guest loads follow its per-page access byte (`ppu_vm_page.h`).
- **robbe1912:** [robbe1912/ps3recomp](https://github.com/robbe1912/ps3recomp) (patch 2057, from the MotorStorm:
  Monument Valley port): the fragment decoder's third-source absolute modifier.

Except for 9387 (by this author), these all arrive through the Killzone 3 series.

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
