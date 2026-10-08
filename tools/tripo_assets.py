#!/usr/bin/env python3
"""One-shot Tripo v3 reference uploads, generation, status and GLB download.

The plan command is offline. Every POST has its own durable state directory;
ambiguous/interrupted requests are never retried. Python standard library only.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import ssl
import struct
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import uuid


API_BASE = "https://openapi.tripo3d.ai/v3"
SECRET_FILE = Path.home() / ".codex" / "secrets" / "diablo-tripo.dpapi"
MODEL = "v3.1-20260211"
TEXTURE_VERSION = "v3.5-20260815"
VIEWS = ("front", "left", "back", "right")
TASK_STATUSES = {"queued", "running", "success", "failed", "cancelled", "banned", "expired"}
ENDPOINTS = {"single": "/generation/image-to-model", "multiview": "/generation/multiview-to-model"}
USER_AGENT = "Diablo3D-Tripo-local/1.0"
TIMEOUT = 60
MAX_IMAGE_BYTES = 20_000_000  # Conservative decimal interpretation of the documented 20 MB.
MAX_JSON_BYTES = 8 * 1024 * 1024
MAX_GLB_BYTES = 1024 * 1024 * 1024
IDENTIFIER = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,159}\Z")


class ToolError(Exception):
    """A bounded message safe for the terminal: no server body, URL or credential."""


class ApiError(ToolError):
    def __init__(self, http_status: int | None = None, api_code: int | None = None):
        self.http_status, self.api_code = http_status, api_code
        super().__init__("Tripo request failed" + (f" (HTTP {http_status})" if http_status else
                                                 f" (API code {api_code})"))


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def atomic_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                         prefix=".tripo-", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        raise ToolError("Missing or invalid local metadata") from None
    if not isinstance(value, dict):
        raise ToolError("Local metadata must be an object")
    return value


@contextmanager
def operation_lock(directory: Path):
    directory.mkdir(parents=True, exist_ok=True)
    lock_path = directory / ".operation.lock"
    try:
        stream = lock_path.open("x", encoding="utf-8")
    except FileExistsError:
        raise ToolError("Another or interrupted operation owns this directory; inspect its state") from None
    try:
        with stream:
            json.dump({"pid": os.getpid(), "startedAt": utc_now()}, stream)
            stream.flush()
            os.fsync(stream.fileno())
        yield
    finally:
        lock_path.unlink(missing_ok=True)


def valid_id(value: object) -> bool:
    return isinstance(value, str) and IDENTIFIER.fullmatch(value) is not None


def number(value: object, *, positive: bool = False) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) \
        and (value > 0 if positive else value >= 0)


def read_credential() -> str:
    credential = os.environ.get("TRIPO_API_KEY", "").strip()
    if not credential:
        if os.name != "nt":
            raise ToolError("Set TRIPO_API_KEY; the saved DPAPI credential requires Windows")
        try:
            protected = SECRET_FILE.read_text(encoding="utf-8-sig").strip()
            if not re.fullmatch(r"(?:[0-9a-fA-F]{2})+", protected):
                raise ValueError()
            encrypted = bytes.fromhex(protected)
        except (OSError, UnicodeError, ValueError):
            raise ToolError("No valid TRIPO_API_KEY or current-user DPAPI credential is available") from None

        class DataBlob(ctypes.Structure):
            _fields_ = [("length", wintypes.DWORD), ("data", ctypes.POINTER(ctypes.c_ubyte))]

        buffer = (ctypes.c_ubyte * len(encrypted)).from_buffer_copy(encrypted)
        source, destination = DataBlob(len(encrypted), buffer), DataBlob()
        crypt32 = ctypes.WinDLL("crypt32", use_last_error=True)
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        crypt32.CryptUnprotectData.argtypes = [ctypes.POINTER(DataBlob), ctypes.c_void_p, ctypes.c_void_p,
                                              ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD,
                                              ctypes.POINTER(DataBlob)]
        crypt32.CryptUnprotectData.restype = wintypes.BOOL
        kernel32.LocalFree.argtypes, kernel32.LocalFree.restype = [ctypes.c_void_p], ctypes.c_void_p
        if not crypt32.CryptUnprotectData(ctypes.byref(source), None, None, None, None, 1,
                                         ctypes.byref(destination)):
            raise ToolError("The DPAPI credential cannot be decrypted by this Windows user")
        try:
            credential = ctypes.string_at(destination.data, destination.length).decode("utf-16-le").strip()
        except UnicodeError:
            raise ToolError("The saved credential encoding is invalid") from None
        finally:
            if destination.data:
                ctypes.memset(destination.data, 0, destination.length)
                kernel32.LocalFree(destination.data)
    if not credential or any(c.isspace() or ord(c) < 32 for c in credential):
        raise ToolError("The Tripo credential is empty or contains invalid whitespace")
    return credential


def redact(value: object, credential: str = "") -> object:
    """Only used for local metadata; signed URLs and payload images are omitted entirely."""
    if isinstance(value, dict):
        return {key: "[redacted]" if str(key).lower() in {
            "authorization", "api_key", "tripo_api_key", "file_token", "filetoken"}
                else redact(item, credential) for key, item in value.items()}
    if isinstance(value, list):
        return [redact(item, credential) for item in value]
    if isinstance(value, str):
        value = value.replace(credential, "[redacted]") if credential else value
        value = re.sub(r"https?://[^\s\"<>]+", "[URL omitted]", value, flags=re.I)
        return "[image data omitted]" if value.startswith("data:image/") else value
    return value


class NoApiRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        return None  # Authorization must never leave the fixed API origin, even on a same-origin redirect.


def require_https(url: str) -> None:
    try:
        parsed = urllib.parse.urlsplit(url)
        valid = parsed.scheme == "https" and parsed.hostname and not parsed.username and not parsed.password \
            and not parsed.fragment and parsed.port in (None, 443)
    except ValueError:
        valid = False
    if not valid or any(ord(c) <= 32 for c in url):
        raise ToolError("The model URL must be ordinary HTTPS without credentials")


class HttpsAssetRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        require_https(new_url)
        # Defense in depth: the download opener starts unauthenticated; strip headers if reused incorrectly.
        redirected = super().redirect_request(request, response, code, message, headers, new_url)
        if redirected:
            redirected.remove_header("Authorization")
            redirected.remove_header("Cookie")
        return redirected


def api_request(path: str, credential: str, payload: dict | None = None, *,
                upload: tuple[bytes, str] | None = None, opener=None) -> dict:
    allowed = path in {"/files", "/account/balance", *ENDPOINTS.values()} or \
        re.fullmatch(r"/tasks/[A-Za-z0-9][A-Za-z0-9_.-]{0,159}", path)
    if not allowed or (payload is not None and upload is not None):
        raise ToolError("Unsupported Tripo v3 endpoint or body")
    headers = {"Authorization": "Bearer " + credential, "User-Agent": USER_AGENT}
    data = None
    if upload is not None:
        image, mime = upload
        boundary = "tripo" + uuid.uuid4().hex
        suffix = "png" if mime == "image/png" else "jpg"
        # Generic filename avoids including a private local path in the request.
        data = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; "
                f"filename=\"reference.{suffix}\"\r\nContent-Type: {mime}\r\n\r\n").encode("ascii") \
            + image + f"\r\n--{boundary}--\r\n".encode("ascii")
        headers["Content-Type"] = "multipart/form-data; boundary=" + boundary
    elif payload is not None:
        data = json.dumps(payload, allow_nan=False).encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(API_BASE + path, data=data, headers=headers,
                                     method="GET" if data is None else "POST")
    opener = opener or urllib.request.build_opener(NoApiRedirect(),
        urllib.request.HTTPSHandler(context=ssl.create_default_context()))
    try:
        with opener.open(request, timeout=TIMEOUT) as response:
            raw = response.read(MAX_JSON_BYTES + 1)
        if len(raw) > MAX_JSON_BYTES:
            raise ToolError("Tripo response exceeds the local metadata limit")
    except urllib.error.HTTPError as error:
        error.close()
        raise ApiError(http_status=error.code) from None
    except (urllib.error.URLError, TimeoutError, OSError):
        raise ToolError("Tripo transport failed; no request retry was made") from None
    try:
        envelope = json.loads(raw)
    except (UnicodeError, ValueError):
        raise ToolError("Tripo returned unreadable metadata; no retry was made") from None
    if not isinstance(envelope, dict) or type(envelope.get("code")) is not int:
        raise ToolError("Tripo returned an unexpected envelope")
    if envelope["code"] != 0:
        raise ApiError(api_code=envelope["code"])
    if not isinstance(envelope.get("data"), dict):
        raise ToolError("Tripo returned no metadata object")
    return envelope["data"]


def image_input(path: Path) -> tuple[bytes, dict]:
    try:
        with path.open("rb") as stream:
            data = stream.read(MAX_IMAGE_BYTES + 1)
    except OSError:
        raise ToolError("A reference image cannot be read") from None
    mime = "image/png" if data.startswith(b"\x89PNG\r\n\x1a\n") else \
        "image/jpeg" if data.startswith(b"\xff\xd8\xff") else None
    if mime is None or len(data) > MAX_IMAGE_BYTES:
        raise ToolError("Uploads require PNG/JPEG up to 20 MB")
    return data, {"path": str(path.resolve()), "bytes": len(data),
                  "sha256": hashlib.sha256(data).hexdigest(), "mimeType": mime}


def plan_task(directory: Path, identity: dict, images: dict[str, Path], *, texture: bool = True,
              texture_quality: str = "standard", face_limit: int = 6000,
              estimated_credits: float | None = None) -> dict:
    if any(not valid_id(identity.get(key)) for key in ("assetId", "variant", "revision")):
        raise ToolError("Asset ID, variant and revision must be explicit safe identifiers")
    labels = set(images)
    if labels == {"image"}:
        mode = "single"
    elif "front" in labels and 2 <= len(labels) <= 4 and labels <= set(VIEWS):
        mode = "multiview"
    else:
        raise ToolError("Use one image, or labeled front/left/back/right views with front and at least 2 views")
    if type(texture) is not bool or texture_quality not in {"standard", "detailed", "fast", "extreme"} \
            or type(face_limit) is not int or not 500 <= face_limit <= 1_500_000:
        raise ToolError("Unsupported generation options")
    if estimated_credits is not None and not number(estimated_credits, positive=True):
        raise ToolError("Manual estimated credits must be a positive finite number")
    inputs = []
    for label in (("image",) if mode == "single" else VIEWS):
        if label in images:
            _, metadata = image_input(images[label])
            inputs.append({"view": label, **metadata})
    if len({item["sha256"] for item in inputs}) != len(inputs):
        raise ToolError("Distinct views must not reuse identical image bytes")
    parameters = {"model": MODEL, "texture": texture, "pbr": texture,
                  "geometry_quality": "standard", "face_limit": face_limit,
                  "auto_size": False, "quad": False, "export_uv": True}
    if texture:
        parameters.update({"texture_version": TEXTURE_VERSION, "texture_quality": texture_quality,
                           "texture_alignment": "original_image", "delight": True})
    if mode == "single":
        parameters["enable_image_autofix"] = False
    plan = {"schemaVersion": 1, "provider": "tripo", "apiVersion": "v3", **identity,
            "preparedAt": utc_now(), "mode": mode, "endpoint": ENDPOINTS[mode],
            "inputs": inputs, "parameters": parameters,
            "cost": {"estimatedCredits": estimated_credits, "source": "manual; verify current pricing",
                     "actualCredits": None}}
    plan["inputHash"] = hashlib.sha256(json.dumps(
        [{"view": i["view"], "sha256": i["sha256"]} for i in inputs],
        sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    with operation_lock(directory):
        if (directory / "plan.json").exists() or (directory / "create-state.json").exists() \
                or (directory / "task.json").exists() or (directory / "uploads").exists():
            raise ToolError("This directory already owns a plan or POST; use its recorded state")
        atomic_json(directory / "plan.json", plan)
    return {**plan, "outDir": str(directory), "status": "PLANNED", "networkRequests": 0}


def load_plan(directory: Path) -> dict:
    plan = read_json(directory / "plan.json")
    parameters = plan.get("parameters")
    if plan.get("schemaVersion") != 1 or plan.get("provider") != "tripo" or plan.get("apiVersion") != "v3" \
            or plan.get("mode") not in ENDPOINTS or plan.get("endpoint") != ENDPOINTS[plan["mode"]] \
            or not isinstance(parameters, dict) or parameters.get("model") != MODEL:
        raise ToolError("Saved plan does not match the supported Tripo v3 contract")
    if any(not valid_id(plan.get(key)) for key in ("assetId", "variant", "revision")) \
            or not isinstance(plan.get("inputs"), list) or not isinstance(plan.get("cost"), dict):
        raise ToolError("Saved plan identity or inputs are invalid")
    expected_labels = {"image"} if plan["mode"] == "single" else set(VIEWS)
    labels = [item.get("view") for item in plan["inputs"] if isinstance(item, dict)]
    if len(labels) != len(plan["inputs"]) or len(set(labels)) != len(labels) \
            or not set(labels) <= expected_labels or \
            (labels != ["image"] if plan["mode"] == "single" else "front" not in labels or len(labels) < 2):
        raise ToolError("Saved plan has invalid view labels")
    allowed = {"model", "texture", "pbr", "geometry_quality", "face_limit", "auto_size", "quad", "export_uv"}
    if parameters.get("texture") is True:
        allowed.update({"texture_version", "texture_quality", "texture_alignment", "delight"})
    if plan["mode"] == "single":
        allowed.add("enable_image_autofix")
    if set(parameters) != allowed or parameters.get("geometry_quality") != "standard" \
            or type(parameters.get("face_limit")) is not int or not 500 <= parameters["face_limit"] <= 1_500_000 \
            or type(parameters.get("texture")) is not bool or parameters.get("pbr") is not parameters["texture"] \
            or parameters.get("quad") is not False or parameters.get("auto_size") is not False \
            or parameters.get("export_uv") is not True \
            or (plan["mode"] == "single" and parameters.get("enable_image_autofix") is not False):
        raise ToolError("Saved options are incompatible with this bounded GLB workflow")
    if parameters["texture"]:
        if parameters.get("texture_version") != TEXTURE_VERSION or parameters.get("texture_quality") not in {
                "standard", "detailed", "fast", "extreme"} or type(parameters.get("delight")) is not bool \
                or parameters.get("texture_alignment") != "original_image":
            raise ToolError("Saved texture options do not match the supported texture version")
    elif any(key in parameters for key in ("texture_version", "texture_quality", "delight")):
        raise ToolError("Geometry-only plans must not contain texturing options")
    expected_hash = hashlib.sha256(json.dumps(
        [{"view": i["view"], "sha256": i.get("sha256")} for i in plan["inputs"]],
        sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    if plan.get("inputHash") != expected_hash:
        raise ToolError("Saved input identity hash does not match the views")
    return plan


def verified_image(item: dict) -> bytes:
    data, actual = image_input(Path(item["path"]))
    if any(actual[key] != item.get(key) for key in ("bytes", "sha256", "mimeType")):
        raise ToolError("Reference image changed since the offline plan")
    return data


def post_once(directory: Path, request_fn, *, metadata: dict, id_key: str, credential: str) -> dict:
    """Caller holds the asset operation lock. This directory owns exactly one POST attempt."""
    state_path = directory / "post-state.json"
    directory.mkdir(parents=True, exist_ok=True)
    if state_path.exists():
        raise ToolError("This POST already has a durable state; uncertain submissions cannot be retried")
    atomic_json(directory / "request.json", metadata)
    atomic_json(state_path, {"state": "SUBMITTING", "startedAt": utc_now()})
    try:
        result = request_fn()
        identity = result.get(id_key)
        if not valid_id(identity) or credential in identity:
            raise ToolError("The response has no valid identity; reconcile the original request")
        receipt = {"state": "CREATED", id_key: identity, "finishedAt": utc_now()}
        atomic_json(state_path, receipt)
        return receipt
    except BaseException as error:
        # Even a rejected attempt is not silently submitted a second time. 5xx/timeout/parse errors are UNKNOWN.
        rejected = isinstance(error, ApiError) and error.http_status in {400, 401, 402, 403, 404, 405, 409, 413, 422, 429}
        failure = {"state": "REJECTED" if rejected else "UNKNOWN", "finishedAt": utc_now()}
        if isinstance(error, ApiError):
            failure.update({"httpStatus": error.http_status, "apiCode": error.api_code})
        atomic_json(state_path, failure)
        raise


def upload_inputs(directory: Path, credential: str, *, request_fn=api_request) -> dict:
    with operation_lock(directory):
        plan = load_plan(directory)
        if (directory / "create-state.json").exists() or (directory / "task.json").exists():
            raise ToolError("Generation already has a state; use status/download without uploading again")
        # Validate every image before starting any upload.
        images = [(item, verified_image(item)) for item in plan["inputs"]]
        for item, image in images:
            upload_dir = directory / "uploads" / item["view"]
            state_path = upload_dir / "post-state.json"
            if state_path.exists():
                state = read_json(state_path)
                if state.get("state") == "CREATED" and valid_id(state.get("file_token")) \
                        and credential not in state["file_token"]:
                    saved = read_json(upload_dir / "request.json")
                    if saved.get("inputSha256") != item["sha256"]:
                        raise ToolError("Saved upload belongs to different reference bytes")
                    continue
                raise ToolError("An upload outcome needs reconciliation; no upload retry was made")
            post_once(upload_dir, lambda image=image, item=item:
                      request_fn("/files", credential, upload=(image, item["mimeType"])),
                      metadata={"provider": "tripo", "apiVersion": "v3", "endpoint": "/files",
                                "view": item["view"], "inputSha256": item["sha256"], "bytes": len(image)},
                      id_key="file_token", credential=credential)
    return {"status": "UPLOADED", "uploadedViews": len(images), "outDir": str(directory)}


def create_task(directory: Path, credential: str, max_credits: float, *, request_fn=api_request) -> dict:
    with operation_lock(directory):
        plan = load_plan(directory)
        if (directory / "create-state.json").exists() or (directory / "task.json").exists() \
                or (directory / "generation" / "post-state.json").exists():
            raise ToolError("This directory already owns a generation attempt; inspect status before another submission")
        estimate = plan["cost"].get("estimatedCredits")
        if not number(estimate, positive=True) or not number(max_credits, positive=True) or max_credits < estimate:
            raise ToolError("Create requires a manual positive estimate and a sufficient explicit credit ceiling")
        tokens = {}
        for item in plan["inputs"]:
            verified_image(item)
            upload_dir = directory / "uploads" / item["view"]
            state = read_json(upload_dir / "post-state.json")
            saved = read_json(upload_dir / "request.json")
            token = state.get("file_token")
            if state.get("state") != "CREATED" or not valid_id(token) or credential in token \
                    or saved.get("inputSha256") != item["sha256"]:
                raise ToolError("Every input needs a successful upload matching its planned hash")
            tokens[item["view"]] = token
        balance = request_fn("/account/balance", credential)
        available = balance.get("balance")
        if not number(available) or available < estimate:
            raise ToolError("The current Tripo available balance is below the manual request estimate")
        payload = dict(plan["parameters"])
        if plan["mode"] == "single":
            payload["input"] = tokens["image"]
        else:
            payload["inputs"] = [{view: tokens[view]} for view in VIEWS if view in tokens]
        metadata = {"provider": "tripo", "apiVersion": "v3", "assetId": plan["assetId"],
                    "variant": plan["variant"], "revision": plan["revision"], "inputHash": plan["inputHash"],
                    "endpoint": plan["endpoint"], "parameters": plan["parameters"],
                    "estimatedCredits": estimate, "creditCeiling": max_credits, "balanceBefore": available}
        atomic_json(directory / "create-state.json", {"state": "SUBMITTING", **metadata, "startedAt": utc_now()})
        try:
            receipt = post_once(directory / "generation", lambda:
                request_fn(plan["endpoint"], credential, payload), metadata=metadata,
                id_key="task_id", credential=credential)
            task = {**metadata, "taskId": receipt["task_id"], "status": "SUBMITTED", "createdAt": utc_now()}
            atomic_json(directory / "task.json", task)
            atomic_json(directory / "create-state.json", {"state": "CREATED", "taskId": task["taskId"]})
        except BaseException:
            state = read_json(directory / "generation" / "post-state.json") \
                if (directory / "generation" / "post-state.json").exists() else {"state": "UNKNOWN"}
            atomic_json(directory / "create-state.json", state)
            raise
    return {"taskId": task["taskId"], "status": "SUBMITTED", "estimatedCredits": estimate,
            "balanceBefore": available, "outDir": str(directory)}


def task_summary(task: dict) -> dict:
    summary = {key: task[key] for key in ("task_id", "status", "progress", "credits_consumed", "error_code")
               if key in task and (valid_id(task[key]) if key in {"task_id", "status"} else number(task[key]))}
    return summary


def get_status(directory: Path, credential: str, *, request_fn=api_request) -> dict:
    plan, saved = load_plan(directory), read_json(directory / "task.json")
    identity = saved.get("taskId")
    if not valid_id(identity) or saved.get("inputHash") != plan["inputHash"] \
            or any(saved.get(key) != plan[key] for key in ("provider", "apiVersion", "assetId", "variant", "revision", "endpoint")):
        raise ToolError("Saved task identity does not match the plan")
    result = request_fn("/tasks/" + identity, credential)
    if result.get("task_id") != identity or result.get("status") not in TASK_STATUSES:
        raise ToolError("Tripo returned an unexpected task identity or status")
    safe = task_summary(result)
    if credential and credential in json.dumps(safe):
        raise ToolError("Tripo returned invalid task metadata")
    atomic_json(directory / "result.json", {**safe, "checkedAt": utc_now(), "outputUrls": "omitted; refresh on download"})
    if number(result.get("credits_consumed")):
        saved["actualCredits"] = result["credits_consumed"]
    saved["status"] = result["status"]
    atomic_json(directory / "task.json", saved)
    return result


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_glb(path: Path) -> None:
    """Validate the GLB container, JSON asset version and embedded binary chunk bounds."""
    size = path.stat().st_size
    if not 20 <= size <= MAX_GLB_BYTES:
        raise ToolError("Downloaded GLB has an invalid size")
    with path.open("rb") as stream:
        magic, version, declared_size = struct.unpack("<4sII", stream.read(12))
        if magic != b"glTF" or version != 2 or declared_size != size:
            raise ToolError("Downloaded file is not a complete GLB version 2 container")
        first = True
        while stream.tell() < size:
            header = stream.read(8)
            if len(header) != 8:
                raise ToolError("GLB has a truncated chunk header")
            length, kind = struct.unpack("<I4s", header)
            if length % 4 or length > size - stream.tell() or (first and (kind != b"JSON" or length > MAX_JSON_BYTES)):
                raise ToolError("GLB has invalid chunk bounds")
            if first:
                try:
                    document = json.loads(stream.read(length).decode("utf-8"))
                except (UnicodeError, ValueError):
                    raise ToolError("GLB JSON chunk is invalid") from None
                if not isinstance(document, dict) or not isinstance(document.get("asset"), dict) \
                        or document["asset"].get("version") != "2.0":
                    raise ToolError("GLB JSON does not declare glTF 2.0")
            else:
                stream.seek(length, 1)
            first = False


def download_glb(directory: Path, credential: str, *, request_fn=api_request, opener=None) -> dict:
    with operation_lock(directory):
        result = get_status(directory, credential, request_fn=request_fn)
        summary = task_summary(result)
        if result["status"] != "success":
            return summary
        destination = directory / "downloads"
        destination.mkdir(exist_ok=True)
        target, manifest_path = destination / "model.glb", directory / "downloads.json"
        if manifest_path.exists():
            old = read_json(manifest_path)
            if old.get("taskId") != result["task_id"]:
                raise ToolError("The download manifest belongs to another task")
            if target.is_file() and target.stat().st_size == old.get("bytes") \
                    and sha256_file(target) == old.get("sha256"):
                validate_glb(target)
                return {**summary, "downloaded": True, "reused": True, "path": str(target)}
        output = result.get("output")
        url = output.get("model_url") if isinstance(output, dict) else None
        if not isinstance(url, str):
            raise ToolError("The completed task has no primary GLB model URL")
        require_https(url)
        opener = opener or urllib.request.build_opener(HttpsAssetRedirect(),
            urllib.request.HTTPSHandler(context=ssl.create_default_context()))
        temporary = None
        try:
            # This request and opener are separate from the API. Never add Bearer or cookies.
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT}, method="GET")
            with opener.open(request, timeout=TIMEOUT) as response, tempfile.NamedTemporaryFile(
                    mode="wb", dir=destination, prefix=".download-", suffix=".part", delete=False) as stream:
                temporary = Path(stream.name)
                length = response.headers.get("Content-Length")
                if length is not None and (not length.isdigit() or int(length) > MAX_GLB_BYTES):
                    raise ToolError("GLB Content-Length is invalid or exceeds the local 1 GiB limit")
                digest, size = hashlib.sha256(), 0
                for block in iter(lambda: response.read(1024 * 1024), b""):
                    size += len(block)
                    if size > MAX_GLB_BYTES:
                        raise ToolError("GLB exceeds the local 1 GiB download limit")
                    stream.write(block)
                    digest.update(block)
                if length is not None and size != int(length):
                    raise ToolError("GLB download is truncated")
                stream.flush()
                os.fsync(stream.fileno())
            validate_glb(temporary)
            os.replace(temporary, target)
            atomic_json(manifest_path, {"provider": "tripo", "apiVersion": "v3", "taskId": result["task_id"],
                        "filename": "model.glb", "bytes": size, "sha256": digest.hexdigest(),
                        "downloadedAt": utc_now(), "status": "DOWNLOADED"})
        except (urllib.error.URLError, TimeoutError, OSError):
            raise ToolError("GLB download failed; refresh its task status and resume without generating again") from None
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    return {**summary, "downloaded": True, "reused": False, "path": str(target), "sha256": digest.hexdigest()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    plan = commands.add_parser("plan", help="Prepare a dry-run plan offline; no credential or request")
    for key in ("asset-id", "variant", "revision"):
        plan.add_argument("--" + key, required=True)
    for label in ("image", *VIEWS):
        plan.add_argument("--" + label, type=Path)
    plan.add_argument("--geometry-only", action="store_true")
    plan.add_argument("--texture-quality", choices=("standard", "detailed", "fast", "extreme"), default="standard")
    plan.add_argument("--face-limit", type=int, default=6000)
    plan.add_argument("--estimated-credits", type=float, help="Manual current-price estimate; required before create")
    commands.add_parser("balance", help="Read available and frozen API credits; no POST")
    commands.add_parser("upload", help="Upload planned images; resume only recorded successful uploads")
    create = commands.add_parser("create", help="Submit exactly one paid generation after uploads and current balance check")
    create.add_argument("--max-credits", type=float, required=True,
                        help="Local estimate ceiling; the API has no verified server-side price cap")
    commands.add_parser("status", help="Read one saved task snapshot; no polling loop")
    commands.add_parser("download", help="Refresh status and download/resume the primary GLB without API authentication")
    for name, command in commands.choices.items():
        if name != "balance":
            command.add_argument("--out-dir", required=True, type=Path)
    args = parser.parse_args()
    try:
        if args.command == "plan":
            result = plan_task(args.out_dir.resolve(),
                {"assetId": args.asset_id, "variant": args.variant, "revision": args.revision},
                {label: getattr(args, label).resolve() for label in ("image", *VIEWS) if getattr(args, label)},
                texture=not args.geometry_only, texture_quality=args.texture_quality,
                face_limit=args.face_limit, estimated_credits=args.estimated_credits)
        else:
            credential = read_credential()
            if args.command == "balance":
                balance = api_request("/account/balance", credential)
                if not number(balance.get("balance")) or not number(balance.get("frozen")):
                    raise ToolError("Tripo returned invalid account balances")
                result = {key: balance[key] for key in ("balance", "frozen")}
            elif args.command == "upload":
                result = upload_inputs(args.out_dir.resolve(), credential)
            elif args.command == "create":
                result = create_task(args.out_dir.resolve(), credential, args.max_credits)
            elif args.command == "status":
                with operation_lock(args.out_dir.resolve()):
                    result = task_summary(get_status(args.out_dir.resolve(), credential))
            else:
                result = download_glb(args.out_dir.resolve(), credential)
            result = redact(result, credential)
        print(json.dumps(result, ensure_ascii=False, allow_nan=False))
        return 0
    except ToolError as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=False), file=sys.stderr)
    except (OSError, ValueError, KeyError, TypeError, struct.error):
        print(json.dumps({"error": "Local metadata, file or response format is invalid; no retry was made"}), file=sys.stderr)
    except Exception:
        print(json.dumps({"error": "Operation failed; inspect the durable POST state before any new submission"}), file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
