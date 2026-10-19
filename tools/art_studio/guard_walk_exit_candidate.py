"""Author a one-shot walk-terminal-to-guard exit with bounded foot contact.

The start is the actual terminal pose of the supplied walk, not a guessed
still. Native movement and action clocks remain authoritative. This offline
candidate only covers a stop at that terminal phase; arbitrary interruption
phases need a separate native transition policy and cannot select this clip.
"""
import argparse
import copy
import json
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation

import foot_contact as fc
from attack_settle import sample_nodes, settle
from merge_actor_clips import static_signature


def geometry(model, clip, time):
    return np.concatenate([part['vertices'] for part in model.geometry(model.pose(clip, float(time)))])


def smoothstep(phase):
    return phase * phase * (3 - 2 * phase)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('guard', type=Path)
    parser.add_argument('walk', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--keys', type=int, default=17)
    parser.add_argument('--refine-from', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Use a fresh candidate directory; preserve rejected revisions')
    if args.keys < 3:
        parser.error('At least three contact keys are required')

    reader, _ = fc.hp.load_reader(fc.hp.ROOT)
    guard_model = fc.hp.Model(args.guard, reader)
    walk_model = fc.hp.Model(args.walk, reader)
    signature = static_signature(walk_model)
    if len(guard_model.animations) != 1 or len(walk_model.animations) != 1 or static_signature(guard_model) != signature:
        parser.error('Exit requires one clip per identical resolved actor layout')
    walk_clip, guard_clip = walk_model.animations[0], guard_model.animations[0]
    walk_terminal = sample_nodes(walk_model, walk_clip, walk_clip['end'])
    guard = sample_nodes(guard_model, guard_clip, guard_clip['start'])
    duration = (walk_clip['end'] - walk_clip['start']) * .25
    if duration <= 0:
        parser.error('Walk source must have a positive duration')

    # Only the source pelvis position differs. All other local translations,
    # scales, and finger poses must agree before authoring the transition.
    hips = next(i for i, node in enumerate(walk_terminal) if node.get('name') == 'mixamorig:Hips')
    fingers = [i for i, node in enumerate(walk_terminal) if node.get('name', '').startswith('mixamorig:RightHand') and node.get('name') != 'mixamorig:RightHand']
    endpoint_length_delta = max(float(np.abs(np.asarray(a.get('translation', [0, 0, 0])) - b.get('translation', [0, 0, 0])).max())
                                for i, (a, b) in enumerate(zip(walk_terminal, guard)) if i != hips)
    endpoint_scale_delta = max(float(np.abs(np.asarray(a.get('scale', [1, 1, 1])) - b.get('scale', [1, 1, 1])).max())
                               for a, b in zip(walk_terminal, guard))
    endpoint_grip_delta = max(float(np.abs(reader.node_matrix(walk_terminal[i]) - reader.node_matrix(guard[i])).max()) for i in fingers)
    if max(endpoint_length_delta, endpoint_scale_delta, endpoint_grip_delta) > 1e-6:
        parser.error('Source endpoints do not preserve local bone lengths, scales, and right finger grip')

    phases = np.linspace(0, 1, args.keys)
    additional = []
    if args.refine_from:
        prior_model = fc.hp.Model(args.refine_from, reader)
        if len(prior_model.animations) != 1 or static_signature(prior_model) != signature:
            parser.error('Refinement must use the same actor layout')
        prior_clip = prior_model.animations[0]
        prior_duration = prior_clip['end'] - prior_clip['start']
        phases = np.unique(np.concatenate([channel['times'] for channel in prior_clip['channels']])) / prior_duration
        for phase in np.linspace(0, 1, 1025):
            if geometry(prior_model, prior_clip, prior_clip['start'] + phase * prior_duration)[:, 1].min() < -.0015:
                additional.append(float(phase))
        phases = np.unique(np.r_[phases, additional])

    args.output.mkdir(parents=True)
    poses, changes = [], []
    source_record = {'guardPath': str(args.guard.resolve()), 'guardSha256': fc.hp.sha(args.guard.read_bytes()),
                     'walkPath': str(args.walk.resolve()), 'walkSha256': fc.hp.sha(args.walk.read_bytes()),
                     'walkTerminalTime': walk_clip['end'], 'guardInitialTime': guard_clip['start'],
                     'staticSignature': signature, 'authoredDurationSeconds': duration,
                     'refineSourceSha256': fc.hp.sha(args.refine_from.read_bytes()) if args.refine_from else None}
    print(json.dumps({'additionalContactKeys': len(additional), 'totalKeys': len(phases)}), flush=True)
    for index, phase in enumerate(phases):
        base = settle(walk_terminal, guard, smoothstep(float(phase)))
        if phase == 0:
            nodes, correction = copy.deepcopy(walk_terminal), []
        elif phase == 1:
            nodes, correction = copy.deepcopy(guard), []
        else:
            nodes, correction = fc.correct(walk_model, base, reader)
        maximum_rotation = max(float((Rotation.from_quat(base[i].get('rotation', [0, 0, 0, 1])).inv() *
                                      Rotation.from_quat(nodes[i].get('rotation', [0, 0, 0, 1]))).magnitude()) for i in range(len(nodes)))
        poses.append(nodes)
        changes.append({'phase': float(phase), 'weight': smoothstep(float(phase)), 'footCorrections': correction,
                        'maximumCorrectionAngleDegrees': float(np.rad2deg(maximum_rotation))})
        fc.hp.write_json(args.output / 'checkpoint.json', {**source_record, 'completed': index + 1, 'total': len(phases),
                                                         'times': (phases[:index + 1] * duration).tolist(), 'poses': poses, 'changes': changes})
        print(json.dumps({'key': index + 1, 'total': len(phases)}), flush=True)

    fc.rt.SEMANTICS = fc.skin_semantics(walk_model)
    output = args.output / 'warrior-walk-exit.glb'
    old, _ = fc.rt.append_animation(walk_model, reader, phases * duration, poses, 'walk-terminal-to-guard-exit-only',
                                   {**source_record, 'authoring': 'smoothstep terminal walk to guard, bounded rotational contact; no added root lift',
                                    'oneShotExitOnly': True, 'arbitraryInterruptPhaseSupported': False}, output)
    result = fc.hp.Model(output, reader)
    clip = result.animations[0]
    start_xyz = geometry(walk_model, walk_clip, walk_clip['end'])
    end_xyz = geometry(guard_model, guard_clip, guard_clip['start'])
    minimum, maximum_motion, maximum_step = 1e9, 0., 0.
    maximum_length_delta, maximum_scale_delta, maximum_grip_delta = 0., 0., 0.
    samples, previous = [], None
    for phase in np.linspace(0, 1, 1025):
        time = clip['start'] + phase * (clip['end'] - clip['start'])
        xyz = geometry(result, clip, time)
        nodes = sample_nodes(result, clip, time)
        minimum = min(minimum, float(xyz[:, 1].min()))
        maximum_motion = max(maximum_motion, float(np.linalg.norm(xyz - start_xyz, axis=1).max()))
        if previous is not None:
            maximum_step = max(maximum_step, float(np.linalg.norm(xyz - previous, axis=1).max()))
        previous = xyz
        for i, node in enumerate(nodes):
            if i != hips:
                maximum_length_delta = max(maximum_length_delta, float(np.abs(np.asarray(node.get('translation', [0, 0, 0])) - walk_terminal[i].get('translation', [0, 0, 0])).max()))
            maximum_scale_delta = max(maximum_scale_delta, float(np.abs(np.asarray(node.get('scale', [1, 1, 1])) - walk_terminal[i].get('scale', [1, 1, 1])).max()))
        maximum_grip_delta = max(maximum_grip_delta, max(float(np.abs(reader.node_matrix(nodes[i]) - reader.node_matrix(walk_terminal[i])).max()) for i in fingers))
        samples.append({'phase': float(phase), 'minimumY': float(xyz[:, 1].min())})
        if phase == 0:
            start_error = float(np.linalg.norm(xyz - start_xyz, axis=1).max())
        if phase == 1:
            end_error = float(np.linalg.norm(xyz - end_xyz, axis=1).max())

    _, binary, _ = reader.read_glb(output)
    checks = {'staticLayoutPreserved': static_signature(result) == signature,
              'sourceBinaryPrefixPreserved': bytes(binary[:len(old)]) == bytes(old),
              'groundPass': minimum >= -.002, 'startsAtActualWalkTerminal': start_error < 1e-6,
              'endsAtGuard': end_error < 1e-6, 'bodyMotionNotStill': maximum_motion > .01,
              'localBoneLengthsPreserved': maximum_length_delta < 1e-6,
              'scalesPreserved': maximum_scale_delta < 1e-6, 'rightFingerGripPreserved': maximum_grip_delta < 1e-6}
    receipt = {**source_record, 'candidateSha256': fc.hp.sha(output.read_bytes()), 'checks': checks, 'minimumY': minimum,
               'startWalkTerminalDelta': start_error, 'endGuardDelta': end_error, 'maximumMotion': maximum_motion,
               'maximumAdjacentSampleDisplacement': maximum_step, 'maximumNonPelvisTranslationDelta': maximum_length_delta,
               'maximumScaleDelta': maximum_scale_delta, 'maximumRightFingerLocalMatrixDelta': maximum_grip_delta,
               'samples': 1025, 'contactKeys': len(phases), 'additionalContactKeys': len(additional), 'changes': changes,
               'oneShotExitOnly': True, 'followsWithGuardIdle': True, 'nativeTimingConnected': False, 'gameplaySelected': False,
               'arbitraryInterruptPhaseSupported': False, 'rootLiftAdded': False,
               'pelvisTranslation': 'Only interpolates the existing walk terminal to existing guard; contact solver changes rotations only',
               'limitation': 'Terminal-phase stop only; arbitrary walk interruption, foot locking, collision, joint anatomy, native dispatch and playable gates remain unqualified',
               'status': 'grounded-exit-candidate-needs-native-transition-and-art-review' if all(checks.values()) else 'rejected'}
    fc.hp.write_json(args.output / 'DENSE-CONTACT.json', {'samples': samples})
    fc.hp.write_json(args.output / 'receipt.json', receipt)
    print(json.dumps({key: value for key, value in receipt.items() if key != 'changes'}), flush=True)


if __name__ == '__main__':
    main()
