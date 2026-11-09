#!/usr/bin/env python3
"""Package a built RADV eboot. Game archives are not included."""
import argparse
import os
import shutil
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--game", choices=["tfe", "tse"], required=True)
parser.add_argument("--version", default="01.00")
parser.add_argument("--title-id")
parser.add_argument("--title")
parser.add_argument("--label")
parser.add_argument("--icon")
args = parser.parse_args()

title_id = args.title_id or ("SSAM00001" if args.game == "tfe" else "SSAM00002")
title = args.title or ("Serious Sam TFE" if args.game == "tfe" else "Serious Sam TSE")
label = args.label or ("SERIOUSSAMTFE001" if args.game == "tfe" else "SERIOUSSAMTSE001")
icon = Path(args.icon) if args.icon else root / f"assets/icon0-{args.game}.png"
eboot = root / f"build/runtime-radv-{args.game}/eboot.bin"
if not eboot.is_file():
    raise SystemExit(f"Missing {eboot}. Run scripts/build-ps4-radv.sh {args.game} first.")
if not icon.is_file():
    raise SystemExit(f"Missing icon {icon}")

stage = root / f"build/package-radv-{args.game}"
if stage.exists():
    shutil.rmtree(stage)
(stage / "sce_sys").mkdir(parents=True)
shutil.copy(eboot, stage / "eboot.bin")
shutil.copy(icon, stage / "sce_sys/icon0.png")
out = root / "artifacts/radv"
out.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env["SFO_STYLE"] = "plain"
make_pkg = root / "scripts/make-pkg.sh"
proc = subprocess.run(
    ["bash", str(make_pkg), str(stage), title_id, title, args.version, label, str(out)],
    text=True, env=env)
raise SystemExit(proc.returncode)
