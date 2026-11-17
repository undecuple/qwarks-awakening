# The launcher on Linux

*What was run (2026-11-16): on NixOS (NVIDIA GeForce RTX 4060, GE-Proton11-7, no Steam and no umu-launcher), a launcher built from source started* Tools of Destruction *through Play up to its "Press the START button" screen, and Stop ended it with no process left. The Linux download, made the way the release workflow makes it, was installed, started and removed in a fresh Ubuntu 22.04 container under Xvfb. Not run yet: starting a game from the download, and building a game with the Linux build tools.*

The games are Windows programs. On Linux the launcher itself runs natively and starts each game through Proton,
the compatibility layer Steam uses, with one Wine prefix per game. Steam does not need to be installed.

## Install the Linux download

Each launcher release from 0.4.0 on has `qwarks-awakening-launcher-<version>-linux-x86_64.tar.gz`. It holds the
launcher (SDL 3 is built into it), its engine (Python scripts), the build files (the port's source code of that
release and the recompiler with the port's changes), the fonts, the licence texts and `install.sh`.

You need Linux on x86-64 with glibc 2.34 or newer (built on Ubuntu 22.04 and checked there), an X11 or Wayland
desktop, and Python 3.10 or newer as `python3`. To play: the 64-bit Vulkan driver of your GPU.

```sh
tar xf qwarks-awakening-launcher-<version>-linux-x86_64.tar.gz
cd qwarks-awakening-launcher-<version>
./install.sh
```

`install.sh` installs for your user only, without administrator rights:

| What | Where |
|---|---|
| The launcher, its engine and the build files | `$XDG_DATA_HOME/qwarks-awakening/launcher/` (default `~/.local/share/qwarks-awakening/launcher/`) |
| A link to start it from a terminal | `~/.local/bin/qwarks-awakening` |
| The menu entry and its icon | `$XDG_DATA_HOME/applications/qwarks-awakening.desktop`, `$XDG_DATA_HOME/icons/hicolor/256x256/apps/qwarks-awakening.png` |

Start "Qwark's Awakening" from your applications menu. To update, run the `install.sh` of the newer download: it
replaces the launcher folder. To remove the launcher, run
`~/.local/share/qwarks-awakening/launcher/install.sh --uninstall`. It removes the files above and keeps your library,
built games, prefixes, Proton runtimes, build tools, saves and settings ([Folders](#folders)); delete those folders
yourself to remove them too. Without installing, `./qwarks-awakening` also runs from the unpacked folder.

`SHA256SUMS` on the release page lists the tarball's checksum.

## Build the launcher from source

Instead of the download, you can build the launcher from a clone of this repository; it then runs from its build
folder and builds games from that clone. You need Git, CMake 3.20 or newer, a C++17 compiler,
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

The Linux download carries the build files (the port's source of that release and the patched recompiler), so Build
needs no other download from this project; what a build makes goes to `$XDG_DATA_HOME/qwarks-awakening/build/`,
outside the launcher folder, so an update keeps it. A launcher
built from a clone builds the games from that clone (the port's source and, through `tools/ps3recomp-patched.sh`, the
patched recompiler). No game has been built on Linux with the Linux build tools yet.

## Folders

| What | Where |
|---|---|
| Library, built games, prefixes, runtimes, build tools, build files of each game, logs | `$XDG_DATA_HOME/qwarks-awakening/` (default `~/.local/share/qwarks-awakening/`) |
| The installed launcher (Linux download) | `$XDG_DATA_HOME/qwarks-awakening/launcher/` |
| Display and control settings | `$XDG_CONFIG_HOME/qwarks-awakening/` (default: `.config/qwarks-awakening/` in your home folder) |

Credits and licences of GE-Proton, umu-launcher, the build tools and what the Linux download carries (SDL 3 with
the HIDAPI and yuv2rgb code it includes, Dear ImGui, the fonts, ps3recomp): [NOTICE.md](../NOTICE.md); the download
has their texts in `licenses/`.
