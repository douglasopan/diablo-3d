#!/usr/bin/env python3
"""Offline target/validation checks; synthetic files and a mocked native loader.

No game, API, licensed data or user profile is opened. Real-loader integration
remains covered separately by test_godot_bridge.py.
"""
from __future__ import annotations

import argparse
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import godot_bridge as bridge


def state(directory: Path) -> dict:
    return {p.relative_to(directory).as_posix(): (p.read_bytes(), p.stat().st_mtime_ns)
            for p in directory.rglob("*") if p.is_file()} if directory.exists() else {}


class PackTargetChecks(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="d3d-pack-target-")
        self.addCleanup(self.temporary.cleanup)
        self.workspace = Path(self.temporary.name)
        self.habitual = self.workspace / "perfil-tristram"
        self.habitual.mkdir()
        for relative, data in {
            "diablo.ini": b"[Graphics]\nWidth=1600\nHeight=900\n",
            "single_0.sv": b"synthetic-save-preservation-fixture",
            "d3d-models/cabin-east.d3d": b"synthetic-selected-cabin",
            "d3d-lighting.ini": b"synthetic-selected-lighting",
            "d3d-maps/tristram.ini": b"synthetic-previous-manifest",
            "d3d-models/editor/well.d3d": b"synthetic-previous-model",
        }.items():
            target = self.habitual / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        self.pack = self.workspace / "input-pack"
        model = self.pack / "d3d-models/editor/well.d3d"
        model.parent.mkdir(parents=True)
        model.write_bytes(b"D3DMESH1" + struct.pack("<III15f3B", 1, 1, 1,
                          0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 20, 30, 40))
        self.expected = bridge.digest(model)
        self.manifest = ("[Scene]\nformat=d3d.town-map\nschemaVersion=1\nedition=retail\ninstance=well\n"
                         "\n[well]\nassetId=tristram.arch.well\nvariantId=clean-water\n"
                         "nativeMin=60,70\nnativeMax=61,71\nmodel=d3d-models/editor/well.d3d\n"
                         f"sha256={self.expected}\nrevision=synthetic-offline-target-v1\ninterior=none\n").encode()
        manifest_path = self.pack / "d3d-maps/tristram.ini"
        manifest_path.parent.mkdir(parents=True)
        manifest_path.write_bytes(self.manifest)
        self.executable = self.workspace / "candidate.exe"
        self.executable.write_bytes(b"synthetic-file-never-executed")
        self.args = argparse.Namespace(
            workspace=self.workspace, project=self.workspace / "editor", pack=self.pack,
            executable=self.executable, allow_prototypes=False,
            profile="perfil-tristram", validate_only=True,
        )
        self.snapshot = {
            "models": [{"instanceId": "well", "sha256": self.expected}],
            "editorMapAudit": {
                "assetPath": "d3d-maps/tristram.ini", "status": "matched", "failure": "",
                "sha256": hashlib.sha256(self.manifest).hexdigest(),
                "instances": [{"instanceId": "well", "status": "matched", "failure": "",
                               "sha256": self.expected, "expectedSha256": self.expected}],
            },
        }
        self.events: list[tuple[str, Path]] = []
        self.baseline = self.enter_patch("install_baseline", side_effect=self.install_baseline)
        self.native = self.enter_patch("run_snapshot", side_effect=self.run_snapshot)
        self.enter_patch("check_cabin")
        self.closed = self.enter_patch("check_closed", side_effect=self.check_closed)
        self.enter_patch("subprocess.run", side_effect=AssertionError("offline checks must never launch a process"))
        self.output = io.StringIO()

    def enter_patch(self, name: str, **kwargs):
        mocked = patch("godot_bridge." + name, **kwargs)
        result = mocked.start()
        self.addCleanup(mocked.stop)
        return result

    def install_baseline(self, root: Path, profile: Path) -> dict:
        self.events.append(("baseline", profile))
        return {"pipeline": {"id": "synthetic-offline-baseline"}}

    def run_snapshot(self, args: argparse.Namespace, staging: Path) -> dict:
        self.events.append(("snapshot", staging))
        return self.snapshot

    def check_closed(self, profile: Path) -> None:
        self.events.append(("closed", profile))

    def apply(self) -> None:
        with redirect_stdout(self.output):
            bridge.apply_pack(self.args)

    def test_default_and_explicit_target_choices(self) -> None:
        parser = bridge.argument_parser()
        default = parser.parse_args(["apply"])
        self.assertEqual(default.profile, "perfil-godot-review")
        self.assertFalse(default.validate_only)
        for name in bridge.TARGET_PROFILES:
            parsed = parser.parse_args(["apply", "--profile", name, "--validate-only"])
            self.assertEqual(parsed.profile, name)
            self.assertTrue(parsed.validate_only)
            self.assertEqual(bridge.target_profile(argparse.Namespace(workspace=self.workspace, profile=name)),
                             self.workspace / name)

    def test_invalid_target_rejected_by_parser_and_application(self) -> None:
        for name in ("../other", "custom-profile", "D:/outside", str(self.habitual)):
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                bridge.argument_parser().parse_args(["apply", "--profile", name])
            self.args.profile = name
            with self.assertRaises(ValueError):
                self.apply()
        self.assertFalse(self.baseline.called)
        self.assertFalse(self.native.called)

    def test_validate_only_preserves_both_profiles_and_input_without_process_guard(self) -> None:
        before_profile, before_pack = state(self.habitual), state(self.pack)
        self.closed.side_effect = AssertionError("validation must not inspect or interrupt an open game")
        self.apply()
        self.assertEqual(state(self.habitual), before_profile)
        self.assertEqual(state(self.pack), before_pack)
        self.assertFalse((self.workspace / "perfil-godot-review").exists())
        self.assertFalse(self.closed.called)
        self.assertEqual(self.baseline.call_count, 1)
        self.assertEqual([name for name, _ in self.events], ["baseline", "snapshot"])
        self.assertEqual(state(self.events[1][1])["d3d-maps/tristram.ini"][0], self.manifest)

    def test_validation_cannot_be_redirected_inside_a_game_profile(self) -> None:
        before = state(self.habitual)
        self.args.project = self.habitual / "misdirected-editor"
        with self.assertRaisesRegex(ValueError, "fora dos perfis"):
            self.apply()
        self.assertEqual(state(self.habitual), before)
        self.assertFalse(self.baseline.called)

    def test_live_game_guard_occurs_after_validation_before_any_profile_write(self) -> None:
        before_profile, before_pack = state(self.habitual), state(self.pack)
        self.args.validate_only = False
        def refuse(profile: Path) -> None:
            self.events.append(("closed", profile))
            raise ValueError("synthetic open game")
        self.closed.side_effect = refuse
        with self.assertRaisesRegex(ValueError, "open game"):
            self.apply()
        self.assertEqual([name for name, _ in self.events], ["baseline", "snapshot", "closed"])
        self.assertEqual(self.baseline.call_count, 1)
        self.assertEqual(state(self.habitual), before_profile)
        self.assertEqual(state(self.pack), before_pack)

    def test_explicit_habitual_apply_preserves_save_config_baseline_and_backs_up_pack(self) -> None:
        before, before_pack = state(self.habitual), state(self.pack)
        self.args.validate_only = False
        self.apply()
        for relative in ("diablo.ini", "single_0.sv", "d3d-models/cabin-east.d3d", "d3d-lighting.ini"):
            self.assertEqual(state(self.habitual)[relative], before[relative])
        self.assertEqual(state(self.pack), before_pack)
        self.assertEqual([name for name, _ in self.events], ["baseline", "snapshot", "closed", "baseline"])
        self.assertEqual(self.events[-1][1], self.habitual)
        self.assertEqual((self.habitual / "d3d-maps/tristram.ini").read_bytes(), self.manifest)
        backups = list((self.habitual / "editor-backups").glob("*/d3d-maps/tristram.ini"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), before["d3d-maps/tristram.ini"][0])
        receipt = json.loads((self.habitual / "godot-pack-receipt.json").read_text(encoding="utf-8"))
        self.assertEqual(receipt["gameExecutableSha256"], bridge.digest(self.executable))
        self.assertEqual(receipt["manifestSha256"], hashlib.sha256(self.manifest).hexdigest())

    def test_legacy_namespace_keeps_review_default_and_preserves_habitual(self) -> None:
        before = state(self.habitual)
        del self.args.profile
        del self.args.validate_only
        self.apply()
        review = self.workspace / "perfil-godot-review"
        self.assertTrue((review / "godot-pack-receipt.json").is_file())
        self.assertEqual((review / "single_0.sv").read_bytes(), (self.habitual / "single_0.sv").read_bytes())
        self.assertEqual(state(self.habitual), before)

    def test_loader_failure_preserves_profile_and_does_not_reach_live_guard(self) -> None:
        before = state(self.habitual)
        self.args.validate_only = False
        self.snapshot["editorMapAudit"]["status"] = "partial"
        with self.assertRaisesRegex(ValueError, "loader real recusou"):
            self.apply()
        self.assertEqual(state(self.habitual), before)
        self.assertFalse(self.closed.called)
        self.assertEqual(self.baseline.call_count, 1)

    def test_missing_candidate_rejected_before_staging(self) -> None:
        before = state(self.habitual)
        self.args.executable = self.workspace / "missing.exe"
        with self.assertRaises(OSError):
            self.apply()
        self.assertEqual(state(self.habitual), before)
        self.assertFalse(self.baseline.called)
        self.assertFalse(self.args.project.exists())


if __name__ == "__main__":
    unittest.main()
