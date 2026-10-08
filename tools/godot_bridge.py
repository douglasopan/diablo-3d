#!/usr/bin/env python3
"""Prepare the local Godot workspace and apply an explicit static town pack.

Source art, snapshots, profiles, receipts and saves remain local. No networking,
generation service, game archive modification or selection by timestamp occurs.
"""
from __future__ import annotations

import argparse
import configparser
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys


REPO = Path(__file__).resolve().parents[1]
TARGET_PROFILES = ("perfil-godot-review", "perfil-tristram")


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def contained(root: Path, relative: str) -> Path:
    name = PurePosixPath(relative.replace("\\", "/"))
    if name.is_absolute() or any(part in ("..", ".") or ":" in part for part in name.parts):
        raise ValueError(f"Caminho relativo invalido: {relative}")
    path = (root / str(name)).resolve()
    if not path.is_relative_to(root.resolve()) or path == root.resolve():
        raise ValueError("O caminho sai da pasta permitida")
    return path


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def copy_checked(source: Path, target: Path, expected: str) -> None:
    if not re.fullmatch(r"[a-f0-9]{64}", expected) or digest(source) != expected:
        raise ValueError(f"A fonte diverge da revisao selecionada: {source.name}")
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix(target.suffix + ".tmp")
    shutil.copy2(source, temporary)
    if digest(temporary) != expected:
        raise ValueError(f"A copia diverge da fonte: {target.name}")
    temporary.replace(target)


def workspace_root() -> Path:
    return REPO.parent if REPO.name == "devilutionx" else REPO


def find_data(root: Path) -> Path:
    candidates = [Path(r"C:\Program Files (x86)\GOG Galaxy\Games\Diablo"), root / "data"]
    for path in candidates:
        if any((path / name).is_file() for name in ("DIABDAT.MPQ", "diabdat.mpq", "spawn.mpq", "SPAWN.MPQ")):
            return path
    raise ValueError("Informe --data com a pasta de DIABDAT.MPQ ou spawn.mpq")


def check_closed(profile: Path) -> None:
    if sys.platform != "win32":
        return
    # Static PowerShell, no paths interpolated as executable code. Only inspect
    # process names; refusing all project game instances is conservative.
    result = subprocess.run([
        "powershell.exe", "-NoProfile", "-Command",
        "Get-Process -ErrorAction SilentlyContinue | Where-Object ProcessName -Like '*devilution*' | Select-Object -ExpandProperty Id",
    ], capture_output=True, text=True, check=True)
    if result.stdout.strip():
        raise ValueError("Feche a partida antes de aplicar o perfil de destino. Nenhum processo foi encerrado.")


def install_baseline(root: Path, profile: Path) -> dict:
    baseline = json.loads((REPO / "assets/runtime-baseline.json").read_text(encoding="utf-8-sig"))
    for source_key, target_key, hash_key in (
        ("sourcePath", "runtimePath", "sourceSha256"),
        ("lightingSourcePath", "lightingRuntimePath", "lightingSha256"),
    ):
        source = contained(root, baseline[source_key])
        fallback = contained(root / "perfil-tristram", baseline[target_key])
        if not source.is_file() and fallback.is_file():
            source = fallback
        target = contained(profile, baseline[target_key])
        if source.is_file() and digest(source) != baseline[hash_key]:
            raise ValueError("A fonte da baseline diverge da revisao selecionada; nenhum modelo existente sera substituido")
        if target.is_file():
            if digest(target) != baseline[hash_key]:
                raise ValueError("O asset local diverge da baseline; preserve e selecione uma revisao explicita")
            continue
        if source.is_file():
            copy_checked(source, target, baseline[hash_key])
    return baseline


def run_snapshot(args: argparse.Namespace, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([str(args.smoke), str(args.data), str(args.assets), str(output), "--editor-snapshot"],
                            capture_output=True, text=True)
    (output / "snapshot-command.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise ValueError(f"O diagnostico recusou o cenario. Consulte {output / 'snapshot-command.log'}")
    snapshot = json.loads((output / "town-snapshot.json").read_text(encoding="utf-8"))
    if snapshot.get("format") != "d3d.town-snapshot" or snapshot.get("schemaVersion") != 1:
        raise ValueError("Snapshot fora do contrato do editor")
    if snapshot.get("edition") != "retail":
        raise ValueError("A ponte Godot v1 requer DIABDAT.MPQ do Diablo completo; shareware ainda nao e suportado")
    return snapshot


def prepare(args: argparse.Namespace) -> None:
    root, local = args.workspace, args.project / "local"
    local.mkdir(parents=True, exist_ok=True)
    staging = local / "preparation" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    baseline = install_baseline(root, staging)
    snapshot = run_snapshot(args, staging)
    check_cabin(snapshot, baseline, args.allow_prototypes)
    from PIL import Image
    for model in snapshot["models"]:
        relative = model.get("texture", "")
        if relative:
            source = contained(staging, relative)
            if source.suffix.lower() == ".ppm":
                target = source.with_suffix(".png")
                with Image.open(source) as image:
                    image.save(target)
                model["texture"] = target.relative_to(staging).as_posix()
                model["textureSha256"] = digest(target)
    registry = json.loads((REPO / "assets/registry.json").read_text(encoding="utf-8"))
    write_json(staging / "catalog.json", registry)
    # The optional original GLB is kept separate from the assembled runtime mesh.
    conversion = contained(root, baseline["sourcePath"] + ".json")
    if conversion.is_file():
        metadata = json.loads(conversion.read_text(encoding="utf-8-sig"))
        original = Path(metadata.get("sourceGlb", ""))
        if original.is_file():
            target = staging / "source/cabin-east.glb"
            copy_checked(original, target, metadata["sourceSha256"])
            write_json(staging / "source/cabin-east-fit.json", metadata)
            for model in snapshot["models"]:
                if model["instanceId"] == "cabin-east":
                    model["sourceGlb"] = "source/cabin-east.glb"
                    model["sourceSha256"] = metadata["sourceSha256"]
                    model["sourceFit"] = metadata["fit"]
    snapshot["preparedAt"] = datetime.now(timezone.utc).isoformat()
    ground_path = staging / "ground-reference.json"
    if ground_path.is_file():
        snapshot["ground"] = json.loads(ground_path.read_text(encoding="utf-8"))
    snapshot["baselinePipeline"] = baseline["pipeline"]["id"]
    snapshot["catalog"] = "catalog.json"
    # Publish the snapshot last. A failed preparation cannot leave a new
    # procedural/fallback snapshot that Abrir would mistake for a ready scene.
    for source in staging.rglob("*"):
        if source.is_file() and source.name != "town-snapshot.json":
            target = contained(local, source.relative_to(staging).as_posix())
            copy_checked(source, target, digest(source))
    write_json(local / "town-snapshot.json", snapshot)
    print(f"Cenario real preparado: {len(snapshot['models'])} grupos; {local}")


def check_cabin(snapshot: dict, baseline: dict, allow_prototypes: bool) -> None:
    cabin = next((model for model in snapshot["models"] if model.get("instanceId") == "cabin-east"), {})
    if not allow_prototypes and (not cabin.get("external") or cabin.get("sha256") != baseline["sourceSha256"]
            or cabin.get("sourceAudit", {}).get("status") != "matched" or not cabin.get("hasFireSources")):
        raise ValueError("A cabana selecionada com interior/fogo nao foi carregada. A cena editada foi preservada. Verifique os assets locais; --allow-prototypes e uma escolha explicita para trabalhar sem a baseline privada.")


def parse_pack(pack: Path) -> tuple[bytes, list[tuple[str, str, str]]]:
    manifest = contained(pack, "d3d-maps/tristram.ini").read_bytes()
    text = manifest.decode("utf-8-sig")
    instances: list[str] = []
    section = ""
    seen_sections: set[str] = set()
    seen_keys: set[tuple[str, str]] = set()
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith((";", "#")):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            if section in seen_sections:
                raise ValueError("Secao duplicada no pacote")
            seen_sections.add(section)
        elif "=" in line:
            key, value = (part.strip() for part in line.split("=", 1))
            if section == "Scene" and key == "instance":
                if value in instances:
                    raise ValueError("Instancia duplicada no pacote")
                instances.append(value)
            elif (section, key) in seen_keys:
                raise ValueError("Campo duplicado no pacote")
            seen_keys.add((section, key))
        else:
            raise ValueError("Linha invalida no manifesto")
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    parser.optionxform = str
    parser.read_string(text)
    if not parser.has_section("Scene") or parser["Scene"].get("format") != "d3d.town-map" \
            or parser["Scene"].get("schemaVersion") != "1" or parser["Scene"].get("edition") != "retail":
        raise ValueError("Pacote nao corresponde a Tristram retail v1")
    if set(parser.sections()) != {"Scene", *instances} or not 1 <= len(instances) <= 14:
        raise ValueError("Secoes/instancias fora do contrato")
    files: list[tuple[str, str, str]] = []
    for instance in instances:
        if not re.fullmatch(r"[a-z0-9-]{1,48}", instance):
            raise ValueError("Identificador de instancia invalido")
        entry = parser[instance]
        relative, expected = entry.get("model", ""), entry.get("sha256", "")
        if not re.fullmatch(r"d3d-models/editor/[a-z0-9-]+\.d3d", relative):
            raise ValueError("Modelo fora da pasta de exportacao permitida")
        source = contained(pack, relative)
        if not re.fullmatch(r"[a-f0-9]{64}", expected) or digest(source) != expected:
            raise ValueError(f"Hash divergente no modelo {instance}")
        header = source.read_bytes()[:20]
        if len(header) != 20 or header[:8] != b"D3DMESH1":
            raise ValueError("Modelo fora do formato D3DMESH1")
        triangles, width, height = struct.unpack_from("<III", header, 8)
        if not 1 <= triangles <= 20000 or not 1 <= width <= 2048 or not 1 <= height <= 2048 \
                or source.stat().st_size != 20 + triangles * 60 + width * height * 3:
            raise ValueError("Modelo excede limites ou tamanho declarado")
        files.append((instance, relative, expected))
    return manifest, files


def target_profile(args: argparse.Namespace) -> Path:
    name = getattr(args, "profile", TARGET_PROFILES[0])
    if name not in TARGET_PROFILES:
        raise ValueError("Perfil de destino fora das duas pastas permitidas")
    return contained(args.workspace, name)


def apply_pack(args: argparse.Namespace) -> None:
    profile = target_profile(args)
    # Require the explicit candidate before creating baseline/config/save files.
    # A legacy v4 can run while silently ignoring this map format.
    executable_sha256 = digest(args.executable)
    manifest, files = parse_pack(args.pack)
    # Native binding and triangle validation are also performed by the actual
    # game loader in an isolated staging profile before any live-profile write.
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    staging = args.project / "local/validation" / stamp
    if any(staging.resolve().is_relative_to((args.workspace / name).resolve()) for name in TARGET_PROFILES):
        raise ValueError("A validacao deve usar uma pasta fora dos perfis de jogo")
    baseline = install_baseline(args.workspace, staging)
    for _, relative, expected in files:
        copy_checked(contained(args.pack, relative), contained(staging, relative), expected)
    manifest_path = contained(staging, "d3d-maps/tristram.ini")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_bytes(manifest)
    snapshot = run_snapshot(args, staging)
    check_cabin(snapshot, baseline, args.allow_prototypes)
    map_audit = snapshot.get("editorMapAudit", {})
    if map_audit.get("status") != "matched" or map_audit.get("failure"):
        raise ValueError("O loader real recusou o pacote. O perfil de destino foi preservado; consulte a validacao local.")
    if map_audit.get("assetPath") != "d3d-maps/tristram.ini" \
            or map_audit.get("sha256") != hashlib.sha256(manifest).hexdigest():
        raise ValueError("O loader encontrou outro manifesto; a origem da validacao diverge do pacote")
    audits = {entry["instanceId"]: entry for entry in map_audit.get("instances", [])}
    if set(audits) != {instance for instance, _, _ in files}:
        raise ValueError("O recibo de carregamento nao corresponde as instancias exportadas")
    loaded = {model["instanceId"]: model for model in snapshot["models"]}
    for instance, _, expected in files:
        audit = audits[instance]
        if loaded.get(instance, {}).get("sha256") != expected or audit.get("status") != "matched" \
                or audit.get("failure") or audit.get("sha256") != expected or audit.get("expectedSha256") != expected:
            raise ValueError(f"O jogo nao carregou o override pedido: {instance}. O perfil foi preservado.")
    if getattr(args, "validate_only", False):
        print(f"Pacote validado pelo loader real: {staging}")
        print("Validacao somente: nenhum perfil de jogo foi modificado.")
        return
    # Inspect running games only after staging validation and immediately before
    # any target-profile write. Validation alone is safe while a game is open.
    check_closed(profile)
    install_baseline(args.workspace, profile)
    habitual = args.workspace / "perfil-tristram"
    # Only initialize missing local configuration/save files. Existing review
    # saves and every original save are preserved, never synchronized backwards.
    profile.mkdir(parents=True, exist_ok=True)
    for source in [habitual / "diablo.ini", *habitual.glob("*.sv")]:
        if source.is_file() and not (profile / source.name).exists():
            shutil.copy2(source, profile / source.name)
    if not (profile / "diablo.ini").exists():
        (profile / "diablo.ini").write_text("[Graphics]\nWidth=1280\nHeight=720\nFullscreen=0\n3D GPU Rendering=1\n3D Edge Smoothing=0\n", encoding="utf-8")
    backup = profile / "editor-backups" / stamp
    changes = [relative for _, relative, _ in files] + ["d3d-maps/tristram.ini"]
    previous: dict[str, bytes | None] = {}
    for relative in changes:
        target = contained(profile, relative)
        previous[relative] = target.read_bytes() if target.exists() else None
        if target.exists():
            saved = contained(backup, relative)
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, saved)
    try:
        for _, relative, expected in files:
            copy_checked(contained(args.pack, relative), contained(profile, relative), expected)
        target = contained(profile, "d3d-maps/tristram.ini")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.with_suffix(".ini.tmp").write_bytes(manifest)
        target.with_suffix(".ini.tmp").replace(target)
        write_json(profile / "godot-pack-receipt.json", {
            "format": "d3d.godot-pack-receipt", "schemaVersion": 1,
            "appliedAt": stamp, "manifestSha256": hashlib.sha256(manifest).hexdigest(),
            "sourcePack": str(args.pack), "gameExecutableSha256": executable_sha256,
            "baselinePipeline": baseline["pipeline"]["id"], "validation": str(staging),
            "models": [{"instanceId": i, "path": p, "sha256": h} for i, p, h in files],
            "approval": "technical-only; visual review in game is pending",
        })
    except Exception:
        for relative, data in previous.items():
            target = contained(profile, relative)
            if data is None:
                target.unlink(missing_ok=True)
            else:
                target.write_bytes(data)
        raise
    print(f"Pacote validado pelo loader real e aplicado somente em {profile}")
    if profile.name == "perfil-tristram":
        print("Configuracao e saves existentes foram preservados. Reabra por Iniciar-Tristram.cmd.")
    else:
        print("O perfil habitual e seus saves foram preservados. Teste com Testar-Mapa-Godot.cmd.")


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "apply"))
    parser.add_argument("--workspace", type=Path, default=workspace_root())
    parser.add_argument("--project", type=Path, default=REPO / "editor/godot")
    parser.add_argument("--data", type=Path)
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--smoke", type=Path)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--pack", type=Path)
    parser.add_argument("--profile", choices=TARGET_PROFILES, default=TARGET_PROFILES[0],
                        help="Perfil de destino da aplicacao explicita")
    parser.add_argument("--validate-only", action="store_true",
                        help="Validar apply em staging, sem gravar em qualquer perfil de jogo")
    parser.add_argument("--allow-prototypes", action="store_true", help="Preparar explicitamente sem a cabana privada selecionada")
    return parser


def main() -> int:
    parser = argument_parser()
    args = parser.parse_args()
    if args.validate_only and args.action != "apply":
        parser.error("--validate-only requer a acao apply")
    args.workspace = args.workspace.resolve()
    args.project = args.project.resolve()
    try:
        args.data = (args.data or find_data(args.workspace)).resolve()
        args.assets = (args.assets or args.workspace / "build/assets").resolve()
        args.smoke = (args.smoke or args.workspace / "build/town_view_smoke.exe").resolve()
        args.executable = (args.executable or args.workspace / "build/devilutionx-tristram-godot.exe").resolve()
        args.pack = (args.pack or args.project / "local/export").resolve()
        if args.action == "prepare":
            prepare(args)
        else:
            apply_pack(args)
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"Erro: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
