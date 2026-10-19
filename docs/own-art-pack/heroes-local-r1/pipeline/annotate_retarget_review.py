#!/usr/bin/env python3
"""Attach independent dense QA to diagnostic receipt; preserve original bytes."""
import argparse
import json
from pathlib import Path

import numpy as np

import hero_pipeline as hp


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--render-receipt",type=Path,required=True)
    parser.add_argument("--independent-review",type=Path,required=True)
    args=parser.parse_args()
    old=args.render_receipt.read_bytes()
    receipt=json.loads(old)
    review=json.loads(args.independent_review.read_text(encoding="utf-8"))
    review_sha=hp.sha(args.independent_review.read_bytes())
    if "independentFullClipQA" in receipt:
        if receipt["independentFullClipQA"]["reviewSha256"]!=review_sha:
            raise ValueError("Independent review changed; use a new annotation revision")
        print(json.dumps({"status":"already annotated","reviewSha256":review_sha}))
        return
    candidate=Path(receipt["candidate"]["path"])
    if hp.sha(candidate.read_bytes())!=receipt["candidate"]["sha256"] or not any(
            item["sha256"]==receipt["candidate"]["sha256"] for item in review["inputs"]):
        raise ValueError("Independent review does not match this exact candidate")
    preserved=args.render_receipt.with_name("render-receipt-cpu.json")
    if preserved.exists() and preserved.read_bytes()!=old:
        raise ValueError("Immutable CPU receipt differs; refusing overwrite")
    if not preserved.exists():
        preserved.write_bytes(old)
    ground=review["ground"]
    worst=ground["lowest"]
    reader,_=hp.load_reader(hp.ROOT)
    model=hp.Model(candidate,reader)
    animation=next(a for a in model.animations if a["name"]==receipt["candidate"]["clip"])
    keys=np.unique(np.concatenate([c["times"] for c in animation["channels"]]))
    lo=max(0,int(np.searchsorted(keys,worst["targetTimeSeconds"],side="right")-1))
    receipt["worstOf8RenderedPhases"]=receipt.pop("worstSample")
    receipt["independentFullClipQA"]={"reviewPath":str(args.independent_review.resolve()),"reviewSha256":review_sha,
             "status":review["status"],"denseSampleCount":ground["denseSampleCount"],"denseMinimumY":ground["denseMinimumY"],
             "worst":{**worst,"sourceKeyIndices":[lo,min(len(keys)-1,lo+1)],
                     "sourceOriginalTimeSeconds":worst["targetTimeSeconds"]+receipt["timing"]["outputTimeShiftSeconds"]},
             "units":ground["units"],"structuralChecksPassed":review["structuralChecksPassed"],
             "renderScope":"Principal explicitly authorized64 diagnostic frames after rejection; no native-compatible export/selection"}
    receipt["unannotatedCpuReceipt"]={"path":str(preserved.resolve()),"sha256":hp.sha(old)}
    receipt["annotationTool"]={"path":str(Path(__file__).resolve()),"sha256":hp.sha(Path(__file__).read_bytes())}
    receipt["status"]="diagnostic-reject"
    receipt["gameplayEligible"]=False
    hp.write_json(args.render_receipt,receipt)
    print(json.dumps({"output":str(args.render_receipt),"denseMinimumY":ground["denseMinimumY"],"sourceKeyIndices":[lo,min(len(keys)-1,lo+1)],"status":receipt["status"]}))


if __name__=="__main__":
    main()
