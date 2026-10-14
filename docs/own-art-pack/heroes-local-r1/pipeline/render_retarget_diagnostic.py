#!/usr/bin/env python3
"""Render honest retarget motion while preserving its diagnostic rejection gate."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

import numpy as np
from PIL import Image,ImageDraw

import hero_pipeline as hp


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retarget-receipt",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--pixels-per-unit",type=float,default=360,help="Uniform scale across every frame/direction; fixed pivot/footpoint")
    args=parser.parse_args()
    retarget=json.loads(args.retarget_receipt.read_text(encoding="utf-8"))
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("Use a fresh output revision")
    candidate=Path(retarget["candidate"]["path"])
    if hp.sha(candidate.read_bytes())!=retarget["candidate"]["sha256"] or retarget["gameplayEligible"]:
        parser.error("Candidate identity/status mismatch")
    reader,reader_record=hp.load_reader(hp.ROOT)
    model=hp.Model(candidate,reader)
    animation=next(a for a in model.animations if a["name"]==retarget["candidate"]["clip"])
    times=np.linspace(animation["start"],animation["end"],8,endpoint=False)
    geometries=[model.geometry(model.pose(animation,float(t))) for t in times]
    hashes=[hp.sha(np.concatenate([p["vertices"] for p in g]).astype("<f8").tobytes()) for g in geometries]
    if len(set(hashes))!=8:
        parser.error("Eight sampled poses do not actually vary")
    model.load_materials()
    args.output.mkdir(parents=True,exist_ok=True)
    keytimes=np.unique(np.concatenate([c["times"] for c in animation["channels"]]))
    samples=[]
    for i,(t,g) in enumerate(zip(times,geometries)):
        points=np.concatenate([p["vertices"] for p in g])
        lo=max(0,int(np.searchsorted(keytimes,t,side="right")-1))
        hi=min(len(keytimes)-1,lo+1)
        samples.append({"sampleIndex":i,"targetTimeSeconds":float(t),
                        "sourceOriginalTimeSeconds":float(t+retarget["timing"]["outputTimeShiftSeconds"]),
                        "sourceKeyIndices":[lo,hi],"worldBounds":hp.bbox(points),"meshPoseSha256":hashes[i],
                        "minimumY":float(points[:,1].min()),"groundPenetrationSourceUnits":max(0,float(-points[:,1].min()))})
    report={"schemaVersion":1,"createdAtUtc":datetime.now(timezone.utc).isoformat(),"status":"diagnostic-reject",
            "deliverableKind":"rendered retarget motion diagnostics; not native animation validated","gameplayEligible":False,
            "candidate":retarget["candidate"],"retargetReceipt":{"path":str(args.retarget_receipt.resolve()),"sha256":hp.sha(args.retarget_receipt.read_bytes())},
            "source":retarget["source"],"tool":{"path":str(Path(__file__).resolve()),"sha256":hp.sha(Path(__file__).read_bytes())},
            "renderer":{"path":str(Path(hp.__file__).resolve()),"sha256":hp.sha(Path(hp.__file__).read_bytes())},"reader":reader_record,
            "projection":{"canvas":[512,512],"pixelPivot":[256,448],"footpointSourceWorld":[0,0,0],"pixelsPerSourceUnit":args.pixels_per_unit,
                          "elevationDegrees":30,"perFrameRecenter":False,"groundBias":0,"uniformScale":True},
            "timing":{**retarget["timing"],"renderSamplesPerDirection":8,"sourcePhaseSampling":"8 evenly spaced source-phase samples, endpoint excluded",
                      "nativeSlotsOrDelaysChanged":False,"timeWarpToNativeWalk":False},
            "samples":samples,"directions":[],"counts":{"renderedMotionDiagnosticFrames":64,"nativeAnimationValidated":0},
            "limitations":["retarget ground penetration persists; no lift, pixel shift or root correction hides it",
                           "source units are normalized GLB units, not calibrated native meters","diffuse CPU material approximation; no full PBR/shadows",
                           "source Griswold feet ~.10m below ground documented; no foot locking or native timing validation",
                           "sprites-only gameplay rule remains; correct rig/full clip coverage not qualified"]}
    worst=max(samples,key=lambda s:s["groundPenetrationSourceUnits"])
    report["worstSample"]={"sampleIndex":worst["sampleIndex"],"sourceKeyIndices":worst["sourceKeyIndices"],
                           "sourceOriginalTimeSeconds":worst["sourceOriginalTimeSeconds"],"minimumY":worst["minimumY"],
                           "groundPenetrationSourceUnits":worst["groundPenetrationSourceUnits"]}
    sheet=Image.new("RGBA",(8*192,8*212+36),(22,22,26,255))
    draw=ImageDraw.Draw(sheet)
    draw.text((10,8),"WARRIOR FIRST WALK / REAL RETARGET / DIAGNOSTIC REJECT / 0 NATIVE CLIPS VALIDATED",fill=(245,220,200,255))
    for native_index,(direction,yaw) in enumerate(hp.DIRECTIONS):
        directory=args.output/direction
        directory.mkdir()
        record={"direction":direction,"nativeIndex":native_index,"modelYawDegrees":yaw,"frames":[]}
        for sample_index,(time,geometry) in enumerate(zip(times,geometries)):
            picture,stats=hp.raster(model,geometry,yaw,[512,512],[256,448],np.zeros(3),args.pixels_per_unit,30,2,False)
            path=directory/f"frame-{sample_index:03}.png"
            picture.save(path)
            record["frames"].append({"frame":sample_index,"file":str(path.resolve()),"bytes":path.stat().st_size,
                                     "sha256":hp.sha(path.read_bytes()),"pixelPivot":[256,448],**samples[sample_index],**stats})
            sheet.alpha_composite(picture.resize((192,192),Image.Resampling.LANCZOS),(sample_index*192,36+native_index*212))
            draw.text((sample_index*192+6,36+native_index*212+192),f"{direction.upper()} / {sample_index} / Ymin {samples[sample_index]['minimumY']:.4f}",fill=(225,225,225,255))
        report["directions"].append(record)
        print(json.dumps({"direction":direction,"frames":8,"status":"diagnostic-reject"}),flush=True)
    sheet_path=args.output/"warrior-firstwalk1-diagnostic-64-contact.png"
    sheet.save(sheet_path)
    report["contactSheet"]={"path":str(sheet_path.resolve()),"sha256":hp.sha(sheet_path.read_bytes()),"kind":"review artifact; not frame"}
    report["validation"]={"allInsideCanvas":all(not f["touchesFrameBoundary"] for d in report["directions"] for f in d["frames"]),
                          "allNonempty":all(f["coveredPixels"]>0 for d in report["directions"] for f in d["frames"]),
                          "everyDirectionHasMotion":all(len(set(f["rgbaPixelsSha256"] for f in d["frames"]))==8 for d in report["directions"]),
                          "distinctMeshPoses":len(set(hashes)),"groundCheck":"FAIL preserved, diagnostic reject","visualReview":"pending"}
    hp.write_json(args.output/"render-receipt.json",report)
    print(json.dumps({"output":str(args.output),"status":report["status"],"frames":64,"worstSample":report["worstSample"],"validation":report["validation"]}))


if __name__=="__main__":
    main()
