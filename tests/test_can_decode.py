"""Actual CAN -> varstore -> UDP path, with captured payloads and edge cases."""
import ctypes as c
import unittest
from test_can import Frame
from test_udp import Codec


class CanDecodeTests(unittest.TestCase):
    def setUp(self):
        self.g = Codec()
        self.lib = self.g.lib
        self.lib.gw_can_rx_accept.argtypes = [c.POINTER(Frame)]
        self.lib.gw_process_data_manual.argtypes = [c.c_size_t]
        self.lib.gw_process_data_manual.restype = c.c_bool
        self.lib.gw_can_rx_data_init()

    def frame(self, identifier, payload, *, extended=False, rtr=False):
        data = bytes.fromhex(payload) if isinstance(payload, str) else payload
        frame = Frame(id=identifier, dlc=len(data), extended=extended, rtr=rtr)
        frame.data[:len(data)] = data
        self.lib.gw_can_rx_accept(c.byref(frame))

    def read(self, name, max_age=0):
        return self.lib.varstore_read(self.g.ids[name], max_age, -99999)

    def state(self):
        return self.g.call(self.g.request('state.get', {}))['state']

    def test_recorded_ready_and_wake_soc_reach_actual_udp_without_test_mode(self):
        self.frame(0x5C3, 'CF 80 32 C3 00 07 D0 00')
        self.assertEqual(707, self.read('battery.soc_pct_x10'))
        self.assertEqual(415000, self.read('battery.voltage_mv'))
        state = self.state()
        self.assertEqual({'value': 70.7, 'age_ms': 0}, state['soc_pct'])
        self.assertEqual({'value': 415, 'age_ms': 0}, state['voltage_v'])
        self.assertIsNone(state['cabin_c']['value'])
        self.assertEqual('unknown', state['climate']['state'])
        self.frame(0x5C3, 'D0 00 02 C1 00 07 D0 00')
        self.assertEqual(705, self.read('battery.soc_pct_x10'))
        self.assertEqual(416000, self.read('battery.voltage_mv'))
        info_request = self.g.request('info.get', {}); info_request['gw'] = None
        info = self.g.call(info_request)['info']
        self.assertEqual('none', info['control_backend'])
        self.assertEqual({}, info['operations'])

    def test_zero_startup_and_unavailable_pack_never_mean_empty_battery(self):
        self.frame(0x5C3, bytes(8))
        self.assertIsNone(self.state()['soc_pct']['value'])
        self.frame(0x5C3, 'D0 00 02 C1 00 07 D0 00')
        self.frame(0x5C3, bytes(8))
        self.assertEqual(-99999, self.read('battery.soc_pct_x10'))
        self.assertIsNone(self.state()['voltage_v']['value'])
        self.frame(0x5C3, 'FF E0 02 C1 00 07 D0 00')
        self.assertIsNone(self.state()['soc_pct']['value'])

    def test_soc_extremes_invalid_codes_and_exact_millivolts(self):
        self.frame(0x5C3, 'D0 20 00 00 00 00 00 00')
        self.assertEqual(0, self.read('battery.soc_pct_x10'))
        self.assertEqual(416250, self.read('battery.voltage_mv'))
        self.assertEqual(416.25, self.state()['voltage_v']['value'])
        self.frame(0x5C3, 'D0 00 03 E8 00 00 00 00')
        self.assertEqual(1000, self.read('battery.soc_pct_x10'))
        for low in (0xE9, 0xFE, 0xFF):
            self.frame(0x5C3, bytes([0xD0, 0, 3, low, 0, 0, 0, 0]))
            self.assertEqual(-99999, self.read('battery.soc_pct_x10'))
            self.assertEqual(416000, self.read('battery.voltage_mv'))

    def test_wrong_identifier_format_rtr_dlc_and_unrelated_traffic_do_not_refresh(self):
        self.frame(0x5C3, 'D0 00 02 C1 00 07 D0 00')
        self.lib.gw_test_set_time(7001)
        for identifier, data, flags in [
            (0x123, bytes(8), {}),
            (0x5C3, bytes(8), {'extended': True}),
            (0x5C3, bytes(8), {'rtr': True}),
            (0x5C3, bytes(7), {}),
        ]:
            self.frame(identifier, data, **flags)
        self.assertEqual(-99999, self.read('battery.soc_pct_x10', 5000))
        self.assertEqual({'value': 70.5, 'age_ms': 6001}, self.state()['soc_pct'])

    def test_force_keeps_real_can_producer_and_live_fields_are_not_manual(self):
        for name in ('battery.soc_pct_x10', 'battery.voltage_mv'):
            self.assertFalse(self.lib.gw_process_data_manual(self.g.ids[name]))
        self.g.set('battery.soc_pct_x10', 500)
        self.frame(0x5C3, 'D0 00 02 C1 00 07 D0 00')
        self.assertEqual(50, self.state()['soc_pct']['value'])
        self.lib.varstore_force(self.g.ids['battery.soc_pct_x10'], False, 0)
        self.assertEqual(70.5, self.state()['soc_pct']['value'])

    def test_recorded_hvac_stays_in_candidate_signals_not_phone_cabin_or_state(self):
        self.frame(0x3CE, '40 60 A8 5E C2 0A 23 80')
        expected = {'upstream_mc': 8000, 'heater_outlet_mc': 44000,
                    'heater_setpoint_mc': 47000, 'evaporator_setpoint_mc': 12000,
                    'evaporator_mc': 12200, 'airflow_gps_x10': 355}
        for name, value in expected.items():
            self.assertEqual(value, self.read('can.hvac.' + name), name)
        self.frame(0x56E, 'F4 01 7B 00 00 00')
        self.assertEqual(37900, self.read('can.hvac.compressor_current_ma'))
        self.frame(0x412, '10 08 00 00 00 3C 00 00')
        self.assertEqual(1, self.read('can.hvac.heating_request_raw'))
        self.frame(0x5A2, bytes(7))
        self.assertEqual(-99999, self.read('can.hvac.preconditioning_state_raw'))
        self.frame(0x5A2, '28 3C 20 FE 01 31 62')
        self.assertEqual(0, self.read('can.hvac.preconditioning_state_raw'))
        self.frame(0x5A2, '28 3C 20 FF 01 31 62')
        self.assertEqual(1, self.read('can.hvac.preconditioning_state_raw'))
        self.assertIsNone(self.state()['cabin_c']['value'])
        self.assertEqual('unknown', self.state()['climate']['state'])

    def test_candidate_sentinels_invalidate_instead_of_refreshing_a_fake_number(self):
        self.frame(0x3CE, '40 60 A8 5E C2 0A 23 80')
        self.frame(0x3CE, 'FF FF FF FF FF FF FF FF')
        for name in ('upstream_mc', 'heater_outlet_mc', 'heater_setpoint_mc',
                     'evaporator_setpoint_mc', 'evaporator_mc', 'airflow_gps_x10'):
            self.assertEqual(-99999, self.read('can.hvac.' + name), name)
        self.frame(0x56E, 'F4 01 FF 00 00 00')
        self.assertEqual(-99999, self.read('can.hvac.compressor_current_ma'))

    def test_outside_and_odometer_are_explicit_candidates_with_validity_checks(self):
        self.frame(0x5B2, '00 00 60 80 00 02 00 00')
        self.assertEqual(8000, self.read('can.hvac.outside_mc_candidate'))
        self.frame(0x552, '05 75 CC A5 00 E6 70 FE')
        self.assertEqual(58992, self.read('can.vehicle.odometer_km_candidate'))
        self.assertIsNone(self.state()['outside_c']['value'])
        self.assertIsNone(self.state()['odometer_km']['value'])
        self.frame(0x5B2, '00 00 60 80 00 02 40 00')
        self.assertEqual(-99999, self.read('can.hvac.outside_mc_candidate'))
        self.frame(0x5B2, '00 00 FF 80 00 02 00 00')
        self.assertEqual(-99999, self.read('can.hvac.outside_mc_candidate'))
        self.frame(0x552, '05 75 CC A5 FF FF FF FE')
        self.assertEqual(-99999, self.read('can.vehicle.odometer_km_candidate'))
