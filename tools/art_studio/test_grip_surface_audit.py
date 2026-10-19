import unittest
import numpy as np
from grip_surface_audit import distances


class SurfaceDistanceTests(unittest.TestCase):
    def test_projection_edges_vertices_and_degenerate_face(self):
        faces=np.array([[[0,0,0],[2,0,0],[0,2,0]], [[0,0,0],[0,0,0],[0,0,0]]],dtype=float)
        points=np.array([[.5,.5,3],[1.5,1.5,0],[-1,-1,0],[.5,.5,-3]],dtype=float)
        np.testing.assert_allclose(distances(points,faces),[3,np.sqrt(.5),np.sqrt(2),3],atol=1e-12)
        with self.assertRaises(ValueError):distances(points,faces[1:])

    def test_rigid_transform_and_uniform_scale(self):
        faces=np.array([[[0,0,0],[1,0,0],[0,1,0]]],dtype=float)
        points=np.array([[.2,.2,1],[-.5,0,0]],dtype=float)
        rotation=np.array([[0,0,1],[1,0,0],[0,1,0]],dtype=float)
        shift=np.array([4,-3,2])
        np.testing.assert_allclose(distances(points@rotation*2+shift,faces@rotation*2+shift),
                                   distances(points,faces)*2,atol=1e-12)


if __name__=='__main__':unittest.main()
