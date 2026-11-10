# Serious Sam PS4 (RADV)

A PS4 port of Serious Sam: The First Encounter and The Second Encounter. The engine stays [SeriousSamClassic-VK](https://github.com/tx00100xt/SeriousSamClassic-VK). The graphics device is Dolphin's published RADV stack, not the older OpenGNM driver. Pad, audio, and the external `/data` game folders follow [Nichlas Ek's SeriousSamPS4](https://github.com/NichlasEk/SeriousSamPS4).

The engine is GPL-2.0. Retail game files are not included. See [THIRD-PARTY.md](THIRD-PARTY.md).

## What you install

The package contains the game executable only. On the console, each game reads one folder, and that folder is filled from two places: your Classic install, and the engine data at commit `510d36f`.

| Game | Title ID | Game data on the console | Saves and settings | Engine directory |
| --- | --- | --- | --- | --- |
| First Encounter | `SSAM00001` | `/data/serioussam-game` | `/data/serioussam` | `engine-vk/SamTFE` |
| Second Encounter | `SSAM00002` | `/data/serioussamse-game` | `/data/serioussamse` | `engine-vk/SamTSE` |

HDD `/data` is checked first, then USB slots 0–7. Use Serious Sam Classic from GOG or Steam. Serious Sam HD, Fusion, and Revolution do not load. The commercial files are not in this repository.

On a first launch the game creates `/data/serioussam` and `/data/serioussamse` itself, mode `0777`, including `SaveGame` and the other user folders.

### Files from your Classic install

Copy these into the game-data folder. GOG and Steam name the First Encounter campaign archive `1_00c.gro`. Copy that file and name the copy `1_00.gro`. Leave the store file unchanged. The Second Encounter names already match.

| Game | Folder | Copy from your Classic install |
| --- | --- | --- |
| First Encounter | `/data/serioussam-game` | `1_00.gro`, `1_00_music.gro`, and the whole `Levels/` directory |
| Second Encounter | `/data/serioussamse-game` | `SE1_00.gro`, `SE1_00_Extra.gro`, `SE1_00_Levels.gro`, `SE1_00_Music.gro` |

The startup check looks for `Levels/01_Hatshepsut.wld` inside the First Encounter `Levels/` directory.

### Files from the engine

Copy `SE1_10b.gro`, the whole `Scripts/` directory, and the whole `Data/` directory into that same game-data folder. First Encounter takes them from `engine-vk/SamTFE`. Second Encounter takes them from `engine-vk/SamTSE`.

Those three items belong to [SeriousSamClassic-VK](https://github.com/tx00100xt/SeriousSamClassic-VK) at [`510d36f`](https://github.com/tx00100xt/SeriousSamClassic-VK/tree/510d36f901cbe883c56b155b53717e4efdeb6522). They are not inside the PKG. A zip download of this repository leaves `engine-vk` empty, because it is a submodule.

```sh
git clone --recurse-submodules https://github.com/JarnoVos/SeriousSamPS4-RADV.git
```

An existing checkout gets the same tree with `git submodule update --init engine-vk`. Copy the three items only. `Sources/`, `Bin/`, and the other engine directories stay where they are.

The game reads the rest of `Scripts/` and `Data/` while it runs. Copying `Scripts/Game_startup.ini` and `Data/NoCRC.lst` by themselves leaves the rest of those directories behind. Before startup, the game also checks these paths, and every `.gro` must begin with `PK`:

| Game | Paths checked at startup |
| --- | --- |
| First Encounter | `1_00.gro`, `1_00_music.gro`, `Levels/01_Hatshepsut.wld`, `SE1_10b.gro`, `Scripts/Game_startup.ini`, `Data/NoCRC.lst` |
| Second Encounter | `SE1_00.gro`, `SE1_00_Extra.gro`, `SE1_00_Levels.gro`, `SE1_00_Music.gro`, `SE1_10b.gro`, `Scripts/Game_startup.ini`, `Data/NoCRC.lst` |

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

The title IDs above are the defaults. Pass your own title ID, version, label, and icon when this package should not replace an existing install.

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

## Screenshots / Video (recorded on PS4 Slim 10.50)

<img width="1920" height="1080" alt="tfe1" src="https://github.com/user-attachments/assets/664ef7a5-3d2c-47e2-aff2-a91dddaf37d0" />
<img width="1920" height="1080" alt="tse1" src="https://github.com/user-attachments/assets/d2ba0c82-686b-4397-a7a2-ec33a81a972e" />

https://github.com/user-attachments/assets/bd35842d-2549-4057-80c6-d689bfa58f66

## Behaviour that is easy to miss

* The linked ELF has no dynamic linker. The first load is at address 0 and the entry point is `0x20`. A normal PIE executable crashes before `main`.
* `malloc` is replaced. The retail libc heap faults on the first allocation.
* SystemService, UserService, VideoOut, Pad, and AudioOut are loaded with `sceSysmoduleLoadModuleInternal` before those libraries are called.
* SDL video is the dummy driver. RADV owns one 1280×720 swapchain. The menu cannot change that size.
* Audio is the PS4 48 kHz port. The engine still mixes at its own rate, and the driver resamples.
* Saves and settings use `/data/serioussam` or `/data/serioussamse`. Those directories are created on first launch. The game-data folder is only for reading archives.
* The engine timer stays single-threaded. Cutscenes stay software-decoded.
