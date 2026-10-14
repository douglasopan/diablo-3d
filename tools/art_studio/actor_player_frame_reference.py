"""Private rigid inputs and independent GLB socket references; not game assets."""
import argparse
import json
import struct
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
import foot_contact as fc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Use a fresh directory')
    root = fc.hp.ROOT.parent
    diag = root / 'diagnostics'
    common = diag / 'universal-modder-20261012'
    body_path = common / 'warrior-clips-bundle-r2/warrior-clips-candidate.glb'
    sources = [
        (common / 'great-sword-isolated-r2/left-sword-candidate.glb', common / 'weapon-socket-calibration-r4/receipt.json', 'diagnostic.held-sword', 1),
        (diag / 'starter-shield-20261013-r1/refinement-r2/buckler-original-candidate-r1.glb', diag / 'starter-shield-20261013-r1/refinement-r2/SOCKET-CANDIDATE.json', 'diagnostic.buckler', 2),
    ]
    reader, _ = fc.hp.load_reader(fc.hp.ROOT)
    body = fc.hp.Model(body_path, reader)
    blob = bytearray(b'D3DRIGD1' + struct.pack('<I', len(sources)))
    closures, sockets = [], []
    def text(value):
        encoded = value.encode('utf-8')
        blob.extend(struct.pack('<H', len(encoded)) + encoded)
    for source, receipt_path, identity, item_id in sources:
        receipt = json.loads(receipt_path.read_text(encoding='utf-8-sig'))
        model = fc.hp.Model(source, reader)
        geometry = model.geometry(model.rest_world)
        checksum = fc.hp.sha(source.read_bytes())
        expected = receipt.get('weaponSha256', receipt.get('shieldSha256'))
        if checksum != expected:
            raise ValueError('Socket belongs to another source mesh')
        local = receipt['localTransform']
        blob.extend(struct.pack('<i', item_id))
        for value in (identity, 'private-fixture-r1', checksum, receipt['bone']):
            text(value)
        fields = np.asarray([*local['translation'], *local['rotationXYZW'], *([local['uniformScale']] * 3)], dtype='<f4')
        blob.extend(fields.tobytes() + struct.pack('<I', len(geometry)))
        for part in geometry:
            if part['world_normals'] is None or part['uv'] is None:
                raise ValueError('Fixture requires real normals and UVs')
            vertices = np.column_stack([part['vertices'], part['world_normals'], part['uv']]).astype('<f4')
            indices = part['triangles'].astype('<u4').flatten()
            blob.extend(struct.pack('<III', part['material'], len(vertices), len(indices)))
            blob.extend(vertices.tobytes() + indices.tobytes())
        offset = np.eye(4)
        offset[:3, :3] = Rotation.from_quat(fields[3:7]).as_matrix() * fields[7]
        offset[:3, 3] = fields[:3]
        nodes = [i for i, n in enumerate(body.nodes) if n.get('name') == receipt['bone']]
        if len(nodes) != 1:
            raise ValueError('Ambiguous skin socket')
        sockets.append((nodes[0], offset))
        closures.append({'path': str(source), 'sha256': checksum, 'receiptSha256': fc.hp.sha(receipt_path.read_bytes()),
                         'parts': len(geometry), 'triangles': sum(len(p['triangles']) for p in geometry),
                         'diagnosticNativeItemId': item_id, 'nativeItemArtIdentityApproved': False})
    foot = np.asarray([.11, -.04, .02], dtype=np.float32).astype(float)
    location = np.asarray([5, .3, 7], dtype=np.float32).astype(float)
    pi = np.float32(np.pi)
    yaw = np.asarray([pi/np.float32(4), 0, -pi/np.float32(4), -pi/np.float32(2), -np.float32(3)*pi/np.float32(4), pi, np.float32(3)*pi/np.float32(4), pi/np.float32(2)], dtype=np.float32)
    rows = []
    for clip_index, animation in enumerate(body.animations):
        for phase in (0, .25, .5, .75, 1):
            seconds = float(np.float32(np.float32(animation['start']) + phase * (np.float32(animation['end']) - np.float32(animation['start']))))
            pose = body.pose(animation, seconds)
            for direction in range(8):
                angle = float(yaw[direction]) + float(np.float32(.15))
                c, s = np.cos(angle), np.sin(angle)
                world = np.eye(4)
                world[:3, :3] = 2 * np.asarray([[c, 0, s], [0, 1, 0], [-s, 0, c]])
                world[:3, 3] = location - world[:3, :3] @ foot
                matrices = [world @ pose[node] @ offset for node, offset in sockets]
                rows.append([clip_index, phase, direction, *np.concatenate([m.flatten(order='F') for m in matrices])])
    args.output.mkdir(parents=True)
    (args.output / 'rigid-inputs.bin').write_bytes(blob)
    np.savetxt(args.output / 'socket-world-reference.txt', rows, fmt='%.17g')
    report = {'bodyPath': str(body_path), 'bodySha256': fc.hp.sha(body_path.read_bytes()), 'attachments': closures,
              'samples': len(rows), 'clips': len(body.animations), 'directions': 8, 'phaseSamplesPerClip': 5,
              'rigidInputSha256': fc.hp.sha(blob), 'referenceSha256': fc.hp.sha((args.output / 'socket-world-reference.txt').read_bytes()),
              'scope': 'GLB evaluator reference and actual rigid mesh parts; geometry comparison only, no rendering/material/item identity/admission.'}
    (args.output / 'REFERENCE.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
