# Serious Sam PS4 (RADV)

A PS4 port of Serious Sam: The First Encounter and The Second Encounter. The engine stays [SeriousSamClassic-VK](https://github.com/tx00100xt/SeriousSamClassic-VK). The graphics device is Dolphin's published RADV stack, not the older OpenGNM driver. Pad, audio, and the external `/data` game folders follow [Nichlas Ek's SeriousSamPS4](https://github.com/NichlasEk/SeriousSamPS4).

The engine is GPL-2.0. Retail game files are not included. See [THIRD-PARTY.md](THIRD-PARTY.md).

## What you install

The package contains the game executable only. On the console, the game reads a folder you copy yourself:

| Game | Title ID | Game data on the console | Saves and settings |
| --- | --- | --- | --- |
| First Encounter | `SSAM00001` | `/data/serioussam-game` | `/data/serioussam` |
| Second Encounter | `SSAM00002` | `/data/serioussamse-game` | `/data/serioussamse` |

Those title IDs are the defaults. Pass your own title ID, version, label, and icon if this package should not replace an existing install.

HDD `/data` is checked first, then USB slots 0–7. The folder layout is the installed game: `1_00.gro` and `Levels/` for the First Encounter, `SE1_00.gro` and the other `SE1_00_*.gro` archives for the Second Encounter, plus `Scripts/Game_startup.ini`.

## What you build with

Use the Dolphin PS4 WSL recipe until its Vulkan probe has drawn a triangle on the console. This build uses that same toolchain:

* clang 21 compiles
* PacBrew LLD 12.0.1 links. LLVM 21's linker does not
* Mesa 26.0.8 with Dolphin's `mesa-ps4.patch`, already built
* OpenOrbis from that PacBrew toolchain
* The host also needs CMake, Ninja, Python 3, flex, and bison

```sh
export OPENORBIS=$HOME/dolphinps4-build/pacbrew/root/opt/pacbrew/ps4/openorbis
export PS4_SYSROOT=$HOME/dolphinps4-build-pacbrew/sysroot
export LLVM21=$HOME/dolphinps4-build/llvm-21/bin
export MESA_SRC=$HOME/dolphinps4-build/src/mesa-official
export MESA_BUILD=$HOME/dolphinps4-build-pacbrew/build/mesa
export SS_JBC_SOURCE=$HOME/src/ps4-libjbc   # bucanero/ps4-libjbc at 835fe016
```

## Build a package

```sh
bash scripts/build-ps4-radv.sh tfe
python3 scripts/package-ps4-radv.py --game tfe --version 01.00 \
  --title-id SSAM00001 --label SERIOUSSAMTFE001 \
  --icon assets/icon0-tfe.png

bash scripts/build-ps4-radv.sh tse
python3 scripts/package-ps4-radv.py --game tse --version 01.00 \
  --title-id SSAM00002 --label SERIOUSSAMTSE001 \
  --icon assets/icon0-tse.png
```

Replace the icon with your own 512×512 PNG. The label is 16 letters or digits. Packages are written under `artifacts/radv/`.

## Behaviour that is easy to miss

* The linked ELF has no dynamic linker. The first load is at address 0 and the entry point is `0x20`. A normal PIE executable crashes before `main`.
* `malloc` is replaced. The retail libc heap faults on the first allocation.
* SystemService, UserService, VideoOut, Pad, and AudioOut are loaded with `sceSysmoduleLoadModuleInternal` before those libraries are called.
* SDL video is the dummy driver. RADV owns one 1280×720 swapchain. The menu cannot change that size.
* Audio is the PS4 48 kHz port. The engine still mixes at its own rate, and the driver resamples.
* Saves and settings are written through `/ssam_user`, a sandbox mount of `/data/serioussam` or `/data/serioussamse`. The game-data mount is only for reading archives.
* The engine timer stays single-threaded. Cutscenes stay software-decoded.
