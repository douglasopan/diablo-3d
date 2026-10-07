#!/usr/bin/env python3
"""Produce a local, unaligned audit of frozen ground-shadow mattes.

Requires Pillow and a town_view_smoke native primitive export. These images are
private analysis of the user's game data; do not commit the generated output.
The proposed column applies the exact authored header data, not a darkness
threshold. It is an expected result, never represented as a runtime capture.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw

from check_ground_shadow_pixels import load_sources, read_masks


def rgba(indices: list[int], opacity: list[int], palette: list[list[int]]) -> Image.Image:
    image = Image.new("RGBA", (64, 32))
    image.putdata([(*palette[index], 255 if covered else 0) for index, covered in zip(indices, opacity)])
    return image


def expected_indices(entry: dict, pieces: dict[int, dict], masks: dict[int, tuple[int, list[int]]]) -> list[int]:
    result = list(entry["indices"])
    if entry["piece"] not in masks:
        return result
    donor, rows = masks[entry["piece"]]
    for offset, covered in enumerate(entry["opacity"]):
        y, x = divmod(offset, 64)
        if covered and pieces[donor]["opacity"][offset] and rows[y] & (1 << x):
            result[offset] = pieces[donor]["indices"][offset]
    return result


def panel(image: Image.Image, scale: int) -> Image.Image:
    background = Image.new("RGBA", image.size, (65, 65, 65, 255))
    background.alpha_composite(image)
    return background.convert("RGB").resize((64 * scale, 32 * scale), Image.Resampling.NEAREST)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--header", type=Path, default=Path(__file__).resolve().parents[1] / "Source/engine/render/town_ground_shadow.hpp")
    args = parser.parse_args()
    directory = args.captures / "ground-shadow-sources"
    source, pieces = load_sources(directory)
    masks = read_masks(args.header)
    required = set(masks) | {donor for donor, _ in masks.values()}
    if required - set(pieces):
        raise ValueError(f"Missing native floor exports: {sorted(required - set(pieces))}")
    args.output.mkdir(parents=True, exist_ok=True)
    palette = source["palette"]
    scale, row_height = 4, 158
    contact = Image.new("RGB", (64 * scale * 4, row_height * len(masks)), (24, 24, 24))
    draw = ImageDraw.Draw(contact)
    results = []
    for row_number, (piece, (donor, rows)) in enumerate(sorted(masks.items())):
        entry = pieces[piece]
        if entry["opacity"] != pieces[donor]["opacity"]:
            raise ValueError(f"Piece {piece}: donor coverage differs from the native floor")
        selected = [bool(rows[y] & (1 << x)) for y in range(32) for x in range(64)]
        if any(chosen and not covered for chosen, covered in zip(selected, entry["opacity"])):
            raise ValueError(f"Piece {piece}: mask escapes native coverage")
        proposed = expected_indices(entry, pieces, masks)
        original_image = rgba(entry["indices"], entry["opacity"], palette)
        overlay = original_image.copy()
        overlay.putdata([((180 + color[0]) // 2, color[1] // 2, color[2] // 2, color[3]) if chosen else color
                         for color, chosen in zip((original_image.getpixel((x, y)) for y in range(32) for x in range(64)), selected)])
        cached = Image.open(directory / f"piece-{piece}-cached-floor-indexed.png").convert("RGB")
        cached.putalpha(Image.frombytes("L", (64, 32), bytes(255 if value else 0 for value in entry["opacity"])))
        images = (original_image, overlay, cached, rgba(proposed, entry["opacity"], palette))
        labels = ("native primitive", "frozen mask (red)", "captured runtime", "expected masked result")
        y = row_number * row_height
        for column, (image, label) in enumerate(zip(images, labels)):
            x = column * 64 * scale
            draw.text((x + 4, y + 2), f"{piece} -> {donor}: {label}", fill="white")
            contact.paste(panel(image, scale), (x, y + 28))
        covered = sum(entry["opacity"])
        count = sum(selected)
        results.append({"piece": piece, "donor": donor, "sourceTiles": entry["sourceTiles"],
                        "donorTiles": pieces[donor]["sourceTiles"], "coveredPixels": covered,
                        "maskPixels": count, "maskCoveragePercent": round(100 * count / covered, 2),
                        "preservedOpaquePixels": covered - count,
                        "preservedOpaqueBlackPixels": sum(bool(covered) and index == 0 and not chosen
                                                          for covered, index, chosen in zip(entry["opacity"], entry["indices"], selected)),
                        "changedColorPixels": sum(a != b for a, b in zip(entry["indices"], proposed)),
                        "preservedTransparentPixels": 2048 - covered})
    contact.save(args.output / "ground-shadow-mattes.png")
    # Assemble exactly the exported floor diamonds at their original isometric
    # coordinates. Each SOL-painted floor is tinted, not mistaken for grass.
    for name, tile_min, tile_max in (("east", (68, 66), (75, 75)), ("west", (26, 48), (31, 55))):
        tiles = [(x, y, entry) for entry in pieces.values() for x, y in entry["sourceTiles"]
                 if tile_min[0] <= x <= tile_max[0] and tile_min[1] <= y <= tile_max[1]]
        positions = [(32 * (x - y), 16 * (x + y) - 31, entry) for x, y, entry in tiles]
        min_x = min(x for x, _, _ in positions)
        min_y = min(y for _, y, _ in positions)
        width = max(x for x, _, _ in positions) - min_x + 64
        height = max(y for _, y, _ in positions) - min_y + 32
        images = []
        for proposed in (False, True):
            canvas = Image.new("RGBA", (width, height), (50, 50, 50, 255))
            for x, y, entry in sorted(tiles, key=lambda tile: (tile[0] + tile[1], tile[0])):
                indices = expected_indices(entry, pieces, masks) if proposed else entry["indices"]
                image = rgba(indices, entry["opacity"], palette)
                if entry["sol"] != 0:
                    tint = Image.new("RGBA", (64, 32), (32, 70, 120, 120))
                    image = Image.alpha_composite(image, tint)
                    image.putalpha(Image.frombytes("L", (64, 32), bytes(255 if value else 0 for value in entry["opacity"])))
                canvas.alpha_composite(image, (32 * (x - y) - min_x, 16 * (x + y) - 31 - min_y))
            images.append(canvas.convert("RGB").resize((width * 2, height * 2), Image.Resampling.NEAREST))
        pair = Image.new("RGB", (width * 4, height * 2 + 30), (24, 24, 24))
        for column, image in enumerate(images):
            pair.paste(image, (column * width * 2, 30))
        ImageDraw.Draw(pair).text((4, 4), "Native floor layer (SOL painting blue); right: EXPECTED frozen-mask cleanup", fill="white")
        pair.save(args.output / f"cabin-{name}-floor-layer.png")
    report = {"archiveMode": source["archiveMode"], "source": str(args.captures.resolve()),
              "maskHeader": str(args.header.resolve()), "maskCount": len(masks),
              "method": "native pixel coordinates, original independent opacity, frozen rowbits; no image alignment or runtime color threshold",
              "expectedColumnIsRuntimeCapture": False, "results": results}
    (args.output / "ground-shadow-mattes.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(args.output.resolve())


if __name__ == "__main__":
    main()
