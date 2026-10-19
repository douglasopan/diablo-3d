#!/usr/bin/env python3
"""Offline GLB -> indexed D3DMESH2, preserving smooth normals and PBR images.

This exports a private review candidate; it does not install or select anything.
No reduction, texture resizing, provider request or deformation is performed.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import struct

import numpy as np
from PIL import Image

import inspect_glb as glb


MAX_BYTES = 512 * 1024 * 1024
OPENINGS = {"native-unchanged": 0, "external-code-required": 1, "baked-reviewed": 2}
ALPHA = {"OPAQUE": 0, "MASK": 1, "BLEND": 2}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def finite(values, count, name):
    result = np.asarray(values, dtype=np.float64)
    require(result.shape == (count,) and np.isfinite(result).all() and np.all(np.abs(result) <= 1_000_000), name + " must be finite/in range")
    return result


def text(value):
    require(isinstance(value, str) and 0 < len(value) <= 256
            and all(32 <= ord(c) <= 126 for c in value), "Identity must be printable ASCII, 1..256 bytes")
    data = value.encode("ascii")
    return struct.pack("<I", len(data)) + data


def info(record, texture_count):
    if not record:
        return struct.pack("<iI5f", -1, 0, 0, 0, 1, 1, 0)
    require(not (set(record.get("extensions", {})) - {"KHR_texture_transform"}), "Unsupported texture-info extension")
    transform = record.get("extensions", {}).get("KHR_texture_transform", {})
    index = record["index"]
    texcoord = transform.get("texCoord", record.get("texCoord", 0))
    require(type(index) is int and 0 <= index < texture_count and texcoord == 0, "Only valid TEXCOORD_0 is supported")
    values = finite([*transform.get("offset", [0, 0]), *transform.get("scale", [1, 1]), transform.get("rotation", 0)], 5, "UV transform")
    return struct.pack("<iI5f", index, texcoord, *values)


def material_bytes(material, texture_count):
    require(not material.get("extensions"), "Material extension requires a new explicit contract")
    pbr = material.get("pbrMetallicRoughness", {})
    base = finite(pbr.get("baseColorFactor", [1, 1, 1, 1]), 4, "Base color")
    emissive = finite(material.get("emissiveFactor", [0, 0, 0]), 3, "Emissive")
    normal = material.get("normalTexture", {})
    occlusion = material.get("occlusionTexture", {})
    factors = finite([pbr.get("metallicFactor", 1), pbr.get("roughnessFactor", 1),
                      material.get("alphaCutoff", .5), normal.get("scale", 1), occlusion.get("strength", 1)], 5, "PBR factors")
    require(np.all((base >= 0) & (base <= 1)) and np.all((emissive >= 0) & (emissive <= 1)), "Color factors outside glTF range")
    require(np.all((factors[[0, 1, 2, 4]] >= 0) & (factors[[0, 1, 2, 4]] <= 1)) and factors[3] >= 0, "Invalid material factors")
    require(material.get("alphaMode", "OPAQUE") in ALPHA, "Unknown alpha mode")
    require(type(material.get("doubleSided", False)) is bool, "doubleSided must be boolean")
    result = struct.pack("<12fII", *base, *emissive, *factors, ALPHA[material.get("alphaMode", "OPAQUE")], material.get("doubleSided", False))
    for record in (pbr.get("baseColorTexture"), pbr.get("metallicRoughnessTexture"), normal, occlusion, material.get("emissiveTexture")):
        result += info(record, texture_count)
    return result


def textures(reader):
    result, audit = [], []
    document = reader.document
    require(len(document.get("textures", [])) <= 64, "Too many textures")
    for texture in document.get("textures", []):
        require(not texture.get("extensions"), "Compressed/extended texture requires explicit decoder")
        image = document.get("images", [])[texture["source"]]
        payload = reader.view(image["bufferView"])[0] if "bufferView" in image else reader.uri_bytes(image["uri"])
        if payload.startswith(b"\x89PNG\r\n\x1a\n"):
            mime = 0
        elif payload.startswith(b"\xff\xd8\xff"):
            mime = 1
        else:
            raise ValueError("Only original PNG/JPEG image payloads are supported")
        with Image.open(io.BytesIO(payload)) as decoded:
            width, height = decoded.size
            decoded.verify()
        require(1 <= width <= 8192 and 1 <= height <= 8192 and len(payload) <= 64 * 1024 * 1024, "Texture exceeds v2 bounds")
        sampler = document.get("samplers", [])[texture["sampler"]] if "sampler" in texture else {}
        wrap_s, wrap_t = sampler.get("wrapS", 10497), sampler.get("wrapT", 10497)
        min_filter, mag_filter = sampler.get("minFilter", 9987), sampler.get("magFilter", 9729)
        require(wrap_s in (33071, 33648, 10497) and wrap_t in (33071, 33648, 10497), "Unknown wrap mode")
        require(min_filter in (9728, 9729, 9984, 9985, 9986, 9987) and mag_filter in (9728, 9729), "Unknown filter")
        digest = hashlib.sha256(payload).digest()
        result.append(struct.pack("<8I", mime, width, height, wrap_s, wrap_t, min_filter, mag_filter, len(payload)) + digest + payload)
        audit.append({"sha256": digest.hex(), "width": width, "height": height, "encodedBytes": len(payload)})
    return result, audit


def vertex_tangents(reader, arrays, primitives):
    """Keep supplied tangent frames; otherwise generate explicit UV tangents.

    Existing UV/normal seams remain split. Generated frames are not claimed to
    match MikkTSpace; this is recorded so consumers/reviewers can decide.
    """
    count = len(arrays["world_vertices"])
    tangents = np.zeros((count, 4), dtype=np.float64)
    all_source = True
    for primitive in primitives:
        start, end = primitive["vertexRange"]
        source = reader.document["meshes"][primitive["mesh"]]["primitives"][primitive["primitive"]]
        attr = source["attributes"]
        normals = arrays["normals"][start:end].astype(np.float64)
        if "TANGENT" in attr:
            values = reader.accessor(attr["TANGENT"]).astype(np.float64)
            require(values.shape == (end - start, 4) and np.isfinite(values).all(), "Invalid source tangents")
            matrix = np.asarray(primitive["worldMatrix"], dtype=np.float64)[:3, :3]
            tangent = values[:, :3] @ matrix.T
            tangent -= normals * np.sum(normals * tangent, axis=1)[:, None]
            length = np.linalg.norm(tangent, axis=1)
            require(np.all(length > 1e-12) and np.all(np.abs(np.abs(values[:, 3]) - 1) < .001), "Invalid tangent frame")
            tangents[start:end, :3] = tangent / length[:, None]
            tangents[start:end, 3] = values[:, 3] * (1 if np.linalg.det(matrix) > 0 else -1)
        else:
            all_source = False
            first, last = primitive["triangleRange"]
            faces = arrays["triangles"][first:last].astype(np.int64) - start
            points = arrays["world_vertices"][start:end].astype(np.float64)
            uv = arrays["source_uv"][start:end].astype(np.float64)
            edge1, edge2 = points[faces[:, 1]] - points[faces[:, 0]], points[faces[:, 2]] - points[faces[:, 0]]
            duv1, duv2 = uv[faces[:, 1]] - uv[faces[:, 0]], uv[faces[:, 2]] - uv[faces[:, 0]]
            det = duv1[:, 0] * duv2[:, 1] - duv1[:, 1] * duv2[:, 0]
            valid = np.abs(det) > 1e-15
            tan = np.zeros_like(edge1)
            bitan = np.zeros_like(edge1)
            tan[valid] = (edge1[valid] * duv2[valid, 1, None] - edge2[valid] * duv1[valid, 1, None]) / det[valid, None]
            bitan[valid] = (edge2[valid] * duv1[valid, 0, None] - edge1[valid] * duv2[valid, 0, None]) / det[valid, None]
            accumulated, accumulated_b = np.zeros((end - start, 3)), np.zeros((end - start, 3))
            for corner in range(3):
                np.add.at(accumulated, faces[:, corner], tan)
                np.add.at(accumulated_b, faces[:, corner], bitan)
            accumulated -= normals * np.sum(normals * accumulated, axis=1)[:, None]
            length = np.linalg.norm(accumulated, axis=1)
            missing = length <= 1e-12
            # Degenerate UVs have no authored tangent direction. A stable frame
            # is usable for base color; the audit marks it for normal-map review.
            axis = np.zeros_like(normals[missing])
            axis[np.arange(len(axis)), np.argmin(np.abs(normals[missing]), axis=1)] = 1
            accumulated[missing] = np.cross(axis, normals[missing])
            accumulated /= np.linalg.norm(accumulated, axis=1)[:, None]
            tangents[start:end, :3] = accumulated
            tangents[start:end, 3] = np.where(np.sum(np.cross(normals, accumulated) * accumulated_b, axis=1) < 0, -1, 1)
    return tangents.astype("<f4"), 1 if all_source else 2


def export(source: Path, output: Path, *, asset_id: str, variant: str, revision: str,
           anchor=(0, 0, 0), openings_pipeline="native", openings_mode="native-unchanged"):
    source, output = source.resolve(), output.resolve()
    require(source != output and not output.exists() and not output.with_suffix(output.suffix + ".json").exists(), "Output and sidecar must be new paths distinct from source")
    source_bytes = source.read_bytes()
    document, binary, _ = glb.read_glb(source)
    require(all("uri" not in b for b in document.get("buffers", [])) and all("bufferView" in image for image in document.get("images", [])),
            "V2 source identity requires self-contained embedded GLB resources")
    require(not (set(document.get("extensionsRequired", [])) - {"KHR_texture_transform"}), "Unsupported required glTF extension")
    require(not document.get("animations") and not document.get("skins"), "Static v2 cannot carry actors/animation")
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            require(primitive.get("mode", 4) == 4 and not primitive.get("targets") and not primitive.get("extensions"), "Only uncompressed static TRIANGLES are supported")
            require(not (set(primitive.get("attributes", {})) - {"POSITION", "NORMAL", "TEXCOORD_0", "TANGENT", "COLOR_0"}), "Unsupported vertex attribute cannot be discarded")
    reader = glb.Reader(source, document, binary)
    arrays, primitives, warnings = glb.flatten(reader)
    require(arrays["uv_present"].all() and arrays["normals_present"].all(), "Drawable v2 requires UVs and smooth normals; keep geometry-only master as inspection source")
    require(not warnings, "Unsupported GLB flattening warning: " + "; ".join(warnings))
    vc, ic = len(arrays["world_vertices"]), arrays["triangles"].size
    require(0 < vc <= 8_000_000 and 0 < ic <= 24_000_000, "Geometry exceeds v2 bounds")
    require(len(primitives) <= 4096 and all(p["texCoordSet"] == 0 for p in primitives), "Primitive/UV-set limit")
    normal_length = np.linalg.norm(arrays["normals"], axis=1)
    require(np.isfinite(normal_length).all() and np.all(np.abs(normal_length - 1) < .001), "Invalid smooth normals")
    positions = arrays["world_vertices"].astype(np.float64)
    faces = arrays["triangles"]
    cross = np.cross(positions[faces[:, 1]] - positions[faces[:, 0]], positions[faces[:, 2]] - positions[faces[:, 0]])
    require(np.all(np.sum(cross * cross, axis=1) > 0), "Degenerate face; do not silently delete geometry")
    require(np.isfinite(arrays["source_uv"]).all() and np.isfinite(arrays["colors"]).all() and np.all(np.abs(arrays["source_uv"]) <= 1_000_000)
            and np.all(np.abs(positions) <= 1_000_000), "Nonfinite/out-of-range vertex attribute")
    require(np.all((arrays["colors"] >= 0) & (arrays["colors"] <= 1)), "Vertex color outside glTF range")
    tangents, tangent_mode = vertex_tangents(reader, arrays, primitives)
    encoded_textures, texture_audit = textures(reader)
    materials = list(document.get("materials", []))
    if any(p["material"] < 0 for p in primitives):
        default_index = len(materials)
        materials.append({})
    else:
        default_index = -1
    require(1 <= len(materials) <= 256, "Material count outside v2 bounds")
    material_data = b"".join(material_bytes(m, len(encoded_textures)) for m in materials)
    require(source.read_bytes() == source_bytes, "Source changed during export")
    source_hash = hashlib.sha256(source_bytes).digest()
    anchor = finite(anchor, 3, "Anchor").astype("<f4")
    require(openings_mode in OPENINGS, "Unknown openings compatibility")
    bounds_min, bounds_max = positions.min(axis=0), positions.max(axis=0)
    packed_vertices = np.column_stack([arrays["world_vertices"], arrays["normals"], arrays["source_uv"], tangents, arrays["colors"]]).astype("<f4")
    require(np.isfinite(packed_vertices).all(), "Float32 conversion overflow")
    header = b"D3DMESH2" + struct.pack("<6I9fI", tangent_mode, vc, ic, len(primitives), len(materials), len(encoded_textures), *anchor, *bounds_min, *bounds_max, OPENINGS[openings_mode])
    header += b"".join(text(s) for s in (asset_id, variant, revision, openings_pipeline)) + source_hash
    primitive_data = b"".join(struct.pack("<3I", p["triangleRange"][0] * 3, (p["triangleRange"][1] - p["triangleRange"][0]) * 3,
                                        p["material"] if p["material"] >= 0 else default_index) for p in primitives)
    data = header + packed_vertices.tobytes() + faces.astype("<u4").tobytes() + primitive_data + material_data + b"".join(encoded_textures)
    require(len(data) <= MAX_BYTES, "Packet exceeds 512 MiB review limit")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation ensures a second run cannot overwrite another revision.
    with output.open("xb") as destination:
        destination.write(data)
    audit = {"schemaVersion": 2, "assetId": asset_id, "variant": variant, "revision": revision,
             "sourceSha256": source_hash.hex(), "runtimeSha256": hashlib.sha256(data).hexdigest(),
             "vertices": vc, "triangles": ic // 3, "primitives": len(primitives), "materials": len(materials),
             "textures": texture_audit, "encodedBytes": len(data), "anchor": anchor.tolist(),
             "boundsMin": bounds_min.tolist(), "boundsMax": bounds_max.tolist(),
             "openings": {"pipelineId": openings_pipeline, "compatibility": openings_mode},
             "tangentMode": "source" if tangent_mode == 1 else "generated-uv-not-mikktspace",
             "geometryReduced": False, "textureResized": False, "approvedForGame": False,
             "limitations": "Transport only; current game does not load v2. Generated tangents need normal-map review."}
    with output.with_suffix(output.suffix + ".json").open("x", encoding="utf-8") as destination:
        destination.write(json.dumps(audit, indent=2) + "\n")
    return audit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--asset-id", required=True)
    parser.add_argument("--variant", required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--anchor", nargs=3, type=float, default=[0, 0, 0])
    parser.add_argument("--openings-pipeline", default="native")
    parser.add_argument("--openings-mode", choices=OPENINGS, default="native-unchanged")
    args = parser.parse_args()
    result = export(args.source, args.output, asset_id=args.asset_id, variant=args.variant, revision=args.revision,
                    anchor=args.anchor, openings_pipeline=args.openings_pipeline, openings_mode=args.openings_mode)
    print(json.dumps(result, ensure_ascii=True))


if __name__ == "__main__":
    main()
