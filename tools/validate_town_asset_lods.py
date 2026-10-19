#!/usr/bin/env python3
"""Validate a PRIVATE v2 master/LOD review manifest without selecting assets.

The optional standalone C++ parser checks each D3DMESH2 packet. Geometric error
is never inferred from triangle counts; unverified derivatives stay quarantined.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess

import inspect_glb as glb
from export_town_model_v2 import OPENINGS


def require(ok, message):
    if not ok:
        raise ValueError(message)


def valid_hash(value):
    return isinstance(value, str) and re.fullmatch("[0-9a-f]{64}", value) is not None


def checked_path(parent, value, digest):
    require(isinstance(value, str) and value and valid_hash(digest), "Missing path/exact SHA-256")
    require("://" not in value, "Manifest is offline; URLs are unsupported")
    path = (parent / value).resolve()
    require(path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == digest, "File missing or SHA-256 mismatch: " + path.name)
    return path


def packet_identity(path):
    data = path.read_bytes()
    require(data[:8] == b"D3DMESH2" and 120 <= len(data) <= 512 * 1024 * 1024, "Invalid v2 envelope")
    tangent, vc, ic, pc, mc, tc, *rest = struct.unpack_from("<6I9fI", data, 8)
    offset = 72
    strings = []
    for _ in range(4):
        require(offset + 4 <= len(data), "Truncated v2 identity")
        length, = struct.unpack_from("<I", data, offset)
        offset += 4
        require(1 <= length <= 256 and offset + length <= len(data), "Invalid v2 identity length")
        strings.append(data[offset:offset + length].decode("ascii"))
        offset += length
    require(offset + 32 <= len(data), "Truncated v2 source hash")
    return {"assetId": strings[0], "variant": strings[1], "revision": strings[2], "pipelineId": strings[3],
            "sourceSha256": data[offset:offset + 32].hex(), "anchor": rest[:3], "openingsMode": rest[-1],
            "vertices": vc, "triangles": ic // 3, "materials": mc, "textures": tc, "tangentMode": tangent}


def validate(manifest_path: Path, runtime_validator: Path | None = None):
    manifest_path = manifest_path.resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    require(manifest.get("schemaVersion") == 2, "Expected manifest schemaVersion=2")
    for key in ("assetId", "revision", "variant"):
        require(isinstance(manifest.get(key), str) and manifest[key], "Missing " + key)
    anchor = manifest.get("anchor", {})
    position = anchor.get("position")
    require(anchor.get("upAxis") == "Y" and isinstance(position, list) and len(position) == 3
            and all(isinstance(v, (float, int)) and not isinstance(v, bool) and math.isfinite(v) for v in position), "Invalid common object anchor")
    require(all(abs(v) <= 1_000_000 for v in position), "Common anchor outside v2 range")
    canonical_position = list(struct.unpack("<3f", struct.pack("<3f", *position)))
    openings = manifest.get("openings", {})
    require(isinstance(openings.get("pipelineId"), str) and openings["pipelineId"]
            and openings.get("compatibility") in OPENINGS, "Missing openings pipeline/compatibility")
    if "sha256" in openings:
        require(valid_hash(openings["sha256"]), "Invalid openings pipeline hash")
    fit = manifest.get("fit", {})
    require(set(fit) <= {"uniformScale", "yawRadians", "translation"}, "Only uniform scale/yaw/translation may fit the object")
    require(isinstance(fit.get("uniformScale", 1), (float, int)) and math.isfinite(fit.get("uniformScale", 1)) and fit.get("uniformScale", 1) > 0,
            "Fit uniformScale must be positive and finite")
    if "yawRadians" in fit:
        require(isinstance(fit["yawRadians"], (float, int)) and math.isfinite(fit["yawRadians"]), "Invalid yaw")
    if "translation" in fit:
        require(isinstance(fit["translation"], list) and len(fit["translation"]) == 3 and all(isinstance(x, (int, float)) and math.isfinite(x) for x in fit["translation"]), "Invalid fit translation")
    sources = manifest.get("sources")
    require(isinstance(sources, list) and 1 <= len(sources) <= 32, "Manifest sources missing/too many")
    require(any(isinstance(source, dict) and source.get("role") == "master-geometry" for source in sources), "At least one preserved inspection master is required")
    records, drawable = [], []
    for source in sources:
        require(isinstance(source, dict), "Each source must be an object")
        role = source.get("role")
        require(role in ("master-geometry", "drawable-lod"), "Unknown source role")
        path = checked_path(manifest_path.parent, source.get("path"), source.get("sha256"))
        document, binary, _ = glb.read_glb(path)
        require(all("uri" not in b for b in document.get("buffers", [])) and all("bufferView" in image for image in document.get("images", [])),
                "Review source must be self-contained for exact hash identity")
        reader = glb.Reader(path, document, binary)
        arrays, primitives, warnings = glb.flatten(reader)
        stats = {"role": role, "sha256": source["sha256"], "vertices": len(arrays["world_vertices"]),
                 "triangles": len(arrays["triangles"]), "hasUv": bool(arrays["uv_present"].all()),
                 "hasNormals": bool(arrays["normals_present"].all()), "runtimeStructuralValidation": "not-run"}
        if role == "drawable-lod":
            require(stats["hasUv"] and stats["hasNormals"] and not warnings, "Drawable LOD lacks required UV/normal/static data")
            index = source.get("lodIndex")
            error = source.get("geometricError")
            require(type(index) is int and index >= 0 and isinstance(error, (int, float)) and not isinstance(error, bool) and math.isfinite(error) and error >= 0,
                    "LOD index/error invalid")
            require(type(source.get("errorVerified")) is bool, "errorVerified must be explicit")
            maximum = source.get("maxDiameterPixels", 0)
            require(isinstance(maximum, (int, float)) and math.isfinite(maximum) and maximum >= 0, "Invalid projected diameter threshold")
            if index > 0:
                require(maximum > 0 and type(source.get("openingEquivalenceVerified")) is bool, "Coarse LOD needs diameter and opening-equivalence result")
            stats.update({"lodIndex": index, "geometricError": error, "automaticEligible": source["errorVerified"] and (index == 0 or source["openingEquivalenceVerified"])})
            drawable.append((index, error, source))
        else:
            require("runtimePath" not in source and "runtimeSha256" not in source, "Geometry-only master must not be exported as drawable")
            stats["automaticEligible"] = False
        if "runtimePath" in source or "runtimeSha256" in source:
            packet = checked_path(manifest_path.parent, source.get("runtimePath"), source.get("runtimeSha256"))
            identity = packet_identity(packet)
            for key in ("assetId", "variant", "revision"):
                require(identity[key] == manifest[key], "Runtime packet has incompatible " + key)
            require(identity["sourceSha256"] == source["sha256"] and identity["anchor"] == canonical_position
                    and identity["pipelineId"] == openings["pipelineId"] and identity["openingsMode"] == OPENINGS[openings["compatibility"]],
                    "Runtime packet source/anchor/openings incompatible")
            if runtime_validator is not None:
                result = subprocess.run([str(runtime_validator.resolve()), str(packet), source["runtimeSha256"]], capture_output=True, text=True, check=False)
                require(result.returncode == 0, "C++ packet validation failed: " + result.stderr.strip())
                stats["runtimeStructuralValidation"] = "passed-cpp"
        records.append(stats)
    drawable.sort(key=lambda entry: entry[0])
    require(drawable and [entry[0] for entry in drawable] == list(range(len(drawable))), "Drawable LOD indices must be contiguous from 0")
    require(drawable[0][1] == 0 and drawable[0][2]["errorVerified"], "Finest drawable must be its own verified zero-error reference")
    require(manifest.get("geometricReferenceSha256") == drawable[0][2]["sha256"], "LOD geometric reference must identify drawable LOD0, not untextured master")
    require(all(drawable[i][1] >= drawable[i - 1][1] for i in range(1, len(drawable))), "LOD errors must be nondecreasing")
    require(all(drawable[i][2]["maxDiameterPixels"] <= drawable[i - 1][2]["maxDiameterPixels"] for i in range(2, len(drawable))),
            "Coarse projected diameter limits must be nonincreasing")
    return {"schemaVersion": 2, "assetId": manifest["assetId"], "revision": manifest["revision"],
            "manifestSha256": hashlib.sha256(manifest_path.read_bytes()).hexdigest(), "sources": records,
            "approvedForGame": False, "selectionChanged": False,
            "scope": "Identity/structure validation only; no silhouette, opening or material appearance approval"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--runtime-validator", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = validate(args.manifest, args.runtime_validator)
    if args.report:
        require(not args.report.exists(), "Report output must be new")
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
