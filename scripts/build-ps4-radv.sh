#!/usr/bin/env bash
# Link SeriousSamClassic-VK to Dolphin's published RADV stack.
# Confirm that stack with Dolphin's Vulkan probe before trusting a game package.
# This script keeps its own patched source tree under build/.
set -euo pipefail
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)

OPENORBIS="${OPENORBIS:-$HOME/dolphinps4-build/pacbrew/root/opt/pacbrew/ps4/openorbis}"
PS4_SYSROOT="${PS4_SYSROOT:-$HOME/dolphinps4-build-pacbrew/sysroot}"
LLVM21="${LLVM21:-$HOME/dolphinps4-build/llvm-21/bin}"
MESA_SRC="${MESA_SRC:-$HOME/dolphinps4-build/src/mesa-official}"
export OO_PS4_TOOLCHAIN="$OPENORBIS"
export SS_PS4_SYSROOT="$PS4_SYSROOT"
export SS_PS4_CLANG="$LLVM21/clang"
export SS_PS4_CLANGXX="$LLVM21/clang++"
export SS_PS4_LLVM_AR="$LLVM21/llvm-ar"
export SS_PS4_LLVM_RANLIB="$LLVM21/llvm-ranlib"
export SS_PS4_LLD="$OPENORBIS/bin/ld.lld"
export SS_SDL_BUILD="build/ps4-sdl-radv"
export PATH="$HOME/serioussam-radv/host-prefix/usr/bin:$HOME/dolphinps4-build/host-tools/bin:$LLVM21:$OPENORBIS/bin:$PATH"
export SS_JBC_SOURCE="${SS_JBC_SOURCE:-$HOME/serioussam-radv/src/ps4-libjbc}"
export BISON_PKGDATADIR="${BISON_PKGDATADIR:-$HOME/serioussam-radv/host-prefix/usr/share/bison}"
export M4="${M4:-$HOME/serioussam-radv/host-prefix/usr/bin/m4}"

game=${1:-tfe}
case "$game" in
  tfe) tree=SamTFE; patch_name=001-openorbis-tfe.patch; target=SeriousSam; ecc=ecc; logdir=radv-tfe; hostdir=host-tools ;;
  tse) tree=SamTSE; patch_name=002-openorbis-tse.patch; target=SeriousSamMP; ecc=ecc-se; logdir=radv-tse; hostdir=host-tools-tse ;;
  *) echo 'Usage: build-ps4-radv.sh [tfe|tse]' >&2; exit 1 ;;
esac
revision=510d36f901cbe883c56b155b53717e4efdeb6522
source="$root/build/ps4-source-radv"
host_source="$root/build/$hostdir-source"
mkdir -p "$root/build" "$root/artifacts/$logdir" "$root/build/runtime-radv-$game"
test -f "$MESA_SRC/include/vulkan/vulkan.h"
test -x "$SS_PS4_CLANG"
"$SS_PS4_LLD" --version | head -n 1 | grep -q "LLD 12.0.1"

for checkout in "$source" "$host_source"; do
  if [[ ! -d "$checkout/.git" ]]; then git clone --shared "$root/engine-vk" "$checkout"; fi
  [[ $(git -C "$checkout" rev-parse HEAD) == "$revision" ]] || { echo "Unexpected source revision: $checkout" >&2; exit 1; }
done
patch="$root/patches/$patch_name"
external_patch="$root/patches/003-external-data-$game.patch"
extent_patch="$root/patches/004-radv-extent-$game.patch"
if git -C "$source" apply --reverse --check "$extent_patch" 2>/dev/null; then
  :
elif git -C "$source" apply --reverse --check "$external_patch" 2>/dev/null; then
  if ! grep -q ss_ps4_radv_extent "$source/$tree/Sources/SeriousSam/SeriousSam.cpp"; then
    git -C "$source" apply --check "$extent_patch"
    git -C "$source" apply "$extent_patch"
  fi
else
  git -C "$source" diff --quiet -- "$tree" || { echo "Preserve and review existing RADV source edits before applying the patch." >&2; exit 1; }
  git -C "$source" apply --check "$patch"
  git -C "$source" apply "$patch"
  git -C "$source" apply --check "$external_patch"
  git -C "$source" apply "$external_patch"
  git -C "$source" apply --check "$extent_patch"
  git -C "$source" apply "$extent_patch"
fi
python3 "$root/scripts/build-ps4-usb.py"
# PacBrew has no prebuilt libSDL2.a. Build the pinned SDL-PS4 static library,
# then let build-ps4-sdl.py swap in the dummy video backend, the PS4 audio
# driver, and the kernel semaphore.
sdl_src="$root/.tools/SDL-PS4"
if [[ ! -d "$sdl_src/.git" ]]; then
  git clone https://github.com/OpenOrbis/SDL-PS4.git "$sdl_src"
  git -C "$sdl_src" checkout 7774cf3e7d04448c44d569c50d1f9a4532a2efe7
fi
sdl_static="$root/build/ps4-sdl-static"
if [[ ! -f "$sdl_static/libSDL2.a" ]]; then
  rm -rf "$sdl_static"
  # Dummy video and audio. PacBrew has no OpenOrbis audioout.h/pad.h/video_out.h,
  # which the SDL PS4 backends include. The engine asks for the dummy video driver.
  cmake -S "$root/.tools/SDL-PS4" -B "$sdl_static" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/cmake/openorbis.cmake" \
    -DCMAKE_PROJECT_INCLUDE="$root/cmake/sdl-ps4-nowerror.cmake" \
    -DPS4=OFF -DORBIS=OFF -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF \
    -DVIDEO_X11=OFF -DVIDEO_WAYLAND=OFF -DVIDEO_DUMMY=ON -DVIDEO_OPENGL=OFF -DVIDEO_VULKAN=OFF \
    -DSNDIO=OFF -DPULSEAUDIO=OFF -DJACK=OFF -DESD=OFF -DNAS=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    > "$root/artifacts/$logdir/sdl-configure.log" 2>&1
  python3 - "$sdl_static/include/SDL_config.h" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text()
for name in (
    "HAVE__STRREV", "HAVE__STRUPR", "HAVE__STRLWR", "HAVE_ITOA", "HAVE__LTOA",
    "HAVE__UITOA", "HAVE__ULTOA", "HAVE__I64TOA", "HAVE__UI64TOA",
    "HAVE__STRICMP", "HAVE__STRNICMP"):
    text = text.replace("#define %s 1" % name, "/* %s not in the PS4 libc */" % name)
path.write_text(text)
PY
  cmake --build "$sdl_static" --target SDL2-static -j"${BUILD_JOBS:-8}" \
    > "$root/artifacts/$logdir/sdl-static.log" 2>&1
fi
export SS_SDL_ARCHIVE="$sdl_static/libSDL2.a"
python3 "$root/scripts/build-ps4-sdl.py"
if [[ ! -x "$root/build/$hostdir/$ecc" ]]; then
  # The entity compiler is a host tool. Its CMake still searches for SDL2.
  host_sdl="$root/build/host-sdl-stub"
  mkdir -p "$host_sdl"
  if [[ ! -f "$host_sdl/libSDL2.a" ]]; then
    echo 'void ss_host_sdl_stub(void) {}' > "$host_sdl/stub.c"
    cc -c "$host_sdl/stub.c" -o "$host_sdl/stub.o"
    ar rcs "$host_sdl/libSDL2.a" "$host_sdl/stub.o"
  fi
  rm -rf "$root/build/$hostdir"
  cmake -S "$host_source/$tree/Sources" -B "$root/build/$hostdir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DUSE_ASM=OFF -DUSE_SYSTEM_VULKAN=OFF \
    -DSDL2_INCLUDE_DIR="$root/.tools/SDL-PS4/include" \
    -DSDL2_LIBRARY_TEMP="$host_sdl/libSDL2.a" \
    > "$root/artifacts/$logdir/host-configure.log" 2>&1
  cmake --build "$root/build/$hostdir" --target "$ecc" -j"${BUILD_JOBS:-8}" \
    > "$root/artifacts/$logdir/host-build.log" 2>&1
fi
python3 "$root/scripts/compile-ps4-shaders.py" "$source/$tree/Sources"
cmake -S "$source/$tree/Sources" -B "$root/build/ps4-radv-$game" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$root/cmake/openorbis.cmake" -DSS_PS4_ROOT="$root" \
  -DSS_PS4_GFX=radv -DSS_MESA_BUILD="${MESA_BUILD:-$HOME/dolphinps4-build-pacbrew/build/mesa}" \
  -DECC="$root/build/$hostdir/$ecc" -DECC-SE="$root/build/$hostdir/$ecc" \
  -DUSE_ASM=OFF -DUSE_SINGLE_THREAD=ON -DUSE_SYSTEM_ZLIB=OFF \
  -DVulkan_INCLUDE_DIR="$MESA_SRC/include" \
  -DVulkan_LIBRARY="$HOME/dolphinps4-build-pacbrew/build/mesa/src/amd/vulkan/libvulkan_radeon.a" \
  -DSDL2_INCLUDE_DIR="$root/.tools/SDL-PS4/include" \
  -DSDL2_LIBRARY="$root/build/ps4-sdl-radv/libSDL2.a" \
  -DSDL2_LIBRARY_TEMP="$root/build/ps4-sdl-radv/libSDL2.a" \
  -DCMAKE_BUILD_TYPE=Release \
  > "$root/artifacts/$logdir/configure.log" 2>&1
cmake --build "$root/build/ps4-radv-$game" --target "$target" -j"${BUILD_JOBS:-8}" \
  > "$root/artifacts/$logdir/build.log" 2>&1
# Same program authority as the probe that mapped RADV's GPU windows.
auth=000000000000000000000000001C004000FF000000000080000000000000000000000000000000000000008000400040000000000000008000000000000000080040FFFF000000F000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
OO_PS4_TOOLCHAIN="$OPENORBIS" "$OPENORBIS/bin/create-fself" \
  -in="$root/build/ps4-radv-$game/SeriousSam" \
  -out="$root/build/runtime-radv-$game/serioussam.oelf" \
  --eboot "$root/build/runtime-radv-$game/eboot.bin" --paid 0x3800000000000035 \
  --authinfo "$auth" -library-path "$OPENORBIS/lib" \
  > "$root/artifacts/$logdir/fself.log" 2>&1
echo "RADV $game eboot: $root/build/runtime-radv-$game/eboot.bin"
echo "External /data folders are unchanged. This eboot is not a Hatshepsut result until that level is on screen."
