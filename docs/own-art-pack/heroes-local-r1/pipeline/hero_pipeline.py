#!/usr/bin/env python3
"""Offline CPU GLB -> honest RGBA hero stills/clips. No GPU, network or source edits.

Uses the established repository GLB reader. Only actual GLB TRS animation can
produce an animation deliverable. Bind stills are never labelled idle/walk.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path
import sys

import numpy as np
from PIL import Image


# Source/engine/direction.hpp order. +Z is authored front; fixed camera is +Z.
DIRECTIONS = [("s", 0), ("sw", -45), ("w", -90), ("nw", -135),
              ("n", -180), ("ne", -225), ("e", -270), ("se", -315)]
ROOT = Path(__file__).resolve().parents[4]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def load_reader(repo):
    path = repo / "tools" / "inspect_glb.py"
    spec = importlib.util.spec_from_file_location("heroes_local_glb_reader", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, {"path": str(path), "sha256": sha(path.read_bytes())}


def bbox(points):
    return {"min": points.min(axis=0).tolist(), "max": points.max(axis=0).tolist(),
            "extent": np.ptp(points, axis=0).tolist()}


def finite(values, name):
    if not np.isfinite(values).all():
        raise ValueError(name + " contains non-finite values")
    return values


def interpolate(times, values, method, time, quaternion=False):
    """glTF STEP/LINEAR/CUBICSPLINE; quaternion LINEAR uses shortest-arc slerp."""
    if time <= times[0]:
        result = values[1] if method == "CUBICSPLINE" else values[0]
    elif time >= times[-1]:
        result = values[-2] if method == "CUBICSPLINE" else values[-1]
    else:
        k = int(np.searchsorted(times, time, side="right") - 1)
        dt = times[k + 1] - times[k]
        u = (time - times[k]) / dt
        if method == "STEP":
            result = values[k]
        elif method == "CUBICSPLINE":
            a, b = values[3*k+1], values[3*(k+1)+1]
            ta, tb = values[3*k+2] * dt, values[3*(k+1)] * dt
            result = (2*u**3-3*u**2+1)*a + (u**3-2*u**2+u)*ta + (-2*u**3+3*u**2)*b + (u**3-u**2)*tb
        elif quaternion:
            a, b = values[k], values[k+1]
            dot = float(a @ b)
            if dot < 0:
                b, dot = -b, -dot
            if dot > 0.9995:
                result = a*(1-u) + b*u
            else:
                angle = np.arccos(np.clip(dot, -1, 1))
                result = (np.sin((1-u)*angle)*a + np.sin(u*angle)*b) / np.sin(angle)
        else:
            result = values[k]*(1-u) + values[k+1]*u
    if quaternion:
        length = np.linalg.norm(result)
        if length <= 1e-12:
            raise ValueError("Quaternion interpolation produced zero length")
        result = result / length
    return result


class Model:
    def __init__(self, path, reader):
        self.path, self.reader_module = path.resolve(), reader
        self.doc, binary, self.chunks = reader.read_glb(self.path)
        self.reader = reader.Reader(self.path, self.doc, binary)
        self.nodes = self.doc.get("nodes", [])
        self.parents = [None] * len(self.nodes)
        self.locals = [finite(reader.node_matrix(n), "node matrix") for n in self.nodes]
        for i, node in enumerate(self.nodes):
            for child in node.get("children", []):
                if not isinstance(child, int) or not 0 <= child < len(self.nodes) or self.parents[child] is not None:
                    raise ValueError("Invalid child index or multiple parents")
                self.parents[child] = i
        self.rest_world = self.world(self.locals)
        self.skins = []
        for skin in self.doc.get("skins", []):
            joints = np.asarray(skin["joints"], dtype=int)
            if not len(joints) or len(joints) != len(set(joints.tolist())) or np.any(joints < 0) or np.any(joints >= len(self.nodes)):
                raise ValueError("Invalid skin joints")
            ibms = (self.array(skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1)
                    if "inverseBindMatrices" in skin else np.repeat(np.eye(4)[None], len(joints), axis=0))
            if len(ibms) < len(joints) or np.any(np.abs(np.linalg.det(ibms)) < 1e-12):
                raise ValueError("Invalid/singular inverse bind matrices")
            self.skins.append((joints, ibms[:len(joints)]))
        self.primitives = []
        meshes = self.doc.get("meshes", [])
        for ni, node in enumerate(self.nodes):
            if "mesh" not in node:
                continue
            for primitive in meshes[node["mesh"]].get("primitives", []):
                if primitive.get("targets"):
                    raise ValueError("Morph targets need a dedicated evaluator; not silently ignored")
                if primitive.get("extensions", {}).get("KHR_draco_mesh_compression"):
                    raise ValueError("Draco requires a dedicated decoder")
                attrs = primitive["attributes"]
                pos = self.array(attrs["POSITION"]).astype(float)
                count = len(pos)
                indices = self.array(primitive["indices"]).reshape(-1).astype(int) if "indices" in primitive else np.arange(count)
                if np.any(indices < 0) or np.any(indices >= count):
                    raise ValueError("Primitive indices out of range")
                pairs = []
                skin = node.get("skin")
                if skin is not None:
                    if not 0 <= skin < len(self.skins) or "JOINTS_0" not in attrs or "WEIGHTS_0" not in attrs:
                        raise ValueError("Skinned mesh lacks a valid skin/weights")
                    sets = sorted(k for k in attrs if k.startswith("JOINTS_"))
                    for jname in sets:
                        suffix = jname.split("_")[-1]
                        j = self.array(attrs[jname]).astype(int)
                        w = self.array(attrs["WEIGHTS_" + suffix]).astype(float)
                        if j.shape != (count, 4) or w.shape != (count, 4) or np.any(j < 0) or np.any(j >= len(self.skins[skin][0])) or np.any(w < 0):
                            raise ValueError("Invalid JOINTS/WEIGHTS arrays")
                        pairs.append((j, w))
                    combined = np.concatenate([p[1] for p in pairs], axis=1)
                    if np.max(np.abs(combined.sum(axis=1) - 1)) > 1e-4 or np.max((combined > 1e-7).sum(axis=1)) > 4:
                        raise ValueError("Weight sums or 4-influence contract failed")
                normals = self.array(attrs["NORMAL"]).astype(float) if "NORMAL" in attrs else None
                uv = self.array(attrs["TEXCOORD_0"]).astype(float) if "TEXCOORD_0" in attrs else np.zeros((count, 2))
                colors = self.array(attrs["COLOR_0"]).astype(float) if "COLOR_0" in attrs else np.ones((count, 4))
                if colors.shape[1] == 3:
                    colors = np.column_stack([colors, np.ones(count)])
                self.primitives.append({"node": ni, "skin": skin, "positions": pos, "normals": normals,
                                        "uv": uv, "colors": colors, "pairs": pairs,
                                        "triangles": reader.triangles(indices, primitive.get("mode", 4)),
                                        "material": primitive.get("material", -1)})
        if not self.primitives:
            raise ValueError("No renderable primitives")
        self.animations = []
        for ai, animation in enumerate(self.doc.get("animations", [])):
            channels, targets = [], set()
            for channel in animation.get("channels", []):
                target = channel["target"]
                ni, prop = target["node"], target["path"]
                if prop not in ("translation", "rotation", "scale") or "matrix" in self.nodes[ni]:
                    raise ValueError("Only TRS-node animation is supported; morph/matrix target refused")
                if (ni, prop) in targets:
                    raise ValueError("Duplicate animation target")
                targets.add((ni, prop))
                s = animation["samplers"][channel["sampler"]]
                t = self.array(s["input"]).reshape(-1).astype(float)
                v = self.array(s["output"]).astype(float)
                method = s.get("interpolation", "LINEAR")
                if method not in ("STEP", "LINEAR", "CUBICSPLINE") or not len(t) or np.any(t < 0) or np.any(np.diff(t) <= 0):
                    raise ValueError("Invalid animation times/interpolation")
                if v.shape != (len(t)*(3 if method == "CUBICSPLINE" else 1), 4 if prop == "rotation" else 3):
                    raise ValueError("Invalid animation output dimensions")
                if method == "CUBICSPLINE" and len(t) < 2:
                    raise ValueError("CUBICSPLINE requires two keys")
                if prop == "rotation":
                    q = v[1::3] if method == "CUBICSPLINE" else v
                    if np.max(np.abs(np.linalg.norm(q, axis=1)-1)) > .001:
                        raise ValueError("Non-unit animation rotation")
                channels.append({"node": ni, "path": prop, "times": t, "values": v, "method": method})
            start = min((c["times"][0] for c in channels), default=0)
            end = max((c["times"][-1] for c in channels), default=0)
            self.animations.append({"index": ai, "name": animation.get("name", f"animation-{ai}"),
                                    "channels": channels, "start": float(start), "end": float(end)})
        self.textures, self.materials = {}, {}

    def array(self, index):
        return finite(self.reader.accessor(index), "accessor")

    def world(self, locals_):
        result, visiting = {}, set()
        def visit(i):
            if i in result:
                return result[i]
            if i in visiting:
                raise ValueError("Node hierarchy cycle")
            visiting.add(i)
            p = self.parents[i]
            result[i] = locals_[i] if p is None else visit(p) @ locals_[i]
            visiting.remove(i)
            return result[i]
        for i in range(len(self.nodes)):
            visit(i)
        return result

    def pose(self, animation=None, time=0):
        if animation is None:
            return self.rest_world
        modified = {}
        for channel in animation["channels"]:
            ni = channel["node"]
            modified.setdefault(ni, dict(self.nodes[ni]))
            modified[ni][channel["path"]] = interpolate(channel["times"], channel["values"], channel["method"], time,
                                                       channel["path"] == "rotation").tolist()
        locals_ = list(self.locals)
        for ni, node in modified.items():
            locals_[ni] = self.reader_module.node_matrix(node)
        return self.world(locals_)

    def geometry(self, world):
        result = []
        for p in self.primitives:
            pos = np.column_stack([p["positions"], np.ones(len(p["positions"]))])
            if p["skin"] is None:
                matrices = np.repeat(world[p["node"]][None], len(pos), axis=0)
            else:
                joints, ibms = self.skins[p["skin"]]
                palette = np.asarray([world[int(j)] @ ibms[n] for n, j in enumerate(joints)])
                matrices = np.zeros((len(pos), 4, 4))
                for js, ws in p["pairs"]:
                    for slot in range(4):
                        matrices += palette[js[:, slot]] * ws[:, slot, None, None]
            vertices = np.einsum("vij,vj->vi", matrices, pos)[:, :3]
            normals = None
            if p["normals"] is not None:
                # Inverse transpose of the blended deformation, correct for the
                # actual CPU deformation even when scale is present.
                normal_matrix = np.linalg.inv(matrices[:, :3, :3]).transpose(0, 2, 1)
                normals = np.einsum("vij,vj->vi", normal_matrix, p["normals"])
                normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
            result.append({**p, "vertices": finite(vertices, "deformed vertices"), "world_normals": normals})
        return result

    def load_materials(self):
        for i, material in enumerate(self.doc.get("materials", [])):
            m = dict(material)
            pbr = m.get("pbrMetallicRoughness", {})
            info = pbr.get("baseColorTexture")
            m["factor"] = np.asarray(pbr.get("baseColorFactor", [1, 1, 1, 1]))
            m["texture"] = None
            if info:
                if info.get("texCoord", 0) != 0 or info.get("extensions"):
                    raise ValueError("Nondefault texture coordinates/texture transforms need a dedicated evaluator")
                texture = self.doc["textures"][info["index"]]
                if "source" not in texture:
                    raise ValueError("Compressed textures need a dedicated decoder")
                image_index = texture["source"]
                if image_index not in self.textures:
                    image = self.doc["images"][image_index]
                    data = self.reader.uri_bytes(image["uri"]) if "uri" in image else self.reader.view(image["bufferView"])[0]
                    self.textures[image_index] = np.asarray(Image.open(io.BytesIO(data)).convert("RGBA"))
                m["texture"] = self.textures[image_index]
                m["sampler"] = self.doc.get("samplers", [])[texture["sampler"]] if "sampler" in texture else {}
            if m.get("alphaMode", "OPAQUE") == "BLEND":
                raise ValueError("BLEND material needs sorted transparency; not silently made opaque")
            self.materials[i] = m

    def clip_summary(self, animation):
        keys = np.unique(np.concatenate([c["times"] for c in animation["channels"]])) if animation["channels"] else np.array([0.])
        if len(keys) > 129:
            keys = keys[np.linspace(0,len(keys)-1,129).astype(int)]
        times = np.unique(np.r_[np.linspace(animation["start"], animation["end"], 5),keys])
        signatures = [sha(np.concatenate([p["vertices"] for p in self.geometry(self.pose(animation, float(t)))]).astype("<f8").tobytes()) for t in times]
        return {"index": animation["index"], "name": animation["name"], "rangeSeconds": [animation["start"], animation["end"]],
                "channels": len(animation["channels"]), "keyCounts": [len(c["times"]) for c in animation["channels"]],
                "hasSampledDeformation": len(set(signatures)) > 1,
                "sampledPoseCount":len(times),
                "proof": "actual source key times plus five phase samples (keys capped at129); no 2D offsets or duplicate-frame animation"}

    def report(self):
        vertices = np.concatenate([p["vertices"] for p in self.geometry(self.rest_world)])
        dependencies = []
        for kind in ("buffers", "images"):
            for i, item in enumerate(self.doc.get(kind, [])):
                if item.get("uri") and not item["uri"].startswith("data:"):
                    data = self.reader.uri_bytes(item["uri"])
                    dependencies.append({"kind": kind, "index": i, "uri": item["uri"], "bytes": len(data), "sha256": sha(data)})
        return {"source": {"path": str(self.path), "bytes": self.path.stat().st_size, "sha256": sha(self.path.read_bytes()), "dependencies": dependencies},
                "counts": {"nodes": len(self.nodes), "skinJoints": [len(s[0]) for s in self.skins], "clips": len(self.animations),
                           "vertices": sum(len(p["positions"]) for p in self.primitives), "triangles": sum(len(p["triangles"]) for p in self.primitives)},
                "bindWorldBounds": bbox(vertices), "animations": [self.clip_summary(a) for a in self.animations],
                "status": "candidate with real clips" if self.animations else "master/rig only; zero animation clips",
                "materials": "real source UV/base color; CPU renderer below explicitly approximates directional diffuse lighting",
                "approval": "technical candidate; no selection, visual acceptance or game integration"}


def wrap(values, mode):
    if mode == 33071:
        return np.clip(values, 0, 1)
    if mode == 33648:
        return 1 - np.abs(np.mod(values, 2)-1)
    if mode != 10497:
        raise ValueError("Unsupported texture wrapping")
    return np.mod(values, 1)


def srgb_to_linear(v):
    return np.where(v <= .04045, v/12.92, ((v+.055)/1.055)**2.4)


def linear_to_srgb(v):
    return np.where(v <= .0031308, v*12.92, 1.055*np.maximum(v, 0)**(1/2.4)-.055)


def raster(model, geometry, yaw, size, pivot, footpoint, ppm, elevation, supersample, unlit):
    """Orthographic textured triangle raster. Constant camera, pivot and scale."""
    width, height = (v*supersample for v in size)
    pivot = np.asarray(pivot)*supersample
    ppm *= supersample
    theta, pitch = math.radians(yaw), math.radians(elevation)
    rotation = np.array([[math.cos(theta), 0, math.sin(theta)], [0, 1, 0], [-math.sin(theta), 0, math.cos(theta)]])
    right, up, eye = np.array([1, 0, 0]), np.array([0, math.cos(pitch), -math.sin(pitch)]), np.array([0, math.sin(pitch), math.cos(pitch)])
    light = np.array([-.4, .8, .5]); light /= np.linalg.norm(light)
    rgba = np.zeros((height, width, 4), dtype=np.uint8)
    depth = np.full((height, width), -np.inf)
    culled = drawn = degenerate = 0
    for primitive in geometry:
        physical = (primitive["vertices"]-footpoint) @ rotation.T
        normals = primitive["world_normals"] @ rotation.T if primitive["world_normals"] is not None else None
        screen = np.column_stack([pivot[0]+ppm*(physical @ right), pivot[1]-ppm*(physical @ up), physical @ eye])
        material = model.materials.get(primitive["material"], {"factor": np.ones(4), "texture": None})
        for face in primitive["triangles"]:
            points = physical[face]
            face_normal = np.cross(points[1]-points[0], points[2]-points[0])
            if not material.get("doubleSided", False) and face_normal @ eye <= 0:
                culled += 1
                continue
            p = screen[face]
            left, right_edge = max(0, int(np.floor(p[:,0].min()))), min(width-1, int(np.ceil(p[:,0].max())))
            top, bottom = max(0, int(np.floor(p[:,1].min()))), min(height-1, int(np.ceil(p[:,1].max())))
            if left > right_edge or top > bottom:
                continue
            denominator = (p[1,1]-p[2,1])*(p[0,0]-p[2,0]) + (p[2,0]-p[1,0])*(p[0,1]-p[2,1])
            if abs(denominator) < 1e-8:
                degenerate += 1
                continue
            yy, xx = np.mgrid[top:bottom+1, left:right_edge+1]
            w0 = ((p[1,1]-p[2,1])*(xx+.5-p[2,0])+(p[2,0]-p[1,0])*(yy+.5-p[2,1]))/denominator
            w1 = ((p[2,1]-p[0,1])*(xx+.5-p[2,0])+(p[0,0]-p[2,0])*(yy+.5-p[2,1]))/denominator
            w2 = 1-w0-w1
            z = w0*p[0,2]+w1*p[1,2]+w2*p[2,2]
            active = (w0 >= -1e-6)&(w1 >= -1e-6)&(w2 >= -1e-6)&(z > depth[top:bottom+1,left:right_edge+1])
            if not active.any():
                continue
            weights = np.stack([w0[active],w1[active],w2[active]], axis=1)
            color = np.ones((len(weights), 4))
            if material["texture"] is not None:
                uv = weights @ primitive["uv"][face]
                sampler = material["sampler"]
                texture = material["texture"]
                u, v = wrap(uv[:,0],sampler.get("wrapS",10497)), wrap(uv[:,1],sampler.get("wrapT",10497))
                # Bilinear base-color sampling, texel centers follow glTF UVs.
                tx, ty = u*texture.shape[1]-.5, v*texture.shape[0]-.5
                x0, y0 = np.floor(tx).astype(int), np.floor(ty).astype(int)
                fx, fy = tx-x0, ty-y0
                def tex(dx,dy):
                    xi, yi = x0+dx, y0+dy
                    def address(index,count,mode):
                        if mode == 10497:
                            return np.mod(index,count)
                        if mode == 33648:
                            folded = np.mod(index,2*count)
                            return np.where(folded >= count,2*count-1-folded,folded)
                        return np.clip(index,0,count-1)
                    xi = address(xi,texture.shape[1],sampler.get("wrapS",10497))
                    yi = address(yi,texture.shape[0],sampler.get("wrapT",10497))
                    return texture[yi,xi].astype(float)/255
                color = tex(0,0)*((1-fx)*(1-fy))[:,None]+tex(1,0)*(fx*(1-fy))[:,None]+tex(0,1)*((1-fx)*fy)[:,None]+tex(1,1)*(fx*fy)[:,None]
            vertex_color = weights @ primitive["colors"][face]
            linear = srgb_to_linear(color[:,:3])*vertex_color[:,:3]*material["factor"][:3]
            if not unlit:
                if normals is not None:
                    n = weights @ normals[face]
                    n /= np.maximum(np.linalg.norm(n,axis=1,keepdims=True),1e-12)
                    lighting = .65+.35*np.maximum(n @ light,0)
                else:
                    face_normal /= max(np.linalg.norm(face_normal),1e-12)
                    lighting = .65+.35*max(float(face_normal @ light),0)
                linear *= np.asarray(lighting).reshape(-1,1)
            alpha = color[:,3]*vertex_color[:,3]*material["factor"][3]
            accepted = np.ones(len(weights),dtype=bool) if material.get("alphaMode","OPAQUE") == "OPAQUE" else alpha >= material.get("alphaCutoff",.5)
            row, col = np.nonzero(active)
            row, col = row[accepted]+top, col[accepted]+left
            rgba[row,col,:3] = np.clip(np.rint(linear_to_srgb(linear[accepted])*255),0,255).astype(np.uint8)
            rgba[row,col,3] = 255
            depth[row,col] = z[active][accepted]
            drawn += 1
    picture = Image.fromarray(rgba)
    if supersample != 1:
        picture = picture.resize(tuple(size), Image.Resampling.LANCZOS)
    output = np.asarray(picture)
    covered = output[:,:,3] > 0
    if not covered.any():
        raise ValueError("Raster has no covered pixels; winding/framing failure")
    ys, xs = np.nonzero(covered)
    return picture, {"drawnTriangles": drawn,"culledTriangles":culled,"degenerateTriangles":degenerate,
                     "coveredPixels":int(covered.sum()),"alphaBoundingBox":[int(xs.min()),int(ys.min()),int(xs.max())+1,int(ys.max())+1],
                     "touchesFrameBoundary":bool(covered[0].any() or covered[-1].any() or covered[:,0].any() or covered[:,-1].any()),
                     "distinctAlphaValues":int(len(np.unique(output[:,:,3]))),"rgbaPixelsSha256":sha(output.tobytes())}


def parse_numbers(text):
    return [float(x) for x in text.split(",")]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root",type=Path,default=ROOT)
    sub = parser.add_subparsers(dest="command",required=True)
    inspect = sub.add_parser("inspect")
    inspect.add_argument("models",type=Path,nargs="+")
    inspect.add_argument("--output",type=Path,required=True)
    render = sub.add_parser("render")
    render.add_argument("model",type=Path)
    render.add_argument("--output",type=Path,required=True)
    kind = render.add_mutually_exclusive_group(required=True)
    kind.add_argument("--bind-stills",action="store_true")
    kind.add_argument("--clip",help="Exact embedded clip name; bind stills are never an idle clip")
    render.add_argument("--state",help="Explicit native state binding, only when a real clip is supplied")
    render.add_argument("--frames",type=int,default=1)
    render.add_argument("--loop",action="store_true")
    render.add_argument("--footpoint",type=parse_numbers,required=True,help="Immutable source-world ground anchor x,y,z; no per-frame recenter")
    render.add_argument("--size",type=parse_numbers,default=[512,512])
    render.add_argument("--pivot",type=parse_numbers,default=[256,448])
    render.add_argument("--pixels-per-unit",type=float,default=400)
    render.add_argument("--elevation",type=float,default=30)
    render.add_argument("--front-yaw",type=float,default=0,help="Explicit authoring-axis correction; applies identically to all directions")
    render.add_argument("--supersample",type=int,default=2)
    render.add_argument("--unlit",action="store_true")
    args = parser.parse_args(argv)
    reader, dependency = load_reader(args.repo_root.resolve())
    tool = {"path":str(Path(__file__).resolve()),"sha256":sha(Path(__file__).read_bytes())}
    report = {"schemaVersion":1,"createdAtUtc":datetime.now(timezone.utc).isoformat(),"tool":tool,"reader":dependency,
              "policy":{"cpuOnly":True,"networkAccess":False,"sourceModified":False,"globalRegistryModified":False,
                        "sourcePixels":"only embedded own GLB textures; no sprite pack/reference-image input"}}
    if args.command == "inspect":
        report["models"] = [Model(path,reader).report() for path in args.models]
        write_json(args.output,report)
        print(json.dumps({"output":str(args.output),"models":len(report["models"]),"clips":[m["counts"]["clips"] for m in report["models"]]}))
        return 0
    if args.frames < 1 or args.frames > 240 or args.supersample not in (1,2,3,4) or len(args.size) != 2 or len(args.pivot) != 2 or len(args.footpoint) != 3 or args.pixels_per_unit <= 0:
        parser.error("Invalid frames/supersample/canvas/pivot/footpoint/scale")
    if args.bind_stills and (args.frames != 1 or args.state or args.loop):
        parser.error("Bind stills have one frame per direction and no animation state/loop")
    model = Model(args.model,reader)
    report["model"] = model.report()
    animation = None
    if args.clip:
        matching = [a for a in model.animations if a["name"] == args.clip]
        if len(matching) != 1 or args.frames < 2 or not args.state:
            parser.error("Animation export needs one exact embedded clip, >=2 frames, and an explicit state")
        animation = matching[0]
        if not model.clip_summary(animation)["hasSampledDeformation"]:
            parser.error("Clip has no demonstrated mesh deformation; not an animation deliverable")
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("Output directory is not empty; use a fresh revision")
    model.load_materials()
    size = [int(x) for x in args.size]
    if min(size) < 16 or max(size) > 4096 or not all(0 <= args.pivot[i] < size[i] for i in range(2)):
        parser.error("Canvas or fixed pivot out of range")
    times = [0.0] if animation is None else np.linspace(animation["start"],animation["end"],args.frames,endpoint=not args.loop).tolist()
    evaluated = [model.geometry(model.pose(animation,t)) for t in times]
    pose_hashes = [sha(np.concatenate([p["vertices"] for p in g]).astype("<f8").tobytes()) for g in evaluated]
    if animation is not None and len(set(pose_hashes)) < 2:
        parser.error("Requested sample times produced one pose; change sampling without fabricating motion")
    report.update({"status":"bind-stills-only-zero-clips" if animation is None else "rendered-real-clip-candidate",
                   "deliverableKind":"directional-master-stills" if animation is None else "real-GLB-animation-samples",
                   "nativeState":args.state,"clip":args.clip,"frameCountPerDirection":len(times),"directions":[],
                   "projection":{"type":"orthographic","modelFront":"+Z","camera":"fixed +Z elevated",
                                 "elevationDegrees":args.elevation,"frontYawDegrees":args.front_yaw,"canvas":size,
                                 "pixelPivot":args.pivot,"footpointSourceWorld":args.footpoint,"pixelsPerSourceUnit":args.pixels_per_unit,
                                 "uniformScale":True,"perFrameRecenter":False,"rootMotion":"preserved from actual clip; footpoint stays fixed"},
                   "renderer":{"supersample":args.supersample,"downsample":"RGBA premultiplied Pillow LANCZOS",
                               "lighting":"unlit" if args.unlit else "linear-light diffuse ambient .65 plus directional .35",
                               "limitations":["diffuse base color only; metallic/roughness/normal/emission maps not evaluated","no ground/contact shadows","candidate projection/scale requires native slot calibration"]},
                   "sourcePoseSha256":pose_hashes,"loopSampling":args.loop,
                   "simulation":"native states/events/timing remain authoritative; no game frame delay is inferred from source seconds"})
    args.output.mkdir(parents=True,exist_ok=True)
    for native_index,(direction,yaw) in enumerate(DIRECTIONS):
        directory = args.output / direction
        directory.mkdir()
        record = {"direction":direction,"nativeIndex":native_index,"modelYawDegrees":yaw+args.front_yaw,"frames":[]}
        for frame,(time,geometry) in enumerate(zip(times,evaluated)):
            picture,stats = raster(model,geometry,yaw+args.front_yaw,size,args.pivot,np.asarray(args.footpoint),args.pixels_per_unit,args.elevation,args.supersample,args.unlit)
            name = "bind.png" if animation is None else f"frame-{frame:03}.png"
            path = directory/name
            picture.save(path)
            record["frames"].append({"frame":frame,"sourceTimeSeconds":time,"file":str(path.resolve()),"bytes":path.stat().st_size,
                                     "sha256":sha(path.read_bytes()),"pixelPivot":args.pivot,**stats})
        report["directions"].append(record)
    report["validation"] = {"allNonempty":all(f["coveredPixels"] > 0 for d in report["directions"] for f in d["frames"]),
                            "allInsideCanvas":all(not f["touchesFrameBoundary"] for d in report["directions"] for f in d["frames"]),
                            "distinctDirectionPixels":len(set(f["rgbaPixelsSha256"] for d in report["directions"] for f in d["frames"])),
                            "poseCount":len(set(pose_hashes)),"visualReview":"pending; mechanical checks do not accept art"}
    write_json(args.output/"render-receipt.json",report)
    print(json.dumps({"output":str(args.output),"status":report["status"],"frames":len(times)*8,"validation":report["validation"]}))
    return 0 if report["validation"]["allInsideCanvas"] else 2


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, KeyError, IndexError, np.linalg.LinAlgError) as error:
        print(str(error),file=sys.stderr)
        sys.exit(1)
