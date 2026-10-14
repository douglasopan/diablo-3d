#!/usr/bin/env python3
"""Synthetic and optional native integration tests; never load game profiles."""
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

import numpy as np
from PIL import Image

import inspect_glb as glb
import export_town_model_v2 as exporter
import validate_town_asset_lods as validator


def make_glb(path, *, uv=True, normal=True, tangent=True, transform=True, extended=False):
    blob = bytearray()
    views, accessors = [], []

    def view(data):
        blob.extend(b"\0" * (-len(blob) % 4))
        index = len(views)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)})
        blob.extend(data)
        return index

    def accessor(array, kind, component):
        index = len(accessors)
        accessors.append({"bufferView": view(array.tobytes()), "componentType": component, "count": len(array), "type": kind})
        return index

    attrs = {"POSITION": accessor(np.asarray([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype="<f4"), "VEC3", 5126)}
    if normal:
        attrs["NORMAL"] = accessor(np.asarray([[0, 0, 1]] * 3, dtype="<f4"), "VEC3", 5126)
    if uv:
        attrs["TEXCOORD_0"] = accessor(np.asarray([[-1, 2], [1, 0], [0, 1]], dtype="<f4"), "VEC2", 5126)
    if tangent:
        attrs["TANGENT"] = accessor(np.asarray([[1, 0, 0, 1]] * 3, dtype="<f4"), "VEC4", 5126)
    attrs["COLOR_0"] = accessor(np.asarray([[1, .5, .25, .75]] * 3, dtype="<f4"), "VEC4", 5126)
    indices = accessor(np.asarray([0, 1, 2], dtype="<u2"), "SCALAR", 5123)
    buffer = io.BytesIO()
    Image.new("RGBA", (2, 2), (64, 128, 192, 127)).save(buffer, format="PNG")
    image_bytes = buffer.getvalue()
    image_view = view(image_bytes)
    texture_info = {"index": 0, "extensions": {"KHR_texture_transform": {"offset": [.2, -.3], "scale": [2, .5], "rotation": .4}}}
    material = {"pbrMetallicRoughness": {"baseColorFactor": [.8, .6, .4, .5], "metallicFactor": .3, "roughnessFactor": .7,
                                         "baseColorTexture": texture_info, "metallicRoughnessTexture": {"index": 0}},
                "normalTexture": {"index": 0, "scale": .4}, "occlusionTexture": {"index": 0, "strength": .6},
                "emissiveTexture": {"index": 0}, "emissiveFactor": [.1, .2, .3], "alphaMode": "MASK", "alphaCutoff": .45, "doubleSided": True}
    if extended:
        material["extensions"] = {"KHR_materials_transmission": {"transmissionFactor": .5}}
    node = {"mesh": 0}
    if transform:
        node.update({"scale": [-2, 1, 3], "translation": [-4, 5, -6]})
    document = {"asset": {"version": "2.0"}, "buffers": [{"byteLength": len(blob)}], "bufferViews": views, "accessors": accessors,
                "meshes": [{"primitives": [{"attributes": attrs, "indices": indices, "material": 0}]}],
                "nodes": [node], "scenes": [{"nodes": [0]}], "scene": 0, "materials": [material],
                "textures": [{"source": 0, "sampler": 0}], "samplers": [{"wrapS": 33648, "wrapT": 33071, "minFilter": 9984, "magFilter": 9728}],
                "images": [{"bufferView": image_view, "mimeType": "image/png"}], "extensionsUsed": ["KHR_texture_transform"]}
    write_glb(path, document, bytes(blob))
    return document, bytes(blob), image_bytes


def write_glb(path, document, binary):
    encoded = json.dumps(document, separators=(",", ":")).encode()
    encoded += b" " * (-len(encoded) % 4)
    binary += b"\0" * (-len(binary) % 4)
    data = struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(encoded) + 8 + len(binary))
    data += struct.pack("<II", len(encoded), glb.JSON_CHUNK) + encoded + struct.pack("<II", len(binary), glb.BIN_CHUNK) + binary
    path.write_bytes(data)


def offsets(data):
    _, vc, ic, pc, mc, tc = struct.unpack_from("<6I", data, 8)
    at = 72
    for _ in range(4):
        length, = struct.unpack_from("<I", data, at)
        at += 4 + length
    at += 32
    return {"vertices": at, "indices": at + vc * 64, "materials": at + vc * 64 + ic * 4 + pc * 12,
            "textures": at + vc * 64 + ic * 4 + pc * 12 + mc * 196, "vc": vc, "ic": ic, "tc": tc}


class ModelV2Tests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.source = self.directory / "source.glb"
        self.output = self.directory / "candidate.d3d2"

    def tearDown(self):
        self.temporary.cleanup()

    def export(self):
        return exporter.export(self.source, self.output, asset_id="synthetic.triangle", variant="open", revision="r1")

    def test_attributes_and_original_pbr_bytes(self):
        _, _, payload = make_glb(self.source)
        before = self.source.read_bytes()
        audit = self.export()
        data = self.output.read_bytes()
        at = offsets(data)
        vertices = np.frombuffer(data, dtype="<f4", offset=at["vertices"], count=3 * 16).reshape(3, 16)
        np.testing.assert_array_equal(vertices[:, :3], [[-4, 5, -6], [-6, 5, -6], [-4, 6, -6]])
        np.testing.assert_array_equal(vertices[:, 3:6], [[0, 0, 1]] * 3)
        np.testing.assert_array_equal(vertices[:, 6:8], [[-1, 2], [1, 0], [0, 1]])
        np.testing.assert_array_equal(vertices[:, 8:12], [[-1, 0, 0, -1]] * 3)
        np.testing.assert_array_equal(np.frombuffer(data, dtype="<u4", offset=at["indices"], count=3), [0, 2, 1])
        self.assertEqual(data[at["textures"] + 64:], payload)
        self.assertEqual(audit["textures"][0]["sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(struct.unpack_from("<4I", data, at["textures"] + 12), (33648, 33071, 9984, 9728))
        material = struct.unpack_from("<12fII", data, at["materials"])
        np.testing.assert_allclose(material[:12], [.8, .6, .4, .5, .1, .2, .3, .3, .7, .45, .4, .6], atol=1e-7)
        self.assertEqual(material[12:], (1, 1))
        base_info = struct.unpack_from("<iI5f", data, at["materials"] + 56)
        np.testing.assert_allclose(base_info, [0, 0, .2, -.3, 2, .5, .4], atol=1e-7)
        self.assertEqual(self.source.read_bytes(), before)
        self.assertFalse(audit["approvedForGame"])
        self.assertFalse(audit["textureResized"])

    def test_generated_tangent_explicit(self):
        make_glb(self.source, tangent=False)
        self.assertEqual(self.export()["tangentMode"], "generated-uv-not-mikktspace")

    def test_no_uv_master_is_not_drawable(self):
        make_glb(self.source, uv=False)
        with self.assertRaisesRegex(ValueError, "requires UVs"):
            self.export()
        self.assertFalse(self.output.exists())

    def test_missing_normals_rejected(self):
        make_glb(self.source, normal=False)
        with self.assertRaisesRegex(ValueError, "requires UVs"):
            self.export()

    def test_no_overwrite(self):
        make_glb(self.source)
        self.export()
        previous = self.output.read_bytes()
        with self.assertRaisesRegex(ValueError, "new path"):
            self.export()
        self.assertEqual(self.output.read_bytes(), previous)

    def test_existing_sidecar_preserved(self):
        make_glb(self.source)
        sidecar = self.output.with_suffix(".d3d2.json")
        sidecar.write_text("preserved")
        with self.assertRaisesRegex(ValueError, "sidecar must be new"):
            self.export()
        self.assertEqual(sidecar.read_text(), "preserved")
        self.assertFalse(self.output.exists())

    def test_multiple_material_primitives(self):
        document, binary, _ = make_glb(self.source)
        document["materials"].append({"pbrMetallicRoughness": {"baseColorFactor": [.2, .3, .4, 1]}})
        second = copy.deepcopy(document["meshes"][0]["primitives"][0])
        second["material"] = 1
        document["meshes"][0]["primitives"].append(second)
        write_glb(self.source, document, binary)
        audit = self.export()
        self.assertEqual((audit["triangles"], audit["primitives"], audit["materials"]), (2, 2, 2))
        if os.environ.get("TOWN_MODEL_V2_VALIDATOR"):
            result = subprocess.run([os.environ["TOWN_MODEL_V2_VALIDATOR"], str(self.output), audit["runtimeSha256"]], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_material_extensions_never_silently_dropped(self):
        make_glb(self.source, extended=True)
        with self.assertRaisesRegex(ValueError, "Material extension"):
            self.export()

    def test_required_extensions_rejected(self):
        document, binary, _ = make_glb(self.source)
        document["extensionsRequired"] = ["unknown_extension"]
        write_glb(self.source, document, binary)
        with self.assertRaisesRegex(ValueError, "required glTF"):
            self.export()

    def test_additional_uv_rejected(self):
        document, binary, _ = make_glb(self.source)
        attrs = document["meshes"][0]["primitives"][0]["attributes"]
        attrs["TEXCOORD_1"] = attrs["TEXCOORD_0"]
        write_glb(self.source, document, binary)
        with self.assertRaisesRegex(ValueError, "Unsupported vertex"):
            self.export()

    def manifest(self):
        make_glb(self.source)
        audit = self.export()
        manifest = {"schemaVersion": 2, "assetId": "synthetic.triangle", "variant": "open", "revision": "r1",
                    "anchor": {"position": [0, 0, 0], "upAxis": "Y"}, "openings": {"pipelineId": "native", "compatibility": "native-unchanged"},
                    "geometricReferenceSha256": audit["sourceSha256"], "sources": [
                        {"role": "master-geometry", "path": "source.glb", "sha256": audit["sourceSha256"]},
                        {"role": "drawable-lod", "path": "source.glb", "sha256": audit["sourceSha256"], "lodIndex": 0, "geometricError": 0,
                         "errorVerified": True, "runtimePath": "candidate.d3d2", "runtimeSha256": audit["runtimeSha256"]}]}
        return manifest

    def validate(self, manifest):
        path = self.directory / "review.json"
        path.write_text(json.dumps(manifest))
        executable = Path(os.environ["TOWN_MODEL_V2_VALIDATOR"]) if "TOWN_MODEL_V2_VALIDATOR" in os.environ else None
        return validator.validate(path, executable)

    def test_manifest_identity_and_inspection_role(self):
        report = self.validate(self.manifest())
        self.assertFalse(report["sources"][0]["automaticEligible"])
        self.assertTrue(report["sources"][1]["automaticEligible"])
        self.assertFalse(report["selectionChanged"])

    def test_manifest_hash_mismatch(self):
        manifest = self.manifest()
        manifest["sources"][1]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.validate(manifest)

    def test_manifest_anchor_incompatibility(self):
        manifest = self.manifest()
        manifest["anchor"]["position"] = [1, 0, 0]
        with self.assertRaisesRegex(ValueError, "anchor/openings"):
            self.validate(manifest)

    def test_decimal_anchor_canonical_float32(self):
        manifest = self.manifest()
        self.output.unlink()
        self.output.with_suffix(".d3d2.json").unlink()
        audit = exporter.export(self.source, self.output, asset_id="synthetic.triangle", variant="open", revision="r1", anchor=[.1, .2, .3])
        manifest["anchor"]["position"] = [.1, .2, .3]
        manifest["sources"][1]["runtimeSha256"] = audit["runtimeSha256"]
        self.assertTrue(self.validate(manifest)["sources"][1]["automaticEligible"])

    @unittest.skipUnless(os.environ.get("TOWN_MODEL_V2_VALIDATOR"), "Set TOWN_MODEL_V2_VALIDATOR for C++ integration")
    def test_jpeg_actual_dimensions_checked(self):
        document, binary, png = make_glb(self.source)
        buffer = io.BytesIO()
        Image.open(io.BytesIO(png)).convert("RGB").save(buffer, format="JPEG")
        jpeg = buffer.getvalue()
        image_view = document["bufferViews"][document["images"][0]["bufferView"]]
        binary = binary[:image_view["byteOffset"]] + jpeg
        image_view["byteLength"] = len(jpeg)
        document["buffers"][0]["byteLength"] = len(binary)
        document["images"][0]["mimeType"] = "image/jpeg"
        write_glb(self.source, document, binary)
        audit = self.export()
        executable = os.environ["TOWN_MODEL_V2_VALIDATOR"]
        result = subprocess.run([executable, str(self.output), audit["runtimeSha256"]], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        modified = bytearray(self.output.read_bytes())
        struct.pack_into("<I", modified, offsets(modified)["textures"] + 8, 8192)
        self.output.write_bytes(modified)
        result = subprocess.run([executable, str(self.output), hashlib.sha256(modified).hexdigest()], capture_output=True)
        self.assertNotEqual(result.returncode, 0)

    def test_manifest_rejects_nonuniform_fit(self):
        manifest = self.manifest()
        manifest["fit"] = {"scale": [1, 2, 3]}
        with self.assertRaisesRegex(ValueError, "uniform scale"):
            self.validate(manifest)

    def test_unverified_lod_and_openings_quarantined(self):
        manifest = self.manifest()
        derivative = copy.deepcopy(manifest["sources"][1])
        derivative.update({"lodIndex": 1, "geometricError": .1, "errorVerified": False, "maxDiameterPixels": 120, "openingEquivalenceVerified": True})
        manifest["sources"].append(derivative)
        self.assertFalse(self.validate(manifest)["sources"][2]["automaticEligible"])
        derivative.update({"errorVerified": True, "openingEquivalenceVerified": False})
        self.assertFalse(self.validate(manifest)["sources"][2]["automaticEligible"])

    def test_geometry_master_cannot_be_runtime_lod(self):
        manifest = self.manifest()
        manifest["sources"][0]["runtimePath"] = "candidate.d3d2"
        with self.assertRaisesRegex(ValueError, "Geometry-only master"):
            self.validate(manifest)

    def test_manifest_requires_preserved_master(self):
        manifest = self.manifest()
        manifest["sources"] = manifest["sources"][1:]
        with self.assertRaisesRegex(ValueError, "preserved inspection master"):
            self.validate(manifest)

    @unittest.skipUnless(os.environ.get("TOWN_MODEL_V2_VALIDATOR"), "Set TOWN_MODEL_V2_VALIDATOR for C++ integration")
    def test_native_texture_hash_and_dimensions_checked(self):
        make_glb(self.source)
        self.export()
        data = self.output.read_bytes()
        at = offsets(data)["textures"]
        executable = os.environ["TOWN_MODEL_V2_VALIDATOR"]
        self.assertEqual(subprocess.run([executable, str(self.output), hashlib.sha256(data).hexdigest()], capture_output=True).returncode, 0)
        for mutation in ("hash", "dimensions", "sampler"):
            modified = bytearray(data)
            if mutation == "hash":
                modified[-8] ^= 1
            elif mutation == "dimensions":
                struct.pack_into("<I", modified, at + 4, 3)
            else:
                struct.pack_into("<I", modified, at + 12, 7)
            self.output.write_bytes(modified)
            result = subprocess.run([executable, str(self.output), hashlib.sha256(modified).hexdigest()], capture_output=True)
            self.assertNotEqual(result.returncode, 0, mutation)


if __name__ == "__main__":
    unittest.main()
