"""Author left-hand cast gestures on the guarded Warrior, visual only."""
import argparse
import copy
from pathlib import Path
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation
import foot_contact as fc
from attack_settle import sample_nodes


def gesture(model, reader, guard, names, target):
    arm=names['mixamorig:LeftArm'];fore=names['mixamorig:LeftForeArm'];hand=names['mixamorig:LeftHand']
    initial=fc.world(model,guard,reader)
    upper=initial[fore][:3,3]-initial[arm][:3,3]
    lower=initial[hand][:3,3]-initial[fore][:3,3]
    axis=np.cross(upper,lower);axis/=np.linalg.norm(axis)
    elbow_axis=np.linalg.inv(initial[fore][:3,:3])@axis;elbow_axis/=np.linalg.norm(elbow_axis)
    base_arm=Rotation.from_quat(guard[arm]['rotation']);base_fore=Rotation.from_quat(guard[fore]['rotation'])
    def nodes_for(values):
        nodes=copy.deepcopy(guard)
        nodes[arm]['rotation']=(base_arm*Rotation.from_rotvec(values[:3])).as_quat().tolist()
        nodes[fore]['rotation']=(base_fore*Rotation.from_rotvec(elbow_axis*values[3])).as_quat().tolist()
        return nodes
    def residual(values):
        nodes=nodes_for(values);world=fc.world(model,nodes,reader)
        return np.r_[(world[hand][:3,3]-target)*100,values*.02]
    solution=least_squares(residual,np.zeros(4),bounds=(np.deg2rad([-85,-85,-85,-65]),np.deg2rad([85,85,85,65])),max_nfev=100)
    result=nodes_for(solution.x);world=fc.world(model,result,reader)
    # Actual palm basis inferred from the rig, not assumed from a bone name.
    index=initial[names['mixamorig:LeftHandIndex1']][:3,3]
    pinky=initial[names['mixamorig:LeftHandPinky1']][:3,3]
    middle=initial[names['mixamorig:LeftHandMiddle1']][:3,3]
    finger=middle-initial[hand][:3,3];finger/=np.linalg.norm(finger)
    normal=np.cross(index-pinky,finger);normal/=np.linalg.norm(normal)
    local_basis=np.linalg.inv(initial[hand][:3,:3])@np.array([normal,finger]).T
    local_basis/=np.linalg.norm(local_basis,axis=0)
    desired,_=Rotation.align_vectors(np.array([[0,0,1],[0,1,0]]),local_basis.T)
    parent=world[fore][:3,:3];parent/=np.linalg.norm(parent,axis=0)
    result[hand]['rotation']=(Rotation.from_matrix(parent).inv()*desired).as_quat().tolist()
    error=float(np.linalg.norm(world[hand][:3,3]-target))
    return result,{'handTargetError':error,'solverSuccess':bool(solution.success),'jointOffsetsDegrees':np.rad2deg(solution.x).tolist()}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path);p.add_argument('--kind',choices=('fire','lightning','magic'),required=True)
    p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    if args.output.exists():p.error('Use a fresh candidate directory')
    reader,_=fc.hp.load_reader(fc.hp.ROOT);model=fc.hp.Model(args.source,reader)
    if len(model.animations)!=1:p.error('Expected one source guard clip')
    names={n.get('name'):i for i,n in enumerate(model.nodes)}
    guard=sample_nodes(model,model.animations[0],model.animations[0]['start'])
    targets={'fire':([.15,.735,.13],[.145,.74,.235]),'lightning':([.15,.77,.12],[.155,.785,.225]),'magic':([.15,.70,.13],[.14,.73,.225])}
    windup,windup_fit=gesture(model,reader,guard,names,np.array(targets[args.kind][0]))
    release,release_fit=gesture(model,reader,guard,names,np.array(targets[args.kind][1]))
    emission=13/19
    phases=np.array([0,.2,.62,emission,.75,1]);poses=[guard,windup,release,release,release,guard]
    args.output.mkdir(parents=True);output=args.output/('warrior-cast-'+args.kind+'.glb')
    fc.rt.SEMANTICS=fc.skin_semantics(model)
    old,_=fc.rt.append_animation(model,reader,phases,poses,'authored-cast-'+args.kind,
        {'source':str(args.source),'sha256':fc.hp.sha(args.source.read_bytes()),'visualEmissionPhase':emission,
         'authoring':'left-hand windup, palm-forward gesture and guard recovery; no spell/damage event'},output)
    result=fc.hp.Model(output,reader);clip=result.animations[0]
    minimum=1e9;motion=0;first=None;unchanged_delta=0
    affected={names['mixamorig:'+k] for k in ('LeftArm','LeftForeArm','LeftHand')}
    for phase in np.unique(np.r_[np.linspace(0,1,129),phases]):
        nodes=sample_nodes(result,clip,float(phase))
        for i in range(len(nodes)):
            if i not in affected:unchanged_delta=max(unchanged_delta,float(abs(reader.node_matrix(nodes[i])-reader.node_matrix(guard[i])).max()))
        xyz=np.concatenate([g['vertices'] for g in result.geometry(result.pose(clip,float(phase)))])
        if first is None:first=xyz
        minimum=min(minimum,float(xyz[:,1].min()));motion=max(motion,float(np.linalg.norm(xyz-first,axis=1).max()));last=xyz
    doc,binary,_=reader.read_glb(output)
    checks={'layoutPreserved':all(doc.get(k)==model.doc.get(k) for k in ('nodes','meshes','skins','materials','textures','images','samplers','scenes')),
        'binaryPrefixPreserved':bytes(binary[:len(old)])==bytes(old),'noncastingLocalDeltaBelow1e-6':unchanged_delta<1e-6,
        'groundPass':minimum>=-.002,'realMotion':motion>.02,'recoveryPass':float(np.linalg.norm(last-first,axis=1).max())<1e-6,
        'reachPass':max(windup_fit['handTargetError'],release_fit['handTargetError'])<.002}
    receipt={'kind':args.kind,'sourceSha256':fc.hp.sha(args.source.read_bytes()),'candidateSha256':fc.hp.sha(output.read_bytes()),
        'visualEmissionPhase':emission,'checks':checks,'windup':windup_fit,'release':release_fit,
        'minimumY':minimum,'maximumMotion':motion,'noncastingLocalDelta':unchanged_delta,
        'status':'candidate-needs-anatomy-weapon-native-clock-review' if all(checks.values()) else 'rejected',
        'gameplaySelected':False,'nativeTimingConnected':False,'spellEmitted':False}
    fc.hp.write_json(args.output/'receipt.json',receipt);print(__import__('json').dumps(receipt),flush=True)


if __name__=='__main__':main()
