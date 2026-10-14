#!/usr/bin/env python3
"""Isolated synthetic tests; no local models, services, editor or game required."""

from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path
import struct
import tempfile
import unittest

import numpy as np

import inspect_glb as glb
import transfer_town_material as transfer


PNG = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/l9sAAAAASUVORK5CYII=")


def fixture(path, positions, faces, uv=None, normals=None):
    """Small independent GLB writer for end-to-end fixtures."""
    positions = np.asarray(positions, dtype="<f4")
    faces = np.asarray(faces, dtype="<u4").reshape(-1)
    normals = np.tile([0, 0, 2], (len(positions), 1)).astype("<f4") if normals is None else np.asarray(normals, dtype="<f4")
    document = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
                "nodes": [{"mesh": 0}], "meshes": [{"primitives": []}], "buffers": [{"byteLength": 0}],
                "bufferViews": [], "accessors": [], "materials": [{"doubleSided": True,
                    "pbrMetallicRoughness": {"metallicFactor": 0.15, "roughnessFactor": 0.7}}]}
    binary = bytearray()

    def view(data):
        binary.extend(b"\0" * (-len(binary) % 4))
        document["bufferViews"].append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)})
        binary.extend(data)
        return len(document["bufferViews"]) - 1

    def accessor(values, kind, component):
        document["accessors"].append({"bufferView": view(values.tobytes()), "componentType": component,
                                      "count": len(values), "type": kind})
        return len(document["accessors"]) - 1

    attrs = {"POSITION": accessor(positions, "VEC3", 5126), "NORMAL": accessor(normals, "VEC3", 5126)}
    if uv is not None:
        attrs["TEXCOORD_0"] = accessor(np.asarray(uv, dtype="<f4"), "VEC2", 5126)
        document["images"] = [{"bufferView": view(PNG), "mimeType": "image/png"}]
        document["textures"] = [{"source": 0}]
        document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"] = {"index": 0}
        document["materials"][0]["normalTexture"] = {"index": 0, "scale": 0.6}
    document["meshes"][0]["primitives"].append({"attributes": attrs,
        "indices": accessor(faces, "SCALAR", 5125), "material": 0})
    document["buffers"][0]["byteLength"] = len(binary)
    encoded = json.dumps(document).encode()
    encoded += b" " * (-len(encoded) % 4)
    binary.extend(b"\0" * (-len(binary) % 4))
    path.write_bytes(struct.pack("<4sII", b"glTF", 2, 28 + len(encoded) + len(binary)) +
                     struct.pack("<II", len(encoded), glb.JSON_CHUNK) + encoded +
                     struct.pack("<II", len(binary), glb.BIN_CHUNK) + binary)


class ClosestSurfaceTests(unittest.TestCase):
    def test_analytic_interior_and_outside(self):
        tri = np.repeat(np.array([[[0., 0, 0], [1, 0, 0], [0, 1, 0]]]), 3, axis=0)
        distances, bary = transfer.closest_barycentric(np.array([[.25, .25, 2], [2, 0, 0], [-1, -1, 0]]), tri)
        np.testing.assert_allclose(distances, [2, 1, np.sqrt(2)])
        np.testing.assert_allclose(bary, [[.5, .25, .25], [0, 1, 0], [1, 0, 0]])

    def test_degenerate_triangle(self):
        tri = np.array([[[0., 0, 0], [2, 0, 0], [2, 0, 0]], [[1, 1, 1]] * 3])
        distances, bary = transfer.closest_barycentric(np.array([[1, 1, 0], [1, 1, 3]]), tri)
        np.testing.assert_allclose(distances, [1, 2])
        np.testing.assert_allclose(np.einsum("ij,ijk->ik", bary, tri), [[1, 0, 0], [1, 1, 1]])

    def test_nearest_is_not_nearest_centroid(self):
        positions = np.array([[0., 0, 0], [1000, 0, 0], [0, 1000, 0],
                              [-.1, -.1, 1], [.1, -.1, 1], [0, .1, 1]])
        surface = transfer.Surface(positions, [[0, 1, 2], [3, 4, 5]])
        nearest = surface.nearest(np.array([[.01, .01, .01]]))
        self.assertEqual(int(nearest["triangle"][0]), 0)
        self.assertAlmostEqual(float(nearest["distance"][0]), .01)

    def test_radius_search_matches_exhaustive_query(self):
        rng = np.random.default_rng(1844)
        triangles = rng.normal(size=(91, 3, 3)) * np.exp(rng.uniform(-3, 3, (91, 1, 1)))
        positions, faces = triangles.reshape(-1, 3), np.arange(91 * 3).reshape(-1, 3)
        points = rng.normal(size=(79, 3))
        nearest = transfer.Surface(positions, faces).nearest(points, batch_size=17)
        distances, _ = transfer.closest_barycentric(np.repeat(points, len(triangles), axis=0),
                                                     np.tile(triangles, (len(points), 1, 1)))
        distances = distances.reshape(len(points), len(triangles))
        np.testing.assert_allclose(nearest["distance"], distances.min(axis=1), rtol=1e-12, atol=1e-12)
        np.testing.assert_array_equal(nearest["triangle"], distances.argmin(axis=1))

    def test_normal_interpolation(self):
        surface = transfer.Surface(np.array([[0., 0, 0], [1, 0, 0], [0, 1, 0]]), [[0, 1, 2]],
                                   np.array([[0., 0, 2]] * 3))
        np.testing.assert_allclose(surface.nearest(np.array([[.2, .2, 0]]))["normal"], [[0, 0, 1]])

    def test_charts_join_continuous_edge_only(self):
        positions = np.array([[0., 0, 0], [1, 0, 0], [0, 1, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]])
        faces = np.array([[0, 1, 2], [3, 4, 5]])
        uv = positions[:, :2].copy()
        joined = transfer.uv_charts(positions, faces, uv, np.array([0, 0]))
        self.assertEqual(joined[0], joined[1])
        uv[3:] += 2
        split = transfer.uv_charts(positions, faces, uv, np.array([0, 0]))
        self.assertNotEqual(split[0], split[1])
        material_split = transfer.uv_charts(positions, faces, positions[:, :2], np.array([0, 1]))
        self.assertNotEqual(material_split[0], material_split[1])


class TransferTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.source = self.root / "source.glb"
        self.target = self.root / "target.glb"
        source_positions = [[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]]
        fixture(self.source, source_positions, [[0, 1, 2], [3, 4, 5]],
                [[0, 0], [.4, 0], [0, .4], [.6, 0], [1, .4], [.6, .4]])
        fixture(self.target, [[.1, .1, .01], [.9, .1, .01], [.9, .9, .01], [.1, .9, .01]],
                [[0, 1, 2], [0, 2, 3]])
        self.source_sha = transfer.sha256(self.source)
        self.target_sha = transfer.sha256(self.target)

    def tearDown(self):
        self.temp.cleanup()

    def arguments(self, **overrides):
        values = dict(source=self.source, target=self.target, output=self.root / "evidence",
                      sample_vertices=0, export=True, no_normal_variant=True,
                      batch_size=16, max_distance_fraction=.001, max_normal_angle=60)
        values.update(overrides)
        return argparse.Namespace(**values)

    def test_end_to_end_seam_positions_normals_material_images(self):
        report = transfer.transfer(self.arguments())
        self.assertEqual(report["seams"]["mixedChartTargetFaces"], 2)
        self.assertEqual(report["verticesBeyondDistance"], 4)
        self.assertTrue(all(report["preservation"].values()))
        self.assertEqual(transfer.sha256(self.source), self.source_sha)
        self.assertEqual(transfer.sha256(self.target), self.target_sha)
        _, _, original, _ = transfer.load_static(self.target, False)
        output = Path(report["outputs"][0]["path"])
        document, reader, candidate, _ = transfer.load_static(output, True)
        np.testing.assert_array_equal(candidate["world_vertices"][candidate["triangles"]],
                                      original["world_vertices"][original["triangles"]])
        np.testing.assert_array_equal(candidate["normals"][candidate["triangles"]],
                                      original["normals"][original["triangles"]])
        self.assertEqual(document["materials"], glb.read_glb(self.source)[0]["materials"])
        self.assertEqual(reader.view(document["images"][0]["bufferView"])[0], PNG)
        no_normal = glb.read_glb(Path(report["outputs"][1]["path"]))[0]
        self.assertNotIn("normalTexture", no_normal["materials"][0])
        self.assertEqual(document["materials"][0]["normalTexture"], {"index": 0, "scale": .6})
        # Each face is assigned one chart rather than interpolating through the
        # unrelated gap in the donor atlas (left chart <=.4, right chart >=.6).
        for face_uv in candidate["source_uv"][candidate["triangles"]]:
            self.assertTrue((face_uv[:, 0] <= .4 + 1e-6).all() or (face_uv[:, 0] >= .6 - 1e-6).all())

    def test_existing_evidence_is_not_overwritten(self):
        args = self.arguments(export=False)
        transfer.transfer(args)
        evidence_sha = transfer.sha256(args.output / "report.json")
        with self.assertRaisesRegex(ValueError, "Refusing to replace"):
            transfer.transfer(args)
        self.assertEqual(transfer.sha256(args.output / "report.json"), evidence_sha)

    def test_same_input_rejected(self):
        with self.assertRaisesRegex(ValueError, "different files"):
            transfer.transfer(self.arguments(target=self.source))

    def test_sample_is_audited_without_export(self):
        report = transfer.transfer(self.arguments(sample_vertices=2, export=False))
        self.assertFalse(report["sample"]["full"])
        self.assertEqual(report["sample"]["vertices"], 2)
        self.assertNotIn("outputs", report)
        self.assertFalse((self.root / "evidence/candidate-pbr.glb").exists())

    def test_missing_uv_rejected(self):
        with self.assertRaisesRegex(ValueError, "UVs"):
            transfer.load_static(self.target, True)

    def test_zero_normals_rejected(self):
        invalid = self.root / "zero-normal.glb"
        fixture(invalid, [[0, 0, 0], [1, 0, 0], [0, 1, 0]], [[0, 1, 2]], normals=np.zeros((3, 3)))
        with self.assertRaisesRegex(ValueError, "finite and nonzero"):
            transfer.load_static(invalid, False)

    def test_normal_threshold_flags_opposite_surface(self):
        opposite = self.root / "opposite.glb"
        fixture(opposite, [[0, 0, 0], [1, 0, 0], [0, 1, 0]], [[0, 1, 2]],
                [[0, 0], [1, 0], [0, 1]], normals=[[0, 0, -1]] * 3)
        report = transfer.transfer(self.arguments(source=opposite, export=False,
                                   max_distance_fraction=1, max_normal_angle=120))
        self.assertEqual(report["verticesBeyondDistance"], 0)
        self.assertEqual(report["verticesBeyondNormalAngle"], 4)
        self.assertAlmostEqual(report["normalAngleDegrees"]["min"], 180)


if __name__ == "__main__":
    unittest.main()
