"""Move the accessories with the dog's new geometry, and optionally move the eyes.

Accessory pixels are in dog-box units (3 units per grid cell, the dog box starts 6 units into the canvas).
Accessory rects are in grid cells. Every shift is in units and must be a multiple of 3 for rects.

First look at where the features are, then pick shifts from the numbers:

    python -I scripts/pet/refit_accessories.py                       # report only
    python -I scripts/pet/refit_accessories.py --dx-head 12 --dx-face 15 --dx-body 8 --write

--dx-head moves the head slot (hat, beanie, sprout, crown); --dx-face moves glasses and sunglasses;
--dx-body moves the body slot (bow tie). --eyes "7,3 10,3" sets the eye cells before the shift is computed.
The accessory block is written one accessory per line, the same as the shipped file.
"""
import argparse
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "ui-prototypes" / "pet-prototype" / "assets" / "pet"
UNITS_PER_CELL = 3


def mean_x(cells):
    xs = [x for x, _ in cells]
    return sum(xs) / len(xs)


def units_of(cell_x):
    """Horizontal centre, in units, of a grid cell column: a cell spans 3 units and the dog box starts at 6."""
    return 6 + UNITS_PER_CELL * cell_x + (UNITS_PER_CELL - 1) / 2


def report(doc):
    head = [(x, y) for y, row in enumerate(doc["head"]) for x, c in enumerate(row) if c == "#"]
    body = [(x, y) for y, row in enumerate(doc["body"]) for x, c in enumerate(row) if c == "#"]
    mid = mean_x(doc["eyes"])
    print(f"head centre    x = {mean_x(head):.2f} cells -> {units_of(mean_x(head)):.1f} units")
    print(f"body centre    x = {mean_x(body):.2f} cells -> {units_of(mean_x(body)):.1f} units")
    print(f"eyes midpoint  x = {mid:.2f} cells -> {units_of(mid):.1f} units")
    print(f"canvas centre  x = 8.50 cells -> {units_of(8.5):.1f} units (the 18-cell grid is centred here)")


def shift_pixels(items, dx):
    return [[x + dx, y, w, h, c] for x, y, w, h, c in items]


def shift_rects(items, dx):
    if dx % UNITS_PER_CELL:
        raise SystemExit(f"rect shift {dx} is not a multiple of {UNITS_PER_CELL} units")
    return [[x + dx // UNITS_PER_CELL, y, w, h, c] for x, y, w, h, c in items]


def compact_accessories(accessories):
    def item(value):
        if isinstance(value, str):
            return json.dumps(value, ensure_ascii=False)
        if isinstance(value, list):
            return "[" + ", ".join(item(v) for v in value) + "]"
        return str(value)

    entries = []
    for name, spec in accessories.items():
        fields = ", ".join(f'"{key}": {item(val)}' for key, val in spec.items())
        entries.append(f'    "{name}": {{ {fields} }}')
    return ['  "accessories": {', ",\n".join(entries), "  }"]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sprite", type=Path, default=ASSETS / "sprite.json")
    parser.add_argument("--manifest", type=Path, default=ASSETS / "manifest.json")
    parser.add_argument("--eyes", help='eye cells, for example "7,3 10,3"')
    parser.add_argument("--dx-head", type=int, default=0)
    parser.add_argument("--dx-face", type=int, default=0)
    parser.add_argument("--dx-body", type=int, default=0)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()

    text = args.sprite.read_text(encoding="utf-8")
    doc = json.loads(text)
    slots = {entry["id"]: entry["slot"] for entry in json.loads(args.manifest.read_text(encoding="utf-8"))["accessories"]}
    shifts = {"head": args.dx_head, "face": args.dx_face, "body": args.dx_body}

    if args.eyes:
        doc["eyes"] = [[int(v) for v in pair.split(",")] for pair in args.eyes.split()]

    report(doc)
    for name, spec in doc["accessories"].items():
        dx = shifts[slots[name]]
        if not dx:
            continue
        if "pixels" in spec:
            spec["pixels"] = shift_pixels(spec["pixels"], dx)
        if "rects" in spec:
            spec["rects"] = shift_rects(spec["rects"], dx)
        print(f"{name}: {slots[name]} slot shifted {dx} units")

    # The accessories are the last key of the document, so their block closes with `  }` just before the final `}`.
    lines = text.split("\n")
    start = next(i for i, line in enumerate(lines) if line.startswith('  "accessories": {'))
    end = max(i for i, line in enumerate(lines) if line == "  }")
    lines[start:end + 1] = compact_accessories(doc["accessories"])
    if args.eyes:
        cells = ", ".join(f"[{x}, {y}]" for x, y in doc["eyes"])
        lines = [f'  "eyes": [{cells}],' if line.startswith('  "eyes": ') else line for line in lines]
    updated = "\n".join(lines)
    assert json.loads(updated) == doc, "the rewritten file does not match the computed document"

    if updated == text:
        print("no change")
        return
    if not args.write:
        print("preview only; pass --write to change the sprite source")
        return
    args.sprite.write_text(updated, encoding="utf-8", newline="\n")
    print(f"wrote {args.sprite}")


if __name__ == "__main__":
    main()
