"""Build the read-only production study from public game tables; no media access.

This is an inventory of source identities and proposed production units, not the
runtime asset registry, an approval database, or an installer.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
BASE = "assets/txtdata/items/itemdat.tsv"
UNIQUE = "assets/txtdata/items/unique_itemdat.tsv"
HF_UNIQUE = "mods/hf/txtdata/items/unique_itemdat.tsv"
HEADER = "Source/tables/itemdat.h"
ITEMS = "Source/items.cpp"
SPELLS = ("assets/txtdata/spells/spelldat.tsv", "mods/hf/txtdata/spells/spelldat.tsv")
AFFIXES = tuple(f"{p}/txtdata/items/item_{k}.tsv" for p in ("assets", "mods/hf") for k in ("prefixes", "suffixes"))


def slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def rows(path: str) -> list[dict]:
    return list(csv.DictReader(io.StringIO(read(path)), delimiter="\t"))


def location(path: str, line: int, native_index: int | None = None) -> dict:
    result = {"path": path, "line": line}
    if native_index is not None:
        result["nativeIndex"] = native_index
    return result


def definition(path: str, needle: str) -> dict:
    return location(path, next(i for i, line in enumerate(read(path).splitlines(), 1) if needle in line))


def parse_array(name: str, text: str) -> list:
    body = re.search(r"\b" + name + r"\[\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    if '"' in body:
        return re.findall(r'"([^"\n]*)"', body)
    return [int(value) for value in re.findall(r"-?\d+", body)]


def enum_aliases(path: str, enum_name: str) -> dict[int, list[str]]:
    body = re.search(r"enum " + enum_name + r"[^\{]*\{(.*?)\};", read(path), re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    values, aliases, current = {}, defaultdict(list), -1
    for token in body.split(","):
        token = token.strip()
        if not token:
            continue
        name, *assigned = token.split("=", 1)
        name = name.strip()
        if assigned:
            expression = assigned[0].strip()
            current = values[expression] if expression in values else int(expression)
        else:
            current += 1
        values[name] = current
        if name not in ("IDI_NONE", "IDI_FIRSTQUEST", "IDI_LASTQUEST", "IDI_NUM_DEFAULT_ITEMS", "UITEM_INVALID"):
            aliases[current].append(name)
    return dict(aliases)


def scope(index: int) -> dict:
    # Exact full-game IsItemAvailable result with testBard=false, gbIsSpawn=false.
    available = index not in (5, 22, 32, 92) and not 35 <= index <= 47 and not 83 <= index <= 86 and not 161 <= index <= 165
    return {"diabloTableEligible": available, "hellfireTableEligible": True,
            "diabloTestBardException": index in (37, 38),
            "sharewareExcluded": 62 <= index <= 70 or index in (105, 107, 108, 110, 111, 113),
            "meaning": "Table eligibility, not a guarantee of normal drop, quest availability, or spawnability."}


def family(row: dict) -> str:
    typ, misc, name = row.get("itemType", "Misc"), row.get("miscId", "NONE"), row.get("name", "")
    if typ in ("Sword", "Axe", "Bow", "Mace", "Staff", "Shield", "Helm"):
        return {"Sword": "blades", "Axe": "axes", "Bow": "bows", "Mace": "blunt-weapons", "Staff": "staves", "Shield": "shields", "Helm": "headgear"}[typ]
    if typ in ("LightArmor", "MediumArmor", "HeavyArmor"):
        return "body-armor"
    if typ in ("Ring", "Amulet") or row.get("equipType") in ("Ring", "Amulet"):
        return "jewelry"
    if typ == "Gold":
        return "gold"
    if misc in ("SCROLL", "SCROLLT"):
        return "scrolls"
    if misc == "BOOK":
        return "spellbooks"
    if misc.startswith("OIL"):
        return "oils"
    if misc in ("RUNEF", "RUNEL", "GR_RUNEF", "GR_RUNEL", "RUNES"):
        return "runes"
    if "Potion" in name or "Elixir" in name:
        return "potions-elixirs"
    if misc == "EAR":
        return "ears"
    return "quest-objects"


def equipment(row: dict) -> dict:
    typ, loc = row.get("itemType"), row.get("equipType")
    native_weapon = {"Sword": ["Sword", "SwordShield"], "Axe": ["Axe"], "Bow": ["Bow"], "Mace": ["Mace", "MaceShield"], "Staff": ["Staff"], "Shield": ["UnarmedShield", "SwordShield", "MaceShield"]}.get(typ, [])
    armor = {"LightArmor": "Light", "MediumArmor": "Medium", "HeavyArmor": "Heavy"}.get(typ)
    socket = "hand-primary" if row.get("class") == "Weapon" else "hand-secondary" if typ == "Shield" else "head" if loc == "Helm" else "chest-skin" if loc == "Armor" else "finger-or-neck" if loc in ("Ring", "Amulet") else None
    return {"nativeWeaponGroups": native_weapon, "nativeArmorTier": armor,
            "nativeIndividualModelSelection": False, "proposedAttachment": socket,
            "rigOwner": "existing-characters-front" if socket else None,
            "implementationStatus": "planned-not-integrated",
            "notes": "The source selects weapon category and chest armor tier, not the exact inventory item. Head/ring/amulet do not select a separate sprite variant here."}


def build() -> dict:
    base_rows, diablo_unique, hf_unique = rows(BASE), rows(UNIQUE), rows(HF_UNIQUE)
    item_text = read(ITEMS)
    cursor_values = {name: int(value) for name, value in re.findall(r"ICURS_([A-Z0-9_]+)\s*=\s*(\d+)", read(HEADER))}
    anim_map = parse_array("ItemCAnimTbl", item_text)
    anim_names = parse_array("ItemDropNames", item_text)
    anim_lengths = parse_array("ItemAnimLs", item_text)
    base_enums = enum_aliases(HEADER, "_item_indexes")
    unique_enums = enum_aliases("Source/items.h", "_unique_items")
    production: dict[str, dict] = {}

    def unit(key: str, name: str, group: str, cursors: list[str], kind: str = "base-design") -> str:
        key = "item.design." + key
        if key not in production:
            production[key] = {"id": key, "name": name, "family": group, "kind": kind,
                               "registryParent": "tristram.item.ground-visuals", "runtimeAssetId": None,
                               "claimOwner": None, "claimIssue": None,
                               "status": "concept-needed", "accepted": False, "selected": False,
                               "sourceBaseIds": [], "sourceUniqueIds": [], "sourceDynamicIds": [],
                               "nativeCursorNames": [], "concept": {"status": "not-generated", "image": None, "sha256": None},
                               "model": {"status": "not-generated", "masterGlb": None, "sha256": None},
                               "inventoryIcon": {"status": "not-generated", "image": None, "sha256": None, "policy": "Render the accepted master model, same silhouette, materials and handedness as equipped and ground forms."},
                               "artBrief": "Isolated complete object, neutral reference lighting, no painted floor shadow; front/profile/back and isometric inventory view must describe one coherent design. No character or hand fused to the object.",
                               "requiredViews": ["front", "side", "back", "isometric-inventory"],
                               "approval": {"reference": "pending", "concept": "pending", "master3d": "pending", "inventoryIcon": "pending", "equipped": "pending", "ground": "pending"}}
        production[key]["nativeCursorNames"] = sorted(set(production[key]["nativeCursorNames"] + cursors))
        return key

    # Unique identities are semantic; mapping IDs remain profile-specific.
    unique_by_identity: dict[str, dict] = {}
    for path, profile, source_rows in ((UNIQUE, "diablo", diablo_unique), (HF_UNIQUE, "hellfire", hf_unique)):
        for index, row in enumerate(source_rows):
            key = slug(row["name"])
            native_bases = [i for i, base in enumerate(base_rows) if base["uniqueBaseItem"] == row["uniqueBaseItem"] and row["uniqueBaseItem"] != "NONE"]
            effective = sorted({row["cursorGraphic"]} if row["cursorGraphic"] else {base_rows[i]["cursorGraphic"] for i in native_bases if base_rows[i]["cursorGraphic"]})
            if key not in unique_by_identity:
                like = base_rows[native_bases[0]] if native_bases else {"itemType": "Misc", "name": row["name"]}
                design = unit("unique." + key, row["name"], family(like), effective, "unique-identity")
                unique_by_identity[key] = {"id": "item.unique." + key, "name": row["name"], "productionId": design, "profiles": [], "sourceVariants": [], "sourceBaseIds": [], "effectiveCursorNames": [], "status": "concept-needed"}
            target = unique_by_identity[key]
            target["profiles"].append(profile)
            target["sourceVariants"].append({"profile": profile, "source": location(path, index + 2, index), "mappingId": index,
                                             "sourceEnumAliases": unique_enums.get(index, []),
                                             "nativeData": row, "cursorPolicy": "explicit-override" if row["cursorGraphic"] else "inherit-runtime-base-cursor",
                                             "effectiveCursorNames": effective,
                                             "matchingBaseIndices": native_bases,
                                             "sourceResolution": "resolved" if native_bases else "orphan-unique-base-no-itemdat-match"})
            target["effectiveCursorNames"] = sorted(set(target["effectiveCursorNames"] + effective))
            production[target["productionId"]]["nativeCursorNames"] = sorted(set(production[target["productionId"]]["nativeCursorNames"] + effective))
            production[target["productionId"]]["sourceUniqueIds"] = [target["id"]]
    unique_items = list(unique_by_identity.values())
    unique_by_base: dict[str, list[dict]] = defaultdict(list)
    for unique in unique_items:
        for variant in unique["sourceVariants"]:
            if unique not in unique_by_base[variant["nativeData"]["uniqueBaseItem"]]:
                unique_by_base[variant["nativeData"]["uniqueBaseItem"]].append(unique)

    base_items = []
    for index, row in enumerate(base_rows):
        identity = f"item.base.{index:03d}." + (slug(row["id"]) if row["id"] else slug(row["name"]) or "reserved-placeholder")
        cursors = [row["cursorGraphic"]] if row["cursorGraphic"] else []
        related = unique_by_base.get(row["uniqueBaseItem"], []) if row["uniqueBaseItem"] != "NONE" else []
        special = related if row["miscId"] == "UNIQUE" else []
        if not row["name"]:
            design = None
        elif len(special) == 1:
            design = special[0]["productionId"]
        else:
            name = row["name"].strip()
            # These native records are game/stat templates of the same physical object.
            if row["miscId"] in ("SCROLL", "SCROLLT"):
                key, name = "scroll", "Scroll physical family (spell-specific markings)"
            elif row["miscId"] == "BOOK":
                key, name = "spellbook-blue", "Spellbook blue (Lightning)"
            elif row["miscId"].startswith("OIL"):
                key, name = "oil-flask", "Oil flask physical family (label variants)"
            elif row["id"] in ("IDI_BARDSWORD",):
                key, name = "short-sword", "Short Sword"
            elif row["id"] in ("IDI_SORCERER",) or index == 166:
                key, name = "short-staff", "Short Staff"
            else:
                key = slug(name)
            design = unit("base." + key, name, family(row), cursors)
        entry = {"id": identity, "nativeIndex": index, "mappingId": index, "sourceEnumId": row["id"] or None, "sourceEnumAliases": base_enums.get(index, []),
                 "name": row["name"], "source": location(BASE, index + 2, index), "nativeData": row,
                 "scope": scope(index), "recordKind": "reserved-placeholder" if not row["name"] else "non-random-template" if row["dropRate"] == "0" else "droppable-base",
                 "inventory": {"cursorName": row["cursorGraphic"] or None, "cursorValue": cursor_values.get(row["cursorGraphic"]), "dynamic": row["miscId"] in ("BOOK", "EAR") or row["itemType"] == "Gold", "pixelDimensions": None, "dimensionsStatus": "Read licensed installed sprite; not inferred from source or copied to public catalog."},
                 "equipment": equipment(row), "uniqueIds": [u["id"] for u in related], "productionId": design,
                 "productionStatus": "not-applicable" if design is None else "concept-needed"}
        base_items.append(entry)
        if design:
            production[design]["sourceBaseIds"].append(identity)
            production[design]["nativeCursorNames"] = sorted(set(production[design]["nativeCursorNames"] + cursors))
        for related_unique in related:
            if identity not in related_unique["sourceBaseIds"]:
                related_unique["sourceBaseIds"].append(identity)
            related_unit = production[related_unique["productionId"]]
            if identity not in related_unit["sourceBaseIds"]:
                related_unit["sourceBaseIds"].append(identity)
    by_index = {item["nativeIndex"]: item for item in base_items}

    dynamic = []
    def add_dynamic(key: str, name: str, cursor: str, bases: list[int], source: dict, group: str, native_rule: str, design_key: str | None = None, extra: dict | None = None):
        design = unit(design_key or "dynamic." + key, name, group, [cursor], "runtime-appearance")
        ident = "item.dynamic." + key
        production[design]["sourceDynamicIds"].append(ident)
        dynamic.append({"id": ident, "name": name, "cursorName": cursor, "cursorValue": cursor_values[cursor],
                        "sourceBaseIds": [by_index[i]["id"] for i in bases], "source": source, "nativeRule": native_rule,
                        "productionId": design, "status": "concept-needed", **(extra or {})})

    gold_source = definition(ITEMS, "int GetGoldCursor(int value)")
    for level, cursor, rule in (("small", "GOLD_SMALL", "value <=1000"), ("medium", "GOLD_MEDIUM", "1000 <value <2500"), ("large", "GOLD_LARGE", "value >=2500")):
        add_dynamic("gold-" + level, "Gold pile " + level, cursor, [0], gold_source, "gold", rule,
                    "base.gold" if level == "small" else None)
    ear_source = definition(ITEMS, "void RecreateEar(")
    for hero, cursor in (("sorcerer", "EAR_SORCERER"), ("warrior", "EAR_WARRIOR"), ("rogue", "EAR_ROGUE")):
        add_dynamic("ear-" + hero, "Ear trophy " + hero, cursor, [23], ear_source, "ears", "Native bCursval class bits choose the ear cursor; hero name is metadata, not another mesh.")
    book_source = definition(ITEMS, "void GetBookSpell(")
    for magic, cursor, key in (("Fire", "BOOK_RED", "spellbook-red"), ("Lightning", "BOOK_BLUE", "spellbook-blue"), ("Magic", "BOOK_GREY", "spellbook-grey")):
        add_dynamic("book-" + magic.lower(), "Spellbook " + magic, cursor, [114, 115, 116, 117], book_source, "spellbooks", "SpellData.type() chooses the cover; not one mesh per spell.", "base." + key)

    # OilOf is a generator, not an eleventh physical oil. Keep all ten named results.
    oil_names = re.findall(r'N_\("([^"]+)"\)', re.search(r"char OilNames\[10\]\[25\]\s*=\s*\{(.*?)\};", item_text, re.S).group(1))
    oil_misc = re.findall(r"IMISC_([A-Z]+)", re.search(r"item_misc_id OilMagic\[\]\s*=\s*\{(.*?)\};", item_text, re.S).group(1))
    oil_levels, oil_values = parse_array("OilLevels", item_text), parse_array("OilValues", item_text)
    for index, (name, misc, level, value) in enumerate(zip(oil_names, oil_misc, oil_levels, oil_values)):
        add_dynamic("oil-" + slug(name), name, "OIL", [35, 83, 84, 85, 86], definition(ITEMS, "char OilNames[10][25]"), "oils",
                    "GetOilType selects among the named oils. Multiplayer uses indices 5 and 6 only; level eligibility applies in single player.", "base.oil-flask",
                    {"nativeOilIndex": index, "miscId": misc, "minLevel": level, "value": value, "multiplayerEligible": index in (5, 6), "scope": "conditional-hellfire"})

    spell_variants = []
    for path, profile in zip(SPELLS, ("diablo", "hellfire")):
        for index, row in enumerate(rows(path)):
            magic = row["flags"].split(",")[0]
            for kind, level_field in (("book", "bookLevel"), ("staff-charge", "staffLevel")):
                if int(row[level_field]) == -1:
                    continue
                cursor = {"Fire": "BOOK_RED", "Lightning": "BOOK_BLUE", "Magic": "BOOK_GREY"}[magic] if kind == "book" else None
                source_bases = [114, 115, 116, 117] if kind == "book" else [i for i, base in enumerate(base_rows) if base["itemType"] == "Staff" and base["miscId"] == "STAFF" and base["spell"] == "Null"]
                spell_variants.append({"id": f"item.spell-variant.{profile}.{kind}." + slug(row["id"]), "profile": profile, "kind": kind,
                                       "spellId": row["id"], "spellNumericId": index + 1, "name": row["name"], "source": location(path, index + 2, index + 1),
                                       "nativeData": row, "minLevel": int(row[level_field]), "magicType": magic, "cursorName": cursor,
                                       "sourceBaseIds": [by_index[i]["id"] for i in source_bases],
                                       "productionIds": sorted({by_index[i]["productionId"] for i in source_bases}) if kind == "staff-charge" else ["item.design.base.spellbook-" + {"Fire": "red", "Lightning": "blue", "Magic": "grey"}[magic]],
                                       "geometryPolicy": "reuse-physical-family", "multiplayerOnly": row["id"] in ("Resurrect", "HealOther"),
                                       "availabilityNote": "Table level !=-1, subject to runtime spell, item, level, single/multiplayer and shareware filters; not a Cartesian spawn list."})

    affixes = []
    for path in AFFIXES:
        profile = "hellfire" if path.startswith("mods/") else "diablo"
        kind = "prefix" if "prefixes" in path else "suffix"
        for index, row in enumerate(rows(path)):
            affixes.append({"id": f"item.affix.{profile}.{kind}.{index:03d}." + slug(row["name"]), "profile": profile, "kind": kind,
                            "name": row["name"], "source": location(path, index + 2, index), "nativeData": row,
                            "geometryPolicy": "reuse-base-or-unique-master-no-new-mesh-per-roll", "productionId": None,
                            "optionalVisualEffect": "future-explicit-effect-variant" if row["power"] in ("FIREDAM", "LIGHTDAM", "FIRE_ARROWS", "LIGHT_ARROWS") else None,
                            "status": "data-mapped-no-art-generation-needed"})

    native_visuals = []
    for cursor, value in sorted(cursor_values.items(), key=lambda pair: pair[1]):
        def_line = next(i for i, line in enumerate(read(HEADER).splitlines(), 1) if re.search(r"\bICURS_" + cursor + r"\s*=", line))
        uses = [b["id"] for b in base_items if b["inventory"]["cursorName"] == cursor]
        unique_uses = [u["id"] for u in unique_items if cursor in u["effectiveCursorNames"]]
        dynamic_uses = [d["id"] for d in dynamic if d["cursorName"] == cursor]
        designs = sorted({p["id"] for p in production.values() if cursor in p["nativeCursorNames"]})
        animation = anim_map[value]
        native_visuals.append({"id": "item.native-cursor." + cursor.lower().replace("_", "-"), "cursorName": cursor, "cursorValue": value,
                               "inventoryCursorId": value + 12, "combinedSpriteIndexZeroBased": value + 11,
                               "source": location(HEADER, def_line), "scope": "conditional-hellfire" if value >= 168 else "shared-or-diablo",
                               "baseItemIds": uses, "uniqueItemIds": unique_uses, "dynamicIds": dynamic_uses, "productionIds": designs,
                               "groundSprite": {"nativeAnimationIndex": animation, "sourceAssetStem": "items/" + anim_names[animation], "nativeFrameCount": anim_lengths[animation], "mediaIncluded": False},
                               "pixelDimensions": None, "newIconStatus": "concept-needed"})

    issues = [
        {"id": "item.issue.generic-equipped-weapon", "status": "code-proven", "sources": [definition(ITEMS, "PlayerWeaponGraphic GetPlrAnimWeaponId("), definition("Source/player.cpp", "const char prefixBuf[3]")], "description": "Distinct inventory weapons collapse to Sword/Axe/Bow/Mace/Staff and shield combinations. The exact inventory silhouette is absent from this selector; all item-specific attachments are planned.", "resolution": "One accepted master per approved design, referenced by ground/equipped/icon. Preserve native attack category/timing/range."},
        {"id": "item.issue.generic-equipped-armor", "status": "code-proven", "sources": [definition(ITEMS, "PlayerArmorGraphic GetPlrAnimArmorId(")], "description": "Chest selection is Light/Medium/Heavy only; the selector does not select individual armor, helmets or jewelry.", "resolution": "Character front owns compatible skin/socket binding; item front owns detachable piece design."},
        {"id": "item.issue.shared-icon-distinct-base", "status": "data-proven-not-art-error-judgment", "sources": [b["source"] for b in base_items if b["name"] in ("Hunter's Bow", "Long Bow", "Short Staff", "Quarter Staff", "Plate Mail", "Field Plate")], "description": "Hunter's Bow/Long Bow share HUNTERS_BOW; Short Staff/Quarter Staff share SHORT_STAFF; Plate Mail/Field Plate share FIELD_PLATE. Shared art is factual; which shape is correct requires licensed visual comparison."},
        {"id": "item.issue.lightforge-orphan", "status": "data-proven-unresolved-reference", "sources": [location(UNIQUE, 11), location(BASE, 34)], "description": "Diablo unique mapping ID9 uses LGTFORGE with no base-item uniqueBaseItem match in the current itemdat.tsv. IDI_LGTFORGE is Bovine Plate/BOVINE. Hellfire ID9 explicitly replaces the unique with Bovine Plate. Preserve both semantic identities; never silently bind Lightforge to Bovine Plate."},
        {"id": "item.issue.undead-crown-override", "status": "data-proven-not-art-error-judgment", "sources": [location(BASE, 9), location(UNIQUE, 3)], "description": "The Undead Crown base cursor THE_UNDEAD_CROWN78 is overridden by unique HELM_OF_SPIRITS77. Inspect actual unique state before using an inventory reference; icon enum name alone does not prove wrong artwork."},
        {"id": "item.issue.unique-inherits-multiple-bases", "status": "data-proven", "sources": [definition(ITEMS, "void GetUniqueItem("), definition(ITEMS, "Item *SpawnUnique(")], "description": "An empty unique cursor inherits the runtime base cursor. SPIKCLUB and GOTHSHIELD may match multiple base records, while SpawnUnique chooses the first matching row. Keep all possible base links, not only the first."},
        {"id": "item.issue.visual-comparison-pending", "status": "not-yet-visually-confirmed", "sources": [], "description": "No pixel-level comparison or proprietary image extraction was performed for this public inventory. Specific alleged icon-versus-held differences beyond generic grouping remain a review task."},
    ]
    source_paths = [BASE, UNIQUE, HF_UNIQUE, *AFFIXES, *SPELLS, HEADER, ITEMS, "Source/items.h", "Source/player.h", "Source/player.cpp", "Source/cursor.cpp", "Source/cursor_defs.hpp", "Source/tables/itemdat.cpp", "Source/spells.cpp", "mods/hf/manifest.ini"]
    sources = [{"path": path, "sha256": hashlib.sha256((ROOT / path).read_bytes()).hexdigest()} for path in source_paths]
    result = {"format": "d3d.item-production-study", "schemaVersion": 1, "date": "2026-10-08", "sourceScope": "Current public repository Diablo/Hellfire tables; no arbitrary third-party mods or future Lua-loaded items.",
              "registryPolicy": "This is a derived production study. assets/registry.json remains the sole reservation/acceptance authority; proposed design IDs are not installed runtime asset IDs.",
              "mediaIncluded": False, "sourceImagesIncluded": False, "paidGenerationPerformed": False,
              "sources": sources, "baseItems": base_items, "uniqueItems": unique_items, "nativeVisuals": native_visuals,
              "dynamicAppearances": dynamic, "spellVariants": spell_variants, "affixes": affixes, "productionUnits": list(production.values()), "issues": issues}
    result["summary"] = {"baseRecords": len(base_items), "namedBaseRecords": sum(bool(b["name"]) for b in base_items), "reservedBaseRecords": sum(not b["name"] for b in base_items),
                         "distinctNamedBaseLabels": len({b["name"] for b in base_items if b["name"]}),
                         "diabloUniqueRows": len(diablo_unique), "hellfireUniqueRows": len(hf_unique), "semanticUniqueIdentities": len(unique_items),
                         "nativeCursorDefinitions": len(native_visuals), "dynamicAppearances": len(dynamic), "spellVariantsByProfileAndKind": dict(Counter(v["profile"] + ":" + v["kind"] for v in spell_variants)),
                         "affixRowsByProfileAndKind": dict(Counter(a["profile"] + ":" + a["kind"] for a in affixes)),
                         "productionUnits": len(production), "productionUnitsByFamily": dict(sorted(Counter(p["family"] for p in production.values()).items())),
                         "generatedConcepts": 0, "generatedModels": 0, "acceptedModels": 0,
                         "unresolvedUniqueSourceVariants": sum(v["sourceResolution"] != "resolved" for u in unique_items for v in u["sourceVariants"])}
    return result


def validate(catalog: dict) -> dict:
    errors = []
    base = catalog["baseItems"]
    unique = catalog["uniqueItems"]
    units = {p["id"]: p for p in catalog["productionUnits"]}
    native = {v["cursorName"]: v for v in catalog["nativeVisuals"]}
    source_records = [(BASE, b["source"]["nativeIndex"]) for b in base]
    source_records += [(v["source"]["path"], v["source"]["nativeIndex"]) for u in unique for v in u["sourceVariants"]]
    source_records += [(a["source"]["path"], a["source"]["nativeIndex"]) for a in catalog["affixes"]]
    for path in (BASE, UNIQUE, HF_UNIQUE, *AFFIXES):
        actual = sorted(i for p, i in source_records if p == path)
        if actual != list(range(len(rows(path)))):
            errors.append("Missing or duplicated source row: " + path)
    ids = [e["id"] for key in ("baseItems", "uniqueItems", "nativeVisuals", "dynamicAppearances", "spellVariants", "affixes", "productionUnits", "issues") for e in catalog[key]]
    if len(ids) != len(set(ids)):
        errors.append("Duplicate identity")
    for b in base:
        if b["name"] and b["productionId"] not in units:
            errors.append("Missing base production binding: " + b["id"])
        if b["inventory"]["cursorName"] and b["inventory"]["cursorName"] not in native:
            errors.append("Unknown base cursor: " + b["inventory"]["cursorName"])
    for u in unique:
        if u["productionId"] not in units:
            errors.append("Missing unique production binding: " + u["id"])
        for cursor in u["effectiveCursorNames"]:
            if cursor not in native:
                errors.append("Unknown unique cursor: " + cursor)
    for appearance in catalog["nativeVisuals"]:
        if not appearance["baseItemIds"] and not appearance["uniqueItemIds"] and not appearance["dynamicIds"]:
            errors.append("Uncovered native cursor: " + appearance["cursorName"])
        if not appearance["productionIds"]:
            errors.append("Unbound native cursor: " + appearance["cursorName"])
    for v in catalog["spellVariants"]:
        if any(p not in units for p in v["productionIds"]):
            errors.append("Unbound spell variant: " + v["id"])
    for source in catalog["sources"]:
        if hashlib.sha256((ROOT / source["path"]).read_bytes()).hexdigest() != source["sha256"]:
            errors.append("Source hash drift: " + source["path"])
    return {"format": "d3d.item-study-coverage", "schemaVersion": 1, "passed": not errors, "errors": errors,
            "sourceRowsCovered": len(source_records), "nativeCursorDefinitionsCovered": len(native), "productionBindingsChecked": len(base) + len(unique),
            "sourceImagesRead": 0, "modelsGenerated": 0, "runtimeChanged": False,
            "limits": ["Structural coverage of current source tables only.", "Does not prove artwork, spawnability, rig compatibility or installed assets.", "An orphan Lightforge source relationship is explicitly retained as unresolved, not silently repaired."]}


def csv_text(fields: list[str], records: list[dict]) -> str:
    stream = io.StringIO(newline="")
    writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
    writer.writeheader()
    writer.writerows(records)
    return stream.getvalue()


def outputs(catalog: dict) -> dict[str, str]:
    flat = []
    for b in catalog["baseItems"]:
        flat.append({"kind": "base", "id": b["id"], "name": b["name"], "profile": "hellfire" if not b["scope"]["diabloTableEligible"] else "diablo+hellfire", "source": b["source"]["path"], "line": b["source"]["line"], "nativeIndex": b["nativeIndex"], "enumId": "|".join(b["sourceEnumAliases"]), "type": b["nativeData"]["itemType"], "cursor": b["inventory"]["cursorName"] or "", "productionId": b["productionId"] or "", "status": b["productionStatus"]})
    for u in catalog["uniqueItems"]:
        for v in u["sourceVariants"]:
            flat.append({"kind": "unique", "id": u["id"], "name": v["nativeData"]["name"], "profile": v["profile"], "source": v["source"]["path"], "line": v["source"]["line"], "nativeIndex": v["mappingId"], "enumId": "|".join(v["sourceEnumAliases"]), "type": v["nativeData"]["uniqueBaseItem"], "cursor": "|".join(v["effectiveCursorNames"]), "productionId": u["productionId"], "status": v["sourceResolution"]})
    units = [{"id": p["id"], "name": p["name"], "family": p["family"], "kind": p["kind"], "nativeCursors": "|".join(p["nativeCursorNames"]), "sourceBaseIds": "|".join(p["sourceBaseIds"]), "sourceUniqueIds": "|".join(p["sourceUniqueIds"]), "sourceDynamicIds": "|".join(p["sourceDynamicIds"]), "conceptStatus": p["concept"]["status"], "modelStatus": p["model"]["status"], "iconStatus": p["inventoryIcon"]["status"]} for p in catalog["productionUnits"]]
    affixes = [{"id": a["id"], "name": a["name"], "profile": a["profile"], "kind": a["kind"], "source": a["source"]["path"], "line": a["source"]["line"], "power": a["nativeData"]["power"], "eligibleItemTypes": a["nativeData"]["itemTypes"], "geometryPolicy": a["geometryPolicy"]} for a in catalog["affixes"]]
    return {"catalog.json": json.dumps(catalog, ensure_ascii=False, indent=2) + "\n",
            "catalog.csv": csv_text(["kind", "id", "name", "profile", "source", "line", "nativeIndex", "enumId", "type", "cursor", "productionId", "status"], flat),
            "concept-slots.csv": csv_text(["id", "name", "family", "kind", "nativeCursors", "sourceBaseIds", "sourceUniqueIds", "sourceDynamicIds", "conceptStatus", "modelStatus", "iconStatus"], units),
            "affixes.csv": csv_text(["id", "name", "profile", "kind", "source", "line", "power", "eligibleItemTypes", "geometryPolicy"], affixes),
            "coverage.json": json.dumps(validate(catalog), ensure_ascii=False, indent=2) + "\n"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Validate sources and generated text without writing.")
    args = parser.parse_args()
    catalog = build()
    report = validate(catalog)
    if not report["passed"]:
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 1
    produced = outputs(catalog)
    if args.check:
        drift = [name for name, text in produced.items() if not (OUT / name).is_file() or (OUT / name).read_text(encoding="utf-8") != text]
        if drift:
            print("Generated study drift: " + ", ".join(drift))
            return 1
    else:
        for name, text in produced.items():
            (OUT / name).write_text(text, encoding="utf-8", newline="\n")
    print(json.dumps({"coverage": report, "summary": catalog["summary"]}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
