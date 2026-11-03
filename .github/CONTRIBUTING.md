# Contributing

Contributions are welcome: bug reports, fixes, feedback and questions. There is nothing to run yet, so most useful
right now are notes on the games' formats and on ps3recomp.

## Ground rules

- **No game material, ever:** no files from a disc, update or digital copy, no decrypted or lifted game code, no
  extracted assets, saves, keys or firmware, and no links to where they can be downloaded. Numbers, names, structure
  layouts and formulas worked out from the games are fine.
- **No GPL code:** RPCS3 is a reference to read, not code to copy. Write behaviour from your own understanding.
- **Recompiler fixes go upstream:** a fix that is not specific to these games belongs in
  [ps3recomp](https://github.com/sp00nznet/ps3recomp). Here it lives as a patch in `port/patches/` until upstream
  takes it.

## Reporting a problem

Say which build you ran (commit), which game and version, your GPU and driver, Windows or Proton (and which Proton),
and attach the run's log. Remove your user name and folder paths from logs first. Do not attach save files or
anything else that contains game data. Security problems: see [SECURITY.md](SECURITY.md), not an issue.
