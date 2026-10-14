"""Private finger-only Short Sword grip recipe, evaluated in memory.

Preserve all source bytes, scales, translations, wrist/arm/root tracks, meshes,
rig and materials. Only fifteen right-finger local rotations and an explicit
socket translation are candidates. No GLB/native export, bundle or activation.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace

import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation
import foot_contact as fc
import shield_grip_candidate as grip
import starter_shield_candidate as render_tools
from attack_settle import sample_nodes

WORKSPACE = Path(__file__).resolve().parents[3]
LEFT = WORKSPACE / 'diagnostics/warrior-left-grip-library-20261013-r1'
spec = importlib.util.spec_from_file_location('short_sword_exact_surface', LEFT / 'exact_surface_field.py')
surface_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(surface_module)
FINGERS = ('Index', 'Middle', 'Ring', 'Pinky', 'Thumb')
SWORD = WORKSPACE / 'diagnostics/starter-sword-20261013-r1/candidate-r1/short-sword-original-candidate-r1.glb'
SOCKET = WORKSPACE / 'diagnostics/starter-sword-20261013-r1/candidate-r1/SOCKET-CANDIDATE.json'
SHIELD = WORKSPACE / 'diagnostics/starter-shield-20261013-r1/refinement-r2/buckler-original-candidate-r1.glb'
SHIELD_SOCKET = WORKSPACE / 'diagnostics/shield-grip-20261013-r1/exact-contact-r2/SOCKET-CANDIDATE-PROVENANCE-FIXED.json'
EXPECTED_SWORD = '0204d107bdf5ca131bfe5affb7449d57f292d19cddcd16c9a27ac41ba4a078b8'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def file_record(path):
    return {'path': str(path), 'sha256': sha(path), 'bytes': path.stat().st_size}


def write(path, value):
    if path.exists():
        raise ValueError('Preserve previous outputs')
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def socket_matrix(record):
    t = record['localTransform']
    matrix = np.eye(4)
    matrix[:3, :3] = Rotation.from_quat(t['rotationXYZW']).as_matrix() * t['uniformScale']
    matrix[:3, 3] = t['translation']
    return matrix


def pose(body, reader, clip, time, rotations):
    nodes = sample_nodes(body, clip, time)
    for node, value in rotations.items():
        nodes[node]['rotation'] = list(value)
    return nodes, fc.world(body, nodes, reader)


def positions(body, world):
    rows = []
    for primitive in body.primitives:
        if primitive['skin'] is None:
            rows.append((np.c_[primitive['positions'], np.ones(len(primitive['positions']))] @ world[primitive['node']].T)[:, :3])
        else:
            rows.append(fc.points(body, primitive, world, np.arange(len(primitive['positions']))))
    return np.concatenate(rows)


def static_equal(a, b):
    if any(a.doc.get(k) != b.doc.get(k) for k in grip.STATIC_KEYS):
        return False
    if len(a.primitives) != len(b.primitives) or len(a.skins) != len(b.skins):
        return False
    for x, y in zip(a.skins, b.skins):
        if not np.array_equal(x[0], y[0]) or not np.array_equal(x[1], y[1]):
            return False
    for x, y in zip(a.primitives, b.primitives):
        if any(x.get(k) != y.get(k) for k in ('node', 'skin', 'material')):
            return False
        if any(not np.array_equal(x[k], y[k]) for k in ('positions', 'normals', 'uv', 'colors', 'triangles')):
            return False
        if len(x['pairs']) != len(y['pairs']) or any(not np.array_equal(p, q) for xx, yy in zip(x['pairs'], y['pairs']) for p, q in zip(xx, yy)):
            return False
    for x, y in zip(a.doc.get('images', []), b.doc.get('images', [])):
        bx = a.reader.uri_bytes(x['uri']) if 'uri' in x else a.reader.view(x['bufferView'])[0]
        by = b.reader.uri_bytes(y['uri']) if 'uri' in y else b.reader.view(y['bufferView'])[0]
        if bx != by:
            return False
    return True


def skin_groups(body, names):
    count = sum(len(p['positions']) for p in body.primitives)
    masks = {f: np.zeros(count, bool) for f in FINGERS}
    masks.update({f + '-distal': np.zeros(count, bool) for f in FINGERS})
    masks.update({f: np.zeros(count, bool) for f in ('Palm', 'ALL-RIGHT-INFLUENCE')})
    all_fingers = {names['mixamorig:RightHand' + f + str(i)] for f in FINGERS for i in (1, 2, 3)}
    hand, fore = names['mixamorig:RightHand'], names['mixamorig:RightForeArm']
    offset = 0
    for p in body.primitives:
        n = len(p['positions'])
        if p['skin'] is not None:
            joints, _ = body.skins[p['skin']]
            def weights(target):
                return sum((ws * np.isin(joints[js], list(target))).sum(axis=1) for js, ws in p['pairs'])
            for f in FINGERS:
                ids = {names['mixamorig:RightHand' + f + str(i)] for i in (1, 2, 3)}
                masks[f][offset:offset+n] = weights(ids) > .55
                masks[f + '-distal'][offset:offset+n] = weights({names['mixamorig:RightHand' + f + '3']}) > .5
            masks['Palm'][offset:offset+n] = weights({hand}) > .55
            masks['ALL-RIGHT-INFLUENCE'][offset:offset+n] = weights(all_fingers | {hand, fore}) > 1e-7
        offset += n
    selected = np.flatnonzero(masks['ALL-RIGHT-INFLUENCE'])
    result = {key: value[selected] for key, value in masks.items()}
    if any(not result[f + '-distal'].any() for f in FINGERS):
        raise ValueError('All five actual distal skin groups required')
    return selected, result


class GripSurface:
    def __init__(self, sword):
        self.volumes = []
        self.names = []
        for name in ('closed-cylindrical-leather-core', 'raised-red-brown-helical-leather-wrap'):
            primitive = next(p for p in sword.primitives if sword.nodes[p['node']].get('name') == name)
            triangles = primitive['positions'][primitive['triangles']]
            self.volumes.append(surface_module.Field(triangles, grip.closed_inside))
            self.names.append({'name': name, 'triangles': len(triangles)})
        # Treat each physical solid separately; concatenated ray parity must
        # never substitute for the explicit union-containment contract.
        self.triangles = np.concatenate([f.triangles for f in self.volumes])

    def measure(self, points):
        fields = np.array([field(points) for field in self.volumes])
        # A negative zero may be on an internal component face while still
        # inside another solid. Never infer union containment from its merged
        # distance sign alone. For near-zero queries use closed winding again.
        component_inside = fields < 0
        for i, field in enumerate(self.volumes):
            ambiguous = abs(fields[i]) <= 1e-10
            if ambiguous.any():
                component_inside[i, ambiguous] = grip.closed_inside(points[ambiguous], field.triangles)
        inside = component_inside.any(axis=0)
        distance = abs(fields).min(axis=0)
        return np.where(inside, -distance, distance), inside

    def __call__(self, points):
        return self.measure(points)[0]


class CertifiedOutsideCache:
    """Actual triangle measurements with per-point conservative movement bounds.

    An originally outside point cannot cross any component boundary while its
    displacement is smaller than its measured nearest triangle distance.
    Triangle distance is 1-Lipschitz. Only strictly positive certified intervals
    are reused; uncertain/interior points are evaluated against the real solids.
    """
    def __init__(self, surface):
        self.surface = surface
        self.points = self.distance = self.inside = None
        self.exactQueries = self.certifiedQueries = 0
        self.maximumMovementBound = 0

    def measure(self, points):
        if self.points is None:
            signed, inside = self.surface.measure(points)
            self.points = points.copy(); self.distance = abs(signed); self.inside = inside.copy()
            self.exactQueries += len(points)
            return signed, abs(signed), inside, 0.0
        movement = np.linalg.norm(points-self.points, axis=1)
        valid = (~self.inside) & (self.distance-movement > 1e-9)
        uncertain = ~valid
        lower = self.distance-movement; upper = self.distance+movement
        inside = np.zeros(len(points), bool)
        if uncertain.any():
            signed, actual_inside = self.surface.measure(points[uncertain])
            lower[uncertain] = signed; upper[uncertain] = abs(signed); inside[uncertain] = actual_inside
            self.points[uncertain] = points[uncertain]
            self.distance[uncertain] = abs(signed); self.inside[uncertain] = actual_inside
        bound = float(movement[valid].max()) if valid.any() else 0.0
        self.maximumMovementBound = max(self.maximumMovementBound, bound)
        self.exactQueries += int(uncertain.sum()); self.certifiedQueries += int(valid.sum())
        return lower, upper, inside, bound


def source_skin(body, reader, nodes, hand, socket, selected):
    world = fc.world(body, nodes, reader)
    transform = world[hand] @ socket
    rows = []; offset = 0
    for primitive in body.primitives:
        n = len(primitive['positions'])
        indices = selected[(selected >= offset) & (selected < offset+n)] - offset
        if len(indices):
            if primitive['skin'] is None:
                rows.append((np.c_[primitive['positions'][indices], np.ones(len(indices))] @ world[primitive['node']].T)[:, :3])
            else:
                rows.append(fc.points(body, primitive, world, indices))
        offset += n
    points = np.concatenate(rows)
    return (np.c_[points, np.ones(len(points))] @ np.linalg.inv(transform).T)[:, :3], world


def calibrate_socket(body, reader, guard, hand, selected, masks, surface, original):
    matrix = socket_matrix(original)
    scale = original['localTransform']['uniformScale']
    candidates = []
    # Independently inspect current skin at hand-local offsets, not old R4 TRS.
    for y in (.012, .020, .028):
        for x in (-.004, 0, .004):
            for z in (0, .006):
                trial = matrix.copy()
                trial[:3, 3] += [x, y, z]
                points, _ = source_skin(body, reader, guard, hand, trial, selected)
                exact = surface(points) * scale
                palm = exact[masks['Palm']]
                score = np.linalg.norm(np.minimum(palm - .0003, 0)) * 1000
                score += sum(abs(exact[masks[f + '-distal']]).min() for f in FINGERS) * 30
                candidates.append((score, trial, {'handLocalOffset': [x, y, z], 'palmInside': int((palm < 0).sum()), 'score': float(score)}))
    _, chosen, metric = min(candidates, key=lambda x: x[0])
    return chosen, metric, [c[2] for c in candidates]


def fit_finger(body, reader, guard, names, selected, masks, surface, socket, finger, global_clearance=False):
    hand = names['mixamorig:RightHand']
    chain = [names['mixamorig:RightHand' + finger + str(i)] for i in (1, 2, 3)]
    initial = fc.world(body, guard, reader)
    bases = [Rotation.from_quat(guard[i]['rotation']) for i in chain]
    center = (initial[hand] @ socket @ np.array([0, 0, 0, 1]))[:3]
    root = initial[chain[0]][:3, 3]
    direction = initial[chain[0]][:3, 1]
    world_axis = np.cross(direction, center - root)
    world_axis /= np.linalg.norm(world_axis)
    axes = []
    for i in chain:
        axis = np.linalg.inv(initial[i][:3, :3]) @ world_axis
        axes.append(axis / np.linalg.norm(axis))
    axis_opposition = np.linalg.inv(initial[chain[0]][:3, :3]) @ (initial[hand] @ socket)[:3, 1]
    axis_opposition /= np.linalg.norm(axis_opposition)
    thumb = finger == 'Thumb'
    # Delta hinges from the current source grip, permitting it to open as well
    # as close. The root has a second bounded rotation axis. These inferred
    # axes may be nearly parallel; they are not anatomical spread/opposition.
    lower = np.deg2rad([-65, -70, -65, -35 if thumb else -18])
    upper = np.deg2rad([70, 85, 70, 35 if thumb else 18])
    if global_clearance:
        lower, upper = np.deg2rad([-12, -12, -12, -12]), np.deg2rad([12, 12, 12, 12])
    scale = float(np.linalg.norm(socket[:3, 0]))
    def target(values):
        nodes = copy.deepcopy(guard)
        for k, i in enumerate(chain):
            delta = Rotation.from_rotvec(axes[k] * values[k])
            if k == 0:
                delta = Rotation.from_rotvec(axis_opposition * values[3]) * delta
            nodes[i]['rotation'] = (bases[k] * delta).as_quat().tolist()
        points, _ = source_skin(body, reader, nodes, hand, socket, selected)
        return nodes, points
    skin_mask = masks[finger]
    distal_mask = masks[finger + '-distal']
    combined = masks['ALL-RIGHT-INFLUENCE'] if global_clearance else skin_mask | distal_mask
    distal_local = distal_mask[combined]
    def residual(values):
        _, points = target(values)
        exact = surface(points[combined]) * scale
        collision = np.minimum(exact - (.00005 if global_clearance else .00018), 0) * (1100 if global_clearance else 700)
        nearest = np.sort(abs(exact[distal_local]))[:6]
        contact = (nearest - .00065) * 160
        regularize = values * .004
        return np.r_[collision, contact, regularize]
    seeds = [np.zeros(4), np.deg2rad([25, 25, 15, 0]), np.deg2rad([-25, -25, -15, 0])]
    if thumb:
        seeds += [np.deg2rad([0, 0, 0, 25]), np.deg2rad([0, 0, 0, -25])]
    if global_clearance:
        seeds = [np.zeros(4)]
    solutions = [least_squares(residual, seed, bounds=(lower, upper), max_nfev=60 if global_clearance else 90, ftol=1e-8, xtol=1e-8, gtol=1e-8) for seed in seeds]
    solution = min(solutions, key=lambda s: np.linalg.norm(s.fun))
    nodes, points = target(solution.x)
    exact = surface(points[combined]) * scale
    return {i: nodes[i]['rotation'] for i in chain}, {
        'finger': finger, 'deltaHingeDegrees': np.rad2deg(solution.x).tolist(),
        'hingeAxesLocal': [a.tolist() for a in axes], 'rootSecondRotationAxisLocal': axis_opposition.tolist(),
        'rootAxesAbsoluteAngleDegrees': float(np.rad2deg(np.arccos(np.clip(abs(axes[0] @ axis_opposition), 0, 1)))),
        'axisSemantics': 'Two explicitly bounded inferred root rotation axes; anatomical flexion/spread/opposition is not certified.',
        'boundsDegrees': [np.rad2deg(lower).tolist(), np.rad2deg(upper).tolist()],
        'solverSuccess': bool(solution.success), 'residual': float(np.linalg.norm(solution.fun)),
        'clearanceScope': 'Every actual skin vertex with any positive right ForeArm/Hand/finger influence' if global_clearance else 'Own dominant finger skin and distal groups; full combined recipe must be audited separately',
        'sampledInside': int((exact < 0).sum()), 'minimumSigned': float(exact.min()),
        'distalNearest': float(abs(exact[distal_local]).min())}


def audit_pose(body, reader, clip, time, rotations, hand, socket, selected, masks, surface, cache=None):
    source_nodes = sample_nodes(body, clip, time)
    nodes, world = pose(body, reader, clip, time, rotations)
    maximum = 0
    for i in range(len(nodes)):
        if nodes[i].get('translation') != source_nodes[i].get('translation') or nodes[i].get('scale') != source_nodes[i].get('scale'):
            raise ValueError('Local translation/scale changed')
        if i not in rotations:
            maximum = max(maximum, float(abs(reader.node_matrix(nodes[i]) - reader.node_matrix(source_nodes[i])).max()))
    xyz = positions(body, world)
    matrix = world[hand] @ socket
    points = (np.c_[xyz[selected], np.ones(len(selected))] @ np.linalg.inv(matrix).T)[:, :3]
    singular = np.linalg.svd(matrix[:3, :3], compute_uv=False)
    if cache is None:
        exact, inside = surface.measure(points)
        upper = abs(exact); bound = 0.0
    else:
        exact, upper, inside, bound = cache.measure(points)
    rows = {}
    for key, mask in masks.items():
        values = exact[mask]
        minimum = float(values.min())
        rows[key] = {'vertices': int(mask.sum()), 'inside': int(inside[mask].sum()),
                     'minimumSignedWorldLowerBound': minimum * float(singular[-1] if minimum >= 0 else singular[0]),
                     'nearestWorldUpperBound': float(upper[mask].min()) * float(singular[0])}
    sword_xyz = np.concatenate([g['vertices'] for g in render_tools.transformed_geometry(SWORD_MODEL, matrix, 0)])
    return rows, {'bodyMinimumY': float(xyz[:, 1].min()), 'swordMinimumY': float(sword_xyz[:, 1].min()),
                  'maximumNonfingerLocalDelta': maximum, 'maximumCachedSourceDistanceBound': bound}, nodes, world


def render_previews(body, reader, clip, role, rotations, hand, socket, output):
    body.load_materials(); SWORD_MODEL.load_materials(); SHIELD_MODEL.load_materials()
    shift = max(body.materials, default=-1) + 1
    shield_shift = shift + len(SWORD_MODEL.materials)
    mixed = SimpleNamespace(materials={**body.materials, **{k+shift: v for k, v in SWORD_MODEL.materials.items()}, **{k+shield_shift: v for k, v in SHIELD_MODEL.materials.items()}})
    names = {n.get('name'): i for i, n in enumerate(body.nodes)}
    hand_nodes = {hand} | {names['mixamorig:RightHand'+f+str(i)] for f in FINGERS for i in (1, 2, 3)}
    left_hand = names['mixamorig:LeftHand']
    left_socket = socket_matrix(SHIELD_SOCKET_RECORD)
    views = []
    # Idle uses one representative preview; its full motion is audited densely.
    # Walk/attack retain three views; isolate the whole hand from occlusion.
    for phase in ((0,) if role == 'idle' else (0, .5, 1)):
        _, world = pose(body, reader, clip, clip['start']+phase*(clip['end']-clip['start']), rotations)
        matrix = world[hand] @ socket
        body_geometry = body.geometry(world)
        sword_geometry = render_tools.transformed_geometry(SWORD_MODEL, matrix, shift)
        geometry = body_geometry + sword_geometry + render_tools.transformed_geometry(SHIELD_MODEL, world[left_hand] @ left_socket, shield_shift)
        hand_geometry = []
        for gp, primitive in zip(body_geometry, body.primitives):
            if primitive['skin'] is None:
                continue
            joints, _ = body.skins[primitive['skin']]
            weights = sum((ws * np.isin(joints[js], list(hand_nodes))).sum(axis=1) for js, ws in primitive['pairs'])
            keep = np.mean(weights[primitive['triangles']], axis=1) > .45
            if keep.any():
                item = dict(gp); item['triangles'] = primitive['triangles'][keep]; hand_geometry.append(item)
        handle_geometry = [gp for gp in sword_geometry if SWORD_MODEL.nodes[gp['node']].get('name') in ('closed-cylindrical-leather-core', 'raised-red-brown-helical-leather-wrap')]
        center = (matrix @ np.array([0, 0, 0, 1]))[:3]
        for label, yaw, ppm in [('body-front', 0, 510), ('body-side', 90, 510), ('isolated-hand-palm', 90, 4200), ('isolated-hand-dorsal', -90, 4200), ('isolated-hand-front', 0, 4200)]:
            full = label.startswith('body')
            isolated = label.startswith('isolated')
            drawn = hand_geometry + handle_geometry if isolated else geometry
            picture, stats = fc.hp.raster(mixed, drawn, yaw, (896, 896), [448, 820] if full else [448, 448], np.zeros(3) if full else center, ppm, 15, 1, False)
            path = output / f'{role}-{phase:g}-{label}.png'
            picture.save(path)
            views.append({'phase': phase, 'view': label, 'file': path.name, 'stats': stats,
                          'geometryScope': 'RightHand/finger triangles with mean influence>.45 and actual whole grip; wrist cut is a preview isolation, not a mesh edit/collision subset' if isolated else 'Whole unmodified source body plus actual Short Sword and canonical left buckler'})
    return views


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--samples', default=1025, type=int)
    parser.add_argument('--recipe', type=Path)
    parser.add_argument('--fit-only', action='store_true')
    parser.add_argument('--no-renders', action='store_true')
    parser.add_argument('--previews-only', action='store_true')
    parser.add_argument('--refine-pinky', action='store_true', help='Refine only Pinky rotations by bounded deltas against all right-influenced skin after composing every finger')
    args = parser.parse_args()
    if args.output.exists() or not 17 <= args.samples <= 2049:
        parser.error('Use a fresh directory and 17..2049 samples')
    if (args.refine_pinky or args.previews_only) and args.recipe is None:
        parser.error('Refinement/preview-only requires an exact recipe input')
    if args.fit_only and args.previews_only:
        parser.error('Fit-only and previews-only are distinct stages')
    if sha(SWORD) != EXPECTED_SWORD:
        parser.error('Exact immutable Short Sword candidate required')
    args.output.mkdir(parents=True)
    reader, reader_info = fc.hp.load_reader(fc.hp.ROOT)
    global SWORD_MODEL, SHIELD_MODEL, SHIELD_SOCKET_RECORD
    SWORD_MODEL = fc.hp.Model(SWORD, reader)
    SHIELD_MODEL = fc.hp.Model(SHIELD, reader)
    SHIELD_SOCKET_RECORD = json.loads(SHIELD_SOCKET.read_text())
    if sha(SHIELD) != SHIELD_SOCKET_RECORD['shieldSha256']:
        raise ValueError('Canonical read-only left shield identity mismatch')
    surface = GripSurface(SWORD_MODEL)
    clip_map = json.loads((LEFT / 'CLIP-MAP.json').read_text())
    sources = {row['role']: row['candidate'] for row in clip_map['clips'] if row['role'] in ('idle', 'walk', 'attack')}
    if set(sources) != {'idle', 'walk', 'attack'}:
        raise ValueError('Exact three real compatible roles required')
    bodies = {}
    for role, record in sources.items():
        path = Path(record['path'])
        if sha(path) != record['sha256']:
            raise ValueError('Left-composed source hash mismatch')
        bodies[role] = fc.hp.Model(path, reader)
    body = bodies['idle']
    if any(not static_equal(body, other) or len(other.animations) != 1 for other in bodies.values()):
        raise ValueError('Exact resolved static mesh/rig/material/layout required')
    names = {n.get('name'): i for i, n in enumerate(body.nodes)}
    hand = names['mixamorig:RightHand']
    selected, masks = skin_groups(body, names)
    guard = sample_nodes(body, body.animations[0], body.animations[0]['start'])
    original = json.loads(SOCKET.read_text())
    if original['swordSha256'] != EXPECTED_SWORD:
        raise ValueError('Socket candidate item mismatch')
    socket_reference_body = Path(original['bodyPath'])
    if sha(socket_reference_body) != original['bodySha256'] or not static_equal(body, fc.hp.Model(socket_reference_body, reader)):
        raise ValueError('Original Short Sword socket body and exact current static rig/mesh/material layout must agree')
    fit_metrics = []
    if args.recipe:
        recipe = json.loads(args.recipe.read_text())
        if recipe['sword']['sha256'] != EXPECTED_SWORD or recipe['referenceBody']['sha256'] != sha(body.path):
            raise ValueError('Recipe reference identity mismatch')
        rotations = {int(i): q for i, q in recipe['rotations'].items()}
        socket_record = recipe['socket']
        socket = socket_matrix(socket_record)
        fit_metrics = recipe['fit']
        for metric in fit_metrics:
            if 'rootSpreadOppositionAxisLocal' in metric:
                second = np.array(metric.pop('rootSpreadOppositionAxisLocal'))
                metric['rootSecondRotationAxisLocal'] = second.tolist()
                metric['rootAxesAbsoluteAngleDegrees'] = float(np.rad2deg(np.arccos(np.clip(abs(np.array(metric['hingeAxesLocal'][0]) @ second), 0, 1))))
                metric['axisSemantics'] = 'Two explicitly bounded inferred root rotation axes; anatomical flexion/spread/opposition is not certified.'
        if args.refine_pinky:
            combined_guard = copy.deepcopy(guard)
            for node, quaternion in rotations.items():
                combined_guard[node]['rotation'] = quaternion
            fitted, metric = fit_finger(body, reader, combined_guard, names, selected, masks, surface, socket, 'Pinky', global_clearance=True)
            rotations.update(fitted)
            metric['stage'] = 'Explicit bounded second-stage refinement over the entire composed hand, correcting shared skin influence.'
            fit_metrics.append(metric)
            print(json.dumps(metric), flush=True)
    else:
        socket, calibration, trials = calibrate_socket(body, reader, guard, hand, selected, masks, surface, original)
        write(args.output / 'SOCKET-SEARCH.json', trials)
        socket_record = {'bone': 'mixamorig:RightHand', 'sourceGrip': [0, 0, 0], 'swordSha256': EXPECTED_SWORD,
                         'localTransform': {**original['localTransform'], 'translation': socket[:3, 3].tolist()},
                         'referenceSocket': file_record(SOCKET), 'translationCalibration': calibration,
                         'orientationAndUniformScalePreserved': True, 'oldR4Applied': False,
                         'method': 'Current real right-skin sampled at independently bounded hand-local offsets; only translation changes. No previous R4 TRS copied.'}
        rotations = {}
        for finger in FINGERS:
            fitted, metrics = fit_finger(body, reader, guard, names, selected, masks, surface, socket, finger)
            rotations.update(fitted); fit_metrics.append(metrics)
            print(json.dumps(metrics), flush=True)
    expected_nodes = {names['mixamorig:RightHand'+f+str(i)] for f in FINGERS for i in (1, 2, 3)}
    if set(rotations) != expected_nodes or any(len(q) != 4 or not np.isfinite(q).all() or abs(np.linalg.norm(q)-1) > 1e-6 for q in rotations.values()):
        raise ValueError('Exactly fifteen finite unit right-finger quaternions required')
    if socket_record['bone'] != 'mixamorig:RightHand' or socket_record['sourceGrip'] != [0, 0, 0]:
        raise ValueError('Exact right-hand socket and geometric grip origin required')
    if any(socket_record['localTransform'][k] != original['localTransform'][k] for k in ('rotationXYZW', 'uniformScale')):
        raise ValueError('Original Short Sword socket orientation/scale must be preserved')
    offset = np.array(socket_record['localTransform']['translation']) - np.array(original['localTransform']['translation'])
    if not np.isfinite(offset).all() or np.any(abs(offset) > [.01, .04, .015]):
        raise ValueError('Socket translation exceeds explicit candidate search bounds')
    recipe = {'referenceBody': file_record(body.path), 'sword': file_record(SWORD), 'leftClipMap': file_record(LEFT/'CLIP-MAP.json'),
              'readOnlyLeftShield': file_record(SHIELD), 'readOnlyLeftSocket': file_record(SHIELD_SOCKET),
              'rotations': rotations, 'fingerNames': {i: body.nodes[i]['name'] for i in sorted(rotations)}, 'fit': fit_metrics, 'socket': socket_record,
              'scope': 'Candidate local right-finger rotations only; every other local rotation, every translation/scale, rig, mesh, UV/material/image byte and source animation is preserved. This recipe is applied only in memory here.',
              'sourceFingerMotionClampExplicit': True, 'clampPolicy': 'Replace fifteen local right-finger rotation tracks with this constant equipment grip; source variation/delta reported for each real clip.',
              'approved': False, 'selected': False, 'nativeExported': False, 'bundleCreated': False}
    write(args.output/'GRIP-RECIPE.json', recipe)
    write(args.output/'SOCKET-CANDIDATE.json', socket_record)
    if args.fit_only:
        return
    if args.previews_only:
        views = []
        for role, body in bodies.items():
            role_dir = args.output / role; role_dir.mkdir()
            rows = render_previews(body, reader, body.animations[0], role, rotations, hand, socket, role_dir)
            write(role_dir/'VIEWS.json', rows)
            views += [{'role': role, **row} for row in rows]
        write(args.output/'PREVIEW-RECEIPT.json', {'recipeInput': file_record(args.recipe), 'tool': file_record(Path(__file__)), 'sword': file_record(SWORD), 'views': views, 'approved': False})
        return
    reports = []
    for role, body in bodies.items():
        role_dir = args.output / role; role_dir.mkdir()
        clip = body.animations[0]
        selected, masks = skin_groups(body, names)
        times = np.unique(np.r_[np.linspace(clip['start'], clip['end'], args.samples), np.concatenate([c['times'] for c in clip['channels']])])
        rows = []; variation = delta = 0
        cache = CertifiedOutsideCache(surface)
        first = sample_nodes(body, clip, clip['start'])
        for index, time in enumerate(times):
            measurements, invariant, source_nodes, _ = audit_pose(body, reader, clip, float(time), rotations, hand, socket, selected, masks, surface, cache)
            before = sample_nodes(body, clip, float(time))
            for node in rotations:
                variation = max(variation, float(abs(reader.node_matrix(before[node])-reader.node_matrix(first[node])).max()))
                delta = max(delta, float(abs(reader.node_matrix(source_nodes[node])-reader.node_matrix(before[node])).max()))
            rows.append({'time': float(time), 'phase': float((time-clip['start'])/(clip['end']-clip['start'])), 'groups': measurements, **invariant})
            if index % 128 == 0:
                print(json.dumps({'role': role, 'completed': index+1, 'poses': len(times)}), flush=True)
        checks = {'allSampledRightInfluencedSkinOutsideGrip': all(r['groups']['ALL-RIGHT-INFLUENCE']['inside'] == 0 for r in rows),
                  'allFiveDistalProximity': all(r['groups'][f+'-distal']['nearestWorldUpperBound'] <= .002 for r in rows for f in FINGERS),
                  'bodyFloor': min(r['bodyMinimumY'] for r in rows) >= -.002,
                  'swordFloor': min(r['swordMinimumY'] for r in rows) >= -.002,
                  'maximumNonfingerLocalDeltaZero': max(r['maximumNonfingerLocalDelta'] for r in rows) == 0,
                  'allTranslationsScalesExact': True, 'sourceBytesPreserved': sha(body.path) == sources[role]['sha256']}
        report = {'role': role, 'source': file_record(body.path), 'poses': len(times), 'regularSamples': args.samples,
                  'rightInfluencedVerticesPerPose': len(selected), 'checks': checks, 'maximumSourceFingerLocalVariation': variation,
                  'maximumSourceToRecipeFingerLocalDelta': delta, 'sourceFingerClampExplicit': variation > 1e-6,
                  'minimumBodyY': min(r['bodyMinimumY'] for r in rows), 'minimumSwordY': min(r['swordMinimumY'] for r in rows),
                  'minimumSignedWorldLowerBound': min(r['groups']['ALL-RIGHT-INFLUENCE']['minimumSignedWorldLowerBound'] for r in rows),
                  'maximumFiveDistalNearestWorldUpperBound': max(r['groups'][f+'-distal']['nearestWorldUpperBound'] for r in rows for f in FINGERS),
                  'distanceCertification': {'exactActualTriangleQueries': cache.exactQueries, 'positiveOutsideMovementCertifiedQueries': cache.certifiedQueries,
                                           'maximumSourceDistanceErrorBound': cache.maximumMovementBound,
                                           'rule': 'Actual nearest triangle distance plus per-point displacement bounds. Reuse only originally outside points with strictly positive lower bounds; recompute every uncertain/interior point using each real closed solid. SVD then bounds world units.'},
                  'samples': rows, 'anatomyApproved': False, 'triangleTriangleNonintersectionProved': False}
        write(role_dir/'DENSE-AUDIT.json', report)
        if not args.no_renders:
            write(role_dir/'VIEWS.json', render_previews(body, reader, clip, role, rotations, hand, socket, role_dir))
        reports.append({k: v for k, v in report.items() if k != 'samples'})
        print(json.dumps({'role': role, 'checks': checks}), flush=True)
    write(args.output/'DELIVERY.json', {'recipe': file_record(args.output/'GRIP-RECIPE.json'), 'socket': file_record(args.output/'SOCKET-CANDIDATE.json'),
          'sword': file_record(SWORD), 'readOnlyLeftShield': file_record(SHIELD), 'readOnlyLeftSocket': file_record(SHIELD_SOCKET), 'reports': reports, 'actualClosedVolumes': surface.names,
          'insideMethod': 'Each closed actual core/wrap separately: AABB-certified nearest triangles, guarded ray parity with winding fallback; inside is OR across solids. Signed magnitude is nearest component surface, not penetration depth of the union.',
          'allTechnicalChecksPass': all(all(r['checks'].values()) for r in reports),
          'dependencies': [file_record(Path(__file__)), file_record(Path(fc.__file__)), file_record(Path(grip.__file__)), file_record(Path(render_tools.__file__)), file_record(Path(fc.hp.__file__)), file_record(Path(surface_module.__file__)), reader_info],
          'limits': ['Vertex/handle triangle sampling does not prove triangle-triangle or thumb/finger self-nonintersection.',
                     'Dense phases plus source keys are sampled, not a continuous collision certificate.',
                     'Sword guard/blade/pommel, shield/body interactions, anatomical pressure and artistic acceptance remain separate gates.',
                     'Python in-memory recipe evaluation and CPU previews, not native C++ or gameplay.',
                     'No scales, source meshes/rig/materials, source clips, body masters, operational docs, registry, normal build, runtime, native export or bundle changed.'],
          'approved': False, 'runtimeSelected': False, 'anatomyApproved': False})


if __name__ == '__main__':
    main()
