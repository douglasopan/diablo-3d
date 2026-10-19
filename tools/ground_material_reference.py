#!/usr/bin/env python3
"""Copy and display local native ground evidence without changing source pixels.

Output contains proprietary-data-derived reference images: keep it private.
This does not classify materials, install assets, or modify the game map.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ground", type=Path, required=True)
    parser.add_argument("--atlas", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    source = json.loads(args.ground.read_text(encoding="utf-8"))
    entries = sorted(source["textures"], key=lambda item: -1 if item["key"] == "fallback" else int(item["key"][1:]))
    font = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 16)
    cell_w, cell_h, cols = 198, 130, 8
    records = []
    for page_no, start in enumerate(range(0, len(entries), 80)):
        selected = entries[start:start + 80]
        contact = Image.new("RGB", (cols * cell_w, 40 + ((len(selected) + cols - 1) // cols) * cell_h), (28, 28, 32))
        draw = ImageDraw.Draw(contact)
        draw.text((12, 8), "LOCAL NATIVE EVIDENCE / 3x nearest pixels / IDs are not material names", font=font, fill="white")
        for i, entry in enumerate(selected):
            path = args.ground.parent / entry["path"]
            im = Image.open(path).convert("RGBA")
            if im.size != (64, 32):
                raise ValueError(f"Expected native 64x32 pixels: {entry['key']}")
            x, y = (i % cols) * cell_w, 40 + (i // cols) * cell_h
            panel = Image.new("RGBA", im.size, (64, 64, 68, 255))
            panel.alpha_composite(im)
            contact.paste(panel.convert("RGB").resize((192, 96), Image.Resampling.NEAREST), (x, y + 25))
            draw.text((x + 4, y + 2), entry["key"], font=font, fill="white")
            records.append({"key": entry["key"], "source": str(path.resolve()), "sha256": sha256(path), "dimensions": list(im.size), "role": "native RGBA evidence, not semantic classification"})
        contact.save(args.output / f"native-pieces-{page_no:02d}.png")
    atlas = Image.open(args.atlas)
    # Only crop existing art. These bounds are recorded for repeatability.
    crops = {"native-town-overview": (1600, 800, 4400, 3100),
             "native-path-grass": (2700, 1850, 3500, 2500),
             "native-river-bank": (3650, 1750, 4550, 2500)}
    for name, bounds in crops.items():
        atlas.crop(bounds).save(args.output / (name + ".png"))
    shutil.copy2(args.ground, args.output / "native-ground-reference.source.json")
    manifest = {"schema": 1, "private": True, "sourceKind": "original-game-derived-local-export", "groundManifest": {"path": str(args.ground.resolve()), "sha256": sha256(args.ground)}, "atlas": {"path": str(args.atlas.resolve()), "sha256": sha256(args.atlas), "dimensions": list(atlas.size)}, "transforms": {"contacts": "RGBA composited on gray only for display, nearest 3x", "crops": crops}, "textures": records, "rights": "Local reference evidence; no redistribution clearance or artistic acceptance implied."}
    (args.output / "reference-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"textureCount": len(entries), "contactPages": (len(entries) + 79) // 80, "output": str(args.output)}))


if __name__ == "__main__":
    main()
