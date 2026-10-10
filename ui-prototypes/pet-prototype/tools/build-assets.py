"""Render the pet's PNG layers from assets/pet/sprite.json and manifest.json.

Run from anywhere: python tools/build-assets.py
Writes body sheets, the expression sheet, the effect sheet and accessory PNGs under assets/pet/.
The grid in sprite.json is the source; edit it and rerun instead of editing PNGs by hand.
"""
import json
import pathlib

from PIL import Image, ImageDraw

ASSETS = pathlib.Path(__file__).resolve().parents[1] / "assets" / "pet"
CANVAS = 64
INK = (26, 26, 31, 255)
DOG = (251, 251, 253, 255)
ACCENT = (47, 158, 110, 255)  # --ok light token from ui-prototypes/pet.html
COLOURS = {"ink": INK, "accent": ACCENT}
NEIGHBOURS = [(1, 0), (-1, 0), (0, 1), (0, -1)]

sprite = json.loads((ASSETS / "sprite.json").read_text(encoding="utf-8"))
manifest = json.loads((ASSETS / "manifest.json").read_text(encoding="utf-8"))
view = sprite["view"]


def cell_box(x, y, w=1, h=1, scale=None):
    """Canvas pixel box (inclusive end) of a grid cell rectangle."""
    s = view["scale"] if scale is None else scale
    left = view["x"] + x * s
    top = view["y"] + y * s
    return (left, top, left + w * s - 1, top + h * s - 1)


def grid_cells(rows):
    return {(x, y) for y, row in enumerate(rows) for x, ch in enumerate(row) if ch == "#"}


def new_sheet(frames):
    return Image.new("RGBA", (CANVAS * frames, CANVAS), (0, 0, 0, 0))


def paste_frame(sheet, index, draw_fn):
    frame = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(frame))
    sheet.paste(frame, (index * CANVAS, 0))


# The dog: filled cells plus the face holes (eyes, mouth), then a one-cell ink outline around that union.
body_cells = grid_cells(sprite["head"]) | grid_cells(sprite["body"])
features = {tuple(p) for p in sprite["eyes"] + sprite["mouth"]}
union = body_cells | features
outline = {(x + dx, y + dy) for x, y in union for dx, dy in NEIGHBOURS} - union


def draw_body(draw):
    for x, y in outline:
        draw.rectangle(cell_box(x, y), fill=INK)
    for x, y in union:
        draw.rectangle(cell_box(x, y), fill=DOG)


def eye_box(x, y, top, bottom):
    """Ink band inside an eye cell, in canvas rows top..bottom (0..2 is the whole cell)."""
    left, y0, right, _ = cell_box(x, y)
    return (left, y0 + top, right, y0 + bottom)


# Body sheets: the same silhouette in every frame. Motion comes from manifest anchors, shared by all layers.
for name, action in manifest["actions"].items():
    sheet = new_sheet(action["frames"])
    for i in range(action["frames"]):
        paste_frame(sheet, i, draw_body)
    sheet.save(ASSETS / action["sheet"])

# Expression sheet: 0 open, 1 half closed, 2 closed. The mouth is on every frame.
EYE_BANDS = [(0, 2), (1, 2), (1, 1)]  # ink rows inside the eye cell for each frame
expr = new_sheet(len(EYE_BANDS))
for i, (top, bottom) in enumerate(EYE_BANDS):
    def draw_face(draw, top=top, bottom=bottom):
        for x, y in sprite["mouth"]:
            draw.rectangle(cell_box(x, y), fill=INK)
        for x, y in sprite["eyes"]:
            draw.rectangle(cell_box(x, y), fill=DOG)
            draw.rectangle(eye_box(x, y, top, bottom), fill=INK)
    paste_frame(expr, i, draw_face)
expr.save(ASSETS / manifest["expression"])

# Sleep effect: a small 'z' rising beside the head, one frame per action frame.
ZZ = ["###", ".#.", "###"]
effect = new_sheet(manifest["actions"]["sleep"]["frames"])
for i in range(manifest["actions"]["sleep"]["frames"]):
    def draw_z(draw, i=i):
        top = 16 - 3 * i
        for y, row in enumerate(ZZ):
            for x, ch in enumerate(row):
                if ch == "#":
                    draw.rectangle((46 + 2 * x, top + 2 * y, 46 + 2 * x + 1, top + 2 * y + 1), fill=INK)
    paste_frame(effect, i, draw_z)
effect.save(ASSETS / manifest["actions"]["sleep"]["effect"])

# Accessories: one full canvas PNG each. `rects` are in grid cells; `pixels` are in canvas units for
# thin parts (glasses frames) that are narrower than one cell.
for acc in manifest["accessories"]:
    spec = sprite["accessories"][acc["id"]]
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    for x, y, w, h, colour in spec.get("rects", []):
        draw.rectangle(cell_box(x, y, w, h), fill=COLOURS[colour])
    for x, y, w, h, colour in spec.get("pixels", []):
        draw.rectangle((x, y, x + w - 1, y + h - 1), fill=COLOURS[colour])
    (ASSETS / acc["asset"]).parent.mkdir(parents=True, exist_ok=True)
    img.save(ASSETS / acc["asset"])

print("assets written to", ASSETS)
