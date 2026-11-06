"""LCD parity through real GW CAN handlers, derived timestamps, UDP and HA JSON."""
import csv
import ctypes as c
import json
import unittest
import cantools
from jinja2 import Environment, StrictUndefined
from test_udp import Codec, ROOT


class TelemetryParityTests(unittest.TestCase):
    def setUp(self):
        self.g = Codec('udp-info-test.dll')
        self.lib = self.g.lib
        self.lib.gw_vehicle_init()
        self.lib.gw_can_info_decode.argtypes = [c.c_uint32, c.c_void_p, c.c_size_t, c.c_bool, c.c_bool]
        self.lib.gw_can_info_init()
        self.lib.gw_ha_state_json.restype = c.c_void_p
        self.lib.gw_ha_diagnostics_json.restype = c.c_void_p
        self.lib.gw_ha_diagnostics_due.argtypes = [c.c_uint64, c.c_uint64, c.c_bool]
        self.lib.gw_ha_diagnostics_due.restype = c.c_bool
        self.lib.cJSON_free.argtypes = [c.c_void_p]
        self.lib.gw_ha_entity_count.restype = c.c_size_t
        self.lib.gw_ha_entity_key.argtypes = [c.c_size_t]
        self.lib.gw_ha_entity_key.restype = c.c_char_p
        self.lib.gw_ha_discovery_json.argtypes = [c.c_size_t] + [c.c_char_p]*5 + [c.c_uint]
        self.lib.gw_ha_discovery_json.restype = c.c_void_p

    def frame(self, ident, data):
        return self.lib.gw_can_info_decode(ident, bytes(data), len(data), False, False)

    def value(self, name):
        return self.lib.varstore_read(self.g.ids[name], 0, -99999)

    def parse_owned(self, pointer):
        self.assertTrue(pointer)
        try:
            return json.loads(c.string_at(pointer))
        finally:
            self.lib.cJSON_free(pointer)

    def climate(self, left=9, right=11):
        # Center21C and relative350 codes:9=X-1,11=X; evap raw455=5.5C.
        self.frame(0x2AD, [0, 0, 0, 0, 0, 0, 0, 0])
        # Independent raw mapping for center is checked against DBC below;
        # use a producer write here to isolate the derived process.
        self.lib.varstore_write(self.g.ids['bsi.climate_center_setpoint_mc'], 21000)
        self.frame(0x350, [0, 1, 199, left, right, 0, 0, 0])
        self.lib.gw_vehicle_tick()

    def test_12v_battery_example_and_phone_units(self):
        self.assertEqual(1, self.frame(0x396, bytes.fromhex('00 8E A3 00 C8 09')))
        self.assertEqual(12600, self.value('bsi.aux_battery_voltage_mv'))
        self.assertEqual(815, self.value('bsi.aux_battery_soc_pct_x10'))
        state = self.g.call(self.g.request('state.get', {}))['state']
        self.assertEqual(12.6, state['aux_battery_voltage_v']['value'])
        self.assertEqual(81.5, state['aux_battery_soc_pct']['value'])
        self.assertIsNone(state['voltage_v']['value'])  # Never overwrite HV voltage.

    def test_climate_derived_age_force_and_non_numeric_codes(self):
        self.climate()
        state = self.g.call(self.g.request('state.get', {}))['state']
        self.assertEqual(20, state['climate_left_setpoint_c']['value'])
        self.assertEqual(21, state['climate_right_setpoint_c']['value'])
        self.assertEqual(5.5, state['evaporator_c']['value'])
        self.assertIsNone(state['cabin_c']['value'])
        self.g.advance(500)
        self.frame(0x350, [0, 1, 199, 9, 11, 0, 0, 0])  # New code must not renew the older reference.
        self.lib.gw_vehicle_tick()
        state = self.g.call(self.g.request('state.get', {}))['state']
        self.assertEqual(500, state['climate_left_setpoint_c']['age_ms'])
        self.g.advance(2501)
        self.lib.gw_vehicle_tick()
        self.assertEqual(20000, self.value('climate.left_setpoint_mc'))
        self.g.advance(60000); self.lib.gw_vehicle_tick()
        cached = self.g.call(self.g.request('state.get', {}))['state']['climate_left_setpoint_c']
        self.assertEqual(20, cached['value']); self.assertEqual(63001, cached['age_ms'])
        self.lib.gw_vehicle_tick()
        self.assertEqual(cached, self.g.call(self.g.request('state.get', {}))['state']['climate_left_setpoint_c'])
        self.lib.varstore_force(self.g.ids['bsi.climate_center_setpoint_mc'], True, 23000)
        self.lib.varstore_force(self.g.ids['icsv.climate_left_temp_code'], True, 11)
        self.lib.gw_vehicle_tick()
        self.assertEqual(23000, self.value('climate.left_setpoint_mc'))
        self.lib.varstore_force(self.g.ids['icsv.climate_left_temp_code'], True, 22)
        self.lib.gw_vehicle_tick()
        self.assertEqual(-99999, self.value('climate.left_setpoint_mc'))

    def test_bsi_power_management_has_its_own_can_age_and_keeps_last_actual_phase(self):
        for pm in range(5):
            self.frame(0x036, bytes([0,0,0,0,pm,0,0,0]))
            state=self.g.call(self.g.request('state.get', {}))['state']
            self.assertEqual({'power_management':pm,'age_ms':0},state['bsi'])
            self.assertIsNone(state['vehicle']['age_ms'])  # No parked/gear frame received.
            self.g.advance(6000)
            state=self.g.call(self.g.request('state.get', {}))['state']
            self.assertEqual({'power_management':pm,'age_ms':6000},state['bsi'])
        self.frame(0x036, bytes([0,0,0,0,7,0,0,0]))
        self.assertNotIn('bsi',self.g.call(self.g.request('state.get', {}))['state'])

    def test_capture_climate_and_12v_against_independent_dbc(self):
        db = cantools.database.load_file(ROOT/'dbc/opel_corsa_e_info.dbc', encoding='utf-8')
        count = 0
        for path in (ROOT/'logs').glob('info*.csv'):
            for row in csv.DictReader(path.read_text(encoding='utf-8-sig').splitlines()):
                ident = int(row['ID'], 16)
                if ident not in (0x396, 0x350, 0x2AD):
                    continue
                data = bytes(int(row[f'D{i}'], 16) for i in range(1, int(row['LEN'])+1))
                self.assertEqual(1, self.frame(ident, data)); count += 1
                decoded = db.get_message_by_frame_id(ident).decode(data, decode_choices=False)
                if ident == 0x396:
                    self.assertEqual(round(decoded['AuxBatteryVoltage']*1000) if data[1] >> 1 <= 126 else -99999, self.value('bsi.aux_battery_voltage_mv'))
                    self.assertEqual(round(decoded['AuxBatterySoc']*10) if data[2] <= 200 else -99999, self.value('bsi.aux_battery_soc_pct_x10'))
                elif ident == 0x350:
                    self.assertEqual(decoded['FrontLeftTempSetpointValue'], self.value('icsv.climate_left_temp_code'))
                    self.assertEqual(decoded['FrontRightTempSetpointValue'], self.value('icsv.climate_right_temp_code'))
                else:
                    self.assertEqual(18000 + decoded['CenterTemperatureSetpoint']*1000, self.value('bsi.climate_center_setpoint_mc'))
        self.assertGreater(count, 700)

    def test_ha_diagnostics_bsi_rx_interrupts_idle_wait_and_batches_at_one_hz(self):
        due = lambda last, sent=True: self.lib.gw_ha_diagnostics_due(self.g.now, last, sent)
        last = self.g.now
        self.assertTrue(due(last, False))  # First publication/reconnect.
        self.g.advance(30000)
        self.assertFalse(due(last))
        self.frame(0x036, bytes(8))  # Standby still means BSI is transmitting.
        self.assertTrue(due(last))  # Do not finish the old minute-long wait.
        last = self.g.now
        for _ in range(9):
            self.g.advance(100)
            self.frame(0x036, bytes(8))  # Identical frames are real observations.
            self.assertFalse(due(last))
        self.g.advance(100)
        self.frame(0x036, bytes(8))
        self.assertTrue(due(last))

    def test_ha_diagnostics_all_bsi_contents_are_active_then_silence_is_slow(self):
        last = self.g.now
        for phase in range(8):
            self.g.advance(1000)
            self.frame(0x036, bytes([0, 0, 0, 0, phase, 0, 0, 0]))
            self.assertTrue(self.lib.gw_ha_diagnostics_due(self.g.now, last, True))
            last = self.g.now
        self.g.advance(2000)
        self.assertTrue(self.lib.gw_ha_diagnostics_due(self.g.now, last, True))
        self.g.advance(1)
        self.assertFalse(self.lib.gw_ha_diagnostics_due(self.g.now, last, True))
        self.g.advance(57998)
        self.assertFalse(self.lib.gw_ha_diagnostics_due(self.g.now, last, True))
        self.g.advance(1)
        self.assertTrue(self.lib.gw_ha_diagnostics_due(self.g.now, last, True))

    def test_ha_diagnostics_rejects_fake_activity_and_decoder_reset(self):
        last = self.g.now
        self.g.advance(3000)
        self.g.set('bsi.power_management', 1)
        self.g.set('bsi.status_rx_generation', 123)
        self.assertFalse(self.lib.gw_ha_diagnostics_due(self.g.now, last, True))
        self.assertEqual(-1, self.frame(0x036, bytes(7)))
        for extended, rtr in ((True, False), (False, True)):
            self.assertEqual(-1, self.lib.gw_can_info_decode(0x036, bytes(8), 8, extended, rtr))
        self.frame(0x396, bytes.fromhex('00 8E A3 00 C8 09'))
        self.assertFalse(self.lib.gw_ha_diagnostics_due(self.g.now, last, True))
        self.frame(0x036, bytes(8))
        self.assertTrue(self.lib.gw_ha_diagnostics_due(self.g.now, last, True))
        self.lib.gw_can_info_reset()
        self.assertFalse(self.lib.gw_ha_diagnostics_due(self.g.now, last, True))

    def test_ha_fast_bsi_diagnostics_never_renews_old_soc(self):
        self.frame(0x3E0, bytes([85, 0, 0, 0, 0, 0, 0, 0]))
        before = self.parse_owned(self.lib.gw_ha_state_json())
        self.g.advance(360000)
        self.frame(0x036, bytes(8))
        self.assertTrue(self.lib.gw_ha_diagnostics_due(self.g.now, self.g.now - 1000, True))
        ages = self.parse_owned(self.lib.gw_ha_diagnostics_json())
        self.assertEqual(360, ages['vehicle_data_age_s'])
        self.assertEqual(360, ages['age_s']['soc_pct'])
        self.assertEqual(0, ages['age_s']['bsi_state'])
        self.assertEqual(85, self.parse_owned(self.lib.gw_ha_state_json())['soc_pct'])
        self.frame(0x3E0, bytes([85, 0, 0, 0, 0, 0, 0, 0]))
        ages = self.parse_owned(self.lib.gw_ha_diagnostics_json())
        self.assertEqual(0, ages['vehicle_data_age_s'])
        self.assertEqual(before['soc_pct'], self.parse_owned(self.lib.gw_ha_state_json())['soc_pct'])

    def test_ha_uses_same_vehicle_values_and_does_not_wake(self):
        self.climate()
        self.frame(0x396, bytes.fromhex('00 8E A3 00 C8 09'))
        self.lib.varstore_write(self.g.ids['vehicle.range_km_x10'], 2145)
        before = self.value('udp.wake_request_count')
        state = self.parse_owned(self.lib.gw_ha_state_json())
        self.assertEqual(214.5, state['range_km'])
        self.assertEqual(20, state['climate_left_setpoint_c'])
        self.assertEqual(12.6, state['aux_battery_voltage_v'])
        self.assertNotIn('cabin_c', state)
        self.assertEqual(before, self.value('udp.wake_request_count'))
        self.assertEqual(48, self.lib.gw_ha_entity_count())
        self.assertNotIn('demo', state)
        self.assertNotIn('age_s', state)
        diagnostics = self.parse_owned(self.lib.gw_ha_diagnostics_json())
        self.assertNotIn('demo', diagnostics['age_s'])
        for i in range(self.lib.gw_ha_entity_count()):
            key = self.lib.gw_ha_entity_key(i).decode()
            discovery = self.parse_owned(self.lib.gw_ha_discovery_json(i, b'gw_test', b'gw/state', b'gw/status', b'0.5.0', b'1234', 15))
            if key == 'vehicle_refresh':
                self.assertEqual('gw/command/request', discovery['command_topic'])
                self.assertFalse(discovery['retain'])
                self.assertEqual(1, discovery['qos'])
                self.assertEqual([{'topic': 'gw/status'}], discovery['availability'])
                self.assertNotIn('state_topic', discovery)
                self.assertNotIn(key, state)
                self.assertNotIn(key, diagnostics['age_s'])
                continue
            diagnostic = key == 'vehicle_data_age_s'
            self.assertIn(key, diagnostics if diagnostic else state)
            self.assertEqual(195 if diagnostic else 15, discovery['expire_after'])
            self.assertFalse(discovery['force_update'])
            self.assertEqual('all', discovery['availability_mode'])
            self.assertIn('is not none', discovery['availability'][1]['value_template'])
            self.assertEqual('eCMPilot gateway', discovery['device']['name'])
            if diagnostic:
                self.assertEqual('gw/state/diagnostics', discovery['state_topic'])
                self.assertEqual('gw/state/diagnostics', discovery['json_attributes_topic'])
                self.assertIn('data.age_s', discovery['json_attributes_template'])
                self.assertEqual('diagnostic', discovery['entity_category'])
                self.assertNotIn('state_class', discovery)
            else:
                self.assertEqual('gw/state', discovery['state_topic'])
                self.assertNotIn('json_attributes_topic', discovery)
                self.assertNotIn('json_attributes_template', discovery)
            self.assertEqual(discovery['state_topic'], discovery['availability'][1]['topic'])
            self.assertNotIn('command_topic', discovery)
        self.g.advance(6001)
        state = self.parse_owned(self.lib.gw_ha_state_json())
        self.assertEqual(20, state['climate_left_setpoint_c'])
        self.assertEqual(12.6, state['aux_battery_voltage_v'])
        self.assertGreaterEqual(self.parse_owned(self.lib.gw_ha_diagnostics_json())['age_s']['climate_left_setpoint_c'], 6)

    def test_ha_observations_change_only_with_values_not_source_age(self):
        self.g.set('battery.soc_pct_x10', 570, force=False)
        self.g.set('charge.connected', 1, force=False)
        self.g.set('charge.state', 2, force=False)
        self.g.set('temperature.outside_mc', 5400, force=False)
        original = self.parse_owned(self.lib.gw_ha_state_json())
        original_ages = self.parse_owned(self.lib.gw_ha_diagnostics_json())
        self.g.advance(301000)
        self.assertEqual(original, self.parse_owned(self.lib.gw_ha_state_json()))
        aged = self.parse_owned(self.lib.gw_ha_diagnostics_json())
        self.assertEqual(301, aged['age_s']['soc_pct'])
        self.assertNotEqual(original_ages, aged)
        self.g.set('battery.soc_pct_x10', 580, force=False)
        changed = self.parse_owned(self.lib.gw_ha_state_json())
        self.assertEqual({'soc_pct'}, {key for key in original if original[key] != changed[key]})
        # Refreshing the same value is a fresh observation, not a new HA value.
        self.g.set('battery.soc_pct_x10', 580, force=False)
        self.assertEqual(changed, self.parse_owned(self.lib.gw_ha_state_json()))
        self.lib.varstore_invalidate(self.g.ids['battery.soc_pct_x10'])
        self.assertIsNone(self.parse_owned(self.lib.gw_ha_state_json())['soc_pct'])

    def ha_configs(self):
        for i in range(self.lib.gw_ha_entity_count()):
            config = self.parse_owned(self.lib.gw_ha_discovery_json(
                i, b'gw_test', b'gw/state', b'gw/status', b'0.5.0', b'1234', 15))
            if 'value_template' in config:
                yield self.lib.gw_ha_entity_key(i).decode(), config

    def test_ha_templates_handle_empty_invalid_json_and_missing_fields(self):
        env = Environment(undefined=StrictUndefined)
        def unexpected_clock():
            self.fail('Malformed diagnostics must not claim a fresh received_at')
        env.globals.update(now=unexpected_clock, as_timestamp=lambda value: value)
        for key, config in self.ha_configs():
            for raw in ('', 'not json', '{', 'null', '[]', '42', 'true', '"text"', '{}', '{"other":1}'):
                with self.subTest(key=key, raw=raw):
                    context = dict(value=raw)
                    try:
                        context['value_json'] = json.loads(raw)
                    except ValueError:
                        pass  # Same absence of value_json as HA's MQTT renderer.
                    self.assertEqual('None', env.from_string(config['value_template']).render(context))
                    self.assertEqual('offline', env.from_string(config['availability'][1]['value_template']).render(context))
                    if 'json_attributes_template' in config:
                        self.assertEqual({}, json.loads(env.from_string(config['json_attributes_template']).render(context)))

    def test_ha_templates_preserve_real_values_false_and_unknown(self):
        env = Environment(undefined=StrictUndefined)
        env.globals.update(now=lambda: 123456, as_timestamp=lambda value: value)
        self.g.set('battery.soc_pct_x10', 570, force=False)
        self.g.set('bsi.door_rear_right_open', 0, force=False)
        self.g.set('bsi.door_driver_open', 1, force=False)
        state = self.parse_owned(self.lib.gw_ha_state_json())
        diagnostics = self.parse_owned(self.lib.gw_ha_diagnostics_json())
        for key, config in self.ha_configs():
            data = diagnostics if key == 'vehicle_data_age_s' else state
            with self.subTest(key=key):
                value = data[key]
                expected = ('ON' if value else 'OFF') if type(value) is bool else str(value)
                self.assertEqual(expected, env.from_string(config['value_template']).render(value_json=data))
                self.assertEqual('offline' if value is None else 'online',
                                 env.from_string(config['availability'][1]['value_template']).render(value_json=data))
                if 'json_attributes_template' in config:
                    attrs = json.loads(env.from_string(config['json_attributes_template']).render(value_json=data))
                    self.assertEqual({'age_s': diagnostics['age_s'], 'received_at': 123456}, attrs)

    def test_ha_retained_unavailable_snapshot_is_safe_for_legacy_subscribers(self):
        self.lib.gw_ha_unavailable_json.restype = c.c_void_p
        # Even when live values exist, reconnect must retain no vehicle values.
        self.g.set('battery.soc_pct_x10', 570)
        self.g.set('bsi.door_rear_right_open', 1)
        data = self.parse_owned(self.lib.gw_ha_unavailable_json())
        env = Environment(undefined=StrictUndefined)
        for key, config in self.ha_configs():
            with self.subTest(key=key):
                self.assertIsNone(data[key])
                self.assertIsNone(data['age_s'][key])
                legacy = "{{ {'age_s': value_json.age_s.KEY, 'stale': (value_json.age_s.KEY is not none and value_json.age_s.KEY > 300)} | tojson }}".replace('KEY', key)
                self.assertEqual({'age_s': None, 'stale': False}, json.loads(env.from_string(legacy).render(value_json=data)))
                self.assertEqual('None', env.from_string(config['value_template']).render(value_json=data))
                self.assertEqual('offline', env.from_string(config['availability'][1]['value_template']).render(value_json=data))
        self.assertNotIn('vehicle_refresh', data)

    def test_ha_removes_unsupported_measurements_and_retained_discovery(self):
        unsupported = {'cabin_c', 'battery_min_c', 'battery_max_c', 'voltage_v',
                       'current_a', 'charge_power_kw', 'charge_remaining_min'}
        # A manual Force must not resurrect an unsupported MQTT sensor.
        self.g.set('battery.current_ma', 12000)
        state = self.parse_owned(self.lib.gw_ha_state_json())
        diagnostics = self.parse_owned(self.lib.gw_ha_diagnostics_json())
        active = {self.lib.gw_ha_entity_key(i).decode() for i in range(self.lib.gw_ha_entity_count())}
        for key in unsupported | {'demo'}:
            self.assertNotIn(key, active)
            self.assertNotIn(key, state)
            self.assertNotIn(key, diagnostics['age_s'])
        self.lib.gw_ha_retired_count.restype = c.c_size_t
        for name in ('gw_ha_retired_key', 'gw_ha_retired_component'):
            fn = getattr(self.lib, name)
            fn.argtypes = [c.c_size_t]
            fn.restype = c.c_char_p
        count = self.lib.gw_ha_retired_count()
        retired = {self.lib.gw_ha_retired_key(i).decode(): self.lib.gw_ha_retired_component(i).decode()
                   for i in range(count)}
        self.assertEqual({**dict.fromkeys(unsupported, 'sensor'), 'demo': 'binary_sensor'}, retired)
        self.assertIsNone(self.lib.gw_ha_retired_key(count))
        self.assertIsNone(self.lib.gw_ha_retired_component(count))
        i = next(i for i in range(self.lib.gw_ha_entity_count())
                 if self.lib.gw_ha_entity_key(i) == b'aux_battery_voltage_v')
        config = self.parse_owned(self.lib.gw_ha_discovery_json(i, b'gw_test', b'gw/state', b'gw/status', b'0.5.0', b'1234', 15))
        self.assertEqual(1, config['suggested_display_precision'])
        self.assertEqual('V', config['unit_of_measurement'])
        self.assertEqual('gw_test_aux_battery_voltage_v', config['unique_id'])
        self.g.set('charge.mode', 1)
        self.assertEqual('delayed', self.parse_owned(self.lib.gw_ha_state_json())['charge_mode'])
        for i in range(self.lib.gw_ha_entity_count()):
            config = self.parse_owned(self.lib.gw_ha_discovery_json(i, b'gw_test', b'gw/state', b'gw/status', b'0.5.0', b'1234', 15))
            self.assertNotIn('Deferred', config['name'])
            if self.lib.gw_ha_entity_key(i) == b'charge_mode':
                self.assertEqual(['immediate', 'delayed'], config['options'])

    def test_ha_new_fields_keep_source_age_unknowns_force_and_phone_units(self):
        values = {
            'battery.soc_pct_x10': 570, 'bsi.power_management': 1,
            'bsi.door_driver_open': 1, 'bsi.door_passenger_open': 0,
            'bsi.locking_state': 3, 'bsi.trip1.average_x10': 153,
            'bsi.trip1.distance_km': 42, 'bsi.trip1.speed_kmh': 68,
            'bsi.precond.programs12_hi24': 0x5801C2,
            'charge.mode': 0, 'charge.start_minute': 180,
            'charge.state': 2, 'climate.state': 1,
        }
        for key, value in values.items():
            self.g.set(key, value, force=False)
        before = self.value('udp.wake_request_count')
        self.g.advance(301000)
        state = self.parse_owned(self.lib.gw_ha_state_json())
        self.assertEqual(57, state['soc_pct'])
        diagnostics = self.parse_owned(self.lib.gw_ha_diagnostics_json())
        self.assertEqual(301, diagnostics['vehicle_data_age_s'])
        self.assertEqual(301, diagnostics['age_s']['soc_pct'])
        self.assertEqual('normal', state['bsi_state'])
        self.assertTrue(state['door_driver_open'])
        self.assertFalse(state['door_passenger_open'])
        self.assertIsNone(state['door_rear_left_open'])
        self.assertEqual('unlocked', state['locking_state'])
        self.assertEqual(15.3, state['trip1_average'])
        self.assertEqual(42, state['trip1_distance_km'])
        self.assertEqual(68, state['trip1_speed_kmh'])
        self.assertEqual('07:30 off Mon Wed Thu', state['preconditioning_program1'])
        self.assertEqual('03:00', state['charge_start_local'])
        self.assertEqual('charging', state['charge_state'])
        self.assertEqual('on', state['climate_state'])
        self.g.set('battery.soc_pct_x10', 580)
        state = self.parse_owned(self.lib.gw_ha_state_json())
        self.assertEqual(58, state['soc_pct'])
        self.assertEqual(0, self.parse_owned(self.lib.gw_ha_diagnostics_json())['age_s']['soc_pct'])
        self.lib.varstore_force(self.g.ids['battery.soc_pct_x10'], False, 0)
        self.assertEqual(57, self.parse_owned(self.lib.gw_ha_state_json())['soc_pct'])
        self.assertEqual(before, self.value('udp.wake_request_count'))

    def test_legacy_hscan_never_transmits_even_with_forced_requests(self):
        legacy = Codec()
        legacy.lib.gw_can_wake_reset()
        legacy.lib.varstore_force(legacy.ids['bsi.power_management'], True, 1)
        legacy.lib.varstore_force(legacy.ids['can.version_tx_requested'], True, 1)
        data = (c.c_uint8 * 2)()
        identifier = c.c_uint32()
        legacy.lib.varstore_force(legacy.ids['can.wake_tx_request_count'], True, 1)
        payload = c.c_uint16()
        legacy.lib.gw_can_wake_encode.argtypes = [c.POINTER(c.c_uint32), c.POINTER(c.c_uint16)]
        legacy.lib.gw_can_wake_encode.restype = c.c_bool
        self.assertFalse(legacy.lib.gw_can_wake_encode(c.byref(identifier), c.byref(payload)))

    def test_gvret_reports_real_controller_mode_for_both_profiles(self):
        for name, expected in [('udp-info-test.dll', 0x01), ('udp-test.dll', 0x11)]:
            lib = Codec(name).lib
            lib.gw_gvret_protocol_reset.argtypes = [c.c_void_p]
            lib.gw_gvret_protocol_feed.argtypes = [c.c_void_p, c.c_uint8, c.c_uint64, c.c_bool, c.c_void_p]
            protocol = c.create_string_buffer(64)
            reply = (c.c_uint8 * 20)()
            lib.gw_gvret_protocol_reset(protocol)
            for byte in [0xE7, 0xF1, 0x06]:
                length = lib.gw_gvret_protocol_feed(protocol, byte, 0, True, reply)
            self.assertEqual(12, length)
            self.assertEqual(expected, reply[2])


if __name__ == '__main__':
    unittest.main()
