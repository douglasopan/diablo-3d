#!/usr/bin/env python3
"""Audit actual smoke ground readback against original archive pixels and mattes.

Requires Pillow. The original JSON comes from native MIN/CEL primitive rendering;
cached PNGs must come from the runtime's effective SceneGround diagnostic.
This checks source pixel preservation, not artistic approval or sprite opacity.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

from PIL import Image


def read_masks(header: Path) -> dict[int, tuple[int, list[int]]]:
    """Read the frozen data and verify that its public getter exposes each mask."""
    text = header.read_text(encoding="utf-8")
    matches = re.findall(r"CabinGroundShadow(\d+)\s*\{\s*(\d+)\s*,\s*\{(.*?)\}\s*\}", text, re.S)
    masks: dict[int, tuple[int, list[int]]] = {}
    for piece_text, donor_text, rows_text in matches:
        piece = int(piece_text)
        rows = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)ULL", rows_text)]
        if piece in masks or len(rows) != 32 or not any(rows) or any(row >= 1 << 64 for row in rows):
            raise ValueError(f"Piece {piece}: expected one nonempty 32-row uint64 mask")
        masks[piece] = int(donor_text), rows
    aliases = re.findall(r"CabinGroundShadow(\d+)\s*\{\s*(\d+)\s*,\s*CabinGroundShadow(\d+)\.rows\s*\}", text)
    for piece_text, donor_text, reference_text in aliases:
        piece, reference = int(piece_text), int(reference_text)
        if piece in masks or reference not in masks:
            raise ValueError(f"Piece {piece}: duplicate or unresolved row-mask alias")
        masks[piece] = int(donor_text), list(masks[reference][1])
    exposed = re.findall(r"case\s+(\d+)\s*:\s*return\s+&CabinGroundShadow(\d+)\s*;", text)
    if not masks or len(exposed) != len(masks) or {int(piece) for piece, _ in exposed} != set(masks):
        raise ValueError("Every frozen mask must appear exactly once in the runtime getter")
    if any(int(piece) != int(reference) for piece, reference in exposed):
        raise ValueError("Runtime getter must return the corresponding native piece mask")
    if any(piece == donor or donor in masks for piece, (donor, _) in masks.items()):
        raise ValueError("Donor must be a separate, unmodified native piece")
    return masks


def load_sources(directory: Path) -> tuple[dict, dict[int, dict]]:
    source = json.loads((directory / "ground-pieces.json").read_text(encoding="utf-8"))
    entries = source["pieces"]
    pieces = {entry["piece"]: entry for entry in entries}
    if len(pieces) != len(entries) or source["floorSize"] != [64, 32] or len(source["palette"]) != 256:
        raise ValueError("Native export requires unique IDs, 64x32 floors and the full 256-color palette")
    for piece, entry in pieces.items():
        if len(entry["indices"]) != 2048 or len(entry["opacity"]) != 2048:
            raise ValueError(f"Piece {piece}: native floor requires 2048 colors and coverage values")
        if any(not isinstance(index, int) or not 0 <= index < 256 for index in entry["indices"]):
            raise ValueError(f"Piece {piece}: invalid native palette index")
        if any(value not in (0, 1) for value in entry["opacity"]):
            raise ValueError(f"Piece {piece}: coverage must be independent binary opacity")
    return source, pieces


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path, help="Smoke output directory")
    parser.add_argument("--header", type=Path, default=Path(__file__).resolve().parents[1] / "Source/engine/render/town_ground_shadow.hpp")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--compare-source", type=Path, help="Second smoke archive export; require identical native palette, colors and coverage")
    parser.add_argument("--source-only", action="store_true", help="Check frozen masks against independent native coverage without claiming a runtime readback")
    parser.add_argument("--require-effective-opacity", action="store_true", help="Require actual effective RGBA textures and verify independent native coverage")
    args = parser.parse_args()
    if args.source_only and args.require_effective_opacity:
        parser.error("--require-effective-opacity requires runtime readback")
    directory = args.captures / "ground-shadow-sources"
    source, pieces = load_sources(directory)
    masks = read_masks(args.header)
    required = set(masks) | {donor for donor, _ in masks.values()}
    if required - set(pieces):
        raise ValueError(f"Missing native primitive exports: {sorted(required - set(pieces))}")
    controls = sorted(set(pieces) - set(masks))
    palette = source["palette"]
    results = []
    for piece in (*sorted(masks), *controls):
        original = pieces[piece]
        image = None if args.source_only else Image.open(directory / f"piece-{piece}-cached-floor-indexed.png")
        if image is not None and image.size != (64, 32):
            raise ValueError(f"Piece {piece}: diagnostic dimensions must be 64x32")
        indexed = image is None or image.mode == "P"
        actual = None if image is None else list(image.tobytes()) if indexed else list(image.convert("RGB").getdata())
        donor_id, rows = masks.get(piece, (piece, [0] * 32))
        donor = pieces[donor_id]
        outside_mismatches = inside_mismatches = outside_covered = inside_covered = selected_outside_coverage = 0
        preserved_transparent = transparent_mismatches = 0
        preserved_opaque_black = opaque_black_mismatches = 0
        selected_opaque_black = 0
        rgba_path = directory / f"piece-{piece}-cached-floor-rgba.png"
        rgba = None
        if not args.source_only and rgba_path.is_file():
            effective = Image.open(rgba_path)
            if effective.mode != "RGBA" or effective.size != (64, 32):
                raise ValueError(f"Piece {piece}: effective runtime readback must be native 64x32 RGBA")
            rgba = list(effective.getdata())
        elif args.require_effective_opacity:
            raise ValueError(f"Piece {piece}: missing effective runtime RGBA opacity readback")
        runtime_opacity_mismatches = runtime_rgba_color_mismatches = 0
        for offset, index in enumerate(original["indices"]):
            y, x = divmod(offset, 64)
            selected = bool(rows[y] & (1 << x))
            selected_outside_coverage += selected and not original["opacity"][offset]
            expected_index = donor["indices"][offset] if selected else index
            expected = expected_index if indexed else tuple(palette[expected_index])
            if rgba is not None:
                runtime_opacity_mismatches += rgba[offset][3] != (255 if original["opacity"][offset] else 0)
                runtime_rgba_color_mismatches += rgba[offset][:3] != tuple(palette[expected_index])
            if selected:
                inside_covered += 1
                selected_opaque_black += bool(original["opacity"][offset]) and index == 0
                inside_mismatches += actual is not None and actual[offset] != expected
            else:
                outside_covered += bool(original["opacity"][offset])
                preserved_transparent += not original["opacity"][offset]
                transparent_mismatches += actual is not None and not original["opacity"][offset] and actual[offset] != expected
                outside_mismatches += actual is not None and actual[offset] != expected
                opaque_black = bool(original["opacity"][offset]) and index == 0
                preserved_opaque_black += opaque_black
                opaque_black_mismatches += actual is not None and opaque_black and actual[offset] != expected
        valid = (outside_mismatches == 0 and inside_mismatches == 0 and selected_outside_coverage == 0
                 and runtime_opacity_mismatches == 0 and runtime_rgba_color_mismatches == 0
                 and original["opacity"] == donor["opacity"] and (piece not in masks or original["sol"] == 0))
        results.append({"piece": piece, "donor": donor_id, "readbackMode": None if image is None else image.mode,
                        "maskedOpaquePixels": inside_covered, "preservedOriginalOpaquePixels": outside_covered,
                        "preservedTransparentPixelValues": preserved_transparent, "transparentPixelMismatches": transparent_mismatches,
                        "outsideMaskMismatches": outside_mismatches, "insideMaskDonorMismatches": inside_mismatches,
                        "preservedOriginalOpaqueBlackPixels": preserved_opaque_black, "opaqueBlackMismatches": opaque_black_mismatches,
                        "selectedShadowOpaqueBlackPixels": selected_opaque_black, "nativeSol": original["sol"],
                        "runtimeOpacityReadbackAvailable": rgba is not None,
                        "runtimeOpacityMismatches": runtime_opacity_mismatches,
                        "runtimeRgbaColorMismatches": runtime_rgba_color_mismatches,
                        "selectedOutsideOriginalCoverage": selected_outside_coverage, "pass": valid})
    source_comparison = None
    if args.compare_source:
        other_source, other_pieces = load_sources(args.compare_source / "ground-shadow-sources")
        checked = sorted(pieces)
        missing = sorted(set(checked) - set(other_pieces))
        mismatches = [piece for piece in checked if piece in other_pieces and any(
            pieces[piece][field] != other_pieces[piece][field] for field in ("indices", "opacity", "sol", "microframes"))]
        source_comparison = {"directory": str(args.compare_source.resolve()), "archiveMode": other_source["archiveMode"],
                             "checkedPieces": checked, "missingPieces": missing, "differentNativePieces": mismatches,
                             "paletteIdentical": source["palette"] == other_source["palette"],
                             "pass": not missing and not mismatches and source["palette"] == other_source["palette"]}
    report = {"source": str(args.captures.resolve()), "archiveMode": source["archiveMode"],
              "method": "frozen masks versus independent native primitive coverage" if args.source_only else "actual effective ground PNG versus independent native primitive export and frozen header mattes",
              "runtimeReadbackChecked": not args.source_only, "maskCount": len(masks), "controlCount": len(controls),
              "runtimeOpacityReadbackAvailable": all(result["runtimeOpacityReadbackAvailable"] for result in results),
              "opacityEvidence": "actual effective RGBA alpha versus independently rendered original primitive coverage"
                  if all(result["runtimeOpacityReadbackAvailable"] for result in results)
                  else "source/donor coverage identical; complete runtime opacity readback unavailable; not inferred from black PNG pixels",
              "sourceComparison": source_comparison,
              "results": results, "pass": all(result["pass"] for result in results) and (source_comparison is None or source_comparison["pass"])}
    text = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.write_text(text, encoding="utf-8")
    print(text, end="")
    if not report["pass"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
