"""Author a guarded entry into the first walk cycle with bounded foot contact.

Entry clip is one-shot, followed by the original looping walk. This is offline
visual authoring, not a native movement/clock edit or a general runtime blender.
"""
import argparse
import json
from pathlib import Path
import numpy as np
import foot_contact as fc
from attack_settle import sample_nodes, settle
from merge_actor_clips import static_signature


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('guard',type=Path);p.add_argument('walk',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--refine-from',type=Path);args=p.parse_args()
    if args.output.exists():p.error('Use a fresh candidate directory')
    reader,_=fc.hp.load_reader(fc.hp.ROOT);guard_model=fc.hp.Model(args.guard,reader);walk_model=fc.hp.Model(args.walk,reader)
    if len(guard_model.animations)!=1 or len(walk_model.animations)!=1 or static_signature(guard_model)!=static_signature(walk_model):
        p.error('Entry requires one clip per identical resolved actor layout')
    guard=sample_nodes(guard_model,guard_model.animations[0],guard_model.animations[0]['start'])
    clip=walk_model.animations[0];duration=clip['end']-clip['start']
    times=np.unique(np.r_[np.concatenate([c['times'] for c in clip['channels']]),clip['start']+duration*np.linspace(0,.25,17)])
    refined=None;additional=[]
    if args.refine_from:
        refined=fc.hp.Model(args.refine_from,reader)
        if len(refined.animations)!=1 or static_signature(refined)!=static_signature(walk_model):
            p.error('Refinement source must have the same actor layout')
        prior=refined.animations[0]
        times=np.unique(np.concatenate([c['times'] for c in prior['channels']]))
        for phase in np.linspace(0,.25,257):
            time=prior['start']+phase*duration
            xyz=np.concatenate([g['vertices'] for g in refined.geometry(refined.pose(prior,float(time)))])
            if xyz[:,1].min()<-.0015:additional.append(float(time))
        times=np.unique(np.r_[times,additional])
        print(json.dumps({'additionalContactKeys':len(additional),'totalKeys':len(times)}),flush=True)
    args.output.mkdir(parents=True);poses=[];changes=[]
    for index,time in enumerate(times):
        target=sample_nodes(walk_model,clip,float(time));phase=(time-clip['start'])/duration
        weight=min(phase/.25,1);weight=weight*weight*(3-2*weight)
        if phase<.25:
            nodes=sample_nodes(refined,refined.animations[0],float(time)) if refined else settle(guard,target,weight)
        else:nodes=target
        correction=[]
        if phase<.25:nodes,correction=fc.correct(walk_model,nodes,reader)
        poses.append(nodes);changes.append({'phase':float(phase),'weight':float(weight),'footCorrections':correction})
        fc.hp.write_json(args.output/'checkpoint.json',{'guardSha256':fc.hp.sha(args.guard.read_bytes()),'walkSha256':fc.hp.sha(args.walk.read_bytes()),
            'completed':index+1,'total':len(times),'times':times[:index+1].tolist(),'poses':poses,'changes':changes})
        print(json.dumps({'key':index+1,'total':len(times)}),flush=True)
    fc.rt.SEMANTICS=fc.skin_semantics(walk_model);output=args.output/'warrior-walk-entry.glb'
    old,_=fc.rt.append_animation(walk_model,reader,times-times[0],poses,'guard-to-walk-entry-only',
        {'guardSource':str(args.guard),'guardSha256':fc.hp.sha(args.guard.read_bytes()),'walkSource':str(args.walk),
         'walkSha256':fc.hp.sha(args.walk.read_bytes()),'authoring':'smoothstep entry first quarter, bounded leg/sole rotations, original remaining walk'},output)
    result=fc.hp.Model(output,reader);animation=result.animations[0]
    minimum=1e9;preserved=0;start_error=0;end_error=0
    guard_xyz=np.concatenate([g['vertices'] for g in guard_model.geometry(guard_model.pose(guard_model.animations[0],guard_model.animations[0]['start']))])
    for phase in np.linspace(0,1,1025):
        time=animation['start']+phase*(animation['end']-animation['start'])
        xyz=np.concatenate([g['vertices'] for g in result.geometry(result.pose(animation,float(time)))])
        minimum=min(minimum,float(xyz[:,1].min()))
        if phase==0:start_error=float(np.linalg.norm(xyz-guard_xyz,axis=1).max())
        if phase>=.25:
            reference=np.concatenate([g['vertices'] for g in walk_model.geometry(walk_model.pose(clip,float(clip['start']+phase*duration)))])
            preserved=max(preserved,float(abs(xyz-reference).max()))
            if phase==1:end_error=float(np.linalg.norm(xyz-reference,axis=1).max())
    doc,binary,_=reader.read_glb(output)
    checks={'staticLayoutPreserved':static_signature(result)==static_signature(walk_model),'sourceBinaryPrefixPreserved':bytes(binary[:len(old)])==bytes(old),
        'groundPass':minimum>=-.002,'startsAtGuard':start_error<1e-6,'remainingWalkPreserved':preserved<1e-6,'endsAtWalk':end_error<1e-6}
    receipt={'guardSha256':fc.hp.sha(args.guard.read_bytes()),'walkSha256':fc.hp.sha(args.walk.read_bytes()),'candidateSha256':fc.hp.sha(output.read_bytes()),
        'checks':checks,'minimumY':minimum,'startGuardDelta':start_error,'endWalkDelta':end_error,'remainingWalkMaximumDelta':preserved,
        'samples':1025,'changes':changes,'refineSourceSha256':fc.hp.sha(args.refine_from.read_bytes()) if refined else None,
        'additionalContactKeys':len(additional),'oneShotEntryOnly':True,'followsWithOriginalLoopingWalk':True,'nativeTimingConnected':False,'gameplaySelected':False,
        'status':'entry-candidate-needs-contact-motion-and-native-transition-review' if all(checks.values()) else 'rejected'}
    fc.hp.write_json(args.output/'receipt.json',receipt);print(json.dumps({k:v for k,v in receipt.items() if k!='changes'}),flush=True)


if __name__=='__main__':main()
