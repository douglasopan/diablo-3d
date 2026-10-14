"""Private rotational shield-ground correction on an existing death clip.

Only LeftArm, the LeftForeArm elbow hinge and LeftHand rotations may change.
The authored body fall, root/pelvis path, limb translations, scales, fingers,
mesh/materials and stationary corpse plateau remain authoritative. Equipment
stays attached throughout; no release policy, native timing or selection.
"""
import argparse
import copy
import json
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares
from scipy.spatial import ConvexHull
from scipy.spatial.transform import Rotation

import foot_contact as fc
from attack_settle import sample_nodes
from merge_actor_clips import static_signature


def socket_matrix(record):
    local = record['localTransform']
    matrix = np.eye(4)
    matrix[:3, :3] = Rotation.from_quat(local['rotationXYZW']).as_matrix() * local['uniformScale']
    matrix[:3, 3] = local['translation']
    return matrix


def rigid_hull(model):
    vertices = np.concatenate([part['vertices'] for part in model.geometry(model.rest_world)])
    # The minimum of a rigid plane coordinate occurs on the convex hull. These
    # are actual original source vertices, not bounds or a simplified mesh.
    hull = vertices[ConvexHull(vertices).vertices]
    return np.column_stack([hull, np.ones(len(hull))])


def left_skin_groups(model, chain):
    descendants = set(chain)
    for index in range(len(model.nodes)):
        parent = index
        while parent is not None:
            if parent in descendants:
                descendants.add(index)
                break
            parent = model.parents[parent]
    groups = []
    for primitive in model.primitives:
        if primitive['skin'] is None:
            raise ValueError('Body primitive requires skin')
        joints, _ = model.skins[primitive['skin']]
        weight = np.zeros(len(primitive['positions']))
        for js, ws in primitive['pairs']:
            weight += (ws * np.isin(joints[js], list(descendants))).sum(axis=1)
        indices = np.flatnonzero(weight > 1e-8)
        if len(indices):
            groups.append((primitive, indices))
    return groups


def correct_pose(model, reader, base, chain, groups, shield_points, socket, limit, previous):
    world = fc.world(model, base, reader)
    initial_shield = shield_points @ (world[chain[2]] @ socket).T
    if initial_shield[:, 1].min() >= .0002:
        return copy.deepcopy(base), np.zeros(7), {'changed': False, 'initialShieldY': float(initial_shield[:, 1].min())}
    upper = world[chain[1]][:3, 3] - world[chain[0]][:3, 3]
    lower = world[chain[2]][:3, 3] - world[chain[1]][:3, 3]
    axis = np.cross(upper, lower)
    axis /= np.linalg.norm(axis)
    elbow_axis = np.linalg.solve(world[chain[1]][:3, :3], axis)
    elbow_axis /= np.linalg.norm(elbow_axis)
    rotations = [Rotation.from_quat(base[index]['rotation']) for index in chain]
    references = [(primitive, indices, fc.points(model, primitive, world, indices)) for primitive, indices in groups]

    def pose(values):
        nodes = copy.deepcopy(base)
        deltas = (values[:3], elbow_axis * values[3], values[4:])
        for index, rotation, delta in zip(chain, rotations, deltas):
            nodes[index]['rotation'] = (rotation * Rotation.from_rotvec(delta)).as_quat().tolist()
        return nodes

    def residual(values):
        trial = fc.world(model, pose(values), reader)
        shield = shield_points @ (trial[chain[2]] @ socket).T
        errors = [np.minimum(shield[:, 1] - .00035, 0) * 30, values * .02, (values - previous) * .015]
        for primitive, indices, reference in references:
            body = fc.points(model, primitive, trial, indices)
            errors.extend([np.minimum(body[:, 1] - .0002, 0) * 30, (body - reference).reshape(-1) * .06])
        return np.concatenate(errors)

    # The cube is inscribed in a radius-limit ball, so this is a true maximum
    # rotation angle per joint, not a component bound mislabeled as an angle.
    bounds = np.deg2rad(np.array([limit / np.sqrt(3)] * 3 + [limit] + [limit / np.sqrt(3)] * 3))
    solution = least_squares(residual, np.clip(previous, -bounds * .999, bounds * .999),
                             bounds=(-bounds, bounds), max_nfev=80, ftol=1e-8, xtol=1e-8, gtol=1e-8)
    nodes = pose(solution.x)
    changed = fc.world(model, nodes, reader)
    return nodes, solution.x, {'changed': True, 'initialShieldY': float(initial_shield[:, 1].min()),
                              'correctedShieldY': float((shield_points @ (changed[chain[2]] @ socket).T)[:, 1].min()),
                              'solverSuccess': bool(solution.success), 'evaluations': solution.nfev,
                              'jointCorrectionDegrees': [float(np.rad2deg(np.linalg.norm(solution.x[:3]))),
                                                         float(np.rad2deg(abs(solution.x[3]))),
                                                         float(np.rad2deg(np.linalg.norm(solution.x[4:])))],
                              'elbowLocalHingeAxis': elbow_axis.tolist(), 'values': solution.x.tolist()}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('death', type=Path)
    p.add_argument('shield', type=Path)
    p.add_argument('socket', type=Path)
    p.add_argument('--weapon', type=Path, required=True)
    p.add_argument('--weapon-socket', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--maximum-angle', type=float, default=12)
    p.add_argument('--refine-from', type=Path)
    p.add_argument('--resume-checkpoint', type=Path)
    args = p.parse_args()
    if args.output.exists():
        p.error('Preserve previous revisions; use a fresh candidate directory')
    if not 0 < args.maximum_angle <= 30:
        p.error('Correction angle must be explicit and limited to at most 30 degrees')
    reader, _ = fc.hp.load_reader(fc.hp.ROOT)
    model = fc.hp.Model(args.death, reader)
    shield = fc.hp.Model(args.shield, reader)
    weapon = fc.hp.Model(args.weapon, reader)
    record = json.loads(args.socket.read_text(encoding='utf-8-sig'))
    weapon_record = json.loads(args.weapon_socket.read_text(encoding='utf-8-sig'))
    if fc.hp.sha(args.shield.read_bytes()) != record['shieldSha256'] or fc.hp.sha(args.weapon.read_bytes()) != weapon_record['weaponSha256']:
        p.error('Equipment source hashes differ from their sockets')
    if len(model.animations) != 1 or record['bone'] != 'mixamorig:LeftHand':
        p.error('One death clip and the explicit left-hand socket are required')
    names = {node.get('name'): i for i, node in enumerate(model.nodes)}
    chain = [names['mixamorig:' + part] for part in ('LeftArm', 'LeftForeArm', 'LeftHand')]
    weapon_hand = names[weapon_record['bone']]
    groups = left_skin_groups(model, chain)
    shield_points, weapon_points = rigid_hull(shield), rigid_hull(weapon)
    socket, weapon_socket = socket_matrix(record), socket_matrix(weapon_record)
    clip = model.animations[0]
    duration = clip['end'] - clip['start']
    phases = (np.unique(np.concatenate([channel['times'] for channel in clip['channels']])) - clip['start']) / duration
    phases = np.unique(np.r_[phases, .86, 1.0])
    additional = []
    if args.refine_from:
        prior = fc.hp.Model(args.refine_from, reader)
        if len(prior.animations) != 1 or static_signature(prior) != static_signature(model):
            p.error('Refinement source has another actor layout')
        prior_clip = prior.animations[0]
        phases = np.unique(np.r_[phases, (np.concatenate([c['times'] for c in prior_clip['channels']]) - prior_clip['start']) / duration])
        for phase in np.linspace(0, 1, 1025):
            world = prior.pose(prior_clip, float(prior_clip['start'] + phase * duration))
            if (shield_points @ (world[chain[2]] @ socket).T)[:, 1].min() < -.0015:
                additional.append(float(phase))
        phases = np.unique(np.r_[phases, additional])

    # Animation inputs are float32. Canonicalize before authoring so an exact
    # .86 anchor and its source float32 key cannot collapse to duplicate inputs.
    times = np.unique(np.asarray(phases * duration, dtype='<f4')).astype(float)
    phases = times / duration
    hold_index = int(np.flatnonzero(times == float(np.float32(.86 * duration)))[0])
    resumed = None
    if args.resume_checkpoint:
        checkpoint = json.loads(args.resume_checkpoint.read_text(encoding='utf-8-sig'))
        if checkpoint['completed'] != checkpoint['total'] or checkpoint['deathSha256'] != fc.hp.sha(args.death.read_bytes()) or checkpoint['socketSha256'] != fc.hp.sha(args.socket.read_bytes()):
            p.error('Resume requires a complete checkpoint for these exact body/socket sources')
        resumed = {}
        for time, pose, change in zip(checkpoint['times'], checkpoint['poses'], checkpoint['changes']):
            resumed.setdefault(float(np.float32(time)), (pose, change))
        if any(float(time) not in resumed for time in times):
            p.error('Checkpoint does not contain all canonical authored times')

    args.output.mkdir(parents=True)
    source = {'deathPath': str(args.death.resolve()), 'deathSha256': fc.hp.sha(args.death.read_bytes()),
              'shieldPath': str(args.shield.resolve()), 'shieldSha256': record['shieldSha256'],
              'socketPath': str(args.socket.resolve()), 'socketSha256': fc.hp.sha(args.socket.read_bytes()),
              'weaponPath': str(args.weapon.resolve()), 'weaponSha256': weapon_record['weaponSha256'],
              'weaponSocketSha256': fc.hp.sha(args.weapon_socket.read_bytes()),
              'toolSha256': fc.hp.sha(Path(__file__).read_bytes()), 'maximumJointAngleDegrees': args.maximum_angle,
              'terminalHoldPhase': .86, 'storedTerminalHoldPhase': float(phases[hold_index]),
              'resumeCheckpointSha256': fc.hp.sha(args.resume_checkpoint.read_bytes()) if args.resume_checkpoint else None,
              'refineSourceSha256': fc.hp.sha(args.refine_from.read_bytes()) if args.refine_from else None}
    poses, changes, previous, terminal = [], [], np.zeros(7), None
    for index, phase in enumerate(phases):
        base = sample_nodes(model, clip, clip['start'] + float(phase) * duration)
        if resumed:
            nodes, metric = copy.deepcopy(resumed[float(times[index])])
        elif index > hold_index:
            nodes = copy.deepcopy(terminal)
            metric = {'changed': True, 'terminalHoldCopied': True}
        else:
            nodes, previous, metric = correct_pose(model, reader, base, chain, groups, shield_points, socket, args.maximum_angle, previous)
            if index == hold_index:
                terminal = copy.deepcopy(nodes)
        poses.append(nodes)
        changes.append({'phase': float(phase), **metric})
        fc.hp.write_json(args.output / 'checkpoint.json', {**source, 'completed': index + 1, 'total': len(phases),
                                                         'times': times[:index + 1].tolist(), 'poses': poses, 'changes': changes})
        if index % 16 == 0 or index + 1 == len(phases):
            print(json.dumps({'key': index + 1, 'total': len(phases), 'phase': float(phase)}), flush=True)
    fc.rt.SEMANTICS = fc.skin_semantics(model)
    output = args.output / 'warrior-death-shield-contact.glb'
    old, _ = fc.rt.append_animation(model, reader, times, poses, 'death-shield-contact-held-corpse',
                                   {**source, 'authoring': 'bounded LeftArm/elbow hinge/LeftHand rotations only; equipment retained',
                                    'nativeTimingConnected': False, 'equipmentReleased': False}, output)
    result = fc.hp.Model(output, reader)
    candidate_clip = result.animations[0]
    minimum_body, minimum_shield, minimum_weapon = 1e9, 1e9, 1e9
    unchanged_delta, translation_delta, scale_delta, held_delta, max_correction = 0., 0., 0., 0., 0.
    held_reference = None
    samples = []
    sample_times = np.unique(np.r_[np.linspace(0, duration, 1025), times])
    for time in sample_times:
        phase = time / duration
        candidate_nodes = sample_nodes(result, candidate_clip, float(time))
        reference_nodes = sample_nodes(model, clip, float(clip['start'] + time))
        for i, (node, reference) in enumerate(zip(candidate_nodes, reference_nodes)):
            if i not in chain:
                unchanged_delta = max(unchanged_delta, float(abs(reader.node_matrix(node) - reader.node_matrix(reference)).max()))
            translation_delta = max(translation_delta, float(np.abs(np.asarray(node.get('translation', [0, 0, 0])) - reference.get('translation', [0, 0, 0])).max()))
            scale_delta = max(scale_delta, float(np.abs(np.asarray(node.get('scale', [1, 1, 1])) - reference.get('scale', [1, 1, 1])).max()))
            if i in chain:
                correction = (Rotation.from_quat(reference['rotation']).inv() * Rotation.from_quat(node['rotation'])).magnitude()
                max_correction = max(max_correction, float(np.rad2deg(correction)))
        world = result.pose(candidate_clip, float(time))
        xyz = np.concatenate([part['vertices'] for part in result.geometry(world)])
        body_y = float(xyz[:, 1].min())
        shield_y = float((shield_points @ (world[chain[2]] @ socket).T)[:, 1].min())
        weapon_y = float((weapon_points @ (world[weapon_hand] @ weapon_socket).T)[:, 1].min())
        minimum_body, minimum_shield, minimum_weapon = min(minimum_body, body_y), min(minimum_shield, shield_y), min(minimum_weapon, weapon_y)
        if phase >= .86:
            pose = np.r_[xyz.flatten(), (world[chain[2]] @ socket).flatten(), (world[weapon_hand] @ weapon_socket).flatten()]
            if held_reference is None:
                held_reference = pose
            held_delta = max(held_delta, float(np.abs(pose - held_reference).max()))
        samples.append({'phase': float(phase), 'bodyMinimumY': body_y, 'shieldMinimumY': shield_y, 'weaponMinimumY': weapon_y})
    _, binary, _ = reader.read_glb(output)
    checks = {'staticLayoutPreserved': static_signature(result) == static_signature(model),
              'sourceBinaryPrefixPreserved': bytes(binary[:len(old)]) == bytes(old),
              'nonLeftArmLocalMotionPreserved': unchanged_delta < 1e-6, 'translationsPreserved': translation_delta < 1e-6,
              'scalesPreserved': scale_delta < 1e-6, 'jointCorrectionsWithinBound': max_correction < args.maximum_angle + .001,
              'bodyGroundPass': minimum_body >= -.002, 'shieldGroundPass': minimum_shield >= -.002,
              'weaponGroundPass': minimum_weapon >= -.002, 'terminalBodyAndEquipmentHeld': held_delta < 1e-6}
    report = {**source, 'candidateSha256': fc.hp.sha(output.read_bytes()), 'checks': checks, 'denseSamples': len(sample_times),
              'minimumBodyY': minimum_body, 'minimumShieldY': minimum_shield, 'minimumWeaponY': minimum_weapon,
              'maximumNonLeftArmLocalMatrixDelta': unchanged_delta, 'maximumTranslationDelta': translation_delta,
              'maximumScaleDelta': scale_delta, 'maximumCorrectionDegrees': max_correction, 'terminalHoldMaximumDelta': held_delta,
              'shieldConvexHullActualVertexCount': len(shield_points), 'weaponConvexHullActualVertexCount': len(weapon_points),
              'additionalContactKeys': len(additional), 'changes': changes, 'equipmentRetained': True, 'fingerRotationsEdited': False,
              'rootLiftOrScaleCorrection': False, 'gameplaySelected': False, 'nativeMovementRngOrTimingEdited': False,
              'status': 'grounded-candidate-needs-art-and-native-equipment-review' if all(checks.values()) else 'rejected',
              'limitation': 'Exact source death and socket only; shield/body self-collision, grip/anatomy and native release/retention/host admission remain unqualified'}
    fc.hp.write_json(args.output / 'DENSE-CONTACT.json', {'samples': samples})
    fc.hp.write_json(args.output / 'receipt.json', report)
    print(json.dumps({key: value for key, value in report.items() if key != 'changes'}), flush=True)


if __name__ == '__main__':
    main()
