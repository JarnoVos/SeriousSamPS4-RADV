#!/usr/bin/env python3
"""Build the local, pinned libjbc dependency used by ScummVM's USB mapping.

No third-party source or binary is copied into the tracked project. Set
SS_JBC_SOURCE to an existing checkout/cache of the pinned revision.
"""
import hashlib
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
sdk = Path(os.environ.get('OO_PS4_TOOLCHAIN', '/opt/openorbis/OpenOrbis/PS4Toolchain'))
source = Path(os.environ.get('SS_JBC_SOURCE', root.parent / 'ScummVM-PS4/.tools/ps4-libjbc'))
revision = '835fe016ff0ae5dd89b9249f39cc0fe093fd07dd'
if (source / '.git').exists():
    actual = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
else:
    actual = (source / '.revision').read_text().strip()
if actual != revision:
    raise SystemExit('SS_JBC_SOURCE must contain ps4-libjbc revision ' + revision)
build = root / 'build/ps4-usb'
build.mkdir(parents=True, exist_ok=True)
names = ['defs.h', 'jailbreak.c', 'jailbreak.h', 'kernelrw.c', 'kernelrw.h', 'libjbc.h', 'utils.c', 'utils.h']
inputs = {}
def replace_checked(text, old, new):
    if old not in text:
        raise SystemExit('Unexpected libjbc source; missing adaptation: ' + old)
    return text.replace(old, new)
for name in names:
    original = (source / name).read_text()
    inputs[name] = hashlib.sha256(original.encode()).hexdigest()
    text = original
    if name == 'jailbreak.c':
        text = replace_checked(text, 'restart:;', 'unsigned attempts = 0;\nrestart:;\n    if (++attempts > 8) return -1;')
        text = replace_checked(text, 'for(;;)', 'for(unsigned visited = 0;; ++visited)')
        text = replace_checked(text, '        int pid;', '        if (visited >= 4096) return -1;\n        int pid;')
    elif name == 'kernelrw.c':
        text = replace_checked(text, 'jbc_krw_kcall((uintptr_t)k_get_td)', 'jbc_krw_kcall((uintptr_t)k_get_td, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL)')
        text = replace_checked(text, 'jbc_krw_kcall((uintptr_t)k_kcpy, dst, src, sz)', 'jbc_krw_kcall((uintptr_t)k_kcpy, dst, src, sz, 0ULL, 0ULL, 0ULL)')
    elif name == 'utils.c':
        for old, new in [
            ('    jbc_get_cred(&cred);', '    if (jbc_get_cred(&cred)) return;'),
            ('    jbc_jailbreak_cred(&root_cred);', '    if (jbc_jailbreak_cred(&root_cred)) return;'),
            ('    jbc_set_cred(&root_cred);', '    if (jbc_set_cred(&root_cred)) { jbc_set_cred(&cred); return; }'),
            ('nmount(data, 6, 0)', 'nmount(data, 6, 1)'), # MNT_RDONLY
            ('    return;\ninvalid:', '    p->ans = 0;\n    return;\ninvalid:'),
            ('        .ans = 0,', '        .ans = -1,'),
        ]:
            text = replace_checked(text, old, new)
    path = build / name
    if not path.exists() or path.read_text() != text:
        path.write_text(text)
flags = ['--target=x86_64-pc-freebsd12-elf', '-fPIC', '-O2', '-ffreestanding',
         '-mno-red-zone', '-fno-stack-protector', '-isysroot', str(sdk), '-isystem', str(sdk / 'include')]
fingerprint = hashlib.sha256((''.join((build / n).read_text() for n in names) + repr(flags) +
    subprocess.check_output(['clang', '--version'], text=True)).encode()).hexdigest()
stamp = build / '.build-id'
archive = build / 'libss-jbc.a'
if not (archive.exists() and stamp.exists() and stamp.read_text() == fingerprint):
    objects = []
    for name in ['jailbreak', 'kernelrw', 'utils']:
        obj = build / (name + '.o')
        subprocess.run(['clang', *flags, '-c', str(build / (name + '.c')), '-o', str(obj)], check=True)
        subprocess.run(['llvm-objcopy', '--redefine-sym', 'open=jbc_raw_open', '--redefine-sym', 'close=jbc_raw_close', str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run(['llvm-ar', 'rcs', str(archive), *objects], check=True)
    stamp.write_text(fingerprint)
(build / 'source-sha256.txt').write_text('revision ' + revision + '\n' + ''.join(h + '  ' + n + '\n' for n, h in inputs.items()))
print('USB mapping dependency built from pinned local libjbc source')
