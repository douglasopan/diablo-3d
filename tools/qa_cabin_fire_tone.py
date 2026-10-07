#!/usr/bin/env python3
"""Audit real cabin-window fire color without changing exterior lighting.

Requires NumPy and Pillow. Inputs are actual forced-geometry smoke frames and
the same-camera lamp on/off fixture. The native aperture polygon is visually
audited for the 640x640 Tristram fixture; it is not an image registration.
Generated game-art crops/masks belong only in local, ignored diagnostics.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from qa_cabin_lighting import ROIS, assert_same_pose, linear, load_frame, roi_mask, statistics


NATIVE_APERTURE = [[233, 299], [240, 299], [246, 305], [252, 316], [251, 330],
                   [245, 334], [234, 327], [230, 319], [229, 308]]
WINDOW_CROP = (220, 296, 278, 348)
LUMA = np.array([0.2126, 0.7152, 0.0722])


def normalized_linear(samples: np.ndarray) -> list[float]:
    mean = linear(samples).mean(axis=0)
    return (mean / max(mean[0], 1e-12)).tolist()


def lamp_directory(directory: Path, orbit: int = 0) -> tuple[Path, str, str]:
    current = directory / "cabin-interior" / f"orbit-{orbit}"
    if current.is_dir():
        return current, "fire-on.png", "fire-off.png"
    if orbit != 0:
        raise ValueError(f"No same-camera on/off capture for orbit {orbit}")
    return directory / "cabin-interior", "lamp-on.png", "lamp-off.png"


def load_lamp(directory: Path, image: Image.Image, orbit: int = 0) -> tuple[np.ndarray, np.ndarray, np.ndarray, dict]:
    capture_directory, on_name, off_name = lamp_directory(directory, orbit)
    on = np.asarray(Image.open(capture_directory / on_name).convert("RGB"))
    off = np.asarray(Image.open(capture_directory / off_name).convert("RGB"))
    if on.shape != off.shape or not np.array_equal(on, np.asarray(image)):
        raise ValueError(f"Fire/lamp-on must equal the actual raw orbit-{orbit} frame; on/off dimensions must match")
    changed = np.any(on != off, axis=2)
    if not changed.any():
        raise ValueError("Actual on/off fixture contains no visible light changes")
    metadata = json.loads((capture_directory / "interior-light.json").read_text(encoding="utf-8"))
    if metadata["changedPixels"] != int(changed.sum()):
        raise ValueError("Actual RGB on/off difference disagrees with its native indexed-pixel report")
    return on, off, changed, {**metadata, "actualCaptureDirectory": str(capture_directory.resolve()), "orbitDegrees": orbit}


def samples_report(image: np.ndarray, mask: np.ndarray) -> dict:
    if not mask.any():
        return {"pixelCount": 0}
    samples = image[mask]
    colors, counts = np.unique(samples, axis=0, return_counts=True)
    common = np.argsort(counts)[-8:][::-1]
    return {**statistics(samples), "normalizedLinearMeanRGB": normalized_linear(samples),
            "commonActualRGB": [{"rgb": colors[index].tolist(), "pixels": int(counts[index])} for index in common]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path, help="Actual current single-lamp smoke output")
    parser.add_argument("--after", type=Path, help="Optional actual flame-lit smoke output, same camera and imported source")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    before_image, frame = load_frame(args.before)
    if before_image.size != (640, 640) or frame["focus"] != [71, 69] or frame["cameraPan"] != [0, 0]:
        raise ValueError("Re-audit the source aperture polygon when fixture dimensions/focus/pan change")
    if not np.allclose([frame["cameraYaw"], frame["cameraPitch"], frame["cameraDistance"]],
                       [np.pi / 4, np.pi / 6, 22], atol=0.00001, rtol=0):
        raise ValueError("Native aperture polygon requires the original calibrated camera")
    native_image = Image.open(args.before / "turntables/cabin-east-native.png").convert("RGB")
    native = np.asarray(native_image)
    native_aperture = roi_mask(native_image.size, NATIVE_APERTURE)
    # Report the entire aperture, including black/frame pixels, separately from
    # its warm paint. This bounded color selection does not infer transparency.
    native_warm = native_aperture & (native[:, :, 0] >= 25) & (native[:, :, 0] > native[:, :, 1] * 1.1) & (native[:, :, 1] > native[:, :, 2] * 1.2)
    before, before_off, changed, metadata = load_lamp(args.before, before_image)
    bright = changed & (before @ LUMA >= 120)
    reflected = changed & ~bright
    report = {
        "schemaVersion": 1,
        "method": "actual captured RGB only; native aperture reviewed in place; on/off visibility mask from real runtime; no registration, recoloring or brightness adjustment",
        "limitations": "Native warm paint combines glass, frame and baked illumination. The meshes/apertures differ. Bright/non-bright color bands are not triangle IDs; the baseline bright band is corroborated by its constant emissive texture. Linear ratio fits are local suggestions, not pixel-perfect or physical-temperature claims.",
        "before": str(args.before.resolve()), "frame": frame,
        "nativeAperturePolygon": NATIVE_APERTURE, "windowCropLTRB": WINDOW_CROP,
        "nativeEntireAperture": samples_report(native, native_aperture),
        "nativeWarmPaint": samples_report(native, native_warm),
        "beforeAllActualLightChanges": samples_report(before, changed),
        "beforeNonBrightActualLightChanges": samples_report(before, reflected),
        "beforeBrightActualLightChanges": samples_report(before, bright),
        "beforeOffSameChangedPixels": samples_report(before_off, changed),
        "beforeInteriorMetadata": metadata,
    }
    capture_directory, on_name, _ = lamp_directory(args.before)
    palette_source = Image.open(capture_directory / on_name)
    if palette_source.mode == "P":
        palette = np.array(palette_source.getpalette(), dtype=float).reshape(-1, 3)
        wanted = np.array([250, 193, 38])
        distances = ((palette - wanted) ** 2).sum(axis=1)
        index = int(np.argmin(distances))
        report["baselineEmissionQuantization"] = {"requestedSrgb": wanted.tolist(), "closestPaletteIndex": index,
                                                   "actualPaletteRGB": palette[index].astype(int).tolist(),
                                                   "encodedRgbDistance": float(np.sqrt(distances[index]))}
    target = linear(native[native_warm]).mean(axis=0)
    observed = linear(before[reflected]).mean(axis=0)
    gain = target / np.maximum(observed, 1e-12)
    report["localLinearGainEstimate"] = gain.tolist()
    report["suggestedPrototype"] = {
        "reflectedFireColorLinearRGB": [1.0, 0.665, 0.094], "localEnergyFractionOfCurrent": 0.88,
        "samePositionSingleSourceIntensity": 5.3,
        "multipleSources": "Fit combined contribution at the visible interior; do not assign the former full intensity to every candle.",
        "flamePaletteSrgbRamp": {"dimTip": [107, 74, 24], "amberBody": [181, 93, 27], "smallHotCore": [221, 196, 126]},
        "scope": "Interior point-source color/emission only. Preserve accepted exterior ambient/directional profile.",
        "caveat": "Changing source positions, normals, albedo and palette quantization changes this fit; judge the actual new captures. Keep the hot core geometrically small rather than a uniform luminous bulb.",
    }
    panels = [("Original / janela nativa", native_image), ("Antes / interior anterior", before_image)]
    rear_panels = None
    if args.after:
        after_image, after_frame = load_frame(args.after)
        assert_same_pose(frame, after_frame)
        after_native = Image.open(args.after / "turntables/cabin-east-native.png").convert("RGB")
        if native_image.tobytes() != after_native.tobytes():
            raise ValueError("Native reference changed between actual captures")
        before_asset = args.before / "d3d-models/cabin-east.d3d"
        after_asset = args.after / "d3d-models/cabin-east.d3d"
        if hashlib.sha256(before_asset.read_bytes()).digest() != hashlib.sha256(after_asset.read_bytes()).digest():
            raise ValueError("Imported source changed; fire-color comparison requires unchanged exterior source")
        after, after_off, after_changed, after_metadata = load_lamp(args.after, after_image)
        after_bright = after_changed & (after @ LUMA >= 120)
        report.update({"after": str(args.after.resolve()), "sourceAssetUnchanged": True,
                       "afterAllActualLightChanges": samples_report(after, after_changed),
                       "afterNonBrightActualLightChanges": samples_report(after, after_changed & ~after_bright),
                       "afterBrightActualLightChanges": samples_report(after, after_bright),
                       "afterOffSameChangedPixels": samples_report(after_off, after_changed), "afterInteriorMetadata": after_metadata})
        before_state = json.loads((args.before / "lighting-state.json").read_text(encoding="utf-8"))
        after_state = json.loads((args.after / "lighting-state.json").read_text(encoding="utf-8"))
        exterior_fields = ("ambientLinearRGB", "directionalLinearRGB", "toLight", "directionalIntensity")
        if any(before_state[field] != after_state[field] for field in exterior_fields):
            raise ValueError("Accepted exterior light profile changed during the interior comparison")
        report["acceptedExteriorLightProfileUnchanged"] = {field: after_state[field] for field in exterior_fields}
        material_patches = {}
        for key, roi in ROIS.items():
            selected = roi_mask(before_image.size, roi["polygon"])
            material_patches[key] = {"pixels": int(selected.sum()), "changedPixels": int(np.any(before[selected] != after[selected], axis=1).sum())}
        if any(patch["changedPixels"] for patch in material_patches.values()):
            raise ValueError("A calibrated front exterior material patch changed; inspect that defect before approving the tone")
        report["frontExteriorMaterialPatches"] = material_patches
        before_manifest = json.loads((args.before / "turntables/turntables.json").read_text(encoding="utf-8"))
        after_manifest = json.loads((args.after / "turntables/turntables.json").read_text(encoding="utf-8"))
        camera_checks = []
        for angle in (-5, 0, 5, 90, 180, 270):
            old_frame = next(item for item in before_manifest["frames"] if item["object"] == "cabin-east" and item["orbitDegrees"] == angle)
            new_frame = next(item for item in after_manifest["frames"] if item["object"] == "cabin-east" and item["orbitDegrees"] == angle)
            assert_same_pose(dict(old_frame, archiveMode=before_manifest["archiveMode"]), dict(new_frame, archiveMode=after_manifest["archiveMode"]))
            camera_checks.append({"orbitDegrees": angle, "cameraAndFixtureIdentical": True, "architectureHoles": new_frame["architectureHoles"]})
        report["actualOrbitChecks"] = camera_checks
        if (args.after / "cabin-interior/orbit-0").is_dir():
            rear_frame = next(item for item in after_manifest["frames"] if item["object"] == "cabin-east" and item["orbitDegrees"] == 180)
            rear_image = Image.open(args.after / "turntables" / rear_frame["file"]).convert("RGB")
            rear_on, rear_off, rear_changed, rear_metadata = load_lamp(args.after, rear_image, 180)
            if rear_metadata["changesOutsideCabinOwner"] != 0:
                raise ValueError("Rear source changed unrelated geometry")
            ys, xs = np.where(rear_changed)
            rear_box = (max(0, int(xs.min()) - 12), max(0, int(ys.min()) - 12),
                        min(640, int(xs.max()) + 13), min(640, int(ys.max()) + 13))
            report["rearActualOnOff"] = {"actualLightChanges": samples_report(rear_on, rear_changed),
                                         "offSameChangedPixels": samples_report(rear_off, rear_changed),
                                         "metadata": rear_metadata, "cropLTRB": rear_box}
            rear_panels = rear_off, rear_on, rear_box
        panels.append(("Depois / velas 3D", after_image))
    args.output.mkdir(parents=True, exist_ok=True)
    width, height, scale = 58, 52, 8
    contact = Image.new("RGB", (width * scale * len(panels), height * scale + 52), (24, 24, 24))
    draw = ImageDraw.Draw(contact)
    for column, (label, image) in enumerate(panels):
        draw.text((column * width * scale + 5, 5), label, fill="white")
        contact.paste(image.crop(WINDOW_CROP).resize((width * scale, height * scale), Image.Resampling.NEAREST), (column * width * scale, 25))
    draw.text((5, height * scale + 32), "Mesmo recorte/camera; posicoes diferentes preservadas. Ampliacao nearest; nenhuma cor ajustada.", fill="white")
    contact.save(args.output / "window-fire-tone.png")
    if rear_panels is not None:
        rear_off, rear_on, rear_box = rear_panels
        rear_width, rear_height = rear_box[2] - rear_box[0], rear_box[3] - rear_box[1]
        rear_contact = Image.new("RGB", (rear_width * scale * 2, rear_height * scale + 52), (24, 24, 24))
        rear_draw = ImageDraw.Draw(rear_contact)
        for column, (label, array) in enumerate((("Traseira / fogo desligado", rear_off), ("Traseira / fogo ligado", rear_on))):
            rear_draw.text((column * rear_width * scale + 5, 5), label, fill="white")
            rear_contact.paste(Image.fromarray(array).crop(rear_box).resize((rear_width * scale, rear_height * scale), Image.Resampling.NEAREST),
                               (column * rear_width * scale, 25))
        rear_draw.text((5, rear_height * scale + 32), "Capturas reais da mesma vista180; abertura traseira. Sem alinhamento ou ajuste de cores.", fill="white")
        rear_contact.save(args.output / "window-rear-fire-off-on.png")
    mask_image = Image.new("RGB", native_image.size, (0, 0, 0))
    values = np.asarray(mask_image).copy()
    values[native_aperture] = [0, 70, 70]
    values[native_warm] = [0, 190, 190]
    values[changed] = [120, 30, 30]
    values[bright] = [255, 190, 20]
    Image.fromarray(values).crop(WINDOW_CROP).resize((width * scale, height * scale), Image.Resampling.NEAREST).save(args.output / "audited-source-and-on-off-masks.png")
    (args.output / "fire-tone-audit.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(args.output.resolve())


if __name__ == "__main__":
    main()
