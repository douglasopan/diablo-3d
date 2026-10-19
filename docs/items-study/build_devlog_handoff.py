#!/usr/bin/env python3
"""Derive a review/devlog handoff from delivered concept manifests; never approves assets."""
import argparse
import hashlib
import json
import struct
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUTPUT = HERE / "devlog-handoff.json"
REPORT = HERE / "DEVLOG-HANDOFF.md"

def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def safe(relative):
    path = (ROOT / relative).resolve()
    if ROOT not in path.parents:
        raise ValueError("Path outside repository: " + relative)
    return path

def incomplete(entry):
    return (
        entry.get("kind") == "concept-sheet"
        or entry.get("blocksMasterGeneration") is True
        or str(entry.get("modelReadiness", "")).startswith("blocked")
        or "incomplete" in str(entry.get("studyStatus", ""))
        or entry.get("coherenceReview", {}).get("passed") is False
        or entry.get("coherenceReview", {}).get("coherentAcrossViews") is False
        or entry.get("coherenceReview", {}).get("modelingReady") is False
        or entry.get("targetedDefect", {}).get("result") == "failed"
    )

def incomplete_concept_ids(documents, primary_entries):
    records = {}
    def collect(value):
        if isinstance(value, list):
            for child in value:
                collect(child)
        elif isinstance(value, dict):
            if value.get("conceptId") and value.get("productionId"):
                records.setdefault(value["conceptId"], []).append(value)
            for child in value.values():
                if isinstance(child, (dict, list)):
                    collect(child)
    for document in documents.values():
        collect(document)
    blocked = {identity for identity, variants in records.items() if any(incomplete(e) for e in variants)}
    while True:
        inherited = set()
        for identity, variants in records.items():
            for entry in variants:
                source = entry.get("reusedFromConceptId")
                if not source and entry.get("reusedFromProductionId") in primary_entries:
                    source = primary_entries[entry["reusedFromProductionId"]]["conceptId"]
                if source in blocked:
                    inherited.add(identity)
        if inherited <= blocked:
            return blocked
        blocked |= inherited

def derive(require_complete=False):
    overlay = read(HERE / "concept-art.json")
    catalog = read(HERE / "catalog.json")
    queue = read(HERE / "production-queue.json")
    assert digest(HERE / "catalog.json") == overlay["catalogSha256"]
    units = {unit["id"]: unit for unit in catalog["productionUnits"]}
    entries = {entry["productionId"]: entry for entry in overlay["entries"]}
    assert len(entries) == len(overlay["entries"]), "Duplicate primary unit"
    assert set(entries) <= set(units)
    assert {e["productionId"] for e in queue["entries"] if not e["needsConcept"]} == set(entries), "Refresh production queue"
    documents = {}
    pending = ["docs/items-study/concept-art.json"]
    while pending:
        relative = pending.pop()
        if relative in documents:
            continue
        document = read(safe(relative))
        documents[relative] = document
        pending.extend(document.get("batchManifests", []))
        pending.extend(document.get("revisionManifests", []))
    images = {}
    def visit(value, manifest):
        if isinstance(value, list):
            for child in value:
                visit(child, manifest)
        elif isinstance(value, dict):
            image = value.get("image")
            if isinstance(image, str) and image.startswith("assets/concept-art/items/") and image.endswith(".png"):
                path = safe(image)
                data = path.read_bytes()
                sha = hashlib.sha256(data).hexdigest()
                assert data.startswith(b"\x89PNG\r\n\x1a\n"), image
                width, height = struct.unpack(">II", data[16:24])
                if value.get("sha256"):
                    assert value["sha256"] == sha, image
                if value.get("dimensions"):
                    assert value["dimensions"] == [width, height], image
                row = images.setdefault(image, {
                    "image": image, "sha256": sha, "dimensions": [width, height],
                    "productionIds": set(), "conceptIds": set(), "revisions": set(),
                    "manifestSources": set(), "reviewNotes": set(), "explicitIncompleteInHistory": False,
                })
                for key, target in (("productionId", "productionIds"), ("conceptId", "conceptIds"), ("revision", "revisions")):
                    if value.get(key):
                        row[target].add(value[key])
                row["manifestSources"].add(manifest)
                row["explicitIncompleteInHistory"] |= incomplete(value)
                for note in (value.get("notes"), value.get("identityGate"), value.get("revisionReason"),
                             value.get("interpretationGate"), value.get("targetedDefect", {}).get("observation")):
                    if isinstance(note, str) and note.strip():
                        row["reviewNotes"].add(note)
                review = value.get("coherenceReview", {})
                for key in ("introducedOrRemainingIssues", "pendingIssues"):
                    for note in review.get(key, []):
                        if isinstance(note, str):
                            row["reviewNotes"].add(note)
            for child in value.values():
                if isinstance(child, (dict, list)):
                    visit(child, manifest)
    for relative, document in documents.items():
        visit(document, relative)
    disk_images = {p.relative_to(ROOT).as_posix() for p in (ROOT / "assets/concept-art/items").rglob("*.png")}
    unregistered = sorted(disk_images - images.keys())
    assert set(images) <= disk_images
    blocked = [e for e in queue["entries"] if e["referenceStatus"] == "blocked"]
    pending_available = [e for e in queue["entries"] if e["needsConcept"] and e["referenceStatus"] != "blocked"]
    if require_complete:
        assert not pending_available, "Reference-available units still pending"
        assert not unregistered, "Generated PNG not registered in a delivered manifest"
    blocked_concepts = incomplete_concept_ids(documents, entries)
    incomplete_ids = {identity for identity, entry in entries.items() if entry["conceptId"] in blocked_concepts}
    first_images = {entry["image"] for entry in overlay["entries"][:9]}
    primary_images = {entry["image"] for entry in entries.values()}
    rows = []
    for image, row in sorted(images.items()):
        for key in ("productionIds", "conceptIds", "revisions", "manifestSources", "reviewNotes"):
            row[key] = sorted(row[key])
        row["names"] = sorted({units[i]["name"] for i in row["productionIds"] if i in units})
        row["presentationFor"] = sorted(i for i, e in entries.items() if e["image"] == image)
        row["archiveOnly"] = image not in primary_images
        row["initialBatch"] = image in first_images
        row["devlogLabel"] = "Estudo conceitual candidato; sem aprovação ou instalação"
        rows.append(row)
    families = []
    for family in sorted({u["family"] for u in units.values()}):
        ids = {i for i, u in units.items() if u["family"] == family}
        family_blocked = {e["productionId"] for e in blocked} & ids
        families.append({"family": family, "total": len(ids), "linked": len(ids & entries.keys()),
                         "referenceBlocked": len(family_blocked),
                         "pendingAvailable": len(ids - entries.keys() - family_blocked)})
    handoff = {
        "format": "d3d.item-concept-devlog-handoff", "schemaVersion": 1, "date": "2026-10-08",
        "status": "reference-available-round-delivered" if not pending_available and not unregistered else "delivered-batches-snapshot",
        "purpose": "All delivered generated images, including defective and corrected revisions, for the integrator's devlog. This file does not publish or approve content.",
        "sourceFiles": [{"path": p, "sha256": digest(safe(p))} for p in sorted(documents)] + [
            {"path": "docs/items-study/catalog.json", "sha256": digest(HERE / "catalog.json")},
            {"path": "docs/items-study/production-queue.json", "sha256": digest(HERE / "production-queue.json")}],
        "policy": {
            "publishEveryGeneratedRevision": True, "publishOriginalGamePixels": False,
            "visualApproval": False, "accepted": False, "selected": False, "runtimeInstalled": False,
            "generated3dModels": 0, "finalRuntimeIcons": 0,
            "registryAuthority": "assets/registry.json",
            "sharing": "Exact-cursor sharing is proposed, not approved; native unique identity/powers stay separate.",
            "identity": "Inventory, ground and equipped forms must derive from the same reviewed master/materials. Runtime bridge remains pending.",
        },
        "summary": {
            "productionUnits": len(units), "linkedUnits": len(entries),
            "referenceAvailableOrIndirect": len(units) - len(blocked), "referenceBlocked": len(blocked),
            "pendingAvailableUnits": len(pending_available), "generatedPNGs": len(rows),
            "initialBatchPNGs": len(first_images), "newPNGs": len(rows) - len(first_images),
            "primaryReviewImages": len(primary_images), "archivalImages": len(images.keys() - primary_images),
            "turnaroundEntries": sum(e["kind"] == "turnaround" for e in entries.values()),
            "incompleteStudyEntries": sum(e["kind"] == "concept-sheet" for e in entries.values()),
            "sharingProposals": sum(e["kind"] == "family-sheet" for e in entries.values()),
            "explicitIncompleteOrInheritedGates": len(incomplete_ids),
            "unregisteredPNGs": len(unregistered),
        },
        "familyCoverage": families,
        "referenceBlockers": [{k: e.get(k) for k in ("productionId", "name", "family", "nativeCursorNames", "blockerCode")} for e in blocked],
        "explicitIncompleteUnits": sorted(incomplete_ids),
        "pendingAvailableUnits": [e["productionId"] for e in pending_available],
        "unregisteredImages": unregistered,
        "images": rows,
    }
    s = handoff["summary"]
    lines = [
        "# Entrega de conceitos de itens para o integrador e devlog", "",
        "Snapshot derivado dos lotes entregues. Não publica o site e não aprova, seleciona ou instala assets.", "",
        f"**{s['linkedUnits']}/{s['productionUnits']} unidades vinculadas**, entre {s['turnaroundEntries']} entradas de turnaround, {s['incompleteStudyEntries']} estudos incompletos e {s['sharingProposals']} propostas de reaproveitamento.",
        f"**{s['generatedPNGs']} PNGs gerados preservados**: {s['initialBatchPNGs']} iniciais + {s['newPNGs']} novos. Inclui {s['archivalImages']} imagens históricas fora da apresentação principal.",
        f"Referências: {s['referenceAvailableOrIndirect']} disponíveis/diretas ou indiretas; {s['referenceBlocked']} bloqueadas. Restam {s['pendingAvailableUnits']} unidades disponíveis sem vínculo neste snapshot.", "",
        "O devlog deve incluir **todas as imagens abaixo**, inclusive revisões defeituosas, com a condição de candidato e o motivo da correção. As referências originais são privadas e não entram na publicação. Os prompts finais e revisões estão nos manifestos citados.", "",
        "Nenhum modelo 3D ou ícone final foi gerado. Equipamento e chão continuam dependendo do contrato de identidade e da ponte runtime. Um reuso não representa um design exclusivo aprovado nem uma malha obrigatória por nome. Os bloqueios de conceito/identidade continuam individuais.", "",
        "- [Galeria de revisão](gallery.html)",
        "- [Manifesto cumulativo](concept-art.json)",
        "- [Índice completo de imagens, hashes, revisões e bloqueios](devlog-handoff.json)",
        "- [Contrato de produção](../ITEM-PRODUCTION-CONTRACT.md)", "",
        "## Cobertura por família", "",
        "| Família | Unidades | Vinculadas | Referência bloqueada | Disponíveis pendentes |",
        "| --- | ---: | ---: | ---: | ---: |",
    ]
    lines += [f"| {r['family']} | {r['total']} | {r['linked']} | {r['referenceBlocked']} | {r['pendingAvailable']} |" for r in families]
    lines += ["", "## Imagens para o devlog", "", "| Imagem | Revisão | Condição |", "| --- | --- | --- |"]
    for row in rows:
        label = Path(row["image"]).name
        condition = "Histórico; não é a prévia principal" if row["archiveOnly"] else "Candidato em revisão"
        if row["explicitIncompleteInHistory"]:
            condition += "; contém estudo/defeito registrado"
        lines.append(f"| [{label}](../../{row['image']}) | {', '.join(row['revisions']) or 'ver manifesto'} | {condition} |")
    lines += ["", "## Continuidade", "",
        "O integrador reconcilia o estado em PROJECT-EXECUTION.md, Git e site/devlog. Esta frente entrega apenas conceitos, inventário e contrato. Antes de modelar, revisar falhas de coerência, inferências de verso/profundidade, reaproveitamentos e os estados nativos de identidade.",
        "Os 34 gráficos de Hellfire ausentes e Lightforge sem referência resolvida permanecem bloqueados; não usar Bovine Plate como substituto de Lightforge.",
        "", "Regenerar este snapshot após consolidar novos lotes:", "",
        "```powershell", "python -B docs/items-study/build_production_queue.py",
        "python -B docs/items-study/build_devlog_handoff.py",
        "python -B docs/items-study/build_devlog_handoff.py --check",
        "```", "",
    ]
    return handoff, "\n".join(lines)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args()
    handoff, report = derive(args.require_complete)
    data = json.dumps(handoff, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
    if args.check:
        assert OUTPUT.read_text(encoding="utf-8") == data, "Handoff JSON stale"
        assert REPORT.read_text(encoding="utf-8") == report, "Handoff report stale"
    else:
        OUTPUT.write_text(data, encoding="utf-8", newline="\n")
        REPORT.write_text(report, encoding="utf-8", newline="\n")
    print(json.dumps({"passed": True, "mode": "check" if args.check else "write", **handoff["summary"]}))

if __name__ == "__main__":
    main()
