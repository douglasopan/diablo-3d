"""Author a real additive breathing loop from an existing guarded rig pose.

Changes only selected upper-body rotations; never repeats a still as animation,
moves the root, changes bone lengths or emits simulation events. Candidate only.
"""
import argparse
import copy
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
import foot_contact as fc
from attack_settle import sample_nodes


def breathe(guard, names, phase):
    nodes=copy.deepcopy(guard)
    amount=(1-np.cos(2*np.pi*phase))*.5
    # Degrees about each bone's local axes. No nonuniform scaling or root motion.
    recipe={'Spine':[-.8,0,0], 'Spine1':[-.6,0,0], 'Spine2':[-.4,0,0],
            'Neck':[.5,0,0], 'Head':[.5,0,0]}
    for bone, vector in recipe.items():
        index=names['mixamorig:'+bone]
        base=Rotation.from_quat(guard[index]['rotation'])
        nodes[index]['rotation']=(base*Rotation.from_rotvec(np.deg2rad(vector)*amount)).as_quat().tolist()
    return nodes


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists():parser.error('Use a fresh candidate directory')
    reader,_=fc.hp.load_reader(fc.hp.ROOT)
    model=fc.hp.Model(args.source,reader)
    if len(model.animations)!=1:parser.error('Expected one source guard/attack clip')
    names={n.get('name'):i for i,n in enumerate(model.nodes)}
    guard=sample_nodes(model,model.animations[0],model.animations[0]['start'])
    times=np.linspace(0,2.5,49)
    poses=[breathe(guard,names,float(t/2.5)) for t in times]
    args.output.mkdir(parents=True)
    output=args.output/'warrior-guard-idle.glb'
    fc.rt.SEMANTICS=fc.skin_semantics(model)
    old,_=fc.rt.append_animation(model,reader,times,poses,'authored-guard-breathing',
        {'source':str(args.source),'sha256':fc.hp.sha(args.source.read_bytes()),
         'authoring':'upper-body additive breathing, fixed pelvis/legs/finger grip; 2.5s authoring timeline only',
         'distributionPermission':'source guard provenance still requires distribution review'},output)
    result=fc.hp.Model(output,reader);clip=result.animations[0]
    minimum=1e9;maximum_motion=0;first=None;nonupper=0
    affected={names['mixamorig:'+b] for b in ('Spine','Spine1','Spine2','Neck','Head')}
    for time in np.linspace(0,2.5,129):
        nodes=sample_nodes(result,clip,float(time))
        for i in range(len(nodes)):
            if i not in affected:
                nonupper=max(nonupper,float(np.abs(reader.node_matrix(nodes[i])-reader.node_matrix(guard[i])).max()))
        xyz=np.concatenate([p['vertices'] for p in result.geometry(result.pose(clip,float(time)))])
        minimum=min(minimum,float(xyz[:,1].min()))
        if first is None:first=xyz
        maximum_motion=max(maximum_motion,float(np.linalg.norm(xyz-first,axis=1).max()))
        last=xyz
    doc,binary,_=reader.read_glb(output)
    checks={'layoutPreserved':all(doc.get(k)==model.doc.get(k) for k in
        ('nodes','meshes','skins','materials','images','textures','samplers','scenes')),
        'sourceBinaryPrefixPreserved':bytes(binary[:len(old)])==bytes(old),
        'nonupperLocalDeltaBelow1e-6':nonupper<1e-6,'groundPass':minimum>=-.002,
        'realMotion':maximum_motion>.001,'loopSeamPass':float(np.linalg.norm(last-first,axis=1).max())<1e-6}
    receipt={'sourceSha256':fc.hp.sha(args.source.read_bytes()),'candidateSha256':fc.hp.sha(output.read_bytes()),
        'checks':checks,'minimumY':minimum,'maximumVertexMotion':maximum_motion,
        'nonupperLocalDelta':nonupper,'endpointVertexDelta':float(np.linalg.norm(last-first,axis=1).max()),
        'samples':129,'keys':49,'gameplaySelected':False,'nativeTimingConnected':False,
        'status':'candidate-needs-weapon-directional-and-gameplay-review' if all(checks.values()) else 'rejected'}
    fc.hp.write_json(args.output/'receipt.json',receipt)
    print(__import__('json').dumps(receipt),flush=True)


if __name__=='__main__':main()
