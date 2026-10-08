"""Independent, standard-library verification of a synthetic Godot export.

Usage: python tests/verify_export.py local/exporter-tests/<run>/package
Reads only; private snapshots and models must remain in ignored local/.
"""

from __future__ import annotations

import hashlib
import json
import math
import re
import struct
import sys
from pathlib import Path, PurePosixPath


def check(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def mesh_check(data: bytes) -> dict:
    check(len(data) >= 20 and data[:8] == b"D3DMESH1", "D3DMESH1 header")
    count, width, height = struct.unpack_from("<III", data, 8)
    check(1 <= count <= 20_000, "triangle count")
    check(1 <= width <= 2048 and 1 <= height <= 2048, "atlas bounds")
    check(len(data) == 20 + count * 60 + width * height * 3, "exact byte count")
    for triangle in struct.iter_unpack("<15f", data[20 : 20 + count * 60]):
        vertices = [triangle[index : index + 5] for index in (0, 5, 10)]
        for x, y, z, u, v in vertices:
            check(all(math.isfinite(value) for value in (x, y, z, u, v)), "finite vertex")
            check(-64 <= x <= 128 and -64 <= z <= 128 and 0 <= y <= 64, "vertex bounds")
            check(0 <= u <= 1 and 0 <= v <= 1, "UV bounds")
        a, b, c = vertices
        ab = [b[index] - a[index] for index in range(3)]
        ac = [c[index] - a[index] for index in range(3)]
        normal = (
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0],
        )
        check(math.sqrt(sum(value * value for value in normal)) > 1e-6, "nondegenerate triangle")
    return {"triangles": count, "atlasSize": [width, height]}


def verify(folder: Path) -> dict:
    folder = folder.resolve(strict=True)
    manifest = (folder / "d3d-maps/tristram.ini").read_bytes()
    receipt = json.loads((folder / "receipt.json").read_text(encoding="utf-8"))
    check(receipt["format"] == "d3d.godot-export-receipt" and receipt["schemaVersion"] == 1, "receipt format")
    check(receipt["manifestSha256"] == hashlib.sha256(manifest).hexdigest(), "manifest hash")
    sections: dict[str, list[tuple[str, str]]] = {}
    section = ""
    for line in manifest.decode("utf-8").splitlines():
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            check(section not in sections, "duplicate section")
            sections[section] = []
        else:
            key, value = line.split("=", 1)
            sections[section].append((key, value))
    scene = dict(sections["Scene"])
    check(scene["format"] == "d3d.town-map" and scene["schemaVersion"] == "1" and scene["edition"] == "retail", "scene format")
    instances = [value for key, value in sections["Scene"] if key == "instance"]
    check(len(instances) == len(set(instances)) and instances, "unique explicit instances")
    receipt_by_id = {item["instanceId"]: item for item in receipt["instances"]}
    check(set(instances) == set(receipt_by_id) == set(sections) - {"Scene"}, "manifest/receipt IDs")
    results = []
    for identity in instances:
        check(identity != "cabin-east", "v1 fire protection")
        entry = dict(sections[identity])
        check(len(entry) == len(sections[identity]), "duplicate instance property")
        relative = PurePosixPath(entry["model"])
        check(str(relative) == f"d3d-models/editor/{identity}.d3d" and ".." not in relative.parts, "model path")
        model_path = (folder / relative).resolve(strict=True)
        check(model_path.is_relative_to(folder), "model stays inside package")
        data = model_path.read_bytes()
        check(re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]) is not None, "hash syntax")
        check(hashlib.sha256(data).hexdigest() == entry["sha256"] == receipt_by_id[identity]["sha256"], "real model hash")
        check(entry["interior"] == "none" and entry["revision"].strip(), "explicit complete revision")
        check(receipt_by_id[identity]["visualApproval"] is False and receipt_by_id[identity]["validation"] == "technical-only", "technical status")
        decoded = mesh_check(data)
        check(decoded["triangles"] == receipt_by_id[identity]["triangles"] and decoded["atlasSize"] == receipt_by_id[identity]["atlasSize"], "receipt geometry")
        results.append({"instanceId": identity, "sha256": entry["sha256"], **decoded})
    return {"ok": True, "instances": results}


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python tests/verify_export.py <local export folder>")
    try:
        print(json.dumps(verify(Path(sys.argv[1])), ensure_ascii=False))
    except (OSError, KeyError, ValueError, struct.error) as error:
        print(f"VERIFY_EXPORT_FAILED: {error}", file=sys.stderr)
        raise SystemExit(1) from error
