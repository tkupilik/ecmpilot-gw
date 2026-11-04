"""Passive search regression checks using independent planted bit fields."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_info_charging import candidates, float_candidates


def rows(early, late):
    return {0x123: [(10, {'data': early}), (490, {'data': late})]}


class ChargingEnergySearchTests(unittest.TestCase):
    def test_32wh_packed_step(self):
        report=candidates(rows((616<<5).to_bytes(2,'big'),(627<<5).to_bytes(2,'big')))
        self.assertTrue(any(m['endian']=='BE' and m['linear_start']==0 and
                            m['width']==11 and m['scale_kwh']==.032
                            for m in report['growth']))

    def test_little_endian_wh_step(self):
        report=candidates(rows((19700).to_bytes(4,'little'),(20050).to_bytes(4,'little')))
        self.assertTrue(any(m['endian']=='LE' and m['linear_start']==0 and
                            m['width']==32 and m['scale_kwh']==.001
                            for m in report['growth']))

    def test_constant_numeric_energy_is_not_growth(self):
        raw=(20050).to_bytes(4,'little')
        self.assertFalse(candidates(rows(raw,raw))['growth'])

    def test_capacity_match_is_separate_from_growth(self):
        raw=(32290).to_bytes(4,'big')
        report=candidates(rows(raw,raw))
        self.assertFalse(report['growth'])
        self.assertTrue(any(m['endian']=='BE' and m['linear_start']==0 and
                            m['width']==32 and m['scale_kwh']==.001
                            for m in report['constant_capacity_numeric_matches']))

    def test_float32_energy_and_capacity(self):
        report=float_candidates(rows(struct.pack('<f',19.7),struct.pack('<f',20.05)))
        self.assertTrue(any(m['endian']=='LE' and m['growing'] for m in report))
        raw=struct.pack('>f',43.05333)
        report=float_candidates(rows(raw,raw))
        self.assertTrue(any(m['endian']=='BE' and not m['growing'] for m in report))

    def test_window_support_not_replaced_by_a_rare_numeric_hit(self):
        grouped=rows(bytes(4),(20050).to_bytes(4,'little'))
        grouped[0x123]+=[(490+i/10,{'data':bytes(4)}) for i in range(20)]
        self.assertFalse(candidates(grouped)['growth'])


if __name__=='__main__': unittest.main()
