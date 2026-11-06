"""MQTT request adapter through the real shared command cache and CAN controller."""
import ctypes as c
from datetime import datetime, timezone
import json
import re
import unittest

from jinja2 import StrictUndefined
from jinja2.sandbox import ImmutableSandboxedEnvironment

from test_udp import BOOT, Codec
import test_vehicle_control as vehicle_control


class HaCommandTests(unittest.TestCase):
    def setUp(self):
        self.controller = vehicle_control.VehicleControlTests()
        self.controller.setUp()
        self.g = self.controller.g
        self.lib = self.g.lib
        self.configure(self.lib)
        self.next_id = 1000

    @staticmethod
    def configure(lib):
        lib.gw_udp_protocol_mqtt_handle.argtypes = [c.c_char_p, c.c_size_t, c.c_bool, c.c_uint64, c.c_void_p]
        lib.gw_udp_protocol_mqtt_handle.restype = c.c_size_t
        lib.gw_udp_protocol_mqtt_poll.argtypes = [c.c_uint64, c.c_void_p]
        lib.gw_udp_protocol_mqtt_poll.restype = c.c_size_t

    def request(self, op='vehicle.refresh', args=None, **changes):
        self.next_id += 1
        request = dict(v=1, id=f'{self.next_id:032x}', boot=BOOT, op=op,
                       args={} if args is None else args)
        request.update(changes)
        return request

    def call(self, request, retained=False):
        raw = request if isinstance(request, bytes) else json.dumps(request).encode()
        output = c.create_string_buffer(1025)
        length = self.lib.gw_udp_protocol_mqtt_handle(raw, len(raw), retained, self.g.now, output)
        return json.loads(output.raw[:length]) if length else None

    def poll(self):
        output = c.create_string_buffer(1025)
        length = self.lib.gw_udp_protocol_mqtt_poll(self.g.now, output)
        return json.loads(output.raw[:length]) if length else None

    def test_admission_and_qos_duplicate_do_not_transmit_from_network_owner(self):
        request = self.request()
        accepted = self.call(request)
        self.assertEqual('accepted', accepted['command']['phase'])
        self.assertEqual(request['id'], accepted['reply_to'])
        self.assertEqual(1, self.g.read('request.vehicle.generation'))
        self.assertEqual([], self.controller.frames)
        self.g.advance(6000)
        self.assertEqual(accepted['command'], self.call(request)['command'])
        self.assertEqual(1, self.g.read('request.vehicle.generation'))
        self.assertIsNone(self.poll())

    def test_terminal_success_requires_new_can_reply_and_notifies_once(self):
        request = self.request()
        self.call(request)
        self.controller.frame(0x036, '00 00 00 00 01 00 00 00')
        self.lib.gw_vehicle_control_tick()
        def reply(frame):
            if frame[1] == 0x394:
                self.controller.frame(0x3E0, '41 00 00 00 00 00 00 00')
        self.controller.pump(1000, reply=reply)
        response = self.poll()
        self.assertEqual('succeeded', response['command']['phase'])
        self.assertEqual(request['id'], response['reply_to'])
        self.assertEqual(1, len(self.controller.sent(0x394)))
        self.assertEqual(65 * 10, self.g.read('battery.soc_pct_x10'))
        self.assertIsNone(self.poll())
        self.assertEqual(response['command'], self.call(request)['command'])
        self.assertEqual(1, self.g.read('request.vehicle.generation'))

    def test_failed_wake_reports_unknown_instead_of_charging_success(self):
        request = self.request()
        self.call(request)
        self.lib.gw_vehicle_control_tick()
        self.controller.drain()
        self.controller.pump(1400, bsi=None)
        result = self.poll()['command']
        self.assertEqual('unknown', result['phase'])
        self.assertEqual('bsi_wake_timeout', result['error'])
        self.assertEqual(10, len(self.controller.sent(0x43C)))
        self.assertEqual([], self.controller.sent(0x394))

    def test_query_returns_cached_result_and_does_not_wake_vehicle(self):
        request = self.request()
        self.call(request)
        query = self.request('command.get', {'command_id': request['id']})
        reply = self.call(query)
        self.assertEqual(query['id'], reply['reply_to'])
        self.assertEqual(request['id'], reply['command']['id'])
        self.assertEqual(1, self.g.read('request.vehicle.generation'))
        self.assertEqual([], self.controller.frames)

    def test_phone_and_mqtt_share_busy_admission_and_cache_refusals(self):
        self.controller.start()
        request = self.request()
        busy = self.call(request)['command']
        self.assertEqual('rejected', busy['phase'])
        self.assertEqual('busy', busy['error'])
        self.controller.pump(1400, bsi=None)
        self.assertEqual(busy, self.call(request)['command'])
        self.assertEqual(1, self.g.read('request.vehicle.generation'))
        self.assertEqual('accepted', self.call(self.request())['command']['phase'])
        self.assertEqual(2, self.g.read('request.vehicle.generation'))

    def test_mqtt_in_flight_prevents_phone_from_overwriting_request(self):
        self.call(self.request())
        phone = self.g.call(self.controller.action())['command']
        self.assertEqual('busy', phone['error'])
        self.assertEqual(1, self.g.read('request.vehicle.generation'))

    def test_retained_wrong_boot_and_future_operations_never_execute(self):
        cases = [
            (self.request(), True, 'retained_command'),
            (self.request(boot='f' * 32), False, 'boot_changed'),
            (self.request(v=0), False, 'unsupported_version'),
            (self.request('climate.set', {'enabled': True}), False, 'unsupported_operation'),
            (self.request('charge.mode.set', {'mode': 'immediate'}), False, 'unsupported_operation'),
            (self.request(args={'extra': 1}), False, 'unsupported_argument'),
            (self.request(extra=1), False, 'invalid_argument'),
        ]
        for request, retained, error in cases:
            with self.subTest(error=error):
                result = self.call(request, retained)['command']
                self.assertEqual('rejected', result['phase'])
                self.assertEqual(error, result['error'])
        self.assertEqual(0, self.g.read('request.vehicle.generation'))

    def test_malformed_duplicate_keys_and_oversized_payloads_are_dropped(self):
        request = json.dumps(self.request()).encode()
        for raw in (b'not json', request + b'garbage',
                    request.replace(b'"v": 1', b'"v": 1, "v": 1'),
                    request.replace(b'vehicle.refresh', b'vehicle.\\u0000refresh'),
                    b' ' * 513):
            self.assertIsNone(self.call(raw))
        self.assertEqual(0, self.g.read('request.vehicle.generation'))

    def test_test_mode_and_legacy_profile_cannot_return_fake_physical_success(self):
        self.g.set('test.enabled', 1)
        request = self.request()
        self.assertEqual('unsupported_operation', self.call(request)['command']['error'])
        self.g.set('test.enabled', 0)
        self.assertEqual('rejected', self.call(request)['command']['phase'])
        self.assertEqual(0, self.g.read('request.vehicle.generation'))
        legacy = Codec()
        self.configure(legacy.lib)
        raw = json.dumps(self.request()).encode()
        output = c.create_string_buffer(1025)
        n = legacy.lib.gw_udp_protocol_mqtt_handle(raw, len(raw), False, legacy.now, output)
        self.assertEqual('unsupported_operation', json.loads(output.raw[:n])['command']['error'])
        self.assertEqual(0, legacy.read('request.vehicle.generation'))

    def test_rejected_duplicate_cannot_consume_a_pending_terminal_notification(self):
        request = self.request()
        self.call(request)
        self.lib.gw_vehicle_control_tick()
        self.controller.pump(1400, bsi=None)
        self.assertEqual('retained_command', self.call(request, retained=True)['command']['error'])
        self.assertEqual('unknown', self.poll()['command']['phase'])

    def test_button_discovery_generates_unique_boot_bound_requests_without_read_side_effects(self):
        self.lib.gw_ha_entity_count.restype = c.c_size_t
        self.lib.gw_ha_entity_key.argtypes = [c.c_size_t]
        self.lib.gw_ha_entity_key.restype = c.c_char_p
        self.lib.gw_ha_discovery_json.argtypes = [c.c_size_t] + [c.c_char_p] * 5 + [c.c_uint]
        self.lib.gw_ha_discovery_json.restype = c.c_void_p
        self.lib.cJSON_free.argtypes = [c.c_void_p]
        i = next(i for i in range(self.lib.gw_ha_entity_count())
                 if self.lib.gw_ha_entity_key(i) == b'vehicle_refresh')
        ptr = self.lib.gw_ha_discovery_json(i, b'gw_test', b'gw/state', b'gw/status', b'0.5.0', b'1234', 18)
        try:
            config = json.loads(c.string_at(ptr))
        finally:
            self.lib.cJSON_free(ptr)
        self.assertEqual('gw/command/request', config['command_topic'])
        self.assertFalse(config['retain'])
        env = ImmutableSandboxedEnvironment(undefined=StrictUndefined)
        fixed_time = datetime(2026, 11, 6, 20, 0, tzinfo=timezone.utc)
        env.globals.update(now=lambda: fixed_time, as_timestamp=lambda value: value.timestamp())
        # Exercise both 16-bit boundaries at the same timestamp. Keep the
        # sandbox's real safe_range so oversized ranges fail before sampling.
        draws = iter([0, 0, 65535, 65535])
        def boundary_random(values):
            value = next(draws)
            self.assertIn(value, values)
            self.assertEqual(65536, len(values))
            return value
        env.filters['random'] = boundary_random
        template = env.from_string(config['command_template'])
        a, b = json.loads(template.render()), json.loads(template.render())
        self.assertNotEqual(a['id'], b['id'])
        self.assertEqual(a['id'][:24], b['id'][:24])
        self.assertEqual('00000000', a['id'][24:])
        self.assertEqual('ffffffff', b['id'][24:])
        self.assertRegex(a['id'], re.compile('^[0-9a-f]{32}$'))
        self.assertEqual(BOOT, a['boot'])
        self.assertEqual(0, self.g.read('request.vehicle.generation'))
        self.assertEqual('accepted', self.call(a)['command']['phase'])


if __name__ == '__main__':
    unittest.main()
