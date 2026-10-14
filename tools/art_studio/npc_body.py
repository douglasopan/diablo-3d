"""Create an offline MakeHuman body recipe with shared topology/weights.

Only CC0 data is used. This is authoring output, not a game-ready character.
No original Warrior or runtime selected model is modified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import numpy as np

SOURCE=Path(__file__).resolve().parents[2]/'assets/character-sources/makehuman-r1'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def create(output, age=0.0, muscle=0.0, weight=0.0, feminine=0.0):
    values=(age,muscle,weight,feminine)
    if not all(np.isfinite(v) and 0<=v<=1 for v in values):raise ValueError('Parameters must be finite 0..1')
    if output.exists():raise ValueError('Use a new candidate directory')
    provenance=json.loads((SOURCE/'provenance.json').read_text(encoding='utf-8'))
    for name,h in provenance['files'].items():
        if sha(SOURCE/name)!=h:raise ValueError('Pinned MakeHuman source changed: '+name)
    lines=(SOURCE/'base.obj').read_text(encoding='utf-8').splitlines()
    rows=[i for i,s in enumerate(lines) if s.startswith('v ')]
    base=np.array([[float(v) for v in lines[i].split()[1:4]] for i in rows]);vertices=base.copy()
    def delta(name):
        out=np.zeros_like(base)
        for line in (SOURCE/name).read_text(encoding='utf-8').splitlines():
            if not line or line.startswith('#'):continue
            fields=line.split()
            if len(fields)!=4:raise ValueError('Malformed morph row')
            index=int(fields[0]);xyz=np.array(list(map(float,fields[1:])))
            if not 0<=index<len(base) or not np.isfinite(xyz).all():raise ValueError('Invalid morph index/value')
            out[index]+=xyz
        return out
    male=delta('caucasian-male-young.target');female=delta('caucasian-female-young.target');old=delta('caucasian-male-old.target')
    average=delta('universal-male-young-averagemuscle-averageweight.target')
    muscular=delta('universal-male-young-maxmuscle-averageweight.target')
    heavier=delta('universal-male-young-averagemuscle-maxweight.target')
    # Recipe is deliberately bounded. These are technical morph controls, not
    # claims of final Diablo anatomy or full MakeHuman macro slider equivalence.
    vertices+=(1-feminine)*male+feminine*female+(1-feminine)*age*(old-male)
    vertices+=average+muscle*(muscular-average)+weight*(heavier-average)
    if not np.isfinite(vertices).all():raise ValueError('Nonfinite geometry')
    for row,xyz in zip(rows,vertices):lines[row]='v '+' '.join(f'{v:.9g}' for v in xyz)
    output.mkdir(parents=True)
    (output/'body.obj').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    for name in ('default.mhskel','default_weights.mhw','LICENSE.md','LICENSE.ASSETS.md'):shutil.copyfile(SOURCE/name,output/name)
    receipt={'schemaVersion':1,'status':'authoring-candidate-not-runtime','sourceCommit':provenance['commit'],'license':'CC0-1.0','parameters':{'ageBlend':age,'muscleBlend':muscle,'weightBlend':weight,'feminineBlend':feminine},'vertices':len(vertices),'sameTopology':True,'sameVertexIndices':True,'sourceRigBones':163,'rigStatus':'source head/tail anchors must be rebuilt on morphed mesh; no direct import into 96-joint actor format','appearanceStatus':'no final skin/hair/clothes; selected Warrior unaffected','clips':0,'gameplayEligible':False,'outputSha256':sha(output/'body.obj'),'sourceFilesUnchanged':True}
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    return receipt

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    for key in ('age','muscle','weight','feminine'):p.add_argument('--'+key,type=float,default=0)
    a=p.parse_args();print(json.dumps(create(a.output,a.age,a.muscle,a.weight,a.feminine)))
