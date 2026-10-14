#!/usr/bin/env python3
"""Hash only this CPU pipeline's explicit final/proof artifacts; no global edits."""
import json
from pathlib import Path

import hero_pipeline as hp


def main():
    pipeline=Path(__file__).resolve().parent
    workspace=hp.ROOT.parent
    own=workspace/"assets/own-art/heroes/local-r1"
    bind=own/"warrior/bind-directions"
    proof=own/"warrior/candidate-firstwalk1"
    diagnostics=proof/"diagnostic-directions"
    masters=json.loads((own/"masters/master-preservation-receipt.json").read_text(encoding="utf-8"))
    bind_receipt=json.loads((bind/"render-receipt.json").read_text(encoding="utf-8"))
    retarget=json.loads((proof/"retarget-receipt.json").read_text(encoding="utf-8"))
    motion=json.loads((diagnostics/"render-receipt.json").read_text(encoding="utf-8"))
    checks=json.loads((pipeline/"pipeline-validation.json").read_text(encoding="utf-8"))
    files=[]
    def add(path,kind,status):
        path=path.resolve()
        files.append({"path":str(path),"kind":kind,"status":status,"bytes":path.stat().st_size,"sha256":hp.sha(path.read_bytes())})
    for name in ("hero_pipeline.py","stage_warrior_masters.py","verify_delivery.py","retarget_local_probe.py",
                 "render_retarget_diagnostic.py","annotate_retarget_review.py","test_hero_pipeline.py","assemble_pipeline_handoff.py","PIPELINE.md",
                 "warrior-local-glb-audit.json","pipeline-validation.json"):
        add(pipeline/name,"pipeline" if name.endswith(".py") else "evidence","technical-candidate")
    add(own/"masters/master-preservation-receipt.json","master-preservation-receipt","sources-preserved")
    for master in masters["models"]:
        add(Path(master["preservedMaster"]),"preserved-own-glb-master","source-master; no clip or gameplay qualification")
    for direction in bind_receipt["directions"]:
        for frame in direction["frames"]:
            add(Path(frame["file"]),"bind-still","directional master still; zero animation clips")
    add(bind/"render-receipt.json","bind-render-receipt","technical bind render; not selected")
    add(bind/"warrior-bind-8directions-contact.png","review-contact-sheet","bind stills; zero animation clips")
    add(Path(retarget["candidate"]["path"]),"real-retarget-glb-proof","diagnostic-reject")
    add(proof/"retarget-receipt.json","retarget-receipt","diagnostic-reject")
    for direction in motion["directions"]:
        for frame in direction["frames"]:
            add(Path(frame["file"]),"rendered-real-motion-diagnostic-frame","diagnostic-reject; nativeAnimationValidated0")
    add(diagnostics/"render-receipt.json","motion-render-receipt","diagnostic-reject")
    add(diagnostics/"render-receipt-cpu.json","unannotated-motion-render-receipt","diagnostic-reject")
    add(Path(motion["contactSheet"]["path"]),"motion-review-contact-sheet","diagnostic-reject")
    result={"schemaVersion":1,"ownerScope":"local own-art heroes CPU pipeline only; principal integrates",
            "heroId":"tristram.hero.warrior","variant":"light-unarmed technical base; equipment/tiers incomplete",
            "status":"master/bind delivery plus real retarget diagnostic rejection; no gameplay selection",
            "counts":{"preservedGlbMasters":3,"bindDirectionStills":8,"embeddedSourceWarriorClips":0,
                      "realMotionRetargetDiagnosticClips":1,"renderedMotionDiagnosticFrames":64,"nativeAnimationValidated":0},
            "bindProjection":bind_receipt["projection"],"motionDiagnosticProjection":motion["projection"],"sourceRetarget":retarget["source"],
            "checks":{"bindDeliveryChecksPassed":all(c["passed"] for c in checks["checks"]),"bindFixtureTests":5,
                      "realNoClipNegativeControlsWrittenFrames":sum(c["writtenFrames"] for c in checks["realSourceNegativeControls"]),
                      "motion":motion["validation"],"groundStatus":"diagnostic-reject"},
            "worstOf8RenderedPhases":motion["worstOf8RenderedPhases"],"independentFullClipQA":motion["independentFullClipQA"],"gameplayEligible":False,
            "gameplayGate":"only sprites until correct rig and complete animations validated, including Warrior and Ogden",
            "nativeSlotCompatibility":"not qualified; phase samples retain source duration, no native timing synchronization",
            "gaps":["retarget feet penetrate ground; foot contact/IK/phase QA required","all other actions/equipment/armor coverage absent",
                    "Rogue/Sorcerer source production belongs to the principal","no native animation integration or installation gate"],
            "files":files,"excludedAttempts":["bind-directions-framing-r0","bind-directions-preflight-r1","candidate-firstwalk1-json-failed-r0","candidate-firstwalk1/diagnostic-directions-framing-r0"],
            "globalRegistrySourceBuildInstallWrites":False,"npcWrites":False,"newPaidGenerations":0}
    output=pipeline/"pipeline-delivery-manifest.json"
    hp.write_json(output,result)
    print(json.dumps({"output":str(output),"files":len(files),"sha256":hp.sha(output.read_bytes()),"counts":result["counts"]}))


if __name__=="__main__":
    main()
