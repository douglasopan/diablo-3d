"""Author the recovery of a corrected attack into its initial guard pose.

Preserves windup/strike through 75% of the clip. Pure visual candidate; no hit
events, native timing changes, simulation displacement or asset selection.
"""
import argparse
import copy
import json
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation, Slerp
import foot_contact as fc

def sample_nodes(model,clip,time):
    nodes=copy.deepcopy(model.nodes)
    for channel in clip['channels']:
        nodes[channel['node']][channel['path']]=fc.hp.interpolate(channel['times'],channel['values'],channel['method'],float(time),channel['path']=='rotation').tolist()
    return nodes

def settle(nodes,guard,weight):
    if not 0<=weight<=1:raise ValueError('Blend weight out of range')
    result=copy.deepcopy(nodes)
    for i,(pose,reference) in enumerate(zip(nodes,guard)):
        quats=np.array([pose.get('rotation',[0,0,0,1]),reference.get('rotation',[0,0,0,1])])
        result[i]['rotation']=Slerp([0,1],Rotation.from_quat(quats))([weight]).as_quat()[0].tolist()
        for path,default in (('translation',[0,0,0]),('scale',[1,1,1])):
            if path in pose or path in reference:
                result[i][path]=((1-weight)*np.array(pose.get(path,default))+weight*np.array(reference.get(path,default))).tolist()
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    if args.output.exists():p.error('Use a fresh candidate directory')
    reader,_=fc.hp.load_reader(fc.hp.ROOT);m=fc.hp.Model(args.source,reader)
    if len(m.animations)!=1:p.error('A single attack clip is required')
    clip=m.animations[0];start,end=clip['start'],clip['end'];duration=end-start
    guard=sample_nodes(m,clip,start);times=np.unique(np.r_[np.concatenate([c['times'] for c in clip['channels']]),start+duration*np.array([.75,.8,.85,.9,.95,1])])
    args.output.mkdir(parents=True);poses=[];changes=[]
    for index,time in enumerate(times):
        nodes=sample_nodes(m,clip,time);phase=(time-start)/duration;weight=0
        if phase>.75:
            u=(phase-.75)/.25;weight=u*u*(3-2*u)
            nodes=settle(nodes,guard,weight)
            if phase<1:nodes,corrections=fc.correct(m,nodes,reader)
            else:nodes=copy.deepcopy(guard);corrections=[]
        else:corrections=[]
        poses.append(nodes);changes.append({'time':float(time),'recoveryWeight':float(weight),'contactCorrection':corrections})
        (args.output/'checkpoint.json').write_text(json.dumps({'sourceSha256':fc.hp.sha(args.source.read_bytes()),'completed':index+1,'total':len(times),'poses':poses,'times':times[:index+1].tolist(),'changes':changes},separators=(',',':'))+'\n',encoding='utf-8')
        print(json.dumps({'key':index+1,'total':len(times)}),flush=True)
    fc.rt.SEMANTICS=fc.skin_semantics(m);output=args.output/'warrior-attack-settle.glb'
    fc.rt.append_animation(m,reader,times-times[0],poses,'attack-guard-recovery',{'path':str(args.source),'sha256':fc.hp.sha(args.source.read_bytes()),'authoredRecovery':'smoothstep final25%; initial75% preserved; final guard exact; contact corrected with bounded leg rotations'},output)
    result=fc.hp.Model(output,reader);animation=result.animations[0];minimum=1e9;first=None;maximum_preserved_delta=0
    for time in np.linspace(animation['start'],animation['end'],1025):
        xyz=np.concatenate([g['vertices'] for g in result.geometry(result.pose(animation,float(time)))])
        minimum=min(minimum,float(xyz[:,1].min()))
        if first is None:first=xyz
        last=xyz
        if time<=duration*.75:
            original=np.concatenate([g['vertices'] for g in m.geometry(m.pose(clip,float(time+start)))])
            maximum_preserved_delta=max(maximum_preserved_delta,float(np.abs(original-xyz).max()))
    report={'sourceSha256':fc.hp.sha(args.source.read_bytes()),'candidateSha256':fc.hp.sha(output.read_bytes()),'minimumY':minimum,'groundPass':minimum>=-.002,'startEndMaximumVertexDelta':float(np.linalg.norm(last-first,axis=1).max()),'first75PercentMaximumVertexDelta':maximum_preserved_delta,'preservedWindupAndStrike':maximum_preserved_delta<1e-5,'nativeTimingMapped':False,'gameplayEligible':False,'status':'recovery-candidate-needs-native-timing-art-weapon' if minimum>=-.002 and maximum_preserved_delta<1e-5 else 'diagnostic-reject','changes':changes}
    (args.output/'receipt.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:v for k,v in report.items() if k!='changes'}))

if __name__=='__main__':main()
