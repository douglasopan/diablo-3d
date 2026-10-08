#!/usr/bin/env python3
"""Offline contract and failure tests. All transports are fakes; no credential or network needed."""
import io
import json
from pathlib import Path
import struct
import tempfile
import threading
import unittest
from unittest.mock import patch
import urllib.error
import urllib.request

import tripo_assets as api


SECRET = "not-a-real-key-for-offline-tests"
PNG = b"\x89PNG\r\n\x1a\nreference-fixture-"


class Response(io.BytesIO):
    def __init__(self, data, headers=None):
        super().__init__(data)
        self.headers = headers or {}


class FakeOpener:
    def __init__(self, data=None, error=None, headers=None):
        self.data, self.error, self.headers = data, error, headers
        self.calls = []

    def open(self, request, timeout):
        self.calls.append((request, timeout))
        if self.error is not None:
            raise self.error
        return Response(self.data, self.headers)


def glb():
    document = json.dumps({"asset": {"version": "2.0"}, "meshes": []}).encode()
    document += b" " * (-len(document) % 4)
    return struct.pack("<4sII", b"glTF", 2, 20 + len(document)) + \
        struct.pack("<I4s", len(document), b"JSON") + document


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="tripo-offline-")
        self.root = Path(self.temporary.name)
        self.directory = self.root / "asset"
        self.front, self.back = self.root / "front.png", self.root / "back.png"
        self.front.write_bytes(PNG + b"front")
        self.back.write_bytes(PNG + b"back")
        self.identity = {"assetId": "tristram.prop.example", "variant": "main", "revision": "r1"}
        self.calls = []

    def tearDown(self):
        self.temporary.cleanup()

    def plan(self, **kwargs):
        return api.plan_task(self.directory, self.identity, {"front": self.front, "back": self.back},
                             estimated_credits=30, **kwargs)

    def fake_api(self, path, credential, payload=None, *, upload=None):
        self.assertEqual(credential, SECRET)
        self.calls.append((path, payload, upload))
        if path == "/files":
            return {"file_token": "file_" + str(len(self.calls))}
        if path == "/account/balance":
            return {"balance": 100, "frozen": 5}
        if path in api.ENDPOINTS.values():
            return {"task_id": "task_fixture"}
        if path == "/tasks/task_fixture":
            return {"task_id": "task_fixture", "status": "success", "progress": 100,
                    "credits_consumed": 29.5,
                    "output": {"model_url": "https://cdn.example/model.glb?signature=private-example"}}
        self.fail("Unexpected fake endpoint")

    def created(self):
        self.plan()
        api.upload_inputs(self.directory, SECRET, request_fn=self.fake_api)
        return api.create_task(self.directory, SECRET, 30, request_fn=self.fake_api)

    def test_plan_offline_and_identity(self):
        with patch.object(urllib.request.OpenerDirector, "open", side_effect=AssertionError("Network forbidden")), \
                patch.object(api, "read_credential", side_effect=AssertionError("Credential forbidden")):
            plan = self.plan()
        self.assertEqual(plan["networkRequests"], 0)
        self.assertEqual(plan["provider"], "tripo")
        self.assertEqual(plan["parameters"]["model"], "v3.1-20260211")
        self.assertEqual(plan["parameters"]["texture_version"], "v3.5-20260815")
        self.assertEqual(plan["inputs"][0]["view"], "front")
        self.assertEqual(len(plan["inputHash"]), 64)
        self.assertFalse((self.directory / "task.json").exists())

    def test_multiview_requirements_and_duplicates(self):
        for images in ({"back": self.back, "left": self.front}, {"front": self.front},
                       {"front": self.front, "back": self.front}, {"image": self.front, "front": self.front}):
            with self.subTest(labels=list(images)), self.assertRaises(api.ToolError):
                api.plan_task(self.directory, self.identity, images)

    def test_geometry_only_never_forced_to_texture(self):
        plan = self.plan(texture=False)
        self.assertFalse(plan["parameters"]["texture"])
        self.assertFalse(plan["parameters"]["pbr"])
        self.assertNotIn("delight", plan["parameters"])
        self.assertNotIn("texture_version", plan["parameters"])

    def test_plan_cannot_be_overwritten_after_reservation(self):
        self.plan()
        with self.assertRaises(api.ToolError):
            self.plan()

    def test_uploads_resume_without_new_post(self):
        self.plan()
        api.upload_inputs(self.directory, SECRET, request_fn=self.fake_api)
        api.upload_inputs(self.directory, SECRET, request_fn=self.fake_api)
        self.assertEqual([call[0] for call in self.calls], ["/files", "/files"])
        self.assertTrue((self.directory / "uploads/front/request.json").is_file())
        self.assertTrue((self.directory / "uploads/back/post-state.json").is_file())

    def test_upload_timeout_never_retried(self):
        self.plan()
        def timeout(*args, **kwargs):
            self.calls.append("timeout")
            raise api.ToolError("Tripo transport failed; no request retry was made")
        with self.assertRaises(api.ToolError):
            api.upload_inputs(self.directory, SECRET, request_fn=timeout)
        with self.assertRaises(api.ToolError):
            api.upload_inputs(self.directory, SECRET, request_fn=timeout)
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(api.read_json(self.directory / "uploads/front/post-state.json")["state"], "UNKNOWN")

    def test_partial_uploads_resume_remaining_view(self):
        self.plan()
        # Simulate interruption between successful front upload and starting the back upload.
        data = self.front.read_bytes()
        item = api.load_plan(self.directory)["inputs"][0]
        with api.operation_lock(self.directory):
            api.post_once(self.directory / "uploads/front", lambda: {"file_token": "file_front"},
                          metadata={"inputSha256": item["sha256"]}, id_key="file_token", credential=SECRET)
        api.upload_inputs(self.directory, SECRET, request_fn=self.fake_api)
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(self.calls[0][2][0], self.back.read_bytes())
        self.assertNotEqual(self.calls[0][2][0], data)

    def test_changed_image_stops_before_any_upload(self):
        self.plan()
        self.back.write_bytes(PNG + b"changed")
        with self.assertRaises(api.ToolError):
            api.upload_inputs(self.directory, SECRET, request_fn=self.fake_api)
        self.assertEqual(self.calls, [])

    def test_credit_estimate_and_ceiling_before_requests(self):
        api.plan_task(self.directory, self.identity, {"image": self.front})
        with self.assertRaises(api.ToolError):
            api.create_task(self.directory, SECRET, 40, request_fn=self.fake_api)
        self.assertEqual(self.calls, [])
        self.assertFalse((self.directory / "create-state.json").exists())

    def test_insufficient_balance_prevents_generation(self):
        self.plan()
        api.upload_inputs(self.directory, SECRET, request_fn=self.fake_api)
        self.calls.clear()
        def insufficient(path, *args, **kwargs):
            self.calls.append(path)
            return {"balance": 29, "frozen": 100}
        with self.assertRaises(api.ToolError):
            api.create_task(self.directory, SECRET, 30, request_fn=insufficient)
        self.assertEqual(self.calls, ["/account/balance"])
        self.assertFalse((self.directory / "create-state.json").exists())

    def test_create_once_uses_labeled_views_and_fresh_balance(self):
        result = self.created()
        self.assertEqual(result["taskId"], "task_fixture")
        self.assertEqual([c[0] for c in self.calls], ["/files", "/files", "/account/balance",
                                                    "/generation/multiview-to-model"])
        self.assertEqual(self.calls[-1][1]["inputs"], [{"front": "file_1"}, {"back": "file_2"}])
        with self.assertRaises(api.ToolError):
            api.create_task(self.directory, SECRET, 30, request_fn=self.fake_api)
        self.assertEqual(len(self.calls), 4)

    def test_single_image_uses_input_not_multiview(self):
        api.plan_task(self.directory, self.identity, {"image": self.front}, estimated_credits=30)
        api.upload_inputs(self.directory, SECRET, request_fn=self.fake_api)
        api.create_task(self.directory, SECRET, 30, request_fn=self.fake_api)
        self.assertEqual(self.calls[-1][0], "/generation/image-to-model")
        self.assertEqual(self.calls[-1][1]["input"], "file_1")
        self.assertNotIn("inputs", self.calls[-1][1])

    def test_ambiguous_generation_durable_no_retry(self):
        self.plan()
        api.upload_inputs(self.directory, SECRET, request_fn=self.fake_api)
        def ambiguous(path, credential, payload=None):
            if payload is not None:
                self.calls.append("generation-attempt")
                raise api.ApiError(http_status=504)
            return {"balance": 100}
        for _ in range(2):
            with self.assertRaises(api.ToolError):
                api.create_task(self.directory, SECRET, 30, request_fn=ambiguous)
        self.assertEqual(self.calls.count("generation-attempt"), 1)
        self.assertEqual(api.read_json(self.directory / "create-state.json")["state"], "UNKNOWN")

    def test_concurrent_create_attempt_is_blocked(self):
        self.plan()
        api.upload_inputs(self.directory, SECRET, request_fn=self.fake_api)
        entered, release = threading.Event(), threading.Event()
        errors = []
        def slow(path, credential, payload=None):
            if payload is not None:
                entered.set()
                self.assertTrue(release.wait(5))
            return self.fake_api(path, credential, payload)
        def first():
            try:
                api.create_task(self.directory, SECRET, 30, request_fn=slow)
            except BaseException as error:
                errors.append(error)
        worker = threading.Thread(target=first)
        worker.start()
        try:
            self.assertTrue(entered.wait(5))
            with self.assertRaises(api.ToolError):
                api.create_task(self.directory, SECRET, 30, request_fn=self.fake_api)
        finally:
            release.set()
            worker.join(5)
        self.assertFalse(worker.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(sum(c[0] in api.ENDPOINTS.values() for c in self.calls), 1)

    def test_interrupted_lock_is_not_stolen(self):
        self.directory.mkdir()
        (self.directory / ".operation.lock").write_text("interrupted")
        with self.assertRaises(api.ToolError):
            self.plan()
        self.assertTrue((self.directory / ".operation.lock").exists())

    def test_mutated_texture_version_is_rejected(self):
        self.plan()
        plan = api.read_json(self.directory / "plan.json")
        plan["parameters"]["texture_version"] = "v3.0-20250812"
        api.atomic_json(self.directory / "plan.json", plan)
        with self.assertRaises(api.ToolError):
            api.upload_inputs(self.directory, SECRET, request_fn=self.fake_api)
        self.assertEqual(self.calls, [])

    def test_status_records_actual_cost_without_signed_urls(self):
        self.created()
        result = api.get_status(self.directory, SECRET, request_fn=self.fake_api)
        self.assertEqual(result["credits_consumed"], 29.5)
        self.assertEqual(api.read_json(self.directory / "task.json")["actualCredits"], 29.5)
        saved = (self.directory / "result.json").read_text()
        self.assertNotIn("signature", saved)
        self.assertNotIn("https", saved)
        self.assertNotIn(SECRET, saved)

    def test_status_rejects_task_identity_mismatch(self):
        self.created()
        with self.assertRaises(api.ToolError):
            api.get_status(self.directory, SECRET, request_fn=lambda *args: {
                "task_id": "task_other", "status": "success"})
        self.assertFalse((self.directory / "result.json").exists())

    def test_all_seven_official_statuses_persist_without_generation(self):
        self.created()
        self.calls.clear()
        self.assertEqual(api.TASK_STATUSES, {"queued", "running", "success", "failed", "cancelled", "banned", "expired"})
        for status in sorted(api.TASK_STATUSES):
            with self.subTest(status=status):
                calls = []
                def snapshot(path, credential):
                    calls.append(path)
                    return {"task_id": "task_fixture", "status": status, "credits_consumed": 0}
                result = api.get_status(self.directory, SECRET, request_fn=snapshot)
                self.assertEqual(result["status"], status)
                self.assertEqual(api.read_json(self.directory / "result.json")["status"], status)
                self.assertEqual(api.read_json(self.directory / "task.json")["status"], status)
                self.assertEqual(calls, ["/tasks/task_fixture"])
        self.assertEqual(self.calls, [])

    def test_download_banned_and_expired_returns_state_without_asset_request(self):
        self.created()
        for status in ("banned", "expired"):
            with self.subTest(status=status):
                calls = []
                def snapshot(path, credential):
                    calls.append(path)
                    return {"task_id": "task_fixture", "status": status}
                opener = FakeOpener(error=AssertionError("No download or regeneration expected"))
                result = api.download_glb(self.directory, SECRET, request_fn=snapshot, opener=opener)
                self.assertEqual(result["status"], status)
                self.assertEqual(calls, ["/tasks/task_fixture"])
                self.assertEqual(opener.calls, [])
                self.assertFalse((self.directory / "downloads").exists())

    def test_download_separate_unauthenticated_and_resume_by_hash(self):
        self.created()
        data = glb()
        opener = FakeOpener(data, headers={"Content-Length": str(len(data))})
        result = api.download_glb(self.directory, SECRET, request_fn=self.fake_api, opener=opener)
        self.assertTrue(result["downloaded"])
        request = opener.calls[0][0]
        self.assertIsNone(request.get_header("Authorization"))
        self.assertIsNone(request.get_header("Cookie"))
        result = api.download_glb(self.directory, SECRET, request_fn=self.fake_api,
                                  opener=FakeOpener(error=AssertionError("No second download expected")))
        self.assertTrue(result["reused"])
        manifest = (self.directory / "downloads.json").read_text()
        self.assertNotIn("signature", manifest)
        self.assertNotIn(SECRET, manifest)

    def test_download_corrupt_hash_requires_redownload_without_generation(self):
        self.created()
        data = glb()
        api.download_glb(self.directory, SECRET, request_fn=self.fake_api, opener=FakeOpener(data))
        (self.directory / "downloads/model.glb").write_bytes(b"bad file")
        self.calls.clear()
        result = api.download_glb(self.directory, SECRET, request_fn=self.fake_api, opener=FakeOpener(data))
        self.assertFalse(result["reused"])
        self.assertEqual([c[0] for c in self.calls], ["/tasks/task_fixture"])

    def test_invalid_or_truncated_glb_never_promoted(self):
        self.created()
        for data, headers in ((b"not a glb", {}), (glb()[:-1], {}),
                              (glb(), {"Content-Length": str(len(glb()) + 1)})):
            with self.subTest(size=len(data)), self.assertRaises(api.ToolError):
                api.download_glb(self.directory, SECRET, request_fn=self.fake_api,
                                 opener=FakeOpener(data, headers=headers))
            self.assertFalse((self.directory / "downloads/model.glb").exists())
            self.assertFalse(list((self.directory / "downloads").glob("*.part")))


class TransportTests(unittest.TestCase):
    def test_api_fixed_origin_multipart_and_envelope(self):
        opener = FakeOpener(json.dumps({"code": 0, "data": {"file_token": "file_fixture"}}).encode())
        result = api.api_request("/files", SECRET, upload=(PNG, "image/png"), opener=opener)
        self.assertEqual(result["file_token"], "file_fixture")
        request = opener.calls[0][0]
        self.assertEqual(request.full_url, api.API_BASE + "/files")
        self.assertEqual(request.get_header("Authorization"), "Bearer " + SECRET)
        self.assertIn(b'name="file"', request.data)
        self.assertIn(b'filename="reference.png"', request.data)
        self.assertEqual(len(opener.calls), 1)
        with self.assertRaises(api.ToolError):
            api.api_request("https://other.example/steal", SECRET, opener=opener)

    def test_api_redirect_rejected_without_forwarding_header(self):
        request = urllib.request.Request(api.API_BASE + "/account/balance", headers={"Authorization": "Bearer " + SECRET})
        self.assertIsNone(api.NoApiRedirect().redirect_request(request, None, 302, "", {}, "https://other.example/"))
        self.assertIsNone(api.NoApiRedirect().redirect_request(request, None, 302, "", {}, api.API_BASE + "/files"))

    def test_download_redirect_https_and_header_removal(self):
        request = urllib.request.Request("https://cdn.example/a.glb", headers={"Authorization": "Bearer " + SECRET,
                                                                              "Cookie": "private"})
        handler = api.HttpsAssetRedirect()
        redirected = handler.redirect_request(request, None, 302, "", {}, "https://other.example/a.glb")
        self.assertIsNone(redirected.get_header("Authorization"))
        self.assertIsNone(redirected.get_header("Cookie"))
        for url in ("http://other.example/a.glb", "https://name:password@other.example/a.glb", "https://host:8080/a"):
            with self.subTest(url=url), self.assertRaises(api.ToolError):
                handler.redirect_request(request, None, 302, "", {}, url)

    def test_timeout_no_transport_retry(self):
        opener = FakeOpener(error=TimeoutError("secret URL and " + SECRET))
        with self.assertRaises(api.ToolError) as error:
            api.api_request("/account/balance", SECRET, opener=opener)
        self.assertNotIn(SECRET, str(error.exception))
        self.assertEqual(len(opener.calls), 1)

    def test_http_error_and_server_body_never_exposed(self):
        body = io.BytesIO((SECRET + " https://cdn.example/private?signature=secret").encode())
        opener = FakeOpener(error=urllib.error.HTTPError(api.API_BASE, 504, SECRET, {}, body))
        with self.assertRaises(api.ApiError) as error:
            api.api_request("/account/balance", SECRET, opener=opener)
        self.assertEqual(str(error.exception), "Tripo request failed (HTTP 504)")
        self.assertNotIn(SECRET, str(error.exception))
        self.assertTrue(body.closed)

    def test_invalid_json_envelope_and_application_code(self):
        for data in (b"invalid", b"[]", b'{"code": true, "data": {}}', b'{"code": 0, "data": []}',
                     b'{"code": 1004, "message": "private", "data": {}}'):
            opener = FakeOpener(data)
            with self.subTest(data=data), self.assertRaises(api.ToolError):
                api.api_request("/account/balance", SECRET, opener=opener)
            self.assertEqual(len(opener.calls), 1)

    def test_redaction_nested_headers_urls_and_image_bytes(self):
        safe = api.redact({"nested": ["prefix " + SECRET,
                           {"Authorization": "Bearer " + SECRET, "file_token": "private-upload"}],
                           "output": "https://cdn.example/model.glb?signature=private", "image": "data:image/png;base64,abc"}, SECRET)
        text = json.dumps(safe)
        for hidden in (SECRET, "signature", "private-upload", "base64"):
            self.assertNotIn(hidden, text)

    def test_glb_invalid_version_and_chunk_bounds(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "fixture.glb"
            original = glb()
            for data in (original[:4] + struct.pack("<I", 1) + original[8:],
                         original[:12] + struct.pack("<I4s", 100000, b"JSON") + original[20:],
                         original[:16] + b"BIN\0" + original[20:]):
                path.write_bytes(data)
                with self.assertRaises(api.ToolError):
                    api.validate_glb(path)


if __name__ == "__main__":
    unittest.main(verbosity=2)
