"""Summarize the saved v4 diagnostics; native dispatch and raw geometry stay separate."""

import hashlib
import json
import re
from pathlib import Path

from PIL import Image


def audit(root: Path, name: str) -> dict:
    directory = root / "diagnostics" / name
    log = (root / "diagnostics" / (name + "-run.log")).read_text(encoding="utf-8")
    if "FAIL " in log or "Smoke check stopped" in log:
        raise ValueError(f"{name}: diagnostic failure")
    for path in directory.rglob("*.json"):
        json.loads(path.read_text(encoding="utf-8"))
    native_pairs = []
    for reference in sorted(directory.glob("native-*.png")):
        home = directory / reference.name.replace("native-", "nativefidelity-", 1)
        original = Image.open(reference).convert("RGB")
        restored = Image.open(home).convert("RGB")
        native_pairs.append({"view": reference.stem.removeprefix("native-"),
                             "size": list(original.size),
                             "exact": original.size == restored.size and original.tobytes() == restored.tobytes()})
    if not native_pairs or not all(pair["exact"] for pair in native_pairs):
        raise ValueError(f"{name}: original/Home dispatch differs")
    rocks = json.loads((directory / "rock-volume-audit.json").read_text(encoding="utf-8"))["objects"]
    turntables = json.loads((directory / "turntables" / "turntables.json").read_text(encoding="utf-8"))
    tree_audit = re.search(r"actual vegetation volume audit groups=(\d+) failures=(\d+)", log)
    if not tree_audit or tree_audit[2] != "0" or not all(rock["closedVolumeAudit"] for rock in rocks):
        raise ValueError(f"{name}: closed volume audit incomplete")
    frames = turntables["frames"]
    return {
        "diagnosticDirectory": str(directory),
        "smokePassed": True,
        "nativeDispatch": {"method": "same-coordinate RGB; original backend versus Home backend",
                           "exactViews": sum(pair["exact"] for pair in native_pairs), "views": native_pairs},
        "treesAudited": int(tree_audit[1]),
        "rockGroupsAudited": len(rocks),
        "rockSourceCells": sum(map(int, re.findall(r"INFO native rock .* source cells=(\d+)", log))),
        "rockFillersHiddenInsideArchitecture": sum(rock["hiddenByArchitecture"] for rock in rocks),
        "turntableFrames": len(frames),
        "architectureRaySamples": sum(frame["architectureSamples"] for frame in frames),
        "architectureHoles": sum(frame["architectureHoles"] for frame in frames),
        "minimumSelectablePlayerPixelsInOpenClearing": min(frame["playerPixels"] for frame in frames if frame["object"] == "player"),
        "interpretation": "Geometry audits establish closure and interaction, not exact artistic fidelity. Hidden faces are inferred.",
    }


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    executable = root / "build" / "devilutionx-tristram-v4.exe"
    report = {"executable": str(executable), "sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
              "dataSets": [audit(root, name) for name in ("v4-final-gog", "v4-final-shareware")]}
    raw = root / "diagnostics" / "qa-v4-final-gog" / "comparison.json"
    report["forcedRawGeometryComparison"] = json.loads(raw.read_text(encoding="utf-8"))["aggregate"]
    path = root / "diagnostics" / "v4-release-audit.json"
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"report": str(path), "dataSets": [{key: value for key, value in data.items()
                      if key not in ("nativeDispatch", "interpretation", "diagnosticDirectory")}
                     for data in report["dataSets"]],
                      "nativeExactViews": [data["nativeDispatch"]["exactViews"] for data in report["dataSets"]]}))


if __name__ == "__main__":
    main()
