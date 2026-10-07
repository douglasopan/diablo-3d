#!/usr/bin/env python3
"""Check the authored asset registry; no game data, packages or network needed.

Usage: python tools/check_asset_registry.py [assets/registry.json]
Issue assignment is reviewed by maintainers, not queried or changed here.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys


ID = re.compile(r"tristram\.[a-z]+\.[a-z0-9-]+")
LOGIN = re.compile(r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,37}[A-Za-z0-9])?")
STATUSES = {"needed", "prototype", "review", "accepted"}
IDENTIFICATIONS = {"verified", "category", "unidentified"}
PHASES = {"tristram", "conditional-hellfire"}
REQUIRED = {
    "id", "name", "category", "status", "identification", "phase", "sourceKeys",
    "sourcePaths", "implementation", "variantCount", "instanceCount", "variants",
    "components", "dependsOn", "validationProfile", "claimIssue", "claimOwner",
    "reviewer", "approvedRevision", "assetPath", "notes", "provenance",
}


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON object key")
        result[key] = value
    return result


def repo_file(value: object, root: Path) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    if value.startswith("/") or re.match(r"^[A-Za-z]:", value) or ".." in Path(value).parts:
        return False
    path = (root / value).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError:
        return False
    return path.is_file()


def text_list(value: object) -> bool:
    return isinstance(value, list) and all(isinstance(item, str) and item.strip() and item == item.strip() for item in value)


def validate(data: object, root: Path, check_catalog: bool = True) -> list[str]:
    errors = []
    if not isinstance(data, dict):
        return ["registry must be a JSON object"]
    if data.get("format") != "d3d.asset-registry" or type(data.get("schemaVersion")) is not int or data["schemaVersion"] != 1:
        errors.append("unsupported format/schemaVersion")
    repository = data.get("repository")
    if not isinstance(repository, str) or not re.fullmatch(r"https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        errors.append("repository must be an ordinary GitHub HTTPS repository URL")
        repository = ""
    scope = data.get("scope")
    if not isinstance(scope, dict) or scope.get("completeGameCatalog") is not False:
        errors.append("scope must explicitly state completeGameCatalog=false")
    if not isinstance(scope, dict) or type(scope.get("approvedManualModels")) is not int or scope["approvedManualModels"] < 0:
        errors.append("approvedManualModels must be a nonnegative integer")
    profiles = data.get("validationProfiles")
    if not isinstance(profiles, dict) or not profiles:
        errors.append("validationProfiles must be a nonempty object")
        profiles = {}
    elif any(not text_list(value) or not value for value in profiles.values()):
        errors.append("each validation profile needs nonempty text checks")
    entries = data.get("entries")
    if not isinstance(entries, list) or not entries:
        return errors + ["entries must be a nonempty array"]
    identifiers, sources, reservations = set(), {}, {}
    accepted_authored = 0
    for index, entry in enumerate(entries):
        label = f"entry[{index}]"
        if not isinstance(entry, dict):
            errors.append(f"{label} must be an object")
            continue
        identifier = entry.get("id")
        if not isinstance(identifier, str) or not ID.fullmatch(identifier):
            errors.append(f"{label} has an invalid stable ID")
        else:
            label = identifier
            if identifier in identifiers:
                errors.append(f"duplicate ID: {identifier}")
            identifiers.add(identifier)
        if REQUIRED - entry.keys():
            errors.append(f"{label} is missing required schema fields")
        for key in ("name", "category", "implementation", "validationProfile"):
            if not isinstance(entry.get(key), str) or not entry[key].strip():
                errors.append(f"{label}.{key} must be nonempty text")
        for key, allowed in (("status", STATUSES), ("identification", IDENTIFICATIONS), ("phase", PHASES)):
            if not isinstance(entry.get(key), str) or entry[key] not in allowed:
                errors.append(f"{label}.{key} is invalid")
        for key in ("sourceKeys", "sourcePaths", "components", "dependsOn", "notes"):
            value = entry.get(key)
            if not text_list(value):
                errors.append(f"{label}.{key} must be a string array")
            elif len(set(value)) != len(value):
                errors.append(f"{label}.{key} repeats values")
        for key in ("variantCount", "instanceCount"):
            value = entry.get(key)
            if value is not None and (type(value) is not int or value < 0):
                errors.append(f"{label}.{key} must be null or a nonnegative integer")
        variants = entry.get("variants")
        if not isinstance(variants, list) or any(not isinstance(item, dict) or not isinstance(item.get("id"), str) or not item["id"] for item in variants):
            errors.append(f"{label}.variants must contain objects with IDs")
        elif len({item["id"] for item in variants}) != len(variants):
            errors.append(f"{label} repeats variant IDs")
        source_keys = entry.get("sourceKeys")
        if not text_list(source_keys) or not source_keys:
            errors.append(f"{label} needs source keys")
        else:
            for source in source_keys:
                if source in sources:
                    errors.append(f"source key already owned by {sources[source]}: {label}")
                sources[source] = label
        paths = entry.get("sourcePaths")
        if not text_list(paths) or not paths or any(not repo_file(path, root) for path in paths):
            errors.append(f"{label} has missing/unsafe source paths")
        if isinstance(entry.get("validationProfile"), str) and entry["validationProfile"] not in profiles:
            errors.append(f"{label} references an unknown validation profile")
        issue, owner = entry.get("claimIssue"), entry.get("claimOwner")
        if (issue is None) != (owner is None):
            errors.append(f"{label} needs paired claimIssue/claimOwner")
        if owner is not None and (not isinstance(owner, str) or not LOGIN.fullmatch(owner)):
            errors.append(f"{label}.claimOwner must be a GitHub login")
        if issue is not None:
            if not isinstance(issue, str) or not re.fullmatch(re.escape(repository) + r"/issues/[1-9][0-9]*", issue):
                errors.append(f"{label}.claimIssue must belong to this repository")
            elif issue in reservations:
                errors.append(f"reservation issue already owns {reservations[issue]}: {label}")
            else:
                reservations[issue] = label
        if isinstance(entry.get("status"), str) and entry["status"] in {"review", "accepted"} and (issue is None or owner is None):
            errors.append(f"{label} needs an assigned reservation for review/acceptance")
        reviewer, revision = entry.get("reviewer"), entry.get("approvedRevision")
        if reviewer is not None and (not isinstance(reviewer, str) or not LOGIN.fullmatch(reviewer)):
            errors.append(f"{label}.reviewer must be a GitHub login or null")
        if revision is not None and (not isinstance(revision, str) or not re.fullmatch(r"[0-9a-f]{7,40}", revision)):
            errors.append(f"{label}.approvedRevision must be a commit hash or null")
        asset = entry.get("assetPath")
        if asset is not None and not repo_file(asset, root):
            errors.append(f"{label}.assetPath is missing/unsafe")
        provenance = entry.get("provenance")
        if provenance is not None and (not isinstance(provenance, dict) or any(not isinstance(provenance.get(key), str) or not provenance[key].strip() for key in ("kind", "license", "source"))):
            errors.append(f"{label}.provenance needs kind/license/source text")
        if entry.get("status") == "accepted":
            if reviewer is None or revision is None or provenance is None:
                errors.append(f"{label} needs reviewer/revision/provenance for acceptance")
            if isinstance(provenance, dict) and provenance.get("kind") == "authored":
                accepted_authored += 1
                if asset is None:
                    errors.append(f"{label} needs an authored assetPath for acceptance")
    for entry in entries:
        if isinstance(entry, dict) and text_list(entry.get("dependsOn")):
            for dependency in entry["dependsOn"]:
                if dependency not in identifiers or dependency == entry.get("id"):
                    errors.append("unknown/self dependency in registry")
    if isinstance(scope, dict) and scope.get("approvedManualModels") != accepted_authored:
        errors.append("approvedManualModels disagrees with accepted authored entries")
    if check_catalog:
        catalog = root / "docs" / "ASSET-CATALOG.md"
        if not catalog.is_file():
            errors.append("missing docs/ASSET-CATALOG.md")
        else:
            text = catalog.read_text(encoding="utf-8-sig")
            if set(ID.findall(text)) != identifiers:
                errors.append("catalog/registry ID sets disagree")
            for match in re.finditer(r"\]\(([^)]+)\)", text):
                target = match.group(1)
                if not re.match(r"^[a-z]+:", target) and not target.startswith("#"):
                    if not (catalog.parent / target.split("#", 1)[0]).resolve().exists():
                        errors.append("catalog contains a missing local link")
    return errors


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("registry", nargs="?", type=Path, default=root / "assets" / "registry.json")
    arguments = parser.parse_args()
    try:
        data = json.loads(arguments.registry.read_text(encoding="utf-8-sig"), object_pairs_hook=unique_object)
        errors = validate(data, root)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"Registry check failed: {error}", file=sys.stderr)
        return 1
    if errors:
        for error in errors:
            print(f"ERROR {error}", file=sys.stderr)
        return 1
    counts = {status: sum(entry["status"] == status for entry in data["entries"]) for status in sorted(STATUSES)}
    print(f"Asset registry OK: {len(data['entries'])} unique IDs; " + ", ".join(f"{status}={count}" for status, count in counts.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
