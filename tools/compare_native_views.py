#!/usr/bin/env python3
"""Measure RGB differences between matched native/calibrated diagnostic views.

No alignment, resizing, palette-index comparison, or black-as-alpha inference.
Pixels are compared in RGB only where both images have nonzero actual alpha.
Opaque black pixels remain part of the comparison. Metrics never approve art.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont


class ComparisonError(Exception):
    pass


def find_pairs(directory: Path) -> list[tuple[str, Path, Path]]:
    sides = {"native": {}, "calibrated": {}}
    for path in sorted(directory.iterdir()):
        if not path.is_file() or path.suffix.lower() not in {".png", ".bmp", ".jpg", ".jpeg", ".webp", ".tif", ".tiff"}:
            continue
        match = re.fullmatch(r"(native|calibrated)[-_](.+)", path.stem, flags=re.IGNORECASE)
        if not match:
            continue
        side, name = match.group(1).lower(), match.group(2).lower()
        if name in sides[side]:
            raise ComparisonError(f"More than one {side} image matches {name}")
        sides[side][name] = path
    missing_native = sorted(sides["calibrated"].keys() - sides["native"].keys())
    missing_calibrated = sorted(sides["native"].keys() - sides["calibrated"].keys())
    if missing_native or missing_calibrated:
        raise ComparisonError("Unpaired views: " + "; ".join(
            part for part in ("missing native " + ", ".join(missing_native) if missing_native else "",
                              "missing calibrated " + ", ".join(missing_calibrated) if missing_calibrated else "") if part))
    names = sorted(sides["native"].keys() & sides["calibrated"].keys())
    if not names:
        raise ComparisonError("No native-* / calibrated-* image pairs were found")
    return [(name, sides["native"][name], sides["calibrated"][name]) for name in names]


def title_font():
    for path in ("C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"):
        if Path(path).is_file():
            return ImageFont.truetype(path, 24)
    return ImageFont.load_default(size=24)


def save_contact(source: Image.Image, calibrated: Image.Image, path: Path) -> None:
    width, height = source.size
    header, gap = 46, 12
    canvas = Image.new("RGB", (2 * width + gap, height + header), (12, 12, 12))
    draw = ImageDraw.Draw(canvas)
    font = title_font()
    draw.text((12, 9), "Fonte", font=font, fill=(245, 245, 245))
    draw.text((width + gap + 12, 9), "3D", font=font, fill=(245, 245, 245))
    canvas.paste(source.convert("RGB"), (0, header), source.getchannel("A"))
    canvas.paste(calibrated.convert("RGB"), (width + gap, header), calibrated.getchannel("A"))
    canvas.save(path)


def fraction(count: int, total: int) -> float | None:
    return 100 * count / total if total else None


def compare(name: str, native_path: Path, calibrated_path: Path, out_dir: Path,
            roi: tuple[int, int, int, int] | None = None, difference: bool = False) -> dict:
    with Image.open(native_path) as original:
        native_mode = original.mode
        native = original.convert("RGBA")
    with Image.open(calibrated_path) as original:
        calibrated_mode = original.mode
        calibrated = original.convert("RGBA")
    if native.size != calibrated.size:
        raise ComparisonError(f"{name}: image sizes differ ({native.size} vs {calibrated.size}); no rescaling is performed")
    width, height = native.size
    rectangle = roi or (0, 0, width, height)
    left, top, right, bottom = rectangle
    if not (0 <= left < right <= width and 0 <= top < bottom <= height):
        raise ComparisonError(f"{name}: ROI is outside the image or empty")
    source = np.asarray(native.crop(rectangle), dtype=np.uint8)
    candidate = np.asarray(calibrated.crop(rectangle), dtype=np.uint8)
    native_visible = source[:, :, 3] > 0
    calibrated_visible = candidate[:, :, 3] > 0
    joint = native_visible & calibrated_visible
    total = int(joint.size)
    comparable = int(joint.sum())
    delta = np.abs(source[:, :, :3].astype(np.int16) - candidate[:, :, :3].astype(np.int16))
    identical = np.all(delta == 0, axis=2) & joint
    same = int(identical.sum())
    source_only = int((native_visible & ~calibrated_visible).sum())
    calibrated_only = int((calibrated_visible & ~native_visible).sum())
    pixel_differences = delta[joint]
    exact_rgb = {
        "comparedVisiblePixels": comparable, "samePixels": same, "differentPixels": comparable - same,
        "samePixelPercent": fraction(same, comparable), "differentPixelPercent": fraction(comparable - same, comparable),
        "meanAbsoluteChannelError": float(pixel_differences.mean()) if comparable else None,
        "meanAbsoluteChannelErrorPercentOf255": float(pixel_differences.mean()) / 255 * 100 if comparable else None,
        "rmseChannel": float(np.sqrt(np.mean(pixel_differences.astype(np.float64) ** 2))) if comparable else None,
        "meanAbsoluteErrorRGB": pixel_differences.mean(axis=0).tolist() if comparable else None,
        "maximumChannelError": int(pixel_differences.max()) if comparable else None,
        "medianMaximumPixelChannelError": float(np.median(pixel_differences.max(axis=1))) if comparable else None,
        "p95MaximumPixelChannelError": float(np.percentile(pixel_differences.max(axis=1), 95)) if comparable else None,
    }
    exact_visible_image = comparable > 0 and same == comparable and bool(np.array_equal(source[:, :, 3], candidate[:, :, 3]))
    safe_name = re.sub(r"[^A-Za-z0-9_.-]", "_", name)
    contact = out_dir / ("comparison-" + safe_name + ".png")
    save_contact(native.crop(rectangle), calibrated.crop(rectangle), contact)
    report = {
        "name": name, "native": str(native_path), "calibrated": str(calibrated_path),
        "nativeSha256": hashlib.sha256(native_path.read_bytes()).hexdigest(),
        "calibratedSha256": hashlib.sha256(calibrated_path.read_bytes()).hexdigest(),
        "inputModes": {"native": native_mode, "calibrated": calibrated_mode},
        "imageSize": [width, height], "roiExclusive": list(rectangle), "roiPixels": total,
        "rgb": exact_rgb,
        "samePerspectivePixelsExact": exact_visible_image,
        "actualAlphaCoverage": {
            "nativeVisiblePixels": int(native_visible.sum()), "calibratedVisiblePixels": int(calibrated_visible.sum()),
            "jointVisiblePercentOfRoi": fraction(comparable, total),
            "nativeOnlyPixels": source_only, "nativeOnlyPercentOfRoi": fraction(source_only, total),
            "calibratedOnlyPixels": calibrated_only, "calibratedOnlyPercentOfRoi": fraction(calibrated_only, total),
            "coverageSamePercentOfRoi": fraction(total - source_only - calibrated_only, total),
            "bothOpaqueEverywhere": bool(np.all(source[:, :, 3] == 255) and np.all(candidate[:, :, 3] == 255)),
        },
        "contactSheet": str(contact),
    }
    if difference:
        image = np.zeros(source.shape[:2] + (3,), dtype=np.uint8)
        maximum = delta.max(axis=2).astype(np.uint8)
        image[:, :, 0][joint] = maximum[joint]
        image[:, :, 2][native_visible ^ calibrated_visible] = 255
        difference_path = out_dir / ("difference-" + safe_name + ".png")
        Image.fromarray(image).save(difference_path)
        report["differenceImage"] = str(difference_path)
        report["differenceLegend"] = "Red intensity = max RGB channel error on joint alpha coverage; blue = actual alpha-coverage mismatch"
    return report


def write_report(out_dir: Path, pairs: list[dict]) -> dict:
    visible = sum(pair["rgb"]["comparedVisiblePixels"] for pair in pairs)
    identical = sum(pair["rgb"]["samePixels"] for pair in pairs)
    exact_pairs = sum(pair["samePerspectivePixelsExact"] for pair in pairs)
    report = {
        "schemaVersion": 1,
        "method": "Same-coordinate RGB after palette conversion; nonzero alpha intersection; opaque black retained; no alignment or resizing",
        "interpretation": [
            "These numbers measure the selected captures/ROI and do not approve geometric or artistic fidelity.",
            "Pixel/decode round-trip tests validate storage and rendering mechanics, not an exact reconstruction of the original artwork.",
            "Alpha coverage comes only from actual image alpha. Opaque captures provide no object-silhouette measurement.",
            "A global match can be dominated by matching ground/background. Choose the same explicit ROI to inspect a building.",
            "The aggregate is weighted by compared pixels across captures; overlapping captures repeat their content and are not unique town coverage.",
        ],
        "aggregate": {"comparedVisiblePixels": visible, "samePixels": identical,
                      "samePixelPercent": fraction(identical, visible),
                      "exactComparedViews": exact_pairs, "totalComparedViews": len(pairs),
                      "allComparedViewsExact": bool(pairs) and exact_pairs == len(pairs)},
        "pairs": pairs,
    }
    (out_dir / "comparison.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = ["Comparação de Fonte e 3D", ""]
    lines += [f"Enquadramentos com pixels exatamente iguais: {exact_pairs}/{len(pairs)}.",
              "A exigência de coincidência completa não é satisfeita por uma média alta.", ""]
    for pair in pairs:
        rgb = pair["rgb"]
        percent = rgb["samePixelPercent"]
        error = rgb["meanAbsoluteChannelError"]
        value = "sem cobertura alfa comum" if percent is None else f"{percent:.4f}% de pixels RGB iguais"
        lines.append(f"{pair['name']}: {value}; {rgb['comparedVisiblePixels']} pixels comparados.")
        if error is not None:
            lines.append(f"Erro absoluto médio por canal: {error:.4f} / 255 ({rgb['meanAbsoluteChannelErrorPercentOf255']:.4f}%).")
        coverage = pair["actualAlphaCoverage"]
        lines.append(f"Cobertura alfa somente na fonte: {coverage['nativeOnlyPixels']}; somente no 3D: {coverage['calibratedOnlyPixels']}.")
        lines.append(f"Imagem: {pair['contactSheet']}")
        lines.append("")
    lines += ["As medidas não definem aprovação de fidelidade. Preto opaco é comparado como qualquer outra cor.",
              "Capturas opacas não fornecem uma máscara de silhueta do prédio. Testes mecânicos e fidelidade artística são avaliações separadas."]
    (out_dir / "comparison.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return {"pairs": len(pairs), "comparedVisiblePixels": visible,
            "samePixelPercent": fraction(identical, visible), "exactComparedViews": exact_pairs,
            "allComparedViewsExact": bool(pairs) and exact_pairs == len(pairs), "report": str(out_dir / "comparison.json")}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path, help="Directory containing native-* and calibrated-* pairs")
    parser.add_argument("--native", type=Path, help="Explicit source image instead of automatic pairs")
    parser.add_argument("--calibrated", type=Path, help="Explicit 3D image instead of automatic pairs")
    parser.add_argument("--outDir", "--out-dir", required=True, type=Path)
    parser.add_argument("--roi", "--bbox", nargs=4, type=int, metavar=("LEFT", "TOP", "RIGHT", "BOTTOM"),
                        help="Compare the same explicit pixel rectangle in both images; right/bottom exclusive")
    parser.add_argument("--difference", action="store_true", help="Also save an RGB-error image, separate from the Fonte/3D contact sheet")
    args = parser.parse_args()
    if bool(args.native) != bool(args.calibrated) or (args.directory and args.native) or not (args.directory or args.native):
        parser.error("Provide a directory, or both --native and --calibrated")
    try:
        out_dir = args.outDir.resolve()
        out_dir.mkdir(parents=True, exist_ok=True)
        pairs = find_pairs(args.directory.resolve()) if args.directory else [
            (args.native.stem.removeprefix("native-"), args.native.resolve(), args.calibrated.resolve())]
        report = [compare(name, native, calibrated, out_dir, tuple(args.roi) if args.roi else None, args.difference)
                  for name, native, calibrated in pairs]
        print(json.dumps(write_report(out_dir, report), ensure_ascii=False))
        return 0
    except (ComparisonError, OSError, ValueError) as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=False), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
