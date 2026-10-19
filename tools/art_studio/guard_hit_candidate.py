"""Author a visual hit reaction on the existing guarded Warrior rig.

Only upper-body rotations change. Native hit/damage/timing remain separate.
"""
import argparse
import copy
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
import foot_contact as fc
from attack_settle import sample_nodes


def recoil(guard, names, amount):
    if not 0<=amount<=1:raise ValueError('Recoil weight out of range')
    nodes=copy.deepcopy(guard)
    recipe={'Spine':[-5,0,-2], 'Spine1':[-4,0,-1], 'Spine2':[-3,0,0],
            'Neck':[3,0,1], 'Head':[4,0,1]}
    for name, angles in recipe.items():
        i=names['mixamorig:'+name]
        nodes[i]['rotation']=(Rotation.from_quat(guard[i]['rotation'])*
            Rotation.from_rotvec(np.deg2rad(angles)*amount)).as_quat().tolist()
    return nodes


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    if args.output.exists():p.error('Use a fresh candidate directory')
    reader,_=fc.hp.load_reader(fc.hp.ROOT);model=fc.hp.Model(args.source,reader)
    if len(model.animations)!=1:p.error('Expected one source guarded clip')
    guard=sample_nodes(model,model.animations[0],model.animations[0]['start'])
    names={n.get('name'):i for i,n in enumerate(model.nodes)}
    phases=np.array([0,.15,.30,.60,1]);weights=[0,1,.8,.35,0]
    poses=[recoil(guard,names,weight) for weight in weights]
    args.output.mkdir(parents=True);output=args.output/'warrior-guard-hit.glb'
    fc.rt.SEMANTICS=fc.skin_semantics(model)
    old,_=fc.rt.append_animation(model,reader,phases*.75,poses,'authored-guard-hit-recovery',
        {'source':str(args.source),'sha256':fc.hp.sha(args.source.read_bytes()),
         'authoring':'upper-body recoil and guard recovery; native damage/timing unchanged'},output)
    result=fc.hp.Model(output,reader);clip=result.animations[0]
    affected={names['mixamorig:'+b] for b in ('Spine','Spine1','Spine2','Neck','Head')}
    minimum=1e9;maximum_motion=0;nonupper=0;first=None
    for time in np.unique(np.r_[np.linspace(0,.75,129),phases*.75]):
        nodes=sample_nodes(result,clip,float(time))
        for i in range(len(nodes)):
            if i not in affected:nonupper=max(nonupper,float(abs(reader.node_matrix(nodes[i])-reader.node_matrix(guard[i])).max()))
        xyz=np.concatenate([g['vertices'] for g in result.geometry(result.pose(clip,float(time)))])
        minimum=min(minimum,float(xyz[:,1].min()))
        if first is None:first=xyz
        maximum_motion=max(maximum_motion,float(np.linalg.norm(xyz-first,axis=1).max()));last=xyz
    doc,binary,_=reader.read_glb(output)
    checks={'layoutPreserved':all(doc.get(k)==model.doc.get(k) for k in ('nodes','meshes','skins','materials','textures','images','samplers','scenes')),
        'sourceBinaryPrefixPreserved':bytes(binary[:len(old)])==bytes(old),'nonupperLocalDeltaBelow1e-6':nonupper<1e-6,
        'groundPass':minimum>=-.002,'realMotion':maximum_motion>.01,'guardRecoveryPass':float(np.linalg.norm(last-first,axis=1).max())<1e-6}
    receipt={'sourceSha256':fc.hp.sha(args.source.read_bytes()),'candidateSha256':fc.hp.sha(output.read_bytes()),
        'checks':checks,'minimumY':minimum,'maximumVertexMotion':maximum_motion,'nonupperLocalDelta':nonupper,
        'endpointVertexDelta':float(np.linalg.norm(last-first,axis=1).max()),'keys':len(poses),
        'nativeTimingConnected':False,'gameplaySelected':False,'status':'candidate-needs-weapon-anatomy-directions-and-gameplay-review' if all(checks.values()) else 'rejected'}
    fc.hp.write_json(args.output/'receipt.json',receipt);print(__import__('json').dumps(receipt),flush=True)


if __name__=='__main__':main()
