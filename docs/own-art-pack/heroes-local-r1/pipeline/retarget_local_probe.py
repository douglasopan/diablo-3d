#!/usr/bin/env python3
"""Candidate-only Griswold real walk -> Warrior bind rig, offline CPU.

World bind delta retarget. Preserves target shape, IBMs, local bone translations
and scales; pelvis Y alone receives measured source displacement. Explicit
in-place derivation removes source pelvis XZ translation, never game movement.
"""
import argparse
import copy
from datetime import datetime, timezone
import json
from pathlib import Path
import struct

import numpy as np

import hero_pipeline as hp


SEMANTICS = [("Hips","Hips"),("Spine02","Spine"),("Spine01","Spine1"),("Spine","Spine2"),
             ("neck","Neck"),("Head","Head")]+[(side+part,side+part) for side in ("Left","Right")
             for part in ("Shoulder","Arm","ForeArm","Hand","UpLeg","Leg","Foot","ToeBase")]


def rotation(matrix):
    u,_,v=np.linalg.svd(matrix[:3,:3])
    result=u @ v
    if np.linalg.det(result)<0:
        raise ValueError("Reflection in rig; explicit correction required")
    return result


def quaternion(matrix):
    """Rotation matrix -> normalized XYZW; no external library dependency."""
    r=matrix
    trace=float(np.trace(r))
    if trace>0:
        s=np.sqrt(trace+1)*2
        q=np.array([(r[2,1]-r[1,2])/s,(r[0,2]-r[2,0])/s,(r[1,0]-r[0,1])/s,s/4])
    else:
        i=int(np.argmax(np.diag(r)))
        if i==0:
            s=np.sqrt(1+r[0,0]-r[1,1]-r[2,2])*2
            q=np.array([s/4,(r[0,1]+r[1,0])/s,(r[0,2]+r[2,0])/s,(r[2,1]-r[1,2])/s])
        elif i==1:
            s=np.sqrt(1+r[1,1]-r[0,0]-r[2,2])*2
            q=np.array([(r[0,1]+r[1,0])/s,s/4,(r[1,2]+r[2,1])/s,(r[0,2]-r[2,0])/s])
        else:
            s=np.sqrt(1+r[2,2]-r[0,0]-r[1,1])*2
            q=np.array([(r[0,2]+r[2,0])/s,(r[1,2]+r[2,1])/s,s/4,(r[1,0]-r[0,1])/s])
    q/=np.linalg.norm(q)
    return q if q[3]>=0 else -q


def joint_names(model):
    return {model.nodes[int(j)]["name"]:int(j) for skin in model.skins for j in skin[0]}


def bind_world(model):
    result={int(j):np.linalg.inv(ibms[i]) for joints,ibms in model.skins for i,j in enumerate(joints)}
    for node,matrix in result.items():
        # Existing sources have default pose = bind within finite float error.
        # Refuse a file where these different references were silently conflated.
        if np.max(np.abs(matrix-model.rest_world[node]))>1e-4:
            raise ValueError("Source/target default not verified bind; need explicit bind-rest reconstruction")
    return result


def leg_length(bind,names,prefix=""):
    lengths=[]
    for side in ("Left","Right"):
        chain=[bind[names[prefix+side+part]][:3,3] for part in ("UpLeg","Leg","Foot")]
        lengths.append(sum(np.linalg.norm(b-a) for a,b in zip(chain,chain[1:])))
    return float(np.mean(lengths))


def pose(target,source,clip,time,mapping,sbind,tbind,ratio,reader):
    sworld=source.pose(clip,time)
    hip_source=mapping[next(i for i,n in enumerate(target.nodes) if n.get("name")=="mixamorig:Hips")]
    hip_delta=sworld[hip_source][:3,3]-sbind[hip_source][:3,3]
    nodes,world=[],{}
    for n in target.nodes:
        nodes.append(dict(n))
    desired={t:rotation(sworld[s]) @ rotation(sbind[s]).T @ rotation(tbind[t]) for t,s in mapping.items()}
    def visit(i):
        if i in world:
            return world[i]
        parent=target.parents[i]
        pw=np.eye(4) if parent is None else visit(parent)
        if i in mapping:
            nodes[i]["rotation"]=quaternion(rotation(pw).T @ desired[i]).tolist()
            if nodes[i].get("name")=="mixamorig:Hips":
                # Only vertical source pelvis motion; planar XZ is explicitly
                # removed once in authored candidate, not applied to gameplay.
                vertical_world=np.array([0,hip_delta[1]*ratio,0])
                nodes[i]["translation"]=(np.asarray(target.nodes[i].get("translation",[0,0,0]))+
                                          np.linalg.solve(pw[:3,:3],vertical_world)).tolist()
        world[i]=pw @ reader.node_matrix(nodes[i])
        return world[i]
    for i in range(len(nodes)):
        visit(i)
    return nodes,world,hip_delta


def append_animation(target,reader,times,poses,clip_name,source_record,output):
    document,binary,_=reader.read_glb(target.path)
    document=copy.deepcopy(document)
    binary=bytearray(binary)
    original_binary=bytes(binary)
    views=document.setdefault("bufferViews",[])
    accessors=document.setdefault("accessors",[])
    def add(array,kind):
        array=np.asarray(array,dtype="<f4")
        while len(binary)%4:
            binary.append(0)
        views.append({"buffer":0,"byteOffset":len(binary),"byteLength":array.nbytes})
        binary.extend(array.tobytes())
        record={"bufferView":len(views)-1,"componentType":5126,"count":len(array),"type":kind}
        if kind=="SCALAR":
            record.update({"min":[float(array.min())],"max":[float(array.max())]})
        accessors.append(record)
        return len(accessors)-1
    input_=add(times,"SCALAR")
    animation={"name":clip_name,"samplers":[],"channels":[],"extras":{"ownSourceAnimation":source_record,
                "retargetStatus":"diagnostic candidate only; not correct/native qualified","inPlace":"source pelvis XZ discarded; vertical scaled by leg length"}}
    # Track every semantic joint including ones whose first frame happens to be bind.
    mapped=[i for i,n in enumerate(target.nodes) if n.get("name") in {"mixamorig:"+t for _,t in SEMANTICS}]
    for ni in mapped:
        values=np.asarray([p[ni]["rotation"] for p in poses])
        for i in range(1,len(values)):
            if values[i] @ values[i-1]<0:
                values[i]*=-1
        ai=add(values,"VEC4")
        animation["samplers"].append({"input":input_,"output":ai,"interpolation":"LINEAR"})
        animation["channels"].append({"sampler":len(animation["samplers"])-1,"target":{"node":ni,"path":"rotation"}})
        if target.nodes[ni].get("name")=="mixamorig:Hips":
            ai=add([p[ni]["translation"] for p in poses],"VEC3")
            animation["samplers"].append({"input":input_,"output":ai,"interpolation":"LINEAR"})
            animation["channels"].append({"sampler":len(animation["samplers"])-1,"target":{"node":ni,"path":"translation"}})
    document["animations"]=[animation]
    document["buffers"][0]["byteLength"]=len(binary)
    encoded=json.dumps(document,separators=(",",":"),ensure_ascii=True,allow_nan=False).encode()
    encoded+=b" "*((-len(encoded))%4)
    binary+=b"\0"*((-len(binary))%4)
    output.write_bytes(struct.pack("<4sII",b"glTF",2,28+len(encoded)+len(binary))+
                       struct.pack("<II",len(encoded),0x4e4f534a)+encoded+
                       struct.pack("<II",len(binary),0x004e4942)+binary)
    return original_binary,animation


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--target",type=Path,required=True)
    parser.add_argument("--source-clip",required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--name",default="own-retarget-griswold-walk-proof-r1")
    args=parser.parse_args()
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("Use a fresh proof revision")
    reader,reader_record=hp.load_reader(hp.ROOT)
    source,target=hp.Model(args.source,reader),hp.Model(args.target,reader)
    snames,tnames=joint_names(source),joint_names(target)
    mapping={tnames["mixamorig:"+t]:snames[s] for s,t in SEMANTICS}
    sbind,tbind=bind_world(source),bind_world(target)
    source_clip=next(a for a in source.animations if a["name"]==args.source_clip)
    source_length,target_length=leg_length(sbind,snames),leg_length(tbind,tnames,"mixamorig:")
    ratio=target_length/source_length
    # Every original source key time, no artificial sine waves/authored motion.
    times=np.unique(np.concatenate([c["times"] for c in source_clip["channels"]]))
    authored=[pose(target,source,source_clip,float(t),mapping,sbind,tbind,ratio,reader) for t in times]
    args.output.mkdir(parents=True,exist_ok=True)
    output=args.output/"warrior-r2-firstwalk1.glb"
    source_record={"path":str(source.path),"sha256":hp.sha(source.path.read_bytes()),"clip":args.source_clip}
    old_binary,animation=append_animation(target,reader,times-times[0],[a[0] for a in authored],args.name,source_record,output)
    reopened=hp.Model(output,reader)
    newdoc,newbin,_=reader.read_glb(output)
    metrics=[]
    probe_times=np.linspace(source_clip["start"],source_clip["end"],8,endpoint=False)
    hip_t=tnames["mixamorig:Hips"]
    for time in probe_times:
        _,tw,delta=pose(target,source,source_clip,float(time),mapping,sbind,tbind,ratio,reader)
        geometry=target.geometry(tw)
        points=np.concatenate([p["vertices"] for p in geometry])
        reopened_points=np.concatenate([p["vertices"] for p in reopened.geometry(reopened.pose(reopened.animations[0],float(time-times[0])))])
        metrics.append({"sourceTimeSeconds":float(time),"targetTimeSeconds":float(time-times[0]),"worldBounds":hp.bbox(points),
                        "sourcePelvisDeltaWorld":delta.tolist(),"targetPelvisDeltaWorld":(tw[hip_t][:3,3]-target.rest_world[hip_t][:3,3]).tolist(),
                        "meshPoseSha256":hp.sha(points.astype("<f8").tobytes()),"reopenedMeshPoseSha256":hp.sha(reopened_points.astype("<f8").tobytes()),
                        "reopenedVertexMaxDelta":float(np.max(np.abs(points-reopened_points)))})
    # Render gate: penetrating feet/body or non-finite/unchanging/reopen faults are
    # diagnostic rejection, not fixed by translating output pixels/ground anchor.
    floor_min=min(m["worldBounds"]["min"][1] for m in metrics)
    unique=len(set(m["meshPoseSha256"] for m in metrics))
    checks={"shapeMaterialsSkinsIbmNodesPreserved":all(newdoc.get(k)==target.doc.get(k) for k in
              ("nodes","meshes","skins","materials","images","textures","samplers","scene","scenes")),
            "oldBinaryPrefixPreserved":newbin[:len(old_binary)]==old_binary,
            "allSemanticLocalTranslationsPreservedExceptPelvisY":all(np.allclose(p[ni].get("translation",[0,0,0]),target.nodes[ni].get("translation",[0,0,0]),atol=1e-10)
                  for p,_,_ in authored for ni in mapping if ni!=hip_t),
            "allLocalScalesPreserved":all(p[ni].get("scale")==target.nodes[ni].get("scale") for p,_,_ in authored for ni in range(len(target.nodes))),
            "eightActualPosesVary":unique==8,"reopenedVertexTolerance1e-4":max(m["reopenedVertexMaxDelta"] for m in metrics)<=1e-4,
            "groundPenetrationTolerance2mm":floor_min>=-.002,
            "noPlanarPelvisDisplacement":max(np.linalg.norm(np.array(m["targetPelvisDeltaWorld"])[[0,2]]) for m in metrics)<=1e-8}
    checks={k:bool(v) for k,v in checks.items()}
    structural=all(v for k,v in checks.items() if k!="groundPenetrationTolerance2mm")
    report={"schemaVersion":1,"createdAtUtc":datetime.now(timezone.utc).isoformat(),"source":source_record,
            "targetSource":{"path":str(target.path),"sha256":hp.sha(target.path.read_bytes())},
            "candidate":{"path":str(output.resolve()),"sha256":hp.sha(output.read_bytes()),"bytes":output.stat().st_size,"clip":args.name},
            "script":{"path":str(Path(__file__).resolve()),"sha256":hp.sha(Path(__file__).read_bytes())},"reader":reader_record,
            "method":"World rotation delta sourceCurrentR * inverse(sourceBindR) applied to targetBindR; solve local rotations under actual target parents; source scale excluded via proper polar rotation",
            "semanticMap":[{"source":s,"sourceNode":snames[s],"target":"mixamorig:"+t,"targetNode":tnames["mixamorig:"+t]} for s,t in SEMANTICS],
            "bindVerified":"both source/target world defaults compared with inverse IBMs, max abs tolerance1e-4",
            "proportion":{"sourceMeanLegLength":source_length,"targetMeanLegLength":target_length,"pelvisYScaleRatio":ratio,"meshOrBoneScaleApplied":False},
            "inPlace":"Source pelvis XZ delta discarded once; target pelvisY = bindY + measured source deltaY * leg length ratio; native movement never applied here",
            "timing":{"sourceRangeSeconds":[source_clip["start"],source_clip["end"]],"sourceDurationSeconds":source_clip["end"]-source_clip["start"],
                       "sourceKeyTimesPreserved":len(times),"outputTimeShiftSeconds":float(times[0]),"proofSamples":8,"proofEndpointIncluded":False,
                       "nativeWalkSlots":8,"nativeTimingResampleStatus":"NOT synchronized to native 8-slot timings; source-phase proof only"},
            "checks":checks,"structuralProofPassed":structural,"groundMinimumY":floor_min,"samples":metrics,
            "status":"technical-retarget-proof-pending-render" if all(checks.values()) else "diagnostic-reject",
            "gameplayEligible":False,"gameplayGate":"sprites only until correct rig/all animations validated",
            "limitations":["source Griswold walk/run previously measured feet ~0.10m below ground; this proof does not fix foot locking",
                           "30 target finger joints keep their original bind rotations; no source fingers",
                           "13 unweighted non-skin terminals have inferred identity transforms in target source; no terminal lengths recovered",
                           "no IK, foot locking, anatomical/weapon approval, native slot timing or loop acceptance",
                           "source .01 ancestor scale and target ~1 basis handled by world bind rotations/leg ratio, not copied scales"]}
    hp.write_json(args.output/"retarget-receipt.json",report)
    print(json.dumps({"output":str(output),"status":report["status"],"structuralProofPassed":structural,"groundMinimumY":floor_min,"checks":checks}))


if __name__=="__main__":
    main()
