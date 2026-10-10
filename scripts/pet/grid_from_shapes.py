"""Build the dog's source grid (head, body, eyes, mouth) from a shape recipe.

A recipe lists shapes per layer; each shape adds or discards grid cells, in order. Cell (x, y) is the
centre of an 18 x 17 grid, x to the right and y down, so cell (0, 0) is the top-left cell.

    python -I scripts/pet/grid_from_shapes.py scripts/pet/recipes/original-dog.json
    python -I scripts/pet/grid_from_shapes.py recipe.json --out "$TEMP/grid.json"

Without --out it prints an ASCII preview of each layer and writes nothing. The output feeds apply_grid.py.
"""
import argparse
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")


def inside_ellipse(x, y, shape):
    cx, cy, rx, ry = shape["cx"], shape["cy"], shape["rx"], shape["ry"]
    return ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1.0


def cells_of(op, width, height):
    """Every cell an op covers. `rows` and `cols` are inclusive bounds and default to the whole grid."""
    y0, y1 = op.get("rows", [0, height - 1])
    x0, x1 = op.get("cols", [0, width - 1])
    kind = op["shape"]
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if kind == "rect" or (kind == "ellipse" and inside_ellipse(x, y, op)):
                yield (x, y)
    if kind not in ("rect", "ellipse"):
        raise ValueError(f"unknown shape {kind!r}")


def build_layer(ops, width, height):
    cells = set()
    for op in ops:
        covered = set(cells_of(op, width, height))
        if op["op"] == "add":
            cells |= covered
        elif op["op"] == "discard":
            cells -= covered
        else:
            raise ValueError(f"unknown op {op['op']!r}")
    return cells


def rows_of(cells, width, height):
    return ["".join("#" if (x, y) in cells else "." for x in range(width)) for y in range(height)]


def preview(name, cells, width, height):
    print(name)
    for row in rows_of(cells, width, height):
        print("|" + row + "|")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("recipe", type=Path)
    parser.add_argument("--out", type=Path, help="write the grid JSON here (for apply_grid.py)")
    args = parser.parse_args()

    recipe = json.loads(args.recipe.read_text(encoding="utf-8"))
    width, height = recipe["width"], recipe["height"]
    layers = {name: build_layer(recipe["layers"][name], width, height) for name in ("head", "body")}
    eyes = [tuple(p) for p in recipe["eyes"]]
    mouth = [tuple(p) for p in recipe["mouth"]]

    for name, cells in layers.items():
        if not cells:
            raise SystemExit(f"layer {name} is empty; check the recipe")
        for x, y in cells:
            if not (0 <= x < width and 0 <= y < height):
                raise SystemExit(f"layer {name} covers ({x}, {y}), outside the {width} x {height} grid")
    for name, points in (("eyes", eyes), ("mouth", mouth)):
        for x, y in points:
            if not (0 <= x < width and 0 <= y < height):
                raise SystemExit(f"{name} cell ({x}, {y}) is outside the grid")

    if args.out is None:
        for name, cells in layers.items():
            preview(name, cells, width, height)
        print(f"eyes {eyes}")
        print(f"mouth {mouth}")
        print("preview only; pass --out to write the grid")
        return

    grid = {
        "head": rows_of(layers["head"], width, height),
        "body": rows_of(layers["body"], width, height),
        "eyes": [list(p) for p in eyes],
        "mouth": [list(p) for p in mouth],
    }
    args.out.write_text(json.dumps(grid, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
