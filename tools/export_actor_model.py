#!/usr/bin/env python3
"""Export a bounded isolated D3DACTR1 actor candidate from a local skinned GLB.

No installation, API calls or static architectural fit. Runtime integration is
separate. Loss of advanced material features requires explicit --diffuse-only.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import re
import struct
import tempfile
import os

import numpy as np
from PIL import Image
from inspect_glb import Reader, read_glb


class ActorExportError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ActorExportError(message)


def string(value: str) -> bytes:
    data = value.encode('utf-8')
    require(1 <= len(data) <= 127 and not any(ord(c) < 32 or ord(c) == 127 for c in value), 'Actor strings must be1..127 UTF8 bytes without controls')
    return struct.pack('<H', len(data)) + data


def finite(array, shape=None) -> np.ndarray:
    value = np.asarray(array, dtype=np.float64)
    require((shape is None or value.shape == shape) and np.isfinite(value).all(), 'Invalid shape or nonfinite numeric data')
    return value


def floats(array) -> bytes:
    value = finite(array)
    require(np.all(np.abs(value) <= np.finfo(np.float32).max), 'Numeric data exceeds finite float32 range')
    converted = value.astype('<f4')
    require(np.isfinite(converted).all(), 'Nonfinite float32 conversion')
    return converted.tobytes()


def export_actor(source: Path, destination: Path, *, asset_id: str, variant: str,
                 revision: str, diffuse_only: bool, texture_size: int) -> dict:
    source = source.resolve()
    destination = destination.resolve()
    require(source != destination, 'Input GLB cannot be overwritten')
    require(1 <= texture_size <= 2048, 'Texture budget must be1..2048')
    source_bytes = source.read_bytes()
    source_hash = hashlib.sha256(source_bytes).hexdigest()
    doc, binary, _ = read_glb(source)
    reader = Reader(source, doc, binary)
    nodes = doc.get('nodes', [])
    skins = doc.get('skins', [])
    require(1 <= len(nodes) <= 256 and len(skins) == 1, 'V1 requires1skin and1..256nodes')
    mesh_nodes = [(i, n) for i, n in enumerate(nodes) if 'mesh' in n]
    require(len(mesh_nodes) == 1 and mesh_nodes[0][1].get('skin') == 0, 'V1 requires exactly1skinned mesh instance')
    require(not doc.get('extensionsRequired'), 'Required glTF extensions are unsupported by this profile')
    parents = [-1] * len(nodes)
    for index, node in enumerate(nodes):
        require('matrix' not in node, 'Node matrices require an explicit TRS bake; no hidden decomposition')
        for child in node.get('children', []):
            require(isinstance(child, int) and 0 <= child < len(nodes) and child != index and parents[child] == -1,
                    'Invalid or shared node parent')
            parents[child] = index
    for index in range(len(nodes)):
        seen = set()
        cursor = index
        while cursor != -1:
            require(cursor not in seen, 'Cyclic node graph')
            seen.add(cursor)
            cursor = parents[cursor]
    node_records = []
    for index, node in enumerate(nodes):
        t = finite(node.get('translation', [0, 0, 0]), (3,))
        q = finite(node.get('rotation', [0, 0, 0, 1]), (4,))
        s = finite(node.get('scale', [1, 1, 1]), (3,))
        require(np.all(s > 0) and abs(np.linalg.norm(q) - 1) <= 1e-3, 'Nonpositive scale or nonunit rest quaternion')
        node_records.append(struct.pack('<i', parents[index]) + string(node.get('name') or f'node-{index}') + floats(np.r_[t, q, s]))
    joint_nodes = skins[0].get('joints', [])
    require(1 <= len(joint_nodes) <= 96 and len(set(joint_nodes)) == len(joint_nodes), 'Joint budget or uniqueness invalid')
    require(all(isinstance(i, int) and 0 <= i < len(nodes) for i in joint_nodes), 'Joint node out of range')
    ibms = reader.accessor(skins[0]['inverseBindMatrices']) if 'inverseBindMatrices' in skins[0] else np.tile(np.eye(4).T.reshape(1,16), (len(joint_nodes), 1))
    ibms = finite(ibms, (len(joint_nodes), 16))
    require(np.all(np.abs(ibms[:,[3,7,11]]) <= 1e-5) and np.all(np.abs(ibms[:,15] - 1) <= 1e-5), 'Inverse bind matrix must be affine')
    require(all(abs(np.linalg.det(row.reshape(4, 4).T)) > 1e-12 for row in ibms), 'Singular inverse bind matrix')
    joint_records = [struct.pack('<I', node) + floats(ibm) for node, ibm in zip(joint_nodes, ibms)]
    mesh = doc['meshes'][mesh_nodes[0][1]['mesh']]
    require(not mesh.get('weights'), 'Morph weights are unsupported')
    primitives = mesh.get('primitives', [])
    require(primitives and len({p.get('material', -1) for p in primitives}) == 1, 'V1 requires one material')
    vertices = bytearray()
    index_values = []
    vertex_count = 0
    for primitive in primitives:
        require(primitive.get('mode', 4) == 4 and not primitive.get('targets') and not primitive.get('extensions'), 'Triangles without morph/compression required')
        attr = primitive.get('attributes', {})
        require(all(key in attr for key in ('POSITION', 'NORMAL', 'TEXCOORD_0', 'JOINTS_0', 'WEIGHTS_0')), 'Position/normal/UV/skin data missing')
        p = finite(reader.accessor(attr['POSITION']))
        n = finite(reader.accessor(attr['NORMAL']), p.shape)
        uv = finite(reader.accessor(attr['TEXCOORD_0']), (len(p), 2))
        j = finite(reader.accessor(attr['JOINTS_0']), (len(p), 4))
        w = finite(reader.accessor(attr['WEIGHTS_0']), (len(p), 4))
        require(p.ndim == 2 and p.shape[1] == 3 and np.all(np.linalg.norm(n, axis=1) > 1e-8), 'Invalid positions/normals')
        require(np.all(j == np.floor(j)) and np.all(j >= 0) and np.all(j < len(joint_nodes)), 'Invalid palette indices')
        require(np.all(w >= 0) and np.max(np.abs(w.sum(axis=1) - 1)) <= 1e-3, 'Skin weights not normalized')
        for key in ('JOINTS_1', 'WEIGHTS_1'):
            if key in attr:
                require(key == 'JOINTS_1' or np.all(reader.accessor(attr[key]) == 0), 'More than4active influences unsupported')
        require(np.all(uv >= -1e-5) and np.all(uv <= 1 + 1e-5), 'Repeating UVs need a sampler-aware profile')
        idx = reader.accessor(primitive['indices']).reshape(-1) if 'indices' in primitive else np.arange(len(p))
        require(len(idx) % 3 == 0 and np.all(idx == np.floor(idx)) and np.all(idx >= 0) and np.all(idx < len(p)), 'Invalid triangle indices')
        triangles = p[idx.astype(np.int64)].reshape(-1, 3, 3)
        areas = np.linalg.norm(np.cross(triangles[:,1] - triangles[:,0], triangles[:,2] - triangles[:,0]), axis=1)
        require(np.all(areas > 1e-12), 'Degenerate source triangles require explicit correction')
        for position, normal, texcoord, joints, weights in zip(p, n, uv, j, w):
            vertices += floats(np.r_[position, normal / np.linalg.norm(normal), texcoord])
            vertices += struct.pack('<4H', *joints.astype(int)) + floats(weights)
        index_values.extend((idx.astype(np.uint32) + vertex_count).tolist())
        vertex_count += len(p)
    require(1 <= vertex_count <= 100000 and 3 <= len(index_values) <= 300000, 'Geometry budget exceeded')
    material_index = primitives[0].get('material', -1)
    require(0 <= material_index < len(doc.get('materials', [])), 'Textured material is required')
    material = doc['materials'][material_index]
    require(material.get('alphaMode', 'OPAQUE') == 'OPAQUE', 'Alpha-mask/blended materials need a later profile')
    pbr = material.get('pbrMetallicRoughness', {})
    base = pbr.get('baseColorTexture', {})
    require('index' in base and not base.get('extensions') and base.get('texCoord', 0) == 0, 'Basic baseColorTexture UV0 required')
    advanced = [key for key in ('normalTexture', 'occlusionTexture', 'emissiveTexture', 'extensions') if material.get(key)]
    if pbr.get('metallicRoughnessTexture'):
        advanced.append('metallicRoughnessTexture')
    if pbr.get('metallicFactor', 1) != 0:
        advanced.append('metallic response')
    if material.get('emissiveFactor') and any(material['emissiveFactor']):
        advanced.append('emissive factor')
    require(diffuse_only or not advanced, 'Advanced material would be lost; explicitly choose --diffuse-only after review')
    image_index = doc['textures'][base['index']]['source']
    image = doc['images'][image_index]
    if 'bufferView' in image:
        image_data, _ = reader.view(image['bufferView'])
    else:
        image_data = reader.uri_bytes(image['uri'])
    decoded = Image.open(io.BytesIO(image_data)).convert('RGBA')
    require(decoded.width <= 16384 and decoded.height <= 16384, 'Source texture budget exceeded')
    require(np.all(np.asarray(decoded)[:,:,3] == 255), 'Nonopaque albedo requires explicit alpha-aware profile')
    source_texture_size = [decoded.width, decoded.height]
    decoded.thumbnail((texture_size, texture_size), Image.Resampling.LANCZOS)
    rgb = np.asarray(decoded.convert('RGB')).astype(np.float64) / 255
    factor = finite(pbr.get('baseColorFactor', [1,1,1,1]), (4,))
    require(np.all(factor >= 0) and np.all(factor <= 1) and factor[3] == 1, 'Unsupported baseColorFactor')
    linear = np.where(rgb <= .04045, rgb / 12.92, ((rgb + .055) / 1.055) ** 2.4) * factor[:3]
    srgb = np.where(linear <= .0031308, linear * 12.92, 1.055 * linear ** (1 / 2.4) - .055)
    texture = np.clip(np.rint(srgb * 255), 0, 255).astype(np.uint8).tobytes()
    clips = doc.get('animations', [])
    require(len(clips) <= 128, 'Clip budget exceeded')
    clip_records = []
    clip_metadata = []
    names = set()
    for clip_index, clip in enumerate(clips):
        name = clip.get('name') or f'clip-{clip_index}'
        require(name not in names, 'Duplicate clip name')
        names.add(name)
        channels = clip.get('channels', [])
        require(1 <= len(channels) <= 768, 'Invalid clip track count')
        tracks = []
        keys_seen = set()
        start, end = math.inf, -math.inf
        for channel in channels:
            target = channel['target']
            node_index, path = target.get('node'), target.get('path')
            require(isinstance(node_index,int) and 0 <= node_index < len(nodes) and path in ('translation','rotation','scale'), 'Invalid/morph track')
            require((node_index,path) not in keys_seen, 'Duplicate node/path animation track')
            keys_seen.add((node_index,path))
            sampler = clip['samplers'][channel['sampler']]
            interpolation = sampler.get('interpolation', 'LINEAR')
            require(interpolation in ('LINEAR','STEP'), 'CUBICSPLINE requires a separate explicit bake')
            times = finite(reader.accessor(sampler['input'])).reshape(-1)
            values = finite(reader.accessor(sampler['output']), (len(times), 4 if path == 'rotation' else 3))
            require(1 <= len(times) <= 100000 and times[0] >= 0 and np.all(np.diff(times) > 0), 'Invalid animation key times')
            if path == 'rotation':
                require(np.all(np.abs(np.linalg.norm(values,axis=1) - 1) <= 1e-3), 'Nonunit animation quaternion')
            if path == 'scale':
                require(np.all(values > 0), 'Nonpositive animation scale')
            start, end = min(start,float(times[0])), max(end,float(times[-1]))
            tracks.append(struct.pack('<IBBI', node_index, ('translation','rotation','scale').index(path), ('LINEAR','STEP').index(interpolation), len(times)) + floats(times) + floats(values))
        clip_records.append(string(name) + struct.pack('<ffI',start,end,len(tracks)) + b''.join(tracks))
        clip_metadata.append({'name':name,'start':start,'end':end,'tracks':len(tracks)})
    header = b'D3DACTR1' + struct.pack('<8I',1,len(nodes),len(joint_nodes),vertex_count,len(index_values),len(clips),decoded.width,decoded.height)
    data = header + b''.join(map(string,(asset_id,variant,revision,source_hash))) + b''.join(node_records) + b''.join(joint_records) + vertices + struct.pack('<' + 'I'*len(index_values),*index_values) + b''.join(clip_records) + texture
    require(len(data) <= 128 * 1024 * 1024, 'Output exceeds128MiB budget')
    destination.parent.mkdir(parents=True, exist_ok=True)
    require(not destination.exists(), 'Preserve existing actor output; use an explicit new revision or remove your own diagnostic consciously')
    with tempfile.NamedTemporaryFile(dir=destination.parent, prefix='.actor-', suffix='.tmp', delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    return {'format':'D3DACTR1','formatVersion':1,'assetId':asset_id,'variant':variant,'revision':revision,
            'source':str(source),'sourceSha256':source_hash,'output':str(destination),'outputSha256':hashlib.sha256(data).hexdigest(),
            'bytes':len(data),'nodes':len(nodes),'joints':len(joint_nodes),'vertices':vertex_count,'triangles':len(index_values)//3,
            'clips':clip_metadata,'textureSourceSize':source_texture_size,'textureOutputSize':[decoded.width,decoded.height],
            'diffuseOnly':diffuse_only,'materialFeaturesOmitted':advanced,'nativeTimingMapped':False,'gameplayIntegrated':False,
            'rootPolicy':'sourceTRS retained including Armature0.01; output positions are skin worldJoint*IBM*vertex; no architectural fit or implicit root-motion edit'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--asset-id',required=True)
    parser.add_argument('--variant',default='default')
    parser.add_argument('--revision',required=True)
    parser.add_argument('--diffuse-only',action='store_true')
    parser.add_argument('--texture-size',type=int,default=1024)
    args = parser.parse_args()
    try:
        result = export_actor(args.source,args.output,asset_id=args.asset_id,variant=args.variant,
                              revision=args.revision,diffuse_only=args.diffuse_only,texture_size=args.texture_size)
        args.output.with_suffix(args.output.suffix+'.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(result,ensure_ascii=False))
    except (ActorExportError,ValueError,OSError,KeyError,IndexError) as exc:
        print(json.dumps({'error':str(exc)}))
        raise SystemExit(1)


if __name__ == '__main__':
    main()
