#!/usr/bin/env python3
"""Inspect private ground base-color masters without modifying their pixels.

Inputs are PNG paths, directories, or quoted glob patterns. Outputs include a
JSON report, an exact 3x3 repeat, an unscaled seam-intersection crop, a contact
sheet, and separately derived linear-light mip chains. An optional runtime
size produces a declared derivative, never a replacement for the source.

Requires Pillow and NumPy. This reports evidence and seam warnings; it cannot
approve art, establish material semantics, or validate engine integration.
Generated and original-game-derived media must remain private.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import glob
import hashlib
import json
from pathlib import Path
import shutil
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont


LIMITATIONS = [
    "No numerical artistic approval: edge metrics are review warnings only.",
    "An edge match does not prove seamless repetition, coherent scale, or absence of baked lighting.",
    "Input RGB values are assumed to be sRGB; embedded ICC profiles are recorded but not transformed.",
    "Semantic material boundaries, native coverage, water, collision, and map layout are not inferred or validated.",
    "Mip PNGs are derived/export candidates; current engine upload and filtering support are not validated here.",
    "Alpha mips use linear premultiplied-color area averaging, not coverage-preserving cutout mip generation.",
    "No normals, height, roughness, metallic, or other PBR maps are generated.",
    "Contacts are resized display aids; exact repeats and seam crops are saved separately.",
    "All generated/private reference media require separate rights and provenance review before redistribution.",
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def srgb_to_linear(encoded: np.ndarray) -> np.ndarray:
    value = np.asarray(encoded, dtype=np.float32)
    return np.where(value <= 0.04045, value / 12.92,
                    ((value + 0.055) / 1.055) ** 2.4).astype(np.float32)


def linear_to_srgb(linear: np.ndarray) -> np.ndarray:
    value = np.clip(linear, 0, 1)
    return np.where(value <= 0.0031308, value * 12.92,
                    1.055 * value ** (1 / 2.4) - 0.055)


def linear_rgba(image: Image.Image) -> np.ndarray:
    encoded = np.asarray(image.convert("RGBA"), dtype=np.float32) / 255
    encoded[:, :, :3] = srgb_to_linear(encoded[:, :, :3])
    return encoded


def encode_linear(value: np.ndarray, has_alpha: bool) -> Image.Image:
    encoded = np.concatenate((linear_to_srgb(value[:, :, :3]),
                              np.clip(value[:, :, 3:4], 0, 1)), axis=2)
    result = Image.fromarray(np.rint(encoded * 255).astype(np.uint8))
    return result if has_alpha else result.convert("RGB")


def resize_linear(value: np.ndarray, size: tuple[int, int]) -> np.ndarray:
    """Area-resize linear RGB premultiplied by alpha, then unpremultiply."""
    premultiplied = value.copy()
    premultiplied[:, :, :3] *= premultiplied[:, :, 3:4]
    channels = [np.asarray(Image.fromarray(premultiplied[:, :, channel])
                           .resize(size, Image.Resampling.BOX), dtype=np.float32)
                for channel in range(4)]
    result = np.stack(channels, axis=2)
    np.divide(result[:, :, :3], result[:, :, 3:4], out=result[:, :, :3],
              where=result[:, :, 3:4] > 1e-8)
    result[:, :, :3] *= result[:, :, 3:4] > 1e-8
    return np.clip(result, 0, 1)


def distribution(values: np.ndarray) -> dict:
    flattened = np.asarray(values, dtype=np.float32).reshape(-1)
    if flattened.size == 0:
        return {"count": 0, "mean": None, "p95": None, "max": None}
    return {"count": int(flattened.size), "mean": float(flattened.mean()),
            "p95": float(np.percentile(flattened, 95)), "max": float(flattened.max())}


def ratio(numerator: float | None, denominator: float | None) -> float | None:
    return numerator / denominator if numerator is not None and denominator is not None and denominator > 1e-8 else None


def seam_metrics(rgba: np.ndarray) -> dict:
    rgb, alpha = rgba[:, :, :3].astype(np.float32), rgba[:, :, 3]
    records = {}
    for axis, name in ((1, "left_right"), (0, "top_bottom")):
        edge = np.abs(np.take(rgb, 0, axis=axis) - np.take(rgb, -1, axis=axis)).mean(axis=-1)
        edge_visible = (np.take(alpha, 0, axis=axis) > 0) & (np.take(alpha, -1, axis=axis) > 0)
        adjacent = np.abs(np.diff(rgb, axis=axis)).mean(axis=-1)
        before = np.take(alpha, range(alpha.shape[axis] - 1), axis=axis)
        after = np.take(alpha, range(1, alpha.shape[axis]), axis=axis)
        edge_stats = distribution(edge[edge_visible])
        adjacent_stats = distribution(adjacent[(before > 0) & (after > 0)])
        mean_ratio = ratio(edge_stats["mean"], adjacent_stats["mean"])
        p95_ratio = ratio(edge_stats["p95"], adjacent_stats["p95"])
        gradient_mean = adjacent_stats["mean"] or 0
        edge_mean = edge_stats["mean"] or 0
        warning = edge_mean > max(2.0, gradient_mean * 2.0)
        records[name] = {"opposite_edge": edge_stats, "within_image_adjacent_gradient": adjacent_stats,
                         "edge_to_adjacent_mean_ratio": mean_ratio, "edge_to_adjacent_p95_ratio": p95_ratio,
                         "seam_warning": warning,
                         "alpha_edge_absolute_difference": distribution(np.abs(np.take(alpha.astype(np.float32), 0, axis=axis)
                                                                               - np.take(alpha.astype(np.float32), -1, axis=axis)))}
    return {"units": "0..255 encoded sRGB, mean absolute RGB channel difference per compared pixel",
            "visible_pair_rule": "Both pixels must have alpha > 0 for RGB metrics; alpha is reported separately.",
            "warning_heuristic": "opposite-edge mean > max(2 byte levels, 2 * within-image adjacent mean); advisory only",
            "axes": records}


def basecolor_stats(rgba: np.ndarray, linear: np.ndarray) -> dict:
    visible = rgba[:, :, 3] > 0
    pixels = rgba[:, :, :3][visible].astype(np.float32)
    channels = {}
    for index, channel in enumerate(("red", "green", "blue")):
        values = pixels[:, index]
        channels[channel] = {**distribution(values),
                             "min": float(values.min()) if values.size else None,
                             "std": float(values.std()) if values.size else None,
                             "p05": float(np.percentile(values, 5)) if values.size else None,
                             "p50": float(np.percentile(values, 50)) if values.size else None}
    luminance = linear[:, :, :3] @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    return {"rgb_units": "0..255 encoded sRGB", "channels": channels,
            "linear_rec709_luminance": {"units": "0..1 linear light", **distribution(luminance[visible])},
            "alpha": {"min": int(rgba[:, :, 3].min()), "max": int(rgba[:, :, 3].max()),
                      "transparent_pixels": int((rgba[:, :, 3] == 0).sum()),
                      "partial_pixels": int(((rgba[:, :, 3] > 0) & (rgba[:, :, 3] < 255)).sum()),
                      "opaque_pixels": int((rgba[:, :, 3] == 255).sum())}}


def file_record(path: Path, root: Path, size: tuple[int, int]) -> dict:
    return {"path": path.relative_to(root).as_posix(), "sha256": sha256(path), "dimensions": list(size)}


def write_mips(linear: np.ndarray, has_alpha: bool, directory: Path, root: Path,
               source_copy: Path | None = None) -> dict:
    directory.mkdir(parents=True, exist_ok=True)
    levels = []
    value = linear
    level = 0
    while True:
        height, width = value.shape[:2]
        path = directory / f"mip-{level:02d}-{width}x{height}.png"
        if level == 0 and source_copy is not None:
            shutil.copy2(source_copy, path)
        else:
            encode_linear(value, has_alpha).save(path)
        levels.append(file_record(path, root, (width, height)))
        if width == 1 and height == 1:
            break
        value = resize_linear(value, (max(1, width // 2), max(1, height // 2)))
        level += 1
    return {"derivation": "sRGB IEC 61966-2-1 decode; premultiplied linear RGB + alpha area average (Pillow float BOX); unpremultiply; sRGB encode + round to 8-bit for export",
            "dimension_rule": "Each next axis = max(1, floor(previous_axis / 2)); odd dimensions are supported.",
            "working_precision": "float32 chain retained between levels; exported 8-bit PNG is not fed back into the next level",
            "level_zero": "Exact byte copy of full-resolution source" if source_copy else "Explicitly resampled runtime derivative",
            "levels": levels}


def font(size: int) -> ImageFont.ImageFont:
    try:
        return ImageFont.truetype("arial.ttf", size)
    except OSError:
        return ImageFont.load_default(size=size)


def displayed(image: Image.Image, bounds: tuple[int, int]) -> Image.Image:
    rgba = image.convert("RGBA")
    rgba.thumbnail(bounds, Image.Resampling.LANCZOS)
    background = Image.new("RGBA", rgba.size, (40, 40, 44, 255))
    background.alpha_composite(rgba)
    return background.convert("RGB")


def inspect(path: Path, output: Path, runtime_size: int | None, seam_width: int) -> tuple[dict, list[Image.Image]]:
    original_hash = sha256(path)
    with Image.open(path) as source:
        if source.format != "PNG" or getattr(source, "n_frames", 1) != 1:
            raise ValueError("Expected a single-frame PNG master")
        source.load()
        mode, size, info = source.mode, source.size, dict(source.info)
        image = source.convert("RGBA")
    width, height = size
    if min(size) < 2:
        raise ValueError("Master must have at least two pixels on each axis")
    if runtime_size is not None and runtime_size > max(size):
        raise ValueError("--runtime-size must not upscale the source")
    key = f"{path.stem}-{original_hash[:8]}"
    directory = output / key
    if path.is_relative_to(directory):
        raise ValueError("Source cannot be inside its derived output directory")
    directory.mkdir(parents=True, exist_ok=True)
    rgba = np.asarray(image, dtype=np.uint8)
    linear = linear_rgba(image)
    has_alpha = bool((rgba[:, :, 3] != 255).any())
    repeat = Image.new("RGBA", (width * 3, height * 3))
    for y in range(3):
        for x in range(3):
            repeat.paste(image, (x * width, y * height))
    repeat_path = directory / "repeat-3x3.png"
    repeat.save(repeat_path)
    half = min(seam_width // 2, width, height)
    crop_bounds = (width - half, height - half, width + half, height + half)
    seam = repeat.crop(crop_bounds)
    seam_path = directory / "seam-intersection.png"
    seam.save(seam_path)
    records = {"source": {"path": str(path), "sha256": original_hash, "dimensions": list(size),
                           "mode": mode, "decoded_stats_mode": "RGBA8", "format": "PNG",
                           "embedded_icc_sha256": hashlib.sha256(info["icc_profile"]).hexdigest() if info.get("icc_profile") else None,
                           "embedded_gamma": info.get("gamma"), "embedded_srgb_intent": info.get("srgb"),
                           "colorspace_assumption": "sRGB; no embedded-profile color transform performed",
                           "full_resolution_preserved": True},
               "basecolor": basecolor_stats(rgba, linear), "seams": seam_metrics(rgba),
               "previews": {"repeat_3x3": {**file_record(repeat_path, output, repeat.size), "derivation": "Exact RGBA copies, no source resize or edge repair"},
                            "seam_intersection": {**file_record(seam_path, output, seam.size), "repeat_crop_bounds": list(crop_bounds),
                                                  "derivation": "Exact unscaled crop around x=source_width, y=source_height repeat boundary intersection"}},
               "mips_source": write_mips(linear, has_alpha, directory / "mips-source", output, path),
               "runtime_derivative": None, "artistic_acceptance": False, "engine_integration_validated": False}
    if runtime_size is not None:
        scale = runtime_size / max(size)
        runtime_dimensions = (max(1, round(width * scale)), max(1, round(height * scale)))
        runtime = resize_linear(linear, runtime_dimensions)
        records["runtime_derivative"] = {"source_dimensions": list(size), "dimensions": list(runtime_dimensions),
                                          "resampling": "Explicit --runtime-size; linear-light premultiplied float BOX area resize, then sRGB encoding; source retained",
                                          "aspect_policy": "Uniform scale to requested longest axis; each output axis rounded to a whole pixel",
                                          "mips": write_mips(runtime, has_alpha, directory / f"runtime-{runtime_size}", output)}
    if sha256(path) != original_hash:
        raise RuntimeError("Source changed during validation; outputs are not a stable source audit")
    return records, [image, repeat, seam]


def expand_inputs(patterns: list[str]) -> list[Path]:
    result = set()
    for pattern in patterns:
        direct = Path(pattern)
        if direct.is_dir():
            matches = list(direct.glob("*.png"))
        elif direct.is_file():
            matches = [direct]
        else:
            matches = [Path(value) for value in glob.glob(pattern, recursive=True)]
        if not matches:
            raise ValueError(f"No input files matched: {pattern}")
        result.update(path.resolve() for path in matches if path.is_file())
    if not result:
        raise ValueError("No master files matched")
    return sorted(result)


def contact_sheet(rows: list[tuple[dict, list[Image.Image]]], path: Path) -> None:
    width, row_height = 1152, 444
    contact = Image.new("RGB", (width, 54 + row_height * len(rows)), (24, 24, 28))
    draw = ImageDraw.Draw(contact)
    label, small = font(19), font(15)
    draw.text((14, 10), "PRIVATE GROUND QA / source, 3x3 repeat, exact seam crop / no artistic approval", font=label, fill="white")
    for row, (record, images) in enumerate(rows):
        top = 54 + row * row_height
        source = record["source"]
        draw.text((14, top), f"{Path(source['path']).name}   {source['dimensions'][0]}x{source['dimensions'][1]}   {source['mode']}   SHA {source['sha256'][:12]}", font=label, fill="white")
        for column, (image, title) in enumerate(zip(images, ("Source (display resize)", "3x3 repeat (display resize)", "Seam intersection (exact crop)"))):
            left = 14 + column * 380
            draw.text((left, top + 32), title, font=small, fill=(200, 200, 208))
            preview = displayed(image, (360, 340))
            contact.paste(preview, (left + (360 - preview.width) // 2, top + 57 + (340 - preview.height) // 2))
        axes = record["seams"]["axes"]
        parts = []
        for axis, short in (("left_right", "L/R"), ("top_bottom", "T/B")):
            metric = axes[axis]
            mean = metric["opposite_edge"]["mean"]
            gradient = metric["within_image_adjacent_gradient"]["mean"]
            parts.append(f"{short}: edge {mean:.2f} / adjacent {gradient:.2f}" if mean is not None and gradient is not None else f"{short}: no visible pairs")
        draw.text((14, top + 407), "  |  ".join(parts) + "  (warning evidence only)", font=small, fill=(210, 210, 218))
    contact.save(path)


def positive_size(value: str) -> int:
    size = int(value)
    if size < 2:
        raise argparse.ArgumentTypeError("Size must be at least 2")
    return size


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("masters", nargs="+", help="PNG paths, directories, or quoted glob patterns")
    parser.add_argument("--output", type=Path, required=True, help="Private report/preview/derived-mip directory")
    parser.add_argument("--runtime-size", type=positive_size, help="Optional runtime derivative longest axis, e.g. 1024; keeps aspect and never upscales or replaces source")
    parser.add_argument("--seam-width", type=positive_size, default=256, help="Unscaled crop width/height at a repeat-boundary intersection (default: 256)")
    args = parser.parse_args()
    output = args.output.resolve()
    try:
        inputs = expand_inputs(args.masters)
    except ValueError as error:
        parser.error(str(error))
    output.mkdir(parents=True, exist_ok=True)
    rows, errors = [], []
    for path in inputs:
        try:
            rows.append(inspect(path, output, args.runtime_size, args.seam_width))
        except (OSError, ValueError, RuntimeError) as error:
            errors.append({"source": str(path), "error": str(error)})
    contact = output / "master-contact.png"
    contact_record = None
    if rows:
        contact_sheet(rows, contact)
        with Image.open(contact) as contact_image:
            contact_record = file_record(contact, output, contact_image.size)
    report = {"schema": "ground-material-private-qa-v1", "private": True,
              "created_utc": datetime.now(timezone.utc).isoformat(), "tool_sha256": sha256(Path(__file__).resolve()),
              "status": "diagnostics-generated-with-errors" if errors else "diagnostics-generated",
              "source_count": len(inputs), "inspected_count": len(rows), "runtime_size_requested": args.runtime_size,
              "artistic_acceptance": False, "engine_integration_validated": False, "limitations": LIMITATIONS,
              "contact": contact_record,
              "masters": [record for record, _ in rows], "errors": errors}
    report_path = output / "report.json"
    report_path.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"report": str(report_path), "inspected": len(rows), "errors": len(errors),
                      "artistic_acceptance": False, "runtime_size_requested": args.runtime_size}))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
