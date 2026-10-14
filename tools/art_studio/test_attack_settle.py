import unittest
import copy
import numpy as np
from attack_settle import settle

class RecoveryTests(unittest.TestCase):
    def test_endpoints_preserve_scale_and_source(self):
        pose=[{'rotation':[0,0,.7071067811865476,.7071067811865476],'translation':[0,.1,0],'scale':[1,1,1]}]
        guard=[{'rotation':[0,0,0,1],'translation':[0,0,0],'scale':[1,1,1]}]
        before=copy.deepcopy(pose)
        np.testing.assert_allclose(settle(pose,guard,0)[0]['rotation'],pose[0]['rotation'],atol=1e-12)
        np.testing.assert_allclose(settle(pose,guard,1)[0]['rotation'],guard[0]['rotation'],atol=1e-12)
        self.assertEqual(settle(pose,guard,.5)[0]['scale'],[1,1,1]);self.assertEqual(pose,before)

    def test_antipodal_quaternions_do_not_spin(self):
        pose=[{'rotation':[0,0,0,1]}];guard=[{'rotation':[0,0,0,-1]}]
        mid=settle(pose,guard,.5)[0]['rotation']
        self.assertAlmostEqual(abs(mid[3]),1)
        np.testing.assert_allclose(mid[:3],[0,0,0],atol=1e-12)
        with self.assertRaises(ValueError):settle(pose,guard,1.1)

if __name__=='__main__':unittest.main()
