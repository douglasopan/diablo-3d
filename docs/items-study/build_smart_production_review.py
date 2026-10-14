"""Derive a review packet from the existing item queue; never submit or reserve."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
DIRECTORY = ROOT / "docs/items-study"
INPUTS = [
    "docs/items-study/catalog.json",
    "docs/items-study/production-queue.json",
    "docs/items-study/concept-art.json",
    "docs/items-study/devlog-handoff.json",
    "docs/items-study/inventory-prep-r1.json",
    "docs/items-study/smart-production-context.json",
    "docs/items-study/studio-preflight-candidate-r1.json",
    "docs/ITEM-PRODUCTION-CONTRACT.md",
    "assets/registry.json",
    "editor/godot/review/catalog.json",
    "docs/items-study/batch-02-great-sword-r3.json",
    "docs/items-study/build_smart_production_review.py",
    "docs/items-study/grouped-items-study.json",
]

COMMON_QA = {
    "input": "Arquivo/hash e aprovação exatos; por padrão, um objeto completo por imagem, sem título, moldura, labels ou miniatura da prancha. A exceção documental grouped-compact-01 estuda 2–3 itens distintos sem autorizar execução. Preservar inscrições legítimas. Multivisão Frente/Esquerda/Direita/Verso é do mesmo objeto; 3/4 não é perfil lateral. O controle de lote é outro fluxo.",
    "geometry": "Revisão em 360°: contagem de objetos/partes, silhueta, peças finas, espessuras, aberturas, normais, topologia e duplicatas desconectadas. Quad ou número de polígonos não comprova qualidade.",
    "materials": "Preservar UVs, texturas, espaços de cor, fatores e alpha do master; distinguir cor do material de luz gravada. Conferir suporte real do renderer e preservar dados de origem não suportados.",
    "identity": "Preservar IDs de produção/base/único/dinâmico, cursor efetivo, magia, quantidade e variantes de classe. Cursor ou imagem compartilhados não aprovam malha compartilhada.",
    "attachment": "GLB/glTF2, metros/Y para cima/+Z e transforms declarados; calibração uniforme uma vez. Conferir grip, encaixe, rig, skin e tempo nativo. Roupas não podem trazer anatomia inventada fundida.",
    "lod": "Preservar master imutável. Derivados com hash, âncora comum e revisão de silhueta, peças finas, UV/material e qualidade próxima. Erro geométrico precisa de evidência; sem seleção automática com errorVerified=false.",
    "icon": "RGBA final do mesmo master/material aceito, sem títulos, labels ou texto acrescentado pela prancha, fundo ou sombra aderida; preservar inscrições nativas legítimas. Conferir células nativas de 28 px, legibilidade, contorno/hover e fundos claros/escuros da interface.",
    "runtime": "Principal integra/instala e registra testes reais de inventário, loja, cinto, chão e equipado quando aplicáveis. Preservar nomes, stats, slots, animação, picking, simulação e fallback original. Dimensões de chão não são inferidas do ícone.",
}

FAMILY_QA = {
    "blades": ["Thin blade/edge survives topology and LOD; preserve point, guard handedness, grip and pommel."],
    "axes": ["Keep blade head thickness, opening/profile, haft and head connection; verify grip and close-up edge."],
    "blunt-weapons": ["Preserve head/haft scale and joins. Flail chain and head need separate articulation review without changing native attack timing."],
    "bows": ["Preserve limb curvature, thin cord and negative space. Keep bow/cord separate from the attack projectile contract."],
    "staves": ["Preserve long thin shaft, endcaps/bands and hand placement. Charges/spells do not automatically create independent geometry."],
    "shields": ["Check front/back emblem, rim thickness, handle/strap and handedness; no duplicated front boss on rear/side."],
    "headgear": ["Check inside opening, thickness, class fit and clipping. Do not fuse an invented head/anatomy into the item."],
    "body-armor": ["Separate rigid pieces from skinned clothing; check rig, bind pose, skin weights and native pose clipping. No fused invented body/anatomy."],
    "jewelry": ["Preserve loop/hole, stone setting and thin parts at icon and close-up scale; do not replace identity with a generic name-based shape."],
    "potions-elixirs": ["Preserve bottle/neck/cork/cord and exact liquid/color variant. Verify glass transmission/alpha and liquid volume without baking the board backdrop into the final asset."],
    "oils": ["Preserve bottle/closure and declared oil identity; ten named effects do not imply ten independent meshes."],
    "gold": ["Preservar três aparências de pilha dependentes da quantidade. Reconciliar contagem física visível e arranjo entre vistas; a fonte não comprova integralmente moedas ocultas. Não duplicar pilhas nem alterar os limiares nativos independentes de quantidade."],
    "scrolls": ["One tied physical roll; preserve ribbon/end spiral and diagonal pose. Spell IDs/SCROLL/SCROLLT stay native; no invented spell marks."],
    "spellbooks": ["Preserve cover color, binding/spine, clasp/ribbon and rear details; verify the same object across views."],
    "runes": ["Review missing Hellfire source first, then exact shape/inscription and thickness; name alone is not an input reference."],
    "ears": ["Preserve three native class appearances and class-bit mapping. Base Ear is a family, not a fourth graphic or a forced Warrior appearance."],
    "quest-objects": ["Verify quest identity/effective cursor before production, including Lightforge's unresolved source. Preserve readable organic anatomy/parts where the specific object requires it."],
}


def read(relative: str):
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def digest(relative: str) -> str:
    return hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()


def root_group(pid, by_id):
    visited = set()
    current = pid
    while current in by_id and by_id[current].get("reusedFromProductionId"):
        if current in visited:
            raise ValueError(f"Reuse cycle: {pid}")
        visited.add(current)
        current = by_id[current]["reusedFromProductionId"]
    return current


def snapshot_reference(art):
    if art is None:
        return None
    path = art.get("image")
    if path and digest(path) != art["sha256"]:
        raise ValueError(f"Concept image hash changed: {path}")
    reference = art.get("reference", {})
    return {
        "conceptId": art["conceptId"],
        "revision": art["revision"],
        "path": path,
        "sha256": art.get("sha256"),
        "dimensions": art.get("dimensions"),
        "tool": art.get("generationTool"),
        "role": "concept-board-or-family-study; not a prepared individual input",
        "sourcePixelIconPasted": art.get("sourcePixelIconPasted", False),
        "originalBytesPreserved": True,
        "nativeReference": {k: reference.get(k) for k in ("cursorId", "sha256", "availability")},
        "individualInput": {"status": "not-prepared", "path": None, "sha256": None, "hasAlpha": None, "textOrFrameRemoved": False, "approved": False},
        "specificConceptApproval": art.get("visualApproval", False),
        "approvalEvidence": None,
        "reusedFromProductionId": art.get("reusedFromProductionId"),
        "reusedFromConceptId": art.get("reusedFromConceptId") or art.get("sharedDesignProposal", {}).get("sourceConceptId"),
        "referenceStudies": [{k: s.get(k) for k in ("productionId", "conceptId", "image", "sha256", "cursorId", "role")} for s in art.get("referenceStudies", [])],
        "notes": art.get("notes"),
    }


def build():
    catalog = read(INPUTS[0])
    queue = read(INPUTS[1])
    overlay = read(INPUTS[2])
    handoff = read(INPUTS[3])
    prep = read(INPUTS[4])
    context = read(INPUTS[5])
    grouped = read("docs/items-study/grouped-items-study.json")
    for source in grouped["sourceSnapshots"]:
        if digest(source["path"]) != source["sha256"]:
            raise ValueError(f"Grouped study source snapshot changed: {source['path']}")
    for candidate in grouped["candidates"]:
        if digest(candidate["sourceBoard"]["path"]) != candidate["sourceBoard"]["sha256"]:
            raise ValueError(f"Grouped study board hash changed: {candidate['productionId']}")
    art_by_id = {a["productionId"]: a for a in overlay["entries"]}
    prod_by_id = {u["id"]: u for u in catalog["productionUnits"]}
    prep_by_id = {e["productionId"]: e for e in prep["entries"]}
    incomplete = set(handoff["explicitIncompleteUnits"])
    manual = {m["productionId"]: m for m in context["existingAssetAudit"]["resolvedManualExclusions"]}
    for model in manual.values():
        source = model.get("sourceCandidate")
        if source and digest(source["path"]) != source["sha256"]:
            raise ValueError(f"Manual-test source candidate hash changed: {source['path']}")
    resolved_absent = set(context["existingAssetAudit"]["resolvedNoExistingAssetIdentities"])
    first_ids = context["firstSmallLotProposal"]["productionIdsInPreparationOrder"]
    groups = defaultdict(list)
    rows = []
    for q in queue["entries"]:
        pid = q["productionId"]
        p = prod_by_id[pid]
        a = art_by_id.get(pid)
        group = root_group(pid, art_by_id)
        if group not in prod_by_id:
            raise ValueError(f"Unknown reuse root: {group}")
        groups[group].append(pid)
        existing = manual.get(group) or manual.get(pid)
        is_excluded = existing is not None
        own_manual = manual.get(pid)
        native = [{k: n.get(k) for k in ("id", "cursorName", "cursorValue", "inventoryCursorId", "combinedSpriteIndexZeroBased", "groundSprite")} for n in catalog["nativeVisuals"] if pid in n["productionIds"]]
        native_prep = prep_by_id.get(pid, {}).get("native")
        reasons = ["Individual approved reference not prepared", "Per-asset authenticated cost/private destination/spend ceiling pending", "Submission not authorized"]
        reasons.append("Known manual candidate excluded pending QA/reuse" if is_excluded else "Existing manual/remote model exclusion unresolved")
        if q["referenceStatus"] == "blocked":
            reasons.append(q["blockerCode"] or "Native reference blocked")
        if pid in incomplete:
            reasons.append("Recorded concept/coherence/identity gate remains incomplete")
        rows.append({
            "productionId": pid, "name": q["name"], "family": q["family"], "kind": q["kind"],
            "priority": {"existingQueueSequence": q["sequenceIndex"], "existingQueueGroup": q["groupId"], "classification": q["classification"], "firstSmallLotPreparationRank": first_ids.index(pid) + 1 if pid in first_ids else None},
            "sourceBaseIds": p["sourceBaseIds"], "sourceUniqueIds": p["sourceUniqueIds"], "sourceDynamicIds": p["sourceDynamicIds"],
            "nativePresentations": native,
            "verifiedInventoryFootprint": {k: native_prep[k] for k in ("logicalPixelDimensions", "cellSizePixels", "cellFootprint")} if native_prep else None,
            "method": {"chosenFeature": context["methodDecision"]["feature"], "profilePerAssetConfirmed": False, "automaticRegenerationToStandardizeMethod": False},
            "referenceStatus": q["referenceStatus"], "referenceBlocker": q["blockerCode"], "artCandidate": snapshot_reference(a),
            "conceptIncomplete": pid in incomplete,
            "reuse": {"proposedMasterGroupId": group, "directSourceProductionId": a.get("reusedFromProductionId") if a else None, "sourceConceptRevisionPinned": bool(a and (a.get("reusedFromConceptId") or a.get("sharedDesignProposal", {}).get("sourceConceptId"))), "geometrySharingApproved": False, "policy": "Explicit concept linkage only, not approved geometry reuse. Preserve unique/dynamic/material identities and review family exceptions."},
            "manualExclusion": {"status": "known-existing" if own_manual else ("absence-confirmed" if pid in resolved_absent else "unknown"), "excludedFromNewJobs": is_excluded, "dependencyRootProductionId": group if is_excluded and not own_manual else None, "providerTaskId": own_manual.get("providerTaskId") if own_manual else None, "dependencySourceProviderTaskId": existing.get("providerTaskId") if is_excluded and not own_manual else None, "artifactSha256": None, "inputUploadedSha256": own_manual["sourceCandidate"]["sha256"] if own_manual and own_manual.get("sourceCandidate", {}).get("uploadedBytesHashVerifiedByOperator") else None, "evidenceKind": "Human/operator report" if own_manual else ("Explicit reuse dependency on manual source" if is_excluded else "Unresolved audit"), "newJobUnitsNow": 0, "qualityApproved": False},
            "deliverables": {"master": {"status": "manual-candidate-reported" if own_manual else ("reuse-source-candidate-reported; own-model-unknown" if is_excluded else "unknown-existing-model-audit-pending"), "path": None, "sha256": None, "approved": False, "providerTaskId": own_manual.get("providerTaskId") if own_manual else None}, "material": {"revision": None, "sha256": None, "uvTexturesVerified": False}, "lods": [], "inventoryIcon": {"path": None, "sha256": None, "sameAcceptedMaster": False, "approved": False}, "ground": {"masterRevision": None, "approved": False, "installed": False}, "equipped": {"masterRevision": None, "socketRigRevision": None, "approved": False, "installed": False}},
            "qa": {"commonGateIds": list(COMMON_QA), "familyChecks": FAMILY_QA[q["family"]], "gateStatus": {key: "pending" for key in COMMON_QA}, "passedGateIds": [], "inGameTested": False},
            "fallback": {"enabled": True, "source": "native inventory cursor, ground animation and equipped category", "preserveNativeGameplay": True, "unapprovedOrMissingResourceUsesNative": True},
            "budget": {"confirmedCostCredits": None, "approvedCeilingCredits": None, "newJobUnitsAuthorized": 0, "conditionalNewJobUnitsAfterAllGates": 0 if is_excluded or group != pid else 1, "conditionalUnitCreditsAssumption": context["conditionalBudgetAssumption"]["creditsPerSingleOutput"], "hypothesisOnly": True},
            "eligibility": {"submitNow": False, "reasons": reasons},
        })
    row_by_id = {r["productionId"]: r for r in rows}
    group_rows = []
    for gid, members in groups.items():
        excluded = any(row_by_id[m]["manualExclusion"]["excludedFromNewJobs"] for m in members)
        covered = any(row_by_id[m]["referenceStatus"] != "blocked" for m in members)
        group_rows.append({"proposedMasterGroupId": gid, "productionIds": members, "referenceAvailable": covered, "manualCandidateExcluded": excluded, "approvedSharing": False, "newJobUnitsAuthorized": 0, "conditionalJobUnitsAfterAllGates": 0 if excluded else 1})
    first = [row_by_id[pid] for pid in first_ids]
    count = len(rows)
    available = sum(r["referenceStatus"] != "blocked" for r in rows)
    excluded_ids = sum(r["manualExclusion"]["excludedFromNewJobs"] for r in rows)
    excluded_groups = sum(g["manualCandidateExcluded"] for g in group_rows)
    available_groups = sum(g["referenceAvailable"] for g in group_rows)
    remaining_groups = len(group_rows) - excluded_groups
    remaining_available_groups = sum(g["referenceAvailable"] and not g["manualCandidateExcluded"] for g in group_rows)
    cost = context["conditionalBudgetAssumption"]["creditsPerSingleOutput"]
    scenarios = [
        {"id": "pilot-only", "label": "Piloto único Scroll, se elegível", "hypotheticalOutputs": 1},
        {"id": "small-lot-including-pilot", "label": "Três candidatos, incluindo o piloto uma vez", "hypotheticalOutputs": len(first)},
        {"id": "small-lot-one-trial-if-reconfirmed", "label": "Os mesmos três candidatos, se um único trial de 0 for reconfirmado", "hypotheticalOutputs": len(first), "conditionalTrialOutputs": 1},
        {"id": "available-proposed-groups", "label": "Grupos com referência após excluir o manual; reusos ainda condicionais", "hypotheticalOutputs": remaining_available_groups},
        {"id": "all-proposed-groups", "label": "Todos os grupos após excluir o manual; inclui fontes bloqueadas", "hypotheticalOutputs": remaining_groups},
        {"id": "available-no-reuse", "label": "Uma saída por identidade com referência restante, sem aprovar reuso", "hypotheticalOutputs": available - sum(r["manualExclusion"]["excludedFromNewJobs"] and r["referenceStatus"] != "blocked" for r in rows)},
        {"id": "one-per-identity-no-reuse", "label": "Uma saída por identidade restante, sem aprovar reuso", "hypotheticalOutputs": count - excluded_ids},
    ]
    for s in scenarios:
        trials = s.get("conditionalTrialOutputs", 0)
        if trials > 1 or trials > s["hypotheticalOutputs"]:
            raise ValueError("Trial scenario cannot discount more than one output")
        s.update({"conditionalCredits": (s["hypotheticalOutputs"] - trials) * cost, "conditionalTrialOutputs": trials, "trialAssignedProductionId": None, "finalInputTrialEligibilityConfirmed": False, "scope": "geometry-only; texture and other extras excluded", "authenticatedQuoteConfirmed": False, "approved": False, "outputsAuthorized": 0})
    summary = {"productionIdentities": count, "linkedConceptIdentities": len(art_by_id), "nativeReferenceAvailableOrIndirect": available, "referenceBlocked": count - available, "explicitReuseEdges": sum(bool(a.get("reusedFromProductionId")) for a in art_by_id.values()), "proposedGroupsAll": len(group_rows), "proposedGroupsWithReference": available_groups, "manualCandidateGroupsExcluded": excluded_groups, "identitiesExcludedByManualGroup": excluded_ids, "manualExclusionUnknownIdentities": sum(r["manualExclusion"]["status"] == "unknown" for r in rows), "conditionalRemainingGroupsAll": remaining_groups, "conditionalRemainingGroupsWithReference": remaining_available_groups, "incompleteConceptIdentities": len(incomplete), "preparedIndividualInputs": 0, "approvedInputs": 0, "validatedMasters": 0, "generatedByItems": 0, "authorizedNewJobs": 0, "creditsAuthorized": 0, "installedByPacket": 0}
    return {"format": "d3d.item-smart-production-review", "schemaVersion": 1, "date": context["date"], "status": "review-only-no-jobs-authorized", "purpose": "Complete item review list and conditional budget derived from the existing queue, with explicit manual-model exclusion; not a competing queue, claim registry or provider scheduler.", "sourceFiles": [{"path": p, "sha256": digest(p)} for p in INPUTS], "context": context, "groupedStudy": grouped, "geometryBudgetLinkage": geometry_linkage(rows, grouped), "summary": summary, "commonQaGates": COMMON_QA, "budgetScenarios": scenarios, "reuseExceptions": [{"productionId": "item.design.unique.the-undead-crown", "gate": "Confirm exact catalog ID/state and base78 versus unique override77 before sharing with Helm of Spirits."}, {"productionId": "item.design.unique.gnarled-root", "name": "Gnarled Root", "gate": "Club versus Spiked Club source remains an identity/reuse decision; do not silently choose by name."}, {"productionId": "item.design.base.ear", "gate": "Family with three class variants, not a fourth Warrior-only mesh."}], "localModelInventoryAudit": {"source": "editor/godot/review/catalog.json", "records": len(read("editor/godot/review/catalog.json")["models"]), "explicitItemMappingFoundByReadOnlyAudit": False, "absenceOfManualOrRemoteAssetsProven": False}, "firstSmallLotProposal": {"status": context["firstSmallLotProposal"]["status"], "currentExperiment": False, "productionIds": first_ids, "pilotIncluded": True, "eligibleNow": 0, "approved": False, "conditionalAggregateAlreadyIncludesThisLot": True, "alternativeForHumanReview": {"productionId": "item.design.base.dagger", "selected": False, "reason": "Opaque steel/leather reduces glass-material uncertainty versus Healing, but duplicates some Sword coverage. No automatic substitution."}, "referenceViewProposals": {first_ids[0]: "Isolate the large original-angle three-quarter roll, not the complete board or boxed thumbnail.", first_ids[1]: "Isolate one complete sword view with consistent blade/guard/grip; approve the exact view before production.", first_ids[2]: "Isolate one complete vial view; review transmitted glass, red liquid and neutral material lighting."}}, "proposedMasterGroups": group_rows, "entries": rows}


def md_link(path):
    return "../../" + path


def pt_int(value):
    return f"{value:,}".replace(",", ".")


def geometry_linkage(rows, grouped):
    study = grouped["geometryBudgetStudy"]
    for source in study["sourceContracts"]:
        if digest(source["path"]) != source["sha256"]:
            raise ValueError(f"Geometry study contract changed: {source['path']}")
    categories = {r["categoryId"]: r for r in study["researchGuide"]["categoryRules"]}
    proposed_categories = {
        "item.design.base.scroll": "scroll",
        "item.design.base.potion-of-healing": "potion",
        "item.design.base.potion-of-mana": "potion",
    }
    row_by_id = {r["productionId"]: r for r in rows}
    entries = [{"productionId": r["productionId"], "family": r["family"], "proposedMasterGroupId": r["reuse"]["proposedMasterGroupId"], "directReuseSourceProductionId": r["reuse"]["directSourceProductionId"], "geometrySharingApproved": False, "manualGroupExcluded": r["manualExclusion"]["excludedFromNewJobs"], "experimentalGuideCategoryProposal": proposed_categories.get(r["productionId"]), "categoryApplicationApproved": False, "minimumVertexCount": None} for r in rows]
    families = [{"family": family, "identities": sum(r["family"] == family for r in rows), "proposedMasterGroups": len({r["reuse"]["proposedMasterGroupId"] for r in rows if r["family"] == family}), "allCategoriesAssigned": False} for family in sorted({r["family"] for r in rows})]

    def measurement(stage):
        return {"stage": stage, "artifactPath": None, "sha256": None, "tool": None, "toolVersion": None, "method": None, "countingDomain": None, "sourceQuadsOrPolygons": None, "sourceTopologyVertexCount": None, "indexCount": None, "primitiveModesObserved": [], "primitiveMeasurementTemplate": {"mode": None, "indexed": None, "indexAccessor": None, "indexEntryCount": None, "positionAccessor": None, "positionAccessorRows": None, "referencedAttributeVertexRows": None, "triangles": None, "method": None}, "exportedTriangles": None, "degenerateTriangles": None, "storedAttributeVertexRows": None, "referencedAttributeVertexRows": None, "distinctPositionCount": None, "positionComparisonSpace": None, "positionComparisonTolerance": None, "primitiveMeasurements": [], "instanceSelections": [], "materialTextureHashes": [], "sourceTransform": None, "pivot": None, "units": None, "measured": False}

    candidates = []
    for candidate in grouped["candidates"]:
        pid = candidate["productionId"]
        row = row_by_id[pid]
        category = categories[proposed_categories[pid]]
        candidates.append({"productionId": pid, "family": row["family"], "role": candidate["role"], "sourceBoard": candidate["sourceBoard"], "reuse": row["reuse"], "experimentalGuideCategoryProposal": category["categoryId"], "categoryApplicationApproved": False, "triangleHypotheses": category, "minimumVertexCount": None, "visualSize": {"nativeInventoryPixels": candidate["native"]["logicalPixels"], "nativeCells": candidate["native"]["cellFootprint"], "conceptCanvasPixels": candidate["sourceBoard"]["dimensions"], "twoItemLayoutReserve": grouped["composition"]["twoItemRegions"].get(pid), "threeItemLayoutReserve": grouped["composition"]["threeItemRegions"].get(pid), "physicalBoundsMeters": None, "approvedRelativeScale": None, "cameraViewportFovDepth": None, "measuredProjectedPixels": None}, "measurements": [measurement(s) for s in ("source-identity-selection", "extracted-child", "reopened-child")], "drawableLodMeasurements": [], "partitionQa": {"exclusiveIdentityAssignmentVerified": False, "unassignedTriangleCount": None, "multiplyAssignedTriangleCount": None, "sourceChildTriangleDifference": None, "differenceExplanation": None, "roundTripPreservationVerified": False}, "resourcesPolicy": "Keep immutable joint/source master and exact child lineage. Preserve UVs/normals/tangents/materials/textures/atlas/transforms/pivot. Shared resources can be duplicated in self-contained exports; do not claim memory saving."})
    return {"format": "d3d.item-geometry-budget-linkage", "schemaVersion": 1, "date": grouped["date"], "studyId": grouped["studyId"], "status": study["status"], "purpose": study["scope"], "humanEvent": study["humanEvent"], "sourceFiles": [{"path": p, "sha256": digest(p)} for p in INPUTS], "researchGuide": study["researchGuide"], "operatorComplement": study["operatorComplement"], "referencePreparationPolicy": grouped["referencePreparationPolicy"], "totalGeometry": study["totalGeometry"], "metricPolicy": study["metricPolicy"], "visualSizePolicy": study["visualSizePolicy"], "closeViewQa": study["closeViewQa"], "runtimePolicy": study["runtimePolicy"], "sourceContracts": study["sourceContracts"], "families": families, "entries": entries, "candidates": candidates, "jointMeasurement": measurement("joint-source"), "countsSelected": False, "newJobsAuthorized": 0, "creditsAuthorized": 0}


def geometry_markdown(d):
    guide = d["researchGuide"]
    text = ["# Geometria — vínculo de categorias, identidades e medidas", "", "Atualização: **8 de outubro de 2026**. Requisito `" + d["humanEvent"] + "`. **Vínculo preparado; nenhum alvo selecionado, contagem medida, download concluído, job ou gasto novo.**", "", "Este derivado liga as 228 identidades e famílias da fila existente aos grupos de reuso propostos. Não cria prioridades, outra fila ou reservas. Apenas Scroll/Healing/Mana têm categoria experimental proposta nesta passagem; as outras identidades mantêm categoria pendente. Nenhum compartilhamento de geometria está aprovado.", "", "O guia cloud chegou por resumo repassado pelo pai e pelo operador único. As faixas abaixo são **hipóteses de teste em triângulos exportados**, não mínimos medidos de vértices. O arquivo original/revisão/hash do guia não foi fornecido. Metadados completos e campos de medição estão no [manifesto](geometry-budget-linkage.json).", "", "| Categoria do guia | Uso a 40–150 px: triângulos | Inspeção próxima a 250–700 px: triângulos |", "| --- | ---: | ---: |"]
    for rule in guide["categoryRules"]:
        usage, close = rule["usageTrianglesExperimental"], rule["firstPersonTrianglesExperimental"]
        usage_text = f"{usage[0]}–{usage[1]}" if usage else "Não fornecido"
        close_text = f"{close[0]}–{close[1]}" if close else "Não fornecido"
        text.append(f"| {rule['label']} | {usage_text} | {close_text} |")
    text += ["", "Os intervalos em pixels também são propostas do guia, sem câmera/FOV/distância/viewport medidos nos candidatos. Mínimo de vértices continua **pendente**. Não derivar vértices por proporção fixa de triângulos.", "", "## Candidatos e tamanhos disponíveis", "", "| ID | Família / categoria proposta | Inventário / prancha | Reuso e medida 3D |", "| --- | --- | --- | --- |"]
    for c in d["candidates"]:
        text.append(f"| `{c['productionId']}` | `{c['family']}` / `{c['experimentalGuideCategoryProposal']}` | 28×28 px, 1 célula / 1536×1024 px | Conceito direto; reuso de malha não aprovado. Escala física/bounds/projeção/vértices pendentes. |")
    text += ["", "Scroll corresponde ao rolo amarrado, com espirais e fita; Healing/Mana, ao frasco de vidro, gargalo, rolha, cordão e líquidos vermelho/azul. Healing/Mana podem compartilhar master com material diferente se isso passar QA. As regiões da composição são reservas de enquadramento, sem definir escala física. Mana continua opcional; Short Sword está fora, e Great Sword/Shirotachi continuam excluídos de novos jobs.", "", "## Geometria total — hipótese, sem alvo escolhido", "", "| Composição proposta | Uso a 40–150 px | Inspeção a 250–700 px | Margem experimental 2× para master de inspeção |", "| --- | ---: | ---: | ---: |", "| Scroll + Healing | 400–1.400 triângulos | 1.500–5.000 triângulos | 3.000–10.000 triângulos |", "| Mais Mana, opcional | 700–2.300 triângulos | 2.500–8.000 triângulos | 5.000–16.000 triângulos |", "", "Essas somas não garantem distribuição, densidade, qualidade ou resultado do serviço. Reuso de uma garrafa pode alterar a contagem de recursos únicos. Não dividir igualmente o total. Aumentar o alvo total ainda exige escolher a métrica, configurar o input final e reconferir preço/teto; os controles 5.000/10.000 observados no formulário vazio não garantem faces por item. Os exemplos anteriores de 100/40/20 créditos continuam condicionais à configuração. O exemplo cloud de seis itens não amplia este piloto.", "", "A aproximação em quads relatada pelo guia não é adotada como medição ou alvo: não dividir triângulos por dois sem topologia de autoria e triangulação verificadas. Quad na UI não comprova quad no GLB. Contagens do teste manual Great Sword são relatos da UI, sem transferir densidade aos candidatos.", "", "## Medição e preservação", "", "Registrar, por identidade e etapa: arquivo/hash, ferramenta/versão/método, seleção de nodes/instâncias/primitivas, quads/polígonos de autoria quando disponíveis, triângulos efetivos/degenerados, registros de vértices armazenados e referenciados, modo da primitiva e quantidade de entradas do índice. Registros referenciados distintos não são entradas do índice; índices ÷ 3 vale somente para primitivas indexadas TRIANGLES válidas. Posições coincidentes são diagnóstico com espaço/tolerância explícitos; não demonstram vértices soldados. Preservar splits de UV/normais/tangentes. Distinguir recursos únicos de instâncias/draws e de dados duplicados em filhos autocontidos.", "", "Comparar seleção original → filho extraído → filho reaberto, com a mesma triangulação e domínio de contagem, transforms recompostos e atribuição exclusiva. Registrar faces sem identidade ou atribuídas repetidamente. Extração pura conserva os triângulos/superfícies; diferenças de registros de vértices precisam de explicação e QA. Mais geometria não comprova faces ocultas, silhueta ou material corretos.", "", "Preservar conjunto e master imutáveis, UVs, normais, materiais/texturas/atlas, unidades, pivôs e linhagem. Componentes separados podem ser tampa/frasco/líquido do mesmo ID; fusão entre identidades não é resolvida automaticamente. Exportar filhos autocontidos e reabrir sem perdas. Arquivos e contagens reais ainda inexistem nesta entrega; complemento do operador tem estado próprio no manifesto.", "", "LODs de 100/50/25% por item e avaliação de desvio em torno de 1 px são propostas do guia, sujeitas a silhueta, bordas, pontas, reflexos e materiais. Referência drawable aceita e master preservado são papéis distintos. O [seletor existente](../TOWN-ASSET-LOD-SELECTOR.md) exige erro conservador e tamanho projetados; não impõe orçamento artístico por categoria nem autoriza redução para satisfazer o parser. O erro aproximado/normalizado retornado pelo simplificador, possivelmente incluindo atributos, não preenche automaticamente o erro geométrico conservador do contrato nem torna `errorVerified=true`. Registrar métrica, unidade e referência; demonstrar o limite conservador e revisar geometria/material antes da seleção. Nenhum LOD foi produzido ou selecionado.", "", "Fontes primárias para a semântica das ferramentas, sem atribuir-lhes as faixas numéricas do guia: [meshoptimizer — simplificação](https://meshoptimizer.org/#simplification), [Blender — glTF](https://docs.blender.org/manual/en/5.1/addons/import_export/scene_gltf2.html), [Blender — Separate](https://docs.blender.org/manual/en/5.1/modeling/meshes/editing/mesh/separate.html).", "", "[Estudo agrupado](GROUPED-ITEMS-STUDY.md) · [Contrato de itens](../ITEM-PRODUCTION-CONTRACT.md)", ""]
    supplement = d["operatorComplement"]
    if supplement["sha256"]:
        text += [f"**Complemento do operador conferido:** `{supplement['receiptFileName']}`, SHA-256 `{supplement['sha256']}`. Bytes/hash e linhagem com o recibo anterior foram verificados por Itens; nenhum recibo foi sobrescrito. O complemento distingue peça rígida de armadura; campos não fornecidos permanecem pendentes. Download e medições do modelo continuam sem confirmação.", ""]
    text += ["**Método de referência posterior:** original real identificado → imagem fiel/nítida via imagegen, luz neutra e sem sombras marcadas → Smart Mesh → download/separação → QA → integração pelo principal. Preservar originais, usar imagem individual ou composição deliberada com IDs/hashes externos e não tratar prancha multivista como grupo. Nenhuma imagem nova foi preparada ou aprovada neste delta; alvo, cotação e teto continuam pendentes.", ""]
    return "\n".join(text)


def grouped_markdown(g):
    text = [
        "# Estudo — dois ou três itens distintos numa geração", "",
        f"Atualização: **8 de outubro de 2026**. Estudo `{g['studyId']}`. **Somente planejamento; separabilidade não demonstrada. Imagem criada, upload, geração, reserva e consumo: zero.**", "",
        "O novo pedido humano considera **2–3 itens distintos → uma imagem → uma geração → separação local**. É uma exceção delimitada para estudar economia; não é multivisão do mesmo objeto, controle de lote ou autorização de execução. Substitui o desenho do experimento individual anterior, preservado como comparação. Não cria outra fila.", "",
        "## Candidatos condicionais", "",
        "Proposta: **Scroll + Healing**, com **Mana opcional**. Short Sword e lâminas finas ficam fora deste experimento. Nenhuma escolha ou referência está aprovada; todas as fontes abaixo ainda são pranchas RGB sem alpha.", "",
        "| Papel | ID / fonte | SHA-256 | Inventário nativo |", "| --- | --- | --- | --- |",
    ]
    for c in g["candidates"]:
        a = c["sourceBoard"]
        role = "Opcional" if c["role"].startswith("optional") else "Proposto"
        text.append(f"| {role} | `{c['productionId']}`<br>[{c['revision']}]({md_link(a['path'])}) | `{a['sha256']}` | 28×28 px, 1×1 célula; ICURS {c['native']['cursorValue']} |")
    text += ["", "As duas propostas iniciais cobrem 26 registros base; com Mana, 28. Esses vínculos não criam 26/28 malhas. Os três candidatos têm procedência `image_gen.imagegen`, fontes e hashes preservados, sem master/ícone final instalado. A existência de modelos manuais/remotos próprios segue desconhecida. Great Sword e Shirotachi continuam excluídos de novos jobs.", "", "## Composição proposta, ainda sem pixels produzidos", "", "Uma única representação completa de cada item, com espaços vazios generosos, sem sobreposição, contato, corte, texto, moldura, miniatura, ângulos repetidos ou chão/cenário incorporado. Fundo limpo e câmera/luz coerentes. Preservar inscrições legítimas. Healing mantém líquido vermelho; Mana, azul-cobalto.", "", "O manifesto reserva regiões normalizadas para duas ou três posições. IDs e a associação posição→item ficam em metadados externos. As regiões são um esquema de composição, não escala física medida. Aprovar dimensões relativas antes de preparar a imagem; 28 px de inventário não definem centímetros. Não deformar os objetos para preencher caixas.", "", "Preservar pranchas originais e futuros inputs isolados individuais como backups. A imagem composta final, seu hash, escala e aprovação estão pendentes. Não usar uma prancha existente inteira nem executar geração de imagem paga nesta preparação.", "", "## Comparação de créditos", "", "Hipótese de geometria: **100 por geração**, sem cotação final da imagem conjunta. Separação local usa **zero créditos de serviço**, mas tem custo de tempo, ferramentas, computação e QA. Os valores adicionais relatados na UI estão abaixo, sem cotação confirmada para o input final. Textura20 não foi somada automaticamente.", "", "| Objetos distintos | Trial único reconfirmado em cada caminho alternativo | Conjunto + separação local | Individual, uma geração por identidade | Economia bruta potencial |", "| ---: | --- | ---: | ---: | ---: |"]
    for b in g["budget"]["conditionalComparisons"]:
        text.append(f"| {b['objects']} | {'Sim, condicional' if b['trialScenario'] else 'Não'} | {b['groupedGeometryCredits']} | {b['individualGeometryCredits']} | {b['grossPotentialSavingCredits']} |")
    text += ["", "A coluna trial compara a **mesma oportunidade única** nos caminhos alternativos: não se aplica um trial por objeto nem economia promocional recorrente. Sua elegibilidade para o input conjunto e para os individuais é desconhecida; se diferir, refazer a comparação. Nenhum preço pós-input, economia líquida ou teto está confirmado.", "", "**Comparação com reuso:** Healing e Mana compartilham a proposta de garrafa. Se um master/material compatível for aceito, a terceira identidade pode usar derivado de material sem uma terceira geração de geometria. Nesse caso, comparar o conjunto de três contra duas gerações mais reuso, e não presumir 300 de baseline. O reuso ainda não está aprovado.", "", "Sem extras, a economia bruta hipotética é 100 contra duas gerações ou 200 contra três gerações independentes. Para três identidades com reuso Healing/Mana aceito e somente duas gerações de geometria, a margem volta a 100, descontando também qualquer trabalho/extra específico de material. A margem aplicável é sempre custo do baseline escolhido menos custo conjunto. Uma geração conjunta de 100 seguida da recuperação individual de um item por 100 elimina a economia contra dois individuais de 200; duas recuperações eliminam a economia contra três individuais de 300. São exemplos aritméticos: nenhuma recuperação/repetição é autorizada. Recalcular também tempo local e extras pagos antes de concluir vantagem.", "", "## Extras do serviço — alternativa à separação local", ""]
    text += ["O operador único relatou no modelo existente: **Dividir/Equilíbrio, 6–15 partes, 40 riscado/0 com Teste nova função ×1**; **SmartUV, 20 riscado/0 com Teste ×2**. São ações do serviço, diferentes da separação local de zero créditos. Elegibilidade final e combinação desses trials não foram confirmadas. Preencher Partes exige modelo segmentado e não revelou preço nessa etapa. Textura20 permanece adicional e não executada.", "", "Como comparação aritmética sem trials, se os extras forem necessários somente no caminho conjunto: geometria 100 + Dividir 40 = **140**; acrescentar SmartUV 20 = **160**. Contra baseline 200 (dois objetos ou três identidades com reuso), sobram 60/40; contra três gerações independentes 300, sobram 160/140, antes do esforço local. Comparar somente extras adicionais relativos ao caminho individual/reuso.", "", "O remesh básico Quad 10.000 mostrou **10**, com Smart P1.0 desligado. Ao consultar Smart, a UI informou incompatibilidade de Low Poly Smart com Malha Smart e desativou a opção. Portanto, 10 **não é cotação de retopologia Smart** nem alternativa automaticamente compatível com o método escolhido. Somá-lo aos 160 daria 170 apenas na aritmética, com margens 30/130; não há execução proposta ou aprovada. Nenhum desses exemplos soma Textura20, Preencher Partes ou trabalho local sem avaliação.", ""]
    text += ["## Separação e QA exigidos", ""]
    checks = [
        ("Identidade e partes", "Mapear cenas/nodes, instâncias, meshes, primitivas e componentes para cada ID. Vidro, líquido, rolha e cordão podem ser várias partes de um único frasco; componente conectado não é sinônimo de item."),
        ("Geometria separável", "Sem pontes, faces compartilhadas, interseções entre itens, contato fundido ou fundo incorporado. Extrair grupos por identidade sem cortar ou reconstruir superfícies para declarar sucesso."),
        ("Faces ocultas", "Revisar isoladamente em 360°: verso, base, espessura, gargalo, aberturas, espirais e amarrações. Não assumir faces geradas nem fechar aberturas legítimas automaticamente."),
        ("UV e materiais", "Preservar UVs, normais, texturas, fatores, alpha e samplers. Atlas/material compartilhado pode ser mantido; mistura de cor ou sombra entre itens exige revisão. Repack/rebake cria revisão derivada e novo QA."),
        ("Pivô e escala", "Registrar transforms e offsets de origem; remover apenas o posicionamento da composição. Preservar proporções/unidades e calibrar uniformemente uma vez, sem deformação XYZ."),
        ("Exportação e reabertura", "Manter GLB conjunto imutável. Exportar filhos autocontidos com hashes e linhagem; reabrir e comparar geometria, transforms, UVs, normais, materiais e imagens. A ferramenta de separação ainda não foi validada."),
        ("Detalhe e apresentações", "A contagem-alvo da saída conjunta não é orçamento de polígonos por item. Conferir detalhe próximo, ícone, ground/equipado, materiais e fallback a partir do mesmo master/material aceito."),
    ]
    for title, desc in checks:
        text.append(f"- **{title}:** {desc}")
    text += ["", "Interromper promoção dos itens afetados se houver identidade ambígua, fusão, partes/superfícies necessárias ausentes, contaminação de materiais, perda na exportação, detalhe insuficiente ou reconstrução/retexturização/custo que anule o ganho. Aproveitamento parcial precisa de registro por ID; separar arquivos não concede aprovação artística. Nenhuma recuperação paga automática.", "", "## Linhagem e estado atual", "", "Registrar ID do estudo e IDs individuais → fontes aprovadas/hash → imagem composta/hash e mapping externo → tarefa compartilhada → GLB conjunto/hash → partes/transforms selecionados → GLBs filhos/hash e atlas/material → QA e aprovação por apresentação. Não presumir redução de memória: arquivos autocontidos podem duplicar texturas.", "", "O operador único tentou exportar o manual Great Sword existente em GLB e, como recuperação, FBX nativo, sem cobrança adicional exibida. Os downloads não foram confirmados após timeout, sem arquivo no prefixo local auditado pelo operador; não há GLB/hash nesta frente. Na rotação visual, ele relatou cinco volumes, guardas/proporções incoerentes, miniatura menor e um nó na hierarquia. Não programou repetição ou rota oculta. Sua referência r3 foi correlacionada por hash, mas cinco silhuetas não comprovam componentes separáveis, UVs/materiais, pivôs ou faces ocultas. O arquivo não foi atribuído como master dos candidatos deste estudo. Itens conferiu o hash do recibo final do operador e os bytes/hashes dos 12 arquivos de evidência, sem copiar a mídia privada, inspecionar estas novas capturas ou acessar a conta; isso não substitui QA do modelo.", "", "**Pendências:** aprovação dos candidatos/vistas e escala; exclusão de modelos manuais/remotos; análise do GLB por cópia; composição final/hash; cotação após input, trial e extras; teto humano. Nenhuma dessas pendências é resolvida pela escolha global de Malha Smart.", "", "[Manifesto do estudo](grouped-items-study.json) · [Lista completa de itens](SMART-PRODUCTION-REVIEW.md) · [Contrato de produção](../ITEM-PRODUCTION-CONTRACT.md)", ""]
    text += ["[Geometria por categoria e medidas pendentes](GEOMETRY-BUDGET-LINKAGE.md): guia cloud recebido como hipótese de triângulos/LOD, sem mínimo medido de vértices, alvo total escolhido ou ampliação do piloto.", ""]
    return "\n".join(text)


def markdown(d):
    s = d["summary"]
    rows = d["entries"]
    by_id = {r["productionId"]: r for r in rows}
    first = d["firstSmallLotProposal"]["productionIds"]
    text = [
        "# Malha Smart — lista completa para revisão e orçamento condicional", "",
        "Atualização: **8 de outubro de 2026**. **Método escolhido: Malha Smart. Gerações/consumo autorizados por este pacote: zero.**", "",
        "A direção humana foi repassada no evento `Sentinel_d13a92ff08748191968cf63ddc3307c2`: “vamos fazer todos os itens com essa malha smart com certeza”. Isso escolhe o método para itens que realmente precisarem de nova produção, preservando modelos manuais bons e reusos aprovados. Não aprova arte, lote, orçamento, GLBs ou instalação.", "",
        "Este documento deriva a lista e a ordem da fila existente. Não cria outra fila de produção ou registro de reservas. O [manifesto completo](smart-production-review.json) mantém IDs, variantes, proveniência/hashes, referências, exclusões, vínculos master/material/LOD/ícone e QA por ativo. A autoridade de reserva/aceitação continua em `assets/registry.json`; o principal mantém core/build/instalação/Git.", "",
        f"São **{s['productionIdentities']} identidades**, **{s['linkedConceptIdentities']} ligadas a conceitos**, **{s['referenceBlocked']} bloqueadas por referência** e **{s['explicitReuseEdges']} propostas explícitas de reuso**. O grafo propõe {s['proposedGroupsAll']} grupos, {s['proposedGroupsWithReference']} com referência. Esses grupos ainda não são meshes ou jobs aprovados; uma família pode compartilhar mais, exigir separação ou manter material/variante próprio.", "",
        f"Após excluir o grupo manual Great Sword, restam **{s['conditionalRemainingGroupsWithReference']} grupos com referência** e **{s['conditionalRemainingGroupsAll']} no catálogo inteiro** para cenários futuros. Existem **{s['manualExclusionUnknownIdentities']} identidades com exclusão manual/remota desconhecida**. Desconhecido bloqueia submissão. Entradas individuais prontas/aprovadas: zero; masters validados: zero; {s['incompleteConceptIdentities']} identidades conservam gates de estudo incompleto.", "",
        "## Proposta individual anterior — preservada como comparação", "",
        "O [estudo atual de agrupamento](GROUPED-ITEMS-STUDY.md) considera Scroll + Healing, com Mana opcional, numa imagem/geração e separação local. Short Sword fica fora desse experimento. A proposta individual abaixo está preservada como histórico/comparação, sem execução em paralelo.", "",
        "Os três candidatos individuais anteriores têm o piloto Scroll **incluído uma única vez** no total. Nenhuma referência individual foi aprovada. Seus valores não são o orçamento automático do novo estudo agrupado.", "",
        "| Ordem proposta | Identidade | Fonte candidata / SHA-256 | Entrada individual |", "| --- | --- | --- | --- |",
    ]
    for i, pid in enumerate(first, 1):
        r = by_id[pid]
        a = r["artCandidate"]
        text.append(f"| {i} | {r['name']} — `{pid}` | [{a['revision']}]({md_link(a['path'])}); `{a['sha256']}` | Pendente; prancha sem aprovação específica. |")
    text += ["", "As vistas propostas estão no manifesto. O [handoff de inventário](INVENTORY-PREP-HANDOFF.md) registra espada 28×84 px/1×3 células e Scroll/Healing 28×28 px/1×1, além das receitas de ícone. Elas não são inputs 3D preparados. Healing exige revisão de vidro/líquido; Short Sword exige revisão de lâmina fina, guarda e grip. Outros candidatos permanecem na ordem original da fila.", "", "## Exclusão do teste manual Great Sword", ""]
    for m in d["context"]["existingAssetAudit"]["resolvedManualExclusions"]:
        a = m["sourceCandidate"]
        text += [f"O humano/operador relatou o [teste manual `{m['providerTaskId']}`]({m['providerUrl']}): **uma imagem produziu um modelo com cinco silhuetas de espada**, sem comprovar cinco jobs ou cinco componentes de mesh. UI reportada: Quad, {pt_int(m['facesReported'])} faces e {pt_int(m['verticesReported'])} vértices. O humano relata Malha Smart no teste; engine/custo históricos, GLB/hash, UVs, materiais e QA permanecem pendentes.", "", f"A pasta r3 indicada contém um único PNG: [Great Sword r3]({md_link(a['path'])}), SHA-256 `{a['sha256']}`. Bytes e imagem locais foram conferidos. O operador confirmou a correlação exata: input.png baixado pelo botão de referência tem 1.644.705 bytes e o mesmo SHA-256 da r3 local. Itens conferiu os bytes locais e registrou a evidência do operador, sem acessar a conta. A fonte é uma prancha gerada por `image_gen.imagegen`; a r3 permanece uma correção defeituosa arquivada, com ramo superior da guarda BACK ausente. A seleção anterior não foi promovida ou substituída.", "", "**Great Sword e o reuso Shirotachi têm zero novos jobs propostos até QA/reuso.** Somente Great Sword tem um candidato manual reportado; a existência de um modelo próprio Shirotachi continua desconhecida. Preservar o candidato manual; não regenerar automaticamente nem confundi-lo com o UUID de 08-03. O resultado concreto reforça o gate padrão: uma imagem de entrada deve conter um único objeto completo, sem título, várias vistas e miniatura no mesmo canvas. O novo estudo agrupado é uma exceção delimitada para itens distintos, ainda sem separabilidade demonstrada ou execução autorizada.", ""]
    text += ["O operador também reportou quatro `TripoDemoModel` e o ativo `ee4a954e-6107-4677-8452-8de8e83751da`, com data exibida 08-03. Identidade, procedência, origem manual e qualidade são desconhecidas. Não foram ligados a itens nem usados para declarar ausência de duplicatas. Itens não consultou a conta ou o ledger remoto em paralelo.", "", "## Orçamento", "", "O operador único reportou sessão autenticada e header de **3.200 créditos**. É saldo observado por ele, não teto de gasto. O menu autenticado mostra Pro segundo o operador. O formulário vazio P2.0/Quad/5.000/uma saída mostra 100 riscado e 0 com P2 Teste ×1: um trial, não preço universal zero. A elegibilidade para a referência final, custo após input e destino privado ainda precisam de reconferência. O formulário novo H3.1/55 não identifica engine ou custo do teste manual histórico.", "", "Os cenários abaixo preservam a proposta individual anterior e a projeção do catálogo; não são o orçamento do novo experimento agrupado. A hipótese usa **100 créditos por uma saída**, apenas do preflight público deslogado P2.0/Quad/controle 5.000. Não é cotação autenticada nem preço universal. O cenário alternativo de 200 considera somente um trial de 0 para um dos três ativos, se reconfirmado após input aprovado. Os demais cenários mantêm 100 como hipótese. O botão Textura20 observado é uma ação adicional não executada: não foi somado, pois configuração/custo final seguem pendentes. Os cenários não incluem variantes, tentativas automáticas, extras de textura/pós-processamento, preparação paga ou compra adicional; nenhum total é autorizado.", "", "| Cenário condicional | Saídas hipotéticas | Créditos pela hipótese de 100 |", "| --- | ---: | ---: |"]
    for b in d["budgetScenarios"]:
        text.append(f"| {b['label']} | {b['hypotheticalOutputs']} | {pt_int(b['conditionalCredits'])} |")
    text += ["", "O cenário de 13.400 já inclui os três candidatos iniciais: **300 + 13.100 = 13.400**, se todos os gates e reusos forem aprovados. Não somar novamente o piloto ou o pequeno lote ao total agrupado. Dagger foi uma alternativa técnica não selecionada na proposta individual anterior. Ela não integra o novo estudo agrupado.", "", "Os reusos exigem atenção particular em Undead Crown/Helm of Spirits (base78/override77), Gnarled Root (Club/Spiked Club) e Ear (família de três classes). O inspector público contém 130 registros sem mapping explícito de item; isso não comprova ausência de um modelo manual ou remoto.", "", "Total autenticado confirmado: **pendente**. Teto humano aprovado: **pendente**. Jobs elegíveis/autorizados agora: **zero**. As contagens agrupadas dependem de aprovação dos reusos e da exclusão manual/remota; não são limites financeiros inferior/superior. Não consumir os 3.200 créditos por inferência da escolha de método.", "", "## Entrada correta no Studio", "", "O operador confirmou **Imagens multivisão para 3D** no segundo ícone/cubo: campos Frente, Esquerda, Direita e Verso descrevem o mesmo objeto. Cada imagem deve isolar uma vista completa e coerente. Não colocar a prancha inteira num slot; uma vista 3/4 não substitui um perfil Esquerda/Direita. A r3 de Great Sword conserva defeito na guarda BACK e não está apta a formar um conjunto coerente.", "", "O terceiro ícone/pilhas é **Imagens em lote para 3D**, até dez imagens de objetos separados. Ele não equivale a multivisão, não divide uma prancha com cinco desenhos em um objeto único e não concede autorização para lote. Por padrão, um único objeto por imagem também vale para a entrada de vista única. A exceção proposta no estudo agrupado usa uma representação de cada item distinto; não legitima várias vistas/miniaturas de uma mesma espada no mesmo canvas. Preservar inscrições legítimas do item, removendo apenas títulos/labels/molduras/miniaturas da prancha.", "", "## Gates e QA por ativo", ""]
    for key, description in d["commonQaGates"].items():
        text.append(f"- **{key}:** {description}")
    text += ["", "Cada linha do manifesto inclui os checks da família e entregáveis separados. Armas finas, vidro/material, flail/corrente, arco/corda, roupas/skin/anatomia, contagem de moedas e variantes de orelha exigem revisão própria. Uma etiqueta Quad ou um número de polígonos não libera qualidade. Fallback e todos os dados/tempos nativos permanecem obrigatórios.", "", "## Lista completa na ordem da fila existente", "", "A coluna Grupo representa reuso **proposto**, com nomes curtos abaixo apenas para leitura. M = grupo manual excluído até QA; ? = existência manual/remota desconhecida. Todos os inputs individuais e custos por ativo continuam pendentes. Fontes com link são pranchas candidatas; o hash completo está por linha no manifesto.", "", "| Seq. | Nome / ID | Família | Referência / arte candidata | Grupo proposto | Manual |", "| ---: | --- | --- | --- | --- | --- |"]
    for r in rows:
        a = r["artCandidate"]
        ref = f"[{a['revision']}]({md_link(a['path'])})" if a else (r["referenceBlocker"] or "bloqueada")
        if r["conceptIncomplete"]:
            ref += "; estudo incompleto"
        group = r["reuse"]["proposedMasterGroupId"].removeprefix("item.design.")
        manual = "M — zero novos jobs" if r["manualExclusion"]["status"] == "known-existing" else ("? — excluído pela raiz manual" if r["manualExclusion"]["excludedFromNewJobs"] else "?")
        text.append(f"| {r['priority']['existingQueueSequence']} | {r['name']}<br>`{r['productionId']}` | {r['family']} | {ref} | `{group}` | {manual} |")
    text += ["", "## Reprodução e limites", "", "`python -B docs/items-study/build_smart_production_review.py --check` confere os cinco derivados sem escrever. O gerador lê apenas arquivos públicos locais e verifica os PNGs autorais por hash; não abre conta, mídia de modelo, credenciais ou referências licenciadas privadas. O contexto é um recibo de decisões/relatos, não autorização de execução. Nenhum upload, geração, reserva, API, instalação ou teste da partida foi realizado por este pacote.", ""]
    return "\n".join(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    d = build()
    outputs = {"smart-production-review.json": json.dumps(d, ensure_ascii=False, indent=2) + "\n", "SMART-PRODUCTION-REVIEW.md": markdown(d), "GROUPED-ITEMS-STUDY.md": grouped_markdown(d["groupedStudy"]), "geometry-budget-linkage.json": json.dumps(d["geometryBudgetLinkage"], ensure_ascii=False, indent=2) + "\n", "GEOMETRY-BUDGET-LINKAGE.md": geometry_markdown(d["geometryBudgetLinkage"])}
    bad = []
    for name, content in outputs.items():
        path = DIRECTORY / name
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                bad.append(name)
        else:
            path.write_text(content, encoding="utf-8")
    print(json.dumps({"passed": not bad, "mode": "check" if args.check else "write", "stale": bad, "summary": d["summary"]}, ensure_ascii=True))
    return bool(bad)


if __name__ == "__main__":
    sys.exit(main())
