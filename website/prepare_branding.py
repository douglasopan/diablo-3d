"""Encode the owner's original RGBA sequence for the web, preserving its alpha/timing."""
from __future__ import annotations

import argparse
import hashlib
from io import BytesIO
import json
from pathlib import Path
from zipfile import ZipFile

from PIL import Image

ROOT = Path(__file__).resolve().parent


def prepare(archive: Path):
    provenance = json.loads((ROOT.parent / 'assets/branding/animated-logo-provenance.json').read_text(encoding='utf-8'))
    if hashlib.sha256(archive.read_bytes()).hexdigest() != provenance['sourceArchiveSha256']:
        raise ValueError('Archive does not match the owner-provided source')
    frames = []
    with ZipFile(archive) as source:
        for index in range(provenance['sourceFrames']):
            name = f'frames_png_640x160/diablo3d_frame_{index:04d}.png'
            frame = Image.open(BytesIO(source.read(name)))
            frame.load()
            if frame.mode != 'RGBA' or frame.size != (640, 160):
                raise ValueError('Expected the original transparent 640×160 frames')
            frames.append(frame)
    output = ROOT / 'public/assets/branding'
    output.mkdir(parents=True, exist_ok=True)
    frames[0].save(output / 'd3d-logo-static.webp', lossless=True, exact=True, method=6)
    temporary = output / 'd3d-logo-animated.tmp'
    frames[0].save(temporary, format='WEBP', save_all=True, append_images=frames[1:], duration=[33, 33, 34] * 80, loop=0, quality=86, alpha_quality=100, lossless=False, method=4, kmin=0, kmax=1)
    with Image.open(temporary) as animated:
        if animated.n_frames != 240:
            raise ValueError('Animation lost frames')
        if animated.info['loop'] != 0:
            raise ValueError('Animation must loop continuously')
        duration = 0
        for index in range(animated.n_frames):
            animated.seek(index)
            # Loading a WebP frame updates its duration metadata.
            rendered = animated.convert('RGBA')
            duration += animated.info['duration']
            if rendered.getchannel('A').tobytes() != frames[index].getchannel('A').tobytes():
                raise ValueError(f'Frame {index} lost its original alpha coverage')
        if duration != 8000:
            raise ValueError('Animation must preserve the exact eight-second cycle')
    temporary.replace(output / 'd3d-logo-animated.webp')
    (ROOT / 'branding.json').write_text(json.dumps({
        'sourceArchiveSha256': provenance['sourceArchiveSha256'],
        'source': 'Owner-provided original transparent RGBA frames',
        'frames': 240, 'width': 640, 'height': 160, 'cycleMs': 8000,
        'encoding': {'format': 'animated WebP', 'quality': 86, 'alpha': 'original coverage preserved in every frame', 'method': 4},
        'assets': {file.name: {'bytes': file.stat().st_size, 'sha256': hashlib.sha256(file.read_bytes()).hexdigest()} for file in output.glob('*.webp')},
    }, indent=2) + '\n', encoding='utf-8', newline='\n')
    print('Prepared 240 alpha-preserved frames, exact 8000 ms loop, and lossless static fallback.')
    for file in output.iterdir():
        print(file.name, file.stat().st_size)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('archive', type=Path)
    prepare(parser.parse_args().archive)
