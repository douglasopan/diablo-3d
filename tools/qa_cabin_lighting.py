#!/usr/bin/env python3
"""Measure cabin material patches in actual native/3D captures, without alignment.

Requires NumPy and Pillow. RGB distributions are material/lighting evidence, not
pixel fidelity scores: the meshes, UVs, and native painted shadows differ. The
optional D3DMESH1 pass audits actual imported albedo, normals, and quantization;
it is an isolated mathematical projection, never an in-game after screenshot.
Generated reports and images belong in a local diagnostics directory.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

import numpy as np
from PIL import Image, ImageDraw


ROIS = {
    "roof_main": {"face": "+X roof", "polygon": [[280, 235], [395, 177], [414, 262], [322, 311]]},
    "stone_gable_lower": {"face": "+Z window gable", "polygon": [[210, 332], [250, 352], [250, 365], [210, 344]]},
    "stone_side_lower": {"face": "+X door side", "polygon": [[322, 381], [360, 362], [361, 378], [322, 397]]},
    "stone_side_door_left": {"face": "+X local eave/door shadow", "polygon": [[390, 345], [409, 335], [409, 351], [390, 361]]},
}
DETAIL_CROP = [130, 120, 530, 440]
WINDOW_CROP = [206, 270, 310, 365]
LUMA = np.array([0.2126, 0.7152, 0.0722])


def vector(value: str) -> np.ndarray:
    result = np.array([float(part) for part in value.split(",")])
    if result.shape != (3,) or not np.isfinite(result).all():
        raise argparse.ArgumentTypeError("Expected three finite comma-separated numbers")
    return result


def linear(rgb: np.ndarray) -> np.ndarray:
    encoded = rgb.astype(float) / 255
    return np.where(encoded <= 0.04045, encoded / 12.92, ((encoded + 0.055) / 1.055) ** 2.4)


def encoded(values: np.ndarray) -> np.ndarray:
    values = np.maximum(values, 0)
    rgb = np.where(values <= 0.0031308, 12.92 * values, 1.055 * values ** (1 / 2.4) - 0.055)
    return np.clip(np.rint(rgb * 255), 0, 255).astype(np.uint8)


def statistics(rgb: np.ndarray) -> dict:
    luminance = rgb @ LUMA
    return {
        "pixelCount": len(rgb),
        "meanRGB": rgb.mean(axis=0).round(4).tolist(),
        "medianRGB": np.median(rgb, axis=0).tolist(),
        "linearMeanRGB": linear(rgb).mean(axis=0).tolist(),
        "encodedLuminanceP10P50P90": np.quantile(luminance, [0.1, 0.5, 0.9]).round(4).tolist(),
        "encodedLuminanceStd": float(luminance.std()),
        "paintedBlackPixels": int(np.count_nonzero(np.all(rgb == 0, axis=1))),
    }


def roi_mask(size: tuple[int, int], polygon: list) -> np.ndarray:
    if len(polygon) < 3 or any(len(point) != 2 for point in polygon):
        raise ValueError("ROI must be a polygon with at least three integer points")
    if any(not 0 <= x < size[0] or not 0 <= y < size[1] for x, y in polygon):
        raise ValueError("ROI falls outside the actual capture")
    image = Image.new("L", size)
    ImageDraw.Draw(image).polygon([tuple(point) for point in polygon], fill=255)
    result = np.asarray(image) != 0
    if not result.any():
        raise ValueError("Empty material ROI")
    return result


def load_frame(directory: Path) -> tuple[Image.Image, dict]:
    manifest = json.loads((directory / "turntables" / "turntables.json").read_text(encoding="utf-8-sig"))
    if "forced geometry" not in manifest.get("raw", ""):
        raise ValueError("Actual 3D capture lacks forced-geometry provenance")
    frame = next(item for item in manifest["frames"] if item["object"] == "cabin-east" and item["orbitDegrees"] == 0)
    image = Image.open(directory / "turntables" / frame["file"]).convert("RGB")
    if image.size != (manifest["width"], manifest["height"]):
        raise ValueError("Actual capture dimensions differ from its manifest")
    frame = {**frame, "archiveMode": manifest.get("archiveMode")}
    return image, frame


def assert_same_pose(before: dict, after: dict) -> None:
    if before["archiveMode"] != after["archiveMode"]:
        raise ValueError("Before/after game archives differ")
    for key in ("focus", "heroTile", "cameraYaw", "cameraPitch", "cameraDistance", "cameraPan"):
        if key not in before or key not in after or not np.allclose(before[key], after[key], atol=0.00001, rtol=0):
            raise ValueError(f"Camera/fixture mismatch in {key}; no 2D registration is permitted")


def nearest_palette(rgb: np.ndarray, palette: np.ndarray) -> np.ndarray:
    unique, inverse = np.unique(rgb, axis=0, return_inverse=True)
    indices = []
    for chunk in np.array_split(unique.astype(float), max(1, math.ceil(len(unique) / 8192))):
        distance = ((chunk[:, None] - palette[None]) ** 2).sum(axis=2)
        indices.append(np.argmin(distance, axis=1))
    return palette[np.concatenate(indices)[inverse]].astype(np.uint8)


def imported_surface(model: Path, size: tuple[int, int], focus: list,
                     anchor: np.ndarray, model_origin: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray, dict]:
    data = model.read_bytes()
    if data[:8] != b"D3DMESH1" or len(data) < 20:
        raise ValueError("Expected an actual D3DMESH1 imported asset")
    count, width, height = struct.unpack_from("<III", data, 8)
    if not 1 <= count <= 20000 or not 1 <= width <= 2048 or not 1 <= height <= 2048:
        raise ValueError("Invalid imported asset dimensions")
    if len(data) != 20 + count * 60 + width * height * 3:
        raise ValueError("Imported asset byte count does not match its header")
    vertices = np.frombuffer(data, dtype="<f4", count=count * 15, offset=20).reshape(count, 3, 5).astype(float)
    if not np.isfinite(vertices).all():
        raise ValueError("Imported asset has a nonfinite vertex")
    texture = np.frombuffer(data, dtype=np.uint8, offset=20 + count * 60).reshape(height, width, 3)
    positions = vertices[:, :, :3] + model_origin
    cross = np.cross(positions[:, 1] - positions[:, 0], positions[:, 2] - positions[:, 0])
    lengths = np.linalg.norm(cross, axis=1)
    if np.any(lengths <= 0.000001):
        raise ValueError("Imported asset has a degenerate triangle")
    normals = cross / lengths[:, None]
    # The established native scale. Anchor is explicit and recorded, not fitted
    # to image pixels. Preserve vertex order/winding from the actual import.
    sx = anchor[0] + 32 * (positions[:, :, 0] - positions[:, :, 2] - focus[0] + focus[1])
    sy = anchor[1] + 16 * (positions[:, :, 0] + positions[:, :, 2] - focus[0] - focus[1]) - 32 * positions[:, :, 1]
    screen = np.stack([sx, sy], axis=2)
    physical = positions.copy()
    physical[:, :, 1] *= math.sqrt(2 / 3)
    depths = physical @ np.array([math.sqrt(3 / 8), 0.5, math.sqrt(3 / 8)])
    image_width, image_height = size
    depth_buffer = np.full((image_height, image_width), -np.inf)
    triangle_ids = np.full((image_height, image_width), -1, dtype=int)
    albedo = np.zeros((image_height, image_width, 3), dtype=np.uint8)
    for index, points in enumerate(screen):
        left = max(0, int(np.floor(points[:, 0].min())))
        right = min(image_width - 1, int(np.ceil(points[:, 0].max())))
        top = max(0, int(np.floor(points[:, 1].min())))
        bottom = min(image_height - 1, int(np.ceil(points[:, 1].max())))
        if left > right or top > bottom:
            continue
        denominator = ((points[1, 1] - points[2, 1]) * (points[0, 0] - points[2, 0])
                       + (points[2, 0] - points[1, 0]) * (points[0, 1] - points[2, 1]))
        if abs(denominator) < 0.000001:
            continue
        yy, xx = np.mgrid[top:bottom + 1, left:right + 1]
        w0 = ((points[1, 1] - points[2, 1]) * (xx + 0.5 - points[2, 0])
              + (points[2, 0] - points[1, 0]) * (yy + 0.5 - points[2, 1])) / denominator
        w1 = ((points[2, 1] - points[0, 1]) * (xx + 0.5 - points[2, 0])
              + (points[0, 0] - points[2, 0]) * (yy + 0.5 - points[2, 1])) / denominator
        w2 = 1 - w0 - w1
        depth = w0 * depths[index, 0] + w1 * depths[index, 1] + w2 * depths[index, 2]
        active = ((w0 >= -0.0001) & (w1 >= -0.0001) & (w2 >= -0.0001)
                  & (depth >= depth_buffer[top:bottom + 1, left:right + 1] - 0.0001))
        if not active.any():
            continue
        uv = np.stack([w0[active], w1[active], w2[active]], axis=1) @ vertices[index, :, 3:]
        tx = np.clip((uv[:, 0] * width).astype(int), 0, width - 1)
        ty = np.clip((uv[:, 1] * height).astype(int), 0, height - 1)
        rows, columns = np.nonzero(active)
        rows += top
        columns += left
        albedo[rows, columns] = texture[ty, tx]
        triangle_ids[rows, columns] = index
        depth_buffer[rows, columns] = depth[active]
    metadata = {
        "asset": str(model.resolve()), "sha256": hashlib.sha256(data).hexdigest(),
        "triangles": count, "textureSize": [width, height],
        "modelOriginXYZ": model_origin.tolist(), "screenAnchorXY": anchor.tolist(),
        "camera": "native orthographic yaw45/pitch30; exact 32:16:32 scale",
        "scope": "isolated imported mesh; no other objects or shadow-map occlusion; not runtime readback",
    }
    return albedo, triangle_ids, normals, metadata


def contact(images: list[tuple[str, Image.Image]], crop: list, output: Path) -> None:
    width, height = crop[2] - crop[0], crop[3] - crop[1]
    canvas = Image.new("RGB", (width * len(images), height + 30), (20, 20, 20))
    draw = ImageDraw.Draw(canvas)
    for index, (label, image) in enumerate(images):
        canvas.paste(image.crop(tuple(crop)), (index * width, 30))
        draw.text((index * width + 6, 9), label, fill="white")
    canvas.save(output)


def angle_contact(before: Path, after: Path, angles: tuple, output: Path) -> list[dict]:
    manifests = [json.loads((directory / "turntables" / "turntables.json").read_text(encoding="utf-8-sig")) for directory in (before, after)]
    width, height = DETAIL_CROP[2] - DETAIL_CROP[0], DETAIL_CROP[3] - DETAIL_CROP[1]
    canvas = Image.new("RGB", (width * 2, (height + 30) * len(angles)), (20, 20, 20))
    draw = ImageDraw.Draw(canvas)
    frames = []
    for row, angle in enumerate(angles):
        pair = []
        for index, directory in enumerate((before, after)):
            manifest = manifests[index]
            frame = next(item for item in manifest["frames"] if item["object"] == "cabin-east" and item["orbitDegrees"] == angle)
            frame = {**frame, "archiveMode": manifest.get("archiveMode")}
            image = Image.open(directory / "turntables" / frame["file"]).convert("RGB")
            if image.size != (640, 640):
                raise ValueError("Angle capture dimensions differ from the calibrated fixture")
            canvas.paste(image.crop(tuple(DETAIL_CROP)), (index * width, row * (height + 30) + 30))
            draw.text((index * width + 6, row * (height + 30) + 9),
                      f"{'Antes' if index == 0 else 'Depois'}: 3D real, {angle} graus", fill="white")
            pair.append(frame)
        assert_same_pose(pair[0], pair[1])
        frames.append({"orbitDegrees": angle, "before": pair[0]["file"], "after": pair[1]["file"],
                       "method": "Same actual camera/pixels/crop; no native fallback or alignment"})
    canvas.save(output)
    return frames


def ray_sweep(model: Path, ids: np.ndarray, normals: np.ndarray, masks: dict,
              focus: list, anchor: np.ndarray, model_origin: np.ndarray,
              sun_x_values: list[float]) -> dict:
    """Independent hard rays through actual imported triangles, not shadow LUTs."""
    data = model.read_bytes()
    count = struct.unpack_from("<I", data, 8)[0]
    triangles = np.frombuffer(data, dtype="<f4", count=count * 15, offset=20).reshape(count, 3, 5)[:, :, :3].astype(float)
    triangles += model_origin
    e1, e2 = triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0]
    sx = anchor[0] + 32 * (triangles[:, :, 0] - triangles[:, :, 2] - focus[0] + focus[1])
    sy = anchor[1] + 16 * (triangles[:, :, 0] + triangles[:, :, 2] - focus[0] - focus[1]) - 32 * triangles[:, :, 1]
    screen = np.stack([sx, sy], axis=2)
    toward_eye = np.array([math.sqrt(3 / 8), 0.5 / math.sqrt(2 / 3), math.sqrt(3 / 8)])
    result = {
        "method": "Moller-Trumbore hard rays through the unchanged imported mesh; two-sided triangles; 0.025 world-unit start offset",
        "limitations": "Isolated cabin only, at most512 evenly selected pixels per patch; no other architecture, PCF, slope bias or shadow texel grid. Geometric diagnosis, not runtime shadow readback.",
        "patches": {},
    }
    for key, mask in masks.items():
        y, x = np.nonzero(mask & (ids >= 0))
        selected = np.linspace(0, len(x) - 1, min(512, len(x)), dtype=int)
        x, y = x[selected], y[selected]
        triangle_ids = ids[y, x]
        p = screen[triangle_ids]
        denominator = ((p[:, 1, 1] - p[:, 2, 1]) * (p[:, 0, 0] - p[:, 2, 0])
                       + (p[:, 2, 0] - p[:, 1, 0]) * (p[:, 0, 1] - p[:, 2, 1]))
        w0 = ((p[:, 1, 1] - p[:, 2, 1]) * (x + 0.5 - p[:, 2, 0])
              + (p[:, 2, 0] - p[:, 1, 0]) * (y + 0.5 - p[:, 2, 1])) / denominator
        w1 = ((p[:, 2, 1] - p[:, 0, 1]) * (x + 0.5 - p[:, 2, 0])
              + (p[:, 0, 0] - p[:, 2, 0]) * (y + 0.5 - p[:, 2, 1])) / denominator
        weights = np.stack([w0, w1, 1 - w0 - w1], axis=1)
        points = (triangles[triangle_ids] * weights[:, :, None]).sum(axis=1)
        raw_normal = normals[triangle_ids]
        back = raw_normal @ toward_eye < 0
        visible_normal = np.where(back[:, None], -raw_normal, raw_normal)
        patch = {"sampleCount": len(x), "visibleBackFaceFraction": float(back.mean()), "lights": []}
        for light_x in sun_x_values:
            sun = np.array([light_x, 1.0, -0.3])
            sun /= np.linalg.norm(sun)
            pvec = np.cross(sun, e2)
            determinant = (e1 * pvec).sum(axis=1)
            inverse = np.divide(1, determinant, out=np.zeros_like(determinant), where=np.abs(determinant) > 1e-10)
            occluded = []
            for chunk in np.array_split(points + sun * 0.025, max(1, math.ceil(len(points) / 64))):
                difference = chunk[:, None] - triangles[None, :, 0]
                u = (difference * pvec[None]).sum(axis=2) * inverse
                qvec = np.cross(difference, e1[None])
                v = (qvec * sun).sum(axis=2) * inverse
                distance = (qvec * e2[None]).sum(axis=2) * inverse
                intersects = ((np.abs(determinant)[None] > 1e-10) & (u >= -1e-7) & (v >= -1e-7)
                              & (u + v <= 1 + 1e-7) & (distance > 0.0001))
                occluded.extend(intersects.any(axis=1).tolist())
            occluded = np.array(occluded)
            dot = np.maximum(visible_normal @ sun, 0)
            positive = dot > 0
            patch["lights"].append({
                "toLightUnnormalized": [light_x, 1, -0.3],
                "meanNdotLightTwoSided": float(dot.mean()),
                "hardShadowFractionAllPixels": float(occluded.mean()),
                "hardShadowFractionDirectFacing": float(occluded[positive].mean()) if positive.any() else None,
                "meanDirectAfterHardShadow": float((dot * ~occluded).mean()),
            })
        result["patches"][key] = patch
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path, help="Actual smoke directory with Meshy override")
    parser.add_argument("--after", type=Path, help="Actual new smoke output, same fixture/camera")
    parser.add_argument("--output", required=True, type=Path, help="Local diagnostics output")
    parser.add_argument("--rois", type=Path, help="Optional audited polygon JSON, in actual frame pixels")
    parser.add_argument("--model", type=Path, help="Actual D3DMESH1 for optional source/normal audit")
    parser.add_argument("--model-origin", type=vector, default=np.array([70.0, 0.0, 66.0]))
    parser.add_argument("--screen-anchor", type=str, default="320,335")
    parser.add_argument("--to-light", type=vector, default=np.array([0.18, 1.0, -0.3]))
    parser.add_argument("--ambient", type=vector, default=np.array([0.08, 0.085, 0.09]))
    parser.add_argument("--directional", type=vector, default=np.array([1.72, 1.62, 1.75]))
    parser.add_argument("--ray-sweep", action="store_true", help="Independent candidate sun hard-ray diagnosis; isolated actual mesh")
    parser.add_argument("--ray-sun-x", default="0.18,0.35,0.40,0.45,0.50,0.65,0.85", help="Candidate X components; Y=1,Z=-0.3")
    args = parser.parse_args()
    if np.linalg.norm(args.to_light) == 0 or np.any(args.ambient < 0) or np.any(args.directional < 0):
        raise ValueError("Light must be nonzero and gains must be nonnegative")
    args.output.mkdir(parents=True, exist_ok=True)
    before_image, frame = load_frame(args.before)
    native = Image.open(args.before / "turntables" / "cabin-east-native.png").convert("RGB")
    if before_image.size != native.size:
        raise ValueError("Native and 3D capture dimensions differ")
    if (native.size != (640, 640) or frame["focus"] != [71, 69]
            or not np.isclose(frame["cameraDistance"], 22, atol=0.00001, rtol=0)
            or not np.allclose(frame["cameraPan"], [0, 0], atol=0.00001, rtol=0)):
        raise ValueError("Audited polygons/projection require the 640x640 cabin fixture at focus71,69, distance22, pan0")
    if not np.allclose([frame["cameraYaw"], frame["cameraPitch"]], [math.pi / 4, math.pi / 6], atol=0.00001):
        raise ValueError("Material ROIs require the calibrated native orientation")
    images = [("Original (motor nativo)", native), ("Meshy antes (3D real)", before_image)]
    if args.after:
        after_image, after_frame = load_frame(args.after)
        assert_same_pose(frame, after_frame)
        if after_image.size != native.size:
            raise ValueError("New capture dimensions differ")
        native_after = Image.open(args.after / "turntables" / "cabin-east-native.png").convert("RGB")
        if native_after.size != native.size or native_after.tobytes() != native.tobytes():
            raise ValueError("Original native reference changed; recapture a matching control")
        images.append(("Meshy depois (3D real)", after_image))
    rois = json.loads(args.rois.read_text(encoding="utf-8")) if args.rois else ROIS
    arrays = [np.asarray(image) for _, image in images]
    masks = {key: roi_mask(native.size, value["polygon"]) for key, value in rois.items()}
    results = {}
    for key, roi in rois.items():
        samples = [statistics(array[masks[key]]) for array in arrays]
        target = np.array(samples[0]["linearMeanRGB"])
        result = {**roi, "native": samples[0], "before": samples[1],
                  "nativeOverBeforeLinearGainRGB": (target / np.maximum(samples[1]["linearMeanRGB"], 1e-12)).tolist()}
        if len(samples) == 3:
            result["after"] = samples[2]
            result["nativeOverAfterLinearGainRGB"] = (target / np.maximum(samples[2]["linearMeanRGB"], 1e-12)).tolist()
        results[key] = result
    report = {
        "schemaVersion": 1,
        "method": "Audited material polygons on actual native/before/after frames; no alignment, resizing, pixel fidelity score, or black-pixel filtering",
        "limitations": "Distribution ratios combine albedo, UV detail, baked native lighting, and runtime light; local eave shadows are not global material tint",
        "before": str(args.before.resolve()), "after": str(args.after.resolve()) if args.after else None,
        "frame": frame, "detailCropLTRB": DETAIL_CROP, "windowCropLTRB": WINDOW_CROP,
        "rois": results,
    }
    lighting_state_path = (args.after or args.before) / "lighting-state.json"
    if lighting_state_path.is_file():
        report["actualLightingState"] = json.loads(lighting_state_path.read_text(encoding="utf-8"))
    if len(arrays) == 3 and "roof_main" in masks:
        roof = masks["roof_main"]
        report["roofDarkTransitionDiagnostic"] = {
            "previouslyLitNowDarkPixels": int(np.count_nonzero(roof & (arrays[1] @ LUMA > 16) & (arrays[2] @ LUMA < 4))),
            "nativeDarkPixels": int(np.count_nonzero(roof & (arrays[0] @ LUMA < 4))),
            "beforeDarkPixels": int(np.count_nonzero(roof & (arrays[1] @ LUMA < 4))),
            "afterDarkPixels": int(np.count_nonzero(roof & (arrays[2] @ LUMA < 4))),
            "interpretation": "Color transitions inside the roof material patch; shadows and native painted black are valid. This is not a hole detector or artistic pass/fail score.",
        }
    if args.model:
        captured_asset = args.before / "d3d-models" / "cabin-east.d3d"
        if captured_asset.is_file() and hashlib.sha256(args.model.read_bytes()).digest() != hashlib.sha256(captured_asset.read_bytes()).digest():
            raise ValueError("Audit model differs from the actual captured override")
        if args.after:
            after_asset = args.after / "d3d-models" / "cabin-east.d3d"
            if not after_asset.is_file() or hashlib.sha256(args.model.read_bytes()).digest() != hashlib.sha256(after_asset.read_bytes()).digest():
                raise ValueError("Lighting-only comparison requires the same actual imported asset after the change")
        anchor = np.array([float(value) for value in args.screen_anchor.split(",")])
        if anchor.shape != (2,) or not np.isfinite(anchor).all():
            raise ValueError("Screen anchor requires two finite values")
        albedo, ids, normals, metadata = imported_surface(args.model, native.size, frame["focus"], anchor, args.model_origin)
        ground = json.loads((args.before / "ground-shadow-sources" / "ground-pieces.json").read_text(encoding="utf-8"))
        palette = np.array(ground["palette"], dtype=float)
        sun = args.to_light / np.linalg.norm(args.to_light)
        metadata["testLight"] = {"toLight": sun.tolist(), "ambientLinearRGB": args.ambient.tolist(), "directionalLinearRGB": args.directional.tolist()}
        metadata["lightingPredictionCaveat"] = (
            "Theoretical continuous source-RGB/direct-light calculation, not a runtime LUT reproduction or after screenshot. "
            "No occlusion; source normals can be irregular. Engine RGB bins, discrete diffuse levels, and floating palette searches "
            "can change individual pixels. Quantization here is isolated using the actual logical palette; actual after ROI statistics are authoritative.")
        for key, mask in masks.items():
            active = mask & (ids >= 0)
            source = albedo[active]
            if not len(source):
                raise ValueError(f"No imported mesh coverage in {key}")
            quantized = nearest_palette(source, palette)
            normal = normals[ids[active]]
            dot = np.maximum(0, normal @ sun)
            source_linear = linear(source)
            gain = args.ambient + dot[:, None] * args.directional
            predicted = nearest_palette(encoded(source_linear * gain), palette)
            target = linear(arrays[0][active]).mean(axis=0)
            results[key]["importedSourceAudit"] = {
                "projectedMeshPixels": int(active.sum()), "roiPixelsOutsideIsolatedMesh": int((mask & ~active).sum()),
                "sourceAlbedo": statistics(source), "unlitPaletteAlbedo": statistics(quantized),
                "meanNormalXYZ": normal.mean(axis=0).tolist(),
                "NdotLightP10P50P90": np.quantile(dot, [0.1, 0.5, 0.9]).tolist(),
                "meanNdotLight": float(dot.mean()),
                "nativeOverUnlitPaletteLinearGainRGB": (target / np.maximum(linear(quantized).mean(axis=0), 1e-12)).tolist(),
                "quantizationMeanAbsoluteRGBError": np.abs(quantized.astype(float) - source).mean(axis=0).tolist(),
                "testLightPredictionWithoutOcclusion": statistics(predicted),
            }
        report["importedSource"] = metadata
        if args.ray_sweep:
            sun_x_values = [float(value) for value in args.ray_sun_x.split(",")]
            if not 1 <= len(sun_x_values) <= 32 or not np.isfinite(sun_x_values).all():
                raise ValueError("Ray sweep requires 1..32 finite sun X components")
            report["isolatedHardRaySweep"] = ray_sweep(args.model, ids, normals, masks, frame["focus"], anchor, args.model_origin, sun_x_values)
        Image.fromarray(albedo).save(args.output / "isolated-imported-albedo.png")
    overlays = []
    for label, image in images:
        overlay = image.copy()
        draw = ImageDraw.Draw(overlay)
        for key, roi in rois.items():
            polygon = [tuple(point) for point in roi["polygon"]]
            draw.line(polygon + [polygon[0]], fill=(0, 255, 255), width=1)
            draw.text(polygon[0], key, fill="white")
        overlays.append((label, overlay))
    contact(images, DETAIL_CROP, args.output / "original-before-after.png")
    contact(overlays, [0, 0, native.width, native.height], args.output / "material-rois.png")
    window_labels = ["Original nativo", "3D antes", "3D depois"]
    contact([(window_labels[index], image) for index, (_, image) in enumerate(images)],
            WINDOW_CROP, args.output / "window-closeup.png")
    if args.after:
        report["slightOrbitPairs"] = angle_contact(args.before, args.after, (-5, 0, 5), args.output / "slight-orbit-before-after.png")
        report["allSidePairs"] = angle_contact(args.before, args.after, (0, 90, 180, 270), args.output / "four-angles-before-after.png")
    (args.output / "lighting-audit.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"report": str((args.output / "lighting-audit.json").resolve()), "materialPatches": len(results), "actualNewCapture": bool(args.after), "modelAudit": bool(args.model)}))


if __name__ == "__main__":
    main()
