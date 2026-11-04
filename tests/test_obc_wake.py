"""Check conclusions against the original passive captures and current INFO DBC."""
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from analyze_obc_wake import analyze


class ObcWakeCaptureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = analyze()

    def test_same_seventeen_added_vs_standby_and_operational_drive(self):
        report = self.report
        self.assertEqual(report['comparisons']['info_standby.csv']['added_ids'],
                         report['comparisons']['info_start_jizda.csv']['added_ids'])
        self.assertEqual(17, len(report['comparisons']['info_standby.csv']['added_ids']))
        self.assertEqual(10, len(report['new_vs_all_older']))
        old = [r['id'] for r in report['added_id_details'] if r['older_captures']]
        self.assertEqual(['01C','412','5C1','5D2','5E4','5FB','5FC'], old)

    def test_bsi_announces_cause_then_reaches_normal_without_an_observed_request_ack(self):
        report = self.report
        self.assertEqual([2,3,5,6,7], [f['line'] for f in report['wake_frames']])
        self.assertEqual({'00 20 00 00 00 00 00 00'}, {f['payload'] for f in report['wake_frames']})
        self.assertEqual([3,1], [f['power_management'] for f in report['bsi_phase_changes']])
        self.assertEqual(54.750, report['wake_to_normal_ms'])
        self.assertEqual(34.676, report['bsi_phase3_to_phase1_ms'])
        self.assertEqual([], report['upstream_wake_request_ids_observed'])
        self.assertFalse(report['direct_obc_request_observed'])
        self.assertFalse(report['explicit_transaction_ack_observed'])

    def test_exactly_nine_ids_acquire_cluster_source_without_claiming_4a4_or_bsi(self):
        reviewed = [row for row in self.report['added_id_details'] if row['cluster_attribution']]
        self.assertEqual(9, len(reviewed))
        for row in reviewed:
            self.assertTrue(row['name'].startswith('IC_'))
            self.assertEqual(['IC'], row['dbc_senders'])
            self.assertIn('CMB', row['upstream_senders'])
        other = {row['id']:row for row in self.report['added_id_details'] if not row['cluster_attribution']}
        self.assertEqual(['BSI'], other['412']['dbc_senders'])
        self.assertFalse(other['4A4']['name'].startswith('IC_'))


if __name__ == '__main__':
    unittest.main()
