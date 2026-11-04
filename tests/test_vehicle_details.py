"""Real INFO decoder -> native varstore -> bounded passive details datagram."""
import ctypes as c
import unittest
from test_udp import Codec


class VehicleDetailsTests(unittest.TestCase):
    def setUp(self):
        self.g = Codec('udp-info-test.dll')
        self.g.lib.gw_can_info_decode.argtypes = [c.c_uint32, c.c_void_p, c.c_size_t, c.c_bool, c.c_bool]
        self.g.lib.gw_can_info_init()

    def frame(self, ident, data):
        return self.g.lib.gw_can_info_decode(ident, bytes(data), len(data), False, False)

    def details(self):
        result = self.g.call(self.g.request('vehicle.details.get', {}))
        self.assertIsNotNone(result)
        return result['details']

    def test_doors_individually_and_unknown_at_start(self):
        self.assertEqual(self.details()['body']['known_mask'], 0)
        for i, (byte, bit) in enumerate([(5,6),(7,1),(7,4),(7,5),(5,7)]):
            data = [0]*8; data[byte] = 1 << bit
            self.assertEqual(self.frame(0x236, data), 1)
            body = self.details()['body']
            self.assertEqual(body['known_mask'], 31)
            self.assertEqual(body['open_mask'], 1 << i)
        self.assertEqual(self.frame(0x236, bytes(7)), -1)
        self.assertEqual(self.details()['body']['open_mask'], 16)

    def test_locks_reserved_values_and_independent_age(self):
        self.frame(0x236, bytes(8)); self.g.advance(1000)
        for state in range(8):
            self.frame(0x276, [0, state << 5, 0, 0, 0, 0, 0, 0])
            body = self.details()['body']
            self.assertEqual(body['locking_state'], state if 1 <= state <= 6 else None)
            self.assertEqual(body['age_ms'], 1000)
            self.assertEqual(body['lock_age_ms'], 0 if 1 <= state <= 6 else None)

    def test_capture_trip_values_and_fuel_fields_not_used(self):
        self.frame(0x31A, bytes.fromhex('27 00 6B B3 FF E0 05 A8'))
        self.frame(0x2A1, bytes.fromhex('48 00 A4 FF FF FF FF'))
        self.frame(0x261, bytes.fromhex('58 08 B8 FF FF FF FF'))
        self.frame(0x39A, bytes.fromhex('CB 2C B2 C9 5F F8 00 00'))
        trips = self.details()['trips']
        self.assertEqual(trips['avg1']['value'], 21.5)
        self.assertEqual(trips['avg2']['value'], 20.7)
        self.assertIsNone(trips['instant']['value'])
        self.assertEqual(trips['distance1']['value'], 164)
        self.assertEqual(trips['distance2']['value'], 2232)
        self.assertEqual(trips['speed1']['value'], 72)
        self.assertEqual(trips['speed2']['value'], 88)
        raw = int.from_bytes(bytes.fromhex('CB 2C B2 C9 5F F8 00 00'), 'big')
        expected = [(raw >> (64-(i+1)*6)) & 63 for i in range(6)]
        self.assertEqual(trips['history'], [v if v <= 50 else None for v in expected])

    def test_stale_values_stay_latched_and_reset_makes_them_unknown(self):
        self.frame(0x236, [0,0,0,0,0,0x40,0,0])
        self.g.advance(60000)
        self.assertEqual(self.details()['body']['open_mask'], 1)
        self.assertEqual(self.details()['body']['age_ms'], 60000)
        self.g.lib.gw_can_info_reset()
        self.assertEqual(self.details()['body']['known_mask'], 0)

    def test_full_snapshot_fits_and_read_never_requests_can(self):
        self.frame(0x236, [255]*8); self.frame(0x276, [0,0x20,0,0,0,0,0,0])
        for name in self.g.ids:
            if name.startswith('bsi.trip') or name.startswith('bsi.consumption.'):
                self.g.set(name, 500, force=False)
        self.g.advance(4_000_000_000)
        raw = self.g.raw(__import__('json').dumps(self.g.request('vehicle.details.get', {})).encode())
        self.assertIsNotNone(raw); self.assertLessEqual(len(raw), 1024)
        self.assertIn(self.g.read('control.tx.pending'), (-999, 0))
        self.assertIn(self.g.read('control.phase'), (-999, 0))
        self.assertIn('vehicle_details', self.g.call(self.g.request('info.get', {}))['info']['fields'])


if __name__ == '__main__':
    unittest.main()
