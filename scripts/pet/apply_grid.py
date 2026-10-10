"""Write a grid from grid_from_shapes.py into the sprite source, changing only the four dog layers.

    python -I scripts/pet/apply_grid.py "$TEMP/grid.json"           # show the diff, write nothing
    python -I scripts/pet/apply_grid.py "$TEMP/grid.json" --write   # write it

The accessories, the view and the comment keep their text exactly; only the head, body, eyes and mouth
blocks are replaced. The result must still parse as JSON, or nothing is written.
"""
import argparse
import difflib
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SPRITE = ROOT / "ui-prototypes" / "pet-prototype" / "assets" / "pet" / "sprite.json"
LAYERS = ("head", "body", "eyes", "mouth")


def block_bounds(lines, key):
    """Line range [start, end] of one top-level key: a multi-line array ends at its closing `  ],`."""
    prefix = f'  "{key}": '
    start = next((i for i, line in enumerate(lines) if line.startswith(prefix)), None)
    if start is None:
        raise SystemExit(f'sprite source has no top-level "{key}" key')
    if lines[start].rstrip().endswith("["):
        end = next(i for i in range(start + 1, len(lines)) if lines[i].startswith("  ]"))
        return start, end
    return start, start


def render(key, value):
    if key in ("head", "body"):
        rows = ",\n".join(f'    "{row}"' for row in value)
        return [f'  "{key}": [', rows, "  ],"]
    pairs = ", ".join(f"[{x}, {y}]" for x, y in value)
    return [f'  "{key}": [{pairs}],']


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("grid", type=Path, help="JSON from grid_from_shapes.py --out")
    parser.add_argument("--sprite", type=Path, default=DEFAULT_SPRITE)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()

    grid = json.loads(args.grid.read_text(encoding="utf-8"))
    text = args.sprite.read_text(encoding="utf-8")
    lines = text.split("\n")
    # Replace from the bottom up so the earlier indices stay valid.
    for key in sorted(LAYERS, key=lambda k: -block_bounds(lines, k)[0]):
        start, end = block_bounds(lines, key)
        value = grid[key]
        if key in ("eyes", "mouth"):
            value = [tuple(p) for p in value]
        lines[start:end + 1] = render(key, value)
    updated = "\n".join(lines)
    json.loads(updated)  # refuse to write a file that no longer parses

    if updated == text:
        print("no change: the grid already matches")
        return
    diff = difflib.unified_diff(text.split("\n"), updated.split("\n"), str(args.sprite), "updated", lineterm="")
    print("\n".join(diff))
    if not args.write:
        print("\npreview only; pass --write to change the sprite source")
        return
    args.sprite.write_text(updated, encoding="utf-8", newline="\n")
    print(f"\nwrote {args.sprite}")


if __name__ == "__main__":
    main()
