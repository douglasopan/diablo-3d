"""Small real-geometry controls for the skin/clip boundary; no production render."""
import contextlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest

import numpy as np

import hero_pipeline as hp


def fixture(path, moving):
    arrays = [np.array([[0,0,0],[1,0,0],[0,1,0]],dtype="<f4"),
              np.array([0,1,2],dtype="<u2"),np.zeros((3,4),dtype="<u2"),
              np.array([[1,0,0,0]]*3,dtype="<f4"),np.eye(4,dtype="<f4").reshape(1,16),
              np.array([0,1],dtype="<f4"),np.array([[0,0,0],[1 if moving else 0,0,0]],dtype="<f4")]
    binary,views = bytearray(),[]
    for array in arrays:
        while len(binary)%4:
            binary.append(0)
        views.append({"buffer":0,"byteOffset":len(binary),"byteLength":array.nbytes})
        binary.extend(array.tobytes())
    accessors = [{"bufferView":i,"componentType":component,"count":count,"type":kind} for i,(component,count,kind) in enumerate(
        [(5126,3,"VEC3"),(5123,3,"SCALAR"),(5123,3,"VEC4"),(5126,3,"VEC4"),(5126,1,"MAT4"),(5126,2,"SCALAR"),(5126,2,"VEC3")])]
    doc={"asset":{"version":"2.0"},"buffers":[{"byteLength":len(binary)}],"bufferViews":views,"accessors":accessors,
         "nodes":[{"children":[1,2]},{"name":"rootJoint"},{"mesh":0,"skin":0}],"scenes":[{"nodes":[0]}],"scene":0,
         "skins":[{"joints":[1],"inverseBindMatrices":4}],
         "meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":2,"WEIGHTS_0":3},"indices":1}]}],
         "animations":[{"name":"test-translation","channels":[{"sampler":0,"target":{"node":1,"path":"translation"}}],
                        "samplers":[{"input":5,"output":6,"interpolation":"LINEAR"}]}]}
    encoded=json.dumps(doc,separators=(",",":")).encode()
    encoded+=b" "*((-len(encoded))%4)
    binary+=b"\0"*((-len(binary))%4)
    path.write_bytes(struct.pack("<4sII",b"glTF",2,12+8+len(encoded)+8+len(binary))+
                     struct.pack("<II",len(encoded),0x4e4f534a)+encoded+
                     struct.pack("<II",len(binary),0x004e4942)+binary)


class PipelineControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.reader,_=hp.load_reader(hp.ROOT)

    def test_real_skin_follows_actual_clip(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"fixture.glb"
            fixture(path,True)
            model=hp.Model(path,self.reader)
            animation=model.animations[0]
            at_start=model.geometry(model.pose(animation,0))[0]["vertices"]
            at_half=model.geometry(model.pose(animation,.5))[0]["vertices"]
            np.testing.assert_allclose(at_half-at_start,[[.5,0,0]]*3)
            self.assertTrue(model.clip_summary(animation)["hasSampledDeformation"])

    def test_static_keys_refused_as_animation(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"pose.glb"
            fixture(path,False)
            model=hp.Model(path,self.reader)
            self.assertFalse(model.clip_summary(model.animations[0])["hasSampledDeformation"])
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                hp.main(["render",str(path),"--output",str(Path(directory)/"out"),"--clip","test-translation",
                         "--state","walk","--frames","8","--footpoint","0,0,0"])
            self.assertFalse((Path(directory)/"out").exists())

    def test_bind_cannot_be_labelled_idle(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            hp.main(["render","does-not-exist.glb","--output","unused","--bind-stills","--state","idle","--footpoint","0,0,0"])

    def test_quaternion_short_arc_and_native_order(self):
        q=hp.interpolate(np.array([0.,1.]),np.array([[0,0,0,1],[0,0,0,-1.]]),"LINEAR",.5,True)
        np.testing.assert_allclose(q,[0,0,0,1])
        self.assertEqual([d for d,_ in hp.DIRECTIONS],["s","sw","w","nw","n","ne","e","se"])

    def test_cpu_projection_keeps_ground_pivot_and_winding(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"fixture.glb"
            fixture(path,True)
            model=hp.Model(path,self.reader)
            model.load_materials()
            picture,stats=hp.raster(model,model.geometry(model.rest_world),0,[64,64],[8,56],np.zeros(3),32,30,1,True)
            self.assertEqual(picture.mode,"RGBA")
            self.assertEqual(stats["drawnTriangles"],1)
            self.assertEqual(stats["culledTriangles"],0)
            self.assertFalse(stats["touchesFrameBoundary"])
            self.assertGreater(stats["coveredPixels"],300)


if __name__=="__main__":
    unittest.main()
