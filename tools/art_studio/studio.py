"""Offline candidate preparation. Masters and selected runtime assets are never overwritten."""
from __future__ import annotations
import argparse
import hashlib
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).parent / 'vendor'))
from um.sprite import cutout

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def prepare_sequence(paths, output, size, *, background=None, tolerance=28, smooth=True, pivot=None):
    if output.exists():
        raise ValueError('Use a new candidate directory; existing output is preserved')
    images = [Image.open(p).convert('RGBA') for p in paths]
    if not images or len({im.size for im in images}) != 1:
        raise ValueError('Frames need the same source canvas; provide registered frames')
    prepared = [cutout(im, tol=tolerance, bg=background, trim_result=False) if background else im.copy() for im in images]
    boxes = [im.getchannel('A').getbbox() for im in prepared]
    nonempty = [b for b in boxes if b]
    if not nonempty:
        raise ValueError('All frames are transparent')
    box = (min(b[0] for b in nonempty), min(b[1] for b in nonempty), max(b[2] for b in nonempty), max(b[3] for b in nonempty))
    anchor = pivot or ((box[0]+box[2])/2, box[3])
    if not box[0] <= anchor[0] <= box[2] or not box[1] <= anchor[1] <= box[3]:
        raise ValueError('Pivot must lie in the shared content bounds')
    w, h = size
    left, right = anchor[0]-box[0], box[2]-anchor[0]
    scale = min((w-4)/max(2*left, 2*right, 1), (h-4)/max(anchor[1]-box[1], 1))
    if box[3] > anchor[1]:
        # Keep authored ground registration, reserving space below it.
        scale = min(scale, (h-4)/(box[3]-box[1]))
    tw, th = max(1, round((box[2]-box[0])*scale)), max(1, round((box[3]-box[1])*scale))
    ox = round(w/2 - (anchor[0]-box[0])*scale)
    oy = h-2-th
    ground = oy+(anchor[1]-box[1])*scale
    output.mkdir(parents=True)
    check = Image.new('RGB',(w*len(images),h),(58,58,58))
    records = []
    for index, (path, im) in enumerate(zip(paths, prepared)):
        crop = im.crop(box)
        # RGBa resampling prevents invisible RGB from bleeding into alpha edges.
        small = crop.convert('RGBa').resize((tw,th), Image.Resampling.LANCZOS if smooth else Image.Resampling.NEAREST).convert('RGBA')
        frame = Image.new('RGBA',(w,h)); frame.paste(small,(ox,oy))
        target=output/f'frame-{index:04d}.png';frame.save(target)
        check.paste(frame,(index*w,0),frame)
        records.append({'input':str(path),'inputSha256':digest(path),'output':target.name,'outputSha256':digest(target),'alphaBounds':frame.getchannel('A').getbbox()})
    draw=ImageDraw.Draw(check);draw.line((0,ground,check.width,ground),fill=(180,150,70))
    check.save(output/'registration-preview.png')
    record={'schemaVersion':1,'status':'candidate-not-selected','sharedSourceBounds':box,'sourcePivot':anchor,'framePivot':[w/2,ground],'scale':scale,'frameSize':size,'frames':records,'masterChanged':False}
    (output/'receipt.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    return record

def texture_preview(source, output):
    if output.exists():
        raise ValueError('Use a new candidate directory')
    im=Image.open(source).convert('RGB');a=np.asarray(im).astype(np.float32)
    def distance(x,y): return float(np.abs(x-y).mean())
    metrics={'leftRightMeanDelta':distance(a[:,0],a[:,-1]),'topBottomMeanDelta':distance(a[0],a[-1]),'internalHorizontalMeanDelta':distance(a[:,:-1],a[:,1:]),'internalVerticalMeanDelta':distance(a[:-1],a[1:])}
    # Review only: do not hide seams by automatically blurring the master.
    tile=im.copy();tile.thumbnail((384,384))
    preview=Image.new('RGB',(tile.width*3,tile.height*3))
    for y in range(3):
        for x in range(3): preview.paste(tile,(x*tile.width,y*tile.height))
    output.mkdir(parents=True);preview.save(output/'repeat-3x3.png')
    record={'schemaVersion':1,'source':str(source),'sourceSha256':digest(source),'status':'review-only-not-seamless-certification','metrics':metrics,'masterChanged':False}
    (output/'receipt.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    return record

def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    a=sub.add_parser('sprites');a.add_argument('inputs',nargs='+',type=Path);a.add_argument('--output',type=Path,required=True);a.add_argument('--size',default='256x256');a.add_argument('--background',help='Explicit R,G,B for a flat background; absent preserves alpha');a.add_argument('--tolerance',type=int,default=28);a.add_argument('--pixel-art',action='store_true');a.add_argument('--pivot',help='Authored X,Y in the shared source canvas')
    t=sub.add_parser('texture');t.add_argument('input',type=Path);t.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    if args.command=='sprites':
        size=tuple(map(int,args.size.split('x')))
        if len(size)!=2 or min(size)<8 or max(size)>4096: p.error('Size must be 8..4096 per dimension')
        bg=tuple(map(int,args.background.split(','))) if args.background else None
        if bg and (len(bg)!=3 or min(bg)<0 or max(bg)>255):p.error('Background must be R,G,B 0..255')
        pivot=tuple(map(float,args.pivot.split(','))) if args.pivot else None
        if pivot and len(pivot)!=2:p.error('Pivot must be X,Y')
        result=prepare_sequence(args.inputs,args.output,size,background=bg,tolerance=args.tolerance,smooth=not args.pixel_art,pivot=pivot)
    else:result=texture_preview(args.input,args.output)
    print(json.dumps(result))

if __name__=='__main__':main()
