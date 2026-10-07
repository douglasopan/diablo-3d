#!/usr/bin/env python3
"""Convert an inspected, calibrated single-material GLB into a local D3DMESH1 asset.

The explicit 3D transform is saved in the manifest. No screen-space registration
or native-photo replacement is performed. Generated art remains local for review.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

import numpy as np
from PIL import Image


def export(inspection_path: Path, fit_path: Path, output: Path, texture_size: int) -> dict:
    report = json.loads(inspection_path.read_text(encoding="utf-8-sig"))
    fit = json.loads(fit_path.read_text(encoding="utf-8-sig"))
    arrays = np.load(report["intermediate"]["path"])
    positions = arrays["world_vertices"].astype(np.float64)
    triangles = arrays["triangles"].astype(np.int64)
    uv = arrays["uv"].astype(np.float64)
    material_ids = arrays["material_ids"]
    materials = set(material_ids.tolist())
    if len(materials) != 1 or not np.isfinite(positions).all() or not np.isfinite(uv).all():
        raise ValueError("Only finite single-material meshes are supported")
    material_id = next(iter(materials))
    material = next(item for item in report["materials"] if item["index"] == material_id)
    image_path = material["baseColorTexture"]["path"]
    yaw = math.radians(float(fit.get("yawDegrees", 0)))
    rotation = np.array([[math.cos(yaw), 0, math.sin(yaw)], [0, 1, 0], [-math.sin(yaw), 0, math.cos(yaw)]])
    rotated = positions @ rotation.T
    source_min = np.array(fit["rotatedSourceMin"], dtype=np.float64)
    source_max = np.array(fit["rotatedSourceMax"], dtype=np.float64)
    target_min = np.array(fit["targetMinRelative"], dtype=np.float64)
    target_max = np.array(fit["targetMaxRelative"], dtype=np.float64)
    if not np.isfinite(np.concatenate([source_min, source_max, target_min, target_max])).all() or np.any(source_max <= source_min) or np.any(target_max <= target_min):
        raise ValueError("Fit bounds must be finite and have positive extents")
    scale = (target_max - target_min) / (source_max - source_min)
    calibrated = (rotated - source_min) * scale + target_min
    if calibrated[:, 1].min() < -0.0001:
        raise ValueError("Calibrated model crosses below the town ground; choose a valid ground bound")
    calibrated[:, 1] = np.maximum(calibrated[:, 1], 0)
    if np.any(uv < -0.0001) or np.any(uv > 1.0001):
        raise ValueError("This import format requires normalized UVs")
    uv = np.clip(uv, 0, 1)
    cross = np.cross(calibrated[triangles[:, 1]] - calibrated[triangles[:, 0]], calibrated[triangles[:, 2]] - calibrated[triangles[:, 0]])
    valid = np.linalg.norm(cross, axis=1) > 0.000001
    triangles = triangles[valid]
    if not 1 <= len(triangles) <= 20000:
        raise ValueError("Triangle count is outside the runtime import limit")
    vertices = np.column_stack([calibrated, uv])[triangles].astype("<f4")
    image = Image.open(image_path).convert("RGB")
    image.thumbnail((texture_size, texture_size), Image.Resampling.LANCZOS)
    rgb = np.asarray(image, dtype=np.uint8)
    factor = np.array(material.get("baseColorFactor", [1, 1, 1, 1])[:3])
    if not np.allclose(factor, 1):
        rgb = np.clip(rgb * factor, 0, 255).astype(np.uint8)
    output.parent.mkdir(parents=True, exist_ok=True)
    data = b"D3DMESH1" + struct.pack("<III", len(triangles), image.width, image.height) + vertices.tobytes() + rgb.tobytes()
    output.write_bytes(data)
    manifest = {
        "schemaVersion": 1, "status": "experimental-local-review", "approvedForGame": False,
        "sourceGlb": report["input"], "sourceSha256": report["inputSha256"],
        "output": str(output), "sha256": hashlib.sha256(data).hexdigest(),
        "triangles": len(triangles), "skippedDegenerateTriangles": int((~valid).sum()),
        "textureSize": [image.width, image.height], "fit": fit, "scale": scale.tolist(),
        "boundsRelative": {"min": calibrated.min(axis=0).tolist(), "max": calibrated.max(axis=0).tolist()},
        "uvConvention": "top row first; preserve inspected effective glTF UVs",
        "simulation": "native map, collision and actor positions are preserved by the loader",
        "limitations": "Geometric fit does not establish fidelity; compare actual native and forced-3D captures before approval.",
    }
    output.with_suffix(output.suffix + ".json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inspection", type=Path)
    parser.add_argument("--fit", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--texture-size", type=int, default=1024, choices=[256, 512, 1024, 2048])
    args = parser.parse_args()
    result = export(args.inspection.resolve(), args.fit.resolve(), args.output.resolve(), args.texture_size)
    print(json.dumps({key: result[key] for key in ["output", "triangles", "textureSize", "sha256", "approvedForGame"]}))


if __name__ == "__main__":
    main()
