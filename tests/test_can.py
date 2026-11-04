"""Actual RX-to-varstore publication code, independent of physical TWAI."""
import ctypes as c
import unittest
from test_udp import Codec


class Frame(c.Structure):
    _fields_ = [('id', c.c_uint32), ('dlc', c.c_uint8), ('extended', c.c_bool),
                ('rtr', c.c_bool), ('data', c.c_uint8 * 8)]


class Health(c.Structure):
    _fields_ = [('state', c.c_int), ('error', c.c_int32), ('missed', c.c_uint32),
                ('overrun', c.c_uint32), ('bus_errors', c.c_uint32),
                ('rx_errors', c.c_uint32), ('pending', c.c_uint32)]


class CanRxTests(unittest.TestCase):
    def setUp(self):
        self.g = Codec()
        self.lib = self.g.lib
        self.lib.gw_can_rx_accept.argtypes = [c.POINTER(Frame)]
        self.lib.gw_can_rx_publish_health.argtypes = [c.POINTER(Health)]
        self.lib.gw_can_rx_data_init()

    def publish_health(self):
        self.lib.gw_can_rx_publish_health(c.byref(Health(2, 0, 3, 4, 5, 6, 7)))

    def frame(self, identifier=0x123, data=bytes(range(8)), extended=False, rtr=False):
        f = Frame(id=identifier, dlc=len(data), extended=extended, rtr=rtr)
        f.data[:len(data)] = data
        self.lib.gw_can_rx_accept(c.byref(f))

    def test_startup_and_health_do_not_invent_vehicle_observations(self):
        self.assertEqual(1, self.g.read('can.state'))
        self.assertEqual(-999, self.g.read('can.rx.last.id'))
        self.publish_health()
        self.assertEqual(2, self.g.read('can.state'))
        self.assertEqual(3, self.g.read('can.rx.missed_count'))
        self.assertEqual(4, self.g.read('can.rx.overrun_count'))
        self.assertEqual(5, self.g.read('can.bus_error_count'))
        for name in ['vehicle.awake', 'battery.soc_pct_x10', 'climate.state']:
            self.assertEqual(-999, self.g.read(name), name)
        self.assertEqual(0, self.g.read('request.climate.generation'))

    def test_data_publication_and_short_frame_clear_unused_bytes(self):
        self.frame()
        self.assertEqual(0x123, self.g.read('can.rx.last.id'))
        self.assertEqual(2, self.g.read('can.rx.last.generation'))
        self.assertEqual(list(range(8)), [self.g.read(f'can.rx.last.data{i}') for i in range(8)])
        self.frame(0x1abcde, b'\xaa\xbb', True)
        self.assertEqual(1, self.g.read('can.rx.last.extended'))
        self.assertEqual(4, self.g.read('can.rx.last.generation'))
        self.assertEqual([170, 187, 0, 0, 0, 0, 0, 0], [self.g.read(f'can.rx.last.data{i}') for i in range(8)])
        self.publish_health(); self.assertEqual(2, self.g.read('can.rx.count'))

    def test_rtr_and_invalid_frames_never_publish_payload(self):
        self.frame(); self.frame(rtr=True)
        self.frame(0x800); self.frame(0x20000000, extended=True)
        invalid = Frame(id=1, dlc=9); self.lib.gw_can_rx_accept(c.byref(invalid))
        self.lib.gw_can_rx_accept(None)
        self.publish_health()
        self.assertEqual(2, self.g.read('can.rx.count'))
        self.assertEqual(1, self.g.read('can.rx.rtr_count'))
        self.assertEqual(4, self.g.read('can.rx.rejected_count'))
        self.assertEqual(2, self.g.read('can.rx.last.generation'))

    def test_health_poll_does_not_refresh_a_stale_frame(self):
        self.frame(); self.lib.gw_test_set_time(7000); self.publish_health()
        self.assertEqual(-1, self.lib.varstore_read(self.g.ids['can.rx.last.id'], 5000, -1))
        self.assertEqual(2, self.lib.varstore_read(self.g.ids['can.state'], 2000, -1))

    def test_force_preserves_producer_updates_and_auto_signals_are_not_manual(self):
        self.frame(); signal = self.g.ids['can.rx.last.id']
        self.lib.varstore_force(signal, True, 999)
        self.frame(0x456)
        self.assertEqual(999, self.g.read('can.rx.last.id'))
        self.lib.varstore_force(signal, False, 0)
        self.assertEqual(0x456, self.g.read('can.rx.last.id'))
        self.lib.gw_process_data_manual.argtypes = [c.c_size_t]
        self.lib.gw_process_data_manual.restype = c.c_bool
        self.assertFalse(self.lib.gw_process_data_manual(signal))
