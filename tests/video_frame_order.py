import importlib.util
from pathlib import Path
import unittest
import numpy as np
path = Path(__file__).resolve().parents[1]/'tools/diagnostics/video_frame_order.py'
spec = importlib.util.spec_from_file_location('order',path)
order = importlib.util.module_from_spec(spec); spec.loader.exec_module(order)
class OrderTest(unittest.TestCase):
    def test_22_can_reverse(self):
        # Perfect 2:2 holds do not imply correct temporal content order.
        self.assertEqual(order.signature_returns([48,48,50,50,48,48]),[4])
        self.assertEqual(order.reversals([48,48,50,50,48,48]),[(4,50,48)])
    def test_real_content_match_is_unconstrained(self):
        rng=np.random.default_rng(5)
        reference=rng.integers(0,256,(6,72,96),dtype=np.uint8)
        result=order.match(reference[[1,1,4,4,2,2,3,3]],reference)
        self.assertEqual(result['confident'],8)
        self.assertEqual(result['reversal_count'],1)
        self.assertEqual(result['reversals'][0]['current']['reference_index'],2)
    def test_static_is_inconclusive(self):
        static=np.zeros((4,72,96),np.uint8)
        self.assertEqual(order.match(static,static)['confident'],0)
    def test_forward_repeats_allowed(self):
        self.assertEqual(order.reversals([0,0,1,1,3,3]),[])
        self.assertEqual(order.signature_returns([0,0,1,1,3,3]),[])
if __name__=='__main__': unittest.main()
