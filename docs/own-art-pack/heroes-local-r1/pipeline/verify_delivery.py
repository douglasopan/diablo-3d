#!/usr/bin/env python3
"""Verify actual Warrior outputs, reject nonexistent clips, make an honest sheet."""
import argparse
import contextlib
from datetime import datetime, timezone
import io
import json
from pathlib import Path
import platform
import subprocess
import sys
import tempfile

import numpy as np
import PIL
from PIL import Image, ImageDraw

import hero_pipeline as hp


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--render-dir",type=Path,required=True)
    parser.add_argument("--output",type=Path,default=Path(__file__).with_name("pipeline-validation.json"))
    args=parser.parse_args()
    receipt_path=args.render_dir/"render-receipt.json"
    receipt=json.loads(receipt_path.read_text(encoding="utf-8"))
    source=Path(receipt["model"]["source"]["path"])
    checks=[]
    def check(ok,name,detail=None):
        checks.append({"name":name,"passed":bool(ok),"detail":detail})
        if not ok:
            raise ValueError(name+" failed")
    check(hp.sha(source.read_bytes())==receipt["model"]["source"]["sha256"],"source hash unchanged")
    check(receipt["tool"]["sha256"]==hp.sha(Path(hp.__file__).read_bytes()),"render tool exact source hash")
    check(hp.sha(Path(receipt["reader"]["path"]).read_bytes())==receipt["reader"]["sha256"],"reader exact source hash")
    check(receipt["status"]=="bind-stills-only-zero-clips" and receipt["model"]["counts"]["clips"]==0,"bind never counted as a clip")
    check(len(receipt["sourcePoseSha256"])==1 and receipt["validation"]["poseCount"]==1,"one actual bind pose")
    check([d["direction"] for d in receipt["directions"]]==[d for d,_ in hp.DIRECTIONS],"eight native directions in order")
    pivot=receipt["projection"]["pixelPivot"]
    images=[]
    for direction in receipt["directions"]:
        check(len(direction["frames"])==1,"one still for "+direction["direction"])
        frame=direction["frames"][0]
        path=Path(frame["file"])
        check(hp.sha(path.read_bytes())==frame["sha256"],"PNG hash "+direction["direction"])
        picture=Image.open(path)
        pixels=np.asarray(picture)
        check(picture.mode=="RGBA" and list(picture.size)==receipt["projection"]["canvas"],"RGBA/canvas "+direction["direction"])
        check(hp.sha(pixels.tobytes())==frame["rgbaPixelsSha256"],"pixel hash "+direction["direction"])
        covered=pixels[:,:,3]>0
        check(covered.any() and not (covered[0].any() or covered[-1].any() or covered[:,0].any() or covered[:,-1].any()),"nonempty/not clipped "+direction["direction"])
        check(frame["drawnTriangles"]>0 and frame["culledTriangles"]>0,"actual triangles/backface culling "+direction["direction"])
        check(frame["pixelPivot"]==pivot,"fixed pivot "+direction["direction"])
        images.append((direction["direction"],picture.copy()))
    negative=[]
    with tempfile.TemporaryDirectory(prefix="own-hero-negative-") as directory:
        for clip in ("idle","walk"):
            output=Path(directory)/(clip+"-must-not-exist")
            argv=["render",str(source),"--output",str(output),"--clip",clip,"--state",clip,"--frames","8","--footpoint","0,0,0"]
            stderr=io.StringIO()
            exit_code=None
            with contextlib.redirect_stderr(stderr):
                try:
                    hp.main(argv)
                except SystemExit as error:
                    exit_code=error.code
            check(exit_code==2 and not output.exists(),"real Warrior nonexistent "+clip+" refused before writing")
            negative.append({"args":argv,"exitCode":exit_code,"stderr":stderr.getvalue().strip(),"outputDirectoryCreated":output.exists(),
                             "writtenFrames":len(list(output.rglob("*.png"))) if output.exists() else 0,"sourceClips":0})
    result=subprocess.run([sys.executable,"-m","unittest","-v","test_hero_pipeline"],cwd=Path(__file__).parent,
                          capture_output=True,text=True,check=False)
    check(result.returncode==0,"five CPU fixture controls",result.stderr.strip())
    # A labelled review artifact, never listed as a game frame/state.
    sheet=Image.new("RGBA",(1024,600),(22,22,26,255))
    drawing=ImageDraw.Draw(sheet)
    drawing.text((14,8),"WARRIOR / BIND ONLY / 8 DIRECTIONS / 0 ANIMATION CLIPS",fill=(235,235,235,255))
    for i,(direction,picture) in enumerate(images):
        x,y=(i%4)*256,30+(i//4)*284
        preview=picture.resize((256,256),Image.Resampling.LANCZOS)
        sheet.alpha_composite(preview,(x,y))
        drawing.text((x+12,y+258),f"{i}: {direction.upper()} | bind",fill=(220,220,220,255))
    sheet_path=args.render_dir/"warrior-bind-8directions-contact.png"
    sheet.save(sheet_path)
    pyexe=Path(sys.executable)
    godot=Path("D:/Diablo 1 3D/.tools/godot/Godot_v4.7.2-stable_win64_console.exe")
    report={"schemaVersion":1,"createdAtUtc":datetime.now(timezone.utc).isoformat(),"status":"technical bind QA passed; gameplay ineligible; zero clips",
            "renderReceipt":{"path":str(receipt_path.resolve()),"sha256":hp.sha(receipt_path.read_bytes())},
            "checks":checks,"realSourceNegativeControls":negative,
            "contactSheet":{"path":str(sheet_path.resolve()),"sha256":hp.sha(sheet_path.read_bytes()),"kind":"labelled review artifact; not native frame"},
            "runtime":{"python":sys.version,"pythonExe":str(pyexe),"pythonExeSha256":hp.sha(pyexe.read_bytes()),
                       "platform":platform.platform(),"numpy":np.__version__,"pillow":PIL.__version__,
                       "godotInstalledUnused":{"path":str(godot),"sha256":hp.sha(godot.read_bytes()) if godot.exists() else None},
                       "gpuUsed":False,"renderer":"CPU NumPy/Pillow; no Godot/Blender startup"},
            "gameplayGate":"Only sprites until rig and all animation states correctly validated; including Warrior/Ogden; no candidate promotion",
            "limitations":["diffuse material approximation; full PBR/light match not claimed","human visual acceptance pending",
                           "no variant completeness; no native state/slot timing integration","8 direction pixel variance comes from rotating one bind pose, not animation"],
            "globalWrites":False}
    hp.write_json(args.output,report)
    print(json.dumps({"output":str(args.output),"checks":len(checks),"allPassed":all(c["passed"] for c in checks),
                      "negativeControls":[{"clip":n["args"][n["args"].index("--clip")+1],"exitCode":n["exitCode"],"writtenFrames":n["writtenFrames"]} for n in negative],
                      "contactSheet":str(sheet_path)}))


if __name__=="__main__":
    main()
