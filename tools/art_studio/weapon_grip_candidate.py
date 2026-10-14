"""Author a bounded finger-only grip candidate, preserving wrist and master.

Fits joint endpoints to a calibrated cylindrical handle. This is a diagnostic
pose fit, not a collision/anatomy approval or equipment selection.
"""
import argparse
import copy
import json
from pathlib import Path
from types import SimpleNamespace
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation
import foot_contact as fc
from attack_settle import sample_nodes


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('body', type=Path)
    p.add_argument('weapon', type=Path)
    p.add_argument('calibration', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--grip-recipe', type=Path)
    p.add_argument('--grip-reference', type=Path)
    p.add_argument('--preview-phases', default='0,.5,1')
    args = p.parse_args()
    preview_phases = [float(value) for value in args.preview_phases.split(',')]
    if not preview_phases or len(preview_phases)>16 or any(not np.isfinite(value) or not 0<=value<=1 for value in preview_phases):
        p.error('Preview phases must be 1..16 finite values in [0,1]')
    if args.output.exists(): p.error('Use a fresh candidate directory')
    record = json.loads(args.calibration.read_text(encoding='utf-8-sig'))
    if (args.grip_recipe is None)!=(args.grip_reference is None):
        p.error('A reused grip requires its original reference body')
    if (args.grip_recipe is None and fc.hp.sha(args.body.read_bytes()) != record['bodySha256']) or fc.hp.sha(args.weapon.read_bytes()) != record['weaponSha256']:
        p.error('Calibration source mismatch')
    reader, _ = fc.hp.load_reader(fc.hp.ROOT)
    body, weapon = fc.hp.Model(args.body, reader), fc.hp.Model(args.weapon, reader)
    if len(body.animations)!=1:
        p.error('Grip authoring requires exactly one body clip')
    reused = None
    if args.grip_recipe:
        reused = json.loads(args.grip_recipe.read_text(encoding='utf-8-sig'))
        reference = fc.hp.Model(args.grip_reference, reader)
        if fc.hp.sha(args.grip_reference.read_bytes()) != reused['bodySha256'] or reused['bodySha256'] != record['bodySha256']:
            p.error('Recipe reference body/calibration mismatch')
        if any(body.doc.get(k)!=reference.doc.get(k) for k in ('nodes','meshes','skins','materials','textures','images')):
            p.error('Grip reuse requires identical skeleton, mesh and material layout')
    clip = body.animations[0]
    guard = sample_nodes(body, clip, clip['start'])
    names = {n.get('name'): i for i, n in enumerate(body.nodes)}
    hand = names[record['bone']]
    local = record['localTransform']
    socket = np.eye(4)
    socket[:3, :3] = Rotation.from_quat(local['rotationXYZW']).as_matrix() * local['uniformScale']
    socket[:3, 3] = local['translation']
    initial = fc.world(body, guard, reader)
    transform = initial[hand] @ socket
    center = (transform @ np.r_[record['sourceGrip'], 1])[:3]
    axis = transform[:3, 1]; axis = axis / np.linalg.norm(axis)
    rotations, metrics = {}, []
    for finger in ('Index', 'Middle', 'Ring', 'Pinky', 'Thumb'):
        chain = [names['mixamorig:RightHand' + finger + str(i)] for i in (1, 2, 3)]
        if reused:
            for ni in chain:
                q = reused['rotations'][str(ni)]
                if len(q)!=4 or not np.isfinite(q).all() or abs(np.linalg.norm(q)-1)>.001:
                    raise ValueError('Invalid reused finger rotation')
                rotations[ni] = q
            continue
        root = initial[chain[0]][:3, 3]
        on_axis = center + axis * ((root-center) @ axis)
        radial = root-on_axis
        radial -= axis*(radial@axis)
        radial /= np.linalg.norm(radial)
        # Fit a wrap across the far side, with middle phalanx near the handle.
        radius = .008
        target = on_axis - radial * radius
        tip_length = .012 if finger != 'Pinky' else .009
        bases = [Rotation.from_quat(guard[i]['rotation']) for i in chain]
        # One hinge per phalanx: a free 3D rotation can twist the finger mesh
        # toward an unreachable target while reporting a small endpoint error.
        direction = initial[chain[0]][:3, 1]
        world_axis = np.cross(direction, center-root)
        if np.linalg.norm(world_axis) < 1e-8:
            raise ValueError('Cannot infer finger flexion plane')
        world_axis /= np.linalg.norm(world_axis)
        hinge_axes = []
        for ni in chain:
            axis_local = np.linalg.inv(initial[ni][:3,:3]) @ world_axis
            hinge_axes.append(axis_local/np.linalg.norm(axis_local))
        def nodes_for(values):
            nodes = copy.deepcopy(guard)
            for k, ni in enumerate(chain):
                nodes[ni]['rotation'] = (bases[k] * Rotation.from_rotvec(hinge_axes[k]*values[k])).as_quat().tolist()
            return nodes
        def endpoints(values):
            world = fc.world(body, nodes_for(values), reader)
            tip = (world[chain[2]] @ np.array([0, tip_length, 0, 1]))[:3]
            middle = world[chain[2]][:3, 3]
            return tip, middle
        def residual(values):
            tip, middle = endpoints(values)
            delta = middle-center; radial_distance = np.linalg.norm(delta-axis*(delta@axis))
            return np.r_[(tip-target)*100, (radial_distance-radius)*30, values*.012]
        bound = np.deg2rad([95,110,80])
        solution = least_squares(residual, np.deg2rad([40,50,30]), bounds=(np.zeros(3),bound), max_nfev=120)
        fitted = nodes_for(solution.x)
        for ni in chain: rotations[ni] = fitted[ni]['rotation']
        metrics.append({'finger': finger, 'tipTargetError': float(np.linalg.norm(endpoints(solution.x)[0]-target)),
                        'solverSuccess': bool(solution.success), 'maxComponentDegrees': float(np.rad2deg(abs(solution.x)).max())})
    times = np.unique(np.concatenate([c['times'] for c in clip['channels']]))
    poses = []
    for time in times:
        nodes = sample_nodes(body, clip, float(time))
        for ni, q in rotations.items(): nodes[ni]['rotation'] = q
        poses.append(nodes)
    args.output.mkdir(parents=True)
    fc.rt.SEMANTICS = fc.skin_semantics(body)
    candidate = args.output/'warrior-grip-candidate.glb'
    fc.rt.append_animation(body, reader, times-times[0], poses, clip['name']+'-grip-candidate',
                           {'source': str(args.body), 'sha256': fc.hp.sha(args.body.read_bytes()), 'fingerOnly': True}, candidate)
    result = fc.hp.Model(candidate, reader)
    body.load_materials(); weapon.load_materials()
    shift = max(body.materials, default=-1)+1
    merged = SimpleNamespace(materials={**body.materials, **{k+shift:v for k,v in weapon.materials.items()}})
    frames = []
    for index, phase in enumerate(preview_phases):
        world = result.pose(result.animations[0], result.animations[0]['end']*phase)
        weapon_transform = world[hand] @ socket
        geometry = result.geometry(world)
        for primitive in weapon.geometry(weapon.rest_world):
            item = dict(primitive)
            item['vertices'] = (np.column_stack([item['vertices'],np.ones(len(item['vertices']))]) @ weapon_transform.T)[:,:3]
            if item['world_normals'] is not None:
                normals = item['world_normals'] @ np.linalg.inv(weapon_transform[:3,:3])
                item['world_normals'] = normals/np.maximum(np.linalg.norm(normals,axis=1,keepdims=True),1e-12)
            item['material'] += shift; geometry.append(item)
        for label, yaw, ppm in (('full',0,300), ('grip',0,1800), ('grip-side',90,1800)):
            foot = np.zeros(3) if label=='full' else (world[hand] @ socket @ np.r_[record['sourceGrip'],1])[:3]
            pivot = [320,608] if label=='full' else [320,320]
            image, stats = fc.hp.raster(merged,geometry,yaw,(640,640),pivot,foot,ppm,30,1,False)
            image.save(args.output/f'{label}-{index}.png')
            frames.append({'phase':phase,'view':label,'stats':stats})
    # Independent check: all joints except the authored finger rotations agree.
    nonfinger_delta = 0; ground = 1e9
    for phase in np.linspace(0,1,129):
        time = clip['start']+phase*(clip['end']-clip['start'])
        before, after = body.pose(clip,float(time)), result.pose(result.animations[0],float(time-clip['start']))
        # Children of finger joints may follow; compare locals for all others.
        a,b = sample_nodes(body,clip,float(time)), sample_nodes(result,result.animations[0],float(time-clip['start']))
        for ni in range(len(a)):
            if ni not in rotations: nonfinger_delta=max(nonfinger_delta,float(abs(reader.node_matrix(a[ni])-reader.node_matrix(b[ni])).max()))
        ground=min(ground,float(min(g['vertices'][:,1].min() for g in result.geometry(after))))
    fc.hp.write_json(args.output/'receipt.json',{'bodySha256':fc.hp.sha(args.body.read_bytes()),'weaponSha256':record['weaponSha256'],
        'gripRecipeSha256':fc.hp.sha(args.grip_recipe.read_bytes()) if reused else None,
        'candidateSha256':fc.hp.sha(candidate.read_bytes()),'fingerJoints':sorted(rotations),'rotations':rotations,
        'fit':metrics,'maximumNonfingerLocalDelta':nonfinger_delta,'minimumY':ground,'frames':frames,
        'status':'candidate-needs-grip-anatomy-and-contact-review','masterChanged':False,'gameplaySelected':False})
    print(json.dumps({'fit':metrics,'maximumNonfingerLocalDelta':nonfinger_delta,'minimumY':ground}),flush=True)


if __name__=='__main__': main()
