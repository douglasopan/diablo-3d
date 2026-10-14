#!/usr/bin/env python3
"""Package exact hero authoring bytes locally; never install/select game assets."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import zipfile
from PIL import Image

DOC = Path(__file__).resolve().parent
REPO = DOC.parents[2]
WORKSPACE = REPO.parent
ART = WORKSPACE / "assets/own-art/heroes/local-r1"
OUT = WORKSPACE / "diagnostics/own-art-heroes-local-20261012-r1"
DIRECTIONS = ["s", "sw", "w", "nw", "n", "ne", "e", "se"]

def sha(data):
    return hashlib.sha256(data).hexdigest()

def dump(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")

def record(path, archive):
    data = path.read_bytes()
    r = {"path": str(path.resolve()), "archivePath": archive, "bytes": len(data), "sha256": sha(data)}
    if path.suffix.lower() == ".png":
        with Image.open(path) as im:
            im.load()
            r.update(dimensions=list(im.size), mode=im.mode)
            if im.mode == "RGBA":
                r.update(alphaExtrema=list(im.getchannel("A").getextrema()))
    return r

def build():
    OUT.mkdir(parents=True, exist_ok=True)
    files = []
    for path in sorted(ART.rglob("*")):
        if not path.is_file() or any(x.startswith(("bind-directions-", "diagnostic-directions-framing-")) for x in path.relative_to(ART).parts):
            continue
        files.append((path, "art/" + path.relative_to(ART).as_posix()))
    for path in sorted(DOC.rglob("*")):
        if not path.is_file() or "__pycache__" in path.parts or path.name in {"manifest.json", "SHA256SUMS.txt"}:
            continue
        files.append((path, "metadata/" + path.relative_to(DOC).as_posix()))
    files.append((REPO / "tools/inspect_glb.py", "tools/inspect_glb.py"))
    if (REPO / "LICENSE").is_file():
        files.append((REPO / "LICENSE", "licenses/DevilutionX-LICENSE"))
    records = [record(p, a) for p, a in files]
    stills = {
        "warrior": ART / "warrior/bind-directions",
        "rogue": ART / "rogue/authoring-directions-r1",
        "sorcerer": ART / "sorcerer/authoring-directions-r2",
    }
    heroes = []
    for cls, directory in stills.items():
        name = "bind.png" if cls == "warrior" else "still.png"
        images = [directory / d / name for d in DIRECTIONS]
        if not all(p.is_file() for p in images):
            raise RuntimeError("Missing direction still for " + cls)
        heroes.append({
            "id": "tristram.hero." + cls, "variant": "light-unarmed", "status": "candidate-authoring-stills",
            "stills": [{"nativeDirectionIndex": i, "direction": d, "file": "art/" + p.relative_to(ART).as_posix(), "sha256": sha(p.read_bytes()), "frameCount": 1, "animationFrames": 0} for i, (d, p) in enumerate(zip(DIRECTIONS, images))],
            "authoringPixelPivot": [256, 448] if cls == "warrior" else [256, 608],
            "canvas": [512, 512] if cls == "warrior" else [512, 640],
            "nativePivotCalibrated": False, "nativeAnimationSlotsValidated": 0, "runtimeEligible": False,
            "masterKind": "preserved-own-GLB-master-and-rig" if cls == "warrior" else "new-own-raster-identity-master-no-geometry",
            "projection": "measured orthographic elevation30 from real GLB" if cls == "warrior" else "generated low-camera approximate yaw; needs camera calibration",
        })
    motion_reports = []
    retarget_reports = []
    for path in sorted((ART / "warrior").glob("**/retarget-receipt.json")):
        r = json.loads(path.read_text(encoding="utf-8"))
        retarget_reports.append({"receipt": "art/" + path.relative_to(ART).as_posix(), "status": r.get("status"), "clip": r.get("candidate", {}).get("clip"), "candidate": r.get("candidate"), "source": r.get("source"), "nativeCompatibleValidated": False, "runtimeEligible": False})
    for path in sorted((ART / "warrior").glob("**/render-receipt.json")):
        if path.parent != stills["warrior"] and not any(x.startswith(("bind-directions-", "diagnostic-directions-framing-")) for x in path.parts):
            r = json.loads(path.read_text(encoding="utf-8"))
            clip = r.get("clip") or r.get("candidate", {}).get("clip")
            if clip:
                motion_reports.append({"receipt": "art/" + path.relative_to(ART).as_posix(), "status": "diagnostic-reject" if retarget_reports else r.get("status"), "rendererStatus": r.get("status"), "nativeState": r.get("nativeState"), "clip": clip, "renderedFrames": sum(len(d["frames"]) for d in r.get("directions", [])), "projection": r.get("projection"), "validation": r.get("validation"), "nativeCompatibleValidated": False, "runtimeEligible": False})
    manifest = {
        "schema": "d3d.own-hero-art-handoff/1", "localDate": "2026-10-12", "timezone": "America/Sao_Paulo",
        "status": "authoring-candidates-not-runtime-pack", "scope": "HERÓIS only; principal integrates",
        "sourceThread": "01a1153a-cad1-7fb1-8326-225511ee918d", "cloudCoordinatorThread": "01a12deb-9f8d-751c-ac49-8a1ac93d9031",
        "gameplayPolicy": "ONLY SPRITES for every hero and NPC including Warrior/Ogden until correct rig plus animations are validated; none selected or installed here",
        "generated": {"provider": "built-in imagegen", "calls": 5, "newRasterMasterFilesIncludingRejectedRevision": 5, "newPaidMeshyTripoCalls": 0, "newMeshyTripoSpend": 0},
        "counts": {"baseHeroesWithAuthoringAssets": 3, "preservedWarriorGlbSources": 3, "preservedWarriorConcepts": 1, "newRogueSorcererIdentityMasters": 2, "directionStills": 24, "animatedStillsCountedAsMotion": 0, "authoredMotionDiagnosticClips": len(retarget_reports), "renderedMotionDiagnosticFrames": sum(r["renderedFrames"] for r in motion_reports), "nativeAnimationClipsValidated": 0, "heroesRuntimeIntegrated": 0, "heroesCompleteForDistribution": 0},
        "heroes": heroes, "motionDiagnostics": motion_reports, "retargetDiagnostics": retarget_reports,
        "nativeContract": {"file": "metadata/contract-audit.json", "directionOrder": DIRECTIONS, "clock": "native tick/frame/fraction authoritative; default20 and habitualSpeed32 are distinct", "eventRule": "attackImpact A-1, spellRelease SF; no visual damage/audio/RNG", "clxPivot": "CalculateSpriteTileCenterX(width)=(width-64)/2; native bottom-left/tile-origin anchor differs from authoring raster footpoint", "stateMatrix": "eleven native resource slots per graphic appearance, variant/guard/frame counts in audit; no reference atlas counts treated as native durations"},
        "authoringSelection": {"warriorSource": "warrior-light-unarmed-r3-tripo-bind-basis-repaired-2k-sha256-1c09cc5e55fb.glb", "warriorCloseupMaster": "warrior-light-unarmed-r3-tripo-master-4k-sha256-bb8c85b9186e.glb", "rogueSheet": "rogue-light-unarmed-directions-r1.png", "sorcererSheet": "sorcerer-light-unarmed-directions-r2.png", "rejectedButPreserved": ["sorcerer-light-unarmed-directions-r1.png", "Warrior raw4K rig known defective bind; preserved source only"], "isGlobalRegistrySelection": False},
        "provenance": {"newRogueSorcererPixels": "OpenAI built-in imagegen, prompts bundled, original own-art", "warrior": "existing own Tripo master, job e18a8313-d802-44b6-80bd-b9620409a1ea,40 historicalcredits; re-used immutable source hashes", "productionCredit": "Douglas Pan / Diablo3D", "DamnationXPixelInput": False, "nativeExtractPixelInput": False, "acknowledgement": "DamnationX studied historically for category/compatibility reference; no redistribution of its pixels"},
        "limits": ["Stills/concepts are not clips", "Rogue/Sorcerer have no mesh/rig/motion yet", "Approximate raster directions need exact game-camera/yaw/native-pivot review", "Warrior materials use diffuse-only CPU preview, no fullPBR match", "Armor light/medium/heavy, weapons/shields and all native-state motions are incomplete", "Hellfire conditional, Monk/Bard/Barbarian not newly produced", "Local private production handoff, no release installer/public publication; Panvia entry, own launcher cancelled", "Principal owns Source/build/global registry/container/runtime QA/install"],
        "verification": {"contractAudit": "metadata/contract-audit.json", "rasterQA": "metadata/raster-qa.json", "rasterPostwrite": "metadata/raster-extraction-validation.json", "cpuPipeline": "metadata/pipeline/pipeline-validation.json", "retargetIndependent": "metadata/retarget-independent-review-r1.json" if retarget_reports else None, "nativeRuntimeTestRun": False, "humanArtisticApproval": False},
        "files": records,
    }
    dump(DOC / "manifest.json", manifest)
    files.append((DOC / "manifest.json", "manifest.json"))
    checksums = "".join(sha(p.read_bytes()) + "  " + a + "\n" for p, a in files)
    (DOC / "SHA256SUMS.txt").write_text(checksums, encoding="utf-8")
    files.append((DOC / "SHA256SUMS.txt", "SHA256SUMS.txt"))
    archive = OUT / "Diablo3D_Heroes_Local_R1_CANDIDATES.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for p, a in files:
            z.write(p, "Diablo3D_Heroes_Local_R1/" + a)
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:
            raise RuntimeError("Archive CRC failure")
        for p, a in files:
            if sha(z.read("Diablo3D_Heroes_Local_R1/" + a)) != sha(p.read_bytes()):
                raise RuntimeError("Archive source hash mismatch: " + a)
    receipt = {"schema": "d3d.hero-art-local-package-receipt/1", "archive": str(archive), "bytes": archive.stat().st_size, "sha256": sha(archive.read_bytes()), "fileCount": len(files), "crcAllPassed": True, "allArchiveBytesMatchSources": True, "manifestSha256": sha((DOC / "manifest.json").read_bytes()), "counts": manifest["counts"], "runtimeEligible": False}
    dump(OUT / "delivery-receipt.json", receipt)
    print(json.dumps(receipt))

if __name__ == "__main__":
    build()
