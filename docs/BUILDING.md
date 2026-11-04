# Building the port

The port's program is generated from **your own copy** of the game: the game's code is lifted to C from your
decrypted EBOOT and compiled on your machine. Nothing of the game is in this repository, and nothing is downloaded.
Only one game, *Ratchet & Clank Future: Tools of Destruction*, is being ported so far.

**Status: early work.** The game starts and can be played by hand from its first level, with known bugs; it has not
been played through and is not playable yet. Every test run so far was the Windows program under Proton on Linux. The
releases carry no game program and there is no installer (v0.2.0 adds a launcher preview that does not build games
yet): the only way to get a build is the developer build from source with [Nix](https://nixos.org/), described below.

## What you need

From your own copy of the game:

| Item | Notes |
|---|---|
| Ratchet & Clank Future: Tools of Destruction, US disc **BCUS98127**, as a decrypted folder | The folder that holds `PS3_GAME/`. Other regions are untested. |
| The **decrypted EBOOT** | `EBOOT.elf`, made from your own copy of the game. This project provides no decryption tools or instructions; [RPCS3](https://rpcs3.net) and its documentation are the place to start for dumping questions. |

To build: Linux with Nix (flakes enabled), 16 GB of memory or more and plenty of free disk. To run: a GPU with
Direct3D 12 support. The only tested way to run it is the Windows program under Proton on Linux.

## Toolchain

Everything comes from the Nix flake; nothing needs to be installed system-wide.

```sh
nix develop .#re     # analysis and lifting: Python with capstone and pyelftools, bubblewrap
nix develop .#win    # Windows x64 cross build: MinGW-w64, CMake, Ninja, SDL3 for Windows
nix develop          # everything, plus Xvfb and steam-run for test runs under Proton
```

`tools/sandbox.sh [--gpu] -- CMD` runs a command with the game and the repository read-only, only `work/` and
`build/` writable, and no network. The steps below run inside it.

## Steps

**Status on Linux (2026-11-03, ps3recomp v0.18.0):** steps 1 to 3b were run on NixOS in a fresh clone and gave the
same lifted code as the developer's checkout, whose build (steps 4 and 5) was run under Proton (step 6).

```sh
git clone --recursive https://github.com/undecuple/qwarks-awakening
cd qwarks-awakening
git submodule update --init                          # third_party/ps3recomp, pinned

# 1. Put your decrypted EBOOT in place (it stays outside git)
mkdir -p work/bin/tods && cp /path/to/decrypted/EBOOT.elf work/bin/tods/EBOOT.elf

# 2. Patched recompiler: a fresh copy of the pinned ps3recomp with port/patches/*.patch applied
nix develop .#re --command tools/sandbox.sh -- tools/ps3recomp-patched.sh

# 3. Lift the PPU code to C (work/recomp/tods)
nix develop .#re --command tools/sandbox.sh -- tools/lift.sh tods

# 3b. Lift the SPU programs (job modules, EDGE inflate ELF, policy module, raw SPU programs) to work/spu/tods/lift
nix develop .#re --command tools/sandbox.sh -- tools/spu/lift_spu.sh tods

# 4. Configure and build tods.exe (with the SPU programs from step 3b) (build-rc.sh caps the compiler jobs by free memory)
nix develop .#win --command tools/sandbox.sh -- tools/build-rc.sh build/rc-tods-win

# 5. The result is staged with SDL3.dll in work/stage/tods

# 6. Run it under Proton (hidden Xvfb display, audio drivers off; output in work/proton/NAME/)
nix develop --command tools/proton-test.sh --seconds 60 --name boot work/stage/tods/tods.exe /path/to/PS3_GAME/USRDIR/EBOOT.elf
```

The program takes the decrypted EBOOT as its only argument and finds the game's files from that path, which
must look like `<root>/PS3_GAME/USRDIR/EBOOT.elf`.

The lift and the build are the slow parts: each lifted source chunk can take 1 to 2 GB of memory to compile, and
`build-rc.sh` picks the number of compiler jobs from the free memory (override with `RC_BUILD_JOBS_MAX`).
On the developer machine the lift takes about half a minute and a warm build about twelve minutes.
Without a shared build-slot helper, `lift.sh` prints a warning about it; that warning is harmless on a machine
that runs nothing else.

`tools/proton-test.sh` uses GE-Proton from Steam's `compatibilitytools.d` by default (`--proton DIR` picks another).
The program's output is in `work/proton/NAME/app/app_stdout.txt` and a screenshot in `work/shots/NAME.png`.

## Building on macOS

**Status: not run.** Nobody has built or run the port on a Mac, and the macOS Nix shells of this flake have not been
tested. Reports are welcome (open an issue).

- **What you would need:** a Mac with Apple silicon, 16 GB of memory or more, and Nix with flakes enabled
  (`experimental-features = nix-command flakes` in `~/.config/nix/nix.conf`).
- **Steps:** the same as [Steps](#steps) 1 to 5. `tools/sandbox.sh` relies on bubblewrap, which is Linux-only, so
  on macOS run the commands without it (untested).
- **Running:** `tods.exe` is a Windows program and Proton does not run on macOS. The untested option is to copy
  `work/stage/tods/` to a Windows PC and run it there.

## Building on Windows

**Status: not run.** The port has not been built on Windows or WSL2, and it has not been run on real Windows; it is
tested under Proton on Linux only.

Nix does not run on Windows itself, so the build would run inside **WSL2** (untested):

1. Install WSL2 with Ubuntu (`wsl --install -d Ubuntu`), then install Nix inside it and enable flakes as above.
2. Give WSL2 enough memory: `%UserProfile%\.wslconfig` with `[wsl2]` and `memory=24GB`, then `wsl --shutdown`.
3. Keep the repository inside the Linux file system (not under `/mnt/c`); the build reads and writes many files.
4. Run [Steps](#steps) 1 to 5 unchanged. Skip step 6.
5. Copy `work/stage/tods/` to a Windows folder and start `tods.exe` there with the path of your decrypted EBOOT as
   the argument. You need a GPU with Direct3D 12 and a current driver.
