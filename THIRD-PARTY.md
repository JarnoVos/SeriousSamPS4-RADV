# Credits and licenses

This repository is GPL-2.0 because it is a port of the Serious Engine. See `LICENSE`.

* Croteam published Serious Engine 1.10 under GPL-2.0.
* [SeriousSamClassic-VK](https://github.com/tx00100xt/SeriousSamClassic-VK) at `510d36f901cbe883c56b155b53717e4efdeb6522` is the Vulkan engine this build patches. It is a submodule. That tree is based on [Serious Engine Vulkan](https://github.com/sultim-t/Serious-Engine-Vk).
* [Nichlas Ek's SeriousSamPS4](https://github.com/NichlasEk/SeriousSamPS4) is the PS4 port this project continues: the OpenOrbis patches, pad mapping, external `/data` layout, and the decision to keep the engine's Vulkan backend. The graphics device here is RADV, not that port's OpenGNM driver.
* [DolphinPS4](https://github.com/DolphinPS4/DolphinPS4) publishes the PS4 RADV recipe used here: Mesa 26.0.8, `mesa-ps4.patch`, and the GNM winsys. `scripts/make-pkg.sh` is taken from that tree. Mesa is MIT.
* The retail malloc replacement is Doug Lea's dlmalloc 2.8.6, MIT-0, in `platform/ps4/third_party/dlmalloc`.
* SDL is the zlib-licensed [OpenOrbis SDL-PS4](https://github.com/OpenOrbis/SDL-PS4) pin `7774cf3`. The build checks that out. Video stays on SDL's dummy driver. Audio is a small PS4 AudioOut backend in this repo.
* Jailbreak mounts use [ps4-libjbc](https://github.com/bucanero/ps4-libjbc) revision `835fe016`. Point `SS_JBC_SOURCE` at a checkout. The script does not vendor that source.
* The OpenOrbis toolchain, clang 21, and PacBrew LLD 12.0.1 are installed separately. Sony system modules that the package step copies come from that local SDK at package time and are not in this repository.

Do not commit retail game data, disc images, or built packages.
