"""Compare actual raw cabin captures at fixed pixels; never align or redraw art.

Usage: python tools/qa_cabin_comparison.py BEFORE/turntables AFTER/turntables OUTPUT
Native references are controls. Home/native fallback is never a mesh result.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw


ANGLES = (-5, 0, 5, 90, 180, 270)


def load(directory: Path) -> tuple[dict, dict]:
    manifest = json.loads((directory / "turntables.json").read_text(encoding="utf-8"))
    if "forced geometry" not in manifest["raw"]:
        raise ValueError(f"{directory}: missing explicit forced-geometry provenance")
    frames = {(frame["object"], frame["orbitDegrees"]): frame for frame in manifest["frames"]}
    if len(frames) != len(manifest["frames"]):
        raise ValueError(f"{directory}: duplicate object/angle captures")
    return manifest, frames


def image(path: Path, size: tuple[int, int]) -> Image.Image:
    result = Image.open(path).convert("RGB")
    if result.size != size:
        raise ValueError(f"{path}: expected fixed dimensions {size}, found {result.size}")
    return result


def panel(source: Image.Image | None, label: str, size: tuple[int, int]) -> Image.Image:
    result = Image.new("RGB", (size[0], size[1] + 30), (24, 24, 24))
    if source is not None:
        result.paste(source, (0, 30))
    else:
        ImageDraw.Draw(result).text((16, size[1] // 2), "No recorded baseline: comparison unavailable", fill=(245, 170, 100))
    ImageDraw.Draw(result).text((10, 9), label, fill=(235, 235, 235))
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    before, before_frames = load(args.before)
    after, after_frames = load(args.after)
    size = after["width"], after["height"]
    if size != (before["width"], before["height"]):
        raise ValueError("Before/after viewport dimensions differ; export matching captures")
    if before.get("archiveMode") and after.get("archiveMode") and before["archiveMode"] != after["archiveMode"]:
        raise ValueError("Before/after game archives differ")
    args.output.mkdir(parents=True, exist_ok=True)
    report = {"before": str(args.before.resolve()), "after": str(args.after.resolve()),
              "method": "original pixels, fixed camera/actor coordinates; no alignment or scaling",
              "approval": "visual review required; pixel differences and closed surfaces do not approve artwork",
              "objects": []}
    for name in ("cabin-east", "cabin-west"):
        if (name, 0) not in after_frames:
            raise ValueError(f"Missing after capture {name} at original orientation")
        rows = []
        missing = []
        native_before_path = args.before / f"{name}-native.png"
        native_before = image(native_before_path, size) if native_before_path.is_file() else None
        native_after = image(args.after / f"{name}-native.png", size)
        rows.append((native_before, native_after, "actual original backend (control only)"))
        for degree in ANGLES:
            final = after_frames[name, degree]
            original = before_frames.get((name, degree))
            if original is None:
                missing.append(degree)
            else:
                for field in ("focus", "heroTile"):
                    if original[field] != final[field]:
                        raise ValueError(f"{name} {degree}: {field} differs; comparison would move scenery or actor")
                if abs(original["cameraYaw"] - final["cameraYaw"]) > 0.00002:
                    raise ValueError(f"{name} {degree}: camera yaw differs")
                for field in ("cameraPitch", "cameraDistance", "cameraPan"):
                    if field in original and original[field] != final.get(field):
                        raise ValueError(f"{name} {degree}: {field} differs")
            rows.append((image(args.before / original["file"], size) if original else None,
                         image(args.after / final["file"], size), f"raw mesh orbit {degree:+d} degrees"))
        sheet = Image.new("RGB", (size[0] * 2, (size[1] + 30) * len(rows)), (24, 24, 24))
        for row, (old, new, label) in enumerate(rows):
            for column, (source, version) in enumerate(((old, "BEFORE"), (new, "AFTER"))):
                sheet.paste(panel(source, f"{version}: {name}, {label}", size), (column * size[0], row * (size[1] + 30)))
        filename = f"{name}-before-after.png"
        sheet.save(args.output / filename)
        native_changed = None
        if native_before is not None:
            difference = ImageChops.difference(native_before, native_after).tobytes()
            native_changed = sum(any(pixel) for pixel in zip(difference[0::3], difference[1::3], difference[2::3]))
        report["objects"].append({"object": name, "sheet": filename, "rawAngles": list(ANGLES),
                                  "missingBeforeAngles": missing, "nativeControlChangedPixels": native_changed})
    (args.output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output.resolve()), "objects": len(report["objects"])}))


if __name__ == "__main__":
    main()
