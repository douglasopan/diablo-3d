import tempfile
import unittest
from pathlib import Path
from PIL import Image, ImageDraw
from studio import prepare_sequence, texture_preview, digest

class StudioTests(unittest.TestCase):
    def test_registration_preserves_relative_motion_and_master(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);inputs=[]
            for index,x in enumerate((8,12)):
                im=Image.new('RGBA',(32,32),'white');d=ImageDraw.Draw(im)
                d.rectangle((x,8,x+7,23),fill='black');d.rectangle((x+2,12,x+4,17),fill='white')
                path=p/f'{index}.png';im.save(path);inputs.append(path)
            before=[digest(f) for f in inputs]
            r=prepare_sequence(inputs,p/'candidate',(64,64),background=(255,255,255),smooth=False)
            b=[f['alphaBounds'] for f in r['frames']]
            self.assertGreater(b[1][0],b[0][0]);self.assertEqual(b[0][3],b[1][3])
            self.assertEqual(before,[digest(f) for f in inputs])
            # Enclosed white detail must remain opaque rather than being keyed out.
            frame=Image.open(p/'candidate/frame-0000.png')
            self.assertTrue(any(pixel[:3]==(255,255,255) and pixel[3]==255 for pixel in frame.getdata()))
            with self.assertRaises(ValueError): prepare_sequence(inputs,p/'candidate',(64,64))

    def test_texture_flags_edge_discontinuity_without_modifying_source(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);im=Image.new('RGB',(16,16),'black');ImageDraw.Draw(im).line((15,0,15,15),fill='white');im.save(p/'input.png')
            before=digest(p/'input.png');r=texture_preview(p/'input.png',p/'review')
            self.assertGreater(r['metrics']['leftRightMeanDelta'],r['metrics']['internalHorizontalMeanDelta'])
            self.assertEqual(before,digest(p/'input.png'))
            with Image.open(p/'review/repeat-3x3.png') as preview:
                self.assertEqual(preview.size,(48,48))

    def test_npc_morph_keeps_topology_and_validates_range(self):
        from npc_body import create, SOURCE
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);before=digest(SOURCE/'base.obj')
            r=create(p/'body',age=.4,muscle=.2,weight=.3)
            base=(SOURCE/'base.obj').read_text(encoding='utf-8').splitlines()
            changed=(p/'body/body.obj').read_text(encoding='utf-8').splitlines()
            self.assertEqual([s for s in base if not s.startswith('v ')],[s for s in changed if not s.startswith('v ')])
            self.assertNotEqual([s for s in base if s.startswith('v ')],[s for s in changed if s.startswith('v ')])
            self.assertEqual(before,digest(SOURCE/'base.obj'));self.assertFalse(r['gameplayEligible'])
            with self.assertRaises(ValueError):create(p/'bad',age=float('nan'))

if __name__=='__main__':unittest.main()
