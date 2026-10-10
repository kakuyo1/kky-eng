"""Render the pet's PNG layers from assets/pet/sprite.json and manifest.json.

Run from anywhere: python tools/build-assets.py
Every frame is W x H. The dog's 64x64 box sits at (OFFSET_X, OFFSET) inside the frame: the left margin
holds the book, the top margin holds jumps, headwear and effects. Drawing helpers take dog-box
coordinates (0,0 = top-left of the dog box), so negative values land in the margins.
"""
import json
import math
import pathlib

from PIL import Image, ImageDraw

ASSETS = pathlib.Path(__file__).resolve().parents[1] / "assets" / "pet"
DOG = 64
OFFSET_X = 24  # dog-box x = 0 sits this many columns into the frame
OFFSET = 24  # dog-box y = 0 sits this many rows down the frame
W = OFFSET_X + DOG
H = OFFSET + DOG
INK = (26, 26, 31, 255)
FUR = (251, 251, 253, 255)
COLOURS = {
    "ink": INK,
    "accent": (47, 158, 110, 255),  # --ok light token from ui-prototypes/pet.html
    "gold": (242, 186, 64, 255),
    "rose": (232, 96, 130, 255),
    "sky": (82, 160, 236, 255),
}
NEIGHBOURS = [(1, 0), (-1, 0), (0, 1), (0, -1)]

sprite = json.loads((ASSETS / "sprite.json").read_text(encoding="utf-8"))
manifest = json.loads((ASSETS / "manifest.json").read_text(encoding="utf-8"))
view = sprite["view"]


def cell_box(x, y, w=1, h=1):
    """Dog-box pixel box (inclusive end) of a grid cell rectangle."""
    s = view["scale"]
    left = view["x"] + x * s
    top = view["y"] + y * s
    return (left, top, left + w * s - 1, top + h * s - 1)


def grid_cells(rows):
    return {(x, y) for y, row in enumerate(rows) for x, ch in enumerate(row) if ch == "#"}


class Frame:
    """One W x H frame. Every method takes dog-box coordinates."""

    def __init__(self):
        self.img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        self.draw = ImageDraw.Draw(self.img)

    def box(self, x0, y0, x1, y1, colour):
        self.draw.rectangle((x0 + OFFSET_X, y0 + OFFSET, x1 + OFFSET_X, y1 + OFFSET), fill=colour)

    def rect(self, x, y, w, h, colour):
        """Unit rectangle: (x, y) is the top-left unit, w and h are in units."""
        self.box(x, y, x + w - 1, y + h - 1, colour)

    def cell(self, x, y, w, h, colour):
        self.box(*cell_box(x, y, w, h), colour)

    def glyph(self, rows, x, y, colour, s=2):
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch == "#":
                    self.rect(x + i * s, y + j * s, s, s, colour)


# The standing dog: head and body cells, the face features on top, then a one-cell ink outline.
dog_cells = grid_cells(sprite["head"]) | grid_cells(sprite["body"]) | {tuple(p) for p in sprite["eyes"] + sprite["mouth"]}
EYES = [tuple(p) for p in sprite["eyes"]]
MOUTH = [tuple(p) for p in sprite["mouth"]]


def outline_of(cells):
    return {(x + dx, y + dy) for x, y in cells for dx, dy in NEIGHBOURS} - cells


def silhouette(frame, cells):
    for x, y in outline_of(cells):
        frame.cell(x, y, 1, 1, INK)
    for x, y in cells:
        frame.cell(x, y, 1, 1, FUR)


def standing(n):
    frames = []
    for _ in range(n):
        f = Frame()
        silhouette(f, dog_cells)
        frames.append(f)
    return frames


def sleep(n):
    """The standing dog lying down: the head moves down to rest on the ground, the body is squashed
    to half its height, and the closed eyes stay in the head's holes. Breathing comes from anchors."""
    head = {(x, y + 6) for x, y in grid_cells(sprite["head"])}
    body = {(x, 9 + (y - 4) // 2) for x, y in grid_cells(sprite["body"]) if y >= 4}
    eyes = {(x, y + 6) for x, y in EYES}
    fur = head | body | eyes
    # Nose, then a small closed smile under it, in dog-box units (1 unit per pixel).
    face_pixels = [(17, 36), (18, 36), (15, 37), (20, 37), (16, 38), (17, 38), (18, 38), (19, 38)]
    frames = []
    for _ in range(n):
        f = Frame()
        silhouette(f, fur)
        for x, y in face_pixels:
            f.box(x, y, x, y, INK)
        for x, y in eyes:
            left, top, right, _ = cell_box(x, y)
            f.box(left, top + 1, right, top + 1, INK)
        frames.append(f)
    return frames


# Book: black cover, white pages, held upright on the dog's left. No hands: it is drawn as an object.
BOOK = (-13, 14, 4, 36)  # x0, y0, x1, y1 in dog-box units


def book(f):
    x0, y0, x1, y1 = BOOK
    f.box(x0, y0, x1, y1, INK)
    f.box(x0 + 1, y0 + 1, x1 - 1, y1 - 1, FUR)
    f.box(x0 + 1, y0 + 1, x0 + 2, y1 - 1, INK)  # spine
    for ty in range(y0 + 4, y1 - 3, 4):  # text lines on the page
        f.rect(x0 + 5, ty, 7, 1, INK)


def fireworks(n):
    frames = []
    bursts = [(12, -6, 1, "gold"), (52, -4, 2, "rose"), (32, -6, 3, "sky")]  # x, y, first frame, colour
    for i in range(n):
        f = Frame()
        for x, y, start, colour in bursts:
            age = i - start
            if not 0 <= age < 5:
                continue
            radius = 2 + 2 * age
            size = 3 if age < 3 else 2
            for k in range(8):
                angle = k * math.pi / 4 + age * 0.3
                f.rect(x + round(radius * math.cos(angle)), y + round(radius * math.sin(angle)),
                       size, size, COLOURS[colour])
        frames.append(f)
    return frames


HEART = ["##.##", "#####", ".###.", "..#.."]
EXCLAIM = ["##", "##", "##", "..", "##"]
QUESTION = ["###", "..#", ".##", "...", ".#."]
DROP = ["..#..", ".###.", "#####", ".###."]
ZZ = ["###", ".#.", "###"]


def hearts(n):
    frames = []
    for i in range(n):
        f = Frame()
        f.glyph(HEART, 44, 2 - 5 * i, COLOURS["rose"], s=3)
        if i >= 1:
            f.glyph(HEART, 4, -5 * (i - 1), COLOURS["rose"], s=3)
        frames.append(f)
    return frames


def thought(n):
    frames = []
    for i in range(n):
        f = Frame()
        f.rect(40, 2, 2, 2, INK)
        f.rect(43, -3, 2, 2, INK)
        f.box(44, -16, 60, -6, INK)
        f.box(45, -15, 59, -7, FUR)
        for k in range(min(i + 1, 3)):
            f.rect(47 + 4 * k, -12, 2, 2, INK)
        frames.append(f)
    return frames


def pokes(n):
    frames = []
    for i in range(n):
        f = Frame()
        if i < 3:
            f.glyph(EXCLAIM, 34, -18, INK, s=3)
            f.rect(2, 2, 3, 2, COLOURS["gold"])
            f.rect(50, 2, 3, 2, COLOURS["gold"])
        frames.append(f)
    return frames


def sweat(n):
    frames = []
    for i in range(n):
        f = Frame()
        f.glyph(DROP, 42, 4 * i, COLOURS["sky"], s=3)
        frames.append(f)
    return frames


def question(n):
    frames = []
    for i in range(n):
        f = Frame()
        if 1 <= i <= 4:
            f.glyph(QUESTION, 46, -16, INK, s=3)
        frames.append(f)
    return frames


def motion_lines(n):
    frames = []
    for i in range(n):
        f = Frame()
        if i >= 2:
            f.rect(50, 8, 6, 2, INK)
        if i >= 3:
            f.rect(52, 14, 6, 2, INK)
        if i >= 4:
            f.rect(50, 20, 6, 2, INK)
        frames.append(f)
    return frames


def zzz(n):
    frames = []
    for i in range(n):
        f = Frame()
        f.glyph(ZZ, 42, -2 - 5 * i, INK)
        if i >= 1:
            f.glyph(ZZ, 52, -8 - 5 * (i - 1), INK, s=1)
        frames.append(f)
    return frames


BODY = {
    "idle": standing, "study": standing, "thinking": standing, "celebrate": standing,
    "encourage": standing, "click_react": standing, "pickup": standing, "look_around": standing,
    "yawn": standing, "stretch": standing, "sleep": sleep,
}


def reading_book(n):
    frames = []
    for _ in range(n):
        f = Frame()
        book(f)
        frames.append(f)
    return frames


EFFECTS = {
    "study": reading_book, "thinking": thought, "celebrate": fireworks,
    "encourage": hearts, "click_react": pokes, "pickup": sweat, "look_around": question,
    "stretch": motion_lines, "sleep": zzz,
}


def save_frames(name, frames):
    sheet = Image.new("RGBA", (W * len(frames), H), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sheet.paste(f.img, (i * W, 0))
    path = ASSETS / name
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)


for name, action in manifest["actions"].items():
    save_frames(action["sheet"], BODY[name](action["frames"]))
    if "effect" in action:
        save_frames(action["effect"], EFFECTS[name](action["frames"]))

# Expression sheet: eye patterns inside each 3x3 eye cell ('#' is ink), then the mouth.
EYE_PATTERNS = [
    ["###", "###", "###"],  # 0 open
    ["...", "###", "###"],  # 1 half
    ["...", "###", "..."],  # 2 closed
    ["##.", "##.", "##."],  # 3 looking left
    [".##", ".##", ".##"],  # 4 looking right
    [".#.", "#.#", "..."],  # 5 happy, a ^ arc
    ["###", "###", "..."],  # 6 looking up
    ["...", "###", "..."],  # 7 yawn: eyes shut, mouth opened below
    None,                   # 8 nothing: the sleeping body draws its own eyes
]
YAWN_MOUTH = (2, 6, 4, 2)  # x, y, w, h in cells


def face(index):
    f = Frame()
    pattern = EYE_PATTERNS[index]
    if pattern is None:
        return f
    if index == 7:
        f.cell(*YAWN_MOUTH, INK)
    else:
        for x, y in MOUTH:
            f.cell(x, y, 1, 1, INK)
    for x, y in EYES:
        f.cell(x, y, 1, 1, FUR)
        left, top, _, _ = cell_box(x, y)
        for j, row in enumerate(pattern):
            for i, ch in enumerate(row):
                if ch == "#":
                    f.rect(left + i, top + j, 1, 1, INK)
    return f


save_frames(manifest["expression"], [face(i) for i in range(len(EYE_PATTERNS))])

# Accessories: one full frame each. `rects` are in grid cells, `pixels` in units of the dog box.
for acc in manifest["accessories"]:
    spec = sprite["accessories"][acc["id"]]
    f = Frame()
    for x, y, w, h, colour in spec.get("rects", []):
        f.cell(x, y, w, h, COLOURS[colour])
    for x, y, w, h, colour in spec.get("pixels", []):
        f.rect(x, y, w, h, COLOURS[colour])
    path = ASSETS / acc["asset"]
    path.parent.mkdir(parents=True, exist_ok=True)
    f.img.save(path)

print("assets written to", ASSETS)
