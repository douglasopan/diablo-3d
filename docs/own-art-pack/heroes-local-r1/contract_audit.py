"""Read-only hero contract audit; writes only this audit's JSON and Markdown.

No image pixels are copied, no provider calls are made, no global selection changes.
Run: python docs/own-art-pack/heroes-local-r1/contract_audit.py
"""
from __future__ import annotations

import csv
import hashlib
import json
import struct
import subprocess
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
WORKSPACE = REPO.parent
CHARACTERS = WORKSPACE / "diagnostics/characters-rigs"
CLASSES = ("warrior", "rogue", "sorcerer")
DIRECTIONS = ("South", "SouthWest", "West", "NorthWest", "North", "NorthEast", "East", "SouthEast")
DIRECTION_TOKENS = ("s", "sw", "w", "nw", "n", "ne", "e", "se")
WEAPONS = ("unarmed", "unarmed_shield", "sword", "sword_shield", "bow", "axe", "mace", "mace_shield", "staff")
WEAPON_VARIABLES = ("unarmed", "unarmedShield", "sword", "swordShield", "bow", "axe", "mace", "maceShield", "staff")
WEAPON_CHARS = ("n", "u", "s", "d", "b", "a", "m", "h", "t")


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def meta(path: Path):
    raw = path.read_bytes()
    return {"path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def table(path: Path):
    values, lines = {}, {}
    for number, row in enumerate(csv.DictReader(path.read_text(encoding="utf-8-sig").splitlines(), delimiter="\t"), 2):
        value = row["Value"]
        values[row["Variable"]] = int(value) if value.lstrip("-").isdigit() else value
        lines[row["Variable"]] = number
    return values, lines


def glb(path: Path, stage: str):
    raw = path.read_bytes()
    magic, version, total = struct.unpack_from("<4sII", raw)
    assert (magic, version, total) == (b"glTF", 2, len(raw)), str(path)
    cursor, document, blob = 12, None, b""
    while cursor < total:
        size, kind = struct.unpack_from("<I4s", raw, cursor)
        cursor += 8
        chunk = raw[cursor:cursor + size]
        assert len(chunk) == size
        if kind == b"JSON":
            document = json.loads(chunk.rstrip(b" \0\t\r\n"))
        elif kind == b"BIN\0":
            blob = chunk
        cursor += size
    assert cursor == total and document is not None
    joints = [{"skin": i, "node": node, "name": document["nodes"][node].get("name", "")}
              for i, skin in enumerate(document.get("skins", [])) for node in skin["joints"]]
    primitives = []
    for mesh_index, mesh in enumerate(document.get("meshes", [])):
        for primitive in mesh.get("primitives", []):
            accessors = document["accessors"]
            position = accessors[primitive["attributes"]["POSITION"]]
            index_count = accessors[primitive["indices"]]["count"] if "indices" in primitive else position["count"]
            primitives.append({"mesh": mesh_index, "vertices": position["count"],
                               "triangles": index_count // 3, "attributes": list(primitive["attributes"]),
                               "positionBounds": [position.get("min"), position.get("max")]})
    clips = []
    for animation in document.get("animations", []):
        times = []
        for sampler in animation.get("samplers", []):
            accessor = document["accessors"][sampler["input"]]
            assert accessor["componentType"] == 5126 and accessor["type"] == "SCALAR"
            view = document["bufferViews"][accessor["bufferView"]]
            start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
            stride = view.get("byteStride", 4)
            times.extend(struct.unpack_from("<f", blob, start + n * stride)[0] for n in range(accessor["count"]))
        clips.append({"name": animation.get("name", ""), "channels": len(animation.get("channels", [])),
                      "startSeconds": min(times) if times else None, "endSeconds": max(times) if times else None})
    embedded_images = []
    for image in document.get("images", []):
        record = {"name": image.get("name"), "mimeType": image.get("mimeType"), "externalUri": image.get("uri")}
        if "bufferView" in image:
            view = document["bufferViews"][image["bufferView"]]
            image_raw = blob[view.get("byteOffset", 0):view.get("byteOffset", 0) + view["byteLength"]]
            record.update(bytes=len(image_raw), sha256=hashlib.sha256(image_raw).hexdigest())
            if image_raw.startswith(b"\x89PNG\r\n\x1a\n"):
                record["dimensions"] = list(struct.unpack_from(">II", image_raw, 16))
        embedded_images.append(record)
    explicit_transforms = sum(any(key in node for key in ("translation", "rotation", "scale", "matrix")) for node in document.get("nodes", []))
    return {**meta(path), "stage": stage, "nodes": len(document.get("nodes", [])),
            "skins": len(document.get("skins", [])), "jointCount": len(joints), "joints": joints,
            "primitives": primitives, "materials": len(document.get("materials", [])), "embeddedImages": embedded_images,
            "clipCount": len(clips), "clips": clips, "nodesWithExplicitTransforms": explicit_transforms,
            "noAnimationsMeaning": "Skin/bind or concept is not an idle/walk/action clip." if not clips else None}


def source_line(path: str, substring: str):
    file_path = REPO / path
    for line, text in enumerate(file_path.read_text(encoding="utf-8-sig").splitlines(), 1):
        if substring in text:
            return {"path": path, "line": line, "token": substring}
    raise AssertionError((path, substring))


def native_class(cls: str):
    directory = REPO / "assets/txtdata/classes" / cls
    anim, anim_lines = table(directory / "animations.tsv")
    sprites, _ = table(directory / "sprites.tsv")
    definitions = (
        ("idle_dungeon", "Stand", "as", "idleFrames", 4, "stand", True, "dungeon"),
        ("idle_town", "Stand", "st", "townIdleFrames", 4, "stand", True, "town"),
        ("walk_dungeon", "Walk", "aw", "walkingFrames", 1, "walk", False, "dungeon"),
        ("walk_town", "Walk", "wl", "townWalkingFrames", 1, "walk", False, "town"),
        ("hit_recovery", "Hit", "ht", "recoveryFrames", 1, "swHit", False, "dungeon"),
        ("block", "Block", "bl", "blockingFrames", 3, "block", False, "dungeon"),
        ("spell_lightning", "Lightning", "lm", "castingFrames", 1, "lightning", False, "both"),
        ("spell_fire", "Fire", "fm", "castingFrames", 1, "fire", False, "both"),
        ("spell_magic", "Magic", "qm", "castingFrames", 1, "magic", False, "both"),
        ("death", "Death", "dt", "deathFrames", 2, "death", False, "both"),
    )
    slots = []
    for name, graphic, suffix, variable, delay, width_key, loop, environment in definitions:
        width = sprites[width_key]
        entry = {"slot": name, "nativeGraphic": graphic, "resourceSuffix": suffix,
                 "baseFrames": anim[variable], "directions": 8, "ticksPerFrame": delay, "loop": loop,
                 "environment": environment, "logicalWidth": width, "nativeHorizontalDrawOffset": -(width - 64) // 2,
                 "nominalSlotSpanSecondsAt20Ticks": anim[variable] * delay / 20,
                 "framesEvidence": {"path": f"assets/txtdata/classes/{cls}/animations.tsv", "line": anim_lines[variable]},
                 "resourcePathTemplate": f"plrgfx/{sprites['classPath']}/<classChar><armourChar><weaponChar>/<classChar><armourChar><weaponChar>{suffix}.cl2",
                 "generatedClipStatus": "missing-native-compatible-motion"}
        if name.startswith("spell_"):
            entry["releaseFrameIndex0"] = anim["castingActionFrame"]
            entry["townWeaponFallback"] = {"sword": "unarmed", "mace": "unarmed", "sword_shield": "unarmed_shield", "mace_shield": "unarmed_shield"}
        if name == "block":
            entry["loadGuard"] = "Dungeon only and player._pBlockFlag; do not infer enabled block for every equipment."
            entry["fastBlock"] = "Native FastBlock starts after N-2 skipped frames; final delay is skipped."
        if name == "death":
            entry["weaponGraphicForced"] = "unarmed"
            entry["finalPose"] = "held by native state; post-completion delay becomes 100; no looping resurrection"
            if cls == "warrior":
                entry["armorOverride"] = {"lightFrames": 20, "mediumFrames": 15, "heavyFrames": 15}
        if name.startswith("walk_"):
            entry["loopMeaning"] = "Each native step ends/commits tile; repeated steps may chain. Visual cyclic gait does not own movement."
            entry["rootMotion"] = "in-place; native position and GetOffsetForWalking are authoritative"
        if name == "idle_dungeon" and cls == "warrior":
            entry["bowFramesOverride"] = 8
        slots.append(entry)
    attacks = []
    for weapon, variable in zip(WEAPONS, WEAPON_VARIABLES):
        count, action = anim[variable + "Frames"], anim[variable + "ActionFrame"]
        width = sprites["bow" if weapon == "bow" else "attack"]
        attacks.append({"weapon": weapon, "frames": count, "actionFrameA": action,
                        "swingFrameIndex0": action - 2, "impactOrArrowFrameIndex0": action - 1,
                        "chainFromFrameIndex0": action, "directions": 8, "ticksPerFrame": 1,
                        "logicalWidth": width, "nativeHorizontalDrawOffset": -(width - 64) // 2,
                        "resourceSuffix": "at", "environment": "dungeon", "loop": False,
                        "framesEvidence": {"path": f"assets/txtdata/classes/{cls}/animations.tsv", "line": anim_lines[variable + "Frames"]}})
    return {"id": f"tristram.hero.{cls}", "class": cls, "animationTable": meta(directory / "animations.tsv"),
            "spriteTable": meta(directory / "sprites.tsv"), "rawAnimationValues": anim, "rawSpriteValues": sprites,
            "slots": slots, "attackVariants": attacks,
            "pathCaveat": "Native path is sorceror, class registry ID remains sorcerer." if cls == "sorcerer" else None,
            "frameHeights": "CLX resource metadata is frame-specific; never use tight native crop height as logical canvas height.",
            "equippedVariants": {"armor": [{"name": n, "char": c} for n, c in zip(("light", "medium", "heavy"), ("l", "m", "h"))],
                                 "weapons": [{"name": n, "char": c} for n, c in zip(WEAPONS, WEAPON_CHARS)],
                                 "variantCountPolicy": "Eight directions and animation frames are not separate model reservations; death is shared by weapon variants."}}


def main():
    registry = read_json(REPO / "assets/registry.json")
    inventory = read_json(REPO / "docs/own-art-pack/production-inventory-r1.json")
    inherited = [resource for resource in inventory["resources"] if resource["id"].split("/")[0] in CLASSES]
    native = [native_class(cls) for cls in CLASSES]
    handoff_path = CHARACTERS / "tripo-warrior-submission-20261009/r1/handoff.json"
    handoff = read_json(handoff_path)
    preparation_path = CHARACTERS / "tripo-warrior-submission-20261009/r1/preparation.json"
    preparation = read_json(preparation_path)
    master_path = Path(handoff["masters"][0]["path"])
    rig_path = Path(handoff["masters"][1]["path"])
    repaired_path = Path(handoff["selectedIntegrationSource"]["path"])
    r1_path = CHARACTERS / "tripo-warrior-derivative-20261009/r1/candidate/warrior-light-unarmed-r3-tripo-bind-repaired-2k.glb"
    fpp_path = CHARACTERS / "warrior-first-person-derivative-20261010/r1/warrior-first-person-forearms-hands-CANDIDATE-r1.glb"
    assets = [glb(master_path, "preserved-4k-authoring-master"), glb(rig_path, "preserved-raw-rig-bind-defect"),
              glb(r1_path, "preserved-r1-not-selected-anatomical-basis-pending"),
              glb(repaired_path, "existing-handoff-r2-private-technical-source"),
              glb(fpp_path, "existing-first-person-subset-candidate-not-full-body")]
    old_inputs = [CHARACTERS / f"tripo-warrior-input-20261008/warrior-light-unarmed-a-pose-r{revision}.png" for revision in (1, 2, 3)]
    current_masters_directory = WORKSPACE / "assets/own-art/heroes/local-r1/masters"
    current_raster_paths = sorted(path for path in current_masters_directory.glob("*.png")
                                  if path.name.startswith(("rogue-", "sorcerer-")))
    current_raster_masters = []
    authoring_selections = {"rogue-light-unarmed-directions-r1.png", "sorcerer-light-unarmed-directions-r2.png"}
    for path in current_raster_paths:
        raw = path.read_bytes()
        assert raw.startswith(b"\x89PNG\r\n\x1a\n") and raw[12:16] == b"IHDR", str(path)
        width, height, bit_depth, color_type = struct.unpack_from(">IIBB", raw, 16)
        current_raster_masters.append({**meta(path), "dimensions": [width, height], "bitDepth": bit_depth,
                                      "pngColorType": color_type, "status": "candidate-stills-not-motion",
                                      "selectedForAuthoringByRoot": path.name in authoring_selections,
                                      "registrySelection": False, "nativeClipCountProven": 0,
                                      "selectionAuthority": "Root handoff: Rogue directions-r1 / Sorcerer directions-r2; final records in manifest.json."})
    metadata_inputs = [REPO / "AGENTS.md", REPO / "docs/PROJECT-EXECUTION.md", REPO / "docs/CHARACTERS-RIGS-ANIMATION.md",
                       REPO / "assets/registry.json", REPO / "docs/own-art-pack/production-inventory-r1.json", handoff_path, preparation_path,
                       CHARACTERS / "ownership.json", CHARACTERS / "tripo-warrior-derivative-20261009/r1/bind-candidate-status.json",
                       CHARACTERS / "tripo-warrior-derivative-20261009/r2/bind-basis-repaired-2k-receipt.json",
                       CHARACTERS / "tripo-warrior-validation-20261009/independent-bind-r2/report.json",
                       CHARACTERS / "warrior-first-person-inventory-20261010/r1/reservation.json",
                       CHARACTERS / "warrior-first-person-derivative-20261010/r1/reservation.json",
                       CHARACTERS / "class-preview-existing-assets-20261009/r1/rogue-sorcerer-audit.json"]
    evidence = [meta(path) for path in metadata_inputs]
    glb_inventory = subprocess.run(["rg", "--files", "-uuu", "models", "assets", "data"], cwd=WORKSPACE,
                                  check=True, capture_output=True, text=True).stdout.splitlines()
    model_extensions = {".glb", ".gltf", ".fbx", ".blend"}
    all_model_paths = [path for path in glb_inventory if Path(path).suffix.lower() in model_extensions]
    identified_paths = [path for path in all_model_paths if any(name in path.lower() for name in ("warrior", "rogue", "sorcerer", "sorceror", "hero"))]
    state_counts = Counter()
    state_frames = Counter()
    for resource in inherited:
        state_counts[resource["id"].split("/")[3]] += 1
        state_frames[resource["id"].split("/")[3]] += resource["frameCount"]
    report = {
        "schema": "diablo3d-own-hero-contract-audit/v1", "localDate": "2026-10-12", "timezone": "America/Sao_Paulo",
        "generatedAtUtc": datetime.now(timezone.utc).isoformat(), "status": "source-and-existing-assets-audit-not-generated-pack",
        "ownerScope": "Hero audit only; global registry/Source/build/install/NPCs preserved.",
        "authority": "Explicit hero-only local delegation; existing guide and native source contracts remain authoritative.",
        "currentGameplayRule": {"source": "Latest explicit Douglas instruction relayed by principal on 2026-10-12",
                                "rule": "Gameplay uses ONLY SPRITES for every hero and NPC, including Warrior and Ogden, until correct rig plus animations have been validated.",
                                "allHeroesAndNpcs": True, "warriorAndOgdenIncluded": True,
                                "currentRasterStillsAuthorizeMeshGameplay": False,
                                "existingRigOrAcceptedStaticAppearanceAuthorizesAnimatedMeshGameplay": False,
                                "mastersAndPriorAppearanceRevisionsPreserved": True,
                                "runtimePolicyAppliedByThisAudit": False,
                                "implementationOwner": "principal; audit records the requirement and does not change Source/install"},
        "evidence": evidence,
        "native": {"frameIndexBase": 0, "defaultTicksPerSecond": 20, "profileSpeedRequired": 32,
                   "timingWarning": "N*delay/20 is nominal slot span, never fixed mode lifetime. Read active AnimationInfo effective frame/fraction; skip/distribution/recovery/block/hold/chain are native.",
                   "directionOrder": [{"nativeIndex": i, "name": name, "atlasToken": DIRECTION_TOKENS[i]} for i, name in enumerate(DIRECTIONS)],
                   "directionEvidence": source_line("Source/engine/direction.hpp", "enum class Direction"),
                   "graphics": [{"nativeIndex": i, "name": n} for i, n in enumerate(("Stand", "Walk", "Attack", "Hit", "Lightning", "Fire", "Magic", "Death", "Block"))],
                   "pivot": {"logicalTile": [64, 32], "nativeHorizontalCenterFunction": "(spriteWidth - 64)/2",
                             "nativeDrawOffset": "{-CalculateSpriteTileCenterX(sprite.width()),0}+GetOffsetForWalking when walking",
                             "nativeAnchor": "CLX raster draw uses bottom-left sprite position relative to native tile render origin, not image center.",
                             "independentRgbaAtlasAnchor": "An exporter may define top-left origin, pivot=(canvasWidth/2,canvasHeight-1) at footpoint, but must preserve the explicit conversion to native tile/CLX origin.",
                             "heightAndCrop": "Preserve logical frame height and pad/crop offsets; cropped bounding box is not a ground pivot. Do not align each pose by its changing opaque bounds.",
                             "worldFootpoint": "(tile.x,0,tile.y); walking: x+=(2*offsetY+offsetX)/64,z+=(2*offsetY-offsetX)/64",
                             "uniformScale": True,
                             "evidence": [source_line("Source/levels/dun_tile.hpp", "constexpr int CalculateSpriteTileCenterX"),
                                          source_line("Source/player.h", "getRenderingOffset(const ClxSprite sprite)"),
                                          source_line("Source/engine/render/town_view.cpp", "Vec3 PlayerPosition(")]},
                   "classes": native,
                   "specialRules": ["No native Run graphic: town running alters native walk entry/speed. A 'run' atlas needs explicit mapping, not a new simulation mode.",
                                    "Attack/Hit/Block are not loaded in Town in the audited main Source; the principal's isolated Town attack change needs its own source/build closure.",
                                    "Death forces unarmed art but retains armor. Warrior medium/heavy death has 15 frames; Warrior bow idle in dungeon has 8.",
                                    "Spell has three resource slots lm/fm/qm. Generic 'cast' does not prove coverage of all three.",
                                    "Melee swing=A-2, impact/arrow=A-1 and chain>=A; spell release=SF, without -1. Real damage/projectiles/audio are never fired by generated clips.",
                                    "Head/eyes/socket transforms and footprint scale must be measured per master; bones with matching names do not prove retarget compatibility."],
                   "codeEvidence": [source_line("Source/player.cpp", "void LoadPlrGFX("), source_line("Source/player.cpp", "void SetPlrAnims("),
                                    source_line("Source/player.cpp", "void Player::getAnimationFramesAndTicksPerFrame("),
                                    source_line("Source/player.cpp", "player.AnimInfo.currentFrame == player._pAFNum - 1"),
                                    source_line("Source/player.cpp", "player.AnimInfo.currentFrame == player._pSFNum")]},
        "registrySnapshot": [{key: entry.get(key) for key in ("id", "phase", "status", "implementation", "approvedRevision", "assetPath", "claimIssue", "claimOwner")}
                             for entry in registry["entries"] if entry["category"] == "hero"],
        "inheritedInventory": {"source": meta(REPO / "docs/own-art-pack/production-inventory-r1.json"),
                               "clipsByClass": dict(Counter(resource["id"].split("/")[0] for resource in inherited)),
                               "totalClips": len(inherited), "totalRequestedFrames": sum(resource["frameCount"] for resource in inherited),
                               "variants": sorted(set("/".join(resource["id"].split("/")[1:3]) for resource in inherited)),
                               "states": [{"name": name, "clips": state_counts[name], "requestedFrames": state_frames[name]} for name in sorted(state_counts)],
                               "directions": sorted(set(resource["id"].split("/")[4] for resource in inherited)),
                               "coverageWarning": "Reference metadata only: Warrior light armor, four weapons, no Rogue/Sorcerer/medium/heavy/bow/axe/mace/staff. Not a complete native contract and not generated art.",
                               "mismatchedFramesPerDirection": {"stand": {"inventory": 20, "nativeWarriorLight": 10}, "town_stand": {"inventory": 40, "native": 20},
                                                                "walk": {"inventory": 24, "native": 8}, "town_walk": {"inventory": 24, "native": 8},
                                                                "hit": {"inventory": 12, "nativeWarrior": 6}, "block": {"inventory": 12, "nativeWarrior": 2},
                                                                "cast": {"inventory": 24, "nativeWarrior": 20}, "death": {"inventory": 24, "nativeWarriorLight": 20},
                                                                "run": {"inventory": 24, "nativeGraphic": None}},
                               "referenceRecords": inherited, "pixelsReadOrCopied": False},
        "existingOwnAssets": assets,
        "existingOwnConcepts": [{**meta(path), "revision": i + 1, "selectedForHistoricalSingleTripoInput": i == 2,
                                 "status": "concept-not-animated-clip", "pixelsCopiedByThisAudit": False} for i, path in enumerate(old_inputs)],
        "currentRasterAuthoring": {"beforeThisLot": "Historical class audit found no own Rogue/Sorcerer 3D master, rig or clip. The prior absence text described records before authoring in this local lot.",
                                  "currentMasters": current_raster_masters,
                                  "deliveryManifest": str(HERE / "manifest.json"), "deliveryManifestOwner": "root",
                                  "rootReportedFinalStillDirectionTarget": 24,
                                  "targetBreakdown": {"warriorBindStills": 8, "rogueCandidateStills": 8, "sorcererCandidateStills": 8},
                                  "stillDirectionsAreAnimationFrames": False,
                                  "currentRogueSorcererGlbRigMotion": "No class-identified GLB/rig/native-compatible motion clip found in this audit; new PNG masters are candidate raster stills.",
                                  "verificationScope": "PNG signatures/IHDR metadata and hashes only; alpha/direction QA and selections are root/subagent deliverables, not re-audited as animation here."},
        "reservationReconciliation": {
            "oldOwnership": {"evidence": meta(CHARACTERS / "ownership.json"), "warriorText": "no new submission; complete pilot and historical asset/hash audit first",
                             "interpretation": "Historical 2026-10-08 record superseded for the single completed Warrior operation by 2026-10-09 preparation/handoff. Its NPC reservation is preserved, not reassigned."},
            "completedWarrior": {"providerVisibleAssetId": preparation["providerVisibleAssetId"], "jobId": preparation["jobId"],
                                 "status": preparation["submissionStatus"], "historicalConfirmedCredits": preparation["confirmedCreditsSpent"],
                                 "model": {"submissions": 1, "trialCost": 0}, "texture": {"submissions": 1, "credits": 20},
                                 "rig": {"submissions": 1, "credits": 20}, "animation": {"submissions": 0, "credits": 0},
                                 "internalStageTaskIds": preparation["internalMeshTextureRigTaskIds"], "repeatOrRetryAllowed": False},
            "oldInputReceiptRemoteSubmissionFalse": "Preparation/handoff and exact downloaded master hashes prove completion; the older input receipt is a pre-submission snapshot, not an unresolved request to resubmit.",
            "downloadTimeout": "4K nonrigged master listener timed out, but existing bytes/hash were confirmed. No uncertain generation remains in this record.",
            "browser2kExport": {"status": preparation["browser2kExport"]["status"], "downloadExists": False, "paidCost": 0,
                                 "resolution": "Existing local 2K r2 derivative is the handoff source; do not repeat provider generation."},
            "revisionPolicy": {"rawRig": "preserve; do not animate/export as usable rest pose without repair", "r1Bind": "preserve, not selected; anatomical basis defect documented",
                               "r2BindBasis": "existing principal handoff source, not registry/human full-asset approval", "firstPersonSubset": "separate reviewed candidate; not full hero coverage"},
            "oldFirstPersonReservations": [meta(CHARACTERS / "warrior-first-person-inventory-20261010/r1/reservation.json"),
                                           meta(CHARACTERS / "warrior-first-person-derivative-20261010/r1/reservation.json")],
            "rogueSorcerer": "Before authoring this local lot, no local class-identified own 3D master/rig/clip was found in historical class records. Current root-created PNG masters are listed in currentRasterAuthoring; no GLB/rig/native-compatible motion coverage is inferred. Unlabelled/remote/archived jobs were not queried; coordinate cloud reservations before paid submission.",
            "currentCostAuthorization": "No new Meshy/Tripo expenses. This audit uses only local read operations."},
        "localSearch": {"roots": ["models", "assets", "data"], "modelFileCount": len(all_model_paths),
                        "classAliasMatches": identified_paths, "historicalClassAudit": meta(CHARACTERS / "class-preview-existing-assets-20261009/r1/rogue-sorcerer-audit.json"),
                        "limits": "Not a proof of universal absence: diagnostics Warrior files are explicitly inspected; remote and unlabeled assets are outside this name audit."},
        "hellfire": {"status": "conditional-not-produced-by-this-audit", "classes": ["monk", "bard", "barbarian"],
                     "rules": "Bard may use Rogue and Barbarian Warrior sprite fallback when edition assets are unavailable; own class appearance/timings/TRN still require explicit scope and qualification."},
        "closureForPrincipal": [
            {"step": 1, "owner": "heroes + cloud pack", "requirement": "Confirm cloud reservations/jobs and class/variant IDs; reuse Warrior r2/master/concept hashes before new own raster authoring."},
            {"step": 2, "owner": "heroes", "requirement": "Produce actual distinct native-compatible motion or explicitly missing clips. Provide named clips and all 8 direction renders, pivots/crop offsets/alpha/frames/hash/provenance. Concept sheets and repeated poses never count as motion."},
            {"step": 3, "owner": "heroes + item attachments", "requirement": "Cover light/medium/heavy and nine weapon graphics; preserve armor on forced-unarmed death, per-class frames, shield guard and three spell slots. Separate body masters, attachments and derivatives."},
            {"step": 4, "owner": "principal", "requirement": "Enforce the latest human rule: gameplay ONLY SPRITES for every hero/NPC, including Warrior/Ogden, until correct rig plus animations are validated. Preserve model masters and prior appearance reviews. Freeze exact Source, normal EXE, pack, asset manifest and isolated Speed32 profile; record policy-applied state separately from this audit. Resolve native CL2 logical frame sizes/anchors from metadata, map resampling to active native frame/fraction, and avoid simulation/RNG writes."},
            {"step": 5, "owner": "principal", "requirement": "Bind authored pack through the existing loader/container, validate RGB/alpha/TRN/cache reset and source absence, then check native state/action timing, NEW/save/load/Town-Dungeon, facing and equipment changes; no Python/converter at player install."},
            {"step": 6, "owner": "principal + human reviewer", "requirement": "Inspect native-angle and 360 art, exact temporal poses, feet/ground, view direction, close/low camera and attachments. Keep generated/validated/integrated/accepted separate; only principal changes global registry selection/install."}],
        "operations": {"providerCalls": 0, "paidGenerations": 0, "imageGeneration": 0, "pixelsCopied": 0,
                       "sourceBuildInstallRegistryEdits": 0, "npcEdits": 0, "writes": ["contract_audit.py", "contract-audit.json", "contract-audit.md"]},
    }
    checks = []
    for source in handoff["masters"] + [handoff["selectedIntegrationSource"]]:
        actual = meta(Path(source["path"]))
        checks.append({"name": "existing-handoff-asset-hash", "path": source["path"], "passed": actual["sha256"] == source["sha256"] and actual["bytes"] == source["bytes"]})
    for source in evidence:
        checks.append({"name": "audit-input-preserved", "path": source["path"], "passed": meta(Path(source["path"]))["sha256"] == source["sha256"]})
    checks.append({"name": "three-class-native-tables-27-fields", "passed": all(len(cls["rawAnimationValues"]) == 27 for cls in native)})
    checks.append({"name": "warrior-existing-assets-zero-clips", "passed": all(asset["clipCount"] == 0 for asset in assets)})
    checks.append({"name": "inherited-320-clips-warrior-only", "passed": len(inherited) == 320 and {r["id"].split("/")[0] for r in inherited} == {"warrior"}})
    for raster in current_raster_masters:
        checks.append({"name": "current-root-raster-master-preserved", "path": raster["path"],
                       "passed": meta(Path(raster["path"]))["sha256"] == raster["sha256"]})
    report["auditChecks"] = {"checks": len(checks), "passed": sum(check["passed"] for check in checks), "failed": sum(not check["passed"] for check in checks), "details": checks,
                             "claim": "Local metadata/hash/GLB audit only; no gameplay, rendered motion or artistic approval test."}
    assert report["auditChecks"]["failed"] == 0, report["auditChecks"]
    (HERE / "contract-audit.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = ["# Auditoria dos heróis — 12/10/2026", "",
             "Escopo local de diagnóstico para Warrior → Rogue → Sorcerer. Nenhum Source, build, instalação, seleção global ou NPC foi alterado. O JSON contém hashes, tabelas e metadados completos; esta auditoria não é um pack gerado.", "",
             "**Regra humana vigente, 12/10/2026:** gameplay usa **somente sprites para todos os heróis e NPCs, inclusive Warrior e Ogden**, até rig e animações corretos serem validados. Esta auditoria registra o requisito; aplicação na Source/build/instalação pertence ao principal. Masters e revisões anteriores de aparência permanecem preservados; skin, aceitação de aparência estática ou stills novos não autorizam gameplay por mesh animada.", "",
             "## Reaproveitamento e reservas", "",
             f"O Warrior Tripo já foi produzido na operação `{preparation['providerVisibleAssetId']}`, encerrada em 40 créditos históricos (mesh trial 0, textura 20, rig 20). Os recibos antigos de input/ownership são snapshots anteriores; não autorizam nem exigem repetir a geração. O export 2K bloqueado no navegador foi resolvido pelo derivado local existente.", "",
             "| Fonte preservada | SHA-256 | Conteúdo |", "| --- | --- | --- |"]
    for asset in assets:
        triangles = sum(p["triangles"] for p in asset["primitives"])
        lines.append(f"| `{Path(asset['path']).name}` | `{asset['sha256']}` | {triangles:,} triângulos; {asset['jointCount']} joints; {asset['clipCount']} clips |")
    lines += ["", "O rig bruto tem nodes sem TRS e rest pose defeituosa. O r1 corrigia cancelamento algébrico, mas foi explicitamente não selecionado por base anatômica divergente. O r2, SHA `1c09cc5e55fbc1f6de5d5db94adae2cbb52a2842e5d1b300b3913f98a4d28507`, é a fonte do handoff técnico existente; base B(x,y,z)=(y,z+0,499755859375,x) inferida e validada, sem aprovação artística/integral automática. Os 13 terminais sem IBM têm posições autorais não comprovadas. Preserve os masters 4K e use o derivado 2K identificado. Nenhuma dessas cinco fontes contém animação.", "",
              f"Conceito r3 selecionado historicamente: `{old_inputs[2]}`, SHA `{meta(old_inputs[2])['sha256']}`. R1/R2 foram preservados. Um conceito A-pose não é idle, walk ou ataque.", "",
              "Antes do authoring neste lote local, os registros auditados não identificavam GLB/rig/clips próprios de Rogue/Sorcerer. Agora a frente raiz produziu masters raster PNG próprios em `D:/Diablo 1 3D/assets/own-art/heroes/local-r1/masters`; o JSON lista hashes e metadados atuais. Rogue directions-r1 e Sorcerer directions-r2 são seleções de authoring **candidate-stills**, sem seleção no registry. O manifesto da raiz é `docs/own-art-pack/heroes-local-r1/manifest.json`. A meta reportada pela raiz é 24 direções estáticas (Warrior8/Rogue8/Sorcerer8); direções estáticas e frames derivados não são animação. Não foram identificados GLB/rig/ações compatíveis nativas de Rogue/Sorcerer nesta auditoria. Referências privadas originais de corpo permanecem separadas e nenhum de seus pixels é incorporado. Ausência continua limitada aos nomes/recibos consultados, sem inventário remoto ou arquivos não identificados; gastos novos Meshy/Tripo não estão autorizados.", "",
              "## Contrato nativo", "", "Direções de índice 0 a 7: **S, SW, W, NW, N, NE, E, SE** (`Source/engine/direction.hpp`). Nove enums gráficos geram onze slots de recursos, pois Stand/Walk têm Town e dungeon separados. O JSON lista ataques de todas as nove classes gráficas de armas, larguras, suffixes e guards.", "",
              "| Classe | Idle dungeon/town | Walk dungeon/town | Hit | Block | Death light | Spell / release SF |", "| --- | --- | --- | --- | --- | --- | --- |"]
    for cls in native:
        a = cls["rawAnimationValues"]
        lines.append(f"| {cls['class']} | {a['idleFrames']} / {a['townIdleFrames']} | {a['walkingFrames']} / {a['townWalkingFrames']} | {a['recoveryFrames']} | {a['blockingFrames']} | {a['deathFrames']} | {a['castingFrames']} / {a['castingActionFrame']} |")
    lines += ["", "Pivô nativo: `CalculateSpriteTileCenterX(width)=(width-64)/2`, deslocamento de desenho `{-centerX,0}` mais `GetOffsetForWalking`. CLX usa posição no canto inferior esquerdo relativamente à origem nativa do tile. Em atlas RGBA independente, documentar explicitamente a conversão para o footpoint `(canvasWidth/2,canvasHeight-1)`; não confundir o corte opaco com a altura lógica ou recentrar cada pose. Dimensões lógicas dependem do recurso/frame; larguras nativas estão no JSON. Fonte Sorcerer usa path **sorceror**.", "",
              "Tick padrão é 20/s; perfil habitual é Speed32. Idle usa 4 ticks/frame, walk/attack/spell/hit 1, block 3, death 2 antes de segurar a pose. Essas durações são extensões nominais, não vida fixa do modo. Impacto/flecha=A−1; magia=SF sem −1. Não criar relógio, RNG, dano, som, colisão ou root motion visual independentes. Warrior com arco usa idle dungeon8; morte medium/heavy15; Death força arma unarmed sem apagar armor. Block depende de `_pBlockFlag`, magia usa **lm/fm/qm** e troca sword/mace por unarmed em Town. Run não é um enum gráfico nativo. Attack/Hit/Block em Town exigem a closure específica da Source isolada do principal.", "",
              "## Lacunas do inventário r1", "", f"O r1 traz **320 clips / {sum(resource['frameCount'] for resource in inherited):,} frames de referência**, somente Warrior light com sword/sword_shield/unarmed/unarmed_shield, dez rótulos em oito direções. Não cobre Rogue/Sorcerer, medium/heavy, bow/axe/mace/staff. Contagens de stand20/town_stand40/walk24/hit12/block12/cast24/death24 divergem dos slots nativos; somente os ataques16 dessas quatro variantes coincidem. O rótulo cast não prova os três recursos de magia e run não cria modo nativo. Cells/pivôs do r1 permanecem metadados de referência, sem pixels lidos/copied e sem validar a matriz nativa. Não preencher clips ausentes duplicando a A-pose.", "",
              "## Closure de integração do principal", ""]
    for step in report["closureForPrincipal"]:
        lines.append(f"{step['step']}. **{step['owner']}** — {step['requirement']}")
    lines += ["", "Hellfire permanece condicional: Monk/Bard/Barbarian não estão produzidos nesta entrega; fallback de Bard/Rogue e Barbarian/Warrior não comprova a identidade/timing de cada classe.", "",
              f"Verificação: {len(checks)} checks de metadados/hash, zero falhas. Não foi executado jogo, render de movimento, GPU ou aceitação artística. Nenhuma chamada a provedor, geração, retry ou upload.", ""]
    (HERE / "contract-audit.md").write_text("\n".join(lines), encoding="utf-8")
    print(json.dumps({"status": report["status"], "checks": report["auditChecks"]["checks"],
                      "failed": report["auditChecks"]["failed"], "outputs": [meta(HERE / "contract-audit.json"), meta(HERE / "contract-audit.md")],
                      "warriorExistingClips": [asset["clipCount"] for asset in assets], "inheritedFrames": report["inheritedInventory"]["totalRequestedFrames"]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
