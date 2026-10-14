"""Drop only empty, optional FB_ngon_encoding on explicit triangles.

Preserves all binary geometry, textures, animation data and material features.
The actor exporter separately records its explicit diffuse-only conversion.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from inspect_glb import read_glb

def prepare(source,output):
    if output.exists() or source.resolve()==output.resolve():raise ValueError('Preserve source/previous derivative')
    document,binary,_=read_glb(source);original=binary;removed=0
    for mesh in document.get('meshes',[]):
        for primitive in mesh.get('primitives',[]):
            extensions=primitive.get('extensions',{})
            if 'FB_ngon_encoding' not in extensions:continue
            if extensions['FB_ngon_encoding']!={} or primitive.get('mode',4)!=4 or 'FB_ngon_encoding' in document.get('extensionsRequired',[]):raise ValueError('Not an empty optional marker on explicit triangles')
            del extensions['FB_ngon_encoding'];removed+=1
            if not extensions:primitive.pop('extensions')
    if removed and 'FB_ngon_encoding' in document.get('extensionsUsed',[]):document['extensionsUsed'].remove('FB_ngon_encoding')
    encoded=json.dumps(document,separators=(',',':')).encode();encoded+=b' '*((-len(encoded))%4);binary+=b'\0'*((-len(binary))%4)
    output.write_bytes(struct.pack('<4sII',b'glTF',2,28+len(encoded)+len(binary))+struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(binary),0x004e4942)+binary)
    reopened,checked,_=read_glb(output)
    if checked[:len(original)]!=original:raise ValueError('Binary content changed')
    receipt={'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'outputSha256':hashlib.sha256(output.read_bytes()).hexdigest(),'optionalEmptyMarkersRemoved':removed,'binaryPayloadUnchanged':True,'geometryTexturesMaterialAndAnimationDataUnchanged':True,'gameplayIntegrated':False}
    output.with_suffix('.receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');return receipt

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();print(json.dumps(prepare(a.source,a.output)))
