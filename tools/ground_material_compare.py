#!/usr/bin/env python3
"""Private, offline comparison of explicitly masked ground-material candidates.

Requires numpy and Pillow. This tool neither modifies the game nor approximates
its lighting/palette. It ray-samples the DrawGround quad's inverse projection,
retains the exported nearest-sampled alpha/coverage, and changes RGB only inside
binary, manually audited support masks. The editor export uses generic fallback
grass beneath solid/blocked/replaced scenery: that is always protected here.

Example config (all file paths are relative to the config):
{
  "schemaVersion": 1,
  "excludeFallback": true,
  "materials": {
    "grass": {"path": "masters/grass.png", "worldPeriod": [2, 2],
              "origin": [0, 0], "linearGain": 1, "uvMode": "world",
              "sha256": "optional expected digest"}
  },
  "pieceMasks": [
    {"key": "p859", "material": "grass", "path": "masks/p859-grass.png",
     "blendWidthPixels": 2}
  ],
  "nativeTextureOverrides": [],
  "comparisonUvModes": ["world", "legacy-per-tile"],
  "views": [
    {"name": "close", "mode": "perspective", "position": [48, 1.65, 48],
     "target": [44, 0, 44], "fovYDegrees": 70},
    {"name": "iso", "mode": "orthographic", "target": [44, 0, 44],
     "yawDegrees": 45, "pitchDegrees": 30, "verticalSpan": 12}
  ]
}

Binary masks must be 64x32, contain only 0/255, have no overlaps, and never
select transparent native pixels. Optional entry "tiles": [[x,z],...] limits
a piece mask to explicit map occurrences. No normals/roughness/PBR are inferred.
The output and every input derived from the game must stay local/ignored.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import html
import json
import math
from pathlib import Path
import re
import sys
from typing import Any

import numpy as np
from PIL import Image, ImageDraw


FAMILIES = {"grass", "dirt", "dry", "mud", "stone", "bank"}
SAMPLERS = {"nearest", "trilinear", "anisotropic"}
UV_MODES = {"world", "legacy-per-tile"}
QUAD_ORIGIN = np.array([-1.46875, -0.46875], dtype=np.float64)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")


def resolve_input(root: Path, entry: dict[str, Any]) -> Path:
    path = (root / entry["path"]).resolve()
    if not path.is_file():
        raise ValueError(f"Missing input: {entry['path']}")
    expected = entry.get("sha256")
    if expected is not None and digest(path) != expected.lower():
        raise ValueError(f"SHA-256 mismatch: {entry['path']}")
    return path


def srgb_decode(value: np.ndarray) -> np.ndarray:
    encoded = value.astype(np.float32) / 255.0
    return np.where(encoded <= 0.04045, encoded / 12.92, ((encoded + 0.055) / 1.055) ** 2.4)


def srgb_encode(value: np.ndarray) -> np.ndarray:
    linear = np.clip(value, 0, 1)
    encoded = np.where(linear <= 0.0031308, linear * 12.92, 1.055 * linear ** (1 / 2.4) - 0.055)
    return np.rint(encoded * 255).astype(np.uint8)


def build_mips(rgb: np.ndarray) -> list[np.ndarray]:
    """Linear float BOX area resize, floor next axis, no working-chain encode.

    The size contract matches a GPU mip chain: 1254 -> 627 -> 313, and 3 -> 1.
    Masters are opaque; no alpha/premultiplication or cutout coverage is inferred.
    """
    levels = [srgb_decode(rgb)]
    while levels[-1].shape[:2] != (1, 1):
        parent = levels[-1]
        h, w = parent.shape[:2]
        size = (max(1, w // 2), max(1, h // 2))
        channels = [np.asarray(Image.fromarray(parent[..., channel], mode="F")
                               .resize(size, Image.Resampling.BOX), dtype=np.float32)
                    for channel in range(3)]
        levels.append(np.stack(channels, axis=-1))
    return levels


def sample_bilinear(level: np.ndarray, uv: np.ndarray) -> np.ndarray:
    h, w = level.shape[:2]
    texel = uv * [w, h] - 0.5
    corner = np.floor(texel).astype(np.int64)
    fraction = texel - corner
    x0, y0 = corner[:, 0] % w, corner[:, 1] % h
    x1, y1 = (x0 + 1) % w, (y0 + 1) % h
    fx, fy = fraction[:, 0, None], fraction[:, 1, None]
    top = level[y0, x0] * (1 - fx) + level[y0, x1] * fx
    bottom = level[y1, x0] * (1 - fx) + level[y1, x1] * fx
    return top * (1 - fy) + bottom * fy


def sample_trilinear(levels: list[np.ndarray], uv: np.ndarray, lod: np.ndarray) -> np.ndarray:
    lod = np.clip(lod, 0, len(levels) - 1)
    low = np.floor(lod).astype(np.int32)
    high = np.minimum(low + 1, len(levels) - 1)
    weight = lod - low
    out = np.empty((len(uv), 3), np.float32)
    for index, level in enumerate(levels):
        selected = low == index
        if selected.any():
            out[selected] = sample_bilinear(level, uv[selected]) * (1 - weight[selected, None])
    for index, level in enumerate(levels):
        selected = high == index
        if selected.any():
            out[selected] += sample_bilinear(level, uv[selected]) * weight[selected, None]
    return out


def footprint(jacobian: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Singular footprint axes in texels; Jacobian rows are texture x/y."""
    covariance = jacobian @ np.swapaxes(jacobian, -1, -2)
    eigenvalues, eigenvectors = np.linalg.eigh(covariance)
    return (np.sqrt(np.maximum(eigenvalues[:, 1], 0)),
            np.sqrt(np.maximum(eigenvalues[:, 0], 0)), eigenvectors[:, :, 1])


def inward_weights(mask: np.ndarray, width: float) -> np.ndarray:
    """Bounded eight-neighbor distance; feather never reaches a zero pixel.

    Distance is measured to the binary boundary at pixel-center distance - 0.5.
    Thus width 2 blends the first two selected native pixel rings at 0.25/0.75.
    The image's outer border is treated as zero. This is a Chebyshev-distance
    approximation, deliberately independent of scipy and of generated artwork.
    """
    if not math.isfinite(width) or not 0 <= width <= 32:
        raise ValueError("blendWidthPixels must lie between 0 and 32")
    if width == 0:
        return mask.astype(np.float32)
    distance = np.zeros(mask.shape, np.float32)
    current = mask.copy()
    for _ in range(math.ceil(width) + 1):
        distance[current] += 1
        padded = np.pad(current, 1, constant_values=False)
        current = np.logical_and.reduce([padded[y:y + mask.shape[0], x:x + mask.shape[1]]
                                         for y in range(3) for x in range(3)])
        if not current.any():
            break
    return np.where(mask, np.clip((distance - 0.5) / width, 0, 1), 0).astype(np.float32)


def sample_material(material: dict[str, Any], world: np.ndarray,
                    derivatives: np.ndarray, sampler: str, max_anisotropy: int,
                    legacy_uv: np.ndarray | None = None,
                    uv_override: str | None = None) -> tuple[np.ndarray, dict[str, Any]]:
    """Return linear RGB, with preview gain applied before the final encoding."""
    levels = material["mips"]
    h, w = levels[0].shape[:2]
    period = np.array(material["worldPeriod"], dtype=np.float64)
    uv_mode = uv_override or material["uvMode"]
    if uv_mode == "legacy-per-tile":
        if legacy_uv is None:
            raise ValueError("Per-tile comparison requires native quad UV coordinates")
        uv = legacy_uv
        uv_derivatives = np.einsum("ij,...jk->...ik", np.array([[0.5, -0.5], [0.5, 0.5]]), derivatives)
        jacobian = uv_derivatives * np.array([w, h])[None, :, None]
    else:
        uv = (world - material["origin"]) / period
        jacobian = derivatives * (np.array([w, h]) / period)[None, :, None]
    major, minor, direction = footprint(jacobian)
    if sampler == "nearest":
        xy = np.floor((uv % 1) * [w, h]).astype(np.int64)
        color = levels[0][xy[:, 1] % h, xy[:, 0] % w]
        taps = np.ones(len(world), np.int32)
        lod = np.zeros(len(world))
    elif sampler == "trilinear":
        lod = np.log2(np.maximum(major, 1))
        color = sample_trilinear(levels, uv, lod)
        taps = np.ones(len(world), np.int32)
    else:
        # A bounded offline approximation, not the D3D hardware sampler. Average
        # trilinear taps along the major footprint, with LOD from its minor axis.
        effective_minor = np.maximum.reduce((minor, major / max_anisotropy, np.ones(len(world))))
        lod = np.log2(effective_minor)
        taps = np.clip(np.ceil(major / effective_minor), 1, max_anisotropy).astype(np.int32)
        accum = np.zeros((len(world), 3), np.float32)
        major_uv = direction * major[:, None] / [w, h]
        for tap_count in np.unique(taps):
            selected = taps == tap_count
            for tap in range(int(tap_count)):
                displacement = ((tap + 0.5) / tap_count - 0.5) * major_uv[selected]
                accum[selected] += sample_trilinear(levels, uv[selected] + displacement, lod[selected]) / tap_count
        color = accum
    color = color * material["linearGain"]
    statistics = {"majorTexelsPerPixel": quantiles(major), "minorTexelsPerPixel": quantiles(minor),
                  "lod": quantiles(lod), "tapCount": quantiles(taps), "uvMode": uv_mode,
                  "mapping": "(worldXZ-origin)/worldPeriod" if uv_mode == "world" else "full master in native quad u,v; resets at every tile",
                  "linearGain": material["linearGain"],
                  "linearGainScope": "PREVIEW-only brightness calibration; native artwork contains lighting; not engine light validation"}
    return color, statistics


def quantiles(array: np.ndarray) -> dict[str, float] | None:
    values = np.asarray(array)[np.isfinite(array)]
    if not len(values):
        return None
    return {key: float(value) for key, value in zip(("min", "p10", "p50", "p90", "max"),
                                                    np.quantile(values, [0, 0.1, 0.5, 0.9, 1]))}


def vector(value: Any, length: int, label: str) -> np.ndarray:
    result = np.asarray(value, dtype=np.float64)
    if result.shape != (length,) or not np.isfinite(result).all():
        raise ValueError(f"{label} must have {length} finite values")
    return result


def camera_ground(view: dict[str, Any], width: int, height: int,
                  shift_x: float = 0, shift_y: float = 0) -> tuple[np.ndarray, np.ndarray]:
    target = vector(view["target"], 3, "camera target")
    mode = view["mode"]
    if mode == "orthographic":
        yaw = math.radians(float(view.get("yawDegrees", 45)))
        pitch = math.radians(float(view.get("pitchDegrees", 30)))
        outward = np.array([math.cos(yaw) * math.cos(pitch), math.sin(pitch),
                            math.sin(yaw) * math.cos(pitch)])
        eye = target + outward * 256
    elif mode == "perspective":
        eye = vector(view["position"], 3, "camera position")
    else:
        raise ValueError("Camera mode must be orthographic or perspective")
    forward = target - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, [0, 1, 0])
    if np.linalg.norm(right) < 1e-8:
        raise ValueError("Camera direction cannot be vertical")
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    x, y = np.meshgrid(np.arange(width) + 0.5 + shift_x - width / 2,
                       height / 2 - (np.arange(height) + 0.5 + shift_y))
    if mode == "perspective":
        fov = float(view.get("fovYDegrees", 70))
        if not 1 < fov < 175:
            raise ValueError("fovYDegrees must lie between 1 and 175")
        focal = height / (2 * math.tan(math.radians(fov) / 2))
        ray = forward + x[..., None] / focal * right + y[..., None] / focal * up
        origin = np.broadcast_to(eye, ray.shape)
    else:
        span = float(view.get("verticalSpan", 12))
        if not math.isfinite(span) or span <= 0:
            raise ValueError("verticalSpan must be positive")
        origin = eye + (x[..., None] * right + y[..., None] * up) * span / height
        ray = np.broadcast_to(forward, origin.shape)
    with np.errstate(divide="ignore", invalid="ignore"):
        distance = -origin[..., 1] / ray[..., 1]
        world = origin + distance[..., None] * ray
    valid = (distance > 0) & np.isfinite(world).all(axis=-1)
    if mode == "perspective":
        # Distance along forward, in the same perspective convention as the engine.
        depth = np.sum((world - eye) * forward, axis=-1)
        valid &= (depth >= float(view.get("near", 0.05))) & (depth <= float(view.get("far", 100)))
    return world[..., [0, 2]], valid


class Ground:
    def __init__(self, reference_path: Path, config_path: Path, config: dict[str, Any]):
        reference = read_json(reference_path)
        self.reference_source = reference.get("source", "")
        self.input_files: list[dict[str, Any]] = []
        self.keys = [entry["key"] for entry in reference["textures"]]
        if len(set(self.keys)) != len(self.keys):
            raise ValueError("Duplicate native texture key")
        self.key_ids = {key: index for index, key in enumerate(self.keys)}
        override_entries = config.get("nativeTextureOverrides", [])
        overrides = {entry["key"]: entry for entry in override_entries}
        if len(overrides) != len(override_entries) or not set(overrides) <= set(self.keys):
            raise ValueError("Duplicate/unknown native texture override")
        pixels = []
        for entry in reference["textures"]:
            key = entry["key"]
            selected = overrides.get(key, entry)
            root = config_path.parent if key in overrides else reference_path.parent
            path = resolve_input(root, selected)
            rgba = np.array(Image.open(path).convert("RGBA"))
            if rgba.shape != (32, 64, 4) or not set(np.unique(rgba[..., 3])) <= {0, 255}:
                raise ValueError(f"Native {key} must be 64x32 RGBA with binary alpha")
            pixels.append(rgba)
            self.input_files.append({"role": "native-override" if key in overrides else "native",
                                     "key": key, "path": str(path), "sha256": digest(path)})
        self.rgba = np.stack(pixels)
        tiles = reference["tiles"]
        if not tiles:
            raise ValueError("Native ground has no tiles")
        if any(int(x) != x or int(z) != z or x < 0 or z < 0 or key not in self.key_ids for x, z, key in tiles):
            raise ValueError("Invalid native tile coordinates/key")
        nx, nz = max(t[0] for t in tiles) + 1, max(t[1] for t in tiles) + 1
        self.grid = np.full((nz, nx), -1, np.int32)
        for x, z, key in tiles:
            if self.grid[z, x] != -1:
                raise ValueError("Duplicate native tile coordinate")
            self.grid[z, x] = self.key_ids[key]
        self.materials: list[dict[str, Any]] = []
        material_ids = {}
        for name, entry in config["materials"].items():
            if name not in FAMILIES:
                raise ValueError(f"Unknown material family: {name}")
            path = resolve_input(config_path.parent, entry)
            rgba = np.array(Image.open(path).convert("RGBA"))
            if not np.all(rgba[..., 3] == 255):
                raise ValueError(f"Base-color master must be opaque: {name}")
            period = vector(entry["worldPeriod"], 2, "worldPeriod")
            if (period <= 0).any():
                raise ValueError("worldPeriod must be positive native tile units")
            origin = vector(entry.get("origin", [0, 0]), 2, "material origin")
            gain = float(entry.get("linearGain", 1))
            if not math.isfinite(gain) or not 0 <= gain <= 16:
                raise ValueError("linearGain must be finite and lie between 0 and 16")
            uv_mode = entry.get("uvMode", "world")
            if uv_mode not in UV_MODES:
                raise ValueError("uvMode must be world or legacy-per-tile")
            rgb = rgba[..., :3]
            material_ids[name] = len(self.materials)
            self.materials.append({"name": name, "rgb": rgb, "mips": build_mips(rgb),
                                   "worldPeriod": period, "origin": origin, "path": path,
                                   "linearGain": gain, "uvMode": uv_mode})
            self.input_files.append({"role": "master-basecolor", "family": name, "path": str(path),
                                     "sha256": digest(path), "size": [rgb.shape[1], rgb.shape[0]],
                                     "worldPeriod": period.tolist(), "origin": origin.tolist(),
                                     "configuredUvMode": uv_mode, "linearGain": gain,
                                     "linearGainScope": "PREVIEW-only brightness calibration; not engine light validation",
                                     "texelsPerWorldUnit": (np.array([rgb.shape[1], rgb.shape[0]]) / period).tolist()})
        self.mask_ids = np.full(self.rgba.shape[:3], -1, np.int16)
        self.mask_weights = np.zeros(self.rgba.shape[:3], np.float32)
        self.occurrence_masks: list[tuple[int, int, np.ndarray, set[tuple[int, int]], np.ndarray]] = []
        for entry in config.get("pieceMasks", []):
            key, family = entry["key"], entry["material"]
            if key not in self.key_ids or family not in material_ids:
                raise ValueError(f"Unknown mask key/family: {key}/{family}")
            if key == "fallback":
                raise ValueError("Fallback terrain is protected and cannot be annotated")
            key_id, material_id = self.key_ids[key], material_ids[family]
            path = resolve_input(config_path.parent, entry)
            image = Image.open(path)
            mask_raw = np.array(image.convert("L"))
            if mask_raw.shape != (32, 64) or not set(np.unique(mask_raw)) <= {0, 255}:
                raise ValueError(f"Mask {key}/{family} must be binary 64x32")
            mask = mask_raw != 0
            if not mask.any() or np.any(mask & (self.rgba[key_id, ..., 3] == 0)):
                raise ValueError(f"Mask {key}/{family} is empty or escapes native opaque coverage")
            blend_width = float(entry.get("blendWidthPixels", 0))
            weights = inward_weights(mask, blend_width)
            if "tiles" in entry:
                occurrences = set()
                for tile in entry["tiles"]:
                    x, z = tile
                    if not (0 <= x < nx and 0 <= z < nz) or self.grid[z, x] != key_id:
                        raise ValueError(f"Mask occurrence does not contain native {key}: {tile}")
                    occurrences.add((int(x), int(z)))
                if not occurrences:
                    raise ValueError("An occurrence-restricted mask cannot have no tiles")
                self.occurrence_masks.append((key_id, material_id, mask, occurrences, weights))
            else:
                if np.any(mask & (self.mask_ids[key_id] >= 0)):
                    raise ValueError(f"Overlapping support masks: {key}")
                self.mask_ids[key_id][mask] = material_id
                self.mask_weights[key_id][mask] = weights[mask]
            self.input_files.append({"role": "support-mask", "key": key, "family": family,
                                     "path": str(path), "sha256": digest(path), "selectedNativePixels": int(mask.sum()),
                                     "blendWidthPixels": blend_width,
                                     "blendScope": "inward feather only, Chebyshev boundary distance in native pixels; exact support/alpha unchanged",
                                     "tiles": entry.get("tiles", "all occurrences of this key")})
        # Check restricted/global and restricted/restricted overlap before sampling.
        for index, (key_id, _, mask, occurrences, _) in enumerate(self.occurrence_masks):
            if np.any(mask & (self.mask_ids[key_id] >= 0)):
                raise ValueError(f"Restricted mask overlaps global mask: {self.keys[key_id]}")
            for prior_key, _, prior_mask, prior_tiles, _ in self.occurrence_masks[:index]:
                if key_id == prior_key and occurrences & prior_tiles and np.any(mask & prior_mask):
                    raise ValueError(f"Overlapping occurrence support masks: {self.keys[key_id]}")

    def lookup(self, world: np.ndarray, valid: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
        """Exact UV transform/nearest opacity; stable (tile x+z, tile x) order.

        This is the exported ground plane only. Export lacks SOL flags, scenery,
        lighting, triangle raster edge rules, actor occlusion, or runtime picking.
        """
        shape = valid.shape
        flat_world = world.reshape(-1, 2)
        safe_world = np.where(np.isfinite(flat_world), flat_world, 0)
        base = np.floor(np.clip(safe_world, -1e6, 1e6)).astype(np.int64)
        rgba = np.zeros((len(flat_world), 4), np.uint8)
        support = np.full(len(flat_world), -1, np.int16)
        fallback = np.zeros(len(flat_world), bool)
        legacy_uv = np.zeros_like(flat_world)
        blend_weights = np.zeros(len(flat_world), np.float32)
        valid = valid.ravel()
        nz, nx = self.grid.shape
        # Nine possible rectangles, in native stable draw order.
        offsets = sorted(((x, z) for x in range(3) for z in range(3)), key=lambda p: (sum(p), p[0]))
        for offset in offsets:
            tile = base + offset
            dxz = safe_world - tile - QUAD_ORIGIN
            u = (dxz[:, 0] - dxz[:, 1]) * 0.5
            v = (dxz[:, 0] + dxz[:, 1]) * 0.5
            in_quad = valid & (u >= 0) & (v >= 0) & (u < 1) & (v < 1)
            in_quad &= (tile[:, 0] >= 0) & (tile[:, 0] < nx) & (tile[:, 1] >= 0) & (tile[:, 1] < nz)
            indices = np.flatnonzero(in_quad)
            if not len(indices):
                continue
            key_ids = self.grid[tile[indices, 1], tile[indices, 0]]
            exists = key_ids >= 0
            indices, key_ids = indices[exists], key_ids[exists]
            px = np.minimum((u[indices] * 64).astype(np.int32), 63)
            py = np.minimum((v[indices] * 32).astype(np.int32), 31)
            native = self.rgba[key_ids, py, px]
            opaque = native[:, 3] != 0
            indices, key_ids, px, py, native = indices[opaque], key_ids[opaque], px[opaque], py[opaque], native[opaque]
            rgba[indices] = native
            legacy_uv[indices] = np.stack((u[indices], v[indices]), axis=-1)
            support[indices] = self.mask_ids[key_ids, py, px]
            blend_weights[indices] = self.mask_weights[key_ids, py, px]
            for key_id, material_id, mask, occurrences, weights in self.occurrence_masks:
                possible = np.flatnonzero((key_ids == key_id) & mask[py, px])
                selected = [p for p in possible if tuple(tile[indices[p]]) in occurrences]
                if selected:
                    support[indices[selected]] = material_id
                    blend_weights[indices[selected]] = weights[py[selected], px[selected]]
            fallback[indices] = key_ids == self.key_ids.get("fallback", -2)
        if np.any(fallback & (support >= 0)):
            raise AssertionError("Protected fallback received support")
        return (rgba.reshape(*shape, 4), support.reshape(shape), fallback.reshape(shape),
                legacy_uv.reshape(*shape, 2), blend_weights.reshape(shape))


def render_view(ground: Ground, view: dict[str, Any], output: Path, width: int, height: int,
                samplers: list[str], max_anisotropy: int, uv_override: str | None = None) -> dict[str, Any]:
    name = view["name"]
    if not re.fullmatch(r"[a-zA-Z0-9_-]{1,64}", name):
        raise ValueError("View name must be a short filename-safe identifier")
    world, valid = camera_ground(view, width, height)
    world_dx, valid_dx = camera_ground(view, width, height, shift_x=1)
    world_dy, valid_dy = camera_ground(view, width, height, shift_y=1)
    derivatives = np.stack((world_dx - world, world_dy - world), axis=-1)
    derivatives = np.nan_to_num(derivatives, nan=0, posinf=0, neginf=0)
    native, support, fallback, legacy_uv, blend_weights = ground.lookup(world, valid)
    Image.fromarray(native).save(output / f"{name}-native.png")
    support_image = np.zeros_like(native)
    colors = [(79, 180, 93), (196, 129, 68), (204, 190, 91), (132, 93, 67), (170, 175, 185), (129, 151, 163)]
    for index in range(len(ground.materials)):
        support_image[support == index] = (*colors[index % len(colors)], 255)
    support_image[fallback] = (190, 65, 170, 255)
    Image.fromarray(support_image).save(output / f"{name}-support.png")
    selected = support >= 0
    opaque = native[..., 3] != 0
    if np.any(selected & ~opaque):
        raise AssertionError("Support escaped native opaque coverage")
    native_jacobian = np.einsum("ij,...jk->...ik", np.array([[32, -32], [16, 16]]), derivatives)
    native_major, native_minor, _ = footprint(native_jacobian[opaque])
    report: dict[str, Any] = {"camera": view, "size": [width, height], "opaquePixels": int(opaque.sum()),
                              "supportedPixels": int(selected.sum()), "protectedFallbackPixels": int(fallback.sum()),
                              "unsupportedOpaquePixels": int(np.count_nonzero(opaque & ~selected)),
                              "transparentPixels": int(np.count_nonzero(~opaque)),
                              "comparisonUvMode": uv_override or "configured-per-material",
                              "inwardBlendWeightsInsideSupport": quantiles(blend_weights[selected]),
                              "supportFractionOfOpaque": float(selected.sum() / max(opaque.sum(), 1)),
                              "nativeTexelsPerPixelMajor": quantiles(native_major),
                              "nativeTexelsPerPixelMinor": quantiles(native_minor), "candidates": {}}
    for sampler in samplers:
        candidate = native.copy()
        material_statistics = {}
        for index, material in enumerate(ground.materials):
            mask = support == index
            if not mask.any():
                continue
            linear_rgb, statistics = sample_material(material, world[mask], derivatives[mask], sampler, max_anisotropy,
                                                     legacy_uv[mask], uv_override)
            weights = blend_weights[mask, None]
            blended = srgb_decode(native[mask, :3]) * (1 - weights) + linear_rgb * weights
            candidate[mask, :3] = srgb_encode(blended)
            material_statistics[material["name"]] = statistics
        changed = np.any(candidate != native, axis=-1)
        outside_changed = int(np.count_nonzero(changed & ~selected))
        unsupported_opaque_changed = int(np.count_nonzero(changed & ~selected & opaque))
        fallback_changed = int(np.count_nonzero(changed & fallback))
        alpha_changed = int(np.count_nonzero(candidate[..., 3] != native[..., 3]))
        coverage_changed = int(np.count_nonzero((candidate[..., 3] != 0) != opaque))
        if outside_changed or fallback_changed or alpha_changed or coverage_changed:
            raise AssertionError("Candidate escaped explicit support/coverage")
        filename = f"{name}-{sampler}.png"
        Image.fromarray(candidate).save(output / filename)
        save_comparison(output / f"{name}-{sampler}-comparison.png", native, candidate, sampler)
        changed_rgb = candidate[selected, :3].astype(np.float32) - native[selected, :3].astype(np.float32)
        report["candidates"][sampler] = {
            "image": filename, "changedPixels": int(changed.sum()), "outsideSupportChangedPixels": outside_changed,
            "unsupportedOpaqueChangedPixels": unsupported_opaque_changed, "coverageChangedPixels": coverage_changed,
            "protectedFallbackChangedPixels": fallback_changed, "alphaChangedPixels": alpha_changed,
            "meanAbsoluteRgbDifferenceInsideSupport": float(np.abs(changed_rgb).mean()) if len(changed_rgb) else 0,
            "materials": material_statistics}
    return report


def save_comparison(path: Path, native: np.ndarray, candidate: np.ndarray, sampler: str) -> None:
    h, w = native.shape[:2]
    canvas = Image.new("RGB", (w * 2, h + 42), (20, 20, 22))
    for x, array in ((0, native), (w, candidate)):
        image = Image.fromarray(array)
        canvas.paste(image, (x, 42), image)
    draw = ImageDraw.Draw(canvas)
    draw.text((16, 14), "NATIVE EXPORTED FLOOR | same camera", fill=(230, 230, 230))
    draw.text((w + 16, 14), f"CANDIDATE | {sampler} | offline, unlit", fill=(230, 230, 230))
    canvas.save(path)


def write_viewer(output: Path, reports: dict[str, Any], samplers: list[str]) -> None:
    sections = []
    for name, report in reports.items():
        for sampler in samplers:
            image = f"{name}-{sampler}.png"
            sections.append(f'''<section><h2>{html.escape(name)} / {sampler}</h2>
<div class="compare"><img src="{name}-native.png" alt="Original exported floor">
<div class="over"><img src="{image}" alt="Masked candidate"></div><span class="line"></span></div>
<label>Original <input type="range" min="0" max="100" value="50" aria-label="Comparison split"> Candidate</label>
<p>{report['supportedPixels']:,} supported pixels; {report['protectedFallbackPixels']:,} protected fallback pixels.</p></section>''')
    output.joinpath("index.html").write_text('''<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Private ground-material comparison</title><style>body{background:#17191c;color:#e1e3e6;font:16px system-ui;margin:24px auto;max-width:1200px;padding:0 16px}h1{font-size:24px}h2{font-size:18px}.compare{position:relative;background:#101014;width:100%}.compare>img{display:block;width:100%}.over{position:absolute;inset:0;clip-path:inset(0 0 0 50%)}.over img{width:100%;height:100%}.line{position:absolute;top:0;bottom:0;left:50%;width:2px;background:#eee}input{width:60%;vertical-align:middle}p{color:#afb4bb}section{margin:32px 0}code{background:#282c32;padding:2px 5px}</style>
<h1>Private ground-material study</h1><p>Offline exported floor, unlit RGB. Binary support and native coverage remain fixed. Original left, candidate right; move the slider to shift their dividing line. No engine lighting, palette, performance, collision or artistic acceptance is established.</p>
<p>Editor exports use generic grass under blocked/solid/replaced scenery. Fallback is protected. River or unsupported pixels retain the reference. See <a href="comparison-report.json">full provenance and limits</a>.</p>'''
        + "\n".join(sections) + '''<script>document.querySelectorAll('section').forEach(s=>{s.querySelector('input').oninput=e=>{let v=e.target.value;s.querySelector('.over').style.clipPath=`inset(0 0 0 ${v}%)`;s.querySelector('.line').style.left=v+'%'}})</script>''', encoding="utf-8")


def self_test(output: Path) -> dict[str, Any]:
    """Synthetic invariants for projection, linear mips, and mask confinement."""
    output.mkdir(parents=True, exist_ok=True)
    texture = np.zeros((32, 64, 4), np.uint8)
    texture[5:27, 8:56] = (45, 60, 35, 255)
    Image.fromarray(texture).save(output / "native.png")
    mask = np.zeros((32, 64), np.uint8)
    mask[10:22, 20:44] = 255
    Image.fromarray(mask).save(output / "mask.png")
    master = np.full((16, 16, 3), [130, 80, 40], np.uint8)
    Image.fromarray(master).save(output / "master.png")
    ref = {"source": "synthetic", "textures": [{"key": "p1", "path": "native.png"},
             {"key": "fallback", "path": "native.png"}], "tiles": [[5, 5, "p1"], [6, 5, "fallback"]]}
    config = {"schemaVersion": 1, "materials": {"grass": {"path": "master.png", "worldPeriod": [2, 2]}},
              "pieceMasks": [{"key": "p1", "material": "grass", "path": "mask.png", "blendWidthPixels": 2}],
              "views": [{"name": "fixture", "mode": "orthographic", "target": [5, 0, 5], "verticalSpan": 4}]}
    write_json(output / "reference.json", ref)
    write_json(output / "config.json", config)
    ground = Ground(output / "reference.json", output / "config.json", config)
    u, v = 0.4, 0.6
    point = np.array([[[5 - 1.46875 + u + v, 5 - 0.46875 - u + v]]])
    native, support, _, local_uv, blend_weight = ground.lookup(point, np.array([[True]]))
    assert native[0, 0].tolist() == [45, 60, 35, 255] and support[0, 0] == 0
    assert np.allclose(local_uv[0, 0], [u, v]) and blend_weight[0, 0] == 1
    checker = np.zeros((2, 2, 3), np.uint8)
    checker[0, 0] = checker[1, 1] = 255
    mip = srgb_encode(build_mips(checker)[1])[0, 0, 0]
    assert 187 <= mip <= 188, "Mip average must be linear-light, not sRGB 128"
    report = render_view(ground, config["views"][0], output, 240, 160, sorted(SAMPLERS), 8)
    assert report["supportedPixels"] > 0 and report["protectedFallbackPixels"] > 0
    checks = ["DrawGround inverse projection", "linear-light mip average", "same coverage for all samplers",
              "unsupported pixels preserved", "fallback protected"]
    odd = np.zeros((3, 627, 3), np.uint8)
    odd_mips = build_mips(odd)
    assert odd_mips[1].shape == (1, 313, 3) and all(level.dtype == np.float32 for level in odd_mips)
    checks.append("odd dimensions 627 to 313 and 3 to 1, float working chain")
    three = np.zeros((3, 3, 3), np.uint8)
    three[:, 1] = 255
    three_mips = build_mips(three)
    assert three_mips[1].shape == (1, 1, 3) and np.allclose(three_mips[1][0, 0], [1 / 3] * 3)
    checks.append("3 by 3 to 1 by 1 area average in linear RGB")
    feather = inward_weights(mask != 0, 2)
    assert feather[10, 20] == 0.25 and feather[11, 21] == 0.75 and feather[12, 22] == 1
    assert np.all(feather[mask == 0] == 0)
    checks.append("inward feather rings and zero support conservation")
    material = dict(ground.materials[0], linearGain=0.25)
    linear_rgb, _ = sample_material(material, point.reshape(-1, 2), np.zeros((1, 2, 2)), "nearest", 8)
    assert np.allclose(linear_rgb[0], srgb_decode(master)[0, 0] * 0.25)
    checks.append("linear gain applied before encoding")
    stripe = np.zeros((16, 16, 3), np.uint8)
    stripe[..., 0] = np.arange(16, dtype=np.uint8)[None, :] * 15
    material.update(rgb=stripe, mips=build_mips(stripe), linearGain=1)
    two_points = np.concatenate((point.reshape(-1, 2), point.reshape(-1, 2) + [1, 0]))
    two_uv = np.array([[u, v], [u, v]])
    world_rgb, _ = sample_material(material, two_points, np.zeros((2, 2, 2)), "nearest", 8,
                                   two_uv, "world")
    tile_rgb, _ = sample_material(material, two_points, np.zeros((2, 2, 2)), "nearest", 8,
                                  two_uv, "legacy-per-tile")
    assert not np.array_equal(world_rgb[0], world_rgb[1]) and np.array_equal(tile_rgb[0], tile_rgb[1])
    checks.append("world continuity differs from per-tile UV reset")
    tile_report = render_view(ground, dict(config["views"][0], name="fixture-tile"), output, 240, 160,
                              sorted(SAMPLERS), 8, "legacy-per-tile")
    assert all(item["outsideSupportChangedPixels"] == item["alphaChangedPixels"] == item["protectedFallbackChangedPixels"] == 0
               for item in tile_report["candidates"].values())
    checks.append("per-tile mapping with feather preserves all unsupported RGB and alpha")
    bad_config = json.loads(json.dumps(config))
    bad_config["pieceMasks"][0]["key"] = "fallback"
    try:
        Ground(output / "reference.json", output / "config.json", bad_config)
    except ValueError:
        checks.append("fallback annotations rejected")
    else:
        raise AssertionError("Fallback annotation was accepted")
    bad_mask = mask.copy()
    bad_mask[0, 0] = 255
    Image.fromarray(bad_mask).save(output / "bad-mask.png")
    bad_config["pieceMasks"][0].update(key="p1", path="bad-mask.png")
    try:
        Ground(output / "reference.json", output / "config.json", bad_config)
    except ValueError:
        checks.append("mask outside native alpha rejected")
    else:
        raise AssertionError("Escaping mask was accepted")
    result = {"passed": len(checks), "checks": checks, "fixture": report}
    write_json(output / "self-test.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--reference", type=Path, help="native ground-reference.json, paths relative to its directory")
    parser.add_argument("--config", type=Path, help="explicit audited masks + masters + cameras JSON")
    parser.add_argument("--output", required=True, type=Path, help="private ignored output directory")
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=600)
    parser.add_argument("--samplers", default="nearest,anisotropic", help="nearest,trilinear,anisotropic")
    parser.add_argument("--max-anisotropy", type=int, default=8)
    parser.add_argument("--uv-modes", help="configured,world,legacy-per-tile; overrides config comparisonUvModes")
    parser.add_argument("--export-mips", action="store_true", help="write diagnostic mip PNGs; master stays untouched")
    parser.add_argument("--self-test", action="store_true", help="run only synthetic contract checks")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.self_test:
        result = self_test(output)
        print(f"{result['passed']} synthetic checks passed; {output / 'self-test.json'}")
        return 0
    if args.reference is None or args.config is None:
        parser.error("--reference and --config are required unless --self-test")
    if not 64 <= args.width <= 8192 or not 64 <= args.height <= 8192 or args.width * args.height > 8_000_000:
        parser.error("Output dimensions must be 64..8192, at most 8 million pixels")
    if not 1 <= args.max_anisotropy <= 16:
        parser.error("--max-anisotropy must lie between 1 and 16")
    samplers = list(dict.fromkeys(args.samplers.split(",")))
    if not samplers or not set(samplers) <= SAMPLERS:
        parser.error("Unknown sampler")
    config_path, reference_path = args.config.resolve(), args.reference.resolve()
    config = read_json(config_path)
    if config.get("schemaVersion") != 1 or config.get("excludeFallback", True) is not True:
        raise ValueError("Config requires schemaVersion 1; fallback exclusion cannot be disabled")
    ground = Ground(reference_path, config_path, config)
    uv_modes = args.uv_modes.split(",") if args.uv_modes else config.get("comparisonUvModes", ["configured"])
    uv_modes = list(dict.fromkeys(uv_modes))
    if not uv_modes or not set(uv_modes) <= UV_MODES | {"configured"}:
        raise ValueError("comparisonUvModes/--uv-modes requires configured, world or legacy-per-tile")
    reports = {}
    for view in config.get("views", []):
        for uv_mode in uv_modes:
            variant = dict(view)
            if uv_modes != ["configured"]:
                variant["name"] = f"{view['name']}-{uv_mode}"
            if variant["name"] in reports:
                raise ValueError("Duplicate view name")
            reports[variant["name"]] = render_view(ground, variant, output, args.width, args.height, samplers,
                                                   args.max_anisotropy, None if uv_mode == "configured" else uv_mode)
    if not reports:
        raise ValueError("At least one explicit camera is required")
    if args.export_mips:
        for material in ground.materials:
            mip_directory = output / "mips" / material["name"]
            mip_directory.mkdir(parents=True, exist_ok=True)
            for index, mip in enumerate(material["mips"]):
                Image.fromarray(srgb_encode(mip)).save(mip_directory / f"level-{index:02d}.png")
    write_viewer(output, reports, samplers)
    outputs = [{"path": file.relative_to(output).as_posix(), "sha256": digest(file)}
               for file in sorted(output.glob("*.png"))] + [{"path": "index.html", "sha256": digest(output / "index.html")}]
    report = {
        "schema": "ground-material-offline-comparison-v1", "createdUtc": datetime.now(timezone.utc).isoformat(),
        "scope": "private offline floor-only basecolor study; no game installation or asset acceptance",
        "toolSha256": digest(Path(__file__)), "referenceSha256": digest(reference_path), "configSha256": digest(config_path),
        "referenceSource": ground.reference_source, "nativeProjection": {
            "source": "Source/engine/render/town_view.cpp DrawGround/DrawQuad/Texture::sample",
            "worldX": "tileX - 1.46875 + u + v", "worldZ": "tileZ - 0.46875 - u + v",
            "nativeSize": [64, 32], "coverage": "native nearest-sampled binary alpha; unchanged",
            "coplanarOverlap": "last opaque in stable tileX+tileZ, then tileX order for exported floor only"},
        "materialSampling": {"uv": "world: (worldXZ-origin)/worldPeriod; legacy-per-tile: full master in native quad u,v",
                             "comparisonUvModes": uv_modes,
                             "uvUnits": "worldPeriod is native tile units, not declared metres; ignored in legacy-per-tile mode",
                             "mips": "sRGB decode once; per-channel Pillow float BOX linear RGB area resize; floor next axes max(1,dimension//2); float working chain; encode only exported PNGs",
                             "nearest": "master texel point sample; fair source-detail baseline",
                             "trilinear": "periodic bilinear samples blended in linear RGB; major-axis LOD",
                             "anisotropic": "offline major-axis line taps of trilinear samples; bounded approximation",
                             "maxAnisotropy": args.max_anisotropy, "normalsRoughness": "absent, not inferred"},
        "limits": ["Not an actual game capture or a GPU sampler/performance validation.",
                   "Editor reference substitutes generic grass under Solid|BlockMissile and replaced scenery; fallback is protected.",
                   "Floor-only export lacks scenery/actors, original SOL flags, map collision, lighting and triangle raster coverage rules.",
                   "Unsupported pixels (including any river/water) are retained verbatim; a material mask is an explicit human classification.",
                   "All images are unlit RGB/sRGB outputs; native art may contain painted light, candidate receives no engine light or palette conversion.",
                   "Optional inward feather blends in linear RGB within binary support only, using bounded Chebyshev distance in native pixels; zero/alpha support is unchanged.",
                   "Material linearGain is PREVIEW-only brightness calibration because native artwork contains lighting; it does not validate engine lighting or alter source masters.",
                   "Legacy-per-tile mapping resets the full master at each native quad; it is an architectural contrast, not the proposed world material mapping.",
                   "More texel detail or a larger image does not establish artistic quality, native map fidelity, engine integration or acceptance."],
        "inputs": ground.input_files, "views": reports, "outputs": outputs}
    write_json(output / "comparison-report.json", report)
    print(f"Rendered {len(reports)} views / {len(samplers)} samplers; {output / 'comparison-report.json'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, KeyError, OSError) as error:
        print(f"ground comparison: {error}", file=sys.stderr)
        raise SystemExit(2)
