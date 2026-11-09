#!/usr/bin/env python3
"""Build private SDL video and semaphore adapters for the PS4 client."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
sdk = Path(os.environ.get('OO_PS4_TOOLCHAIN', '/opt/openorbis/OpenOrbis/PS4Toolchain'))
source = root / '.tools/SDL-PS4'
revision = '7774cf3e7d04448c44d569c50d1f9a4532a2efe7'
if not source.exists():
    subprocess.run(['git', 'clone', 'https://github.com/OpenOrbis/SDL-PS4.git', str(source)], check=True)
build = root / os.environ.get('SS_SDL_BUILD', 'build/ps4-sdl')
tree = build / 'source'
tree.mkdir(parents=True, exist_ok=True)
archive = subprocess.check_output(['git', '-C', str(source), 'archive', revision])
subprocess.run(['tar', '-x', '-C', str(tree)], input=archive, check=True)
objects = []
generated = root/'build/ps4-sdl-static/include'
for relative in ['src/video/SDL_video.c', 'src/video/dummy/SDL_nullvideo.c',
                 'src/video/dummy/SDL_nullevents.c', 'src/video/dummy/SDL_nullframebuffer.c',
                 'src/thread/generic/SDL_syssem.c', 'src/audio/SDL_audio.c']:
    obj = build / (Path(relative).name + '.o')
    input_file = root/'platform/ps4/sdl_semaphore.c' if relative.endswith('SDL_syssem.c') else tree/relative
    defines = ['-D__PS4__', '-D__ORBIS__', '-DSDL_VIDEO_DRIVER_DUMMY=1']
    includes = ['-I'+str(tree/'include'), '-I'+str(tree/'src')]
    if relative.endswith('SDL_audio.c'):
        defines.append('-DSDL_AUDIO_DRIVER_PS4=1')
        includes.insert(0, '-I'+str(generated))
    subprocess.run(['clang', '--target=x86_64-pc-freebsd12-elf', '-fPIC', '-O2',
                    *defines, '-isysroot', str(sdk), '-isystem', str(sdk/'include'),
                    *includes, '-c', str(input_file), '-o', str(obj)], check=True)
    objects.append(str(obj))
audio_obj = build/'sdl_ps4_audio.c.o'
subprocess.run(['clang', '--target=x86_64-pc-freebsd12-elf', '-fPIC', '-O2',
                '-D__PS4__', '-D__ORBIS__', '-DSDL_AUDIO_DRIVER_PS4=1',
                '-isysroot', str(sdk), '-isystem', str(sdk/'include'),
                '-I'+str(generated), '-I'+str(tree/'include'), '-I'+str(tree/'src'),
                '-c', str(root/'platform/ps4/sdl_ps4_audio.c'), '-o', str(audio_obj)], check=True)
objects.append(str(audio_obj))
lib = build/'libSDL2.a'
base = Path(os.environ['SS_SDL_ARCHIVE']) if os.environ.get('SS_SDL_ARCHIVE') else sdk/'lib/libSDL2.a'
shutil.copyfile(base, lib)
subprocess.run(['llvm-ar', 'r', str(lib), *objects], check=True)
print(lib)
