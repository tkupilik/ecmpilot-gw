"""Actual chunked serializer: bounded memory, complete JSON and send failures."""
import ctypes as c
import json
import unittest
from test_udp import Codec, Definition


class VarsHttpTests(unittest.TestCase):
    def setUp(self):
        self.g = Codec()
        self.lib = self.g.lib
        self.lib.gw_test_vars.argtypes = [c.c_int]
        self.lib.gw_test_vars_body.restype = c.c_char_p
        self.lib.varstore_invalidate.argtypes = [c.c_size_t]

    def dump(self):
        self.assertEqual(0, self.lib.gw_test_vars(0))
        data = json.loads(self.lib.gw_test_vars_body())
        self.assertEqual(1, self.lib.gw_test_vars_ended())
        self.assertLessEqual(self.lib.gw_test_vars_max_chunk(), 1024)
        return {item['name']: item for item in data['signals']}

    def test_complete_real_registry_and_existing_schema_without_samples(self):
        signals = self.dump()
        self.assertEqual(set(self.g.ids), set(signals))
        soc = signals['battery.soc_pct_x10']
        self.assertEqual(set(soc), {'id', 'name', 'description', 'unit', 'max_age_ms', 'writable',
                                   'state', 'valid', 'forced', 'value', 'age_ms',
                                   'raw_value', 'raw_age_ms', 'raw_valid'})
        self.assertEqual(('invalid', False, False, None, None, None, None, False),
                         tuple(soc[k] for k in ('state', 'valid', 'forced', 'value', 'age_ms',
                                               'raw_value', 'raw_age_ms', 'raw_valid')))
        self.assertFalse(soc['writable'])
        self.assertTrue(signals['demo.value']['writable'])
        self.assertGreater(self.lib.gw_test_vars_chunks(), 2)

    def test_descriptors_match_canonical_integer_units(self):
        signals = self.dump()
        expected = {'battery.voltage_mv': 'mV', 'battery.current_ma': 'mA',
                    'temperature.cabin_mc': 'mC', 'temperature.outside_mc': 'mC',
                    'battery.min_mc': 'mC', 'battery.max_mc': 'mC',
                    'can.hvac.upstream_mc': 'mC', 'can.hvac.evaporator_mc': 'mC',
                    'can.hvac.compressor_current_ma': 'mA',
                    'charge.power_w': 'W', 'battery.soc_pct_x10': '0.1 %'}
        for name, unit in expected.items():
            self.assertEqual(unit, signals[name]['unit'], name)
        for name in signals:
            self.assertNotIn('_c_x10', name)
            self.assertNotIn('_v_x10', name)
            self.assertNotIn('_a_x10', name)

    def test_full_response_works_even_if_every_cjson_allocation_fails(self):
        allocations = []
        alloc_type = c.CFUNCTYPE(c.c_void_p, c.c_size_t)
        free_type = c.CFUNCTYPE(None, c.c_void_p)
        class Hooks(c.Structure):
            _fields_ = [('allocate', alloc_type), ('free', free_type)]
        def denied(size):
            allocations.append(size)
            return None
        hooks = Hooks(alloc_type(denied), free_type(lambda _: None))
        self.lib.cJSON_InitHooks.argtypes = [c.POINTER(Hooks)]
        self.lib.cJSON_InitHooks(c.byref(hooks))
        try:
            self.assertEqual(set(self.g.ids), set(self.dump()))
            self.assertEqual([], allocations)
        finally:
            self.lib.cJSON_InitHooks(None)

    def test_signed_values_forces_staleness_and_saturated_uint32_ages(self):
        self.g.set('demo.value', -2147483648, force=False)
        self.g.set('demo.value', 2147483647)
        self.lib.gw_test_set_time(2**32 + 10000)
        item = self.dump()['demo.value']
        self.assertEqual(('forced', 2147483647, -2147483648, False, 4294967295, 4294967295),
                         tuple(item[k] for k in ('state', 'value', 'raw_value', 'raw_valid', 'age_ms', 'raw_age_ms')))
        self.lib.varstore_force(self.g.ids['demo.value'], False, 0)
        item = self.dump()['demo.value']
        self.assertEqual(('invalid', -2147483648, False),
                         (item['state'], item['value'], item['valid']))
        self.lib.varstore_invalidate(self.g.ids['demo.value'])
        self.assertFalse(self.dump()['demo.value']['raw_valid'])

    def custom_registry(self, names, description, unit):
        definitions = (Definition * len(names))(*[
            Definition(name.encode('utf-8'), description.encode('utf-8'), unit.encode('utf-8'), 5000)
            for name in names])
        slots = (c.c_uint32 * (10 * len(names)))()
        clock_type = c.CFUNCTYPE(c.c_uint64, c.c_void_p)
        clock = clock_type(lambda _: 1000)
        self.lib.varstore_init.argtypes = [c.POINTER(Definition), c.c_void_p, c.c_size_t, clock_type, c.c_void_p]
        self.lib.varstore_init(definitions, slots, len(names), clock, None)
        return definitions, slots, clock

    def test_json_escaping_utf8_and_metadata_longer_than_one_chunk(self):
        name = 'probe."\\\x01'
        description = ('Escaped "value"\\\n\t\r\b\f\x02 / teplota ž °C ' * 150)
        keep = self.custom_registry([name], description, '°C')
        try:
            item = self.dump()[name]
            self.assertEqual(description, item['description'])
            self.assertEqual('°C', item['unit'])
            self.assertGreater(self.lib.gw_test_vars_chunks(), 5)
        finally:
            self.lib.gw_process_data_init()

    def test_large_registry_uses_same_maximum_chunk_size(self):
        names = [f'synthetic.signal_{i}' for i in range(400)]
        keep = self.custom_registry(names, 'Large registry regression', 'raw')
        try:
            self.assertEqual(set(names), set(self.dump()))
            self.assertGreater(self.lib.gw_test_vars_chunks(), 100)
        finally:
            self.lib.gw_process_data_init()

    def test_failed_chunk_aborts_without_completing_partial_json(self):
        self.dump()
        last = self.lib.gw_test_vars_chunks()
        for failed in (1, 2, last):
            self.assertNotEqual(0, self.lib.gw_test_vars(failed))
            self.assertEqual(failed, self.lib.gw_test_vars_chunks())
            self.assertEqual(0, self.lib.gw_test_vars_ended())
