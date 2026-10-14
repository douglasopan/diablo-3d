#!/usr/bin/env python3
"""Mechanical, CPU-only extraction of reviewed own-art hero STILL sheets.

No generation, repainting, alpha removal, mirroring, animation or game install.
Source cells are transformed uniformly into a shared authoring canvas. A raster
support anchor is explicit and is not a measured anatomical/native CLX pivot.
Only the exact reviewed source hashes below are eligible for still extraction.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path

from PIL import Image, ImageDraw, __version__ as PILLOW_VERSION


WORKSPACE = Path(__file__).resolve().parents[5]
DIRECTIONS = ("s", "sw", "w", "nw", "n", "ne", "e", "se")
CANVAS = (512, 640)
FOOTPOINT = (256, 608)
MEASURE_ALPHA = 128  # Measurement only: source alpha bytes are never thresholded.
REVIEWED = {
    "rogue": {
        "file": "rogue-light-unarmed-directions-r1.png",
        "sha256": "4a3fdfb6664eb429b35f0e960349f1b6e58e7c8caf5ac7e1c120da0908d875c4",
        "revision": "r1",
        "orientationStatus": "nominal-eight-views-coherent-approximate-yaw",
        "notes": [
            "Camera is near chest level; not the requested approximately 35-degree elevated isometric camera.",
            "Yaw labels follow visual front/profile/back orientation; exact 45-degree increments are not measured.",
            "Costume/identity broadly consistent; individual views are independently generated, not projections of one mesh.",
        ],
    },
    "sorcerer": {
        "file": "sorcerer-light-unarmed-directions-r2.png",
        "sha256": "29817a7cbdc85cad159aedcd577cef14e8b4bc5ec0d109dc060db8fd8fd768e6",
        "revision": "r2",
        "orientationStatus": "nominal-eight-views-coherent-approximate-yaw",
        "notes": [
            "R2 fixes R1 row1col2 SW and row1col4 NW nominal direction defects.",
            "NE row2col2 is a shallow back-right turn; it does not demonstrate a 45-degree yaw.",
            "Camera is near chest level; not the requested approximately 35-degree elevated isometric camera.",
            "Costume/identity broadly consistent; individual views are independently generated, not projections of one mesh.",
        ],
    },
}
SOURCE_NOTES = {
    "rogue-light-unarmed-a-pose-r1.png": {
        "status": "raster-identity-and-a-pose-authoring-candidate",
        "notes": ["Full body, hands and boots visible; not a mesh, rig or clip."]},
    "sorcerer-light-unarmed-a-pose-r1.png": {
        "status": "raster-identity-and-a-pose-authoring-candidate",
        "notes": ["Full body, hands and boots visible; not a mesh, rig or clip."]},
    "sorcerer-light-unarmed-directions-r1.png": {
        "status": "rejected-direction-sheet-preserved-no-derivatives",
        "defects": [
            {"cell": [1, 2], "nominalDirection": "sw", "observed": "front-right/SE; wrong side"},
            {"cell": [1, 4], "nominalDirection": "nw", "observed": "near-back/N; NW ambiguous and near duplicated"},
        ],
        "notes": ["R1 preserved byte-for-byte; no relabeling or mirroring used to conceal missing directions."]},
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def png_bytes(im: Image.Image) -> bytes:
    buffer = io.BytesIO()
    im.save(buffer, format="PNG")
    return buffer.getvalue()


def save_png(path: Path, im: Image.Image, immutable: bool = False) -> dict:
    data = png_bytes(im)
    if immutable and path.exists() and path.read_bytes() != data:
        raise FileExistsError(f"Refusing to replace existing derivative with different bytes: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return {"path": str(path.resolve()), "sha256": sha(data), "bytes": len(data), "size": list(im.size), "mode": im.mode}


def measured_bbox(alpha: Image.Image, threshold: int) -> tuple | None:
    # Binary image is used for measurement, never substituted into output RGBA.
    return alpha.point([0 if value < threshold else 255 for value in range(256)]).getbbox()


def alpha_stats(im: Image.Image) -> dict:
    a = im.getchannel("A")
    h = a.histogram()
    return {
        "extrema": list(a.getextrema()), "alphaZeroPixels": h[0],
        "alpha1to15Pixels": sum(h[1:16]), "alpha16to127Pixels": sum(h[16:128]),
        "alpha128to253Pixels": sum(h[128:254]), "alpha254Pixels": h[254],
        "alpha255Pixels": h[255], "totalPixels": im.width * im.height,
        "nonzeroBounds": a.getbbox(), "coreBoundsAlpha128": measured_bbox(a, MEASURE_ALPHA),
    }


def checker(size: tuple[int, int], step: int = 24) -> Image.Image:
    im = Image.new("RGBA", size, (220, 220, 220, 255))
    draw = ImageDraw.Draw(im)
    for y in range(0, size[1], step):
        for x in range(0, size[0], step):
            if (x // step + y // step) % 2:
                draw.rectangle((x, y, x + step - 1, y + step - 1), fill=(160, 160, 160, 255))
    return im


def alpha_review(im: Image.Image) -> Image.Image:
    ratio = min(768 / im.width, 768 / im.height)
    im = im.resize((round(im.width * ratio), round(im.height * ratio)), Image.Resampling.LANCZOS)
    w, h = im.size
    sheet = Image.new("RGB", (2 * w, h + 28), (230, 230, 230))
    gray = Image.new("RGBA", (w, h), (224, 224, 224, 255))
    sheet.paste(Image.alpha_composite(gray, im).convert("RGB"), (0, 28))
    sheet.paste(Image.alpha_composite(checker((w, h)), im).convert("RGB"), (w, 28))
    draw = ImageDraw.Draw(sheet)
    draw.text((8, 8), "RGBA composite: light gray", fill=(20, 20, 20))
    draw.text((w + 8, 8), "RGBA composite: checker", fill=(20, 20, 20))
    return sheet


def inspect_cells(sheet: Image.Image) -> list[dict]:
    if sheet.size != (1536, 1024):
        raise ValueError(f"Reviewed 4x2 sheets require 1536x1024, received {sheet.size}")
    cw, ch = sheet.width // 4, sheet.height // 2
    entries = []
    for index, direction in enumerate(DIRECTIONS):
        row, col = divmod(index, 4)
        crop = (col * cw, row * ch, (col + 1) * cw, (row + 1) * ch)
        cell = sheet.crop(crop)
        alpha = cell.getchannel("A")
        core = measured_bbox(alpha, MEASURE_ALPHA)
        nonzero = alpha.getbbox()
        if core is None or nonzero is None:
            raise ValueError(f"No silhouette in {direction}")
        if core[0] == 0 or core[1] == 0 or core[2] == cw or core[3] == ch:
            raise ValueError(f"Core silhouette touches source cell boundary: {direction}")
        # All boots in the lower 14% of body height define a reproducible support
        # envelope. This approximates a raster pivot; anatomical/native pivots
        # need a separate game-camera calibration and are intentionally unclaimed.
        support_top = core[3] - max(1, round((core[3] - core[1]) * 0.14))
        band = measured_bbox(alpha.crop((0, support_top, cw, core[3])), MEASURE_ALPHA)
        if band is None:
            raise ValueError(f"No foot support pixels in {direction}")
        foot = ((band[0] + band[2]) / 2, float(core[3]))
        entries.append({
            "direction": direction, "cellRowColOneBased": [row + 1, col + 1],
            "sourceCropXYXY": list(crop), "sourceCellSize": [cw, ch],
            "sourceCoreBoundsAlpha128Local": list(core), "sourceNonzeroBoundsLocal": list(nonzero),
            "sourceSupportBandLocal": [band[0], support_top + band[1], band[2], support_top + band[3]],
            "sourceFootpointLocal": list(foot),
            "sourceFootpointSheet": [crop[0] + foot[0], crop[1] + foot[1]],
            "sourceAlpha": alpha_stats(cell),
        })
    return entries


def shared_scale(cells: list[dict]) -> float:
    # Fit ALL nonzero alpha bounds of all eight cells, including faint fringe,
    # with common scale and without deforming per-axis or normalizing each view.
    # Leave enough space for bicubic filter support around the entire silhouette.
    left, right, top, bottom = 16, CANVAS[0] - 16, 24, CANVAS[1] - 8
    limits = []
    for cell in cells:
        x0, y0, x1, y1 = cell["sourceNonzeroBoundsLocal"]
        fx, fy = cell["sourceFootpointLocal"]
        for space, span in ((FOOTPOINT[0] - left, fx - x0), (right - FOOTPOINT[0], x1 - fx),
                            (FOOTPOINT[1] - top, fy - y0), (bottom - FOOTPOINT[1], y1 - fy)):
            if span > 0:
                limits.append(space / span)
    result = min(limits)
    if result <= 0:
        raise ValueError("Unable to fit all views with one positive scale")
    return result


def still_review(images: list[Image.Image]) -> Image.Image:
    # Half-size preview pairs, direction labels and the exact authored footpoint.
    tilew, tileh, labelh = 256, 320, 22
    result = Image.new("RGB", (8 * tilew, 2 * (tileh + labelh)), (230, 230, 230))
    draw = ImageDraw.Draw(result)
    for index, (direction, image) in enumerate(zip(DIRECTIONS, images)):
        row, col = divmod(index, 4)
        small = image.resize((tilew, tileh), Image.Resampling.LANCZOS)
        for panel in range(2):
            x, y = col * tilew + panel * 4 * tilew, row * (tileh + labelh)
            draw.text((x + 8, y + 5), direction.upper() + " - STILL / 0 animation frames", fill=(20, 20, 20))
            bg = checker((tilew, tileh), 16) if panel else Image.new("RGBA", (tilew, tileh), (224, 224, 224, 255))
            composed = Image.alpha_composite(bg, small).convert("RGB")
            cross = ImageDraw.Draw(composed)
            px, py = FOOTPOINT[0] // 2, FOOTPOINT[1] // 2
            cross.line((px - 4, py, px + 4, py), fill=(200, 30, 30), width=1)
            cross.line((px, py - 4, px, py + 4), fill=(200, 30, 30), width=1)
            result.paste(composed, (x, y + labelh))
    return result


def extract(hero: str, sheet_path: Path, output_root: Path, review_root: Path) -> dict:
    cfg = REVIEWED[hero]
    source_bytes = sheet_path.read_bytes()
    if sha(source_bytes) != cfg["sha256"]:
        raise ValueError(f"Unreviewed source bytes for {hero}; audit replacement before changing reviewed hash")
    with Image.open(io.BytesIO(source_bytes)) as source:
        if source.mode != "RGBA":
            raise ValueError("Reviewed source must have real RGBA")
        sheet = source.copy()
    cells = inspect_cells(sheet)
    scale = shared_scale(cells)
    outputs, images = [], []
    for cell in cells:
        fx, fy = cell["sourceFootpointLocal"]
        tx, ty = FOOTPOINT[0] - scale * fx, FOOTPOINT[1] - scale * fy
        source = sheet.crop(tuple(cell["sourceCropXYXY"]))
        # PIL's RGBA resampler uses premultiplied alpha internally. Only uniform
        # scale and translation, with zero fill; no source pixels are repainted.
        image = source.transform(CANVAS, Image.Transform.AFFINE,
                                 (1 / scale, 0, -tx / scale, 0, 1 / scale, -ty / scale),
                                 resample=Image.Resampling.BICUBIC, fillcolor=(0, 0, 0, 0))
        bounds = image.getchannel("A").getbbox()
        if bounds is None or bounds[0] <= 0 or bounds[1] <= 0 or bounds[2] >= CANVAS[0] or bounds[3] >= CANVAS[1]:
            raise ValueError(f"Visible alpha clipping after transform: {hero}/{cell['direction']}")
        path = output_root / hero / ("authoring-directions-" + cfg["revision"]) / cell["direction"] / "still.png"
        record = save_png(path, image, immutable=True)
        record.update(cell)
        record.update({
            "kind": "authoring-direction-still", "frameCount": 1, "animationFrames": 0,
            "state": "natural-standing-authoring-pose-not-native-stand-clip", "loop": False,
            "sourcePath": str(sheet_path.resolve()), "sourceSha256": cfg["sha256"],
            "footpointTopLeftPixels": list(FOOTPOINT), "sharedUniformScale": scale,
            "forwardAffineCellToCanvas": [scale, 0, tx, 0, scale, ty],
            "outputAlpha": alpha_stats(image), "status": "candidate-authoring-only-no-runtime-promotion",
        })
        outputs.append(record)
        images.append(image)
    review = save_png(review_root / (hero + "-authoring-directions-" + cfg["revision"] + "-still-review.png"), still_review(images))
    return {
        "hero": hero, "sourceRevision": cfg["revision"], "sourceSha256": cfg["sha256"],
        "orientationStatus": cfg["orientationStatus"], "notes": cfg["notes"],
        "status": "eight-authoring-stills-produced-not-gameplay-approved",
        "canvas": list(CANVAS), "footpointTopLeftPixels": list(FOOTPOINT),
        "footpointAlgorithm": "center X of alpha>=128 support envelope in lower 14% of core body height, Y at core bottom edge; an authored raster approximation",
        "nativeClxPivotValidated": False, "gameCameraValidated": False, "exact45DegreeYawValidated": False,
        "sharedUniformScale": scale, "frameCountPerDirection": 1, "animationFrames": 0,
        "resampler": "Pillow RGBA AFFINE/BICUBIC", "alphaThresholdAppliedToOutput": False,
        "outputs": outputs, "review": review,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, default=WORKSPACE)
    parser.add_argument("--emit", nargs="*", choices=sorted(REVIEWED), default=[])
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    asset_root = workspace / "assets/own-art/heroes/local-r1"
    master_root, review_root = asset_root / "masters", asset_root / "review"
    receipt_path = workspace / "devilutionx/docs/own-art-pack/heroes-local-r1/raster-qa.json"
    masters = sorted(master_root.glob("*.png"))
    before = {str(path.resolve()): sha(path.read_bytes()) for path in masters}
    audits = []
    for path in masters:
        with Image.open(path) as source:
            if source.mode != "RGBA":
                raise ValueError(f"Source has no genuine RGBA: {path}")
            im = source.copy()
        audit = {"path": str(path.resolve()), "sha256": before[str(path.resolve())],
                 "bytes": path.stat().st_size, "size": list(im.size), "mode": im.mode,
                 "alpha": alpha_stats(im), "animationFrames": 0,
                 "alphaReview": save_png(review_root / (path.stem + "-alpha-review.png"), alpha_review(im))}
        audit.update(SOURCE_NOTES.get(path.name, {}))
        for hero, cfg in REVIEWED.items():
            if path.name == cfg["file"]:
                audit.update({"status": "reviewed-for-mechanical-authoring-still-extraction-only",
                              "orientationStatus": cfg["orientationStatus"], "notes": cfg["notes"],
                              "reviewedHashMatches": audit["sha256"] == cfg["sha256"]})
        if "status" not in audit:
            audit["status"] = "unreviewed-source-no-extraction"
        audits.append(audit)
    classes = [extract(hero, master_root / REVIEWED[hero]["file"], asset_root, review_root) for hero in args.emit]
    after = {str(path.resolve()): sha(path.read_bytes()) for path in masters}
    if before != after:
        raise RuntimeError("Master preservation check failed")
    receipt = {
        "schema": "d3d.hero-raster-qa/1", "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
        "sourceThread": "01a1153a-cad1-7fb1-8326-225511ee918d",
        "script": {"path": str(Path(__file__).resolve()), "sha256": sha(Path(__file__).read_bytes())},
        "tool": {"name": "Pillow", "version": PILLOW_VERSION, "execution": "local CPU; no GPU/network/generation"},
        "scope": "Rogue/Sorcerer own raster masters, mechanical STILL extraction and QA only",
        "sources": audits, "classes": classes,
        "visualQa": {
            "method": "Manual inspection of actual RGBA composited on light gray and checker; RGB-only viewer display is not alpha evidence",
            "transparency": "Genuine transparent background; composites show isolated silhouettes, not the brown RGB stored underneath alpha=0",
            "alphaLimitation": "Every master has alpha range 0..254, with no fully opaque 255 pixel; preserved, not silently normalized",
            "legibility": "Faces, hair, hands, boots and clothing remain legible in master/source and half-size still review",
            "fringe": "Low-alpha fringe exists (counts recorded); no obvious broad brown backdrop on the tested composite backgrounds",
            "acceptance": "Technical/manual authoring review only; human artistic acceptance and gameplay calibration not claimed",
        },
        "checks": {
            "mastersPreservedBySha256": before == after, "masterCount": len(masters),
            "outputCanvas512x640": all(o["size"] == list(CANVAS) for c in classes for o in c["outputs"]),
            "eightStillsPerEmittedClass": all(len(c["outputs"]) == 8 for c in classes),
            "uniformScalePerClass": all(len({o["sharedUniformScale"] for o in c["outputs"]}) == 1 for c in classes),
            "footpoint256608PerStill": all(o["footpointTopLeftPixels"] == list(FOOTPOINT) for c in classes for o in c["outputs"]),
            "noAnimationFrames": all(c["animationFrames"] == 0 for c in classes),
            "rejectedSorcererR1NotExtracted": all(c["sourceRevision"] != "r1" for c in classes if c["hero"] == "sorcerer"),
        },
        "totalAuthoringStills": sum(len(c["outputs"]) for c in classes), "animationFrames": 0,
        "gameplayPolicy": "Sprite-only until each actor has correct validated rig and animation; these static authoring candidates are not installed or mapped to native animation slots",
        "integration": {"registryChanged": False, "sourceChanged": False, "buildRun": False, "installationChanged": False,
                        "runtimePromoted": False, "npcOgdenChanged": False},
        "remaining": [
            "Measured/elevated game-camera views or renders; exact yaw calibration",
            "Rig/mesh and actual native-state animations, weapon/armor variants, frame counts and event alignment",
            "Calibrated native logical dimensions/pivot and runtime integration owned by principal",
        ],
    }
    write_json(receipt_path, receipt)
    print(json.dumps({"receipt": str(receipt_path), "mastersPreserved": before == after,
                      "authoringStills": receipt["totalAuthoringStills"], "animationFrames": 0,
                      "classes": [{"hero": c["hero"], "revision": c["sourceRevision"], "scale": c["sharedUniformScale"]} for c in classes]}, indent=2))


if __name__ == "__main__":
    main()
