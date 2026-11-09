#!/usr/bin/env bash
# Package a staged app directory into a PS4 fake-PKG.
#
#   make-pkg.sh <stage-dir> <TITLE_ID> <title> <version XX.YY> <content-label> <out-dir>
#
# <stage-dir> must contain eboot.bin and sce_sys/icon0.png; anything else in it
# (sce_module/, assets, ...) is packaged as-is. The Piglet + shader compiler
# modules in DolphinPS4/modules/*.sprx (Sony files, not in git, never
# redistribute) are bundled into sce_module/ unless DOLPHIN_PS4_MODULES_DIR
# points somewhere else (e.g. an empty folder for public builds).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OPENORBIS="${OPENORBIS:-/opt/pacbrew/ps4/openorbis}"

STAGE="$(readlink -f "$1")"; TITLE_ID="$2"; TITLE="$3"; VERSION="$4"; LABEL="$5"; OUT="$(readlink -f "$6")"
TOOLS="$OPENORBIS/bin/linux"
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1

[[ "$TITLE_ID" =~ ^[A-Z]{4}[0-9]{5}$ ]] || { echo "TITLE_ID must look like ABCD12345" >&2; exit 1; }
[[ "$VERSION" =~ ^[0-9]{2}\.[0-9]{2}$ ]] || { echo "version must look like 01.00" >&2; exit 1; }
[ -f "$STAGE/eboot.bin" ] || { echo "missing $STAGE/eboot.bin" >&2; exit 1; }
[ -f "$STAGE/sce_sys/icon0.png" ] || { echo "missing $STAGE/sce_sys/icon0.png" >&2; exit 1; }

mkdir -p "$STAGE/sce_module"
[ "${NO_RIGHT_SPRX:-0}" = 1 ] || mkdir -p "$STAGE/sce_sys/about"
[ "${NO_RIGHT_SPRX:-0}" = 1 ] || cp "${DOLPHIN_PS4_RIGHT_SPRX:-$OPENORBIS/samples/piglet/sce_sys/about/right.sprx}" "$STAGE/sce_sys/about/"
cp "$OPENORBIS/samples/piglet/sce_module/"*.prx "$STAGE/sce_module/"
shopt -s nullglob
for m in "${DOLPHIN_PS4_MODULES_DIR:-$HERE/../modules}"/*.sprx "${DOLPHIN_PS4_MODULES_DIR:-$HERE/../modules}"/*.prx; do
    echo "bundling module $(basename "$m")"
    cp "$m" "$STAGE/sce_module/"
done
shopt -u nullglob

LABEL="$(printf '%s' "$LABEL" | tr '[:lower:]' '[:upper:]' | tr -cd 'A-Z0-9')"
LABEL="$(printf '%-16s' "${LABEL:0:16}" | tr ' ' '0')"
CONTENT_ID="IV0000-${TITLE_ID}_00-${LABEL}"

SFO="$STAGE/sce_sys/param.sfo"
rm -f "$SFO"
"$TOOLS/PkgTool.Core" sfo_new "$SFO"
set_entry() { "$TOOLS/PkgTool.Core" sfo_setentry "$SFO" "$1" --type "$2" --maxsize "$3" --value "$4" >/dev/null; }
if [ "${SFO_STYLE:-retroarch}" = retroarch ]; then
    # RetroArch for PS4's launch parameters: with the OpenOrbis sample values
    # the shell's Piglet never returns an EGL display (verified by love-ps4).
    set_entry APP_TYPE Integer 4 0
    set_entry ATTRIBUTE Integer 4 0x20814016
    set_entry ATTRIBUTE2 Integer 4 0x6
    set_entry CATEGORY Utf8 4 gde
    set_entry FORMAT Utf8 4 obs
    set_entry SYSTEM_VER Integer 4 0x3fc
else
    # Plain OpenOrbis/PSChrome values (known to launch with the default PAID).
    set_entry APP_TYPE Integer 4 1
    # 0x2: system dialogs (the keyboard) confirm with Cross. Without it they follow the
    # console's region default, which is Circle on some consoles.
    set_entry ATTRIBUTE Integer 4 0x2
    set_entry CATEGORY Utf8 4 gd
    set_entry SYSTEM_VER Integer 4 0
fi
set_entry APP_VER Utf8 8 "$VERSION"
set_entry CONTENT_ID Utf8 48 "$CONTENT_ID"
set_entry DOWNLOAD_DATA_SIZE Integer 4 0
set_entry TITLE Utf8 128 "$TITLE"
set_entry TITLE_ID Utf8 12 "$TITLE_ID"
set_entry VERSION Utf8 8 "$VERSION"

rm -f "$STAGE/pkg.gp4"
python3 - "$STAGE" "$CONTENT_ID" <<'PY'
import os, sys, datetime
from xml.sax.saxutils import quoteattr

stage, content_id = sys.argv[1], sys.argv[2]
files = []
dirs = {}

for root, subdirs, filenames in os.walk(stage):
    rel_root = os.path.relpath(root, stage).replace("\\", "/")
    if rel_root == ".":
        rel_root = ""
    parts = [p for p in rel_root.split("/") if p]
    cur = dirs
    for p in parts:
        cur = cur.setdefault(p, {})
    for d in sorted(subdirs):
        cur.setdefault(d, {})
    for fn in sorted(filenames):
        if rel_root == "" and fn == "pkg.gp4":
            continue
        rel_path = f"{rel_root}/{fn}" if rel_root else fn
        files.append(rel_path)

def render_dirs(dmap, indent="\t\t"):
    out = ""
    for name in sorted(dmap.keys()):
        children = dmap[name]
        if children:
            out += f"{indent}<dir targ_name={quoteattr(name)}>\n"
            out += render_dirs(children, indent + "\t")
            out += f"{indent}</dir>\n"
        else:
            out += f"{indent}<dir targ_name={quoteattr(name)} />\n"
    return out

ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
with open(os.path.join(stage, "pkg.gp4"), "w", encoding="utf-8", newline="\n") as f:
    f.write('<?xml version="1.0"?>\n')
    f.write('<psproject xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" fmt="gp4" version="1000">\n')
    f.write('\t<volume>\n')
    f.write('\t\t<volume_type>pkg_ps4_app</volume_type>\n')
    f.write('\t\t<volume_id>PS4VOLUME</volume_id>\n')
    f.write(f'\t\t<volume_ts>{ts}</volume_ts>\n')
    f.write(f'\t\t<package content_id="{content_id}" passcode="00000000000000000000000000000000" storage_type="digital50" app_type="full" />\n')
    f.write('\t\t<chunk_info chunk_count="1" scenario_count="1">\n')
    f.write('\t\t\t<chunks>\n\t\t\t\t<chunk id="0" layer_no="0" label="Chunk #0" />\n\t\t\t</chunks>\n')
    f.write('\t\t\t<scenarios default_id="0">\n\t\t\t\t<scenario id="0" type="sp" initial_chunk_count="1" label="Scenario #0">0</scenario>\n\t\t\t</scenarios>\n')
    f.write('\t\t</chunk_info>\n')
    f.write('\t</volume>\n')
    f.write('\t<files img_no="0">\n')
    for fp in sorted(files):
        q = quoteattr(fp)
        f.write(f'\t\t<file targ_path={q} orig_path={q} />\n')
    f.write('\t</files>\n')
    f.write('\t<rootdir>\n')
    f.write(render_dirs(dirs))
    f.write('\t</rootdir>\n')
    f.write('</psproject>\n')
PY
mkdir -p "$OUT"
( cd "$STAGE" && "$TOOLS/PkgTool.Core" pkg_build pkg.gp4 "$OUT" ) >/dev/null
rm -f "$STAGE/pkg.gp4"
echo "$OUT/$CONTENT_ID.pkg"
