#!/usr/bin/env python3
"""Compile the PS4 descriptor layout into the engine's embedded SPIR-V."""
from pathlib import Path
import shutil
import subprocess
import sys

source = Path(sys.argv[1]) / 'Engine/Graphics/Vulkan/Shaders'
glslc = shutil.which('glslc')
glslang = shutil.which('glslangValidator')
spirv_val = shutil.which('spirv-val')
if not glslc and not glslang:
    raise SystemExit('Need glslc or glslangValidator to compile the PS4 shaders.')
for filename, name in [('Textured.frag', 'TexturedFrag'), ('TexturedAlpha.frag', 'TexturedAlphaFrag')]:
    binary = source/'CompiledSpirv'/(name+'.spv')
    if glslc:
        subprocess.run(['glslc', '--target-env=vulkan1.1', str(source/filename), '-o', str(binary)], check=True)
    else:
        subprocess.run([glslang, '-V', '--target-env', 'vulkan1.1', '-o', str(binary), str(source/filename)], check=True)
    if spirv_val:
        subprocess.run([spirv_val, '--target-env', 'vulkan1.1', str(binary)], check=True)
    data = binary.read_bytes()
    header = f'alignas(4) unsigned char {name}_Spirv[] = {{' + ','.join(map(str, data)) + '};\n'
    header += f'unsigned int {name}_Size = {len(data)};\n'
    (source/'Compiled'/(name+'Spv.h')).write_text(header)
