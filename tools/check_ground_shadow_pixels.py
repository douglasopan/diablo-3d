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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path, help="Smoke output directory")
    parser.add_argument("--header", type=Path, default=Path(__file__).resolve().parents[1] / "Source/engine/render/town_ground_shadow.hpp")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    directory = args.captures / "ground-shadow-sources"
    source = json.loads((directory / "ground-pieces.json").read_text(encoding="utf-8"))
    pieces = {entry["piece"]: entry for entry in source["pieces"]}
    matches = re.findall(r"CabinGroundShadow(\d+)\s*\{\s*(\d+)\s*,\s*\{(.*?)\}\s*\}", args.header.read_text(encoding="utf-8"), re.S)
    masks = {int(piece): (int(donor), [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)ULL", rows)])
             for piece, donor, rows in matches}
    if set(masks) != {873, 874, 882, 884} or any(len(rows) != 32 for _, rows in masks.values()):
        raise ValueError("Expected precisely the four audited 32-row cabin masks")
    controls = (24, 25, 26, 27, 48, 49, 50, 51, 875, 876, 881, 883)
    palette = source["palette"]
    results = []
    for piece in (*sorted(masks), *controls):
        original = pieces[piece]
        image = Image.open(directory / f"piece-{piece}-cached-floor-indexed.png")
        if image.size != (64, 32):
            raise ValueError(f"Piece {piece}: diagnostic dimensions must be 64x32")
        indexed = image.mode == "P"
        actual = list(image.tobytes()) if indexed else list(image.convert("RGB").get_flattened_data())
        donor_id, rows = masks.get(piece, (piece, [0] * 32))
        donor = pieces[donor_id]
        outside_mismatches = inside_mismatches = outside_covered = inside_covered = selected_outside_coverage = 0
        preserved_transparent = transparent_mismatches = 0
        for offset, index in enumerate(original["indices"]):
            y, x = divmod(offset, 64)
            selected = bool(rows[y] & (1 << x))
            selected_outside_coverage += selected and not original["opacity"][offset]
            expected_index = donor["indices"][offset] if selected else index
            expected = expected_index if indexed else tuple(palette[expected_index])
            if selected:
                inside_covered += 1
                inside_mismatches += actual[offset] != expected
            else:
                outside_covered += bool(original["opacity"][offset])
                preserved_transparent += not original["opacity"][offset]
                transparent_mismatches += not original["opacity"][offset] and actual[offset] != expected
                outside_mismatches += actual[offset] != expected
        valid = (outside_mismatches == 0 and inside_mismatches == 0 and selected_outside_coverage == 0
                 and original["opacity"] == donor["opacity"])
        results.append({"piece": piece, "donor": donor_id, "readbackMode": image.mode,
                        "maskedOpaquePixels": inside_covered, "preservedOriginalOpaquePixels": outside_covered,
                        "preservedTransparentPixelValues": preserved_transparent, "transparentPixelMismatches": transparent_mismatches,
                        "outsideMaskMismatches": outside_mismatches, "insideMaskDonorMismatches": inside_mismatches,
                        "selectedOutsideOriginalCoverage": selected_outside_coverage, "pass": valid})
    report = {"source": str(args.captures.resolve()), "archiveMode": source["archiveMode"],
              "method": "actual effective ground PNG versus independent native primitive export and frozen header mattes",
              "runtimeOpacityReadbackAvailable": False,
              "opacityEvidence": "source/donor coverage identical; runtime copy preserves opacity by contract, not inferred from black PNG pixels",
              "results": results, "pass": all(result["pass"] for result in results)}
    text = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.write_text(text, encoding="utf-8")
    print(text, end="")
    if not report["pass"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
