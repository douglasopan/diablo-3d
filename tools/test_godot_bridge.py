#!/usr/bin/env python3
"""Real-loader integration checks for a locally prepared Godot test export.

Requires the licensed local game data, compiled smoke and editor_roundtrip.gd's
synthetic well export. Writes only below diagnostics; never changes live profiles.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import subprocess
import sys

import godot_bridge as bridge


def state(directory: Path) -> dict:
    return {p.relative_to(directory).as_posix(): (bridge.digest(p), p.stat().st_mtime_ns)
            for p in directory.rglob("*") if p.is_file()} if directory.exists() else {}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path)
    parser.add_argument("--pack", type=Path, default=bridge.REPO / "editor/godot/local/roundtrip/export")
    opts = parser.parse_args()
    real_root = bridge.workspace_root()
    data = opts.data or bridge.find_data(real_root)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = real_root / "diagnostics/godot-bridge" / stamp
    workspace = output / "workspace"
    profile = workspace / "perfil-godot-review"
    before = state(real_root / "perfil-tristram")
    baseline = json.loads((bridge.REPO / "assets/runtime-baseline.json").read_text(encoding="utf-8"))
    for source_key in ("sourcePath", "lightingSourcePath"):
        source = bridge.contained(real_root, baseline[source_key])
        target = bridge.contained(workspace, baseline[source_key])
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    # A synthetic save/config tests initialization without copying private saves.
    habitual = workspace / "perfil-tristram"
    habitual.mkdir(parents=True)
    (habitual / "single_99.sv").write_bytes(b"synthetic-preservation-fixture-not-a-playable-save")
    (habitual / "diablo.ini").write_text("[Graphics]\nWidth=1280\nHeight=720\nFullscreen=0\n3D GPU Rendering=1\n", encoding="utf-8")
    args = argparse.Namespace(
        workspace=workspace, project=bridge.REPO / "editor/godot", data=data,
        assets=real_root / "build/assets", smoke=real_root / "build/town_view_smoke.exe",
        executable=real_root / "build/devilutionx-tristram-godot.exe",
        pack=opts.pack.resolve(), allow_prototypes=False,
    )
    results: list[dict] = []

    def check(condition: bool, description: str) -> None:
        results.append({"passed": condition, "description": description})
        if not condition:
            raise AssertionError(description)

    candidate = args.executable
    args.executable = workspace / "build/missing-candidate.exe"
    try:
        bridge.apply_pack(args)
    except OSError:
        pass
    else:
        raise AssertionError("missing candidate unexpectedly accepted")
    check(not profile.exists(), "missing candidate is rejected before any review profile creation")
    args.executable = candidate
    bridge.apply_pack(args)
    receipt = json.loads((profile / "godot-pack-receipt.json").read_text(encoding="utf-8"))
    loaded = json.loads((Path(receipt["validation"]) / "town-snapshot.json").read_text(encoding="utf-8"))
    well = next(model for model in loaded["models"] if model["instanceId"] == "well")
    cabin = next(model for model in loaded["models"] if model["instanceId"] == "cabin-east")
    check(well["revision"] == "synthetic-static-box-roundtrip-v1" and len(well["triangles"]) == 12,
          "Godot-exported BoxMesh reaches the real C++ loader with 12 triangles and explicit revision")
    check(cabin["sha256"] == baseline["sourceSha256"] and cabin["hasFireSources"],
          "the selected cabin, interior and candle adjunct survive the unrelated well override")
    check(bridge.digest(habitual / "single_99.sv") == bridge.digest(profile / "single_99.sv"),
          "initialization copies a missing save only into the disposable review profile")
    check(well["sha256"] == receipt["models"][0]["sha256"], "decoded bytes match the exported model and application receipt")
    original = state(profile)
    manifest = (opts.pack / "d3d-maps/tristram.ini").read_text(encoding="utf-8")

    def refusal(name: str, text: str) -> None:
        pack = output / name
        shutil.copytree(opts.pack, pack)
        (pack / "d3d-maps/tristram.ini").write_text(text, encoding="utf-8")
        args.pack = pack
        try:
            bridge.apply_pack(args)
        except (ValueError, OSError):
            pass
        else:
            raise AssertionError(f"invalid pack unexpectedly accepted: {name}")
        check(state(profile) == original, f"{name}: rejected without touching the previously applied review profile")

    refusal("bad-native-bounds", manifest.replace("nativeMin=60,70", "nativeMin=59,70"))
    refusal("unknown-scene-field", manifest.replace("[Scene]", "[Scene]\nunknownField=1"))
    refusal("duplicate-instance", manifest.replace("instance=well", "instance=well\ninstance=well"))
    refusal("path-traversal", manifest.replace("model=d3d-models/editor/well.d3d", "model=d3d-models/editor/../well.d3d"))
    value = next(line.split("=", 1)[1] for line in manifest.splitlines() if line.startswith("sha256="))
    wrong_hash = ("0" if value[0] != "0" else "1") + value[1:]
    refusal("hash-divergence", manifest.replace(value, wrong_hash))
    partial = manifest.replace("instance=well", "instance=well\ninstance=house-gillian") + (
        "\n[house-gillian]\nassetId=tristram.arch.house-common\nvariantId=gillian\nnativeMin=35,64\nnativeMax=42,68\n"
        f"model=d3d-models/editor/well.d3d\nsha256={value}\nrevision=invalid-partial\ninterior=none\n")
    refusal("partial-loader-fallback", partial)
    refusal("protected-cabin", manifest.replace("instance=well", "instance=cabin-east").replace("[well]", "[cabin-east]")
            .replace("tristram.arch.well", "tristram.arch.cabin").replace("variantId=clean-water", "variantId=east-long")
            .replace("nativeMin=60,70", "nativeMin=70,66").replace("nativeMax=61,71", "nativeMax=74,72"))
    # A second explicit apply preserves local edits to save/config and creates
    # backup records for the old pack before replacing any referenced artifact.
    (profile / "single_99.sv").write_bytes(b"synthetic-newer-review-progress")
    (profile / "diablo.ini").write_text("[Graphics]\nWidth=1600\nHeight=900\n", encoding="utf-8")
    saved = {p.name: bridge.digest(p) for p in (profile / "single_99.sv", profile / "diablo.ini")}
    args.pack = opts.pack.resolve()
    bridge.apply_pack(args)
    check(all(bridge.digest(profile / name) == expected for name, expected in saved.items()),
          "reapplying a validated pack preserves existing review save/config edits")
    check(any((profile / "editor-backups").rglob("tristram.ini")), "reapply retains a recoverable previous manifest")
    check(before == state(real_root / "perfil-tristram"), "the user's entire habitual profile remains byte/time identical")
    bridge.write_json(output / "results.json", {"format": "d3d.godot-bridge-integration", "schemaVersion": 1,
        "syntheticWellOverride": True, "results": results, "privateEvidence": True, "visualApproval": False})
    print(f"GODOT_BRIDGE_INTEGRATION {len(results)} checks passed; {output / 'results.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
