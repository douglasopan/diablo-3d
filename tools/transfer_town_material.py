#!/usr/bin/env python3
"""Offline, non-destructive nearest-surface material-transfer experiment.

Requires NumPy and SciPy, and the adjacent inspect_glb.py. This is a candidate
authoring tool, not a runtime importer or an asset approval. The source supplies
UV/materials; the target supplies every position, normal and triangle. No ICP,
anisotropic fit, remesh, generation API or network access is performed.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import time

import numpy as np
from scipy.spatial import cKDTree

import inspect_glb as glb


VERSION = "nearest-surface-barycentric-v1"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def unit(values: np.ndarray) -> np.ndarray:
    length = np.linalg.norm(values, axis=-1, keepdims=True)
    return np.divide(values, length, out=np.zeros_like(values), where=length > 1e-30)


def closest_barycentric(points: np.ndarray, triangles: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Exact closest point of paired point/triangle rows, including degeneracy.

    The interior plane projection and all three clamped edge projections are
    compared. Degenerate triangles therefore reduce to their segments/points.
    """
    points = np.asarray(points, dtype=np.float64)
    triangles = np.asarray(triangles, dtype=np.float64)
    a, b, c = triangles[:, 0], triangles[:, 1], triangles[:, 2]
    ab, ac, ap = b - a, c - a, points - a
    d00 = np.einsum("ij,ij->i", ab, ab)
    d01 = np.einsum("ij,ij->i", ab, ac)
    d11 = np.einsum("ij,ij->i", ac, ac)
    d20 = np.einsum("ij,ij->i", ap, ab)
    d21 = np.einsum("ij,ij->i", ap, ac)
    denominator = d00 * d11 - d01 * d01
    valid = denominator > np.maximum(d00 * d11, 1e-300) * 1e-14
    v = np.divide(d11 * d20 - d01 * d21, denominator,
                  out=np.zeros_like(denominator), where=valid)
    w = np.divide(d00 * d21 - d01 * d20, denominator,
                  out=np.zeros_like(denominator), where=valid)
    weights = np.column_stack([1 - v - w, v, w])
    interior = valid & np.all(weights >= 0, axis=1)
    closest = np.einsum("ij,ijk->ik", weights, triangles)
    squared = np.where(interior, np.einsum("ij,ij->i", closest - points, closest - points), np.inf)
    for start, end in ((0, 1), (1, 2), (2, 0)):
        edge = triangles[:, end] - triangles[:, start]
        relative = points - triangles[:, start]
        edge_length = np.einsum("ij,ij->i", edge, edge)
        fraction = np.clip(np.divide(np.einsum("ij,ij->i", relative, edge), edge_length,
                                    out=np.zeros_like(edge_length), where=edge_length > 0), 0, 1)
        projected = triangles[:, start] + fraction[:, None] * edge
        delta = projected - points
        candidate = np.einsum("ij,ij->i", delta, delta)
        better = candidate < squared
        weights[better] = 0
        weights[better, start] = 1 - fraction[better]
        weights[better, end] = fraction[better]
        squared[better] = candidate[better]
    return np.sqrt(np.maximum(squared, 0)), weights


class Surface:
    """Exact closest-surface query accelerated by radius-binned centroid trees.

    Every triangle lies inside a sphere around its centroid. A triangle closer
    than the current upper bound must have centroid distance <= bound+radius.
    Separate radius bins avoid a large floor triangle expanding every query.
    This is not a nearest-centroid heuristic: all such candidates are evaluated.
    """
    def __init__(self, positions: np.ndarray, faces: np.ndarray, normals: np.ndarray | None = None):
        self.faces = np.asarray(faces, dtype=np.int64)
        self.triangles = np.asarray(positions, dtype=np.float64)[self.faces]
        self.centers = self.triangles.mean(axis=1)
        radius = np.linalg.norm(self.triangles - self.centers[:, None], axis=2).max(axis=1)
        self.diagonal = float(np.linalg.norm(np.ptp(np.asarray(positions), axis=0)))
        self.normals = None if normals is None else np.asarray(normals, dtype=np.float64)[self.faces]
        self.tree = cKDTree(self.centers)
        self.bins = []
        codes = np.floor(np.log2(np.maximum(radius, self.diagonal * 1e-12))).astype(int)
        for code in np.unique(codes):
            ids = np.flatnonzero(codes == code)
            self.bins.append((ids, cKDTree(self.centers[ids]), float(radius[ids].max())))

    def nearest(self, points: np.ndarray, batch_size: int = 2048) -> dict[str, np.ndarray]:
        points = np.asarray(points, dtype=np.float64)
        count = len(points)
        all_ids = np.empty(count, dtype=np.int32)
        all_distance = np.empty(count, dtype=np.float64)
        all_bary = np.empty((count, 3), dtype=np.float64)
        candidates_total = 0
        # Smaller initial upper bound from eight nearby centroids; exactness
        # comes from the sphere-bound enumeration below, not from this seed.
        for start in range(0, count, batch_size):
            block = points[start:start + batch_size]
            k = min(8, len(self.faces))
            _, seed = self.tree.query(block, k=k)
            seed = np.asarray(seed).reshape(len(block), k)
            seed_distance, seed_bary = closest_barycentric(
                np.repeat(block, k, axis=0), self.triangles[seed.ravel()])
            seed_distance = seed_distance.reshape(len(block), k)
            chosen = seed_distance.argmin(axis=1)
            row = np.arange(len(block))
            best = seed_distance[row, chosen].copy()
            ids = seed[row, chosen].copy()
            bary = seed_bary.reshape(len(block), k, 3)[row, chosen].copy()
            for bin_ids, tree, radius in self.bins:
                lists = tree.query_ball_point(block, best + radius + self.diagonal * 1e-12)
                sizes = np.fromiter((len(items) for items in lists), dtype=np.int64, count=len(block))
                if not sizes.any():
                    continue
                rows = np.repeat(row, sizes)
                candidate_ids = bin_ids[np.concatenate(lists).astype(np.int64)]
                distances, candidate_bary = closest_barycentric(block[rows], self.triangles[candidate_ids])
                candidates_total += len(rows)
                # query_ball_point returns contiguous rows. Stable lexsort gives
                # deterministic triangle IDs on an exact distance tie.
                order = np.lexsort((candidate_ids, distances, rows))
                ordered_rows = rows[order]
                first = np.r_[True, ordered_rows[1:] != ordered_rows[:-1]]
                winners = order[first]
                r = rows[winners]
                better = ((distances[winners] < best[r]) |
                          ((distances[winners] == best[r]) & (candidate_ids[winners] < ids[r])))
                r, winners = r[better], winners[better]
                best[r], ids[r], bary[r] = distances[winners], candidate_ids[winners], candidate_bary[winners]
            all_ids[start:start + len(block)] = ids
            all_distance[start:start + len(block)] = best
            all_bary[start:start + len(block)] = bary
        result = {"triangle": all_ids, "distance": all_distance, "barycentric": all_bary,
                  "candidatePairs": np.array(candidates_total, dtype=np.int64)}
        if self.normals is not None:
            result["normal"] = unit(np.einsum("ij,ijk->ik", all_bary, self.normals[all_ids]))
        return result


def uv_charts(positions: np.ndarray, faces: np.ndarray, uv: np.ndarray,
              materials: np.ndarray, tolerance: float = 1e-7) -> np.ndarray:
    """Connected UV charts. Shared XYZ edges only join if UV and material agree."""
    scale = max(float(np.linalg.norm(np.ptp(positions, axis=0))), 1e-12)
    quantized = np.rint(np.asarray(positions) / (scale * tolerance)).astype(np.int64)
    tex = np.rint(np.asarray(uv) / tolerance).astype(np.int64)
    parent = np.arange(len(faces), dtype=np.int32)

    def find(value: int) -> int:
        while parent[value] != value:
            parent[value] = parent[parent[value]]
            value = int(parent[value])
        return value

    edges = {}
    for face_id, face in enumerate(faces):
        for i, j in ((0, 1), (1, 2), (2, 0)):
            a, b = int(face[i]), int(face[j])
            left = tuple(quantized[a]) + tuple(tex[a])
            right = tuple(quantized[b]) + tuple(tex[b])
            key = (int(materials[face_id]),) + tuple(sorted((left, right)))
            if key in edges:
                root_a, root_b = find(face_id), find(edges[key])
                parent[root_a] = root_b
            else:
                edges[key] = face_id
    roots = np.array([find(i) for i in range(len(faces))])
    return np.unique(roots, return_inverse=True)[1].astype(np.int32)


def load_static(path: Path, textured: bool) -> tuple[dict, glb.Reader, dict, list]:
    document, binary, _ = glb.read_glb(path)
    if document.get("animations") or document.get("skins"):
        raise ValueError("This experiment accepts static meshes only")
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            if primitive.get("targets"):
                raise ValueError("Morph targets are not silently discarded")
    reader = glb.Reader(path, document, binary)
    arrays, primitives, warnings = glb.flatten(reader)
    if warnings:
        raise ValueError("Unsupported source features: " + "; ".join(warnings))
    if not arrays["normals_present"].all():
        raise ValueError("Both inputs must provide explicit vertex normals")
    # An identity node needs no normal transform or renormalization. Keep its
    # original float values exact (flatten otherwise normalizes all normals).
    for primitive in primitives:
        if np.array_equal(np.asarray(primitive["worldMatrix"]), np.eye(4)):
            attributes = document["meshes"][primitive["mesh"]]["primitives"][primitive["primitive"]]["attributes"]
            lo, hi = primitive["vertexRange"]
            arrays["normals"][lo:hi] = reader.accessor(attributes["NORMAL"])
    if not np.isfinite(arrays["normals"]).all() or np.any(np.linalg.norm(arrays["normals"], axis=1) < 1e-20):
        raise ValueError("Vertex normals must be finite and nonzero")
    if textured:
        if not arrays["uv_present"].all():
            raise ValueError("Material donor must have UVs on every primitive")
        if any(p["texCoordSet"] != 0 for p in primitives):
            raise ValueError("Only TEXCOORD_0 is supported for this transfer")
        if not np.isfinite(arrays["source_uv"]).all():
            raise ValueError("Donor UV coordinates must be finite")
        if not np.array_equal(arrays["colors"], np.ones_like(arrays["colors"])):
            raise ValueError("Vertex-coloured donors require a separate colour-transfer contract")
        for material in document.get("materials", []):
            for info in texture_infos(material):
                if info.get("texCoord", 0) != 0:
                    raise ValueError("Every material texture must use TEXCOORD_0")
        if np.any(arrays["material_ids"] < 0):
            raise ValueError("Material donor has a triangle without a material")
    return document, reader, arrays, primitives


def texture_infos(value):
    if isinstance(value, dict):
        for key, child in value.items():
            if key.endswith("Texture") and isinstance(child, dict) and "index" in child:
                yield child
            yield from texture_infos(child)
    elif isinstance(value, list):
        for child in value:
            yield from texture_infos(child)


def distribution(values: np.ndarray) -> dict:
    values = np.asarray(values)
    return {"count": len(values), "min": float(values.min()), "mean": float(values.mean()),
            "p50": float(np.percentile(values, 50)), "p90": float(np.percentile(values, 90)),
            "p95": float(np.percentile(values, 95)), "p99": float(np.percentile(values, 99)),
            "max": float(values.max())}


def write_glb(path: Path, donor: glb.Reader, positions: np.ndarray, normals: np.ndarray,
              uv: np.ndarray, faces: np.ndarray, materials: np.ndarray, metadata: dict,
              remove_normal_map: bool = False) -> dict:
    """New static GLB; images are copied byte for byte and normals unchanged."""
    document = {"asset": {"version": "2.0", "generator": VERSION},
                "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
                "meshes": [{"primitives": []}], "buffers": [{"byteLength": 0}],
                "bufferViews": [], "accessors": [], "extras": {"materialTransfer": metadata}}
    binary = bytearray()

    def append_data(data: bytes, target: int | None = None) -> int:
        binary.extend(b"\0" * (-len(binary) % 4))
        view = {"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        document["bufferViews"].append(view)
        binary.extend(data)
        return len(document["bufferViews"]) - 1

    def attribute(values: np.ndarray, kind: str, component: int = 5126) -> int:
        dtype = "<f4" if component == 5126 else "<u4"
        view = append_data(np.asarray(values, dtype=dtype).tobytes(), 34963 if component == 5125 else 34962)
        entry = {"bufferView": view, "componentType": component, "count": len(values), "type": kind}
        if kind == "VEC3" and values is positions:
            entry.update({"min": positions.min(axis=0).tolist(), "max": positions.max(axis=0).tolist()})
        document["accessors"].append(entry)
        return len(document["accessors"]) - 1

    attrs = {"POSITION": attribute(positions, "VEC3"), "NORMAL": attribute(normals, "VEC3"),
             "TEXCOORD_0": attribute(uv, "VEC2")}
    for material in np.unique(materials):
        indices = faces[materials == material].reshape(-1)
        document["meshes"][0]["primitives"].append({"attributes": attrs.copy(),
            "indices": attribute(indices, "SCALAR", 5125), "material": int(material), "mode": 4})
    source = donor.document
    for key in ("materials", "textures", "samplers", "extensionsUsed", "extensionsRequired"):
        if key in source:
            document[key] = copy.deepcopy(source[key])
    if remove_normal_map:
        for material in document.get("materials", []):
            material.pop("normalTexture", None)
    images = []
    for original in source.get("images", []):
        data = donor.view(original["bufferView"])[0] if "bufferView" in original else donor.uri_bytes(original["uri"])
        entry = {key: copy.deepcopy(value) for key, value in original.items() if key not in ("bufferView", "uri")}
        if "mimeType" not in entry:
            entry["mimeType"] = "image/png" if data.startswith(b"\x89PNG") else "image/jpeg"
        entry["bufferView"] = append_data(data)
        images.append(entry)
    if images:
        document["images"] = images
    document["buffers"][0]["byteLength"] = len(binary)
    encoded = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    binary.extend(b"\0" * (-len(binary) % 4))
    total = 12 + 8 + len(encoded) + 8 + len(binary)
    with path.open("xb") as stream:
        stream.write(struct.pack("<4sII", b"glTF", 2, total))
        stream.write(struct.pack("<II", len(encoded), glb.JSON_CHUNK))
        stream.write(encoded)
        stream.write(struct.pack("<II", len(binary), glb.BIN_CHUNK))
        stream.write(binary)
    return {"path": str(path.resolve()), "sha256": sha256(path), "bytes": path.stat().st_size,
            "vertices": len(positions), "triangles": len(faces), "normalMapEnabled": not remove_normal_map}


def image_hashes(reader: glb.Reader) -> list[str]:
    return [hashlib.sha256(reader.view(item["bufferView"])[0] if "bufferView" in item else reader.uri_bytes(item["uri"])).hexdigest()
            for item in reader.document.get("images", [])]


def transfer(args) -> dict:
    started = time.monotonic()
    source_path, target_path, output_dir = args.source.resolve(), args.target.resolve(), args.output.resolve()
    if source_path == target_path:
        raise ValueError("Material donor and geometric target must be different files")
    output_dir.mkdir(parents=True, exist_ok=True)
    for name in ("report.json", "mapping.npz", "candidate-pbr.glb", "candidate-no-normal.glb"):
        if (output_dir / name).exists():
            raise ValueError(f"Refusing to replace existing evidence: {name}; choose a new output directory")
    source_hash, target_hash = sha256(source_path), sha256(target_path)
    source_doc, source_reader, source, source_primitives = load_static(source_path, True)
    target_doc, _, target, target_primitives = load_static(target_path, False)
    positions, target_normals = target["world_vertices"], target["normals"]
    target_faces = target["triangles"]
    source_positions, source_faces = source["world_vertices"], source["triangles"]
    diagonal = float(np.linalg.norm(np.ptp(positions, axis=0)))
    print(json.dumps({"phase": "loaded", "sourceTriangles": len(source_faces), "targetTriangles": len(target_faces),
                      "targetVertices": len(positions), "transform": "common glTF world; no independent fit"}), flush=True)
    surface = Surface(source_positions, source_faces, source["normals"])
    charts = uv_charts(source_positions, source_faces, source["source_uv"], source["material_ids"])
    if args.sample_vertices:
        count = min(args.sample_vertices, len(positions))
        selected = np.unique(np.linspace(0, len(positions) - 1, count, dtype=np.int64))
    else:
        selected = np.arange(len(positions))
    mapped = surface.nearest(positions[selected], args.batch_size)
    normal_cosine = np.einsum("ij,ij->i", unit(target_normals[selected]), mapped["normal"])
    angles = np.degrees(np.arccos(np.clip(normal_cosine, -1, 1)))
    uv = np.einsum("ij,ijk->ik", mapped["barycentric"], source["source_uv"][source_faces[mapped["triangle"]]])
    limits = {"distanceFractionOfTargetDiagonal": args.max_distance_fraction,
              "distanceWorld": diagonal * args.max_distance_fraction,
              "normalAngleDegrees": args.max_normal_angle,
              "uvSeamStrategy": "mixed-chart target faces use one donor triangle selected at their centroid"}
    risk_distance = mapped["distance"] > limits["distanceWorld"]
    risk_normal = angles > args.max_normal_angle
    report = {"schema": "town-material-transfer-evidence-v1", "algorithm": VERSION,
              "status": "private experiment; not selected; artistic comparability unproven",
              "source": {"path": str(source_path), "sha256": source_hash, "triangles": len(source_faces),
                         "vertices": len(source_positions), "imageSha256": image_hashes(source_reader)},
              "target": {"path": str(target_path), "sha256": target_hash, "triangles": len(target_faces),
                         "vertices": len(positions)},
              "alignment": {"matrix": np.eye(4).tolist(), "method": "common glTF world coordinates; no ICP, no fit",
                            "sourceBounds": [source_positions.min(axis=0).tolist(), source_positions.max(axis=0).tolist()],
                            "targetBounds": [positions.min(axis=0).tolist(), positions.max(axis=0).tolist()],
                            "targetDiagonal": diagonal},
              "sample": {"vertices": len(selected), "full": len(selected) == len(positions),
                         "selection": "all vertices" if len(selected) == len(positions) else "deterministic equally spaced vertex IDs; not area-weighted"},
              "limits": limits, "distanceWorld": distribution(mapped["distance"]),
              "distanceFractionOfDiagonal": distribution(mapped["distance"] / diagonal),
              "normalAngleDegrees": distribution(angles), "sourceUvCharts": int(charts.max()) + 1,
              "verticesBeyondDistance": int(risk_distance.sum()), "verticesBeyondNormalAngle": int(risk_normal.sum()),
              "verticesBeyondEitherLimit": int((risk_distance | risk_normal).sum()),
              "exactCandidatePairsEvaluated": int(mapped["candidatePairs"]),
              "limitations": ["Proximity is a correspondence hypothesis; it cannot recreate missing semantic texture detail.",
                  "Distance/normal thresholds report risk; they never remove target geometry or imply approval.",
                  "Seams are moved to existing dense triangle edges; no chart-aware clipping or rebake is implemented.",
                  "PBR image bytes are preserved, but donor tangent-space normal maps are not rebaked for the target.",
                  "Interior, back-facing overlaps and tiny props require visual review; vertex percentages are not surface area percentages."]}
    print(json.dumps({"phase": "vertex-projection", "seconds": round(time.monotonic() - started, 2),
                      "distanceP95Fraction": report["distanceFractionOfDiagonal"]["p95"],
                      "riskVertices": report["verticesBeyondEitherLimit"]}), flush=True)
    np.savez_compressed(output_dir / "mapping.npz", targetVertex=selected.astype(np.uint32),
                        donorTriangle=mapped["triangle"], barycentric=mapped["barycentric"].astype(np.float32),
                        uv=uv.astype(np.float32), distance=mapped["distance"].astype(np.float32),
                        normalAngle=angles.astype(np.float32), donorChart=charts[mapped["triangle"]])
    if args.export:
        if len(selected) != len(positions):
            raise ValueError("Sample audits cannot be exported as a full material-transfer candidate")
        out_faces = target_faces.copy()
        vertex_charts = charts[mapped["triangle"]]
        face_charts = vertex_charts[target_faces]
        mixed = np.any(face_charts != face_charts[:, :1], axis=1)
        mixed_ids = np.flatnonzero(mixed)
        # A donor triangle is a single continuous UV domain. Project all three
        # corners of a crossing face into it and split the target vertex records.
        # This prevents an atlas-wide interpolation through unrelated islands;
        # the evidence records the resulting seam displacement and clamping.
        extra_positions = positions[target_faces[mixed]].reshape(-1, 3)
        extra_normals = target_normals[target_faces[mixed]].reshape(-1, 3)
        extra_uv = np.empty((len(extra_positions), 2), dtype=np.float32)
        face_material = source["material_ids"][mapped["triangle"][target_faces[:, 0]]].copy()
        seam_distance = np.empty(len(extra_positions), dtype=np.float32)
        seam_angle = np.empty(len(extra_positions), dtype=np.float32)
        clamped = np.empty(len(extra_positions), dtype=bool)
        for start in range(0, len(mixed_ids), args.batch_size):
            ids = mixed_ids[start:start + args.batch_size]
            donor = surface.nearest(positions[target_faces[ids]].mean(axis=1), args.batch_size)["triangle"]
            corner_points = positions[target_faces[ids]].reshape(-1, 3)
            donor_ids = np.repeat(donor, 3)
            distance, bary = closest_barycentric(corner_points, surface.triangles[donor_ids])
            lo, hi = start * 3, (start + len(ids)) * 3
            extra_uv[lo:hi] = np.einsum("ij,ijk->ik", bary, source["source_uv"][source_faces[donor_ids]])
            source_normals = unit(np.einsum("ij,ijk->ik", bary, surface.normals[donor_ids]))
            seam_angle[lo:hi] = np.degrees(np.arccos(np.clip(np.einsum("ij,ij->i", unit(extra_normals[lo:hi]), source_normals), -1, 1)))
            seam_distance[lo:hi] = distance
            clamped[lo:hi] = np.any(bary <= 1e-10, axis=1)
            face_material[ids] = source["material_ids"][donor]
        out_faces[mixed] = np.arange(len(positions), len(positions) + len(extra_positions), dtype=np.uint32).reshape(-1, 3)
        out_positions = np.concatenate([positions, extra_positions])
        out_normals = np.concatenate([target_normals, extra_normals])
        out_uv = np.concatenate([uv.astype(np.float32), extra_uv])
        metadata = {"algorithm": VERSION, "donorSha256": source_hash, "targetSha256": target_hash,
                    "candidateOnly": True, "geometry": "target world positions/normals and triangles; UV seam vertex duplication only"}
        report["seams"] = {"mixedChartTargetFaces": len(mixed_ids), "targetFaceFraction": len(mixed_ids) / len(target_faces),
                           "duplicatedVertexRecords": len(extra_positions), "clampedCorners": int(clamped.sum()),
                           "projectionDistanceWorld": distribution(seam_distance) if len(seam_distance) else None,
                           "normalAngleDegrees": distribution(seam_angle) if len(seam_angle) else None}
        # Verify without a second complete flatten: every output corner refers
        # exactly to the original target position and normal, in original order.
        for start in range(0, len(target_faces), 100000):
            original = target_faces[start:start + 100000]
            exported = out_faces[start:start + 100000]
            if not (np.array_equal(out_positions[exported], positions[original]) and
                    np.array_equal(out_normals[exported], target_normals[original])):
                raise AssertionError("Material transfer changed target geometry or normals")
        report["preservation"] = {"targetTriangleCountExact": True, "orderedCornerPositionsExact": True,
                                   "orderedCornerNormalsExact": True, "noReduction": True,
                                   "donorMaterialDescriptorsExact": True, "donorEmbeddedImageBytesExact": True,
                                   "sourceFilesUnmodified": sha256(source_path) == source_hash and sha256(target_path) == target_hash}
        report["outputs"] = [write_glb(output_dir / "candidate-pbr.glb", source_reader, out_positions,
                                      out_normals, out_uv, out_faces, face_material, metadata)]
        if args.no_normal_variant:
            report["outputs"].append(write_glb(output_dir / "candidate-no-normal.glb", source_reader, out_positions,
                                            out_normals, out_uv, out_faces, face_material, metadata, True))
        for output in report["outputs"]:
            document, binary, _ = glb.read_glb(Path(output["path"]))
            if image_hashes(glb.Reader(Path(output["path"]), document, binary)) != report["source"]["imageSha256"]:
                raise AssertionError("Image bytes changed during GLB serialization")
    report["elapsedSeconds"] = time.monotonic() - started
    (output_dir / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"phase": "complete", "report": str(output_dir / "report.json"),
                      "seconds": round(report["elapsedSeconds"], 2), "exported": bool(args.export)}), flush=True)
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Textured material donor GLB")
    parser.add_argument("target", type=Path, help="Dense geometric master GLB; never modified")
    parser.add_argument("output", type=Path, help="New private evidence directory; existing evidence is not overwritten")
    parser.add_argument("--sample-vertices", type=int, default=0, help="Audit only N deterministic target vertices; no export")
    parser.add_argument("--export", action="store_true", help="Write full candidate GLB without selecting/installing it")
    parser.add_argument("--no-normal-variant", action="store_true", help="Also write PBR candidate omitting donor normalTexture for comparison")
    parser.add_argument("--max-distance-fraction", type=float, default=0.005, help="Risk distance / target diagonal; default 0.5%%")
    parser.add_argument("--max-normal-angle", type=float, default=60, help="Risk angle in degrees; flags only, does not select another surface")
    parser.add_argument("--batch-size", type=int, default=2048)
    args = parser.parse_args()
    if args.sample_vertices < 0 or args.batch_size <= 0 or not (0 < args.max_distance_fraction <= 1) or not (0 < args.max_normal_angle <= 180):
        parser.error("Invalid sampling, batch size or risk thresholds")
    if args.export and args.sample_vertices:
        parser.error("--export requires all target vertices")
    transfer(args)


if __name__ == "__main__":
    main()
