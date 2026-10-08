"""Validate public candidate item-concept links and PNG identities, read-only."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[2]
SHA = re.compile(r"[0-9a-f]{64}")
IMAGE = re.compile(r"assets/concept-art/items/[a-z0-9][a-z0-9-]*/[a-z0-9][a-z0-9._-]*\.png")
KINDS = {"turnaround", "single-view", "concept-sheet", "family-sheet"}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate JSON key: " + key)
        result[key] = value
    return result


def load(path):
    def reject(value):
        raise ValueError("Non-finite JSON constant: " + value)
    return json.loads(Path(path).read_text(encoding="utf-8-sig"), object_pairs_hook=unique_object, parse_constant=reject)


def png_dimensions(data):
    """Check PNG structure, all chunk CRCs and image dimensions; no pixel claims."""
    if len(data) > 64 * 1024 * 1024 or not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("Expected PNG, at most 64 MiB")
    offset, chunks, dimensions, idat, ended = 8, 0, None, False, False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError("Truncated PNG chunk")
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        end = offset + 12 + length
        if end > len(data):
            raise ValueError("PNG chunk exceeds file")
        body = data[offset + 8:offset + 8 + length]
        expected = struct.unpack_from(">I", data, offset + 8 + length)[0]
        if zlib.crc32(kind + body) & 0xFFFFFFFF != expected:
            raise ValueError("PNG chunk CRC mismatch")
        if chunks == 0:
            if kind != b"IHDR" or length != 13:
                raise ValueError("PNG must start with one IHDR")
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", body)
            if not 0 < width <= 16384 or not 0 < height <= 16384 or width * height > 100_000_000:
                raise ValueError("PNG dimensions exceed review limits")
            valid_depths = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}
            if depth not in valid_depths.get(color, ()) or compression or filtering or interlace not in (0, 1):
                raise ValueError("Unsupported PNG header")
            dimensions = {"width": width, "height": height}
        elif kind == b"IHDR":
            raise ValueError("Duplicate IHDR")
        if kind == b"IDAT":
            idat = True
        if kind == b"IEND":
            if length != 0 or end != len(data):
                raise ValueError("Invalid IEND or trailing bytes")
            ended = True
            break
        chunks += 1
        if chunks > 100_000:
            raise ValueError("Too many PNG chunks")
        offset = end
    if not ended or not idat or dimensions is None:
        raise ValueError("PNG lacks complete header/image/end chunks")
    return dimensions


def validate(catalog, manifest, root):
    errors, warnings, results = [], [], []
    def error(where, message):
        errors.append(where + ": " + message)
    if not isinstance(catalog, dict) or catalog.get("format") != "d3d.item-production-study":
        return {"passed": False, "errors": ["catalog: Unsupported format"], "warnings": [], "entries": []}
    units_list = catalog.get("productionUnits", [])
    if not isinstance(units_list, list):
        return {"passed": False, "errors": ["catalog.productionUnits: Expected array"], "warnings": [], "entries": []}
    units = {unit["id"]: unit for unit in units_list if isinstance(unit, dict) and isinstance(unit.get("id"), str)}
    if len(units) != len(units_list):
        error("catalog.productionUnits", "Missing or duplicate production identity")
    native_list = catalog.get("nativeVisuals", [])
    if not isinstance(native_list, list):
        error("catalog.nativeVisuals", "Expected array")
        native_list = []
    native = {v["cursorName"]: v for v in native_list if isinstance(v, dict) and isinstance(v.get("cursorName"), str) and type(v.get("cursorValue")) is int}
    for unit in units.values():
        names = unit.get("nativeCursorNames", [])
        if not isinstance(names, list) or any(not isinstance(name, str) or name not in native for name in names):
            error(unit["id"], "Invalid native cursor links")
    for field, linkfield in (("baseItems", "sourceBaseIds"), ("uniqueItems", "sourceUniqueIds"), ("dynamicAppearances", "sourceDynamicIds")):
        rows = catalog.get(field, [])
        if not isinstance(rows, list):
            error("catalog." + field, "Expected array")
            rows = []
        records = {r["id"]: r for r in rows if isinstance(r, dict) and isinstance(r.get("id"), str)}
        for unit in units.values():
            linked = unit.get(linkfield, [])
            if not isinstance(linked, list) or any(not isinstance(i, str) or i not in records for i in linked):
                error(unit["id"], "Unresolved source link in " + linkfield)
    if not isinstance(manifest, dict):
        return {"passed": False, "errors": errors + ["manifest: Expected object"], "warnings": [], "entries": []}
    if manifest.get("format") != "d3d.item-concept-art" or type(manifest.get("schemaVersion")) is not int or manifest.get("schemaVersion") != 1:
        error("manifest", "Unsupported format/schemaVersion")
    if "catalogSha256" in manifest and (not isinstance(manifest["catalogSha256"], str) or not SHA.fullmatch(manifest["catalogSha256"])):
        error("manifest.catalogSha256", "Expected lowercase SHA256 of the catalog JSON bytes")
    entries = manifest.get("entries")
    if not isinstance(entries, list):
        return {"passed": False, "errors": errors + ["manifest.entries: Expected array"], "warnings": [], "entries": []}
    seen, image_hashes, image_cache = set(), {}, {}
    root = Path(root).resolve()
    for index, entry in enumerate(entries):
        where = f"entries[{index}]"
        if not isinstance(entry, dict):
            error(where, "Expected object")
            continue
        identity = entry.get("productionId")
        if not isinstance(identity, str) or identity not in units:
            error(where + ".productionId", "Unknown catalog production ID")
            unit = None
        else:
            unit = units[identity]
        revision = entry.get("revision")
        if not isinstance(revision, str) or not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,63}", revision):
            error(where + ".revision", "Expected stable lowercase revision label")
        pair = (identity, revision) if isinstance(identity, str) and isinstance(revision, str) else None
        if pair is not None:
            if pair in seen:
                error(where, "Duplicate production ID/revision")
            seen.add(pair)
        if entry.get("status") != "candidate":
            error(where + ".status", "This concept overlay accepts candidate only")
        kind = entry.get("kind")
        if not isinstance(kind, str) or kind not in KINDS:
            error(where + ".kind", "Expected turnaround/single-view/concept-sheet/family-sheet")
        if entry.get("visualApproval") is not False:
            error(where + ".visualApproval", "Candidate must explicitly be false")
        for field in ("modelStatus", "iconStatus"):
            if entry.get(field) != "not-generated":
                error(where + "." + field, "Concept delivery cannot claim generated model/icon")
        for field in ("accepted", "selected", "installed"):
            if field in entry and entry[field] is not False:
                error(where + "." + field, "Concept delivery cannot claim acceptance, selection or installation")
        prompt = entry.get("prompt")
        if not isinstance(prompt, str) or not prompt.strip() or len(prompt) > 100_000:
            error(where + ".prompt", "Expected the actual nonempty generation prompt, at most 100,000 characters")
        digest = entry.get("sha256")
        if not isinstance(digest, str) or not SHA.fullmatch(digest):
            error(where + ".sha256", "Expected lowercase SHA256")
        image = entry.get("image")
        dimensions = None
        if not isinstance(image, str) or not IMAGE.fullmatch(image):
            error(where + ".image", "Image must be a PNG under assets/concept-art/items/<revision>/; no URLs or private paths")
        else:
            try:
                target = root / PurePosixPath(image)
                target.resolve().relative_to(root / "assets" / "concept-art" / "items")
                if image not in image_cache:
                    if not target.is_file():
                        raise ValueError("Image missing")
                    data = target.read_bytes()
                    image_cache[image] = (hashlib.sha256(data).hexdigest(), png_dimensions(data))
                actual, dimensions = image_cache[image]
                if actual != digest:
                    error(where + ".sha256", "Does not match final PNG bytes")
                if image in image_hashes and image_hashes[image] != digest:
                    error(where + ".image", "Shared sheet path has conflicting hashes")
                image_hashes[image] = digest
            except (OSError, ValueError) as exc:
                error(where + ".image", str(exc))
        reference = entry.get("reference")
        if not isinstance(reference, dict):
            error(where + ".reference", "Expected explicit provenance object")
        else:
            availability = reference.get("availability")
            if availability == "local-private":
                cursor = reference.get("cursorId")
                if type(cursor) is not int:
                    error(where + ".reference.cursorId", "Expected native ICURS integer")
                if not isinstance(reference.get("sha256"), str) or not SHA.fullmatch(reference.get("sha256", "")):
                    error(where + ".reference.sha256", "Expected private reference identity without its file or absolute path")
                names = unit.get("nativeCursorNames", []) if unit else []
                cursor_values = {native[c]["cursorValue"] for c in names if isinstance(c, str) and c in native} if isinstance(names, list) else set()
                if unit and type(cursor) is int and cursor not in cursor_values:
                    error(where + ".reference.cursorId", "Cursor is not associated with this production unit")
            elif availability == "inferred-concept":
                if kind == "turnaround":
                    error(where + ".reference", "A reference-backed turnaround requires local-private provenance")
                warnings.append(where + ": Inferred concept is not a source-faithful individual reference")
            else:
                error(where + ".reference.availability", "Expected local-private or inferred-concept")
            allowed_reference = {"availability", "cursorId", "sha256", "notes"}
            if any(key not in allowed_reference for key in reference):
                error(where + ".reference", "Unexpected provenance field; do not publish private media paths")
        if kind in ("concept-sheet", "family-sheet"):
            warnings.append(where + ": A sheet link does not mean an individual object is ready for 3D generation")
        results.append({"productionId": identity, "revision": revision, "kind": kind, "image": image, "dimensions": dimensions})
    linked = {e["productionId"] for e in results if isinstance(e["productionId"], str) and e["productionId"] in units}
    return {"format": "d3d.item-concept-art-validation", "schemaVersion": 1, "passed": not errors, "errors": errors, "warnings": warnings,
            "productionUnits": len(units), "linkedProductionUnits": len(linked), "entries": results,
            "uniqueImagesChecked": len(image_cache), "unlinkedProductionUnits": len(units) - len(linked),
            "turnaroundEntries": sum(e["kind"] == "turnaround" for e in results),
            "sheetEntries": sum(e["kind"] in ("concept-sheet", "family-sheet") for e in results),
            "modelsGenerated": 0, "iconsGenerated": 0, "installedAssetsChanged": False,
            "limits": ["Checks metadata, complete PNG structure/CRCs/dimensions and byte hashes; does not decode or artistically approve pixels.",
                       "Private reference hashes are recorded without opening or publishing their files.",
                       "Coverage means linked candidate entries, not ready models or individual turnarounds."]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--catalog", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        catalog_path = args.catalog or args.root / "docs/items-study/catalog.json"
        catalog = load(catalog_path)
        manifest = load(args.manifest or args.root / "docs/items-study/concept-art.json")
        report = validate(catalog, manifest, args.root)
        if isinstance(manifest, dict) and "catalogSha256" in manifest:
            if manifest["catalogSha256"] != hashlib.sha256(catalog_path.read_bytes()).hexdigest():
                report["errors"].append("manifest.catalogSha256: Does not match catalog JSON bytes")
                report["passed"] = False
    except (OSError, UnicodeError, ValueError) as exc:
        report = {"passed": False, "errors": ["input: " + str(exc)], "warnings": [], "exitCode": 2}
    code = report.get("exitCode", 0 if report["passed"] else 1)
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        for error in report["errors"]:
            print("ERROR " + error, file=sys.stderr)
        print("Concept art: " + ("PASS" if report["passed"] else "FAIL") + f"; {len(report['errors'])} errors; {len(report['warnings'])} warnings; " + str(report.get("linkedProductionUnits", 0)) + "/" + str(report.get("productionUnits", 0)) + " candidate units linked")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
