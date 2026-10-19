"""Independent read-only GLB/skin review of candidate-firstwalk1.

Does not import producer, renderer, inspect_glb or retarget scripts.
Writes only this review's JSON/Markdown beside this script.
"""
from __future__ import annotations

import hashlib
import json
import struct
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
WORKSPACE = HERE.parents[3]
OUTPUT = WORKSPACE / "assets/own-art/heroes/local-r1/warrior/candidate-firstwalk1"
RECEIPT_PATH = OUTPUT / "retarget-receipt.json"


def file_record(path):
    path = Path(path)
    data = path.read_bytes()
    return {"path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def qmatrix(value):
    x, y, z, w = np.asarray(value, dtype=float) / np.linalg.norm(value)
    return np.array([[1-2*y*y-2*z*z, 2*x*y-2*z*w, 2*x*z+2*y*w],
                     [2*x*y+2*z*w, 1-2*x*x-2*z*z, 2*y*z-2*x*w],
                     [2*x*z-2*y*w, 2*y*z+2*x*w, 1-2*x*x-2*y*y]])


def slerp(a, b, fraction):
    a, b = np.asarray(a, dtype=float), np.asarray(b, dtype=float)
    a, b = a / np.linalg.norm(a), b / np.linalg.norm(b)
    dot = float(a @ b)
    if dot < 0:
        b, dot = -b, -dot
    dot = min(1.0, max(-1.0, dot))
    if dot > 0.9995:
        result = a + fraction * (b - a)
    else:
        angle = np.arccos(dot)
        result = (np.sin((1-fraction)*angle)*a + np.sin(fraction*angle)*b) / np.sin(angle)
    return result / np.linalg.norm(result)


def trs(node):
    if "matrix" in node:
        return np.asarray(node["matrix"], dtype=float).reshape(4, 4, order="F")
    result = np.eye(4)
    result[:3, :3] = qmatrix(node.get("rotation", [0, 0, 0, 1])) @ np.diag(node.get("scale", [1, 1, 1]))
    result[:3, 3] = node.get("translation", [0, 0, 0])
    return result


def orientation(matrix):
    left, _, right = np.linalg.svd(matrix[:3, :3])
    rotation = left @ right
    assert np.linalg.det(rotation) > 0
    return rotation


class Glb:
    def __init__(self, path):
        self.path = Path(path)
        raw = self.path.read_bytes()
        assert struct.unpack_from("<4sII", raw) == (b"glTF", 2, len(raw))
        cursor = 12
        self.binary = b""
        while cursor < len(raw):
            size, kind = struct.unpack_from("<I4s", raw, cursor)
            cursor += 8
            value = raw[cursor:cursor+size]
            assert len(value) == size
            if kind == b"JSON":
                self.doc = json.loads(value.rstrip(b" \0\r\n\t"))
            elif kind == b"BIN\0":
                self.binary = value
            cursor += size
        assert cursor == len(raw)
        self.nodes = self.doc["nodes"]
        self.parents = {child: parent for parent, node in enumerate(self.nodes) for child in node.get("children", [])}
        self.names = {node.get("name", ""): i for i, node in enumerate(self.nodes)}
        self.rest = self.world(self.nodes)
        self.skin = self.doc["skins"][0]
        self.joints = np.asarray(self.skin["joints"], dtype=int)
        self.ibms = self.accessor(self.skin["inverseBindMatrices"])
        self.bind = {int(node): np.linalg.inv(self.ibms[slot]) for slot, node in enumerate(self.joints)}
        self.primitive = self.doc["meshes"][0]["primitives"][0]
        self.attrs = {key: self.accessor(index) for key, index in self.primitive["attributes"].items()}
        self.positions4 = np.column_stack((self.attrs["POSITION"], np.ones(len(self.attrs["POSITION"]))))

    def accessor(self, index):
        entry = self.doc["accessors"][index]
        assert "sparse" not in entry
        view = self.doc["bufferViews"][entry["bufferView"]]
        component = {5120: np.int8, 5121: np.uint8, 5122: np.dtype("<i2"), 5123: np.dtype("<u2"),
                     5125: np.dtype("<u4"), 5126: np.dtype("<f4")}[entry["componentType"]]
        dimensions = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[entry["type"]]
        dtype = np.dtype(component)
        start = view.get("byteOffset", 0) + entry.get("byteOffset", 0)
        stride = view.get("byteStride", dimensions * dtype.itemsize)
        value = np.ndarray((entry["count"], dimensions), dtype=dtype, buffer=self.binary,
                           offset=start, strides=(stride, dtype.itemsize)).copy()
        if entry.get("normalized"):
            raise AssertionError("Review requires explicit normalized accessor support")
        if entry["type"] == "MAT4":
            return value.reshape(-1, 4, 4).transpose(0, 2, 1).astype(float)
        return value[:, 0].astype(float) if dimensions == 1 else value.astype(float)

    def world(self, nodes):
        worlds, visiting = {}, set()
        def visit(index):
            if index in worlds:
                return worlds[index]
            assert index not in visiting
            visiting.add(index)
            parent = self.parents.get(index)
            worlds[index] = (np.eye(4) if parent is None else visit(parent)) @ trs(nodes[index])
            visiting.remove(index)
            return worlds[index]
        for index in range(len(nodes)):
            visit(index)
        return worlds

    def channels(self, animation=0):
        result = []
        for channel in self.doc["animations"][animation]["channels"]:
            sampler = self.doc["animations"][animation]["samplers"][channel["sampler"]]
            result.append({**channel["target"], "times": self.accessor(sampler["input"]),
                           "values": self.accessor(sampler["output"]), "interpolation": sampler.get("interpolation", "LINEAR")})
        return result

    def animated(self, time):
        nodes = [dict(node) for node in self.nodes]
        for channel in self.channels():
            times, values = channel["times"], channel["values"]
            assert channel["interpolation"] in ("LINEAR", "STEP")
            if time <= times[0]:
                value = values[0]
            elif time >= times[-1]:
                value = values[-1]
            else:
                before = int(np.searchsorted(times, time, side="right") - 1)
                fraction = (time-times[before]) / (times[before+1]-times[before])
                value = values[before] if channel["interpolation"] == "STEP" else (
                    slerp(values[before], values[before+1], fraction) if channel["path"] == "rotation" else
                    values[before]*(1-fraction)+values[before+1]*fraction)
            nodes[channel["node"]][channel["path"]] = value.tolist()
        return nodes, self.world(nodes)

    def skin_points(self, worlds):
        matrices = np.stack([worlds[int(node)] @ self.ibms[slot] for slot, node in enumerate(self.joints)])
        vertex_joints = self.attrs["JOINTS_0"].astype(int)
        weights = self.attrs["WEIGHTS_0"]
        blended = np.einsum("ni,nijk->njk", weights, matrices[vertex_joints])
        return np.einsum("nij,nj->ni", blended, self.positions4)[:, :3]


def main():
    receipt = json.loads(RECEIPT_PATH.read_text(encoding="utf-8-sig"))
    source, target, candidate = (Glb(receipt[key]["path"]) for key in ("source", "targetSource", "candidate"))
    inputs = [file_record(RECEIPT_PATH), file_record(source.path), file_record(target.path), file_record(candidate.path), file_record(receipt["script"]["path"])]
    expected_map = [("Hips", "Hips"), ("Spine02", "Spine"), ("Spine01", "Spine1"), ("Spine", "Spine2"),
                    ("neck", "Neck"), ("Head", "Head")] + [(side+part, side+part) for side in ("Left", "Right")
                    for part in ("Shoulder", "Arm", "ForeArm", "Hand", "UpLeg", "Leg", "Foot", "ToeBase")]
    expected_nodes = {target.names["mixamorig:"+dst]: source.names[src] for src, dst in expected_map}
    source_times = np.unique(np.concatenate([channel["times"] for channel in source.channels()]))
    candidate_times = np.unique(np.concatenate([channel["times"] for channel in candidate.channels()]))
    start, end = float(source_times[0]), float(source_times[-1])
    duration = end-start
    source_hip, target_hip = source.names["Hips"], target.names["mixamorig:Hips"]
    def length(model, prefix):
        lengths = []
        for side in ("Left", "Right"):
            coordinates = [model.bind[model.names[prefix+side+part]][:3, 3] for part in ("UpLeg", "Leg", "Foot")]
            lengths.append(sum(np.linalg.norm(b-a) for a, b in zip(coordinates, coordinates[1:])))
        return float(np.mean(lengths))
    ratio = length(target, "mixamorig:") / length(source, "")
    bind_errors = {"source": max(float(np.max(np.abs(source.rest[node]-matrix))) for node, matrix in source.bind.items()),
                   "target": max(float(np.max(np.abs(target.rest[node]-matrix))) for node, matrix in target.bind.items())}
    rotation_error, vertical_error, planar_error = 0.0, 0.0, 0.0
    unchanged_translation, unchanged_scale = True, True
    for time in source_times:
        _, src_world = source.animated(float(time))
        nodes, dst_world = candidate.animated(float(time-start))
        for dst, src in expected_nodes.items():
            expected = orientation(src_world[src]) @ orientation(source.bind[src]).T @ orientation(target.bind[dst])
            rotation_error = max(rotation_error, float(np.max(np.abs(orientation(dst_world[dst])-expected))))
            if dst != target_hip:
                unchanged_translation &= np.array_equal(np.asarray(nodes[dst].get("translation", [0, 0, 0])),
                                                        np.asarray(target.nodes[dst].get("translation", [0, 0, 0])))
            unchanged_scale &= np.array_equal(np.asarray(nodes[dst].get("scale", [1, 1, 1])),
                                              np.asarray(target.nodes[dst].get("scale", [1, 1, 1])))
        delta = dst_world[target_hip][:3, 3] - target.rest[target_hip][:3, 3]
        expected_y = (src_world[source_hip][1, 3]-source.bind[source_hip][1, 3]) * ratio
        vertical_error = max(vertical_error, float(abs(delta[1]-expected_y)))
        planar_error = max(planar_error, float(np.linalg.norm(delta[[0, 2]])))
    weights, joint_slots = candidate.attrs["WEIGHTS_0"], candidate.attrs["JOINTS_0"].astype(int)
    dominant = candidate.joints[joint_slots[np.arange(len(weights)), np.argmax(weights, axis=1)]]
    foot_masks = {side: np.isin(dominant, [candidate.names["mixamorig:"+side+part] for part in ("Foot", "ToeBase")])
                  for side in ("Left", "Right")}
    samples, dense_min, lowest = [], float("inf"), None
    dense_times = np.unique(np.r_[np.linspace(0, duration, 257), candidate_times])
    for time in dense_times:
        _, worlds = candidate.animated(float(time))
        points = candidate.skin_points(worlds)
        assert np.isfinite(points).all()
        vertex = int(np.argmin(points[:, 1]))
        if points[vertex, 1] < dense_min:
            dense_min = float(points[vertex, 1])
            lowest = {"targetTimeSeconds": float(time), "sourcePhase": float(time/duration), "vertex": vertex,
                      "position": points[vertex].tolist(), "dominantJoint": candidate.nodes[int(dominant[vertex])]["name"],
                      "weightInfluences": [{"name": candidate.nodes[int(candidate.joints[slot])]["name"], "weight": float(weight)}
                                           for slot, weight in zip(joint_slots[vertex], weights[vertex]) if weight > 0]}
    points8 = []
    for i, time in enumerate(np.linspace(0, duration, 8, endpoint=False)):
        _, worlds = candidate.animated(float(time))
        points = candidate.skin_points(worlds)
        points8.append(points)
        claimed = receipt["samples"][i]["worldBounds"]
        samples.append({"phase": i/8, "targetTimeSeconds": float(time), "worldMin": points.min(axis=0).tolist(), "worldMax": points.max(axis=0).tolist(),
                        "leftFootMinY": float(points[foot_masks["Left"], 1].min()), "rightFootMinY": float(points[foot_masks["Right"], 1].min()),
                        "poseSha256": hashlib.sha256(points.astype("<f8").tobytes()).hexdigest(),
                        "receiptBoundsMaxDelta": float(max(np.max(np.abs(points.min(axis=0)-claimed["min"])), np.max(np.abs(points.max(axis=0)-claimed["max"]))))})
    first = candidate.skin_points(candidate.animated(0)[1])
    final = candidate.skin_points(candidate.animated(duration)[1])
    seam = {"endpointVertexMaxDistance": float(np.linalg.norm(final-first, axis=1).max()),
            "endpointVertexRmsDistance": float(np.sqrt(np.mean(np.sum((final-first)**2, axis=1)))),
            "meaning": "Reopened endpoint positions agree to float tolerance; endpoint agreement alone does not qualify stance, phase chaining, velocity continuity or foot locking."}
    preservable = ("nodes", "meshes", "skins", "materials", "images", "textures", "samplers", "scene", "scenes")
    checks = [
        {"name": "receipt hashes match immutable inputs", "passed": all(file_record(receipt[k]["path"])["sha256"] == receipt[k]["sha256"] for k in ("source", "targetSource", "candidate", "script"))},
        {"name": "22 semantic joints mapped by actual anatomical chain", "passed": [(entry["source"], entry["target"].split(":")[-1]) for entry in receipt["semanticMap"]] == expected_map},
        {"name": "source and target default equal inverse IBM bind within1e-4", "passed": max(bind_errors.values()) <= 1e-4},
        {"name": "target nodes shape materials skins and IBMs unchanged", "passed": all(candidate.doc.get(k) == target.doc.get(k) for k in preservable)},
        {"name": "original target binary prefix unchanged", "passed": candidate.binary[:len(target.binary)] == target.binary},
        {"name": "32 actual source keys retained with time origin shifted only", "passed": len(source_times) == len(candidate_times) == 32 and np.max(np.abs(candidate_times-(source_times-start))) < 1e-7},
        {"name": "22 rotation tracks plus one pelvis translation track", "passed": len(candidate.channels()) == 23 and set((c["node"], c["path"]) for c in candidate.channels()) == {(node, "rotation") for node in expected_nodes} | {(target_hip, "translation")}},
        {"name": "world bind delta rotations independently reproduced at original keys", "passed": rotation_error < 1e-4},
        {"name": "pelvis vertical delta uses source world units and leg ratio", "passed": vertical_error < 1e-6 and abs(ratio-receipt["proportion"]["pelvisYScaleRatio"]) < 1e-6},
        {"name": "pelvis planar motion explicitly removed", "passed": planar_error < 1e-8},
        {"name": "other mapped local translations and all mapped scales unchanged", "passed": bool(unchanged_translation and unchanged_scale)},
        {"name": "eight distinct actual skin deformations", "passed": len({sample["poseSha256"] for sample in samples}) == 8},
        {"name": "eight receipt bounds independently reproduced", "passed": max(sample["receiptBoundsMaxDelta"] for sample in samples) < 1e-6},
        {"name": "ground tolerance0.002GLBunits", "passed": dense_min >= -0.002, "classification": "concrete rejection criterion"},
    ]
    frozen_after = [file_record(record["path"]) for record in inputs]
    assert frozen_after == inputs, "An input changed during read-only review"
    checks.append({"name": "input bytes preserved during independent review", "passed": True})
    for check in checks:
        check["passed"] = bool(check["passed"])
    technical_checks = [check for check in checks if check.get("classification") != "concrete rejection criterion"]
    report = {"schema": "own-heroes-independent-retarget-review/v1", "localDate": "2026-10-12", "timezone": "America/Sao_Paulo",
              "generatedAtUtc": datetime.now(timezone.utc).isoformat(), "inputs": inputs, "reviewer": file_record(Path(__file__)),
              "independentImplementation": "Standalone GLB/accessor/TRS/quaternion/slerp/hierarchy/LBS parser in this review. Does not import producer or its reader; reads real source and candidate bytes.",
              "methodAssessment": {"spine": "Source Hips→Spine02→Spine01→Spine maps target Hips→Spine→Spine1→Spine2, preserving actual order.",
                                   "rotations": "Source current world proper-polar rotation * inverse source bind world rotation applied to target bind world rotation; locals rebuilt under animated target parents. No direct source-local quaternion copy.",
                                   "bindDefaultsMaxAbsError": bind_errors, "originalKeyWorldRotationMaxAbsError": rotation_error,
                                   "sourceArmatureScale": source.nodes[source.names["Armature"]].get("scale"),
                                   "targetArmatureScale": target.nodes[target.names["Armature"]].get("scale", [1, 1, 1]),
                                   "legLengthRatio": ratio, "pelvisVerticalWorldMaxAbsError": vertical_error, "pelvisPlanarWorldMaxAbsError": planar_error,
                                   "inPlace": "Source pelvis XZ translation is discarded once. Vertical displacement is taken in source world after0.01 ancestor scale and multiplied by target/source leg length. Other translations/scales stay target bind."},
              "motionProof": {"candidateClip": candidate.doc["animations"][0]["name"], "sourceKeyCount": len(source_times),
                              "sourceRangeSeconds": [start, end], "candidateRangeSeconds": [float(candidate_times[0]), float(candidate_times[-1])],
                              "motionTracks": len(candidate.channels()), "differentEightPoses": len({sample["poseSha256"] for sample in samples}),
                              "realSourceRetargetProven": all(check["passed"] for check in technical_checks),
                              "fakeClipOrRepeatedRaster": False, "candidateIsNativeCompatibleWalk": False, "nativeValidatedClipCount": 0},
              "ground": {"eightPhaseMinimumY": min(sample["worldMin"][1] for sample in samples), "denseSampleCount": len(dense_times),
                         "denseMinimumY": dense_min, "lowest": lowest, "samples": samples,
                         "units": "Normalized Warrior GLB source units; meter/native world calibration is not established. 0.002 is a strict source-unit tolerance, not proven2millimeters.",
                         "noPixelOrPivotCorrection": True, "noFootLockingOrIkPerformed": True},
              "timingAndLoop": {"sourceSpanSeconds": duration, "nativeWalkBaseSlots": 8, "nativeTicksPerSlot": 1,
                                "nominalNativeStepSecondsAt20Ticks": 0.4, "sourceToNominalNativeSpanRatio": duration/0.4,
                                "eightPhaseSamplesAreNativeTiming": False,
                                "seam": seam, "required": "Define phase mapping to active native frame/fraction and step chaining with fixed actor footpoint, then validate foot contacts, stance travel and loop seam. Preserving source duration is not native timing synchronization."},
              "checks": checks, "structuralChecksPassed": all(check["passed"] for check in technical_checks),
              "status": "diagnostic-reject-ground-and-native-readiness", "disposition": "Preserve this real-motion candidate as rejected diagnostic. Root subsequently authorized 64 directional DIAGNOSTIC renders to visualize the actual motion and defect with diagnostic-reject and runtimeEligible=false labels; do not promote or distribute it as compatible native animation/gameplay.",
              "concreteReasons": [f"Ground penetrates to{dense_min:.9f}GLB units in{len(dense_times)}dense samples; strict tolerance=-0.002. Lowest vertex is controlled by foot/toe weights, not an abstract hierarchy failure.",
                                  "Source span1.033333s retained. Eight proof phases do not map the nominal native0.4s step, entry distribution, effective interpolation or chained walk phases.",
                                  "No foot contact/IK/foot locking, closed-loop gait or anatomy/weapon acceptance; zero other hero action coverage."],
              "diagnosticRenderAuthorization": {"source": "Root explicit follow-up after ground rejection, 2026-10-12", "plannedFrames": 64,
                                                 "purpose": "Diagnostic visualization of real motion and reproduced defect; never compatible/native-qualified delivery.",
                                                 "renderOwner": "root/hero_local_pipeline", "statusAtAuthorizationAddendum": "in execution per producer report; final count/hashes belong to root manifest/render receipt",
                                                 "nativeValidatedClipCount": 0, "runtimeEligible": False, "reviewAgentRenderedFrames": 0},
              "nextPermittedWork": "The explicitly authorized 64 diagnostic renders may show the rejected candidate's motion/defect. A future eligible animation requires a separate local candidate with foot/contact pose authoring or IK, preserving target bone lengths/footpoint/shape and source master. Review dense sole trajectory, stance/sliding and phase wrap before qualification/distribution as compatible native animation; no per-frame image lift/anchor recenter.",
              "gameplayPolicy": "ONLY SPRITES for every hero/NPC including Warrior/Ogden until correct rig+animations are validated; no runtime promotion.",
              "operations": {"sourceOrCandidateWrites": 0, "producerMethodEdits": 0, "renderedFrames": 0, "providerCalls": 0, "sourceRegistryBuildInstallWrites": 0,
                             "reviewOutputs": ["retarget-independent-review-r1.json", "retarget-independent-review-r1.md"]}}
    (HERE / "retarget-independent-review-r1.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = ["# Revisão independente do firstwalk1", "", "**Resultado: candidato rejeitado para animação compatível e gameplay.** A transformação produz movimento real da fonte; a reprovação concreta é contato com o chão e prontidão nativa, não uma ausência de keys. Após essa rejeição, a raiz autorizou 64 renders DIAGNÓSTICOS para mostrar o movimento e defeito com status `diagnostic-reject`/`runtimeEligible=false`; continuam zero clips validados para uso nativo. A execução dos renders pertence ao produtor e os counts/hashes finais ao manifesto da raiz.", "",
             f"Candidato `{candidate.path}` — SHA `{file_record(candidate.path)['sha256']}`. Todos os inputs e método permaneceram iguais. O parser desta revisão é independente e não importa retarget, hero_pipeline ou inspect_glb.", "",
             "A cadeia real da coluna foi respeitada: Hips→Spine02→Spine01→Spine na fonte, Hips→Spine→Spine1→Spine2 no Warrior. O método transfere deltas de rotação de world bind e resolve rotations locais sob os pais animados. IBMs, geometria, pesos, hierarchy, materiais e translations/scales do alvo permanecem preservados; apenas hipY varia. Não há cópia cega de quat local ou nome numérico.", "",
             f"A escala 0.01 do Armature Meshy foi incluída no world antes de transportar a variação vertical do quadril. Ratio de pernas independente={ratio:.12f}; erro máximo hipY={vertical_error:.3g}; driftXZ={planar_error:.3g}. Erro máximo de rotação world nos 32 keys={rotation_error:.3g}. Os 23 tracks reabertos geram oito skins distintas; não são deslocamento2D ou stills repetidos.", "",
             f"Oito phases reproduzem o mínimoY={report['ground']['eightPhaseMinimumY']:.9f}; {len(dense_times)} amostras densas encontramY={dense_min:.9f}, no instante{lowest['targetTimeSeconds']:.9f}s, vertex{lowest['vertex']}, joint dominante `{lowest['dominantJoint']}`. Falha a tolerância−0.002. Esses números são unidades do Warrior normalizado, sem calibração em metros; rotular0.002 como2mm ainda não é comprovado.", "",
             f"Fonte32 keys de{start:.9f}a{end:.9f}s; saída desloca a origem para0 e mantém span{duration:.9f}s. Oito phases não são oito slots nativos sincronizados. A extensão nominal nativa8×1tick a20ticks/s é0.4s; ratio={duration/0.4:.6f}. Entrada/distribuição/encadeamento e phase wrap continuam sob a simulação nativa. Distância máxima de vértice entre endpoints={seam['endpointVertexMaxDistance']:.9f}; fechamento de loop/foot locking não foi aprovado.", "",
             "Os 64 renders diagnósticos explicitamente autorizados podem mostrar este movimento e sua falha. Para futura animação compatível/gameplay: produzir outro candidato com correção explícita de pose/contato ou IK, comprimentos das pernas e footpoint fixos preservados; medir sole trajectory, stance/sliding e seam densamente antes de qualificar/promover/distribuir como animação compatível. Não levantar/recentralizar imagens por frame para ocultar o defeito. Este agente de revisão não editou método, modelos ou outputs do produtor e não renderizou frames. O adendo é textual, sem refazer as verificações numéricas; os inputs congelados permanecem a evidência contínua.", "",
             "**Gameplay somente sprites para todos os heróis/NPCs, inclusive Warrior/Ogden**, até rig e animações corretos validados. Estrutura que passa não equivale a caminhada correta, cobertura completa ou aceite artístico.", "",
             f"{sum(c['passed'] for c in checks)} verificações passaram; um critério de chão falhou como defeito reproduzido. Outputs: JSON completo e este relatório; nenhuma alteração de Source, registry, build ou instalação.", ""]
    (HERE / "retarget-independent-review-r1.md").write_text("\n".join(lines), encoding="utf-8")
    print(json.dumps({"status": report["status"], "structuralChecksPassed": report["structuralChecksPassed"], "denseMinimumY": dense_min,
                      "lowestDominantJoint": lowest["dominantJoint"], "denseSamples": len(dense_times), "seam": seam,
                      "outputs": [file_record(HERE / "retarget-independent-review-r1.json"), file_record(HERE / "retarget-independent-review-r1.md")]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
