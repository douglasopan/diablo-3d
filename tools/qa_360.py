"""Assemble smoke turntables for visual review without changing game artwork.

Usage: python tools/qa_360.py diagnostics/v3-gog/turntables [output-directory]
The manifest distinguishes actual native references from forced mesh rendering.
Mechanical coverage evidence is reported separately from artistic quality.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw


def panel(path: Path, label: str, size: tuple[int, int]) -> Image.Image:
    image = Image.open(path).convert("RGB")
    if image.size != size:
        raise ValueError(f"{path.name}: expected {size}, got {image.size}")
    result = Image.new("RGB", (size[0], size[1] + 30), (24, 24, 24))
    result.paste(image, (0, 30))
    ImageDraw.Draw(result).text((10, 9), label, fill=(235, 235, 235))
    return result


def sheet(panels: list[Image.Image], columns: int) -> Image.Image:
    width, height = panels[0].size
    rows = (len(panels) + columns - 1) // columns
    result = Image.new("RGB", (columns * width, rows * height), (24, 24, 24))
    for index, image in enumerate(panels):
        result.paste(image, ((index % columns) * width, (index // columns) * height))
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    args = parser.parse_args()
    source = args.captures.resolve()
    destination = (args.output or source / "review").resolve()
    destination.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((source / "turntables.json").read_text(encoding="utf-8"))
    size = manifest["width"], manifest["height"]
    report = {
        "source": str(source),
        "nativeMethod": manifest["native"],
        "rawMethod": manifest["raw"],
        "coverageMethod": manifest["coverage"],
        "artisticApproval": "requires visual review; coverage and closed meshes do not establish fidelity",
        "objects": [],
    }
    for name in dict.fromkeys(frame["object"] for frame in manifest["frames"]):
        frames = {frame["orbitDegrees"]: frame for frame in manifest["frames"] if frame["object"] == name}
        octants = []
        for degree in range(0, 360, 45):
            frame = frames[degree]
            label = f"{name}: raw mesh orbit +{degree} deg"
            octants.append(panel(source / frame["file"], label, size))
        sheet(octants, 4).save(destination / f"{name}-360.png")
        close_views = [panel(source / f"{name}-native.png", f"{name}: actual native backend", size)]
        for degree in (-5, 0, 5):
            close_views.append(panel(source / frames[degree]["file"], f"{name}: raw mesh orbit {degree:+d} deg", size))
        sheet(close_views, 2).save(destination / f"{name}-near-native.png")
        architecture_samples = sum(frame["architectureSamples"] for frame in frames.values())
        holes = sum(frame["architectureHoles"] for frame in frames.values())
        report["objects"].append({
            "object": name,
            "frames": len(frames),
            "dimensions": size,
            "architectureSamples": architecture_samples,
            "architectureCovered": sum(frame["architectureCovered"] for frame in frames.values()),
            "architectureOccluded": sum(frame["architectureOccluded"] for frame in frames.values()),
            "architectureHoles": holes,
            "architectureCoverageApplicable": architecture_samples > 0,
            "minimumSelectablePlayerPixels": min(frame["playerPixels"] for frame in frames.values()),
            "turntable": f"{name}-360.png",
            "nearNativeComparison": f"{name}-near-native.png",
        })
    (destination / "review.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(destination), "objects": len(report["objects"])}))


if __name__ == "__main__":
    main()
