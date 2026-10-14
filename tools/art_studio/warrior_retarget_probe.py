"""Bounded donor-to-existing-Warrior probe; never installs or selects assets."""
import argparse
import json
import sys
from pathlib import Path
import numpy as np

REPO=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(REPO/'docs/own-art-pack/heroes-local-r1/pipeline'))
import hero_pipeline as hp
import retarget_local_probe as rt

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--target',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--clip-index',type=int,required=True)
    args=p.parse_args()
    if args.output.exists():p.error('Preserve previous output; use a fresh candidate path')
    reader,_=hp.load_reader(hp.ROOT);source=hp.Model(args.source,reader);target=hp.Model(args.target,reader)
    sn,tn=rt.joint_names(source),rt.joint_names(target)
    if not 0<=args.clip_index<len(source.animations):p.error('Clip index out of bounds')
    if set(tn)-set(sn):p.error('Every target skin joint must have an exact named donor; use an explicit semantic map otherwise')
    sb,tb=rt.bind_world(source),rt.bind_world(target)
    mapping={tn[name]:sn[name] for name in tn}
    # Reuse validated bind-delta implementation, including all matching finger joints.
    rt.SEMANTICS=[(name,name.removeprefix('mixamorig:')) for name in tn]
    clip=source.animations[args.clip_index]
    ratio=rt.leg_length(tb,tn,'mixamorig:')/rt.leg_length(sb,sn,'mixamorig:')
    times=np.unique(np.concatenate([c['times'] for c in clip['channels']]))
    if len(times)>4096:p.error('Clip budget exceeded')
    poses=[rt.pose(target,source,clip,float(t),mapping,sb,tb,ratio,reader)[0] for t in times]
    source_record={'path':str(source.path),'sha256':hp.sha(source.path.read_bytes()),'clip':clip['name'],'distributionPermission':'not-established-by-this-probe'}
    args.output.mkdir(parents=True);output=args.output/'warrior-candidate.glb'
    old,_=rt.append_animation(target,reader,times-times[0],poses,'donor-probe-'+str(args.clip_index),source_record,output)
    result=hp.Model(output,reader);doc,binary,_=reader.read_glb(output)
    samples=[]
    # Diagnostic sampling includes original key times and regular phases; no fake ground correction.
    for t in np.unique(np.r_[times,np.linspace(clip['start'],clip['end'],33)]):
        points=np.concatenate([g['vertices'] for g in result.geometry(result.pose(result.animations[0],float(t-times[0])))])
        samples.append({'time':float(t-times[0]),'minY':float(points[:,1].min()),'maxY':float(points[:,1].max()),'poseSha256':hp.sha(points.astype('<f8').tobytes())})
    minimum=min(s['minY'] for s in samples)
    checks={'originalBinaryPreserved':binary[:len(old)]==old,'meshMaterialsSkinAndNodesPreserved':all(doc.get(k)==target.doc.get(k) for k in ('nodes','meshes','skins','materials','images','textures','samplers','scenes','scene')),'all52TargetJointsMapped':len(mapping)==len(tn),'actualMotion':len({s['poseSha256'] for s in samples})>1,'groundTolerance':minimum>=-.002}
    receipt={'schemaVersion':1,'source':source_record,'targetSha256':hp.sha(target.path.read_bytes()),'outputSha256':hp.sha(output.read_bytes()),'checks':checks,'samples':samples,'minimumY':minimum,'jointMapping':list(tn),'status':'technical-candidate-needs-visual-and-native-timing-review' if all(checks.values()) else 'diagnostic-reject','gameplayEligible':False,'spritesRemainActive':True,'rootMotion':'explicit in-place derivation: donor planar hips removed; vertical scaled by leg length; no simulation displacement','timing':'donor key seconds preserved; native action-frame mapping pending','masterChanged':False}
    (args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'output':str(output),'status':receipt['status'],'minY':minimum,'checks':checks}))

if __name__=='__main__':main()
