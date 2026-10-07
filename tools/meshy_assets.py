#!/usr/bin/env python3
"""One-shot Meshy asset requests and local downloads; Python standard library only.

Credentials come from MESHY_API_KEY or a current-user Windows DPAPI file.
No credential, authenticated headers, or embedded image data are written to logs.
Each output directory owns one generation. An uncertain POST is never retried.
"""

from __future__ import annotations

import argparse
import base64
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import ssl
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request


API_BASE = "https://api.meshy.ai"
TASK_ENDPOINT = "/openapi/v1/image-to-3d"
SECRET_FILE = Path.home() / ".codex" / "secrets" / "diablo-meshy.dpapi"
ESTIMATED_CREDITS = 30
TIMEOUT_SECONDS = 60
USER_AGENT = "Diablo3D-local-asset-tool/1.0"
PARAMETERS = {
    "ai_model": "meshy-7.1",
    "model_type": "standard",
    "geometry_resolution": "standard",
    "image_enhancement": False,
    "should_texture": True,
    "texture_resolution": "4k",
    "enable_pbr": False,
    "should_remesh": True,
    "target_polycount": 3000,
    "topology": "triangle",
    "save_pre_remeshed_model": True,
    "target_formats": ["glb", "obj"],
    "multi_view_thumbnails": True,
    "alpha_thumbnail": True,
    "auto_size": False,
}


class ToolError(Exception):
    """A bounded, credential-free message that is safe to print."""


class ApiError(ToolError):
    def __init__(self, code: int, retry_after: str | None = None):
        self.code = code
        self.retry_after = retry_after
        labels = {
            400: "invalid Meshy request",
            401: "Meshy credential was rejected",
            402: "insufficient Meshy credits",
            403: "Meshy access or model entitlement was denied",
            404: "Meshy task or endpoint was not found",
            409: "Meshy task state conflicts with this operation",
            422: "Meshy could not validate the request",
            429: "Meshy rate or queue limit was reached",
        }
        super().__init__(f"HTTP {code}: {labels.get(code, 'Meshy request failed')}")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def atomic_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent,
            prefix=".meshy-", suffix=".tmp", delete=False,
        ) as stream:
            temporary = Path(stream.name)
            json.dump(value, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def read_json(path: Path) -> dict:
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        raise ToolError(f"Cannot read valid metadata: {path.name}") from None
    if not isinstance(result, dict):
        raise ToolError(f"Invalid metadata object: {path.name}")
    return result


def read_credential() -> str:
    credential = os.environ.get("MESHY_API_KEY", "").strip()
    if credential:
        if any(character.isspace() for character in credential):
            raise ToolError("MESHY_API_KEY contains unexpected whitespace")
        return credential
    if os.name != "nt":
        raise ToolError("Set MESHY_API_KEY; the saved DPAPI credential requires Windows")
    try:
        protected = SECRET_FILE.read_bytes()
    except OSError:
        raise ToolError("No MESHY_API_KEY or saved DPAPI credential is available") from None
    # PowerShell ConvertFrom-SecureString writes a hexadecimal DPAPI blob.
    # Raw and Base64 blobs are also accepted for equivalent local secret stores.
    try:
        encoding = "utf-16" if protected.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
        text = protected.decode(encoding).strip()
        if re.fullmatch(r"[0-9a-fA-F]+", text) and len(text) % 2 == 0:
            protected = bytes.fromhex(text)
        elif re.fullmatch(r"[A-Za-z0-9+/=\s]+", text):
            protected = base64.b64decode(text, validate=True)
    except (UnicodeError, ValueError):
        pass

    class DataBlob(ctypes.Structure):
        _fields_ = [("length", wintypes.DWORD), ("data", ctypes.POINTER(ctypes.c_ubyte))]

    buffer = (ctypes.c_ubyte * len(protected)).from_buffer_copy(protected)
    source = DataBlob(len(protected), buffer)
    destination = DataBlob()
    crypt32 = ctypes.WinDLL("crypt32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    decrypt = crypt32.CryptUnprotectData
    decrypt.argtypes = [ctypes.POINTER(DataBlob), ctypes.c_void_p, ctypes.c_void_p,
                        ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(DataBlob)]
    decrypt.restype = wintypes.BOOL
    kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    kernel32.LocalFree.restype = ctypes.c_void_p
    if not decrypt(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(destination)):
        raise ToolError("The saved DPAPI credential cannot be decrypted by this Windows user")
    try:
        plaintext = ctypes.string_at(destination.data, destination.length)
        # ConvertFrom-SecureString protects UTF-16LE, while other stores use UTF-8.
        if len(plaintext) % 2 == 0 and b"\x00" in plaintext:
            credential = plaintext.decode("utf-16-le").strip()
        else:
            credential = plaintext.decode("utf-8").strip()
    except UnicodeError:
        raise ToolError("The decrypted credential has an unsupported encoding") from None
    finally:
        if destination.data:
            ctypes.memset(destination.data, 0, destination.length)
            kernel32.LocalFree(destination.data)
    if not credential or any(character.isspace() for character in credential):
        raise ToolError("The saved credential is empty or invalid")
    return credential


def redact(value: object, credential: str) -> object:
    if isinstance(value, str):
        if value.startswith("data:image/"):
            return "[embedded image data omitted]"
        return value.replace(credential, "[redacted]")
    if isinstance(value, list):
        return [redact(item, credential) for item in value]
    if isinstance(value, dict):
        return {
            key: "[redacted]" if str(key).lower() in {"authorization", "api_key", "meshy_api_key"}
            else redact(item, credential)
            for key, item in value.items()
        }
    return value


class NoApiRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        # Never forward the authenticated header to another URL or host.
        return None


class HttpsAssetRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        require_https(new_url)
        return super().redirect_request(request, response, code, message, headers, new_url)


def require_https(url: str) -> None:
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password:
        raise ToolError("An asset URL is not an ordinary HTTPS URL")


def api_request(path: str, credential: str, payload: dict | None = None) -> tuple[dict, str | None]:
    body = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        API_BASE + path, data=body,
        headers={"Authorization": "Bearer " + credential,
                 "Content-Type": "application/json", "User-Agent": USER_AGENT},
        method="GET" if payload is None else "POST",
    )
    opener = urllib.request.build_opener(
        NoApiRedirect(), urllib.request.HTTPSHandler(context=ssl.create_default_context()))
    try:
        with opener.open(request, timeout=TIMEOUT_SECONDS) as response:
            data = response.read(8 * 1024 * 1024)
            retry_after = response.headers.get("Retry-After")
    except urllib.error.HTTPError as error:
        # Server error bodies can echo input. Print only the status category.
        retry_after = error.headers.get("Retry-After") if error.headers else None
        error.close()
        raise ApiError(error.code, retry_after) from None
    except (urllib.error.URLError, TimeoutError, OSError):
        raise ToolError("Meshy transport failed; no automatic request retry was made") from None
    try:
        result = json.loads(data)
    except (UnicodeError, ValueError):
        raise ToolError("Meshy returned an unreadable response; no retry was made") from None
    if not isinstance(result, dict):
        raise ToolError("Meshy returned an unexpected response object")
    return redact(result, credential), retry_after


def output(value: dict) -> None:
    print(json.dumps(value, ensure_ascii=False))


def task_id(out_dir: Path) -> str:
    task = read_json(out_dir / "task.json")
    identity = task.get("taskId") or task.get("id")
    if not isinstance(identity, str) or not re.fullmatch(r"[A-Za-z0-9_-]+", identity):
        raise ToolError("task.json contains no valid task ID")
    return identity


def result_summary(result: dict, retry_after: str | None = None) -> dict:
    summary = {
        "taskId": result.get("id"), "status": result.get("status"),
        "progress": result.get("progress"), "consumedCredits": result.get("consumed_credits"),
        "estimatedCredits": ESTIMATED_CREDITS,
    }
    if retry_after is not None:
        summary["retryAfterSeconds"] = retry_after
    if result.get("task_error"):
        error = result["task_error"]
        summary["taskError"] = {key: error.get(key) for key in ("type", "code") if error.get(key)}
    return summary


def get_status(out_dir: Path, credential: str) -> tuple[dict, str | None]:
    identity = task_id(out_dir)
    result, retry_after = api_request(TASK_ENDPOINT + "/" + identity, credential)
    if result.get("id") != identity:
        raise ToolError("The status response did not match the saved task ID")
    atomic_json(out_dir / "result.json", result)
    return result, retry_after


def create_task(image_path: Path, out_dir: Path, credential: str) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    if (out_dir / "task.json").exists():
        raise ToolError("This output directory already owns a task; use status or download")
    state_path = out_dir / "create-state.json"
    if state_path.exists() and read_json(state_path).get("state") in {"SUBMITTING", "UNKNOWN", "CREATED"}:
        raise ToolError("A previous create may have succeeded; check its task before creating again")
    lock_path = out_dir / ".create.lock"
    try:
        lock = lock_path.open("x", encoding="utf-8")
    except FileExistsError:
        raise ToolError("Another or interrupted create owns this directory; inspect its metadata first") from None
    try:
        with lock:
            lock.write(utc_now() + "\n")
        # Recheck after obtaining the lock: another process may have completed
        # between the first check and this exclusive file creation.
        if (out_dir / "task.json").exists():
            raise ToolError("This output directory already owns a task; use status or download")
        if state_path.exists() and read_json(state_path).get("state") in {"SUBMITTING", "UNKNOWN", "CREATED"}:
            raise ToolError("A previous create may have succeeded; check its task before creating again")
        if not image_path.is_file():
            raise ToolError("The requested input image does not exist")
        image = image_path.read_bytes()
        if image.startswith(b"\x89PNG\r\n\x1a\n"):
            mime = "image/png"
        elif image.startswith(b"\xff\xd8\xff"):
            mime = "image/jpeg"
        else:
            raise ToolError("The input must be a real PNG or JPEG image")
        if not image or len(image) > 100 * 1024 * 1024:
            raise ToolError("The input image is empty or exceeds 100 MiB")
        request_metadata = {
            "schemaVersion": 1, "preparedAt": utc_now(), "endpoint": API_BASE + TASK_ENDPOINT,
            "estimatedCredits": ESTIMATED_CREDITS,
            "inputImage": {"path": str(image_path), "bytes": len(image),
                           "sha256": hashlib.sha256(image).hexdigest(), "mimeType": mime},
            "parameters": PARAMETERS,
            "imageTransport": "base64 data URI; omitted from metadata",
        }
        atomic_json(out_dir / "request.json", request_metadata)
        balance, _ = api_request("/openapi/v1/balance", credential)
        available = balance.get("balance")
        if not isinstance(available, (int, float)):
            raise ToolError("Meshy did not return a numeric credit balance")
        if available < ESTIMATED_CREDITS:
            raise ToolError(f"Balance {available} is below the expected {ESTIMATED_CREDITS} credits")
        payload = dict(PARAMETERS)
        payload["image_url"] = "data:" + mime + ";base64," + base64.b64encode(image).decode("ascii")
        atomic_json(state_path, {"state": "SUBMITTING", "submittedAt": utc_now(),
                                "balanceBefore": available, "estimatedCredits": ESTIMATED_CREDITS})
        try:
            response, _ = api_request(TASK_ENDPOINT, credential, payload)
        except ApiError as error:
            state = "REJECTED" if error.code in {400, 401, 402, 403, 404, 409, 422, 429} else "UNKNOWN"
            atomic_json(state_path, {"state": state, "finishedAt": utc_now(),
                                    "httpStatus": error.code, "estimatedCredits": ESTIMATED_CREDITS})
            raise
        except ToolError:
            atomic_json(state_path, {"state": "UNKNOWN", "finishedAt": utc_now(),
                                    "estimatedCredits": ESTIMATED_CREDITS})
            raise ToolError("Create outcome is uncertain; no retry was made. Check the Meshy task list") from None
        identity = response.get("result")
        if not isinstance(identity, str) or not re.fullmatch(r"[A-Za-z0-9_-]+", identity):
            atomic_json(state_path, {"state": "UNKNOWN", "finishedAt": utc_now()})
            raise ToolError("Create returned no valid task ID; inspect the Meshy task list before retrying")
        task = {"taskId": identity, "id": identity, "status": "SUBMITTED", "createdAt": utc_now(),
                "endpoint": TASK_ENDPOINT, "estimatedCredits": ESTIMATED_CREDITS,
                "balanceBefore": available, "createResponse": response}
        atomic_json(out_dir / "task.json", task)
        atomic_json(state_path, {"state": "CREATED", "taskId": identity, "finishedAt": utc_now()})
        output({"taskId": identity, "status": "SUBMITTED", "estimatedCredits": ESTIMATED_CREDITS,
                "balanceBefore": available, "outDir": str(out_dir)})
    finally:
        lock_path.unlink(missing_ok=True)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def result_assets(result: dict) -> list[tuple[str, str]]:
    assets = []
    for format_name, url in result.get("model_urls", {}).items():
        if isinstance(url, str) and url:
            assets.append(("model." + format_name, url))
    for index, maps in enumerate(result.get("texture_urls", [])):
        for map_name, url in maps.items():
            if isinstance(url, str) and url:
                assets.append((f"texture.{index}.{map_name}", url))
    for key in ("thumbnail_url", "alpha_thumbnail_url"):
        url = result.get(key)
        if isinstance(url, str) and url:
            assets.append((key, url))
    for angle, url in result.get("thumbnail_urls", {}).items():
        if isinstance(url, str) and url:
            assets.append(("thumbnail." + angle, url))
    unique = {}
    for key, url in assets:
        unique.setdefault(url, key)
    return [(key, url) for url, key in unique.items()]


def download_assets(out_dir: Path, credential: str) -> None:
    result, retry_after = get_status(out_dir, credential)
    if result.get("status") != "SUCCEEDED":
        output(result_summary(result, retry_after))
        return
    assets = result_assets(result)
    if not assets or not any(key == "model.glb" for key, _ in assets):
        raise ToolError("The completed task returned no GLB model")
    destination = out_dir / "downloads"
    destination.mkdir(parents=True, exist_ok=True)
    manifest_path = out_dir / "downloads.json"
    manifest = read_json(manifest_path) if manifest_path.exists() else {
        "taskId": result["id"], "startedAt": utc_now(), "files": []}
    if manifest.get("taskId") != result["id"]:
        raise ToolError("Download manifest belongs to another task")
    previous = {item["asset"]: item for item in manifest.get("files", [])}
    opener = urllib.request.build_opener(
        HttpsAssetRedirect(), urllib.request.HTTPSHandler(context=ssl.create_default_context()))
    used_names = {}
    downloaded = []
    for key, url in assets:
        require_https(url)
        filename = urllib.parse.unquote(Path(urllib.parse.urlsplit(url).path).name)
        filename = re.sub(r"[^A-Za-z0-9_.-]", "_", filename)[:160]
        if not filename or filename in {".", ".."}:
            raise ToolError("An asset has no usable original filename")
        if filename in used_names and used_names[filename] != url:
            raise ToolError("Different assets have the same filename; retain separate original material paths")
        used_names[filename] = url
        target = destination / filename
        old = previous.get(key)
        if old and old.get("filename") == filename and target.is_file() \
                and target.stat().st_size == old.get("bytes") and sha256_file(target) == old.get("sha256"):
            downloaded.append(old)
            continue
        temporary = None
        try:
            # Asset URLs contain their own signed access. NO API Authorization header.
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with opener.open(request, timeout=TIMEOUT_SECONDS) as response, tempfile.NamedTemporaryFile(
                mode="wb", dir=destination, prefix=".download-", suffix=".part", delete=False,
            ) as stream:
                temporary = Path(stream.name)
                digest = hashlib.sha256()
                size = 0
                for block in iter(lambda: response.read(1024 * 1024), b""):
                    size += len(block)
                    if size > 1024 * 1024 * 1024:
                        raise ToolError("An asset exceeds the local 1 GiB download limit")
                    stream.write(block)
                    digest.update(block)
                if size == 0:
                    raise ToolError("An asset download was empty")
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, target)
            downloaded.append({"asset": key, "filename": filename, "bytes": size,
                               "sha256": digest.hexdigest(), "downloadedAt": utc_now()})
        except (urllib.error.URLError, TimeoutError, OSError):
            raise ToolError(f"Could not download {key}; completed files are retained for resume") from None
        finally:
            if temporary is not None and temporary.exists():
                temporary.unlink()
        manifest["files"] = downloaded
        atomic_json(manifest_path, manifest)
    manifest["files"] = downloaded
    manifest["finishedAt"] = utc_now()
    atomic_json(manifest_path, manifest)
    summary = result_summary(result)
    summary.update({"downloadedFiles": len(downloaded), "downloadDir": str(destination)})
    output(summary)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("balance", help="Read available Meshy credits; creates no task")
    create = commands.add_parser("create", help="Create exactly one 30-credit image-to-3D task")
    create.add_argument("--imagePath", "--image-path", required=True, type=Path)
    create.add_argument("--outDir", "--out-dir", required=True, type=Path)
    for name in ("status", "download"):
        command = commands.add_parser(name)
        command.add_argument("--outDir", "--out-dir", required=True, type=Path)
    args = parser.parse_args()
    try:
        credential = read_credential()
        if args.command == "balance":
            balance, _ = api_request("/openapi/v1/balance", credential)
            output({"balance": balance.get("balance"), "estimatedCreateCredits": ESTIMATED_CREDITS})
        elif args.command == "create":
            create_task(args.imagePath.resolve(), args.outDir.resolve(), credential)
        elif args.command == "status":
            result, retry_after = get_status(args.outDir.resolve(), credential)
            output(result_summary(result, retry_after))
        elif args.command == "download":
            download_assets(args.outDir.resolve(), credential)
        return 0
    except ToolError as error:
        message = {"error": str(error)}
        if isinstance(error, ApiError) and error.retry_after:
            message["retryAfterSeconds"] = error.retry_after
        print(json.dumps(message, ensure_ascii=False), file=sys.stderr)
        return 1
    except (OSError, ValueError, KeyError, TypeError):
        # Never dump request objects, response bodies, authenticated headers or secrets.
        print(json.dumps({"error": "Local metadata, file or response format is invalid"}), file=sys.stderr)
        return 1
    except Exception:
        print(json.dumps({"error": "The operation could not complete; no automatic retry was made"}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
