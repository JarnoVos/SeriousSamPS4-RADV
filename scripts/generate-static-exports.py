#!/usr/bin/env python3
"""Create the PS4 resolver from actual game archive exports."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
parser.add_argument("archives", nargs="+")
args = parser.parse_args()
raw = subprocess.check_output(["llvm-nm", "--defined-only", "--extern-only", "--just-symbol-name", *args.archives], text=True)
names = sorted({n for n in raw.split() if re.fullmatch(r"(?:[A-Za-z0-9_]+_DLLClass|Shader_[A-Za-z0-9_]+|GAME_Create|ov_[A-Za-z0-9_]+|al[A-Z][A-Za-z0-9_]+)", n)})
if "GAME_Create" not in names or not any(n.endswith("_DLLClass") for n in names):
    raise SystemExit("Missing game/entity exports")
lines = ['#include <string.h>']
for i, name in enumerate(names):
    lines.append(f'extern "C" char ss_export_{i}[] __asm__("{name}");')
lines.append('void *ss_ps4_find_export(const char *name) {')
for i, name in enumerate(names):
    lines.append(f'  if (!strcmp(name, "{name}")) return ss_export_{i};')
lines.extend(['  return nullptr;', '}'])
args.output.write_text('\n'.join(lines) + '\n')
print(f"Generated {len(names)} static exports")
