#!/usr/bin/env python3
"""Prepare a private, occurrence-restricted ground-material comparison pilot.

Only the explicit pure-piece allowlists below receive support masks. The tool
does not classify pixels or tiles, edit source artwork, change the map, or run
the game. Copies, masks, contact sheets, and provenance stay in the ignored
diagnostics directory. Requires numpy and Pillow.
"""

from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import shutil

import numpy as np
from PIL import Image, ImageDraw


REPO = Path(__file__).resolve().parents[1]
GRASS_PIECES = tuple(range(16))
DIRT_PIECES = (44, 45, 48, 49, 50, 51, 60, 68)
RAW_OVERRIDE_PIECES = (*GRASS_PIECES, 44, 45, 48, 49, 50, 51)
LUMA_WEIGHTS = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")


def relative(path: Path, root: Path) -> str:
    return Path(os.path.relpath(path.resolve(), root.resolve())).as_posix()


def copy_exact(source: Path, destination: Path) -> dict:
    destination.parent.mkdir(parents=True, exist_ok=True)
    source_hash = sha256(source)
    if destination.exists() and sha256(destination) != source_hash:
        raise ValueError(f"Refusing to overwrite a differing private source copy: {destination}")
    if not destination.exists():
        shutil.copy2(source, destination)
    if sha256(destination) != source_hash:
        raise AssertionError(f"Source copy verification failed: {source}")
    return {"source": str(source.resolve()), "sourceSha256": source_hash,
            "copy": str(destination.resolve()), "copySha256": source_hash,
            "bytes": source.stat().st_size, "transform": "none; byte-identical copy"}


def native_image(path: Path) -> Image.Image:
    image = Image.open(path).convert("RGBA")
    if image.size != (64, 32) or not set(image.getchannel("A").getdata()) <= {0, 255}:
        raise ValueError(f"Native input must be 64x32 RGBA with binary alpha: {path}")
    return image


def linear_luminance(rgb: np.ndarray) -> np.ndarray:
    encoded = rgb.astype(np.float64) / 255.0
    linear = np.where(encoded <= 0.04045, encoded / 12.92, ((encoded + 0.055) / 1.055) ** 2.4)
    return linear @ LUMA_WEIGHTS


def calibration(family: str, pieces: tuple[int, ...], images: dict[str, Image.Image],
                counts: Counter, master_path: Path) -> dict:
    # Occurrence weighting follows the actual masked area. If there is no
    # supported occurrence, an explicit equal-weight reference swatch sample
    # still defines the unused material's gain; it never creates map support.
    occurrence_count = sum(counts[f"p{piece}"] for piece in pieces)
    selected = [(f"p{piece}", counts[f"p{piece}"] if occurrence_count else 1)
                for piece in pieces if f"p{piece}" in images
                and (not occurrence_count or counts[f"p{piece}"])]
    if not selected:
        raise ValueError(f"No native calibration references for {family}")
    total, weight = 0.0, 0
    samples = []
    for key, occurrences in selected:
        rgba = np.array(images[key])
        alpha = rgba[..., 3] == 255
        values = linear_luminance(rgba[..., :3])[alpha]
        total += float(values.sum()) * occurrences
        weight += len(values) * occurrences
        samples.append({"key": key, "nativeOpaquePixels": int(alpha.sum()),
                        "weight": occurrences, "actualPilotOccurrences": counts[key],
                        "meanLinearLuminance": float(values.mean())})
    native_mean = total / weight
    master = np.array(Image.open(master_path).convert("RGBA"))
    if not np.all(master[..., 3] == 255):
        raise ValueError(f"Master must be opaque: {master_path}")
    master_mean = float(linear_luminance(master[..., :3]).mean())
    if master_mean <= 0:
        raise ValueError(f"Master has zero luminance: {master_path}")
    gain = native_mean / master_mean
    if not math.isfinite(gain) or not 0 <= gain <= 16:
        raise ValueError(f"Gain outside comparison contract for {family}: {gain}")
    return {"family": family, "linearGain": gain, "meanNativeLinearLuminance": native_mean,
            "meanMasterLinearLuminance": master_mean, "weightedNativePixels": weight,
            "actualPilotOccurrences": occurrence_count,
            "samplingScope": "actual supported occurrences, weighted by count" if occurrence_count
            else "explicit pure-piece reference swatches only; no occurrence and no map mask in pilot area",
            "samples": samples,
            "method": "sRGB decode; Rec.709 linear RGB luminance; scalar native mean / master mean",
            "status": "PREVIEW brightness-only fit; native pixels contain painted lighting; not engine lighting, albedo or artistic validation"}


def region_review(path: Path, tiles: list, images: dict[str, Image.Image], support: dict,
                  bounds: list[int]) -> None:
    positions = [(32 * (x - z), 16 * (x + z) - 31, x, z, key) for x, z, key in tiles]
    min_x, min_y = min(p[0] for p in positions), min(p[1] for p in positions)
    width = max(p[0] for p in positions) - min_x + 64
    height = max(p[1] for p in positions) - min_y + 32
    layers = [Image.new("RGBA", (width, height), (24, 24, 28, 255)) for _ in range(3)]
    for px, py, x, z, key in sorted(positions, key=lambda p: (p[2] + p[3], p[2])):
        image = images[key]
        alpha = image.getchannel("A")
        position = (px - min_x, py - min_y)
        layers[0].alpha_composite(image, position)
        coverage = Image.new("RGBA", image.size, (220, 220, 224, 255))
        coverage.putalpha(alpha)
        layers[1].alpha_composite(coverage, position)
        family = support.get((x, z))
        color = (79, 180, 93, 255) if family == "grass" else (196, 129, 68, 255) if family == "dirt" else (58, 58, 64, 255)
        selected = Image.new("RGBA", image.size, color)
        selected.putalpha(alpha)
        layers[2].alpha_composite(selected, position)
    scale, margin, header = 2, 16, 58
    out = Image.new("RGB", (width * scale * 3 + margin * 4, height * scale + header + margin), (24, 24, 28))
    draw = ImageDraw.Draw(out)
    labels = ["NATIVE RGB / private reference", "NATIVE ALPHA / white = covered",
              "SUPPORT / green grass, orange dirt; gray HOLD"]
    for index, layer in enumerate(layers):
        x = margin + index * (width * scale + margin)
        draw.text((x, 8), labels[index], fill="white")
        draw.text((x, 26), f"X {bounds[0]}..{bounds[1]}, Z {bounds[2]}..{bounds[3]} / 2x nearest", fill=(180, 180, 190))
        out.paste(layer.convert("RGB").resize((width * scale, height * scale), Image.Resampling.NEAREST), (x, header))
    out.save(path)


def mask_contact(path: Path, masks: list[dict], images: dict[str, Image.Image]) -> None:
    columns, cell_width, cell_height = 4, 282, 160
    rows = max(1, math.ceil(len(masks) / columns))
    out = Image.new("RGB", (columns * cell_width, 42 + rows * cell_height), (24, 24, 28))
    draw = ImageDraw.Draw(out)
    draw.text((8, 8), "ACTUAL OCCURRENCE MASKS / native RGB left; exact alpha support right / 2x nearest", fill="white")
    if not masks:
        draw.text((8, 50), "No allowlisted pure pieces occur in this area. Nothing is replaced.", fill="white")
    for index, entry in enumerate(masks):
        x, y = index % columns * cell_width + 8, 42 + index // columns * cell_height
        image = images[entry["key"]]
        draw.text((x, y), f"{entry['key']} / {entry['material']} / {len(entry['tiles'])} occurrence(s)", fill="white")
        background = Image.new("RGBA", image.size, (48, 48, 54, 255))
        background.alpha_composite(image)
        out.paste(background.convert("RGB").resize((128, 64), Image.Resampling.NEAREST), (x, y + 24))
        out.paste(image.getchannel("A").convert("RGB").resize((128, 64), Image.Resampling.NEAREST), (x + 136, y + 24))
        draw.text((x, y + 94), "; ".join(f"{tx},{tz}" for tx, tz in entry["tiles"]), fill=(185, 185, 195))
        draw.text((x, y + 116), "Full native alpha; blend width 0", fill=(185, 185, 195))
    out.save(path)


def main() -> None:
    default_root = REPO / "diagnostics/ground-materials-hd/20261008-r1"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=REPO / "editor/godot/local/ground-reference.json")
    parser.add_argument("--raw", type=Path, default=REPO.parent / "diagnostics/v4-lighting-release-gog/ground-shadow-sources")
    parser.add_argument("--masters", type=Path, default=default_root / "masters")
    parser.add_argument("--output", type=Path, default=default_root / "pilot-input")
    parser.add_argument("--bounds", type=int, nargs=4, metavar=("MIN_X", "MAX_X", "MIN_Z", "MAX_Z"), default=[55, 66, 67, 77],
                        help="Explicit inclusive audited area; never expanded automatically")
    parser.add_argument("--grass-bounds", type=int, nargs=4, metavar=("MIN_X", "MAX_X", "MIN_Z", "MAX_Z"), default=[40, 47, 72, 79],
                        help="Second explicit audited area for grass continuity; never expanded automatically")
    args = parser.parse_args()
    for bounds in (args.bounds, args.grass_bounds):
        if bounds[0] > bounds[1] or bounds[2] > bounds[3]:
            raise ValueError("Bounds must be inclusive ordered ranges")
    source = json.loads(args.reference.read_text(encoding="utf-8-sig"))
    keys = [entry["key"] for entry in source["textures"]]
    if len(keys) != len(set(keys)):
        raise ValueError("Duplicate native texture keys")
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output.resolve()
    records, images, reference_entries, overrides = [], {}, [], []
    for entry in source["textures"]:
        key = entry["key"]
        if key != "fallback" and (not key.startswith("p") or not key[1:].isdigit()):
            raise ValueError(f"Unexpected native key: {key}")
        source_path = (args.reference.parent / entry["path"]).resolve()
        destination = output / "native" / (key + ".png")
        record = copy_exact(source_path, destination)
        record.update(key=key, role="original editor RGBA export")
        records.append(record)
        images[key] = native_image(destination)
        reference_entries.append({"key": key, "path": relative(destination, output), "width": 64, "height": 32,
                                  "sha256": sha256(destination)})
    for piece in RAW_OVERRIDE_PIECES:
        key = f"p{piece}"
        raw = args.raw / f"piece-{piece}-floor.png"
        if key not in images or not raw.is_file():
            continue
        destination = output / "native-raw" / (key + ".png")
        record = copy_exact(raw, destination)
        record.update(key=key, role="raw original MIN/CEL native floor before cleanup")
        records.append(record)
        replacement = native_image(destination)
        if replacement.getchannel("A").tobytes() != images[key].getchannel("A").tobytes():
            raise ValueError(f"Raw/native alpha differs for {key}; audit before using override")
        images[key] = replacement
        overrides.append({"key": key, "path": relative(destination, output), "sha256": sha256(destination)})
    tiles = source["tiles"]
    regions = {name: [tile for tile in tiles if bounds[0] <= tile[0] <= bounds[1]
                     and bounds[2] <= tile[1] <= bounds[3]]
               for name, bounds in (("path", args.bounds), ("grass", args.grass_bounds))}
    if any(not area for area in regions.values()):
        raise ValueError("No native map occurrences in an explicit region")
    area = [tile for tile in tiles if any(bounds[0] <= tile[0] <= bounds[1]
            and bounds[2] <= tile[1] <= bounds[3] for bounds in (args.bounds, args.grass_bounds))]
    if len({(x, z) for x, z, _ in tiles}) != len(tiles):
        raise ValueError("Duplicate native tile coordinate")
    if any(key not in images for _, _, key in tiles):
        raise ValueError("Unknown texture key in native tile layout")
    counts = Counter(tile[2] for tile in area)
    families = {"grass": GRASS_PIECES, "dirt": DIRT_PIECES}
    masks, support, calibrations, materials = [], {}, {}, {}
    for family, pieces in families.items():
        master = args.masters / f"{family}-r1.png"
        fit = calibration(family, pieces, images, counts, master)
        calibrations[family] = fit
        materials[family] = {"path": relative(master, output), "sha256": sha256(master), "worldPeriod": [2, 2],
                             "origin": [0, 0], "uvMode": "world", "linearGain": fit["linearGain"]}
        for piece in pieces:
            key = f"p{piece}"
            occurrences = [[x, z] for x, z, observed in area if observed == key]
            if not occurrences:
                continue
            mask_path = output / "masks" / f"{key}-{family}.png"
            mask_path.parent.mkdir(parents=True, exist_ok=True)
            mask = images[key].getchannel("A")
            if not mask.getbbox():
                raise ValueError(f"Empty native alpha for {key}")
            mask.save(mask_path)
            masks.append({"key": key, "material": family, "path": relative(mask_path, output),
                          "sha256": sha256(mask_path), "tiles": occurrences, "blendWidthPixels": 0})
            for x, z in occurrences:
                support[(x, z)] = family
    reference = {"source": source.get("source", ""), "exportLocked": True, "textures": reference_entries,
                 "tiles": tiles, "sourceManifestSha256": sha256(args.reference),
                 "privateCopy": True, "nativeLayoutChanged": False}
    write_json(output / "reference.json", reference)
    config = {"schemaVersion": 1, "excludeFallback": True, "materials": materials, "pieceMasks": masks,
              "nativeTextureOverrides": overrides, "comparisonUvModes": ["world", "legacy-per-tile"],
              "comparisonSamplers": ["nearest", "trilinear", "anisotropic"],
              "views": [
                  {"name": "iso", "mode": "orthographic", "target": [60, 0, 72], "yawDegrees": 45,
                   "pitchDegrees": math.degrees(math.atan(1 / math.sqrt(2))), "verticalSpan": 9},
                  {"name": "close", "mode": "perspective", "position": [59.5, 0.75, 75.2],
                   "target": [59.5, 0, 73.5], "fovYDegrees": 70},
                  {"name": "close-detail", "mode": "perspective", "position": [62.5, 0.6, 71.8],
                   "target": [62.0, 0, 70.0], "fovYDegrees": 65},
                  {"name": "grass-iso", "mode": "orthographic", "target": [44, 0, 76], "yawDegrees": 45,
                   "pitchDegrees": math.degrees(math.atan(1 / math.sqrt(2))), "verticalSpan": 7},
                  {"name": "grass-close", "mode": "perspective", "position": [44, 1.1, 79.8],
                   "target": [44, 0, 74.2], "fovYDegrees": 65}],
              "pilotRegionsInclusive": {"path": args.bounds, "grass": args.grass_bounds},
              "scope": "Only allowlisted pure-piece occurrences inside explicit bounds. Transitions, unknowns and fallback remain native."}
    write_json(output / "config.json", config)
    region_review(output / "region-selection.png", regions["path"], images, support, args.bounds)
    region_review(output / "grass-region-selection.png", regions["grass"], images, support, args.grass_bounds)
    mask_contact(output / "mask-contact.png", masks, images)
    outputs = [{"path": relative(path, output), "sha256": sha256(path)} for path in
               [output / "reference.json", output / "config.json", output / "region-selection.png",
                output / "grass-region-selection.png", output / "mask-contact.png"]]
    raw_metadata = args.raw / "ground-pieces.json"
    manifest = {"schemaVersion": 1, "private": True, "createdUtc": datetime.now(timezone.utc).isoformat(),
                "tool": {"path": str(Path(__file__).resolve()), "sha256": sha256(Path(__file__))},
                "reference": {"path": str(args.reference.resolve()), "sha256": sha256(args.reference)},
                "rawReferenceMetadata": {"path": str(raw_metadata.resolve()), "sha256": sha256(raw_metadata)}
                if raw_metadata.is_file() else None,
                "regionsInclusive": {"path": args.bounds, "grass": args.grass_bounds}, "nativeTilesInArea": len(area),
                "regionCounts": {name: {"nativeTiles": len(region),
                                       "selectedOccurrences": Counter(support[(x, z)] for x, z, _ in region if (x, z) in support),
                                       "heldNativeOccurrences": sum((x, z) not in support for x, z, _ in region)}
                                 for name, region in regions.items()},
                "allowlistedPieces": {key: list(value) for key, value in families.items()},
                "selectedOccurrences": {family: sum(value == family for value in support.values()) for family in families},
                "heldNativeOccurrences": len(area) - len(support),
                "nativeOccurrenceCounts": dict(sorted(counts.items())),
                "sourceCopies": records, "calibration": calibrations,
                "masks": masks, "materials": materials, "outputs": outputs,
                "limits": ["Pure-piece pilot only; semantic allowlist comes from local visual inspection, never SOL or color classification.",
                           "Every support mask lists exact occurrences; no global material replacement.",
                           "Transitions, unknown pieces, water and fallback receive no support and retain native pixels.",
                           "Editor fallback beneath blocked/scenery cells remains generic grass; it is not recovered native terrain.",
                           "Zero-width blending applies full native alpha; preserves native coverage and avoids a feather around every diamond.",
                           "Brightness fitting is PREVIEW-only. Native painted lighting is not clean albedo or engine-light validation.",
                           "Only grass and dirt masters are used; other generated families remain separate swatch review.",
                           "Offline comparison establishes no engine integration, collision, picking, performance, PBR or artistic approval.",
                           "All source pixels, copies and previews are private game-derived references; do not publish."]}
    write_json(output / "pilot-manifest.json", manifest)
    print(json.dumps({"output": str(output), "selectedOccurrences": manifest["selectedOccurrences"],
                      "heldNativeOccurrences": manifest["heldNativeOccurrences"],
                      "linearGain": {family: fit["linearGain"] for family, fit in calibrations.items()}}))
    print("Compare with: python tools/ground_material_compare.py --reference "
          f'"{output / "reference.json"}" --config "{output / "config.json"}" '
          f'--output "{output.parent / "pilot-comparison"}" --samplers nearest,trilinear,anisotropic')


if __name__ == "__main__":
    main()
