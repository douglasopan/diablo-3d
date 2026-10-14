"""Sequence the public item-concept study; --check never writes files.

228 production units are design identities/families/variants, not 228 meshes.
This queue owns no reservations, approval, selection, models or runtime state.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys

OUT = Path(__file__).resolve().parent
BASE = "item.design.base."
UNIQUE = "item.design.unique."
INITIAL = tuple(BASE + name for name in (
    "short-sword", "dagger", "falchion", "scimitar", "broad-sword",
    "potion-of-healing", "potion-of-mana", "gold", "scroll"))
RECOMMENDED = (
    ("next-01-blades", tuple(BASE + name for name in (
        "claymore", "blade", "sabre", "long-sword", "bastard-sword", "two-handed-sword", "great-sword"))),
    ("next-02-shields", tuple(BASE + name for name in (
        "buckler", "small-shield", "large-shield", "kite-shield", "tower-shield", "gothic-shield"))),
    ("next-03-blade-uniques", tuple(UNIQUE + name for name in (
        "griswold-s-edge", "gonnagal-s-dirk", "gryphon-s-claw", "black-razor", "the-defender"))),
    ("next-04-axes", tuple(BASE + name for name in (
        "small-axe", "axe", "large-axe", "broad-axe", "battle-axe", "great-axe"))
        + (UNIQUE + "the-butcher-s-cleaver",)),
    ("next-05-restoration", tuple(BASE + name for name in (
        "potion-of-full-healing", "potion-of-full-mana", "potion-of-rejuvenation", "potion-of-full-rejuvenation"))),
    ("next-06-books-oil", tuple(BASE + name for name in (
        "spellbook-blue", "spellbook-red", "spellbook-grey", "oil-flask"))),
)
FAMILIES = (
    "blunt-weapons", "bows", "staves", "headgear", "body-armor", "jewelry",
    "potions-elixirs", "quest-objects", "axes", "blades", "shields", "gold",
    "ears", "spellbooks", "oils", "runes", "scrolls")
KINDS = ("base-design", "runtime-appearance", "unique-identity")
ARMOR_SETS = (
    ("textile", {BASE + name for name in ("cape", "rags", "cloak", "robe")}),
    ("flexible", {BASE + name for name in (
        "quilted-armor", "leather-armor", "hard-leather-armor", "studded-leather-armor")}),
)
EAR = BASE + "ear"
EAR_VARIANTS = tuple("item.design.dynamic.ear-" + name for name in ("sorcerer", "warrior", "rogue"))
LIGHTFORGE = UNIQUE + "lightforge"
# Public IDs/cursors from the reference-availability audit of 2026-10-08.
# No private manifest, extracted image or absolute path is read or copied.
HELLFIRE_MISSING = dict(line.split(",") for line in """
item.design.unique.bovine-plate,BOVINE
item.design.unique.giant-s-knuckle,GIANTS_KNUCKLE
item.design.unique.mercurial-ring,MERCURIAL_RING
item.design.unique.xorine-s-ring,XORINES_RING
item.design.unique.karik-s-ring,KARIKS_RING
item.design.unique.ring-of-magma,RING_OF_MAGMA
item.design.unique.ring-of-the-mystics,RING_OF_THE_MYSTICS
item.design.unique.ring-of-thunder,RING_OF_THUNDER
item.design.unique.amulet-of-warding,AMULET_OF_WARDING
item.design.unique.gnat-sting,GNAT_STING
item.design.unique.flambeau,FLAMBEAU
item.design.unique.armor-of-gloom,ARMOR_OF_GLOOM
item.design.unique.blitzen,BLITZEN
item.design.unique.thunderclap,THUNDERCLAP
item.design.unique.eater-of-souls,EATER_OF_SOULS
item.design.unique.diamondedge,DIAMONDEDGE
item.design.unique.bone-chain-armor,BONE_CHAIN_ARMOR
item.design.unique.demon-plate-armor,DEMON_PLATE_ARMOR
item.design.unique.acolyte-s-amulet,ACOLYTES_AMULET
item.design.unique.gladiator-s-ring,GLADIATORS_RING
item.design.base.rune-bomb,RUNE_BOMB
item.design.base.theodore,THEODORE
item.design.base.auric-amulet,AURIC_AMULET
item.design.base.torn-note-1,TORN_NOTE_1
item.design.base.torn-note-2,TORN_NOTE_2
item.design.base.torn-note-3,TORN_NOTE_3
item.design.base.reconstructed-note,RECONSTRUCTED_NOTE
item.design.base.brown-suit,BROWN_SUIT
item.design.base.grey-suit,GREY_SUIT
item.design.base.rune-of-fire,RUNE_OF_FIRE
item.design.base.rune-of-lightning,RUNE_OF_LIGHTNING
item.design.base.greater-rune-of-fire,GREATER_RUNE_OF_FIRE
item.design.base.greater-rune-of-lightning,GREATER_RUNE_OF_LIGHTNING
item.design.base.rune-of-stone,RUNE_OF_STONE
""".strip().splitlines())
IMAGE = re.compile(r"assets/concept-art/items/[a-z0-9][a-z0-9-]*/[a-z0-9][a-z0-9._-]*\.png")
SHA = re.compile(r"[0-9a-f]{64}")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "Duplicate JSON key: " + key)
        result[key] = value
    return result


def load(data):
    return json.loads(data.decode("utf-8-sig"), object_pairs_hook=unique_object)


def build(catalog, overlay, catalog_sha, overlay_sha):
    require(catalog.get("format") == "d3d.item-production-study", "Unsupported catalog format")
    require(overlay.get("format") == "d3d.item-concept-art", "Unsupported concept overlay format")
    require(overlay.get("catalogSha256") == catalog_sha, "Concept overlay catalog hash is stale")
    rows = catalog["productionUnits"]
    units = {unit["id"]: unit for unit in rows}
    require(len(rows) == len(units) == 228, "Expected 228 distinct production IDs")
    native = {row["cursorName"]: row for row in catalog["nativeVisuals"]}
    require(len(native) == len(catalog["nativeVisuals"]) == 170, "Expected 170 distinct native cursors")
    require(set(FAMILIES) == {unit["family"] for unit in rows}, "Family taxonomy changed")
    require({unit["kind"] for unit in rows} == set(KINDS), "Kind taxonomy changed")
    blocked = set(HELLFIRE_MISSING) | {LIGHTFORGE}
    require(len(HELLFIRE_MISSING) == 34 and len(blocked) == 35 and blocked <= units.keys(), "Invalid blocker coverage")
    for identity, cursor in HELLFIRE_MISSING.items():
        require(units[identity]["nativeCursorNames"] == [cursor], "Blocked cursor changed: " + identity)
    require(units[LIGHTFORGE]["nativeCursorNames"] == [], "Lightforge source resolution changed; review blocker")
    require(units[EAR]["nativeCursorNames"] == [] and set(EAR_VARIANTS) <= units.keys(), "Ear family mapping changed")
    for unit in rows:
        names = unit["nativeCursorNames"]
        require(all(name in native for name in names), "Unknown cursor: " + unit["id"])
        require(bool(names) or unit["id"] in (EAR, LIGHTFORGE), "New unresolved reference: " + unit["id"])

    concepts = {identity: [] for identity in units}
    seen = set()
    for entry in overlay["entries"]:
        identity, revision = entry["productionId"], entry["revision"]
        require(identity in units, "Unknown concept production ID")
        require(isinstance(revision, str) and re.fullmatch(r"[a-z0-9][a-z0-9-]{0,63}", revision), "Invalid concept revision")
        require((identity, revision) not in seen, "Duplicate concept production ID/revision")
        seen.add((identity, revision))
        require(entry.get("status") == "candidate" and entry.get("visualApproval") is False, "Overlay must contain unapproved candidates")
        require(entry.get("conceptId") == identity + ".concept." + revision, "Invalid concept identity")
        require(isinstance(entry.get("image"), str) and IMAGE.fullmatch(entry["image"]), "Concept image must be a public relative path")
        require(isinstance(entry.get("sha256"), str) and SHA.fullmatch(entry["sha256"]), "Invalid concept image hash")
        concepts[identity].append({key: entry[key] for key in (
            "conceptId", "revision", "status", "kind", "image", "sha256")})
    require(all(concepts[identity] for identity in INITIAL), "Initial batch handoff is incomplete; do not regenerate it")
    for entries in concepts.values():
        entries.sort(key=lambda entry: entry["revision"])

    entries, groups, assigned = [], [], set()
    sequence = 0

    def add_group(group_id, identities, classification, held=False):
        nonlocal sequence
        require(bool(identities) and (len(identities) <= 7 or classification == "initial-batch"), "Invalid group size")
        require(not (set(identities) & assigned) and len(set(identities)) == len(identities), "Duplicate queue assignment")
        require(set(identities) <= units.keys(), "Unknown queued production ID")
        require(all((identity in blocked) == held for identity in identities), "Group mixes held and available units")
        groups.append({"id": group_id, "classification": classification, "heldForReference": held,
                       "units": len(identities), "existingBatch": classification == "initial-batch"})
        for position, identity in enumerate(identities, 1):
            unit = units[identity]
            if not held:
                sequence += 1
            item = {"productionId": identity, "name": unit["name"], "family": unit["family"], "kind": unit["kind"],
                    "classification": classification, "groupId": group_id, "positionInGroup": position,
                    "sequenceIndex": None if held else sequence,
                    "referenceStatus": "blocked" if held else "available-indirect" if identity == EAR else "available-direct",
                    "nativeCursorNames": unit["nativeCursorNames"], "blockerCode": None,
                    "conceptStatus": "candidate" if concepts[identity] else "not-generated",
                    "needsConcept": not bool(concepts[identity]), "conceptEntries": concepts[identity]}
            if identity == LIGHTFORGE:
                item["blockerCode"] = "unresolved-lightforge-source"
            elif held:
                item["blockerCode"] = "missing-hellfire-objcurs2"
            if identity == EAR:
                item["indirectReferenceProductionIds"] = list(EAR_VARIANTS)
            entries.append(item)
        assigned.update(identities)

    add_group("initial-batch-09", INITIAL, "initial-batch")
    for group_id, identities in RECOMMENDED:
        add_group(group_id, identities, "recommended-next")
    for held in (False, True):
        for family in FAMILIES:
            for kind in KINDS:
                candidates = sorted(identity for identity, unit in units.items() if identity not in assigned
                                    and unit["family"] == family and unit["kind"] == kind and (identity in blocked) == held)
                subsets = [("all", candidates)]
                if family == "body-armor" and kind == "base-design":
                    subsets = [(label, [i for i in candidates if i in members]) for label, members in ARMOR_SETS]
                    subsets.append(("rigid", [i for i in candidates if not any(i in members for _, members in ARMOR_SETS)]))
                for label, identities in subsets:
                    for offset in range(0, len(identities), 7):
                        group_id = f"{'hold' if held else 'remaining'}-{family}-{kind}-{label}-{offset // 7 + 1:02d}"
                        add_group(group_id, identities[offset:offset + 7], "remaining-family", held)
    require(assigned == units.keys() and len(entries) == 228, "Queue does not cover catalog exactly once")
    classes = Counter(entry["classification"] for entry in entries)
    refs = Counter(entry["referenceStatus"] for entry in entries)
    require(dict(classes) == {"initial-batch": 9, "recommended-next": 33, "remaining-family": 186}, "Classification counts changed")
    require(dict(refs) == {"available-direct": 192, "available-indirect": 1, "blocked": 35}, "Reference counts changed")
    linked = sum(bool(value) for value in concepts.values())
    return {"format": "d3d.item-production-queue", "schemaVersion": 1, "date": catalog["date"],
            "scope": "Study sequencing only: 228 design identities/families/variants are not 228 independent meshes.",
            "studySequencingOnly": True, "registryAuthority": "assets/registry.json",
            "authorityPolicy": "No ownership, reservation, acceptance, selection or runtime installation is granted by this queue.",
            "sourceFiles": [{"path": "docs/items-study/catalog.json", "sha256": catalog_sha},
                            {"path": "docs/items-study/concept-art.json", "sha256": overlay_sha}],
            "referenceAudit": {"date": "2026-10-08", "privateMediaReadByGenerator": False,
                               "policy": "Availability is an audited snapshot, not visual approval; inspect each licensed reference before generation.",
                               "blockerPolicy": "34 Hellfire graphics lack local objcurs2; Lightforge has unresolved LGTFORGE source. Never substitute Bovine Plate.",
                               "indirectPolicy": "The ear master uses the three class-variant references; Shirotachi uses the available GREAT_SWORD cursor."},
            "sequencingPolicy": {"familyPriority": list(FAMILIES), "kindPriority": list(KINDS),
                                 "maximumNewGroupUnits": 7, "initialBatchException": "Existing nine-item handoff is preserved as one batch.",
                                 "conceptPolicy": "Concept status and all revisions come from the overlay. No latest revision is selected; existing candidates require review, not regeneration.",
                                 "reusePolicy": "Shared cursors, spell/affix variants and family sheets do not imply independent meshes or automatic concept approval.",
                                 "integrationGate": "Concept review precedes master production; character sockets/grip/skin and ground/equipped/icon identity remain separate gates."},
            "summary": {"productionUnits": 228, "classificationCounts": dict(classes), "referenceCounts": dict(refs),
                        "availableOrIndirectUnits": 193, "blockedUnits": 35, "linkedConceptUnits": linked,
                        "conceptEntries": len(seen), "unitsWithoutConcept": 228 - linked,
                        "pendingAvailableConceptUnits": sum(entry["needsConcept"] and entry["referenceStatus"] != "blocked" for entry in entries),
                        "groups": len(groups)},
            "groups": groups, "entries": entries}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Read-only: fail if the saved queue is missing or stale")
    args = parser.parse_args(argv)
    try:
        catalog_bytes = (OUT / "catalog.json").read_bytes()
        overlay_bytes = (OUT / "concept-art.json").read_bytes()
        queue = build(load(catalog_bytes), load(overlay_bytes), hashlib.sha256(catalog_bytes).hexdigest(), hashlib.sha256(overlay_bytes).hexdigest())
        data = (json.dumps(queue, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")
        require(not re.search(rb"[A-Za-z]:[\\/]|\\\\|file://|diagnostics/", data), "Output contains a private or absolute path")
        target = OUT / "production-queue.json"
        if args.check:
            require(target.is_file() and target.read_bytes() == data, "Production queue missing or stale; regenerate it")
        else:
            target.write_bytes(data)
        print(json.dumps({"passed": True, "mode": "check" if args.check else "write", **queue["summary"]}))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print("Production queue error: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
