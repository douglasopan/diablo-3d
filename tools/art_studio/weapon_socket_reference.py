"""Produce independent GLB socket matrices for comparison with native C++.

Diagnostic only: does not alter assets, select equipment or approve finger grip.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
import foot_contact as fc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('body', type=Path)
    parser.add_argument('calibration', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Use a fresh output path')
    receipt = json.loads(args.calibration.read_text(encoding='utf-8-sig'))
    if fc.hp.sha(args.body.read_bytes()) != receipt['bodySha256']:
        parser.error('Calibration belongs to another body revision')
    reader, _ = fc.hp.load_reader(fc.hp.ROOT)
    model = fc.hp.Model(args.body, reader)
    indices = [i for i, node in enumerate(model.nodes) if node.get('name') == receipt['bone']]
    if len(indices) != 1 or len(model.animations) != 1:
        parser.error('Expected one unique bone and one clip')
    local = receipt['localTransform']
    offset = np.eye(4)
    offset[:3, :3] = Rotation.from_quat(local['rotationXYZW']).as_matrix() * local['uniformScale']
    offset[:3, 3] = local['translation']
    clip = model.animations[0]
    rows = []
    for phase in np.linspace(0, 1, 1025):
        time = clip['start'] + phase * (clip['end'] - clip['start'])
        matrix = model.pose(clip, float(time))[indices[0]] @ offset
        rows.append([phase, *matrix.flatten(order='F')])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    np.savetxt(args.output, rows, fmt='%.17g')
    print(json.dumps({'samples': len(rows), 'bodySha256': receipt['bodySha256'],
                      'referenceSha256': fc.hp.sha(args.output.read_bytes()),
                      'bone': receipt['bone'], 'gameplaySelected': False}))


if __name__ == '__main__':
    main()
