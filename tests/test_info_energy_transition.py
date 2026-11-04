"""Check the offline contrast search against planted and absent signals."""
import collections
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from analyze_info_energy_transition import search


class EnergyTransitionTests(unittest.TestCase):
    def run_search(self, a, b):
        return search({0x123: collections.Counter({a: 10})},
                      {0x123: collections.Counter({b: 10})}, {})['matches']

    def test_packed_hscan_style_11_bits_32_wh(self):
        matches = self.run_search((96 << 5).to_bytes(2, 'big'), (621 << 5).to_bytes(2, 'big'))
        self.assertTrue(any(m['endian'] == 'BE' and m['start'] == 0 and m['width'] == 11
                            and m['scale_kwh'] == .032 for m in matches))

    def test_diagnostic_style_16_bits_div64(self):
        matches = self.run_search((192).to_bytes(2, 'big'), (1272).to_bytes(2, 'big'))
        self.assertTrue(any(m['endian'] == 'BE' and m['start'] == 0 and m['width'] == 16
                            and m['scale_kwh'] == 1 / 64 for m in matches))

    def test_little_endian_wh(self):
        matches = self.run_search((3000).to_bytes(4, 'little'), (19880).to_bytes(4, 'little'))
        self.assertTrue(any(m['endian'] == 'LE' and m['start'] == 0 and m['width'] == 32
                            and m['scale_kwh'] == .001 for m in matches))

    def test_static_match_does_not_create_transition(self):
        value = (1272).to_bytes(2, 'big')
        self.assertEqual(self.run_search(value, value), [])

    def test_single_event_is_kept_but_sample_count_is_explicit(self):
        result = search({0x123: collections.Counter({bytes.fromhex('00 c0'): 1})},
                        {0x123: collections.Counter({bytes.fromhex('04 f8'): 1})}, {})
        self.assertTrue(result['matches'])
        self.assertTrue(all(m['counts'] == [1, 1] for m in result['matches']))


if __name__ == '__main__':
    unittest.main()
