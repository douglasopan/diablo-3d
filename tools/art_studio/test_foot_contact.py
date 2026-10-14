import unittest
from types import SimpleNamespace
import numpy as np
import tempfile
from pathlib import Path
from foot_contact import points, skin_semantics, rt, hp

class SkinContactTests(unittest.TestCase):
    def test_world_translation_and_blended_influences(self):
        first=np.eye(4);first[:3,3]=[2,3,4]
        second=np.eye(4);second[:3,3]=[-2,1,0]
        model=SimpleNamespace(skins=[(np.array([0,1]),np.array([np.eye(4),np.eye(4)]))])
        primitive={'skin':0,'positions':np.array([[1.,0.,0.],[0.,-1.,2.]]),'pairs':[(np.array([[0,1,0,0],[1,0,0,0]]),np.array([[.75,.25,0,0],[1,0,0,0]]))]}
        actual=points(model,primitive,[first,second],np.array([0,1]))
        np.testing.assert_allclose(actual,[[2,2.5,3],[-2,0,2]],atol=1e-12)

    def test_inverse_bind_cancels_bind_translation(self):
        bind=np.eye(4);bind[:3,3]=[9,4,-3]
        model=SimpleNamespace(skins=[(np.array([0]),np.array([np.linalg.inv(bind)]))])
        primitive={'skin':0,'positions':np.array([[.2,0.,.8]]),'pairs':[(np.zeros((1,4),dtype=int),np.array([[1.,0,0,0]]))]}
        np.testing.assert_allclose(points(model,primitive,[bind],np.array([0])),primitive['positions'],atol=1e-12)

    def test_export_excludes_unweighted_terminal_without_rotation(self):
        nodes=[{'name':'mixamorig:RightFoot','rotation':[0,0,0,1]}, {'name':'mixamorig:RightFoot_End'}]
        target=SimpleNamespace(nodes=nodes,skins=[(np.array([0]),np.array([np.eye(4)]))],path=Path('fixture.glb'))
        document={'asset':{'version':'2.0'},'nodes':nodes,'buffers':[{'byteLength':4}]}
        reader=SimpleNamespace(read_glb=lambda path:(document,b'TEST',None))
        poses=[[dict(n) for n in nodes],[dict(n) for n in nodes]]
        previous=rt.SEMANTICS
        try:
            rt.SEMANTICS=skin_semantics(target)
            with tempfile.TemporaryDirectory() as td:
                path=Path(td)/'output.glb'
                original,animation=rt.append_animation(target,reader,np.array([0,1]),poses,'regression',{},path)
                self.assertEqual(original,b'TEST')
                self.assertEqual([c['target']['node'] for c in animation['channels']],[0])
                actual_reader,_=hp.load_reader(hp.ROOT)
                doc,blob,_=actual_reader.read_glb(path)
                self.assertEqual(doc['nodes'],nodes)
                self.assertEqual(blob[:4],b'TEST')
        finally:rt.SEMANTICS=previous

if __name__=='__main__':unittest.main()
