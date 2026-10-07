#!/usr/bin/env python3
"""Pack the owner's transparent PNG animation into palette-based UI assets.

No image is generated or redrawn. Resizing, a shared palette and alpha coverage
adapt the supplied 240 PNG frames to the software renderer's PCX format.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import zipfile

import numpy as np
from PIL import Image


FRAME_COUNT = 240
FPS = 30
TRANSPARENT = 250
LAYOUTS = {
    "d3d-menu": (580, 154),
    "d3d-title": (620, 216),
    "d3d-pause": (360, 90),
}
BAYER = np.array(
    [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]],
    dtype=np.uint16,
)


def read_frame(archive: zipfile.ZipFile, name: str) -> Image.Image:
    with archive.open(name) as stream:
        with Image.open(stream) as image:
            if image.size != (640, 160) or image.mode != "RGBA":
                raise ValueError(f"Expected 640x160 RGBA: {name}")
            return image.copy()


def shared_palette(archive: zipfile.ZipFile, names: list[str]) -> tuple[Image.Image, list[int]]:
    # Sample throughout the full cycle, so palette colors cannot flicker by frame.
    samples = []
    for name in names[::8]:
        rgba = np.asarray(read_frame(archive, name).resize((160, 40), Image.Resampling.LANCZOS))
        samples.append(rgba[rgba[:, :, 3] >= 128, :3])
    pixels = np.concatenate(samples)
    sample = Image.fromarray(pixels.reshape((-1, 1, 3)))
    quantized = sample.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    raw = quantized.getpalette()[:768]
    raw += [0] * (768 - len(raw))
    raw[255 * 3:256 * 3] = raw[:3]
    quantized.putpalette(raw)
    final = raw[:]
    # Index 250 is transparent; relocate its actual color to the unused index 255.
    final[255 * 3:256 * 3] = raw[250 * 3:251 * 3]
    final[250 * 3:251 * 3] = [0, 0, 0]
    return quantized, final


def convert_frame(source: Image.Image, width: int, height: int,
                  quantizer: Image.Image, palette: list[int]) -> Image.Image:
    scaled_height = round(source.height * width / source.width)
    if scaled_height > height:
        raise ValueError("Logo does not fit the destination frame")
    resized = source.resize((width, scaled_height), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (width, height))
    canvas.paste(resized, (0, height - scaled_height))
    rgba = np.asarray(canvas)
    indexed = np.asarray(canvas.convert("RGB").quantize(
        palette=quantizer, dither=Image.Dither.NONE)).copy()
    indexed[indexed == 255] = 0
    indexed[indexed == TRANSPARENT] = 255
    # CLX has binary coverage. A fixed ordered pattern retains partial flame
    # coverage without making opaque dark letter faces transparent.
    thresholds = (np.tile(BAYER, ((height + 3) // 4, (width + 3) // 4))[:height, :width] * 16 + 8)
    covered = rgba[:, :, 3].astype(np.uint16) > thresholds
    indexed[~covered] = TRANSPARENT
    opaque_dark = (rgba[:, :, 3] == 255) & (rgba[:, :, :3].max(axis=2) < 25)
    if np.any(indexed[opaque_dark] == TRANSPARENT):
        raise ValueError("An opaque dark letter pixel became transparent")
    result = Image.frombytes("P", (width, height), indexed.tobytes())
    result.putpalette(palette)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="diablo3d_frames_leve_640x160.zip")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    names = [f"frames_png_640x160/diablo3d_frame_{index:04d}.png" for index in range(FRAME_COUNT)]
    with zipfile.ZipFile(args.archive) as archive:
        manifest = json.loads(archive.read("manifest_640x160.json"))
        if (manifest.get("frames"), manifest.get("fps"), manifest.get("width"), manifest.get("height")) != (240, 30, 640, 160):
            raise ValueError("Expected the supplied 240-frame, 30-fps 640x160 sequence")
        pngs = [name for name in archive.namelist() if name.lower().endswith(".png")]
        if set(pngs) != set(names) or len(pngs) != len(names):
            raise ValueError("Frame sequence has missing, duplicate or unexpected files")
        if sum(item.file_size for item in archive.infolist()) > 512 * 1024 * 1024:
            raise ValueError("Unexpected archive size")
        quantizer, palette = shared_palette(archive, names)
        sheets = {name: Image.new("P", (width, height * FRAME_COUNT), TRANSPARENT)
                  for name, (width, height) in LAYOUTS.items()}
        for sheet in sheets.values():
            sheet.putpalette(palette)
        frame_hashes = []
        for index, name in enumerate(names):
            source = read_frame(archive, name)
            frame_hashes.append(hashlib.sha256(archive.read(name)).hexdigest())
            for asset, (width, height) in LAYOUTS.items():
                frame = convert_frame(source, width, height, quantizer, palette)
                sheets[asset].paste(frame, (0, index * height))
            if index % 60 == 0:
                print(f"Packed {index + 1}/{FRAME_COUNT} source frames", flush=True)
    ui_art = args.output / "ui_art"
    ui_art.mkdir(exist_ok=True)
    outputs = []
    for name, sheet in sheets.items():
        path = ui_art / f"{name}.pcx"
        buffer = io.BytesIO()
        sheet.save(buffer, format="PCX")
        content = buffer.getvalue()
        with Image.open(io.BytesIO(content)) as check:
            check.load()
            if check.size != sheet.size or check.tobytes() != sheet.tobytes():
                raise ValueError(f"PCX round trip failed for {name}")
            if check.getpalette()[:768] != palette:
                raise ValueError(f"Palette round trip failed for {name}")
        temporary = path.with_suffix(".pcx.tmp")
        temporary.write_bytes(content)
        temporary.replace(path)
        width, height = LAYOUTS[name]
        outputs.append({"file": f"ui_art/{name}.pcx", "width": width,
                        "frameHeight": height, "frames": FRAME_COUNT,
                        "sha256": hashlib.sha256(content).hexdigest(), "bytes": len(content)})
    report = {"sourceArchiveSha256": hashlib.sha256(args.archive.read_bytes()).hexdigest(),
              "source": "Owner-provided ChatGPT animation; transparent PNG sequence",
              "sourceFrames": FRAME_COUNT, "fps": FPS, "cycleDurationMs": 8000,
              "transparentIndex": TRANSPARENT, "sourceFrameSha256": frame_hashes,
              "outputs": outputs, "pcxRoundTrip": "exact indexed pixels and palette",
              "adaptation": "Proportional resize, common 255-color palette, ordered alpha coverage"}
    (args.output / "animation-import.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"frames": FRAME_COUNT, "cycleDurationMs": 8000, "outputs": outputs}, indent=2))


if __name__ == "__main__":
    main()
