# The launcher on Linux

*What was run (2026-11-16, NixOS, NVIDIA GeForce RTX 4060, GE-Proton11-7, no Steam and no umu-launcher): the launcher's Play started* Tools of Destruction *up to its "Press the START button" screen, and Stop ended it with no process left. Building a game with the Linux build tools has not been run yet. There is no Linux download yet: build the launcher from source ([below](#build-the-launcher-from-source)).*

The games are Windows programs. On Linux the launcher itself runs natively and starts each game through Proton,
the compatibility layer Steam uses, with one Wine prefix per game. Steam does not need to be installed.

## Build the launcher from source

The releases hold Windows downloads only. On Linux, build the launcher from a clone of this repository; it then runs
from its build folder and builds games from that clone. You need Git, CMake 3.20 or newer, a C++17 compiler,
pkg-config, SDL3 with its development files, and Python 3.

```sh
git clone --recurse-submodules https://github.com/undecuple/qwarks-awakening.git
cd qwarks-awakening
cmake -S launcher -B build/launcher
cmake --build build/launcher
./build/launcher/launcher
```

With Nix (flakes enabled), the repository's shell has all of these: run the two `cmake` lines as
`nix develop --command cmake ...`. The launcher keeps its library and settings in the folders listed under
[Folders](#folders), not in the clone. [BUILDING.md](BUILDING.md) covers the rest of the build from source.

## Choosing a Proton

Settings > Proton > "Run games with":

- **GE-Proton (tested)**: the GE-Proton release this port is tested with (GE-Proton11-7). The launcher downloads it
  from its own GitHub release once you choose Download in its dialog (about 564 MB), checks it against the SHA-256 the
  launcher pins, and unpacks it to `~/.local/share/qwarks-awakening/runtimes/`. A download that does not match is
  deleted and nothing is installed.
- **A Proton you already have**: the list shows Steam's compatibility tools (`compatibilitytools.d`), Valve's Proton
  installs (`steamapps/common/Proton ...`) and any folder you add with "Add a Proton folder" (the folder that holds the
  files `proton` and `version`). Nothing is downloaded then.

## How a game starts

- With **umu-launcher** installed (`umu-run` on your PATH) and "Use umu-launcher when installed" on, the game starts
  through umu-launcher with the Proton you chose. umu-launcher runs Proton inside Steam's runtime container, which it
  downloads itself the first time. It is a separate program you install from your distribution; the launcher only
  starts it.
- Without it, the launcher starts Proton directly, the way Steam does: `proton waitforexitandrun` with the game's own
  prefix and an empty stand-in for the Steam folder. This needs the 64-bit Vulkan driver of your GPU. On NixOS, Proton
  needs `steam-run` (from `programs.steam.enable`) or umu-launcher; the launcher puts `steam-run` in front when it finds it.

Each game gets its own prefix in `~/.local/share/qwarks-awakening/prefixes/<game>/`. Saves stay outside it
(`~/.local/share/qwarks-awakening/saves/<game>/` unless you chose a saves folder), so a prefix can be deleted without
losing them. The launcher's display and control settings (`$XDG_CONFIG_HOME/qwarks-awakening/`, see [Folders](#folders)) are linked into the game's
folder, so what you set in the launcher is what the game reads; existing settings files there are never replaced.

**Stop** on the game's page ends the game: the launcher stops every Windows process in that game's prefix, then the
programs it started.

## Building games on Linux

Building turns your own decrypted EBOOT into the game's Windows program, as on Windows. On Linux it needs a compiler for
Windows programs: Settings > Building > "Linux build tools" downloads llvm-mingw, CMake and Ninja for Linux and SDL3's
package, each at the version and SHA-256 the launcher pins, from each project's own release, after you confirm. Without
them, Build uses a MinGW-w64 compiler on your PATH if there is one.

The launcher built from a clone builds the games from that clone (the port's source and, through
`tools/ps3recomp-patched.sh`, the patched recompiler). Not there yet: these build files for a launcher installed
elsewhere.

## Folders

| What | Where |
|---|---|
| Library, built games, prefixes, runtimes, build tools, logs | `$XDG_DATA_HOME/qwarks-awakening/` (default `~/.local/share/qwarks-awakening/`) |
| Display and control settings | `$XDG_CONFIG_HOME/qwarks-awakening/` (default: `.config/qwarks-awakening/` in your home folder) |

Credits and licences of GE-Proton, umu-launcher and the build tools: [NOTICE.md](../NOTICE.md).
