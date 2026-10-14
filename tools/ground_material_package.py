#!/usr/bin/env python3
"""Package six private ground candidates with exact input/output provenance.

Does not install or approve assets. No original/generated media is distributable
by virtue of this manifest. Requires Pillow, plus the local QA report.
"""
from __future__ import annotations

import argparse
import hashlib
import html
import json
from datetime import datetime, timezone
from html.parser import HTMLParser
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class LocalLinks(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.links: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        for name, value in attrs:
            if name in {"href", "src"} and value and not value.startswith(("http:", "https:", "#", "data:")):
                self.links.append(value)


def verify_local_links(root: Path) -> int:
    count = 0
    for page in (root / "index.html", root / "pilot-world/index.html"):
        if not page.exists():
            continue
        parser = LocalLinks()
        parser.feed(page.read_text(encoding="utf-8"))
        for link in parser.links:
            if not (page.parent / link).is_file():
                raise ValueError(f"Missing local preview link: {page.name}: {link}")
            count += 1
    return count


def comparison_overview(root: Path) -> dict | None:
    directory = root / "pilot-world"
    if not (directory / "comparison-report.json").exists():
        return None
    rows = (("close-detail", "Terra / perto"), ("grass-close", "Grama / perto"), ("iso", "Caminhos / isometrico"))
    board = Image.new("RGB", (1440, 1034), (20, 21, 24))
    draw = ImageDraw.Draw(board)
    font = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 16)
    draw.text((12, 8), "ESTUDO PRIVADO OFFLINE / mesma camera / so 32 ocorrencias auditadas / transicoes nativas preservadas", font=font, fill="white")
    columns = (("world-native", "Nativo exportado"), ("legacy-per-tile-anisotropic", "Master / UV por peca"), ("world-anisotropic", "Master / UV continuo no mundo"))
    for row, (name, label) in enumerate(rows):
        y = 40 + row * 330
        for col, (suffix, title) in enumerate(columns):
            draw.text((col * 480 + 10, y), f"{label} - {title}", font=font, fill="white")
            with Image.open(directory / f"{name}-{suffix}.png") as source:
                # A comparison board: only display scaling of already rendered frames.
                board.paste(source.convert("RGB").resize((480, 300), Image.Resampling.LANCZOS), (col * 480, y + 26))
    path = root / "comparison-overview.png"
    board.save(path)
    return {"path": path.name, "sha256": digest(path), "dimensions": list(board.size),
            "transform": "Three fixed rendered cameras, display-scaled into columns; no artwork modification"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="Private versioned candidate directory")
    args = parser.parse_args()
    root = args.root.resolve()
    qa = json.loads((root / "qa/report.json").read_text(encoding="utf-8"))
    families = {"grass": ("Grama rala", "native-grass-identity", "Blades appear dry/fibrous; review close scale and species identity."),
                "dirt": ("Terra compactada", "native-dirt-identity", "Mauve-brown reference direction; no exact albedo recovery claimed."),
                "dry": ("Grama seca", "inferred-subdivision", "Similar to grass candidate; do not count as a verified separate native family."),
                "mud": ("Barro", "inferred-subdivision", "Similar to dirt; wetness is not physically measured or validated."),
                "stone": ("Solo pedregoso", "inferred-subdivision", "Slate ground interpretation; does not authorize adding paved paths."),
                "bank": ("Substrato de margem", "inferred-subdivision", "Substrate only; never assign to water without verified native stencil.")}
    generator_files = {"grass": "exec-ae7a30c2-1ed8-4ded-822e-6b041b60f1a7.png",
                       "dirt": "exec-948cd819-99db-439b-a6df-e863ceb05ad0.png",
                       "dry": "exec-64177dd4-b784-4f78-b725-602282577d14.png",
                       "mud": "exec-77077d53-3cfa-4646-84fe-cb2982644dfc.png",
                       "stone": "exec-93ed0cdf-65de-43fb-a829-b274996474cc.png",
                       "bank": "exec-222a06c7-09b5-44a2-9bd8-74df47f32e2f.png"}
    records = []
    for family, (label, evidence, caveat) in families.items():
        path = root / "masters" / f"{family}-r1.png"
        prompt = root / "prompts" / f"{family}-r1.txt"
        image = Image.open(path)
        second_reference = "native-river-bank.png" if family in {"stone", "bank"} else "native-path-grass.png"
        inputs = [root / "references/native-pieces-00.png", root / "references" / second_reference]
        qa_entry = next(entry for entry in qa["masters"] if entry["source"]["sha256"] == digest(path))
        records.append({"catalogId": "tristram.terrain.native-ground", "candidateFamily": family,
                        "displayName": label, "revision": "20261008-r1", "state": "candidate-private-review",
                        "accepted": False, "selectedForRuntime": False, "semanticEvidence": evidence,
                        "path": path.relative_to(root).as_posix(), "sha256": digest(path),
                        "dimensions": list(image.size), "mode": image.mode,
                        "colorspace": "assumed sRGB; generator supplied no embedded profile",
                        "channel": "basecolor-only", "normal": None, "roughness": None, "height": None,
                        "metallic": None, "basecolorLightingConstraint": "requested flat unlit; appearance inspected, no inverse-rendering proof",
                        "worldPeriod": [2.0, 2.0], "scaleUnits": "native dPiece world units, not validated meters",
                        "texelsPerWorldUnit": [image.width / 2, image.height / 2], "origin": [0.0, 0.0],
                        "generator": {"tool": "built-in image_gen.imagegen", "model": "not reported by tool",
                                      "sourceFileName": generator_files[family], "requestedDimensions": [2048, 2048],
                                      "actualDimensionsAuthoritative": True, "transparentBackground": False,
                                      "promptPath": prompt.relative_to(root).as_posix(), "promptSha256": digest(prompt),
                                      "inputRoles": [{"order": i + 1, "role": "original-game material/palette reference, not edit target",
                                                      "path": p.relative_to(root).as_posix(), "sha256": digest(p)} for i, p in enumerate(inputs)]},
                        "derivativesQaEntry": qa_entry["source"]["sha256"],
                        "visualReview": {"inspected": True, "repeat3x3Inspected": True,
                                         "artisticApproval": False, "notes": caveat},
                        "waterUse": "excluded; bank candidate is not a water material"})
    comparison = comparison_overview(root)
    report_path = root / "pilot-world/comparison-report.json"
    report_record = None
    if report_path.exists():
        report = json.loads(report_path.read_text(encoding="utf-8"))
        candidates = [candidate for view in report["views"].values() for candidate in view["candidates"].values()]
        conservation = {key: max(candidate[key] for candidate in candidates) for key in
                        ("outsideSupportChangedPixels", "unsupportedOpaqueChangedPixels", "coverageChangedPixels",
                         "protectedFallbackChangedPixels", "alphaChangedPixels")}
        if any(conservation.values()):
            raise ValueError("Comparison modified protected/native coverage pixels")
        report_record = {"path": "pilot-world/comparison-report.json", "sha256": digest(report_path),
                         "cameraMappings": len(report["views"]), "candidateRenders": len(candidates),
                         "maxConservationFailures": conservation}
    manifest = {"schema": "ground-material-candidate-package-v1", "private": True,
                "createdUtc": datetime.now(timezone.utc).isoformat(), "revision": "20261008-r1",
                "catalogIds": ["tristram.terrain.native-ground", "tristram.terrain.river-crossings"],
                "trigger": "Explicit user request: improve ground close to first-person camera and compare continuous terrain instead of tile-only appearance.",
                "engineIntegrated": False, "runtimeSelectionChanged": False, "artisticAcceptance": False,
                "provenance": {"kind": "new generated interpretations using private original-game references",
                               "referenceManifest": "references/reference-manifest.json",
                               "referenceManifestSha256": digest(root / "references/reference-manifest.json"),
                               "qaReport": "qa/report.json", "qaReportSha256": digest(root / "qa/report.json"),
                               "redistribution": "not cleared; keep generated and reference media local/ignored"},
                "materials": records, "comparison": report_record, "comparisonOverview": comparison,
                "runtimeProposal": {"detail": "world XZ UV, color filtering before final palette mapping",
                                    "coverage": "separate exact native binary opacity, index zero may be opaque",
                                    "mask": "audited semantic weights with native holdouts; no SOL/color automatic global classifier",
                                    "textureSizeCandidate": 1024, "masterPreserved": True,
                                    "normalRoughnessStatus": "not generated or validated"}}
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    cards = "".join(f'<article><h2>{html.escape(r["displayName"])}</h2><a href="{r["path"]}"><img src="{r["path"]}" alt="{html.escape(r["displayName"])}"></a><p>{r["dimensions"][0]} × {r["dimensions"][1]} · 627 texels/unidade · candidato</p><p>{html.escape(r["visualReview"]["notes"])}</p><details><summary>Prompt / origem</summary><a href="{r["generator"]["promptPath"]}">Prompt completo</a><code>{r["sha256"]}</code></details></article>' for r in records)
    page = '''<!doctype html><html lang="pt-BR"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Chão de Tristram — estudo privado</title><style>body{margin:0;background:#111315;color:#e5e2dc;font:16px system-ui}main{max-width:1400px;margin:auto;padding:32px}h1{font-size:32px}p{line-height:1.5;color:#bab9b3}nav{display:flex;gap:20px;flex-wrap:wrap;margin:24px 0}a{color:#d4b87b}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:24px}article{background:#1b1d20;padding:18px;border:1px solid #3a3c3d;border-radius:8px}article img{width:100%;aspect-ratio:1;object-fit:cover}h2{font-size:20px}code{display:block;overflow-wrap:anywhere;font-size:11px;margin-top:10px}.note{border-left:3px solid #b79962;padding:12px 18px;background:#1b1d20}</style><main><h1>Chão de Tristram · primeiro estudo</h1><p class="note">Estudo privado e offline. Seis candidatos gerados a partir de referências locais inspecionadas. Nenhum foi aplicado ao jogo. Sem normal/roughness ou aprovação artística. A comparação usa o mesmo mapa exportado e mantém células não classificadas intactas; não reproduz iluminação, GPU, paleta ou FPS da partida.</p><nav><a href="pilot-world/index.html">Comparar perto e isométrico</a><a href="qa/master-contact.png">Repetição e costuras dos seis masters</a><a href="manifest.json">Manifesto e hashes</a><a href="pilot-input/pilot-manifest.json">Área e máscaras auditadas</a></nav><p>Recomendação: aparência contínua por blocos, textura ancorada no mundo e desenho preservado por máscaras; grade nativa mantida para gameplay. Os comparativos confrontam isso com UV reiniciado em cada peça. Texturas mais detalhadas precisam também de mips e filtragem no renderer.</p><div class="grid">''' + cards + '</div></main></html>'
    if comparison:
        page = page.replace('<div class="grid">', '<a href="comparison-overview.png"><img style="width:100%;margin:8px 0 28px" src="comparison-overview.png" alt="Comparacao de perto e isometrica: nativo, UV por peca e UV continuo"></a><div class="grid">')
    (root / "index.html").write_text(page, encoding="utf-8")
    link_count = verify_local_links(root)
    print(json.dumps({"materials": len(records), "manifest": str(root / "manifest.json"),
                      "viewer": str(root / "index.html"), "localPreviewLinksVerified": link_count}))


if __name__ == "__main__":
    main()
