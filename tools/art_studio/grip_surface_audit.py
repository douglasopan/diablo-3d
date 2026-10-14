"""Measure distal finger skin against actual selected handle triangles.

Unsigned surface distance proves proximity only, not nonpenetration or anatomy.
The explicit handle bounds are authored in weapon-source world coordinates.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
import foot_contact as fc


def distances(points, triangles):
    a, b, c = triangles[:,0], triangles[:,1], triangles[:,2]
    ab, ac = b-a, c-a
    normal = np.cross(ab, ac)
    norm2 = np.sum(normal*normal, axis=1)
    valid = norm2 > 1e-16
    a,b,c,ab,ac,normal,norm2 = [x[valid] for x in (a,b,c,ab,ac,normal,norm2)]
    if not len(a): raise ValueError('No nondegenerate handle faces')
    output=[]
    for point in points:
        ap=point-a
        signed=np.sum(ap*normal,axis=1)/norm2
        projected=point-signed[:,None]*normal
        v=projected-a
        d00=np.sum(ab*ab,axis=1); d01=np.sum(ab*ac,axis=1); d11=np.sum(ac*ac,axis=1)
        d20=np.sum(v*ab,axis=1); d21=np.sum(v*ac,axis=1)
        denominator=d00*d11-d01*d01
        u=(d11*d20-d01*d21)/denominator; w=(d00*d21-d01*d20)/denominator
        inside=(u>=0)&(w>=0)&(u+w<=1)
        minimum=np.where(inside,signed*signed*norm2,np.inf)
        for start,end in ((a,b),(b,c),(c,a)):
            edge=end-start; length2=np.sum(edge*edge,axis=1)
            t=np.clip(np.sum((point-start)*edge,axis=1)/np.maximum(length2,1e-30),0,1)
            nearest=start+t[:,None]*edge
            minimum=np.minimum(minimum,np.sum((point-nearest)**2,axis=1))
        output.append(np.sqrt(minimum.min()))
    return np.array(output)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('body',type=Path);p.add_argument('weapon',type=Path);p.add_argument('calibration',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--handle-min',required=True);p.add_argument('--handle-max',required=True)
    args=p.parse_args()
    if args.output.exists():p.error('Use a fresh audit output')
    low=np.array([float(x) for x in args.handle_min.split(',')]);high=np.array([float(x) for x in args.handle_max.split(',')])
    if low.shape!=(3,) or np.any(high<=low):p.error('Invalid handle bounds')
    record=json.loads(args.calibration.read_text(encoding='utf-8-sig'))
    if fc.hp.sha(args.weapon.read_bytes())!=record['weaponSha256']:p.error('Wrong weapon revision')
    reader,_=fc.hp.load_reader(fc.hp.ROOT)
    body=fc.hp.Model(args.body,reader);weapon=fc.hp.Model(args.weapon,reader)
    faces=[]
    for primitive in weapon.geometry(weapon.rest_world):
        triangles=primitive['vertices'][primitive['triangles']]
        centers=triangles.mean(axis=1)
        faces.extend(triangles[np.all((centers>=low)&(centers<=high),axis=1)])
    triangles=np.array(faces)
    if not len(triangles):p.error('No faces inside handle selection')
    hand=[i for i,n in enumerate(body.nodes) if n.get('name')==record['bone']]
    if len(hand)!=1:p.error('Expected unique attachment bone')
    local=record['localTransform'];socket=np.eye(4)
    socket[:3,:3]=Rotation.from_quat(local['rotationXYZW']).as_matrix()*local['uniformScale'];socket[:3,3]=local['translation']
    rows=[]
    for phase in (0,.5,1):
        clip=body.animations[0];world=body.pose(clip,clip['start']+phase*(clip['end']-clip['start']))
        transform=world[hand[0]]@socket;inverse=np.linalg.inv(transform)
        scale=float(np.linalg.norm(transform[:3,0]))
        for finger in ('Index','Middle','Ring','Pinky','Thumb'):
            points=[]
            for primitive in body.primitives:
                joints,_=body.skins[primitive['skin']]
                indices=[k for k,j in enumerate(joints) if body.nodes[int(j)].get('name')=='mixamorig:RightHand'+finger+'3']
                weights=sum((ws*np.isin(js,indices)).sum(axis=1) for js,ws in primitive['pairs'])
                selected=np.flatnonzero(weights>.5)
                if len(selected):points.extend(fc.points(body,primitive,world,selected))
            if not points:raise ValueError('Distal finger has no weighted skin')
            source=(np.column_stack([points,np.ones(len(points))])@inverse.T)[:,:3]
            d=distances(source,triangles)*scale
            rows.append({'phase':phase,'finger':finger,'vertices':len(d),'minimumDistance':float(d.min()),
                         'p10Distance':float(np.quantile(d,.1)),'medianDistance':float(np.median(d)),
                         'verticesWithin2mm':int((d<=.002).sum())})
    fc.hp.write_json(args.output,{'bodySha256':fc.hp.sha(args.body.read_bytes()),'weaponSha256':record['weaponSha256'],
        'calibrationSha256':fc.hp.sha(args.calibration.read_bytes()),'handleMin':low.tolist(),'handleMax':high.tolist(),
        'handleTriangles':len(triangles),'samples':rows,'unsignedDistancesOnly':True,'collisionOrAnatomyApproved':False})
    print(json.dumps(rows[:5]))


if __name__=='__main__':main()
