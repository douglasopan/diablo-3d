"""Offline, bounded rotational leg/sole contact correction. No root offset."""
import argparse
import copy
import json
import sys
from pathlib import Path
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation

REPO=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(REPO/'docs/own-art-pack/heroes-local-r1/pipeline'))
import hero_pipeline as hp
import retarget_local_probe as rt

def world(model,nodes,reader):
    return model.world([reader.node_matrix(n) for n in nodes])

def points(model,primitive,w,indices):
    joints,ibm=model.skins[primitive['skin']]
    palette=np.array([w[int(j)]@ibm[k] for k,j in enumerate(joints)])
    matrices=np.zeros((len(indices),4,4))
    for js,ws in primitive['pairs']:
        for slot in range(4):matrices+=palette[js[indices,slot]]*ws[indices,slot,None,None]
    positions=np.column_stack([primitive['positions'][indices],np.ones(len(indices))])
    return np.einsum('vij,vj->vi',matrices,positions)[:,:3]

def correct(model,nodes,reader):
    names={n.get('name'):i for i,n in enumerate(nodes)}
    start=copy.deepcopy(nodes);metrics=[]
    for side in ('Left','Right'):
        chain=[names['mixamorig:'+side+part] for part in ('UpLeg','Leg','Foot','ToeBase')]
        w=world(model,nodes,reader);groups=[]
        for primitive in model.primitives:
            if primitive['skin'] is None:continue
            joints,_=model.skins[primitive['skin']];weight=np.zeros(len(primitive['positions']))
            for js,ws in primitive['pairs']:
                weight+=(ws*np.isin(joints[js],chain[2:])).sum(axis=1)
            indices=np.flatnonzero(weight>.35)
            if len(indices):groups.append((primitive,indices,points(model,primitive,w,indices)))
        if not groups or min(g[2][:,1].min() for g in groups)>=0:continue
        original=copy.deepcopy(nodes);rotations=[Rotation.from_quat(nodes[i].get('rotation',[0,0,0,1])) for i in chain]
        def pose(values):
            result=copy.deepcopy(original)
            for k,i in enumerate(chain):result[i]['rotation']=(rotations[k]*Rotation.from_rotvec(values[k*3:k*3+3])).as_quat().tolist()
            return result
        def residual(values):
            trial=pose(values);wp=world(model,trial,reader);errors=[]
            for primitive,indices,reference in groups:
                xyz=points(model,primitive,wp,indices)
                # Inequality plane constraint, not translating the pelvis or
                # flattening vertices. Keep donor airborne phases untouched.
                errors.append(np.minimum(xyz[:,1]-.0002,0)*20)
                errors.append((xyz-reference).reshape(-1)*.15)
            errors.append(values*.015)
            return np.concatenate(errors)
        bound=np.deg2rad(12)
        solution=least_squares(residual,np.zeros(12),bounds=(-bound,bound),max_nfev=60,ftol=1e-8,xtol=1e-8,gtol=1e-8)
        nodes=pose(solution.x)
        metrics.append({'side':side,'maxCorrectionDegrees':float(np.rad2deg(np.abs(solution.x).max())),'evaluations':solution.nfev,'solverSuccess':bool(solution.success)})
    if any(nodes[i].get('translation')!=start[i].get('translation') or nodes[i].get('scale')!=start[i].get('scale') for i in range(len(nodes))):raise ValueError('Contact solver changed local lengths/translations')
    return nodes,metrics

def skin_semantics(model):
    joints={int(j) for palette,_ in model.skins for j in palette}
    names=[model.nodes[i]['name'] for i in sorted(joints)]
    if not all(name.startswith('mixamorig:') for name in names):
        raise ValueError('This probe requires the verified Mixamo named rig')
    return [(name,name.removeprefix('mixamorig:')) for name in names]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    if args.output.exists():p.error('Use fresh candidate directory')
    reader,_=hp.load_reader(hp.ROOT);m=hp.Model(args.input,reader)
    if len(m.animations)!=1:p.error('Supply a one-clip retarget candidate')
    clip=m.animations[0];times=np.unique(np.concatenate([c['times'] for c in clip['channels']]))
    rt.SEMANTICS=skin_semantics(m)
    args.output.mkdir(parents=True)
    poses=[];changes=[]
    for key_index,time in enumerate(times):
        nodes=copy.deepcopy(m.nodes)
        for c in clip['channels']:
            value=hp.interpolate(c['times'],c['values'],c['method'],float(time),c['path']=='rotation')
            nodes[c['node']][c['path']]=value.tolist()
        fixed,metrics=correct(m,nodes,reader);poses.append(fixed);changes.append({'time':float(time),'correction':metrics})
        (args.output/'pose-checkpoint.json').write_text(json.dumps({'sourceSha256':hp.sha(args.input.read_bytes()),'completedKeys':key_index+1,'totalKeys':len(times),'times':times[:key_index+1].tolist(),'poses':poses,'changes':changes},separators=(',',':'))+'\n',encoding='utf-8')
        print(json.dumps({'key':key_index+1,'total':len(times)}),flush=True)
    output=args.output/'warrior-contact-candidate.glb'
    rt.append_animation(m,reader,times,poses,'contact-candidate',{'path':str(args.input),'sha256':hp.sha(args.input.read_bytes()),'correction':'bounded rotations of thighs/knees/feet/toes; no global offset'},output)
    reopened=hp.Model(output,reader);samples=[]
    for t in np.unique(np.r_[times,np.linspace(times[0],times[-1],257)]):
        geometry=reopened.geometry(reopened.pose(reopened.animations[0],float(t)))
        xyz=np.concatenate([g['vertices'] for g in geometry]);samples.append({'time':float(t),'minY':float(xyz[:,1].min())})
    minimum=min(s['minY'] for s in samples)
    receipt={'schemaVersion':1,'sourceSha256':hp.sha(args.input.read_bytes()),'candidateSha256':hp.sha(output.read_bytes()),'status':'contact-candidate-awaits-independent-validation' if minimum>=-.002 else 'diagnostic-reject','minimumY':minimum,'samples':samples,'changes':changes,'gameplayEligible':False,'rootMoved':False,'localLengthsChanged':False,'meshOrTextureChanged':False,'maxRotationDegrees':12,'limitation':'inequality contact only; foot locking, continuity, knee anatomy and native timing remain unqualified'}
    (args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':receipt['status'],'minimumY':minimum,'output':str(output)}))

if __name__=='__main__':main()
