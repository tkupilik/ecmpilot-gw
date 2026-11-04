"""Database conversion checks: no CAN/vehicle I/O."""
from pathlib import Path
import sys
import unittest

import cantools

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import generate_hscan1_dbc as g


def definition(name, length, signals, senders=None):
    return {'name': name, 'length': length, 'signals': signals,
            'senders': senders or ['BSI'], 'path': 'fixture.yml'}


class Hscan1DatabaseTest(unittest.TestCase):
    def test_default_or_invalid_only_field_is_retained_and_enum_quotes_roundtrip(self):
        defs = {0x208: definition('ENGINE_DATA', 2, {
            'UNVERIFIED_RPM': {'bits': '1.7-1.0', 'factor': 50, 'invalid': 255, 'default': 0},
            'DISPLAY_STATE': {'bits': '2.7-2.0', 'values': {0: '"N"', 1: 'D'}},
        }, ['CMM', 'E_VCU'])}
        frames = [(0, 0x208, bytes([255, 0]))]
        db, _, origins, audit = g.build([('test.csv', frames)], defs)
        loaded = cantools.database.load_string(db.as_dbc_string(), strict=True)
        message = loaded.get_message_by_frame_id(0x208)
        self.assertTrue(message.name.startswith('E_VCU_'))
        self.assertIn('UnverifiedRpm', [s.name for s in message.signals])
        self.assertEqual(message.decode(frames[0][2])['DisplayState'], "'N'")
        result, _ = g.verify(loaded, frames, origins)
        self.assertEqual(result['invalid_samples'], 1)
        self.assertTrue(all(r['status'] == 'retained_source' for r in audit))

    def test_only_provably_out_of_payload_fields_are_excluded(self):
        defs = {0x495: definition('STEERING_STATUS', 6, {
            'PREFIX_VALUE': {'bits': '1.7-2.0'},
            'DOES_NOT_FIT': {'bits': '5.7-5.0'},
        }, ['DAE'])}
        frames = [(0, 0x495, b'\x12\x34\xaa\xbb')]
        db, inventory, origins, audit = g.build([('short.csv', frames)], defs)
        msg = db.get_message_by_frame_id(0x495)
        self.assertEqual(msg.length, 4)
        self.assertEqual(msg.decode(frames[0][2])['PrefixValue'], 0x1234)
        self.assertEqual(msg.decode(frames[0][2])['RawByte3'], 0xAA)
        self.assertEqual(inventory[0]['source_match'], 'DLC_mismatch')
        self.assertEqual([a['source_signal'] for a in audit if a['status'].startswith('excluded')], ['DOES_NOT_FIT'])
        g.verify(db, frames, origins)

    def test_mux_conditions_and_unknown_branch_raw_coverage(self):
        defs = {0x600: definition('STATUS', 2, {
            'GROUP': {'bits': '1.7-1.6', 'mux_selector': True},
            'SIGNED': {'bits': '2.7-2.0', 'type': 'sint', 'factor': .5,
                       'mux': {'selector': 'GROUP', 'values': [0]}},
            'OTHER': {'bits': '2.7-2.0', 'mux': {'selector': 'GROUP', 'values': [1]}},
        })}
        frames = [(0, 0x600, b'\x00\xfe'), (1, 0x600, b'\x40\x05'), (2, 0x600, b'\xc0\x80')]
        db, _, origins, _ = g.build([('mux.csv', frames)], defs)
        self.assertEqual(db.decode_message(0x600, frames[0][2])['Signed'], -1)
        self.assertNotIn('Signed', db.decode_message(0x600, frames[1][2]))
        unknown = db.decode_message(0x600, frames[2][2])
        self.assertTrue(any(n.startswith('RawByte2') and v == 128 for n, v in unknown.items()))
        g.verify(db, frames, origins)

    def test_capture_clocks_are_independent_and_unknown_id_keeps_all_bytes(self):
        first = [(0, 0x777, b'\x10\x20'), (0.1, 0x777, b'\x30\x40')]
        second = [(0, 0x777, b'\x50\x60'), (0.2, 0x777, b'\x70\x80')]
        db, inventory, origins, _ = g.build([('a.csv', first), ('b.csv', second)], {})
        self.assertEqual(db.messages[0].name, 'Unknown_Message01')
        self.assertNotIn('777', db.messages[0].name)
        self.assertEqual(inventory[0]['count'], 4)
        self.assertEqual(inventory[0]['median_period_ms'], 200)
        for frames in (first, second):
            result, _ = g.verify(db, frames, origins)
            self.assertEqual(result['raw_field_values_checked'], 4)


if __name__ == '__main__':
    unittest.main()
