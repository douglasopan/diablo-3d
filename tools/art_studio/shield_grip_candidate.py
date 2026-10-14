"""Private left finger-only buckler grip with bounded anatomical hinges.

No arm/wrist/root/bone length, scale, skin, mesh, material or master edits.
Exact surface distance plus closed-grip solid-angle containment is measured;
an elliptic centerline envelope is only an optimization aid, not a contact
approval. A local socket translation can be explicitly calibrated while
preserving orientation and uniform scale of the prior candidate.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
from types import SimpleNamespace
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation
import foot_contact as fc
from attack_settle import sample_nodes
from grip_surface_audit import distances
import starter_shield_candidate as shield_tools

FINGERS=('Index','Middle','Ring','Pinky','Thumb')
STATIC_KEYS=('nodes','meshes','skins','materials','textures','images','samplers','scenes','scene')


def socket_matrix(record):
    t=record['localTransform'];m=np.eye(4)
    m[:3,:3]=Rotation.from_quat(t['rotationXYZW']).as_matrix()*t['uniformScale']
    m[:3,3]=t['translation'];return m


def closed_inside(points,triangles):
    """Generalized winding number of an outward closed actual triangle mesh."""
    result=[]
    for point in points:
        v=triangles-point; a,b,c=v[:,0],v[:,1],v[:,2]
        la,lb,lc=[np.linalg.norm(t,axis=1) for t in (a,b,c)]
        numerator=np.einsum('ij,ij->i',a,np.cross(b,c))
        denominator=la*lb*lc+np.einsum('ij,ij->i',a,b)*lc+np.einsum('ij,ij->i',b,c)*la+np.einsum('ij,ij->i',c,a)*lb
        result.append(abs(np.arctan2(numerator,denominator).sum()/(2*np.pi)))
    return np.asarray(result)>.5


def signed_triangle_distance(points,triangles):
    """Vectorized actual triangle projection/edge distance and closed winding."""
    a,b,c=triangles[:,0],triangles[:,1],triangles[:,2]
    ab,ac=b-a,c-a;normal=np.cross(ab,ac);norm2=np.sum(normal*normal,axis=1)
    ap=points[:,None,:]-a
    plane=np.einsum('pti,ti->pt',ap,normal)/norm2
    projected=ap-plane[:,:,None]*normal
    d00=np.sum(ab*ab,axis=1);d01=np.sum(ab*ac,axis=1);d11=np.sum(ac*ac,axis=1)
    d20=np.einsum('pti,ti->pt',projected,ab);d21=np.einsum('pti,ti->pt',projected,ac)
    denominator=d00*d11-d01*d01
    u=(d11*d20-d01*d21)/denominator;v=(d00*d21-d01*d20)/denominator
    squared=np.where((u>=0)&(v>=0)&(u+v<=1),plane*plane*norm2,np.inf)
    for start,end in ((a,b),(b,c),(c,a)):
        edge=end-start;length2=np.sum(edge*edge,axis=1)
        t=np.clip(np.einsum('pti,ti->pt',points[:,None,:]-start,edge)/length2,0,1)
        off=points[:,None,:]-start-t[:,:,None]*edge
        squared=np.minimum(squared,np.einsum('pti,pti->pt',off,off))
    distance=np.sqrt(squared.min(axis=1))
    aa,bb,cc=[x-points[:,None,:] for x in (a,b,c)]
    la,lb,lc=[np.linalg.norm(x,axis=2) for x in (aa,bb,cc)]
    numerator=np.einsum('pti,pti->pt',aa,np.cross(bb,cc))
    denominator=la*lb*lc+np.einsum('pti,pti->pt',aa,bb)*lc+np.einsum('pti,pti->pt',bb,cc)*la+np.einsum('pti,pti->pt',cc,aa)*lb
    inside=abs(np.arctan2(numerator,denominator).sum(axis=1))>np.pi
    return np.where(inside,-distance,distance)


def skin_groups(body,names):
    result={}
    for finger in FINGERS+('Palm',):
        target=[names['mixamorig:LeftHand']] if finger=='Palm' else [names['mixamorig:LeftHand'+finger+str(i)] for i in (1,2,3)]
        groups=[];distal=[]
        for primitive in body.primitives:
            if primitive['skin'] is None:continue
            joints,_=body.skins[primitive['skin']]
            weights=sum((ws*np.isin(joints[js],target)).sum(axis=1) for js,ws in primitive['pairs'])
            indices=np.flatnonzero(weights>.55)
            if len(indices):groups.append((primitive,indices))
            if finger!='Palm':
                dweight=sum((ws*(joints[js]==target[-1])).sum(axis=1) for js,ws in primitive['pairs'])
                di=np.flatnonzero(dweight>.5)
                if len(di):distal.append((primitive,di))
        result[finger]={'all':groups,'distal':distal}
    return result


def points_for(body,groups,world):
    return np.concatenate([fc.points(body,p,world,indices) for p,indices in groups])


def source_points(body,groups,world,inverse):
    pts=points_for(body,groups,world)
    return (np.column_stack([pts,np.ones(len(pts))])@inverse.T)[:,:3]


def grip_envelope(points,spine):
    """Approximate elliptical tube implicit distance from actual ring centers."""
    a,b=spine[:-1],spine[1:];segment=b-a;length2=np.sum(segment*segment,axis=1)
    delta=points[:,None,:]-a
    t=np.clip(np.einsum('pij,ij->pi',delta,segment)/length2,0,1)
    nearest=a+t[:,:,None]*segment;off=points[:,None,:]-nearest
    distance=np.sum(off*off,axis=2);choice=np.argmin(distance,axis=1)
    off=off[np.arange(len(points)),choice]
    tangents=segment[choice]/np.sqrt(length2[choice,None])
    # Exported handle rings have one .017 Y and one .009 cross-sectional radius.
    radial_z=np.cross(tangents,np.array([0.,1.,0.])); radial_z/=np.linalg.norm(radial_z,axis=1,keepdims=True)
    yy=off[:,1]/.017;zz=np.einsum('ij,ij->i',off,radial_z)/.009
    return (np.sqrt(yy*yy+zz*zz)-1)*.009


def fit_finger(body,reader,guard,names,groups,inverse,spine,triangles,finger,across,normal):
    chain=[names['mixamorig:LeftHand'+finger+str(i)] for i in (1,2,3)]
    initial=fc.world(body,guard,reader)
    bases=[Rotation.from_quat(guard[i]['rotation']) for i in chain]
    axes=[]
    for i in chain:
        axis=np.linalg.inv(initial[i][:3,:3])@across
        axes.append(axis/np.linalg.norm(axis))
    thumb_opposition=np.linalg.inv(initial[chain[0]][:3,:3])@normal
    thumb_opposition/=np.linalg.norm(thumb_opposition)
    thumb=finger=='Thumb'
    lower=np.deg2rad([0,0,0,-25] if thumb else [0,0,0])
    upper=np.deg2rad([65,80,70,25] if thumb else [85,105,75])
    # The long-thumb saddle joint uses two explicit hinges. Other joints use
    # one flexion hinge only; no free Euler/quaternion solve or finger scaling.
    def nodes_for(values):
        nodes=copy.deepcopy(guard)
        for k,i in enumerate(chain):
            delta=Rotation.from_rotvec(axes[k]*values[k])
            if thumb and k==0:delta=Rotation.from_rotvec(thumb_opposition*values[3])*delta
            nodes[i]['rotation']=(bases[k]*delta).as_quat().tolist()
        return nodes
    def target(values):
        nodes=nodes_for(values);world=fc.world(body,nodes,reader)
        xyz=np.array([(inverse@world[i])[:3,3] for i in chain])
        skin=source_points(body,groups['all'],world,inverse)
        distal=source_points(body,groups['distal'],world,inverse)
        return nodes,xyz,skin,distal
    # Target phalanx centers lie around a .017x.009 actual grip expanded by
    # flesh radius. Local source X follows each finger's individual base.
    x=(inverse@initial[chain[0]])[0,3]
    if thumb:
        desired=np.array([[x,0,-.090],[x, -.027,-.090],[x,-.014,-.091]])
        initial_degrees=[35,15,25,0]
    else:
        desired=np.array([[x,0,-.090],[x,.028,-.078],[x,.023,-.050]])
        initial_degrees=[25,80,55]
    def residual(values):
        _,xyz,skin,distal=target(values)
        exact=signed_triangle_distance(np.r_[skin,distal],triangles)
        # Actual full skin samples drive both clearance and distal contact.
        # The previous sparse elliptic proxy missed thumb intersections and
        # could leave distal phalanges away from the grip despite convergence.
        collision=np.minimum(exact[:len(skin)]-.0002,0)*350
        contact=(np.sort(abs(exact[len(skin):]))[:min(8,len(distal))]-.0006)*75
        positions=(xyz[1:]-desired[1:]).reshape(-1)*6
        preferred=(values-np.deg2rad(initial_degrees))*.025
        return np.r_[positions,collision,contact,preferred]
    seeds=[np.deg2rad(initial_degrees)]
    if thumb:seeds.extend([np.deg2rad([15,20,10,15]),np.deg2rad([50,0,10,-10])])
    else:seeds.extend([np.deg2rad([10,95,60]),np.deg2rad([45,60,35])])
    solutions=[least_squares(residual,seed,bounds=(lower,upper),max_nfev=100,ftol=1e-8,xtol=1e-8,gtol=1e-8) for seed in seeds]
    solved=min(solutions,key=lambda s:np.linalg.norm(s.fun))
    fitted,xyz,skin,distal=target(solved.x)
    return {i:fitted[i]['rotation'] for i in chain},{'finger':finger,'hingeDegrees':np.rad2deg(solved.x).tolist(),
        'hingeAxesLocal':[a.tolist() for a in axes],'thumbOppositionAxisLocal':thumb_opposition.tolist() if thumb else None,
        'lowerDegrees':np.rad2deg(lower).tolist(),'upperDegrees':np.rad2deg(upper).tolist(),
        'solverSuccess':bool(solved.success),'solverResidual':float(np.linalg.norm(solved.fun)),
        'sourcePhalanxCenters':xyz.tolist(),'approximateEnvelopeMinimum':float(grip_envelope(skin,spine).min()),
        'hingeModel':'three bounded flexion hinges; thumb additionally bounded root opposition' if thumb else 'three bounded flexion hinges'}


def surface_audit(body,shield,clip,socket,groups,hand,triangles,phases):
    records=[]
    for phase in phases:
        world=body.pose(clip,clip['start']+(clip['end']-clip['start'])*phase)
        transform=world[hand]@socket;inverse=np.linalg.inv(transform);scale=float(np.linalg.norm(transform[:3,0]))
        for finger in FINGERS+('Palm',):
            pts=source_points(body,groups[finger]['all'],world,inverse)
            d=distances(pts,triangles)*scale;inside=closed_inside(pts,triangles)
            signed=np.where(inside,-d,d)
            row={'phase':phase,'finger':finger,'skinVertices':len(pts),'minimumDistance':float(d.min()),
                 'p10Distance':float(np.quantile(d,.1)),'medianDistance':float(np.median(d)),
                 'verticesWithin002':int((d<=.002).sum()),'insideVertices':int(inside.sum()),
                 'minimumSignedDistance':float(signed.min()),'maximumPenetrationDepth':float((-signed[signed<0]).max()) if inside.any() else 0}
            if finger!='Palm':
                distal=source_points(body,groups[finger]['distal'],world,inverse)
                dd=distances(distal,triangles)*scale;ii=closed_inside(distal,triangles)
                row['distal']={'vertices':len(dd),'minimumDistance':float(dd.min()),'p10Distance':float(np.quantile(dd,.1)),
                               'verticesWithin002':int((dd<=.002).sum()),'insideVertices':int(ii.sum()),
                               'maximumPenetrationDepth':float(dd[ii].max()) if ii.any() else 0}
            records.append(row)
    return records


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('body',type=Path);p.add_argument('shield',type=Path)
    p.add_argument('socket',type=Path);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--socket-offset-source',default='0,0,0');p.add_argument('--recipe',type=Path);p.add_argument('--recipe-reference',type=Path)
    p.add_argument('--bundle-reference',type=Path)
    p.add_argument('--allow-finger-motion-clamp',action='store_true',help='Explicitly author this static closed grip over varying left finger rotations only; preserves every other track')
    args=p.parse_args()
    if args.output.exists():p.error('Use a fresh private output directory')
    offset=np.array([float(v) for v in args.socket_offset_source.split(',')])
    if offset.shape!=(3,) or not np.isfinite(offset).all() or np.linalg.norm(offset)>.09:p.error('Bounded source-space socket translation required')
    if (args.recipe is None)!=(args.recipe_reference is None):p.error('Recipe requires exact original reference body')
    reader,reader_record=fc.hp.load_reader(fc.hp.ROOT)
    body=fc.hp.Model(args.body,reader);shield=fc.hp.Model(args.shield,reader)
    if len(body.animations)!=1:p.error('Use one compatible guard/idle clip')
    original=json.loads(args.socket.read_text(encoding='utf-8-sig'))
    if fc.hp.sha(args.shield.read_bytes())!=original['shieldSha256']:p.error('Exact shield revision required')
    guard_source=fc.hp.Model(fc.hp.ROOT.parent/Path(original['body']),reader)
    if fc.hp.sha(guard_source.path.read_bytes())!=original['bodySha256']:p.error('Socket source body mismatch')
    if any(body.doc.get(k)!=guard_source.doc.get(k) for k in STATIC_KEYS):p.error('Socket reuse requires exact static body layout')
    if args.bundle_reference:
        bundle=fc.hp.Model(args.bundle_reference,reader)
        if any(body.doc.get(k)!=bundle.doc.get(k) for k in STATIC_KEYS):p.error('Bundle static layout mismatch')
    chosen=copy.deepcopy(original);socket=socket_matrix(original)
    socket[:3,3]+=socket[:3,:3]@offset
    chosen['localTransform']['translation']=socket[:3,3].tolist();chosen['body']=str(args.body.resolve())
    chosen['bodySha256']=fc.hp.sha(args.body.read_bytes());chosen['previousSocketSha256']=fc.hp.sha(args.socket.read_bytes())
    chosen['sourceSpaceOffset']=offset.tolist();chosen['socketChanged']=bool(np.any(offset))
    chosen['reason']='Explicit ergonomic calibration towards finger bases / palm clearance; rotation and uniform scale preserved' if np.any(offset) else 'Original R2 socket preserved exactly'
    names={n.get('name'):i for i,n in enumerate(body.nodes)};hand=names['mixamorig:LeftHand'];clip=body.animations[0]
    guard=sample_nodes(body,clip,clip['start']);world=fc.world(body,guard,reader);inverse=np.linalg.inv(world[hand]@socket)
    if 'initialPalmBasis' in chosen:
        chosen['referenceInitialPalmBasis']=chosen.pop('initialPalmBasis')
    attachment_world=world[hand]@socket
    chosen['initialAttachmentBasis']={
        'across':(attachment_world[:3,0]/np.linalg.norm(attachment_world[:3,0])).tolist(),
        'finger':(attachment_world[:3,1]/np.linalg.norm(attachment_world[:3,1])).tolist(),
        'outwardNormal':(attachment_world[:3,2]/np.linalg.norm(attachment_world[:3,2])).tolist(),
        'gripWorldAnchor':(attachment_world@np.r_[original['sourceGrip'],1])[:3].tolist()}
    across=(world[hand]@socket)[:3,0];across/=np.linalg.norm(across)
    normal=(world[hand]@socket)[:3,2];normal/=np.linalg.norm(normal)
    groups=skin_groups(body,names)
    primitive=next(p for p in shield.primitives if shield.nodes[p['node']].get('name')=='back-raised-transverse-grip')
    triangles=primitive['positions'][primitive['triangles']]
    if len(primitive['positions'])!=172:p.error('Verified exact grip ring construction required')
    spine=primitive['positions'][:170].reshape(10,17,3)[:,:16].mean(axis=1)
    metrics=[];rotations={}
    if args.recipe:
        recipe=json.loads(args.recipe.read_text(encoding='utf-8-sig'));reference=fc.hp.Model(args.recipe_reference,reader)
        if fc.hp.sha(args.recipe_reference.read_bytes())!=recipe['bodySha256']:p.error('Recipe reference mismatch')
        if any(body.doc.get(k)!=reference.doc.get(k) for k in STATIC_KEYS):p.error('Recipe static layout mismatch')
        if original['shieldSha256']!=recipe['shieldSha256']:p.error('Recipe shield mismatch')
        if chosen['localTransform']!=recipe['socketLocalTransform']:p.error('Recipe socket mismatch')
        rotations={int(i):q for i,q in recipe['rotations'].items()};metrics=recipe['fit']
    else:
        for finger in FINGERS:
            q,fit=fit_finger(body,reader,guard,names,groups[finger],inverse,spine,triangles,finger,across,normal)
            rotations.update(q);metrics.append(fit)
            print(json.dumps({'finger':finger,'hingeDegrees':fit['hingeDegrees'],'residual':fit['solverResidual']}),flush=True)
    times=np.unique(np.concatenate([c['times'] for c in clip['channels']]))
    # A static grip can explicitly replace finger motion for equipment use.
    # Never silently discard a different source finger gesture.
    source_finger_variation=0
    for time in times:
        source_nodes=sample_nodes(body,clip,float(time))
        for i in rotations:
            source_finger_variation=max(source_finger_variation,float(np.max(abs(reader.node_matrix(source_nodes[i])-reader.node_matrix(guard[i])))))
    if source_finger_variation>1e-6 and not args.allow_finger_motion_clamp:
        p.error('Source fingers vary; reuse requires explicit --allow-finger-motion-clamp')
    args.output.mkdir(parents=True)
    fc.hp.write_json(args.output/'SOCKET-CANDIDATE.json',chosen)
    poses=[]
    for time in times:
        nodes=sample_nodes(body,clip,float(time))
        for i,q in rotations.items():nodes[i]['rotation']=q
        poses.append(nodes)
    fc.rt.SEMANTICS=fc.skin_semantics(body);candidate=args.output/'warrior-left-shield-grip.glb'
    old,_=fc.rt.append_animation(body,reader,times-clip['start'],poses,'left-shield-grip-'+clip['name'],
        {'source':str(args.body.resolve()),'sha256':fc.hp.sha(args.body.read_bytes()),'leftFingerOnly':True,
         'shieldSha256':original['shieldSha256'],'socketSha256':fc.hp.sha((args.output/'SOCKET-CANDIDATE.json').read_bytes())},candidate)
    result=fc.hp.Model(candidate,reader);result.load_materials();shield.load_materials();rclip=result.animations[0]
    maximum=0;length_delta=0;ground=1e9
    for phase in np.linspace(0,1,129):
        a=sample_nodes(body,clip,clip['start']+phase*(clip['end']-clip['start']));b=sample_nodes(result,rclip,rclip['start']+phase*(rclip['end']-rclip['start']))
        for i in range(len(a)):
            if i not in rotations:maximum=max(maximum,float(abs(reader.node_matrix(a[i])-reader.node_matrix(b[i])).max()))
            length_delta=max(length_delta,float(np.max(abs(np.array(a[i].get('translation',[0,0,0]))-np.array(b[i].get('translation',[0,0,0]))))))
            if a[i].get('scale')!=b[i].get('scale'):raise ValueError('Source scale changed')
        ground=min(ground,float(min(g['vertices'][:,1].min() for g in result.geometry(result.pose(rclip,rclip['start']+phase*(rclip['end']-rclip['start']))))))
    d,binary,_=reader.read_glb(candidate)
    checks={'staticPayloadUnchanged':all(d.get(k)==body.doc.get(k) for k in STATIC_KEYS),'binaryPrefixPreserved':binary[:len(old)]==old,
            'onlyLeftFingerLocalRotations':set(rotations)=={names['mixamorig:LeftHand'+f+str(i)] for f in FINGERS for i in (1,2,3)},
            'maximumNonfingerLocalDeltaBelow1e-6':maximum<1e-6,'boneTranslationsExact':length_delta==0,'scalesExact':True,
            'bodyFloorPreserved':ground>=-.002,'socketOrientationScalePreserved':chosen['localTransform']['rotationXYZW']==original['localTransform']['rotationXYZW'] and chosen['localTransform']['uniformScale']==original['localTransform']['uniformScale']}
    before=surface_audit(body,shield,clip,socket,groups,hand,triangles,[0])
    after=surface_audit(result,shield,rclip,socket,groups,hand,triangles,[0,.5,1])
    fc.hp.write_json(args.output/'CONTACT-AUDIT.json',{'gripNode':'back-raised-transverse-grip','actualGripTriangles':len(triangles),
        'shieldSha256':original['shieldSha256'],'candidateSha256':fc.hp.sha(candidate.read_bytes()),'before':before,'after':after,
        'distanceUnits':'Actor source units; not an established physical gameplay scale','containment':'Generalized winding number > .5 on actual closed grip triangles',
        'limitations':['Contains only sampled skin vertices; triangle/triangle intersections between vertices remain untested.',
                       'Handle-only audit does not establish shield plate / thumb-finger / forearm noncollision.','Unsigned distances alone are not acceptance.']})
    merged=SimpleNamespace(materials={**result.materials,**{i+len(result.materials):m for i,m in shield.materials.items()}})
    frames=[]
    for phase in (0,.5,1):
        w=result.pose(rclip,rclip['start']+phase*(rclip['end']-rclip['start']));t=w[hand]@socket
        full=result.geometry(w)+shield_tools.transformed_geometry(shield,t,len(result.materials))
        # Hide only occluding body primitives in hand-only review by retaining
        # triangles substantially weighted to LeftHand and its finger chain.
        hand_geometry=[]
        hand_nodes={hand}|{names['mixamorig:LeftHand'+f+str(i)] for f in FINGERS for i in (1,2,3)}
        for gp,prim in zip(result.geometry(w),result.primitives):
            if prim['skin'] is None:continue
            joints,_=result.skins[prim['skin']];wt=sum((ws*np.isin(joints[js],list(hand_nodes))).sum(axis=1) for js,ws in prim['pairs'])
            keep=np.mean(wt[prim['triangles']],axis=1)>.45
            if keep.any():item=dict(gp);item['triangles']=prim['triangles'][keep];hand_geometry.append(item)
        grip_geometry=shield_tools.transformed_geometry(shield,t,len(result.materials))
        grip_only=[gp for gp in grip_geometry if shield.nodes[gp['node']].get('name')=='back-raised-transverse-grip']
        center=(t@np.array([0,0,-.073,1]))[:3]
        # yaw derived from actual palm outward normal; rear is the grasp side.
        normal=t[:3,2];palm_yaw=np.rad2deg(np.arctan2(-normal[0],normal[2]))+180
        configurations=[('full-front',full,0,550,[384,700],np.zeros(3)),('full-side',full,90,550,[384,700],np.zeros(3)),
                        ('grip-palmar',hand_geometry+grip_only,palm_yaw,5400,[384,384],center),
                        ('grip-profile',hand_geometry+grip_only,palm_yaw+90,5400,[384,384],center),
                        ('hand-shield-rear',full,palm_yaw,2400,[384,384],center)]
        for label,g,yaw,ppm,pivot,center in configurations:
            image,stats=fc.hp.raster(merged,g,yaw,(768,768),pivot,center,ppm,0,1,False)
            file=f'{label}-{phase:g}.png';image.save(args.output/file);frames.append({'phase':phase,'view':label,'file':file,'stats':stats})
    recipe={'bodySha256':fc.hp.sha(args.body.read_bytes()),'shieldSha256':original['shieldSha256'],'rotations':rotations,'fit':metrics,
            'socketLocalTransform':chosen['localTransform'],'socketSourceSpaceOffset':offset.tolist(),'staticLayoutReference':str(args.body.resolve()),
            'fingerJoints':sorted(rotations),'sourceFingerQuaternions':{i:guard[i]['rotation'] for i in rotations},
            'rule':'Reuse only with exact static layout. Replace only these 15 local rotation tracks. Varying source finger tracks require explicit --allow-finger-motion-clamp.',
            'candidateSha256':fc.hp.sha(candidate.read_bytes()),'gameplaySelected':False,'anatomyApproved':False}
    fc.hp.write_json(args.output/'GRIP-RECIPE.json',recipe)
    receipt={'source':str(args.body.resolve()),'sourceSha256':fc.hp.sha(args.body.read_bytes()),'shield':str(args.shield.resolve()),
        'shieldSha256':original['shieldSha256'],'priorSocketSha256':fc.hp.sha(args.socket.read_bytes()),'candidate':str(candidate.resolve()),
        'candidateSha256':fc.hp.sha(candidate.read_bytes()),'toolSha256':fc.hp.sha(Path(__file__).read_bytes()),'reader':reader_record,
        'checks':checks,'maximumNonfingerLocalDelta':maximum,'translationDelta':length_delta,'minimumBodyY':ground,
        'fit':metrics,'contact':after,'frames':frames,'socketChanged':bool(np.any(offset)),
        'maximumSourceFingerLocalVariation':source_finger_variation,
        'sourceFingerMotionClampingExplicit':args.allow_finger_motion_clamp,
        'reusedRecipe':{'path':str(args.recipe.resolve()),'sha256':fc.hp.sha(args.recipe.read_bytes())} if args.recipe else None,
        'status':'Private bounded left-hand grip candidate; contact/anatomy/native admission require review',
        'fiveDistalProximityPass':all(r['distal']['minimumDistance']<=.002 for r in after if r['finger']!='Palm'),
        'sampledVertexNonpenetrationPass':all(r['maximumPenetrationDepth']<=.0005 for r in after),
        'masterChanged':False,'gameplaySelected':False,'artisticApproval':False,'deathChanged':False}
    fc.hp.write_json(args.output/'DELIVERY-GRIP.json',receipt)
    print(json.dumps({'candidate':str(candidate),'checks':checks,'fiveDistalProximityPass':receipt['fiveDistalProximityPass'],
                      'sampledVertexNonpenetrationPass':receipt['sampledVertexNonpenetrationPass'],'contact':after[:6]}),flush=True)


if __name__=='__main__':main()
