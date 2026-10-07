#!/usr/bin/env python3
"""Render a local static GLB with its real UVs in Diablo's native projection.

Offline NumPy/Pillow diagnostic. No texture painting, 2D alignment, network,
normal-map lighting, or game palette quantization. The 3D fit matrix is saved.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

import inspect_glb as glb


def numbers(value: str) -> list[float]:
    return [float(part) for part in value.split(",")]


def wrapped(values: np.ndarray, mode: int) -> np.ndarray:
    if mode == 33071:
        return np.clip(values, 0, 1)
    if mode == 33648:
        return 1 - np.abs(np.mod(values, 2) - 1)
    if mode != 10497:
        raise ValueError(f"Unsupported texture wrapping {mode}")
    return np.mod(values, 1)


def render(vertices: np.ndarray, arrays: dict, materials: dict, textures: dict,
           size: tuple[int, int], ref: dict, focus: np.ndarray, orbit: float) -> tuple[Image.Image, dict]:
    yaw = math.pi / 4 + math.radians(orbit)
    pitch = math.pi / 6
    eye = np.array([math.cos(yaw) * math.cos(pitch), math.sin(pitch), math.sin(yaw) * math.cos(pitch)])
    physical = vertices.copy()
    physical[:, 1] *= math.sqrt(2 / 3)
    center = focus.copy()
    center[1] = 0
    relative = physical - center
    right = np.array([math.sin(yaw), 0, -math.cos(yaw)])
    up = np.array([-math.cos(yaw) * math.sin(pitch), math.cos(pitch), -math.sin(yaw) * math.sin(pitch)])
    reference = ref["referenceTile"]
    origin = ref["pixelOrigin"]
    anchor = np.array([32 * (focus[0] - focus[2] - reference[0] + reference[1]) - origin[0],
                       16 * (focus[0] + focus[2] - reference[0] - reference[1]) - origin[1]])
    screen = np.column_stack([anchor[0] + 32 * math.sqrt(2) * (relative @ right),
                              anchor[1] - 32 * math.sqrt(2) * (relative @ up), relative @ eye])
    width, height = size
    rgba = np.zeros((height, width, 4), dtype=np.uint8)
    depth = np.full((height, width), -np.inf)
    culled = drawn = 0
    for index, face in enumerate(arrays["triangles"]):
        material = materials.get(int(arrays["material_ids"][index]), {})
        alpha_mode = material.get("alphaMode", "OPAQUE")
        if alpha_mode not in ("OPAQUE", "MASK"):
            raise ValueError("BLEND material requires transparent compositing; not silently approximated")
        if not material.get("doubleSided", False):
            points = physical[face]
            if np.dot(np.cross(points[1] - points[0], points[2] - points[0]), eye) <= 0:
                culled += 1
                continue
        p = screen[face]
        left, right_edge = max(0, int(np.floor(p[:, 0].min()))), min(width - 1, int(np.ceil(p[:, 0].max())))
        top, bottom = max(0, int(np.floor(p[:, 1].min()))), min(height - 1, int(np.ceil(p[:, 1].max())))
        if left > right_edge or top > bottom:
            continue
        denominator = (p[1, 1] - p[2, 1]) * (p[0, 0] - p[2, 0]) + (p[2, 0] - p[1, 0]) * (p[0, 1] - p[2, 1])
        if abs(denominator) < 1e-8:
            continue
        yy, xx = np.mgrid[top:bottom + 1, left:right_edge + 1]
        w0 = ((p[1, 1] - p[2, 1]) * (xx + 0.5 - p[2, 0]) + (p[2, 0] - p[1, 0]) * (yy + 0.5 - p[2, 1])) / denominator
        w1 = ((p[2, 1] - p[0, 1]) * (xx + 0.5 - p[2, 0]) + (p[0, 0] - p[2, 0]) * (yy + 0.5 - p[2, 1])) / denominator
        w2 = 1 - w0 - w1
        triangle_depth = w0 * p[0, 2] + w1 * p[1, 2] + w2 * p[2, 2]
        active = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6) & (triangle_depth > depth[top:bottom + 1, left:right_edge + 1])
        if not active.any():
            continue
        weights = np.stack([w0[active], w1[active], w2[active]], axis=1)
        colors = weights @ arrays["colors"][face]
        factor = np.asarray(material.get("baseColorFactor", [1, 1, 1, 1]))
        tex_info = material.get("baseColorTexture")
        if tex_info:
            uv = weights @ arrays["uv"][face]
            sampler = tex_info["sampler"]
            texture = textures[tex_info["image"]]
            u = wrapped(uv[:, 0], sampler.get("wrapS", 10497))
            v = wrapped(uv[:, 1], sampler.get("wrapT", 10497))
            tx = np.minimum((u * texture.shape[1]).astype(int), texture.shape[1] - 1)
            ty = np.minimum((v * texture.shape[0]).astype(int), texture.shape[0] - 1)
            color = texture[ty, tx].astype(float) / 255
        else:
            color = np.ones((len(weights), 4))
        # Vertex colors/factors are linear; base-color image is sRGB.
        linear = np.where(color[:, :3] <= 0.04045, color[:, :3] / 12.92, ((color[:, :3] + 0.055) / 1.055) ** 2.4)
        linear *= colors[:, :3] * factor[:3]
        rgb = np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.maximum(linear, 0) ** (1 / 2.4) - 0.055)
        alpha = color[:, 3] * colors[:, 3] * factor[3]
        accepted = np.ones(len(weights), dtype=bool) if alpha_mode == "OPAQUE" else alpha >= material.get("alphaCutoff", 0.5)
        row, column = np.nonzero(active)
        row, column = row[accepted] + top, column[accepted] + left
        rgba[row, column, :3] = np.clip(np.rint(rgb[accepted] * 255), 0, 255).astype(np.uint8)
        rgba[row, column, 3] = 255
        depth[row, column] = triangle_depth[active][accepted]
        drawn += 1
    covered = rgba[:, :, 3] != 0
    return Image.fromarray(rgba), {"orbitDegrees": orbit, "cameraYaw": yaw, "cameraPitch": pitch,
        "drawnTriangles": drawn, "culledTriangles": culled, "coveredPixels": int(covered.sum()),
        "touchesFrameBoundary": bool(covered[0].any() or covered[-1].any() or covered[:, 0].any() or covered[:, -1].any())}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--reference", required=True, type=Path, help="Original PNG with matching .json composition metadata")
    parser.add_argument("--bounds", type=numbers, default=[69.55, 66.15, 73.2, 71.6, 4.87], help="World minX,minZ,maxX,maxZ,peakHeight")
    parser.add_argument("--source-fit-bounds", type=numbers, help="Source minX,minY,minZ,maxX,maxY,maxZ; explicit body fit excluding floor if measured")
    parser.add_argument("--yaw", type=float, default=0, help="Model rotation in 3D before fit, in degrees")
    parser.add_argument("--angles", type=numbers, default=[0, 90, 180, 270])
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    document, binary, _ = glb.read_glb(args.model)
    reader = glb.Reader(args.model, document, binary)
    arrays, primitives, warnings = glb.flatten(reader)
    records, _ = glb.extract_materials(reader, args.output)
    materials = {record["index"]: record for record in records}
    textures = {record["baseColorTexture"]["image"]: np.asarray(Image.open(record["baseColorTexture"]["path"]).convert("RGBA"))
                for record in records if "baseColorTexture" in record}
    source = arrays["world_vertices"].astype(float)
    if args.source_fit_bounds is not None:
        if len(args.source_fit_bounds) != 6:
            raise ValueError("source-fit-bounds requires six coordinates")
        source_min, source_max = np.array(args.source_fit_bounds[:3]), np.array(args.source_fit_bounds[3:])
    else:
        source_min, source_max = source.min(axis=0), source.max(axis=0)
    if len(args.bounds) != 5 or np.any(source_max <= source_min):
        raise ValueError("Invalid fit bounds")
    min_x, min_z, max_x, max_z, height = args.bounds
    theta = math.radians(args.yaw)
    rotation = np.array([[math.cos(theta), 0, -math.sin(theta)], [0, 1, 0], [math.sin(theta), 0, math.cos(theta)]])
    source_center = np.array([(source_min[0] + source_max[0]) / 2, source_min[1], (source_min[2] + source_max[2]) / 2])
    # Rotate first, then calculate the axis-aligned span of the specified source
    # box in 3D. No output pixels are shifted/aligned to the reference image.
    rotated_span = np.abs(rotation) @ (source_max - source_min)
    scale = np.array([max_x - min_x, height, max_z - min_z]) / rotated_span
    world_center = np.array([(min_x + max_x) / 2, 0, (min_z + max_z) / 2])
    matrix = np.eye(4)
    matrix[:3, :3] = np.diag(scale) @ rotation
    matrix[:3, 3] = world_center - matrix[:3, :3] @ source_center
    vertices = source @ matrix[:3, :3].T + matrix[:3, 3]
    ref = json.loads(args.reference.with_suffix(".json").read_text(encoding="utf-8"))
    reference_image = Image.open(args.reference).convert("RGBA")
    size = reference_image.size
    if list(size) != ref["pixelSize"]:
        raise ValueError("Reference PNG and native composition metadata disagree")
    report = {"input": str(args.model.resolve()), "sha256": hashlib.sha256(args.model.read_bytes()).hexdigest(),
        "triangles": len(arrays["triangles"]), "vertices": len(source), "primitives": primitives,
        "sourceBounds": [source.min(axis=0).tolist(), source.max(axis=0).tolist()],
        "fitSourceBounds": [source_min.tolist(), source_max.tolist()], "modelYawDegrees": args.yaw,
        "fitWorldBounds": args.bounds, "worldMatrix": matrix.tolist(), "reference": ref,
        "render": "unlit real base-color UV/vertex colors, nearest sampling; no normal/metal/roughness lighting or game palette",
        "bodyBoundsMeasured": args.source_fit_bounds is not None, "warnings": warnings,
        "approval": "candidate only; visible model/source mismatch requires artistic review", "frames": []}
    panels = [(reference_image, "Original native artwork")]
    for orbit in args.angles:
        rendered, stats = render(vertices, arrays, materials, textures, size, ref, world_center, orbit)
        filename = f"candidate-orbit-{orbit:g}.png"
        rendered.save(args.output / filename)
        stats["file"] = filename
        report["frames"].append(stats)
        panels.append((rendered, f"Actual GLB, orbit {orbit:g}, model yaw {args.yaw:g}"))
    contact = Image.new("RGB", (size[0] * len(panels), size[1] + 30), (20, 20, 20))
    for index, (panel, label) in enumerate(panels):
        contact.paste(panel, (index * size[0], 30), panel)
        ImageDraw.Draw(contact).text((index * size[0] + 8, 8), label, fill=(235, 235, 235))
    contact.save(args.output / "reference-and-candidate.png")
    (args.output / "render.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output.resolve()), "triangles": report["triangles"], "frames": len(report["frames"])}))


if __name__ == "__main__":
    main()
