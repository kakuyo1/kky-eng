"""Draw one body frame with its expression, and each accessory on it, into a review sheet.

    python -I scripts/pet/compose_check.py "$TEMP/pet-check.png"
    python -I scripts/pet/compose_check.py out.png --action study --frame 2

The first panel is the bare frame; each following panel adds one accessory, so a bad fit shows up beside the
frame it belongs to. Writes only the given PNG. Run sync_sprites.py first if you want the app's files checked.
"""
import argparse
import json
import sys
from pathlib import Path

from PIL import Image

sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "ui-prototypes" / "pet-prototype" / "assets" / "pet"
CANVAS = 88
SCALE = 2


def cell(sheet, index):
    return sheet.crop((index * CANVAS, 0, (index + 1) * CANVAS, CANVAS))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out", type=Path)
    parser.add_argument("--action", default="idle")
    parser.add_argument("--frame", type=int, default=0)
    args = parser.parse_args()

    manifest = json.loads((ASSETS / "manifest.json").read_text(encoding="utf-8"))
    action = manifest["actions"][args.action]
    if not 0 <= args.frame < action["frames"]:
        raise SystemExit(f"{args.action} has {action['frames']} frames; --frame {args.frame} is out of range")

    body = cell(Image.open(ASSETS / action["sheet"]).convert("RGBA"), args.frame)
    expressions = action.get("expressions", [])
    expression_index = expressions[args.frame] if args.frame < len(expressions) else 0
    faces = Image.open(ASSETS / manifest["expression"]).convert("RGBA")
    base = body.copy()
    base.alpha_composite(cell(faces, expression_index))

    accessories = manifest["accessories"]
    panels = [("bare", None)] + [(entry["id"], entry) for entry in accessories]
    sheet = Image.new("RGBA", (CANVAS * len(panels), CANVAS), (255, 255, 255, 255))
    for i, (_, entry) in enumerate(panels):
        panel = base.copy()
        if entry is not None:
            panel.alpha_composite(Image.open(ASSETS / entry["asset"]).convert("RGBA"))
        sheet.paste(panel, (i * CANVAS, 0), panel)
    sheet = sheet.resize((sheet.width * SCALE, sheet.height * SCALE), Image.NEAREST)
    sheet.save(args.out)
    print("panels:", ", ".join(name for name, _ in panels))
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
