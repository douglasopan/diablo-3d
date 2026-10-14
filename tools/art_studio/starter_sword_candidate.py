"""Original offline Short Sword candidate; actual closed geometry, no raster skin.

The existing own concept/native private silhouette are read-only references.
No image generation/editing, paid service, registry selection or game mutation.
All preview PNGs are renders of the exported 3D geometry. Physical dimensions,
wear and the hand socket are explicitly inferred candidates, not approval.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
from types import SimpleNamespace

import numpy as np
from scipy.spatial.transform import Rotation
from scipy.optimize import least_squares

REPO = Path(__file__).resolve().parents[2]
WORKSPACE = REPO.parent
sys.path.insert(0, str(REPO / 'docs/own-art-pack/heroes-local-r1/pipeline'))
import hero_pipeline as hp
import starter_shield_candidate as sc
import foot_contact as fc
import grip_surface_audit as ga
import shield_grip_candidate as sg


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Mesh:
    """Hard-edge UV vertices; closure is checked after geometric welding."""
    def __init__(self):
        self.vertices, self.normals, self.uvs, self.faces = [], [], [], []

    def face(self, points, uvs, target):
        points = np.asarray(points, float)
        normal = np.cross(points[1]-points[0], points[2]-points[0])
        if normal @ target < 0:
            points, uvs = points[::-1], np.asarray(uvs)[::-1]
            normal *= -1
        length = np.linalg.norm(normal)
        if length <= 1e-12:
            raise ValueError('Degenerate authored face')
        base = len(self.vertices)
        self.vertices.extend(points); self.normals.extend([normal/length]*len(points)); self.uvs.extend(uvs)
        for i in range(1, len(points)-1):
            self.faces.append([base, base+i, base+i+1])

    def geometry(self):
        return tuple(np.asarray(v, dtype=int if i == 3 else float)
                     for i, v in enumerate((self.vertices, self.normals, self.uvs, self.faces)))


class Asset:
    def __init__(self):
        self.parts = []

    def add(self, name, geometry, material):
        positions, normals, uv, faces = geometry
        colors = np.ones((len(positions), 4))
        # Surface finish is restrained authored vertex color, not a raster.
        # It remains visible in GLB COLOR_0 and is not screenshot-only detail.
        wear = .955 + .035*np.sin(positions[:,0]*47 + positions[:,1]*31 + positions[:,2]*19)**2
        colors[:, :3] *= wear[:, None]
        self.parts.append(dict(name=name, positions=positions, normals=normals, uv=uv, faces=faces, colors=colors, material=material))

    def save(self, path, provenance):
        def material(name, color, metal, rough):
            return {'name':name, 'pbrMetallicRoughness':{'baseColorFactor':[*color,1], 'metallicFactor':metal, 'roughnessFactor':rough}}
        doc = {'asset':{'version':'2.0','generator':'Diablo3D original offline starter_sword_candidate.py r1'},
               'scene':0, 'scenes':[{'nodes':[]}], 'nodes':[], 'meshes':[], 'materials':[
                   material('restrained-worn-steel-blade', [.50,.50,.465], .94,.43),
                   material('bright-beveled-steel-edge', [.61,.61,.57], .96,.30),
                   material('recessed-steel-channel', [.205,.205,.185], .91,.56),
                   material('wrought-crossguard-and-pommel', [.40,.395,.36], .93,.47),
                   material('dark-red-brown-leather-core', [.145,.047,.019], .02,.86),
                   material('red-brown-leather-helical-wrap', [.23,.075,.031], .02,.78)],
               'buffers':[{'byteLength':0}], 'bufferViews':[], 'accessors':[], 'extras':provenance}
        binary = bytearray()
        def accessor(values, kind, component=5126, target=34962):
            values = np.asarray(values, dtype='<f4' if component == 5126 else '<u4')
            binary.extend(b'\0'*((-len(binary))%4))
            start = len(binary); binary.extend(values.tobytes())
            view = len(doc['bufferViews']); doc['bufferViews'].append({'buffer':0,'byteOffset':start,'byteLength':values.nbytes,'target':target})
            item = {'bufferView':view,'componentType':component,'count':len(values),'type':kind}
            if kind == 'VEC3': item.update(min=values.min(axis=0).tolist(), max=values.max(axis=0).tolist())
            doc['accessors'].append(item); return len(doc['accessors'])-1
        for part in self.parts:
            attributes = {key:accessor(part[field],kind) for key,field,kind in
                          [('POSITION','positions','VEC3'),('NORMAL','normals','VEC3'),('TEXCOORD_0','uv','VEC2'),('COLOR_0','colors','VEC4')]}
            indices = accessor(part['faces'].reshape(-1),'SCALAR',5125,34963)
            mesh = len(doc['meshes']); doc['meshes'].append({'name':part['name'],'primitives':[{'attributes':attributes,'indices':indices,'material':part['material'],'mode':4}]})
            doc['scenes'][0]['nodes'].append(len(doc['nodes'])); doc['nodes'].append({'name':part['name'],'mesh':mesh})
        doc['buffers'][0]['byteLength'] = len(binary)
        encoded = json.dumps(doc,separators=(',',':'),allow_nan=False).encode('utf-8'); encoded += b' '*((-len(encoded))%4)
        binary.extend(b'\0'*((-len(binary))%4))
        total = 12+8+len(encoded)+8+len(binary)
        path.write_bytes(struct.pack('<4sII',b'glTF',2,total)+struct.pack('<II',len(encoded),0x4E4F534A)+encoded+struct.pack('<II',len(binary),0x004E4942)+binary)


def blade():
    """Closed faceted blade with actual front/back ridge and short V channel."""
    stations = [( .105,.029),(.135,.030),(.275,.0295),(.340,.0290),(.405,.027),(.475,.020),(.540,.011)]
    tip = np.array([0,.606,0.])
    rings = []
    for y, width in stations:
        thickness = .0058*(1-.35*(y-.105)/.501)
        channel = max(.00030, .0038*(1-(y-.105)/.225))
        channel_z = thickness*.64 if y < .34 else thickness
        xs = [-width,-width*.73,-channel,0,channel,width*.73,width]
        zs = [.00048,thickness*.52,thickness,channel_z,thickness,thickness*.52,.00048]
        front = [[x,y,z] for x,z in zip(xs,zs)]
        back = [[x,y,-z] for x,z in zip(xs[::-1],zs[::-1])]
        rings.append(np.array(front+back))
    meshes = [Mesh(),Mesh(),Mesh()]
    def uv(point, front):
        u = .04+(point[0]/.030+1)*.205 if front else .55+(point[0]/.030+1)*.205
        return [u, .10+.87*(point[1]-.105)/.501]
    for k in range(len(rings)):
        current = rings[k]
        for j in range(14):
            nxt = (j+1)%14
            points = [current[j],current[nxt],tip] if k == len(rings)-1 else [current[j],current[nxt],rings[k+1][nxt],rings[k+1][j]]
            middle = np.mean(points,axis=0); target = np.array([middle[0],0,middle[2]])
            material = 1 if j in (0,5,6,7,12,13) else 2 if j in (2,3,9,10) and k < 2 else 0
            meshes[material].face(points,[uv(p,j<7) for p in points],target)
    # Concave star-shaped channel section is triangulated around an interior
    # center; the cap is real geometry hidden just above the tang/guard.
    center = np.array([0,.105,0.])
    for j in range(14):
        meshes[0].face([center,rings[0][j],rings[0][(j+1)%14]],[[.25,.04],[.08,.02],[.42,.02]],[0,-1,0])
    return [mesh.geometry() for mesh in meshes]


def triangulate_polygon(polygon):
    polygon = np.asarray(polygon,float)
    cross = lambda a,b: a[0]*b[1]-a[1]*b[0]
    remaining, triangles = list(range(len(polygon))), []
    while len(remaining)>3:
        found = False
        for k, middle in enumerate(remaining):
            a,b,c = remaining[k-1],middle,remaining[(k+1)%len(remaining)]
            if cross(polygon[b]-polygon[a],polygon[c]-polygon[b]) <= 1e-12: continue
            inside = False
            for q in remaining:
                if q in (a,b,c): continue
                p = polygon[q]
                if all(cross(v-u,p-u)>=-1e-12 for u,v in [(polygon[a],polygon[b]),(polygon[b],polygon[c]),(polygon[c],polygon[a])]): inside = True; break
            if inside: continue
            triangles.append((a,b,c)); del remaining[k]; found=True; break
        if not found: raise ValueError('Guard cap triangulation failed')
    return triangles+[tuple(remaining)]


def guard():
    polygon = np.array([[-.071,.075],[-.071,.100],[-.067,.104],[.067,.104],[.071,.100],[.071,.075],[.059,.077],[.040,.083],[-.040,.083],[-.059,.077]])
    if (polygon[:,0]*np.roll(polygon[:,1],-1)-polygon[:,1]*np.roll(polygon[:,0],-1)).sum()<0: polygon=polygon[::-1]
    center=np.array([0,.0895]); inset=center+(polygon-center)*[.955,.72]
    rings=[np.column_stack([xy,np.repeat(z,len(xy))]) for xy,z in [(inset,.017),(polygon,.012),(polygon,-.012),(inset,-.017)]]
    mesh=Mesh()
    uv=lambda p:[(p[0]+.071)/.142,(p[1]-.075)/.029]
    for k in range(3):
        for j in range(len(polygon)):
            n=(j+1)%len(polygon); edge=polygon[n]-polygon[j]
            mesh.face([rings[k][j],rings[k][n],rings[k+1][n],rings[k+1][j]], [uv(p) for p in [rings[k][j],rings[k][n],rings[k+1][n],rings[k+1][j]]], [edge[1],-edge[0],0])
    for ring,target in [(rings[0],[0,0,1]),(rings[-1],[0,0,-1])]:
        for face in triangulate_polygon(inset): mesh.face(ring[list(face)],[uv(p) for p in ring[list(face)]],target)
    return mesh.geometry()


def y_lathe(profile, segments=64):
    p,n,uv,f=sc.lathe(profile,segments)
    rotation=np.array([[1,0,0],[0,0,1],[0,-1,0]])
    return p@rotation.T,n@rotation.T,uv,f


def wrap():
    mesh=Mesh(); turns=6; steps=288; rings=[]
    for k in range(steps+1):
        angle=2*math.pi*turns*k/steps; y=-.062+.124*k/steps
        radial=np.array([math.cos(angle),0,math.sin(angle)])
        # Actual band volume above the core, not a drawn spiral.
        rings.append(np.array([radial*radius+[0,y+offset,0] for offset,radius in [(-.009,.01665),(-.009,.01735),(.009,.01735),(.009,.01665)]]))
    for k in range(steps):
        angle=2*math.pi*turns*(k+.5)/steps; radial=np.array([math.cos(angle),0,math.sin(angle)])
        targets=([0,-1,0],radial,[0,1,0],-radial)
        for j in range(4):
            q=(j+1)%4; points=[rings[k][j],rings[k][q],rings[k+1][q],rings[k+1][j]]
            mesh.face(points,[[k/steps,0],[k/steps,1],[(k+1)/steps,1],[(k+1)/steps,0]],targets[j])
    for k, sign in [(0,-1),(steps,1)]:
        angle=2*math.pi*turns*k/steps
        tangent=np.array([-math.sin(angle)*.017, .124/(2*math.pi*turns), math.cos(angle)*.017])*sign
        mesh.face(rings[k],[[0,0],[1,0],[1,1],[0,1]],tangent)
    return mesh.geometry()


def author():
    asset=Asset()
    for name, geometry, material in zip(['blade-front-back-facets','blade-sharp-beveled-edges','blade-recessed-base-channel'],blade(),[0,1,2]): asset.add(name,geometry,material)
    asset.add('short-hidden-tang',sc.box((0,.076,0),(.011,.067,.009)),3)
    asset.add('modest-beveled-straight-crossguard',guard(),3)
    asset.add('closed-cylindrical-leather-core',y_lathe([(0,.077),(.0158,.077),(.0164,.072),(.0165,.060),(.0161,.02),(.0161,-.02),(.0165,-.060),(.0164,-.072),(.0158,-.077),(0,-.077)]),4)
    asset.add('raised-red-brown-helical-leather-wrap',wrap(),5)
    asset.add('upper-steel-grip-collar',y_lathe([(0,.084),(.018,.084),(.0195,.081),(.0195,.076),(.018,.073),(0,.073)]),3)
    asset.add('pommel-leather-neck',y_lathe([(0,-.076),(.019,-.076),(.020,-.082),(.020,-.119),(.018,-.125),(0,-.125)]),4)
    asset.add('pommel-first-beveled-steel-disc',y_lathe([(0,-.078),(.023,-.078),(.026,-.081),(.026,-.091),(.023,-.094),(0,-.094)]),3)
    asset.add('pommel-second-beveled-steel-disc',y_lathe([(0,-.096),(.023,-.096),(.0265,-.099),(.0265,-.111),(.023,-.115),(0,-.115)]),1)
    return asset


def audit(model):
    result=sc.mesh_audit(model)
    # Blade material partitions are closed only in their union. Audit welded
    # union by named assembly, while distinct GLB material parts stay separate.
    groups={'blade':[p for p in model.primitives if model.nodes[p['node']]['name'].startswith('blade-')]}
    for part in model.primitives:
        name=model.nodes[part['node']]['name']
        if not name.startswith('blade-'): groups[name]=[part]
    assemblies=[]
    for name, parts in groups.items():
        vertices=np.concatenate([p['positions'] for p in parts]); faces=[]; offset=0
        for part in parts: faces.append(part['triangles']+offset); offset+=len(part['positions'])
        faces=np.concatenate(faces); _,weld=np.unique(np.round(vertices,7),axis=0,return_inverse=True)
        edges=np.sort(np.concatenate([weld[faces[:,[0,1]]],weld[faces[:,[1,2]]],weld[faces[:,[2,0]]]]),axis=1)
        _,incidence=np.unique(edges,axis=0,return_counts=True)
        area=np.linalg.norm(np.cross(vertices[faces[:,1]]-vertices[faces[:,0]],vertices[faces[:,2]]-vertices[faces[:,0]]),axis=1)/2
        volume=np.einsum('ij,ij->i',vertices[faces[:,0]],np.cross(vertices[faces[:,1]],vertices[faces[:,2]])).sum()/6
        assemblies.append(dict(name=name,triangles=len(faces),boundaryOrNonmanifoldEdges=int(np.sum(incidence!=2)),minimumTriangleArea=float(area.min()),signedVolume=float(volume)))
    result['assemblies']=assemblies
    # Material partitions of the blade are deliberately open at shared seams.
    # The physical blade assembly, rather than every material primitive, closes.
    del result['checks']['allPartsClosed']
    result['checks']['allPhysicalAssembliesClosed']=all(a['boundaryOrNonmanifoldEdges']==0 for a in assemblies)
    result['checks']['outwardWindingPositiveVolume']=all(a['signedVolume']>1e-12 for a in assemblies)
    result['checks']['actualFrontAndBackDepth']=result['bounds']['extent'][2]>.03
    result['gripAnchor']=[0,0,0]
    result['scope']='Closed blade union preserves separate material facets; all other authored components are independently closed. Overlapping fitted components are not a Boolean union or hand collision proof.'
    return result


def views(model, output):
    model.load_materials(); geometry=model.geometry(model.rest_world)
    center=np.mean([np.concatenate([p['vertices'] for p in geometry]).min(axis=0),np.concatenate([p['vertices'] for p in geometry]).max(axis=0)],axis=0)
    def centered(_):
        return [{**p,'vertices':p['vertices']-center} for p in geometry]
    proxy=SimpleNamespace(materials=model.materials,rest_world=model.rest_world,geometry=centered)
    records=[]
    for label,yaw in [('front',0),('front-three-quarter',45),('side',90),('back',180),('back-three-quarter',225),('opposite-side',270)]:
        picture,stats=sc.studio_raster(proxy,yaw,size=(512,960),ppm=1100,supersample=2)
        picture.save(output/(label+'.png')); records.append(dict(view=label,yaw=yaw,path=label+'.png',stats=stats))
    return records


def attach(reader, sword, body_path, output, samples):
    body=hp.Model(body_path,reader); body.load_materials()
    names={node.get('name'):i for i,node in enumerate(body.nodes)}
    hand=names['mixamorig:RightHand']; idle=body.animations[0]; first=body.pose(idle,idle['start'])
    wrist=first[hand][:3,3]
    index=first[names['mixamorig:RightHandIndex1']][:3,3]; pinky=first[names['mixamorig:RightHandPinky1']][:3,3]
    middle=first[names['mixamorig:RightHandMiddle1']][:3,3]
    y=index-pinky; y/=np.linalg.norm(y)
    finger=middle-wrist; finger-=y*(finger@y); finger/=np.linalg.norm(finger)
    z=np.cross(y,finger); z/=np.linalg.norm(z); x=np.cross(y,z)
    # Derive a new anatomical socket, not the old Great Sword R4 transform.
    # The palm point is deliberately inferred and remains subject to contact QA.
    anchor=wrist+(middle-wrist)*.62+z*.009
    body_points=np.concatenate([p['vertices'] for p in body.geometry(first)])
    scale=float(np.ptp(body_points[:,1])/1.75)
    world=np.eye(4); world[:3,:3]=np.column_stack([x,y,z])*scale; world[:3,3]=anchor
    local=np.linalg.inv(first[hand])@world
    local_scale=float(np.linalg.norm(local[:3,0])); quat=Rotation.from_matrix(local[:3,:3]/local_scale).as_quat()
    rigid=np.concatenate([p['vertices'] for p in sword.geometry(sword.rest_world)])
    mixed=SimpleNamespace(materials={**body.materials,**{i+len(body.materials):m for i,m in sword.materials.items()}})
    ground=[]
    for clip in body.animations:
        minimum=math.inf
        for phase in np.linspace(0,1,samples):
            pose=body.pose(clip,clip['start']+phase*(clip['end']-clip['start'])); matrix=pose[hand]@local
            points=(np.column_stack([rigid,np.ones(len(rigid))])@matrix.T)[:,:3]
            minimum=min(minimum,float(points[:,1].min()))
        ground.append(dict(clip=clip['name'],samples=samples,minimumSwordY=minimum,groundTolerance=-.002,groundPass=minimum>=-.002))
        print(json.dumps({'groundClip':clip['name'],'minimumY':minimum}),flush=True)
    records=[]
    for clip_index,phase in [(0,0),(0,.5),(1,.5),(2,.5),(4,.68),(8,1)]:
        clip=body.animations[clip_index]; pose=body.pose(clip,clip['start']+phase*(clip['end']-clip['start'])); matrix=pose[hand]@local
        geometry=body.geometry(pose)+sc.transformed_geometry(sword,matrix,len(body.materials))
        hand_center=(matrix@np.array([0,0,0,1]))[:3]
        for label,yaw,ppm,pivot,center,size in [('full-front',0,510,[384,720],np.zeros(3),(768,768)),('full-side',90,510,[384,720],np.zeros(3),(768,768)),('hand-front',0,3000,[384,384],hand_center,(768,768)),('hand-side',90,3000,[384,384],hand_center,(768,768))]:
            picture,stats=hp.raster(mixed,geometry,yaw,size,pivot,center,ppm,15,1,False)
            name=f'{clip_index}-{phase:g}-{label}.png'; picture.save(output/name)
            records.append(dict(clip=clip['name'],phase=phase,view=label,file=name,stats=stats))
    record={'bodyPath':str(body_path),'bodySha256':sha(body_path),'swordSha256':sha(sword.path),'bone':'mixamorig:RightHand','sourceGrip':[0,0,0],
            'localTransform':{'translation':local[:3,3].tolist(),'rotationXYZW':quat.tolist(),'uniformScale':local_scale},
            'worldPalmCandidate':anchor.tolist(),'method':'New RightHand/index/middle/pinky anatomical basis and independently inferred palm point; physical scale = body height / nominal 1.75m. R4 Great Sword TRS is not applied.',
            'oldR4RecipeApplied':False,'ground':ground,'views':records,'limits':['New palm placement is not a grasp/contact/collision approval.','Closed fingers authored around the old Great Sword may require a new finger recipe for this different cylindrical handle.','Metres and world fit remain inferred.','Python pose evaluation/ground sampling is not C++ loader/attachment proof, admission or gameplay.']}
    hp.write_json(output/'SOCKET-CANDIDATE.json',record)
    return record


def fit_new_socket(body, record):
    """Bounded translation fit against current skin; no old weapon recipe."""
    names={node.get('name'):i for i,node in enumerate(body.nodes)}; hand=names[record['bone']]
    clip=body.animations[0]; world=body.pose(clip,clip['start']); inverse_hand=np.linalg.inv(world[hand])
    all_points=[]; distal={}
    for finger in ('Index','Middle','Ring','Pinky','Thumb','Palm'):
        target=[hand] if finger=='Palm' else [names['mixamorig:RightHand'+finger+str(i)] for i in (1,2,3)]
        selected_points=[]; distal_points=[]
        for primitive in body.primitives:
            if primitive['skin'] is None: continue
            joints,_=body.skins[primitive['skin']]
            weight=sum((ws*np.isin(joints[js],target)).sum(axis=1) for js,ws in primitive['pairs'])
            selected=np.flatnonzero(weight>.55)
            if len(selected): selected_points.extend(fc.points(body,primitive,world,selected))
            if finger!='Palm':
                weight=sum((ws*(joints[js]==target[-1])).sum(axis=1) for js,ws in primitive['pairs'])
                selected=np.flatnonzero(weight>.5)
                if len(selected): distal_points.extend(fc.points(body,primitive,world,selected))
        all_points.extend(selected_points)
        if distal_points: distal[finger]=(np.column_stack([distal_points,np.ones(len(distal_points))])@inverse_hand.T)[:,:3]
    points=(np.column_stack([all_points,np.ones(len(all_points))])@inverse_hand.T)[:,:3]
    original=np.array(record['localTransform']['translation']); scale=record['localTransform']['uniformScale']
    rotation=Rotation.from_quat(record['localTransform']['rotationXYZW']).as_matrix()
    def distance(p, delta):
        source=(p-original-delta)@rotation/scale
        radial=np.linalg.norm(source[:,[0,2]],axis=1)-.01735
        axial=np.abs(source[:,1])-.077
        pair=np.column_stack([radial,axial])
        return (np.linalg.norm(np.maximum(pair,0),axis=1)+np.minimum(pair.max(axis=1),0))*scale
    def residual(delta):
        d=distance(points,delta)
        terms=[np.minimum(d-.0003,0)*400]
        for p in distal.values(): terms.append((np.sort(abs(distance(p,delta)))[:8]-.0010)*75)
        terms.append(delta*.015)
        return np.concatenate(terms)
    seeds=[np.zeros(3),[0,.018,0],[0,.028,.004],[0,.025,-.004]]
    solutions=[least_squares(residual,seed,bounds=([-.02,-.015,-.02],[.02,.05,.03]),max_nfev=200,ftol=1e-9,xtol=1e-9,gtol=1e-9) for seed in seeds]
    chosen=min(solutions,key=lambda solution:np.linalg.norm(solution.fun))
    updated=json.loads(json.dumps(record)); updated['localTransform']['translation']=(original+chosen.x).tolist()
    updated['socketRecalibration']={'basis':'Current right-hand weighted skin + new cylindrical handle circumscribed proxy; final audit uses actual closed triangles',
        'handLocalOffset':chosen.x.tolist(),'orientationAndUniformScalePreserved':True,'solverResidual':float(np.linalg.norm(chosen.fun)),
        'solverSuccess':bool(chosen.success),'oldR4RecipeApplied':False,'fingerPoseModified':False,
        'limits':'Proxy is only an optimization aid, not a proof of contact or nonpenetration; exact contact audit controls the finding.'}
    return updated


def audit_existing(reader, candidate, socket_path, output, samples, fit_socket=False):
    record=json.loads(socket_path.read_text(encoding='utf-8-sig'))
    body_path=Path(record['bodyPath'])
    if sha(candidate)!=record['swordSha256'] or sha(body_path)!=record['bodySha256']:
        raise ValueError('Existing sword/socket/body identity mismatch')
    sword=hp.Model(candidate,reader); body=hp.Model(body_path,reader)
    if fit_socket:
        original_socket=socket_path
        record=fit_new_socket(body,record)
        record['referenceSocketPath']=str(original_socket);record['referenceSocketSha256']=sha(original_socket)
        # The old render/ground rows describe a different placement. Keep the
        # old receipt separate and never relabel those rows as this socket.
        record.pop('views',None);record.pop('ground',None)
        record['status']='private translation recalibration; exact geometry audit and grasp review still required'
        socket_path=output/'SOCKET-CANDIDATE.json';hp.write_json(socket_path,record)
        print(json.dumps({'socketTranslationRecalibrated':record['socketRecalibration']}),flush=True)
    names={node.get('name'):i for i,node in enumerate(body.nodes)}; hand=names[record['bone']]
    trs=record['localTransform']; local=np.eye(4)
    local[:3,:3]=Rotation.from_quat(trs['rotationXYZW']).as_matrix()*trs['uniformScale']; local[:3,3]=trs['translation']
    rigid=np.concatenate([p['vertices'] for p in sword.geometry(sword.rest_world)])
    ground=[]
    for clip in body.animations:
        minimum=math.inf
        for phase in np.linspace(0,1,samples):
            world=body.pose(clip,clip['start']+phase*(clip['end']-clip['start']))
            points=(np.column_stack([rigid,np.ones(len(rigid))])@(world[hand]@local).T)[:,:3]
            minimum=min(minimum,float(points[:,1].min()))
        ground.append(dict(clip=clip['name'],samples=samples,minimumSwordY=minimum,groundTolerance=-.002,groundPass=minimum>=-.002))
        print(json.dumps({'denseGroundClip':clip['name'],'minimumY':minimum}),flush=True)
    hp.write_json(output/'DENSE-GROUND-AUDIT.json',dict(swordSha256=sha(candidate),bodySha256=sha(body_path),socketSha256=sha(socket_path),
                  rows=ground,evaluator='Independent Python GLB evaluator; not native C++ attachment evaluation',units='Actor source units; physical game fit pending'))
    grip=[]
    for primitive in sword.geometry(sword.rest_world):
        if sword.nodes[primitive['node']].get('name') in ('closed-cylindrical-leather-core','raised-red-brown-helical-leather-wrap'):
            grip.extend(primitive['vertices'][primitive['triangles']])
    grip=np.asarray(grip)
    groups={}
    for finger in ('Index','Middle','Ring','Pinky','Thumb','Palm'):
        nodes=[hand] if finger=='Palm' else [names['mixamorig:RightHand'+finger+str(i)] for i in (1,2,3)]
        all_points,distal=[],[]
        for primitive in body.primitives:
            if primitive['skin'] is None: continue
            joints,_=body.skins[primitive['skin']]
            weight=sum((ws*np.isin(joints[js],nodes)).sum(axis=1) for js,ws in primitive['pairs'])
            selected=np.flatnonzero(weight>.55)
            if len(selected): all_points.append((primitive,selected))
            if finger!='Palm':
                weight=sum((ws*(joints[js]==nodes[-1])).sum(axis=1) for js,ws in primitive['pairs'])
                selected=np.flatnonzero(weight>.5)
                if len(selected): distal.append((primitive,selected))
        groups[finger]=(all_points,distal)
    contacts=[]; clip=body.animations[0]
    for phase in (0,.5,1):
        world=body.pose(clip,clip['start']+phase*(clip['end']-clip['start'])); transform=world[hand]@local
        inverse=np.linalg.inv(transform); scale=float(np.linalg.norm(transform[:3,0]))
        for finger,(all_points,distal) in groups.items():
            if not all_points: raise ValueError('Expected right-hand weighted skin')
            points=np.concatenate([fc.points(body,p,world,indices) for p,indices in all_points])
            points=(np.column_stack([points,np.ones(len(points))])@inverse.T)[:,:3]
            distance=ga.distances(points,grip)*scale; inside=sg.closed_inside(points,grip)
            row=dict(phase=phase,finger=finger,vertices=len(points),insideVertices=int(inside.sum()),minimumDistance=float(distance.min()),
                     medianDistance=float(np.median(distance)),verticesWithin002=int((distance<=.002).sum()),
                     nearestSurfacePenetrationDepth=float(distance[inside].max()) if inside.any() else 0)
            if distal:
                points=np.concatenate([fc.points(body,p,world,indices) for p,indices in distal])
                points=(np.column_stack([points,np.ones(len(points))])@inverse.T)[:,:3]
                dd=ga.distances(points,grip)*scale; ii=sg.closed_inside(points,grip)
                row['distal']=dict(vertices=len(points),insideVertices=int(ii.sum()),minimumDistance=float(dd.min()),verticesWithin002=int((dd<=.002).sum()))
            contacts.append(row)
            print(json.dumps({'contactPhase':phase,'finger':finger,'inside':row['insideVertices'],'minimum':row['minimumDistance']}),flush=True)
    hp.write_json(output/'CONTACT-AUDIT.json',dict(swordSha256=sha(candidate),bodySha256=sha(body_path),socketSha256=sha(socket_path),
                  gripTriangles=len(grip),samples=contacts,actorFingerPosesModified=False,
                  method='Exact nearest triangle distances and generalized winding containment against real closed cylindrical core + helical wrap; no ellipsoid-only approximation',
                  limits=['Only sampled weighted skin vertices; triangle/triangle intersections are not proved absent.','Overlapping core/wrap surfaces can understate union penetration depth; inside counts, not distances alone, determine a reported penetration.','Guard/pommel/blade and thumb/finger self-intersections remain separate gates.','Physical units, anatomy, grasp pressure and gameplay are not approved.'],
                  graspApproved=False))
    if fit_socket:
        body.load_materials();sword.load_materials()
        mixed=SimpleNamespace(materials={**body.materials,**{i+len(body.materials):m for i,m in sword.materials.items()}})
        rendered=[]
        for clip_index,phase in [(0,0),(2,.5),(8,1)]:
            clip=body.animations[clip_index]; world=body.pose(clip,clip['start']+phase*(clip['end']-clip['start'])); matrix=world[hand]@local
            geometry=body.geometry(world)+sc.transformed_geometry(sword,matrix,len(body.materials)); center=(matrix@np.array([0,0,0,1]))[:3]
            for label,yaw,ppm,pivot,pivot_center in [('full-front',0,510,[384,720],np.zeros(3)),('full-side',90,510,[384,720],np.zeros(3)),('hand-front',0,3000,[384,384],center),('hand-side',90,3000,[384,384],center)]:
                picture,stats=hp.raster(mixed,geometry,yaw,(768,768),pivot,pivot_center,ppm,15,1,False)
                name=f'{clip_index}-{phase:g}-{label}.png';picture.save(output/name);rendered.append(dict(clip=clip['name'],phase=phase,view=label,file=name,stats=stats))
        hp.write_json(output/'SOCKET-RENDER-REVIEW.json',dict(socketSha256=sha(socket_path),swordSha256=sha(candidate),views=rendered))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--body',type=Path,default=WORKSPACE/'diagnostics/universal-modder-20261012/warrior-clips-bundle-r2/warrior-clips-candidate.glb')
    parser.add_argument('--samples',type=int,default=129)
    parser.add_argument('--candidate',type=Path,help='Audit existing GLB only; do not regenerate it')
    parser.add_argument('--socket',type=Path,help='Exact existing socket receipt for audit-only mode')
    parser.add_argument('--fit-socket',action='store_true',help='Fit a new bounded translation against current skin; preserve GLB, pose, orientation and scale')
    args=parser.parse_args()
    if args.output.exists(): parser.error('Use a fresh output directory; preserve prior candidates')
    if not 17<=args.samples<=2049: parser.error('Use a bounded pose sample count')
    if bool(args.candidate)!=bool(args.socket): parser.error('Existing-candidate audit requires both --candidate and --socket')
    args.output.mkdir(parents=True)
    concept=REPO/'assets/concept-art/items/r1/short-sword-r1.png'
    native=WORKSPACE/'diagnostics/item-references-20261008/rendered/icurs-064-short-sword-6x.png'
    reader,reader_info=hp.load_reader(REPO)
    if args.candidate:
        audit_existing(reader,args.candidate,args.socket,args.output,args.samples,args.fit_socket)
        return
    provenance={'productionId':'item.design.base.short-sword','nativeItemId':1,'nativeItemSymbol':'IDI_WARRIOR','nativeCursorId':64,
                'concept':{'path':str(concept),'sha256':sha(concept),'use':'Visual silhouette/material reference only; no pixels embedded'},
                'nativePrivateReference':{'path':str(native),'sha256':sha(native),'use':'Read-only silhouette/proportion check; no pixels embedded'},
                'authorship':'Original local procedural geometry for Diablo3D; not extracted native mesh or a relabeled Great Sword',
                'dimensions':'Nominal source metres, inferred: blade .501, grip .154, overall about .731, guard width .142; physical game fit not calibrated',
                'materialScope':'Six real GLB PBR factor materials, COLOR_0 and seam UVs; no textures, images, billboard, alpha card or external URI',
                'inferred':['Absolute scale, thickness, fine wear, hidden tang, backside details, new palm anchor and game-world fit'],
                'runtimeSelected':False,'approved':False,'paidGeneration':False}
    path=args.output/'short-sword-original-candidate-r1.glb'; author().save(path,provenance)
    model=hp.Model(path,reader); geometry=audit(model); hp.write_json(args.output/'GEOMETRY-AUDIT.json',geometry)
    if not all(geometry['checks'].values()): raise ValueError('Candidate geometric checks failed; failed bytes and audit preserved')
    rendered=views(model,args.output)
    socket=attach(reader,model,args.body,args.output,args.samples)
    hp.write_json(args.output/'DELIVERY-SWORD-R1.json',dict(candidate=str(path),sha256=sha(path),provenance=provenance,
                  geometry=geometry,views=rendered,socket=socket,dependencies=[reader_info,{'path':str(Path(sc.__file__)),'sha256':sha(Path(sc.__file__))},{'path':str(Path(hp.__file__)),'sha256':sha(Path(hp.__file__))},{'path':str(Path(__file__)),'sha256':sha(Path(__file__))}],
                  status='private original candidate; requires visual/contact/native export review',runtimeSelected=False,approved=False,
                  preview='Actual exported geometry; local CPU GGX material renders and established diffuse GLB body/socket raster. No raster reference was edited or pasted.'))
    print(json.dumps({'candidate':str(path),'sha256':sha(path),'checks':geometry['checks'],'triangles':geometry['triangles'],'views':len(rendered),'socketViews':len(socket['views'])}),flush=True)


if __name__=='__main__': main()
