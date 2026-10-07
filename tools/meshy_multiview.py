#!/usr/bin/env python3
"""Guarded Meshy reference views -> multi-image 3D workflow.

Every directory owns exactly one POST. Ambiguous submissions must be resolved
from the account task list; the tool never retries a paid request automatically.
Credentials and embedded image bytes are never saved in request metadata.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import sys
import urllib.parse
import urllib.request

import meshy_assets as api

VIEWS_ENDPOINT = "/openapi/v1/image-to-image"
MODEL_ENDPOINT = "/openapi/v1/multi-image-to-3d"
MODEL_PARAMETERS = {
    "ai_model": "meshy-7.1", "geometry_resolution": "2k",
    "should_texture": True, "texture_resolution": "4k", "enable_pbr": True,
    "image_enhancement": False, "remove_lighting": True,
    "should_remesh": True, "topology": "triangle", "target_polycount": 6000,
    "save_pre_remeshed_model": True, "target_formats": ["glb", "obj"],
    "multi_view_thumbnails": True, "alpha_thumbnail": True,
}


def image_input(path: Path) -> tuple[str, dict]:
    data = path.read_bytes()
    mime = "image/png" if data.startswith(b"\x89PNG\r\n\x1a\n") else "image/jpeg" if data.startswith(b"\xff\xd8\xff") else None
    if mime is None or len(data) > 100 * 1024 * 1024:
        raise api.ToolError("References must be PNG/JPEG under 100 MiB")
    return "data:" + mime + ";base64," + base64.b64encode(data).decode("ascii"), {
        "path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "mimeType": mime,
    }


def submit(endpoint: str, payload: dict, out_dir: Path, credential: str, estimate: int, inputs: list[dict]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    state_path = out_dir / "create-state.json"
    if (out_dir / "task.json").exists() or state_path.exists():
        raise api.ToolError("This directory already owns a submission; inspect it before creating another")
    lock_path = out_dir / ".create.lock"
    try:
        lock = lock_path.open("x", encoding="utf-8")
    except FileExistsError:
        raise api.ToolError("Another or interrupted submission owns this directory") from None
    try:
        with lock:
            lock.write(api.utc_now())
        balance, _ = api.api_request("/openapi/v1/balance", credential)
        available = balance.get("balance")
        if not isinstance(available, (int, float)) or available < estimate:
            raise api.ToolError("Available Meshy balance is below the estimated request cost")
        api.atomic_json(out_dir / "request.json", {
            "schemaVersion": 1, "endpoint": endpoint, "preparedAt": api.utc_now(),
            "estimatedCredits": estimate, "inputs": inputs,
            "parameters": api.redact(payload, credential),
            "documentation": "https://docs.meshy.ai/en/api/" + endpoint.rsplit("/", 1)[-1],
        })
        api.atomic_json(state_path, {"state": "SUBMITTING", "submittedAt": api.utc_now(), "balanceBefore": available})
        try:
            result, _ = api.api_request(endpoint, credential, payload)
        except api.ApiError as error:
            api.atomic_json(state_path, {"state": "REJECTED" if error.code in {400, 401, 402, 403, 404, 409, 422, 429} else "UNKNOWN", "httpStatus": error.code})
            raise
        except api.ToolError:
            api.atomic_json(state_path, {"state": "UNKNOWN"})
            raise api.ToolError("Submission outcome is uncertain; check the task list before trying another request") from None
        identity = result.get("result")
        if not isinstance(identity, str) or not re.fullmatch(r"[A-Za-z0-9_-]+", identity):
            api.atomic_json(state_path, {"state": "UNKNOWN"})
            raise api.ToolError("Response contains no valid task ID; inspect account tasks")
        api.atomic_json(out_dir / "task.json", {"taskId": identity, "endpoint": endpoint, "estimatedCredits": estimate, "balanceBefore": available, "createdAt": api.utc_now()})
        api.atomic_json(state_path, {"state": "CREATED", "taskId": identity})
        api.output({"taskId": identity, "endpoint": endpoint, "status": "SUBMITTED", "estimatedCredits": estimate, "balanceBefore": available})
    finally:
        lock_path.unlink(missing_ok=True)


def status(out_dir: Path, credential: str) -> dict:
    task = api.read_json(out_dir / "task.json")
    api.ESTIMATED_CREDITS = task.get("estimatedCredits", 30)
    endpoint = task.get("endpoint")
    if endpoint not in {VIEWS_ENDPOINT, MODEL_ENDPOINT}:
        raise api.ToolError("Unsupported saved task endpoint")
    identity = api.task_id(out_dir)
    result, _ = api.api_request(endpoint + "/" + identity, credential)
    if result.get("id") != identity:
        raise api.ToolError("Response ID does not match the saved task")
    api.atomic_json(out_dir / "result.json", result)
    return result


def download(out_dir: Path, credential: str) -> None:
    result = status(out_dir, credential)
    task = api.read_json(out_dir / "task.json")
    if result.get("status") != "SUCCEEDED":
        api.output(api.result_summary(result))
        return
    if task["endpoint"] == MODEL_ENDPOINT:
        api.TASK_ENDPOINT = MODEL_ENDPOINT
        api.download_assets(out_dir, credential)
        return
    images = result.get("image_urls", [])
    if not 1 <= len(images) <= 4:
        raise api.ToolError("Completed view task returned an unexpected image count")
    destination = out_dir / "downloads"
    destination.mkdir(parents=True, exist_ok=True)
    opener = urllib.request.build_opener(api.HttpsAssetRedirect())
    manifest = []
    for index, url in enumerate(images):
        api.require_https(url)
        suffix = Path(urllib.parse.urlsplit(url).path).suffix.lower()
        if suffix not in {".png", ".jpg", ".jpeg"}:
            suffix = ".png"
        path = destination / f"view-{index + 1}{suffix}"
        if not path.exists():
            request = urllib.request.Request(url, headers={"User-Agent": api.USER_AGENT})
            with opener.open(request, timeout=60) as response:
                data = response.read(100 * 1024 * 1024 + 1)
            if len(data) > 100 * 1024 * 1024:
                raise api.ToolError("View output exceeds 100 MiB")
            path.write_bytes(data)
        _, metadata = image_input(path)
        manifest.append(metadata)
    api.atomic_json(out_dir / "downloads.json", {"taskId": result["id"], "files": manifest})
    api.output({**api.result_summary(result), "downloadedImages": len(manifest), "downloadDir": str(destination)})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    views = commands.add_parser("views", help="Generate coherent unseen views through Meshy Image to Image")
    views.add_argument("--image", type=Path, required=True)
    views.add_argument("--prompt-file", type=Path, required=True)
    model = commands.add_parser("model", help="Generate one object from one to four distinct views")
    model.add_argument("--images", nargs="+", type=Path, required=True)
    for name in ("status", "download"):
        commands.add_parser(name)
    for command in commands.choices.values():
        command.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        credential = api.read_credential()
        directory = args.out_dir.resolve()
        if args.command == "views":
            encoded, metadata = image_input(args.image.resolve())
            prompt = args.prompt_file.read_text(encoding="utf-8-sig").strip()
            if not prompt:
                raise api.ToolError("View generation prompt is empty")
            submit(VIEWS_ENDPOINT, {"ai_model": "nano-banana-pro", "prompt": prompt, "reference_image_urls": [encoded], "generate_multi_view": True, "remove_background": True}, directory, credential, 9, [metadata])
        elif args.command == "model":
            if not 1 <= len(args.images) <= 4:
                raise api.ToolError("Model generation requires one to four views")
            references = [image_input(path.resolve()) for path in args.images]
            payload = {**MODEL_PARAMETERS, "image_urls": [item[0] for item in references], "texture_image_urls": [item[0] for item in references]}
            submit(MODEL_ENDPOINT, payload, directory, credential, 35, [item[1] for item in references])
        elif args.command == "status":
            api.output({**api.result_summary(status(directory, credential)), "estimatedCredits": api.read_json(directory / "task.json").get("estimatedCredits")})
        else:
            download(directory, credential)
        return 0
    except api.ToolError as error:
        print(json.dumps({"error": str(error)}), file=sys.stderr)
    except Exception:
        print(json.dumps({"error": "Local operation or remote response failed; no automatic submission retry was made"}), file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
