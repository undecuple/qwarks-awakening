# The launcher on Linux

*What was run: on NixOS (NVIDIA GeForce RTX 4060, no Steam and no umu-launcher, under Xvfb), the Linux download made by the release workflow was installed into an empty home folder (2026-11-17). The launcher started with steam-run; the Linux build tools (with nix-ld) and GE-Proton11-7 were downloaded and checked;* Tools of Destruction *was built in about 5 minutes (16 parallel jobs) and started through GE-Proton to its "Press the START button" screen; Stop ended it with no process left, and the uninstall removed the launcher's files. Earlier (2026-11-16) a launcher built from source played the same way, and the download was installed, started and removed in a fresh Ubuntu 22.04 container. Not run yet: a real display or Wayland, the launcher's buttons by hand (the engine behind them was run), umu-launcher, and other distributions than NixOS for building and playing.*

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

### NixOS

The launcher, Proton and the Linux build tools are made for the usual Linux folder layout, which NixOS does not
have. Start the launcher with `steam-run` (from `programs.steam.enable`):

```sh
steam-run qwarks-awakening
```

Without it the launcher stops at once with "No available video device" (it cannot find the X11 or Wayland
libraries). The downloaded build tools (llvm-mingw, CMake, Ninja) need `steam-run` around the launcher or nix-ld
(`programs.nix-ld.enable = true`); when they cannot start, Build says so before it begins.

## The AppImage

The same release also has `qwarks-awakening-launcher-<version>-x86_64.AppImage`: the same launcher, engine, build
files, fonts and licence texts as the tarball, in one file that runs where you keep it, without installing.

```sh
chmod +x qwarks-awakening-launcher-<version>-x86_64.AppImage
./qwarks-awakening-launcher-<version>-x86_64.AppImage
```

- It keeps your library, built games, prefixes, Proton runtimes, build tools, saves and settings in the same folders
  as the tarball install ([Folders](#folders)), so the two can be used in turn. Nothing is written inside the
  AppImage.
- It needs FUSE to mount itself (most desktops have it). Without FUSE (some containers and minimal systems), start it
  with `--appimage-extract-and-run`: it unpacks itself to a temporary folder for that run.
- On NixOS, start it with `appimage-run` (nixpkgs), which runs it in the usual Linux folder layout:
  `appimage-run qwarks-awakening-launcher-<version>-x86_64.AppImage`. Building and playing from the AppImage on NixOS
  have not been tried yet.
- To update, use the newer AppImage; to remove it, delete the file. There is no menu entry unless you add one.
- It needs Python 3.10 or newer as `python3`, like the tarball (the engine runs with the system's Python).

*What was run (2026-11-17): the AppImage, made by the release scripts from the release workflow's launcher program,
started with `--appimage-extract-and-run` in a fresh Ubuntu 22.04 container under Xvfb with an empty home folder;
its first start listed the eight games and wrote its library to `~/.local/share/qwarks-awakening/`. Not run yet:
building or playing a game from the AppImage, a start through FUSE, and `appimage-run` on NixOS.*

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
patched recompiler). A game build with the Linux build tools takes about 5 minutes with 16 parallel jobs and needs
about 0.7 GB (*Tools of Destruction*, measured once); fewer CPU cores take longer.

## Logs and troubleshooting

- Each start writes what Proton and Wine print to `logs/proton-<game>.log` in `~/.local/share/qwarks-awakening/`.
  Its first lines name the game's own log folder: `logs/` in the game's build folder (by default
  `games/<game>/logs/<game>/` there, files `QwarksAwakening-<game>-<time>.log`). Read the game's log for what the
  game did; on the game's page, "Open game log folder" opens it.
- Lines in the Proton log such as `Failed to open /etc/machine-id`, fontconfig warnings or ProtonFixes messages are
  Proton's own and normal; they do not mean the game failed.
- A build writes `firstrun.log` in its build folder (`~/.local/share/qwarks-awakening/build/<game>/`).

## Folders

| What | Where |
|---|---|
| Library, built games, prefixes, runtimes, build tools, build files of each game, logs | `$XDG_DATA_HOME/qwarks-awakening/` (default `~/.local/share/qwarks-awakening/`) |
| The installed launcher (Linux download) | `$XDG_DATA_HOME/qwarks-awakening/launcher/` |
| Display and control settings | `$XDG_CONFIG_HOME/qwarks-awakening/` (default: `.config/qwarks-awakening/` in your home folder) |

Credits and licences of GE-Proton, umu-launcher, the build tools and what the Linux download carries (SDL 3 with
the HIDAPI and yuv2rgb code it includes, Dear ImGui, the fonts, ps3recomp): [NOTICE.md](../NOTICE.md); the download
has their texts in `licenses/`.
