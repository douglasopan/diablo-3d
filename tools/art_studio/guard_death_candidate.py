"""Author a controlled full-body Warrior fall and a held grounded corpse.

Visual authoring only.  The pelvis trajectory is part of the fall, not movement
of the native entity.  Its height is solved against the deforming contact
envelope at every authored key; neither mesh vertices nor instance placement
are lifted.  Masters, skin, local limb lengths and materials are preserved.
"""
import argparse
import copy
import json
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation, Slerp

import foot_contact as fc
from attack_settle import sample_nodes


# In world axes of the original guard: X is flexion, Z is lateral collapse.
# Keys describe buckling, backward fall, impact and a settled supine corpse.
# Each row is phase, pelvis flexion, pelvis roll, thigh flexion, knee flexion,
# spine recoil and visual pelvis Z travel. Arms use actual length-preserving IK.
RECIPE = np.array([
    [0, 0, 0, 0, 0, 0, 0],
    [.10, -4, 2, -8, 16, 5, -.012],
    [.24, -18, 5, -28, 52, 9, -.045],
    [.42, -52, 8, -22, 42, 4, -.11],
    [.62, -80, 10, -12, 24, -1, -.19],
    [.76, -92, 10, -6, 12, -3, -.22],
    [.86, -90, 10, -6, 12, -2, -.22],
    [1, -90, 10, -6, 12, -2, -.22],
], dtype=float)


def world_axis_local(world, index, axis):
    basis = world[index][:3, :3]
    result = np.linalg.solve(basis, axis)
    return result / np.linalg.norm(result)


def set_world_rotation(model, reader, nodes, index, desired):
    posed = fc.world(model, nodes, reader)
    parent = model.parents[index]
    basis = posed[parent][:3, :3] if parent is not None else np.eye(3)
    basis = basis / np.linalg.norm(basis, axis=0)
    nodes[index]['rotation'] = (Rotation.from_matrix(basis).inv() * desired).as_quat().tolist()


def resting_arm(model, reader, nodes, names, initial, side, target, pole):
    """Analytic two-bone IK; local offsets/lengths/scales remain untouched."""
    arm, fore, hand = [names['mixamorig:' + side + part]
                       for part in ('Arm', 'ForeArm', 'Hand')]
    posed = fc.world(model, nodes, reader)
    shoulder = posed[arm][:3, 3]
    first = float(np.linalg.norm(initial[fore][:3, 3] - initial[arm][:3, 3]))
    second = float(np.linalg.norm(initial[hand][:3, 3] - initial[fore][:3, 3]))
    delta = target - shoulder
    requested = float(np.linalg.norm(delta))
    direction = delta / requested
    distance = np.clip(requested, abs(first - second) + .0001, first + second - .0001)
    effective_target = shoulder + direction * distance
    perpendicular = pole - shoulder
    perpendicular -= direction * (perpendicular @ direction)
    perpendicular /= np.linalg.norm(perpendicular)
    cosine = np.clip((first * first + distance * distance - second * second) /
                     (2 * first * distance), -1, 1)
    elbow = shoulder + direction * first * cosine + perpendicular * first * np.sqrt(1 - cosine * cosine)
    current_direction = posed[fore][:3, 3] - shoulder
    change, _ = Rotation.align_vectors([elbow - shoulder], [current_direction])
    base = posed[arm][:3, :3]
    base = base / np.linalg.norm(base, axis=0)
    set_world_rotation(model, reader, nodes, arm, change * Rotation.from_matrix(base))
    posed = fc.world(model, nodes, reader)
    current_direction = posed[hand][:3, 3] - posed[fore][:3, 3]
    change, _ = Rotation.align_vectors([effective_target - elbow], [current_direction])
    base = posed[fore][:3, :3]
    base = base / np.linalg.norm(base, axis=0)
    set_world_rotation(model, reader, nodes, fore, change * Rotation.from_matrix(base))
    solved = fc.world(model, nodes, reader)
    return {'side': side, 'requestedReach': requested, 'maximumReach': first + second,
            'reachClamped': bool(abs(requested - distance) > 1e-8),
            'targetError': float(np.linalg.norm(solved[hand][:3, 3] - effective_target))}


def authored_pose(model, reader, guard, names, initial, phase):
    values = np.array([np.interp(phase, RECIPE[:, 0], RECIPE[:, column])
                       for column in range(1, RECIPE.shape[1])])
    tilt, roll, thigh, knee, spine, ztravel = values
    nodes = copy.deepcopy(guard)

    def rotate(name, world_axis, degrees):
        index = names['mixamorig:' + name]
        local_axis = world_axis_local(initial, index, np.asarray(world_axis))
        nodes[index]['rotation'] = (
            Rotation.from_quat(nodes[index]['rotation']) *
            Rotation.from_rotvec(local_axis * np.deg2rad(degrees))
        ).as_quat().tolist()

    rotate('Hips', [1, 0, 0], tilt)
    rotate('Hips', [0, 0, 1], roll)
    for side, asymmetry in [('Left', 1), ('Right', .72)]:
        rotate(side + 'UpLeg', [1, 0, 0], thigh * asymmetry)
        rotate(side + 'Leg', [1, 0, 0], knee * asymmetry)
        rotate(side + 'Foot', [1, 0, 0], -knee * .15 * asymmetry)
    rotate('Spine', [1, 0, 0], spine)
    rotate('Spine1', [1, 0, 0], spine * .6)
    rotate('Neck', [1, 0, 0], -spine * .7)
    rotate('Head', [0, 0, 1], roll * .4)
    relaxation = min(phase / .76, 1)
    relaxation = relaxation * relaxation * (3 - 2 * relaxation)
    # The guard has a chin-forward head and an asymmetric left stance.  Relax
    # these during the fall, so the head and both boots can approach support
    # rather than freezing the guard posture after rotating the whole body.
    rotate('Neck', [1, 0, 0], -12 * relaxation)
    rotate('Head', [1, 0, 0], -18 * relaxation)
    rotate('LeftUpLeg', [1, 0, 0], 8 * relaxation)

    hip = names['mixamorig:Hips']
    parent = model.parents[hip]
    parent_world = initial[parent] if parent is not None else np.eye(4)
    visual_travel = np.array([0, 0, ztravel])
    nodes[hip]['translation'] = (np.asarray(guard[hip]['translation']) +
        np.linalg.solve(parent_world[:3, :3], visual_travel)).tolist()
    arm_fits = []
    if phase > 0:
        posed = fc.world(model, nodes, reader)
        fall_basis = posed[hip][:3, :3] @ np.linalg.inv(initial[hip][:3, :3])
        progress = min(phase / .76, 1)
        progress = progress * progress * (3 - 2 * progress)
        body_support_y = float(min(g['vertices'][:, 1].min() for g in model.geometry(posed)))
        contact_progress = np.clip((phase - .45) / .31, 0, 1)
        contact_progress = contact_progress * contact_progress * (3 - 2 * contact_progress)
        for side, sign in [('Left', 1), ('Right', -1)]:
            wrist = names['mixamorig:' + side + 'Hand']
            rest = np.array([sign * .205, .535, .02])
            source_target = ((1 - progress) * initial[wrist][:3, 3] + progress * rest)
            target = posed[hip][:3, 3] + fall_basis @ (source_target - initial[hip][:3, 3])
            # The late fall lets the hands come down beside the body. Relative
            # to the preplanned body support height, wrists have6.5cm clearance
            # for the actual hand thickness; no mesh/instance lift is applied.
            target[1] = ((1 - contact_progress) * target[1] +
                         contact_progress * (body_support_y + .065))
            arm = names['mixamorig:' + side + 'Arm']
            source_pole = initial[arm][:3, 3] + np.array([sign * .3, 0, .03])
            pole = posed[hip][:3, 3] + fall_basis @ (source_pole - initial[hip][:3, 3])
            pole[1] = ((1 - contact_progress) * pole[1] +
                       contact_progress * (body_support_y + .08))
            arm_fits.append(resting_arm(model, reader, nodes, names, initial, side, target, pole))
        # Carry the weapon grip toward the body fall without imposing an80deg
        # wrist bend. The partial, smooth correction retains a modest upward
        # blade angle and is bounded to50deg from the guarded local wrist.
        hand, fore = names['mixamorig:RightHand'], names['mixamorig:RightForeArm']
        posed = fc.world(model, nodes, reader)
        parent_basis = posed[fore][:3, :3]
        parent_basis /= np.linalg.norm(parent_basis, axis=0)
        initial_basis = initial[hand][:3, :3].copy()
        initial_basis /= np.linalg.norm(initial_basis, axis=0)
        desired_world = Rotation.from_matrix(fall_basis) * Rotation.from_matrix(initial_basis)
        desired_local = Rotation.from_matrix(parent_basis).inv() * desired_world
        current_local = Rotation.from_quat(nodes[hand]['rotation'])
        desired_offset = (current_local.inv() * desired_local).magnitude()
        amount = min(.6 * progress, np.deg2rad(50) / max(desired_offset, 1e-12))
        nodes[hand]['rotation'] = Slerp([0, 1], Rotation.from_quat([
            current_local.as_quat(), desired_local.as_quat()]))([amount]).as_quat()[0].tolist()
        arm_fits.append({'side': 'RightWrist', 'additionalDegrees': float(np.rad2deg(desired_offset * amount))})
    posed = fc.world(model, nodes, reader)
    xyz = np.concatenate([g['vertices'] for g in model.geometry(posed)])
    # Authored support trajectory: the lowest deforming surface is the current
    # support.  This plans actual pelvis height for a collapse/impact, unlike a
    # static lift of an invalid retarget.  Record the active support vertex and
    # heights so foot -> hip/back contact can be inspected independently.
    support = int(np.argmin(xyz[:, 1]))
    contact_y = float(xyz[support, 1])
    height_delta = .00035 - contact_y if phase > 0 else 0
    nodes[hip]['translation'] = (np.asarray(nodes[hip]['translation']) +
        np.linalg.solve(parent_world[:3, :3], [0, height_delta, 0])).tolist()
    return nodes, {'phase': float(phase), 'supportVertex': support,
        'rawSupportY': contact_y, 'authoredPelvisHeightDelta': height_delta,
        'visualPlanarTravel': visual_travel.tolist(), 'degrees': values[:-1].tolist(), 'armFit': arm_fits}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--keys', type=int, default=257)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Use a fresh candidate directory')
    if not 65 <= args.keys <= 1025:
        parser.error('Key count must be between 65 and 1025')
    reader, _ = fc.hp.load_reader(fc.hp.ROOT)
    model = fc.hp.Model(args.source, reader)
    if len(model.animations) != 1:
        parser.error('Expected one guarded source clip')
    names = {n.get('name'): i for i, n in enumerate(model.nodes)}
    guard = sample_nodes(model, model.animations[0], model.animations[0]['start'])
    initial = fc.world(model, guard, reader)
    phases = np.unique(np.r_[np.linspace(0, 1, args.keys), RECIPE[:, 0]])
    poses, support = [], []
    for phase in phases:
        pose, evidence = authored_pose(model, reader, guard, names, initial, float(phase))
        poses.append(pose)
        support.append(evidence)
    # A stationary terminal plateau occupies the last14% of the authored clip.
    terminal = copy.deepcopy(poses[next(i for i, p in enumerate(phases) if p == .86)])
    poses = [copy.deepcopy(terminal) if phase >= .86 else pose
             for phase, pose in zip(phases, poses)]
    args.output.mkdir(parents=True)
    fc.rt.SEMANTICS = fc.skin_semantics(model)
    output = args.output / 'warrior-death.glb'
    old, _ = fc.rt.append_animation(model, reader, phases * 2, poses,
        'authored-death-backward-collapse-held-corpse',
        {'source': str(args.source), 'sha256': fc.hp.sha(args.source.read_bytes()),
         'authoring': 'full body knee buckle, backward fall, support-planned pelvis height, impact and final hold',
         'visualPelvisTrajectory': True, 'simulationPositionChanged': False,
         'terminalHoldPhase': .86, 'loop': False}, output)
    # append_animation's inherited inPlace note describes retargeting; this is
    # original visual authoring with explicit fall travel, not a donor retarget.
    doc, binary, _ = reader.read_glb(output)
    doc['animations'][0]['extras']['inPlace'] = 'native entity placement remains authoritative; authored visual pelvis falls .22 units backward'
    doc['animations'][0]['extras']['retargetStatus'] = 'authored candidate; not gameplay approved'
    import struct
    encoded = json.dumps(doc, separators=(',', ':'), allow_nan=False).encode()
    encoded += b' ' * ((-len(encoded)) % 4)
    output.write_bytes(struct.pack('<4sII', b'glTF', 2, 28 + len(encoded) + len(binary)) +
        struct.pack('<II', len(encoded), 0x4E4F534A) + encoded +
        struct.pack('<II', len(binary), 0x004E4942) + bytes(binary))
    result = fc.hp.Model(output, reader)
    clip = result.animations[0]
    minimum, motion, finite, held = 1e9, 0, True, 0
    first, final, metrics = None, None, []
    terminal_xyz = np.concatenate([g['vertices'] for g in result.geometry(result.pose(clip, 2))])
    for phase in np.unique(np.r_[np.linspace(0, 1, 1025), phases]):
        posed = result.pose(clip, float(phase * 2))
        xyz = np.concatenate([g['vertices'] for g in result.geometry(posed)])
        if first is None:
            first = xyz
        minimum = min(minimum, float(xyz[:, 1].min()))
        motion = max(motion, float(np.linalg.norm(xyz - first, axis=1).max()))
        finite = finite and bool(all(np.isfinite(matrix).all() for matrix in posed.values())
                                  and np.isfinite(xyz).all())
        if phase >= .86:
            held = max(held, float(np.abs(xyz - terminal_xyz).max()))
        metrics.append({'phase': float(phase), 'minimumY': float(xyz[:, 1].min()),
                        'height': float(np.ptp(xyz[:, 1]))})
    doc, binary, _ = reader.read_glb(output)
    preserved = ('nodes', 'meshes', 'skins', 'materials', 'textures', 'images', 'samplers', 'scenes')
    hip = names['mixamorig:Hips']
    lengths = max(float(np.abs(np.asarray(p[i].get('translation', [0, 0, 0])) -
        np.asarray(guard[i].get('translation', [0, 0, 0]))).max())
        for p in poses for i in range(len(p)) if i != hip)
    checks = {'layoutPreserved': all(doc.get(k) == model.doc.get(k) for k in preserved),
        'sourceBinaryPrefixPreserved': bytes(binary[:len(old)]) == old,
        'finitePoses': finite, 'limbTranslationsPreserved': lengths < 1e-10,
        'scalesPreserved': all(p[i].get('scale') == guard[i].get('scale')
                               for p in poses for i in range(len(p))),
        'groundPass2mm': minimum >= -.002, 'fullBodyFall': motion > .5,
        'corpseHeightUnder.4': float(np.ptp(terminal_xyz[:, 1])) < .4,
        'finalHoldWithin1e-7': held < 1e-7, 'doesNotReturnToGuard': float(np.linalg.norm(terminal_xyz - first, axis=1).max()) > .5}
    receipt = {'source': str(args.source), 'sourceSha256': fc.hp.sha(args.source.read_bytes()),
        'candidateSha256': fc.hp.sha(output.read_bytes()), 'scriptSha256': fc.hp.sha(Path(__file__).read_bytes()),
        'checks': checks, 'samples': len(metrics), 'keys': len(phases), 'minimumY': minimum,
        'maximumMotion': motion, 'terminalBounds': fc.hp.bbox(terminal_xyz),
        'terminalHoldMaximumVertexDelta': held, 'supportTrajectory': support,
        'sampleMetrics': metrics, 'nativeTimingConnected': False, 'gameplaySelected': False,
        'simulationMoved': False, 'loop': False, 'terminalHoldPhase': .86,
        'status': 'candidate-needs-anatomy-contact-weapon-directions-review' if all(checks.values()) else 'rejected'}
    fc.hp.write_json(args.output / 'receipt.json', receipt)
    print(json.dumps({k: v for k, v in receipt.items() if k not in ('supportTrajectory', 'sampleMetrics')}), flush=True)


if __name__ == '__main__':
    main()
