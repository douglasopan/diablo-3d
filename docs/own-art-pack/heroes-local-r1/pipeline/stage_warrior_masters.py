#!/usr/bin/env python3
"""Preserve existing own Warrior GLBs as immutable hash-named masters, offline."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--audit",type=Path,default=Path(__file__).with_name("warrior-local-glb-audit.json"))
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    audit=json.loads(args.audit.read_text(encoding="utf-8"))
    records=[]
    for model in audit["models"]:
        source=model["source"]
        path=Path(source["path"])
        if source["dependencies"] or digest(path)!=source["sha256"]:
            raise ValueError("Source changed or is not self-contained; restage requires a new audited revision")
        target=args.output/(path.stem+"-sha256-"+source["sha256"][:12]+".glb")
        if target.exists() and digest(target)!=source["sha256"]:
            raise ValueError("Immutable target has unexpected bytes; refusing overwrite")
        args.output.mkdir(parents=True,exist_ok=True)
        if not target.exists():
            shutil.copyfile(path,target)
        if digest(target)!=source["sha256"]:
            raise ValueError("Copy verification failed")
        records.append({"source":str(path),"preservedMaster":str(target.resolve()),"sha256":source["sha256"],
                        "bytes":target.stat().st_size,"counts":model["counts"],"bindWorldBounds":model["bindWorldBounds"],
                        "status":"candidate only; zero clips; not selected for gameplay"})
    receipt={"schemaVersion":1,"sourcesModified":False,"globalRegistryModified":False,"models":records,
             "humanRule":"Only sprites in gameplay until correct rig and complete animations are validated, including Warrior and Ogden; no candidate promotion here",
             "closeupMaster":"Keep original 4K GLBs alongside derived 2K preview; derivative is not replacement of the master"}
    output=args.output/"master-preservation-receipt.json"
    output.write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"output":str(output),"models":len(records)}))


if __name__=="__main__":
    main()
