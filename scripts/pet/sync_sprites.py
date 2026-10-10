"""Render the prototype's PNG layers, check their sizes, and copy them into data/pet/sprites.

    python -I scripts/pet/sync_sprites.py              # render, compare, check sizes; change nothing else
    python -I scripts/pet/sync_sprites.py --apply      # also copy every render into data/pet/sprites
    python -I scripts/pet/sync_sprites.py --no-render  # compare what is already rendered

The renders in ui-prototypes/pet-prototype/assets/pet are the source; data/pet/sprites is what the app loads.
Every PNG the app reads has a fixed size: a body or effect sheet is frames x 88 wide and 88 tall, and the
expression sheet is a row of 88 x 88 cells. A size error is reported here, before the app refuses the pet.
"""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "ui-prototypes" / "pet-prototype" / "assets" / "pet"
BUILD = ROOT / "ui-prototypes" / "pet-prototype" / "tools" / "build-assets.py"
TARGET = ROOT / "data" / "pet" / "sprites"
CANVAS = 88


def check_sizes(manifest):
    problems = []
    for name, action in manifest["actions"].items():
        for key in ("sheet", "effect"):
            if key not in action:
                continue
            width, height = Image.open(ASSETS / action[key]).size
            want = action["frames"] * CANVAS
            if (width, height) != (want, CANVAS):
                problems.append(f"{action[key]}: {width}x{height}, want {want}x{CANVAS} for {name}")
    width, height = Image.open(ASSETS / manifest["expression"]).size
    if height != CANVAS or width % CANVAS:
        problems.append(f"{manifest['expression']}: {width}x{height} is not a row of {CANVAS}x{CANVAS} cells")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--no-render", action="store_true", help="skip build-assets.py and compare what exists")
    parser.add_argument("--apply", action="store_true", help="copy the renders into data/pet/sprites")
    args = parser.parse_args()

    if not args.no_render:
        subprocess.run([sys.executable, "-I", str(BUILD)], check=True, stdout=subprocess.DEVNULL)

    manifest = json.loads((ASSETS / "manifest.json").read_text(encoding="utf-8"))
    problems = check_sizes(manifest)
    for problem in problems:
        print("SIZE", problem)
    if problems:
        raise SystemExit("sizes do not match the frame counts; fix the source before copying")

    renders = sorted(p.relative_to(ASSETS) for p in ASSETS.rglob("*.png"))
    changed = []
    for rel in renders:
        target = TARGET / rel
        if not target.exists():
            status = "NEW"
        elif target.read_bytes() == (ASSETS / rel).read_bytes():
            status = "same"
        else:
            status = "CHANGED"
        if status != "same":
            changed.append(rel)
        print(f"{status:8} {rel.as_posix()}")

    if not args.apply:
        print(f"\n{len(changed)} of {len(renders)} differ from data/pet/sprites; pass --apply to copy them")
        return
    for rel in renders:
        target = TARGET / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ASSETS / rel, target)
    print(f"\ncopied {len(renders)} renders into {TARGET.relative_to(ROOT).as_posix()}")


if __name__ == "__main__":
    main()
