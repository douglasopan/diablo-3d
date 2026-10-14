"""Reactive left-arm shield posture candidate; shield mesh remains required.

Native block eligibility, item wear, audio and RNG are untouched. Native Warrior
block has two frames, so the first authored pose already braces for impact.
"""
import argparse
import copy
from pathlib import Path
import numpy as np
import foot_contact as fc
from attack_settle import sample_nodes
from guard_cast_candidate import gesture


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    if args.output.exists():p.error('Use a fresh candidate directory')
    reader,_=fc.hp.load_reader(fc.hp.ROOT);model=fc.hp.Model(args.source,reader)
    if len(model.animations)!=1:p.error('Expected one guarded source clip')
    names={n.get('name'):i for i,n in enumerate(model.nodes)}
    guard=sample_nodes(model,model.animations[0],model.animations[0]['start'])
    braced,fit=gesture(model,reader,guard,names,np.array([.13,.775,.185]))
    times=np.array([0,.55,1],dtype=float)*.2;poses=[braced,braced,guard]
    args.output.mkdir(parents=True);output=args.output/'warrior-block-posture.glb'
    fc.rt.SEMANTICS=fc.skin_semantics(model)
    old,_=fc.rt.append_animation(model,reader,times,poses,'authored-shield-block-posture',
        {'source':str(args.source),'sha256':fc.hp.sha(args.source.read_bytes()),
         'authoring':'first pose reactive brace; last returns guard; two-frame native block context',
         'shieldMeshPresent':False},output)
    result=fc.hp.Model(output,reader);clip=result.animations[0]
    allowed={names['mixamorig:'+b] for b in ('LeftArm','LeftForeArm','LeftHand')}
    minimum=1e9;unchanged=0;motion=0;first=None
    for time in np.linspace(0,.2,129):
        nodes=sample_nodes(result,clip,float(time))
        for i in range(len(nodes)):
            if i not in allowed:unchanged=max(unchanged,float(abs(reader.node_matrix(nodes[i])-reader.node_matrix(guard[i])).max()))
        xyz=np.concatenate([g['vertices'] for g in result.geometry(result.pose(clip,float(time)))])
        if first is None:first=xyz
        minimum=min(minimum,float(xyz[:,1].min()));motion=max(motion,float(np.linalg.norm(xyz-first,axis=1).max()));last=xyz
    reference=np.concatenate([g['vertices'] for g in model.geometry(fc.world(model,guard,reader))])
    recovery=float(np.linalg.norm(last-reference,axis=1).max())
    doc,binary,_=reader.read_glb(output)
    checks={'layoutPreserved':all(doc.get(k)==model.doc.get(k) for k in ('nodes','meshes','skins','materials','textures','images','samplers','scenes')),
        'binaryPrefixPreserved':bytes(binary[:len(old)])==bytes(old),'nonblockingLocalDeltaBelow1e-6':unchanged<1e-6,
        'groundPass':minimum>=-.002,'realMotion':motion>.02,'guardRecoveryPass':recovery<1e-6,'reachPass':fit['handTargetError']<.002}
    receipt={'sourceSha256':fc.hp.sha(args.source.read_bytes()),'candidateSha256':fc.hp.sha(output.read_bytes()),'checks':checks,
        'fit':fit,'minimumY':minimum,'nonblockingLocalDelta':unchanged,'maximumMotion':motion,'guardRecoveryDelta':recovery,
        'shieldMeshPresent':False,'fullEquipmentCoverage':False,'nativeTimingConnected':False,'gameplaySelected':False,
        'status':'posture-candidate-requires-shield-grip-and-anatomy-review' if all(checks.values()) else 'rejected'}
    fc.hp.write_json(args.output/'receipt.json',receipt);print(__import__('json').dumps(receipt),flush=True)


if __name__=='__main__':main()
