"""Build/run an offscreen first-person stage audit from frozen private copies.

Reads an existing MSVC/Ninja build, copies its inputs, and replaces only three
objects privately. Never invokes CMake/Ninja or changes the user's settings.
Run through first_person_stage_profile.ps1 for the MSVC environment.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


def sha(path: Path) -> str:
    return hashlib.file_digest(path.open("rb"), "sha256").hexdigest()


def win_args(command: str) -> list[str]:
    count = ctypes.c_int()
    fn = ctypes.windll.shell32.CommandLineToArgvW
    fn.restype = ctypes.POINTER(ctypes.c_wchar_p)
    pointer = fn(command, ctypes.byref(count))
    if not pointer:
        raise ctypes.WinError()
    result = [pointer[i] for i in range(count.value)]
    ctypes.windll.kernel32.LocalFree(pointer)
    return result


def once(text: str, before: str, after: str) -> str:
    if text.count(before) != 1:
        raise RuntimeError(f"Expected one frozen-source anchor, found {text.count(before)}: {before[:90]!r}")
    return text.replace(before, after)


def scope_call(text: str, call: str, stage: str) -> str:
    return once(text, call, "{ fp::Scope profile(fp::" + stage + "); " + call.strip() + " }")


def instrument_view(text: str) -> str:
    text = '#include "first_person_profile.hpp"\nnamespace fp = devilution::first_person_profile;\n' + text
    for signature, stage in (
        ("bool DrawTownView(const Surface &fullOut, bool forceGeometry)", "World"),
        ("Surface PrepareSamplingBuffers(const Surface &logical)", "Buffers"),
        ("void ApplyHorizonAtmosphere(const Surface &out)", "Atmosphere"),
        ("void ResolveSampling(const Surface &logical, const Surface &sampled)", "SamplingResolve"),
        ("void DrawHorizon(const Surface &out)", "HorizonRecord"),
    ):
        text = once(text, signature + "\n{", signature + "\n{\n\tfp::Scope profile(fp::" + stage + ");")
    text = scope_call(text, "\tPrepareSceneLighting();", "Lighting")
    text = scope_call(text, "\t\tPrepareHorizon();", "HorizonPrepare")
    text = scope_call(text, "\tBuildTownShadowMap(GetTownScene(), shadowConfig);", "ShadowValidate")
    text = once(text, "\tconst auto drawWorld = [&]() {", "\tconst auto drawWorld = [&]() {\n\t\tfp::Scope profileScene(fp::SceneRecord);")
    text = once(text, "\t\tforEachTile([&](Point tile) {\n\t\t\tconst TileArt &art", "\t\t{ fp::Scope profileTerrain(fp::TerrainRecord);\n\t\tforEachTile([&](Point tile) {\n\t\t\tconst TileArt &art")
    text = once(text, "\t\tforEachTile([&](Point tile) {\n\t\t\tDrawScenery", "\t\t}\n\t\t{ fp::Scope profileScenery(fp::SceneryRecord);\n\t\tforEachTile([&](Point tile) {\n\t\t\tDrawScenery")
    text = once(text, "\t\tDrawScene(out);\n\t\tDrawVegetation(out);\n\t\tDrawProps(out);", "\t\t}\n\t\t{ fp::Scope profile(fp::ArchitectureRecord); DrawScene(out); }\n\t\t{ fp::Scope profile(fp::VegetationRecord); DrawVegetation(out); }\n\t\t{ fp::Scope profile(fp::PropsRecord); DrawProps(out); }\n\t\tfp::Scope profileActors(fp::ActorsRecord);")
    text = once(text, "\t\tconst bool complete = TownGpuEndFrame(GpuFrame) && !CaptureGpuFailed", "\t\tconst bool ended = TownGpuEndFrame(GpuFrame);\n\t\tfp::Scope profilePublish(fp::GpuPublish);\n\t\tconst bool complete = ended && !CaptureGpuFailed")
    text = once(text, "\t\t\tdrawWorld();", "\t\t\tprofilePublish.Stop();\n\t\t\tdrawWorld();")
    return text


def instrument_gpu(text: str) -> str:
    text = '#include "first_person_profile.hpp"\nnamespace fp = devilution::first_person_profile;\n' + text
    text = once(text, "#include <dxgi.h>", '#include <dxgi.h>\n#include "first_person_gpu_timer.hpp"')
    for signature, stage in (
        ("bool TownGpuBeginFrame(int width, int height, bool allowWarpForDiagnostics, const TownGpuProjection &projection)", "GpuBegin"),
        ("bool TownGpuSetShadow(const TownGpuShadow &shadow)", "GpuShadow"),
        ("bool TownGpuEndFrame(TownGpuFrame &output)", "GpuEnd"),
    ):
        text = once(text, signature + "\n{", signature + "\n{\n\tfp::Scope profile(fp::" + stage + ");")
    text = once(text, "\tconst HRESULT result = Context->Map(target.staging.get(), 0, D3D11_MAP_READ, 0, &mapped);", "\tfp::Scope profileMap(fp::ReadbackMapWait);\n\tconst HRESULT result = Context->Map(target.staging.get(), 0, D3D11_MAP_READ, 0, &mapped);\n\tprofileMap.Stop();")
    text = once(text, '\t\treturn Fail("Map GPU readback", result);\n\tfor (int y', '\t\treturn Fail("Map GPU readback", result);\n\tfp::Scope profileCopy(fp::ReadbackCpuCopy);\n\tfor (int y')
    text = once(text, "\tif (!Vertices.empty()) {\n\t\tif (VertexCapacity", "\tif (!Vertices.empty()) {\n\t\tfp::Scope profileUpload(fp::GpuVertexUpload);\n\t\tif (VertexCapacity")
    text = once(text, "\t\tfor (const Batch &batch : Batches) {", "\t\tprofileUpload.Stop();\n\t\tfp::Scope profileDraw(fp::GpuDrawSubmission);\n\t\tfor (const Batch &batch : Batches) {")
    text = once(text, "\tfor (Target &target : Targets)\n\t\tContext->CopyResource(target.staging.get(), target.texture.get());", "\t{ fp::Scope profileCopy(fp::GpuCopySubmission);\n\tfor (Target &target : Targets)\n\t\tContext->CopyResource(target.staging.get(), target.texture.get());\n\t}")
    text = once(text, "\tTownGpuFrame complete;", "\tfp::Scope profileAllocation(fp::ReadbackAllocation);\n\tTownGpuFrame complete;")
    text = once(text, "\tcomplete.depth.resize(count);", "\tcomplete.depth.resize(count);\n\tprofileAllocation.Stop();")
    text = once(text, "\tconst float zero[] { 0, 0, 0, 0 };", "\tfp::Timer.Begin(Device.get(), Context.get());\n\tconst float zero[] { 0, 0, 0, 0 };")
    text = once(text, "\tID3D11ShaderResourceView *emptyResources[4] {};", "\tfp::Timer.DrawsComplete(Context.get());\n\tID3D11ShaderResourceView *emptyResources[4] {};")
    text = once(text, "\tconst auto readbackStart = Clock::now();", "\tfp::Timer.CopiesComplete(Context.get());\n\tconst auto readbackStart = Clock::now();")
    text = once(text, "\tStatus.readbackMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - readbackStart).count();", "\tfp::Timer.ReadResult(Context.get());\n\tStatus.readbackMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - readbackStart).count();")
    text = once(text, "\tFrameStart = Clock::now();", "\tfp::Current.cacheBytesPeak = TextureBytes;\n\tfp::Current.cachedTexturesPeak = TextureCache.size();\n\tFrameStart = Clock::now();")
    if "\tTextureBytes += bytes;" in text:
        text = once(text, "\tif (bytes > MaxTextureBytes || TextureBytes > MaxTextureBytes - bytes) {", "\tif (bytes > MaxTextureBytes || TextureBytes > MaxTextureBytes - bytes) {\n\t\tfp::Current.cacheBytesPeak = std::max(fp::Current.cacheBytesPeak, TextureBytes);\n\t\tfp::Current.cachedTexturesPeak = std::max(fp::Current.cachedTexturesPeak, TextureCache.size());\n\t\tfp::Current.failedUploadBytes = bytes;\n\t\tfp::Current.failedLutBytes = input.lightLut.size();")
        text = once(text, "\tTextureBytes += bytes;", "\tTextureBytes += bytes;\n\t++fp::Current.textureUploads;\n\tfp::Current.uploadedCodeBytes += count * sizeof(uint32_t);\n\tfp::Current.uploadedLutBytes += input.lightLut.size();\n\tfp::Current.cacheBytesPeak = std::max(fp::Current.cacheBytesPeak, TextureBytes);\n\tfp::Current.cachedTexturesPeak = std::max(fp::Current.cachedTexturesPeak, TextureCache.size() + 1);")
    elif "\t\tStatus.uploadedTexelBytes += texelBytes;" in text:
        text = once(text, "\t\tStatus.uploadedTexelBytes += texelBytes;", "\t\tStatus.uploadedTexelBytes += texelBytes;\n\t\t++fp::Current.textureUploads;\n\t\tfp::Current.uploadedCodeBytes += texelBytes;\n\t\tfp::Current.cacheBytesPeak = std::max(fp::Current.cacheBytesPeak, TextureBytes);\n\t\tfp::Current.cachedTexturesPeak = std::max(fp::Current.cachedTexturesPeak, TexelCache.size());")
        text = once(text, "\t\tStatus.uploadedLutBytes += lutBytes;", "\t\tStatus.uploadedLutBytes += lutBytes;\n\t\tfp::Current.uploadedLutBytes += lutBytes;\n\t\tfp::Current.cacheBytesPeak = std::max(fp::Current.cacheBytesPeak, TextureBytes);")
        text = once(text, "\tif (!MakeTextureRoom(texelBytes + lutBytes))\n\t\treturn nullptr;", "\tif (!MakeTextureRoom(texelBytes + lutBytes)) {\n\t\tfp::Current.failedUploadBytes = texelBytes + lutBytes;\n\t\tfp::Current.failedLutBytes = lutBytes;\n\t\treturn nullptr;\n\t}")
    else:
        raise RuntimeError("Unknown texture cache implementation; review profiling anchors before running")
    return text


def run_logged(args: list[str], cwd: Path, log: Path, env=None) -> None:
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(args, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}); see {log}\n{log.read_text(errors='replace')[-5000:]}")


def prepare(repo: Path, build: Path, output: Path, asset_profile: Path) -> Path:
    private = output / "private-build"
    private.mkdir(parents=True, exist_ok=False)
    copies = private / "source"
    copies.mkdir()
    inputs = []
    def copy(source: Path, dest: Path) -> None:
        digest = sha(source)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)
        if sha(source) != digest or sha(dest) != digest:
            raise RuntimeError(f"Input changed during snapshot: {source}")
        inputs.append({"source": str(source), "snapshot": str(dest), "sha256": digest})
    for source in (repo / "Source").rglob("*"):
        if source.suffix in (".h", ".hpp"):
            copy(source, copies / "Source" / source.relative_to(repo / "Source"))
    for source in (build / "Source").rglob("*"):
        if source.suffix in (".h", ".hpp"):
            copy(source, copies / "GeneratedSource" / source.relative_to(build / "Source"))
    for source in (repo / "tools").rglob("*"):
        if source.suffix in (".h", ".hpp"):
            copy(source, copies / "tools" / source.relative_to(repo / "tools"))
    for name in ("compile_commands.json", "build.ninja"):
        copy(build / name, private / name)
    for name in ("first_person_profile.hpp", "first_person_gpu_timer.hpp", "first_person_runtime_checks.hpp", "first_person_stage_profile.py", "first_person_stage_profile.ps1"):
        copy(repo / "tools" / name, copies / name)
    for relative in ("Source/engine/render/town_view.cpp", "Source/engine/render/town_gpu.cpp", "tools/town_view_smoke.cpp"):
        original = copies / "original" / Path(relative).name
        copy(repo / relative, original)
        shutil.copy2(original, copies / Path(relative).name)
    source = copies / "town_view.cpp"
    source.write_text(instrument_view(source.read_text(encoding="utf-8-sig")), encoding="utf-8")
    source = copies / "town_gpu.cpp"
    source.write_text(instrument_gpu(source.read_text(encoding="utf-8-sig")), encoding="utf-8")
    source = copies / "town_view_smoke.cpp"
    smoke = source.read_text(encoding="utf-8-sig")
    smoke = once(smoke, "int main(int argc, char **argv)", '#include "first_person_runtime_checks.hpp"\n\nint main(int argc, char **argv)')
    branch_start = smoke.index("\t\telse if (architectureCulling || firstPersonPerformance) {")
    branch_end = smoke.index("\t\telse if (cabinOpenings)", branch_start)
    smoke = smoke[:branch_start] + "\t\telse if (firstPersonPerformance) RunFirstPersonStageProfile(output);\n" + smoke[branch_end:]
    source.write_text(smoke, encoding="utf-8")

    # The static build's object and library bytes are frozen before linking, so
    # independent ongoing builds cannot replace an input halfway through link.
    ninja = (private / "build.ninja").read_text(encoding="utf-8-sig")
    match = re.search(r"^build town_view_smoke\.exe: [^ ]+ (.*?)\n(.*?)(?=\n\n)", ninja, re.M | re.S)
    if not match:
        raise RuntimeError("No existing town_view_smoke target found")
    objects = match[1].split(" | ", 1)[0].split()
    variables = dict(re.findall(r"^  ([A-Z_]+) = (.*)$", match[2], re.M))
    libraries = win_args("tool " + variables["LINK_LIBRARIES"])[1:]
    link_inputs = []
    for relative in objects + libraries:
        origin = build / relative
        if origin.is_file():
            snapshot = private / "objects" / relative
            copy(origin, snapshot)
            link_inputs.append(str(snapshot))
        else:
            link_inputs.append(relative)
    commands = json.loads((private / "compile_commands.json").read_text())
    replacements = {}
    for name in ("town_view.cpp", "town_gpu.cpp", "town_view_smoke.cpp"):
        command = next(item for item in commands if Path(item["file"]).name == name)
        args = win_args(command["command"])
        target = private / (name + ".obj")
        transformed = []
        for arg in args:
            if arg.startswith("/Fo") or arg.startswith("-Fo"):
                transformed.append("/Fo" + str(target))
            elif arg.startswith("/Fd") or arg.startswith("-Fd"):
                transformed.append("/Fd" + str(private / (name + ".pdb")))
            elif arg.replace("\\", "/") == command["file"].replace("\\", "/"):
                transformed.append(str(copies / name))
            elif arg.replace("\\", "/").lower() == ("-I" + str(build / "Source")).replace("\\", "/").lower():
                transformed.append("/I" + str(copies / "GeneratedSource"))
            else:
                transformed.append(arg)
        transformed[1:1] = ["/I" + str(copies / "Source"), "/I" + str(copies), "/I" + str(copies / "tools")]
        if "cachedTextureBytes" in (copies / "Source/engine/render/town_gpu.hpp").read_text(encoding="utf-8-sig"):
            transformed.insert(1, "/DFIRST_PERSON_SPLIT_CACHE")
        run_logged(transformed, private, private / (name + ".compile.log"))
        old = next(path for path in link_inputs if path.endswith("\\" + name + ".obj"))
        replacements[old] = str(target)
    link_inputs = [replacements.get(path, path) for path in link_inputs]
    executable = private / "first_person_stage_profile.exe"
    response = private / "link.rsp"
    response.write_text("\n".join('"' + arg + '"' for arg in ["/NOLOGO", "/MACHINE:X64", "/INCREMENTAL:NO", "/SUBSYSTEM:CONSOLE", "/OUT:" + str(executable)] + link_inputs), encoding="utf-8")
    run_logged(["link.exe", "@" + str(response)], private, private / "link.log")
    for source in asset_profile.rglob("*"):
        if source.is_file() and (source.name == "d3d-lighting.ini" or any(part in ("d3d-models", "d3d-maps") for part in source.relative_to(asset_profile).parts)):
            copy(source, output / source.relative_to(asset_profile))
    receipt = {"scope": "Frozen private instrumented copies; read-only existing build/profile/assets; no CMake/Ninja/game window; no sustained FPS", "createdUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "executable": str(executable), "executableSha256": sha(executable), "inputs": inputs, "instrumentedSources": [{"path": str(copies / name), "sha256": sha(copies / name)} for name in ("town_view.cpp", "town_gpu.cpp", "town_view_smoke.cpp")]}
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    return executable


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--asset-profile", type=Path, required=True)
    parser.add_argument("--game-data", type=Path, required=True)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    if args.output.resolve().is_relative_to(args.build.resolve()):
        raise RuntimeError("Private diagnostics must stay outside the shared build directory")
    if args.output.resolve().is_relative_to(args.asset_profile.resolve()):
        raise RuntimeError("Private diagnostics must stay outside the input asset profile")
    args.output.mkdir(parents=True, exist_ok=True)
    executable = prepare(args.repo.resolve(), args.build.resolve(), args.output.resolve(), args.asset_profile.resolve())
    if not args.prepare_only:
        env = os.environ.copy()
        env["SDL_VIDEODRIVER"] = "dummy"
        if args.quick: env["FIRST_PERSON_PROFILE_QUICK"] = "1"
        else: env.pop("FIRST_PERSON_PROFILE_QUICK", None)
        run_logged([str(executable), str(args.game_data), str(args.build / "assets"), str(args.output), "--first-person-performance"], args.output, args.output / "run.log", env)
        receipt_path = args.output / "receipt.json"
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        assets = [item for item in receipt["inputs"] if Path(item["source"]).is_relative_to(args.asset_profile.resolve())]
        receipt["assetInputsPreservedAfterRun"] = all(sha(Path(item["source"])) == item["sha256"] and sha(Path(item["snapshot"])) == item["sha256"] for item in assets)
        receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(f"Evidence: {args.output}", flush=True)


if __name__ == "__main__":
    main()
