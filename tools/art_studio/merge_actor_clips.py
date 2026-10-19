"""Bundle compatible authored clips around one unchanged skinned mesh.

Copies only referenced animation buffer views from additional GLBs. Rejects
different rest skeleton, geometry, weights, inverse binds or material payloads.
No role coverage, selection, native timing or runtime activation is implied.
"""
import argparse
import copy
import json
import struct
from pathlib import Path
import numpy as np
import foot_contact as fc


def static_signature(model):
    keys=('nodes','meshes','skins','materials','textures','images','samplers','scene','scenes')
    state={key:model.doc.get(key) for key in keys}
    accessors=set()
    for mesh in model.doc.get('meshes',[]):
        for primitive in mesh['primitives']:
            accessors.update(primitive['attributes'].values())
            if 'indices' in primitive:accessors.add(primitive['indices'])
    for skin in model.doc.get('skins',[]):
        if 'inverseBindMatrices' in skin:accessors.add(skin['inverseBindMatrices'])
    state['resolvedAccessors']={str(i):fc.hp.sha(np.ascontiguousarray(model.array(i)).tobytes()) for i in sorted(accessors)}
    _,binary,_=model.reader_module.read_glb(model.path)
    state['imageBytes']={}
    for i,image in enumerate(model.doc.get('images',[])):
        if 'uri' in image or 'bufferView' not in image:raise ValueError('Embedded image required')
        view=model.doc['bufferViews'][image['bufferView']]
        if view.get('buffer',0)!=0:raise ValueError('Single embedded buffer required')
        offset=view.get('byteOffset',0)
        state['imageBytes'][str(i)]=fc.hp.sha(bytes(binary[offset:offset+view['byteLength']]))
    return fc.hp.sha(json.dumps(state,sort_keys=True,separators=(',',':')).encode())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--clip',action='append',required=True,help='Unique role=source.glb, first supplies the static mesh')
    p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    if args.output.exists():p.error('Use a fresh bundle directory')
    pairs=[]
    for value in args.clip:
        role,sep,path=value.partition('=')
        if not sep or not role or any(role==r for r,_ in pairs):p.error('Each clip needs one unique role')
        pairs.append((role,Path(path)))
    reader,_=fc.hp.load_reader(fc.hp.ROOT)
    models=[fc.hp.Model(path,reader) for _,path in pairs]
    signature=static_signature(models[0])
    if any(len(m.animations)!=1 or static_signature(m)!=signature for m in models):
        p.error('Each source must have one clip and an identical resolved static asset')
    document,base_binary,_=reader.read_glb(pairs[0][1]);document=copy.deepcopy(document)
    binary=bytearray(base_binary);document['animations']=[];records=[]
    for (role,path),model in zip(pairs,models):
        source,source_binary,_=reader.read_glb(path)
        if len(source.get('buffers',[]))!=1 or 'uri' in source['buffers'][0]:
            raise ValueError('Only one embedded source buffer is supported')
        animation=copy.deepcopy(source['animations'][0]);copied_views={};copied_accessors={}
        def accessor(index):
            if index in copied_accessors:return copied_accessors[index]
            item=copy.deepcopy(source['accessors'][index])
            if 'sparse' in item or 'bufferView' not in item:raise ValueError('Explicit dense animation accessor required')
            vi=item['bufferView']
            if vi not in copied_views:
                view=copy.deepcopy(source['bufferViews'][vi])
                if view.get('buffer',0)!=0:raise ValueError('Animation view belongs to another buffer')
                offset=view.get('byteOffset',0);end=offset+view['byteLength']
                if end>len(source_binary):raise ValueError('Animation view outside source buffer')
                binary.extend(b'\0'*((-len(binary))%4));view['buffer']=0;view['byteOffset']=len(binary)
                binary.extend(source_binary[offset:end]);copied_views[vi]=len(document['bufferViews']);document['bufferViews'].append(view)
            item['bufferView']=copied_views[vi];copied_accessors[index]=len(document['accessors']);document['accessors'].append(item)
            return copied_accessors[index]
        for sampler in animation['samplers']:
            sampler['input']=accessor(sampler['input']);sampler['output']=accessor(sampler['output'])
        original_name=animation.get('name','')
        animation['name']=role
        animation.setdefault('extras',{})['bundleSource']={'path':str(path),'sha256':fc.hp.sha(path.read_bytes()),'clip':original_name}
        document['animations'].append(animation)
        records.append({'role':role,'sourceSha256':fc.hp.sha(path.read_bytes()),'sourceClip':original_name,'copiedAnimationViews':len(copied_views)})
    document['buffers'][0]['byteLength']=len(binary)
    encoded=json.dumps(document,separators=(',',':'),ensure_ascii=True,allow_nan=False).encode();encoded+=b' '*((-len(encoded))%4)
    binary.extend(b'\0'*((-len(binary))%4));args.output.mkdir(parents=True);output=args.output/'warrior-clips-candidate.glb'
    output.write_bytes(struct.pack('<4sII',b'glTF',2,28+len(encoded)+len(binary))+struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(binary),0x004e4942)+binary)
    result=fc.hp.Model(output,reader);maximum=0
    if static_signature(result)!=signature:raise ValueError('Bundle static payload changed')
    for model,clip in zip(models,result.animations):
        reference=model.animations[0]
        for time in np.unique(np.r_[np.linspace(reference['start'],reference['end'],33),np.concatenate([c['times'] for c in reference['channels']])]):
            before=model.pose(reference,float(time));after=result.pose(clip,float(time))
            maximum=max(maximum,max(float(abs(before[i]-after[i]).max()) for i in before))
    if maximum>1e-8:raise ValueError('Bundled clip pose differs from source')
    receipt={'sources':records,'staticSignature':signature,'maximumWorldMatrixDelta':maximum,
        'sourceBaseBinaryPrefixPreserved':bytes(binary[:len(base_binary)])==bytes(base_binary),
        'candidateSha256':fc.hp.sha(output.read_bytes()),'clips':len(records),'geometryAndTextureStoredOnce':True,
        'gameplayCoverageComplete':False,'gameplaySelected':False,'status':'compatible-multiclip-candidate-needs-full-coverage-and-native-dispatch'}
    fc.hp.write_json(args.output/'receipt.json',receipt);print(json.dumps(receipt),flush=True)


if __name__=='__main__':main()
