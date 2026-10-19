"""Original offline buckler geometry and private attachment evidence.

Uses the existing own concept as a style reference only. No source raster is
edited, copied onto geometry, or used as a billboard. No model, registry,
runtime, build, gameplay, or approval is modified. Geometry dimensions and
rear construction are explicitly inferred. The preview uses the established
CPU GLB rasterizer's approximate diffuse lighting, not a PBR acceptance gate.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
from types import SimpleNamespace

import numpy as np
from PIL import Image
from scipy.spatial.transform import Rotation

REPO = Path(__file__).resolve().parents[2]
WORKSPACE = REPO.parent
sys.path.insert(0, str(REPO / 'docs/own-art-pack/heroes-local-r1/pipeline'))
import hero_pipeline as hp


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    hp.write_json(path, value)


def inventory(reader, output):
    """Inspect primary local GLBs without exposing metadata URIs or URLs."""
    roots = [Path('C:/Users/dougl/Downloads'), WORKSPACE / 'models',
             WORKSPACE / 'local-assets/own-art', REPO / 'assets/d3d-models']
    files = []
    for root in roots:
        if not root.exists():
            continue
        for base, dirs, names in os.walk(root):
            # Downloads is an explicit, bounded top-level supply directory.
            if root.name == 'Downloads':
                dirs[:] = []
            for name in names:
                if name.lower().endswith('.glb'):
                    files.append(Path(base) / name)
    records = []
    for path in sorted(set(files)):
        doc, binary, _ = reader.read_glb(path)
        read = reader.Reader(path, doc, binary)
        positions = [read.accessor(p['attributes']['POSITION']) for m in doc.get('meshes', [])
                     for p in m.get('primitives', []) if 'POSITION' in p.get('attributes', {})]
        triangles = sum(len(reader.triangles(read.accessor(p['indices']).reshape(-1), p.get('mode', 4)))
                        for m in doc.get('meshes', []) for p in m.get('primitives', []) if 'indices' in p)
        records.append({'path': str(path), 'bytes': path.stat().st_size, 'sha256': sha(path),
                        'binaryChunkSha256': hashlib.sha256(binary).hexdigest(),
                        'nodes': len(doc.get('nodes', [])), 'meshes': len(doc.get('meshes', [])),
                        'triangles': triangles, 'clips': len(doc.get('animations', [])),
                        'meshPositionBounds': hp.bbox(np.concatenate(positions)) if positions else None,
                        'generator': doc.get('asset', {}).get('generator', '')})
    write_json(output / 'LOCAL-GLB-INVENTORY.json', {
        'roots': [str(r) for r in roots], 'records': records,
        'inspectionScope': 'Complete GLB documents and mesh POSITION/index data in listed primary supply roots; not every diagnostic derivative or arbitrary external drive.',
        'externalMetadataOrSignedUrlsPrinted': False,
        'candidateDecision': 'No supplied standalone buckler/shield master identified. Generic supplied artcraft and hut have identical BIN chunks (GLB document bytes differ); fantasy warrior has no standalone buckler mesh (render review saved). Existing architectural/prop/actor models are preserved.',
        'noSelectionByNewestFilename': True})
    return records


def lathe(profile, segments=128, center=(0, 0, 0), flip=False):
    """Revolve an ordered front-to-back closed meridian with analytic seam UVs."""
    profile = np.asarray(profile, float)
    distance = np.r_[0, np.cumsum(np.linalg.norm(np.diff(profile, axis=0), axis=1))]
    distance /= max(distance[-1], 1e-12)
    tangent = np.empty_like(profile)
    tangent[0], tangent[-1] = profile[1] - profile[0], profile[-1] - profile[-2]
    tangent[1:-1] = profile[2:] - profile[:-2]
    tangent /= np.maximum(np.linalg.norm(tangent, axis=1, keepdims=True), 1e-12)
    vertices, normals, uvs, rings = [], [], [], []
    for k, (radius, z) in enumerate(profile):
        ring = []
        if radius < 1e-10:
            ring = [len(vertices)]
            vertices.append([0, 0, z]); normals.append([0, 0, 1 if k == 0 else -1])
            uvs.append([.5, distance[k]])
        else:
            for j in range(segments + 1):
                angle = 2 * math.pi * j / segments
                c, s = math.cos(angle), math.sin(angle)
                ring.append(len(vertices)); vertices.append([radius*c, radius*s, z])
                normals.append([-tangent[k, 1]*c, -tangent[k, 1]*s, tangent[k, 0]])
                uvs.append([j / segments, distance[k]])
        rings.append(ring)
    faces = []
    for a, b in zip(rings[:-1], rings[1:]):
        for j in range(segments):
            if len(a) == 1:
                faces.append([a[0], b[j], b[j+1]])
            elif len(b) == 1:
                faces.append([a[j], b[0], a[j+1]])
            else:
                faces.extend([[a[j], b[j], b[j+1]], [a[j], b[j+1], a[j+1]]])
    vertices, normals, faces = np.array(vertices), np.array(normals), np.array(faces, int)
    if flip:
        vertices[:, 2] *= -1; normals[:, 2] *= -1; faces = faces[:, ::-1]
    vertices += np.array(center)
    return vertices, normals, np.array(uvs), faces


def ring(major, minor, z, center=(0, 0, 0), segments=96, sides=8):
    vertices, normals, uvs, faces = [], [], [], []
    for i in range(segments + 1):
        angle = 2 * math.pi * i / segments
        for j in range(sides + 1):
            t = 2 * math.pi * j / sides
            n = np.array([math.cos(angle)*math.cos(t), math.sin(angle)*math.cos(t), math.sin(t)])
            v = np.array([major*math.cos(angle), major*math.sin(angle), z]) + n*minor + center
            vertices.append(v); normals.append(n); uvs.append([i/segments, j/sides])
    for i in range(segments):
        for j in range(sides):
            a = i*(sides+1)+j; b = (i+1)*(sides+1)+j
            faces.extend([[a,b,b+1], [a,b+1,a+1]])
    return np.array(vertices), np.array(normals), np.array(uvs), np.array(faces, int)


def box(center, extent):
    c, e = np.array(center), np.array(extent)/2
    vertices, normals, uvs, faces = [], [], [], []
    for axis in range(3):
        for sign in (-1, 1):
            n = np.zeros(3); n[axis] = sign
            other = [v for v in range(3) if v != axis]
            corners = []
            for a, b in ((-1,-1),(1,-1),(1,1),(-1,1)):
                v = c.copy(); v[axis] += e[axis]*sign
                v[other[0]] += e[other[0]]*a; v[other[1]] += e[other[1]]*b
                corners.append(v)
            if np.cross(corners[1]-corners[0], corners[2]-corners[0]) @ n < 0:
                corners = corners[::-1]
            offset = len(vertices); vertices.extend(corners); normals.extend([n]*4)
            uvs.extend([[0,0],[1,0],[1,1],[0,1]])
            faces.extend([[offset,offset+1,offset+2],[offset,offset+2,offset+3]])
    return np.array(vertices), np.array(normals), np.array(uvs), np.array(faces, int)


def grip():
    """Closed raised steel handle, shoulder bends and capsule-like section."""
    path = np.array([[-.100,0,-.016],[-.091,0,-.022],[-.080,0,-.048],
                     [-.065,0,-.070],[-.050,0,-.073],[.050,0,-.073],
                     [.065,0,-.070],[.080,0,-.048],[.091,0,-.022],[.100,0,-.016]])
    tangents = np.empty_like(path)
    tangents[0], tangents[-1] = path[1]-path[0], path[-1]-path[-2]
    tangents[1:-1] = path[2:]-path[:-2]
    tangents /= np.linalg.norm(tangents, axis=1, keepdims=True)
    vertices, normals, uvs, faces = [], [], [], []
    sides = 16
    for i, point in enumerate(path):
        a = np.array([0,1,0]); b = np.cross(tangents[i], a)
        for j in range(sides+1):
            angle = 2*math.pi*j/sides
            radial = a*math.cos(angle)*.017 + b*math.sin(angle)*.009
            normal = a*math.cos(angle)/.017 + b*math.sin(angle)/.009
            normal /= np.linalg.norm(normal)
            vertices.append(point+radial); normals.append(normal); uvs.append([i/(len(path)-1),j/sides])
    for i in range(len(path)-1):
        for j in range(sides):
            a=i*(sides+1)+j; b=(i+1)*(sides+1)+j
            faces.extend([[a,a+1,b+1],[a,b+1,b]])
    for i, sign in ((0,-1),(len(path)-1,1)):
        center = len(vertices); vertices.append(path[i]); normals.append(tangents[i]*sign); uvs.append([.5,.5])
        for j in range(sides):
            face = [center, i*(sides+1)+j, i*(sides+1)+j+1]
            if np.cross(np.array(vertices[face[1]])-path[i],np.array(vertices[face[2]])-path[i]) @ (tangents[i]*sign) < 0:
                face = face[::-1]
            faces.append(face)
    return np.array(vertices), np.array(normals), np.array(uvs), np.array(faces,int)


class Asset:
    def __init__(self):
        self.parts = []

    def add(self, name, geometry, material):
        p,n,uv,f = geometry
        # Only low-frequency subtle color variation. High-frequency vertex
        # noise on radial triangulation creates false sunburst streaks.
        # No raster master or reference is sampled/edited/generated here.
        varied = .96 + .035*np.sin(p[:,0]*19+p[:,1]*27+p[:,2]*11)**2
        colors = np.column_stack([np.repeat(varied[:,None],3,axis=1),np.ones(len(p))])
        self.parts.append({'name':name,'positions':p,'normals':n,'uv':uv,'faces':f,'colors':colors,'material':material})

    def save(self, path, provenance):
        doc = {'asset':{'version':'2.0','generator':'Diablo3D original offline starter_shield_candidate.py r1'},
               'scene':0,'scenes':[{'nodes':[]}],'nodes':[],'meshes':[], 'materials':[
                   {'name':'blackened-forged-iron','pbrMetallicRoughness':{'baseColorFactor':[.072,.070,.065,1],'metallicFactor':.88,'roughnessFactor':.69}},
                   {'name':'worn-steel-rim-and-collars','pbrMetallicRoughness':{'baseColorFactor':[.16,.155,.145,1],'metallicFactor':.94,'roughnessFactor':.49}},
                   {'name':'muted-steel-rivets','pbrMetallicRoughness':{'baseColorFactor':[.195,.19,.175,1],'metallicFactor':.95,'roughnessFactor':.43}},
                   {'name':'dark-forged-grip','pbrMetallicRoughness':{'baseColorFactor':[.058,.054,.048,1],'metallicFactor':.83,'roughnessFactor':.76}}],
               'buffers':[{'byteLength':0}],'bufferViews':[],'accessors':[], 'extras':provenance}
        binary = bytearray()
        def accessor(data, kind, component=5126, target=34962):
            data = np.asarray(data,dtype='<f4' if component==5126 else '<u4')
            while len(binary)%4: binary.append(0)
            start = len(binary); binary.extend(data.tobytes())
            view = len(doc['bufferViews']); doc['bufferViews'].append({'buffer':0,'byteOffset':start,'byteLength':data.nbytes,'target':target})
            a={'bufferView':view,'componentType':component,'count':len(data),'type':kind}
            if kind=='VEC3':a.update({'min':data.min(axis=0).tolist(),'max':data.max(axis=0).tolist()})
            doc['accessors'].append(a); return len(doc['accessors'])-1
        for part in self.parts:
            attrs={k:accessor(part[v],t) for k,v,t in [('POSITION','positions','VEC3'),('NORMAL','normals','VEC3'),('TEXCOORD_0','uv','VEC2'),('COLOR_0','colors','VEC4')]}
            indices=accessor(part['faces'].reshape(-1),'SCALAR',5125,34963)
            mesh=len(doc['meshes']); doc['meshes'].append({'name':part['name'],'primitives':[{'attributes':attrs,'indices':indices,'material':part['material'],'mode':4}]})
            doc['scenes'][0]['nodes'].append(len(doc['nodes'])); doc['nodes'].append({'name':part['name'],'mesh':mesh})
        doc['buffers'][0]['byteLength']=len(binary)
        encoded=json.dumps(doc,separators=(',',':'),allow_nan=False).encode(); encoded+=b' '*((-len(encoded))%4)
        binary+=b'\0'*((-len(binary))%4)
        total=12+8+len(encoded)+8+len(binary)
        path.write_bytes(struct.pack('<4sII',b'glTF',2,total)+struct.pack('<II',len(encoded),0x4E4F534A)+encoded+struct.pack('<II',len(binary),0x004E4942)+binary)


def author_buckler():
    asset=Asset()
    # Round all-metal body. Boss and face are one continuous front/back shell.
    # Recessed concentric channels are actual meridian geometry, not pixels.
    profile=[(0,.083),(.012,.0825),(.025,.080),(.040,.075),(.055,.067),(.070,.057),(.083,.044),(.094,.027),
             (.099,.017),(.102,.014),(.106,.013),(.109,.017),(.113,.018),(.116,.015),(.119,.012),(.127,.0115),
             (.143,.0105),(.154,.010),(.155,.0084),(.158,.0084),(.159,.010),(.182,.009),(.208,.008),
             (.223,.0075),(.227,.006),(.229,.008),(.233,.009),(.235,.006),(.236,.000),(.235,-.008),
             (.233,-.010),(.226,-.010),(.223,-.0085),(.204,-.008),(.174,-.009),(.140,-.010),(.108,-.011),
             (.070,-.012),(.035,-.0125),(0,-.013)]
    asset.add('continuous-front-boss-plate-and-real-back',lathe(profile),0)
    asset.add('real-front-and-back-metal-rim-band',lathe([
        (.222,.006),(.223,.011),(.227,.013),(.232,.012),(.236,.008),
        (.238,.001),(.237,-.009),(.234,-.013),(.225,-.013),(.223,-.010),(.222,.006)]),1)
    asset.add('rounded-narrow-steel-rim',ring(.231,.0035,.001),1)
    asset.add('central-boss-steel-collar',ring(.1115,.002,.016),1)
    dome=[(0,.011),(.002,.0107),(.004,.0095),(.006,.0076),(.008,.0048),(.009,.001),(.009,-.001),(0,-.001)]
    for i in range(4):
        angle=math.pi/2+i*math.pi/2; c=(.192*math.cos(angle),.192*math.sin(angle),.0084)
        asset.add('front-cardinal-rivet-'+str(i),lathe(dome,32,c),2)
    for i in range(4):
        angle=math.pi/4+i*math.pi/2; c=np.array([.179*math.cos(angle),.179*math.sin(angle),.010])
        boss=np.array(dome); boss[:,0]*=1.55; boss[:,1]*=1.05
        asset.add('front-diagonal-dark-stud-'+str(i),lathe(boss,40,c),0)
        asset.add('front-diagonal-incised-collar-'+str(i),ring(.025,.0016,.012,center=(c[0],c[1],0),segments=64),1)
    for i in range(4):
        angle=math.pi/4+i*math.pi/2; c=(.184*math.cos(angle),.184*math.sin(angle),-.009)
        back=np.array(dome); back*=.72
        asset.add('back-plain-rivet-'+str(i),lathe(back,28,c,flip=True),0)
    for side in (-1,1):
        asset.add('back-grip-mount-'+str(side),box((side*.102,0,-.017),(.028,.062,.009)),3)
        for y in (-.020,.020):
            fast=np.array(dome)*.60
            asset.add('back-grip-mount-fastener-'+str(side)+'-'+str(y),lathe(fast,24,(side*.102,y,-.022),flip=True),2)
    asset.add('back-raised-transverse-grip',grip(),3)
    return asset


def mesh_audit(model):
    parts=[]
    for p in model.primitives:
        v,f=p['positions'],p['triangles']
        # Weld only for measurement. Preserve all GLB seam/normal UV vertices.
        _, inverse=np.unique(np.round(v,8),axis=0,return_inverse=True)
        edges=np.sort(np.concatenate([inverse[f[:,[0,1]]],inverse[f[:,[1,2]]],inverse[f[:,[2,0]]]]),axis=1)
        _, incidence=np.unique(edges,axis=0,return_counts=True)
        area=np.linalg.norm(np.cross(v[f[:,1]]-v[f[:,0]],v[f[:,2]]-v[f[:,0]]),axis=1)/2
        volume=float(np.einsum('ij,ij->i',v[f[:,0]],np.cross(v[f[:,1]],v[f[:,2]])).sum()/6)
        normals=p['normals']; normal_length=np.linalg.norm(normals,axis=1)
        parts.append({'name':model.nodes[p['node']]['name'],'triangles':len(f),'closedByWeldedEdgeIncidence':bool(np.all(incidence==2)),
                      'boundaryOrNonmanifoldEdges':int(np.sum(incidence!=2)),'minimumTriangleArea':float(area.min()),
                      'signedVolume':volume,'normalLengthError':float(abs(normal_length-1).max()),
                      'finiteUVsWithinUnitRange':bool(np.isfinite(p['uv']).all() and p['uv'].min()>=0 and p['uv'].max()<=1)})
    xyz=np.concatenate([g['vertices'] for g in model.geometry(model.rest_world)])
    checks={'allPartsClosed':all(p['closedByWeldedEdgeIncidence'] for p in parts),
            'positiveTriangleArea':all(p['minimumTriangleArea']>1e-12 for p in parts),
            'outwardWindingPositiveVolume':all(p['signedVolume']>1e-12 for p in parts),
            'unitNormals':all(p['normalLengthError']<1e-5 for p in parts),
            'finiteUnitUVs':all(p['finiteUVsWithinUnitRange'] for p in parts),
            'noTexturesOrExternalURIs':not model.doc.get('images') and not any('uri' in b for b in model.doc.get('buffers',[])),
            'actualFrontAndBackDepth':float(np.ptp(xyz[:,2]))>.12,
            'uniformNodeTransforms':all('scale' not in n and 'matrix' not in n for n in model.nodes)}
    return {'parts':parts,'checks':checks,'bounds':hp.bbox(xyz),'vertices':len(xyz),'triangles':sum(p['triangles'] for p in parts),
            'front':'positive Z','back':'negative Z','gripAnchor':[0,0,-.073],
            'dimensionsUnits':'authored nominal meters; not yet a calibrated game-world physical scale',
            'scope':'Closed geometric components may deliberately overlap at welded-looking fittings; per-part watertightness does not establish a boolean union or hand collision safety.'}


def studio_raster(model, yaw, size=(768,768), ppm=1300, supersample=2):
    """CPU GGX material review of actual exported geometry and GLB materials.

    Direct-light BRDF and broad neutral studio reflections. No ray tracing,
    shadows, new screenshot-only surface details or texture modification.
    """
    width,height=[n*supersample for n in size]; ppm*=supersample
    theta=math.radians(yaw)
    rotation=np.array([[math.cos(theta),0,math.sin(theta)],[0,1,0],[-math.sin(theta),0,math.cos(theta)]])
    eye=np.array([0.,0.,1.]); pivot=np.array([width/2,height/2])
    rgba=np.zeros((height,width,4),np.uint8); depth=np.full((height,width),-np.inf)
    lights=[(np.array([-.45,.65,.62]),4.0),(np.array([.75,.1,.63]),2.1),(np.array([-.6,-.2,.35]),.65)]
    lights=[(l/np.linalg.norm(l),i) for l,i in lights]
    drawn=culled=0
    for primitive in model.geometry(model.rest_world):
        points=primitive['vertices']@rotation.T; normals=primitive['world_normals']@rotation.T
        screen=np.column_stack([pivot[0]+ppm*points[:,0],pivot[1]-ppm*points[:,1],points[:,2]])
        material=model.materials[primitive['material']]; pbr=material['pbrMetallicRoughness']
        factor=np.array(pbr['baseColorFactor'][:3]); metal=pbr['metallicFactor']; rough=pbr['roughnessFactor']
        for face in primitive['triangles']:
            physical=points[face]; face_normal=np.cross(physical[1]-physical[0],physical[2]-physical[0])
            if face_normal@eye<=0:culled+=1;continue
            p=screen[face]; left=max(0,int(np.floor(p[:,0].min()))); right=min(width-1,int(np.ceil(p[:,0].max())))
            top=max(0,int(np.floor(p[:,1].min()))); bottom=min(height-1,int(np.ceil(p[:,1].max())))
            if left>right or top>bottom:continue
            denominator=(p[1,1]-p[2,1])*(p[0,0]-p[2,0])+(p[2,0]-p[1,0])*(p[0,1]-p[2,1])
            if abs(denominator)<1e-8:continue
            yy,xx=np.mgrid[top:bottom+1,left:right+1]
            w0=((p[1,1]-p[2,1])*(xx+.5-p[2,0])+(p[2,0]-p[1,0])*(yy+.5-p[2,1]))/denominator
            w1=((p[2,1]-p[0,1])*(xx+.5-p[2,0])+(p[0,0]-p[2,0])*(yy+.5-p[2,1]))/denominator
            w2=1-w0-w1; z=w0*p[0,2]+w1*p[1,2]+w2*p[2,2]
            active=(w0>=-1e-6)&(w1>=-1e-6)&(w2>=-1e-6)&(z>depth[top:bottom+1,left:right+1])
            if not active.any():continue
            weights=np.column_stack([w0[active],w1[active],w2[active]])
            n=weights@normals[face]; n/=np.maximum(np.linalg.norm(n,axis=1,keepdims=True),1e-12)
            nv=np.maximum(n[:,2],1e-5); base=factor*(weights@primitive['colors'][face])[:,:3]
            f0=.04*(1-metal)+base*metal
            reflected=2*n*nv[:,None]-eye; panel=np.zeros(len(n))
            for l,intensity in lights[:2]:
                panel+=intensity*.065*np.maximum(reflected@l,0)**(3+5*(1-rough))
            color=base*.15+(f0+(1-f0)*(1-nv[:,None])**5)*(panel[:,None]+.014)
            alpha=rough*rough; a2=alpha*alpha; k=(rough+1)**2/8
            for l,intensity in lights:
                nl=np.maximum(n@l,0); h=eye+l; h/=np.linalg.norm(h)
                nh=np.maximum(n@h,0); vh=max(float(eye@h),0)
                d=a2/(math.pi*(nh*nh*(a2-1)+1)**2)
                g=(nv/(nv*(1-k)+k))*(nl/(nl*(1-k)+k+1e-12))
                fresnel=f0+(1-f0)*(1-vh)**5
                specular=d[:,None]*g[:,None]*fresnel/(4*nv*nl+1e-8)[:,None]
                diffuse=(1-fresnel)*(1-metal)*base/math.pi
                color+=(diffuse+specular)*(nl*intensity)[:,None]
            color=np.maximum(color,0); color=color/(1+color)
            rgb=np.clip(np.rint(hp.linear_to_srgb(color)*255),0,255).astype(np.uint8)
            row,col=np.nonzero(active); row+=top;col+=left
            rgba[row,col,:3]=rgb;rgba[row,col,3]=255;depth[row,col]=z[active];drawn+=1
    image=Image.fromarray(rgba)
    if supersample!=1:image=image.resize(size,Image.Resampling.LANCZOS)
    result=np.asarray(image); covered=result[:,:,3]>0; ys,xs=np.nonzero(covered)
    if not len(xs):raise ValueError('Studio renderer produced no coverage')
    return image,{'drawnTriangles':drawn,'culledTriangles':culled,'coveredPixels':int(covered.sum()),
                  'alphaBoundingBox':[int(xs.min()),int(ys.min()),int(xs.max())+1,int(ys.max())+1],
                  'touchesFrameBoundary':bool(covered[0].any() or covered[-1].any() or covered[:,0].any() or covered[:,-1].any()),
                  'rgbaPixelsSha256':hp.sha(result.tobytes()),'shader':'CPU GGX metallic/roughness plus neutral broad studio environment; no shadows'}


def render_views(model, output):
    model.load_materials(); records=[]
    for label,yaw in [('front',0),('three-quarter',45),('side',90),('back',180),('back-three-quarter',225),('opposite-side',270)]:
        picture,stats=hp.raster(model,model.geometry(model.rest_world),yaw,(768,768),[384,384],np.zeros(3),1300,0,2,False)
        picture.save(output/(label+'.png')); records.append({'view':label,'yaw':yaw,'stats':stats,'path':label+'.png'})
        picture,stats=studio_raster(model,yaw)
        picture.save(output/(label+'-metallic-review.png')); records.append({'view':label+'-metallic-review','yaw':yaw,'stats':stats,'path':label+'-metallic-review.png'})
    return records


def transformed_geometry(model, matrix, shift):
    result=[]
    for p in model.geometry(model.rest_world):
        item=dict(p); item['vertices']=(np.column_stack([p['vertices'],np.ones(len(p['vertices']))])@matrix.T)[:,:3]
        n=p['world_normals']@np.linalg.inv(matrix[:3,:3]); item['world_normals']=n/np.linalg.norm(n,axis=1,keepdims=True)
        item['material']+=shift; result.append(item)
    return result


def attach_probe(reader, shield, source, output):
    body=hp.Model(source,reader); body.load_materials(); names={n.get('name'):i for i,n in enumerate(body.nodes)}
    hand=names['mixamorig:LeftHand']; clip=body.animations[0]; first=body.pose(clip,clip['start'])
    wrist=first[hand][:3,3]; index=first[names['mixamorig:LeftHandIndex1']][:3,3]; pinky=first[names['mixamorig:LeftHandPinky1']][:3,3]
    finger=first[names['mixamorig:LeftHandMiddle1']][:3,3]-wrist; finger/=np.linalg.norm(finger)
    across=index-pinky; across-=finger*(across@finger); across/=np.linalg.norm(across)
    normal=np.cross(across,finger); normal/=np.linalg.norm(normal)
    orientation=np.column_stack([across,finger,normal])
    xyz=np.concatenate([g['vertices'] for g in body.geometry(first)])
    # Same physical inference everywhere: no axis-wise scaling to match bounds.
    scale=float(np.ptp(xyz[:,1])/1.75)
    anchor=wrist+(first[names['mixamorig:LeftHandMiddle1']][:3,3]-wrist)*.58+normal*.004
    world=np.eye(4); world[:3,:3]=orientation*scale; world[:3,3]=anchor-world[:3,:3]@np.array([0,0,-.073])
    socket=np.linalg.inv(first[hand])@world
    uniform=float(np.linalg.norm(socket[:3,0])); rotation=Rotation.from_matrix(socket[:3,:3]/uniform).as_quat()
    mixed=SimpleNamespace(materials={**body.materials,**{i+len(body.materials):m for i,m in shield.materials.items()}})
    views=[]
    for phase in (0,.5,1):
        w=body.pose(clip,clip['start']+(clip['end']-clip['start'])*phase); matrix=w[hand]@socket
        g=body.geometry(w)+transformed_geometry(shield,matrix,len(body.materials))
        for label,yaw,ppm,pivot,center in [('full',0,550,[384,700],np.zeros(3)),('hand-front',0,2400,[384,384],anchor),('hand-side',90,2400,[384,384],anchor),('hand-back',180,2400,[384,384],anchor)]:
            picture,stats=hp.raster(mixed,g,yaw,(768,768),pivot,center,ppm,15,1,False)
            file=f'{label}-{phase:g}.png'; picture.save(output/file); views.append({'phase':phase,'view':label,'file':file,'stats':stats})
    record={'body':str(source),'bodySha256':sha(source),'shieldSha256':sha(shield.path),'bone':'mixamorig:LeftHand',
            'sourceGrip':[0,0,-.073], 'localTransform':{'translation':socket[:3,3].tolist(),'rotationXYZW':rotation.tolist(),'uniformScale':uniform},
            'uniformScaleMethod':'Guarded body height / assumed 1.75m adult height; absolute physical scale is inferred.',
            'initialPalmBasis':{'across':across.tolist(),'finger':finger.tolist(),'outwardNormal':normal.tolist(),'gripWorldAnchor':anchor.tolist()},
            'bodySourceUnchanged':sha(source),'fingerRotationsEdited':False,'views':views,
            'limits':['Open source left-hand fingers are retained and do not grip the handle.','Bone-following placement is a reviewable socket candidate only.',
                      'No signed-distance collision, finger contact or anatomy gate passes here.','No shield-equipped cast/block/death or complete equipment set is admitted.'],
            'gameplaySelected':False,'artisticApproval':False}
    write_json(output/'SOCKET-CANDIDATE.json',record)
    return record


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--guard',type=Path); args=p.parse_args()
    if args.output.exists():p.error('Use a new private diagnostic output directory')
    args.output.mkdir(parents=True); reader,dependency=hp.load_reader(REPO)
    inventory_records=inventory(reader,args.output)
    # Inspect generic supplied warrior rather than concluding from its filename.
    generic=Path('C:/Users/dougl/Downloads/fantasy+warrior+3d+model.glb')
    if generic.exists():
        gm=hp.Model(generic,reader);gm.load_materials();gg=gm.geometry(gm.rest_world)
        xx=np.concatenate([g['vertices'] for g in gg]);center=(xx.min(axis=0)+xx.max(axis=0))/2
        ppm=600/max(np.ptp(xx,axis=0))
        for label,yaw in [('supplied-warrior-front',0),('supplied-warrior-back',180)]:
            picture,stats=hp.raster(gm,gg,yaw,(768,768),[384,384],center,ppm,0,1,False);picture.save(args.output/(label+'.png'))
    concept_path=REPO/'assets/concept-art/items/r1/buckler-r1.png'
    catalog=REPO/'docs/items-study/concept-art.json'; doc=json.loads(catalog.read_text(encoding='utf-8-sig'))
    # The file's schema is a top-level manifest with a concepts list.
    entries=doc if isinstance(doc,list) else next((v for v in doc.values() if isinstance(v,list) and any(isinstance(e,dict) and e.get('conceptId')=='item.design.base.buckler.concept.r1' for e in v)),[])
    reference=next(e for e in entries if e.get('conceptId')=='item.design.base.buckler.concept.r1')
    if sha(concept_path)!=reference['sha256']:raise ValueError('Concept source hash does not match its audit')
    provenance={'createdAtUtc':datetime.now(timezone.utc).isoformat(),'productionId':'item.design.base.buckler',
        'runtimeItem':'IDI_WARRSHLD','nativeItemIndex':2,'nativeCursor':'BUCKLER','nativeCursorId':83,
        'styleReference':{'path':str(concept_path),'sha256':sha(concept_path),'conceptId':reference['conceptId'],'revision':reference['revision'],
                          'status':reference['status'],'visualApproval':reference['visualApproval'],'modelStatusBeforeThisPrivateCandidate':reference['modelStatus']},
        'conceptAudit':{'path':str(catalog),'sha256':sha(catalog),'referenceNativeSpriteSha256':reference.get('referenceNativeSpriteSha256')},
        'authorship':'Original deterministic 3D meridian/sweep/fastener geometry authored locally for this candidate. No third-party mesh reused and no paid service invoked.',
        'sourceRastersEdited':False,'sourcePixelsOnMesh':False,'generatedBitmapTexture':False,
        'inferred':['Absolute scale, thickness, back shell, grip and attachment construction.','Detailed wear is a limited vertex-color/material approximation; sculpted finish and PBR review remain open.'],
        'sourceModelOrConceptApproval':False,'artisticApproval':False,'gameplaySelected':False,'registryChanged':False,
        'existingSuppliedGLBsInspected':len(inventory_records),'reader':dependency,'toolSha256':sha(Path(__file__))}
    asset=author_buckler();path=args.output/'buckler-original-candidate-r1.glb';asset.save(path,provenance)
    model=hp.Model(path,reader);audit=mesh_audit(model);write_json(args.output/'GEOMETRY-AUDIT.json',audit)
    views=render_views(model,args.output);attachment=attach_probe(reader,model,args.guard,args.output) if args.guard else None
    receipt={**provenance,'candidate':{'path':str(path),'sha256':sha(path),'bytes':path.stat().st_size},'geometry':audit,
        'views':views,'attachment':attachment,'status':'private-original-candidate-needs-style-finish-and-left-hand-grip-review',
        'checksPassed':all(audit['checks'].values()),'rendering':'Six established diffuse CPU views plus six local CPU GGX metallic/roughness studio views of the same GLB. Fixed scale, exposure, lighting and camera. The studio renderer has no shadows/ray tracing; runtime remains a separate gate.',
        'limits':['No new concept, native inventory icon or runtime package generated.','No body, rig, source GLB, image master, bundle or installed selection changed.',
                  'Positive geometry checks do not establish artistic acceptance or equipment compatibility.','No runtime/CPU-GPU/native-item attachment/gameplay proof.']}
    write_json(args.output/'DELIVERY-SHIELD-R1.json',receipt)
    print(json.dumps({'candidate':str(path),'sha256':sha(path),'suppliedGLBsInspected':len(inventory_records),'triangles':audit['triangles'],'checks':audit['checks'],'views':len(views),'attachmentViews':len(attachment['views']) if attachment else 0},ensure_ascii=False),flush=True)


if __name__=='__main__':main()
