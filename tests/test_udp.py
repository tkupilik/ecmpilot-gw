"""Exercise the actual firmware codec, real cJSON and actual varstore definitions."""
import ctypes as c
import json
from pathlib import Path
import socket
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
BOOT = '0123456789abcdef0123456789abcdef'
CLIENT = 'a' * 32


class Definition(c.Structure):
    _fields_ = [('name', c.c_char_p), ('description', c.c_char_p), ('unit', c.c_char_p), ('max_age', c.c_uint32)]


class Codec:
    def __init__(self, library='udp-test.dll'):
        self.lib = c.CDLL(str(ROOT / 'build-host-tests' / library))
        self.lib.varstore_count.restype = c.c_size_t
        self.lib.varstore_definition.argtypes = [c.c_size_t]
        self.lib.varstore_definition.restype = c.POINTER(Definition)
        self.lib.gw_test_set_time.argtypes = [c.c_uint64]
        self.lib.gw_udp_protocol_tick.argtypes = [c.c_uint64]
        self.lib.gw_udp_protocol_init.argtypes = [c.c_char_p] * 3
        self.lib.gw_udp_protocol_handle.argtypes = [c.c_char_p, c.c_size_t, c.c_uint64, c.c_char_p]
        self.lib.gw_udp_protocol_handle.restype = c.c_size_t
        self.lib.varstore_write.argtypes = [c.c_size_t, c.c_int32]
        self.lib.varstore_force.argtypes = [c.c_size_t, c.c_bool, c.c_int32]
        self.lib.varstore_read.argtypes = [c.c_size_t, c.c_uint32, c.c_int32]
        self.lib.varstore_read.restype = c.c_int32
        self.now = 1000
        self.lib.gw_test_set_time(self.now)
        assert self.lib.gw_process_data_init() == 0
        assert self.lib.gw_auto_refresh_init() == 0
        self.ids = {self.lib.varstore_definition(i).contents.name.decode(): i for i in range(self.lib.varstore_count())}
        self.lib.gw_udp_protocol_init(b'ecmpilot-test', BOOT.encode(), b'0.3.0')
        self.next_id = 1

    def set(self, name, value, force=True):
        if force:
            self.lib.varstore_force(self.ids[name], True, value)
        else:
            self.lib.varstore_write(self.ids[name], value)

    def read(self, name):
        return self.lib.varstore_read(self.ids[name], 0, -999)

    def advance(self, ms):
        self.now += ms
        self.lib.gw_test_set_time(self.now)
        self.lib.gw_udp_protocol_tick(self.now)

    def request(self, op='climate.set', args=None, rev=0):
        self.next_id += 1
        r = dict(v=0, type='request', gw='ecmpilot-test', boot=BOOT, client=CLIENT,
                 id=f'{self.next_id:032x}', op=op, args=args if args is not None else {'enabled': True})
        if op in ('climate.set', 'charge.mode.set'):
            r.update(deadline_ms=self.now + 5000, domain_rev=rev)
        return r

    def raw(self, data):
        output = c.create_string_buffer(1025)
        n = self.lib.gw_udp_protocol_handle(data, len(data), self.now, output)
        if not n:
            return None
        assert n <= 1024
        return output.raw[:n]

    def call(self, request):
        raw = self.raw(json.dumps(request, separators=(',', ':')).encode())
        return json.loads(raw) if raw else None

    def bench(self):
        for name, value in {'test.enabled': 1, 'vehicle.awake': 1, 'vehicle.parked': 1,
                            'climate.state': 0, 'charge.state': 1, 'charge.mode': 0,
                            'charge.connected': 1}.items():
            self.set(name, value)


class UdpProtocolTests(unittest.TestCase):
    def setUp(self):
        self.g = Codec()

    def test_bootstrap_readonly_and_invalid_values(self):
        req = self.g.request('info.get', {}); req['gw'] = None
        info = self.g.call(req)['info']
        self.assertEqual('none', info['control_backend']); self.assertEqual({}, info['operations'])
        state = self.g.call(self.g.request('state.get', {}))['state']
        self.assertIsNone(state['soc_pct']['value']); self.assertIsNone(state['vehicle']['awake'])
        self.assertEqual('unknown', state['climate']['state'])
        self.assertEqual('unsupported_operation', self.g.call(self.g.request())['command']['error'])

    def test_scales_freshness_force_and_release(self):
        self.g.set('battery.soc_pct_x10', 682, force=False)
        self.g.advance(120000)
        state = self.g.call(self.g.request('state.get', {}))['state']
        self.assertEqual({'value': 68.2, 'age_ms': 120000}, state['soc_pct'])
        self.g.set('battery.soc_pct_x10', 900)
        self.g.advance(120000)
        state = self.g.call(self.g.request('state.get', {}))['state']
        self.assertEqual({'value': 90, 'age_ms': 0}, state['soc_pct'])
        self.g.lib.varstore_force(self.g.ids['battery.soc_pct_x10'], False, 0)
        state = self.g.call(self.g.request('state.get', {}))['state']
        self.assertEqual(240000, state['soc_pct']['age_ms'])
        self.g.set('battery.soc_pct_x10', 9999)
        self.assertIsNone(self.g.call(self.g.request('state.get', {}))['state']['soc_pct']['value'])

    def test_milli_units_convert_to_unchanged_phone_units(self):
        inputs = {'temperature.cabin_mc': -1250, 'temperature.outside_mc': 9125,
                  'battery.min_mc': -60000, 'battery.max_mc': 150000,
                  'battery.voltage_mv': 416250, 'battery.current_ma': -12345,
                  'battery.soc_pct_x10': 705, 'charge.power_w': 1234}
        for name, value in inputs.items():
            self.g.set(name, value, force=False)
        state = self.g.call(self.g.request('state.get', {}))['state']
        for name, value in {'cabin_c': -1.25, 'outside_c': 9.125, 'battery_min_c': -60,
                            'battery_max_c': 150, 'voltage_v': 416.25, 'current_a': -12.345,
                            'soc_pct': 70.5}.items():
            self.assertEqual(value, state[name]['value'], name)
        self.assertEqual(1.234, state['charge']['power_kw'])
        for name, value, phone in [('temperature.cabin_mc', -60001, 'cabin_c'),
                                    ('battery.voltage_mv', 1000001, 'voltage_v'),
                                    ('battery.current_ma', -2000001, 'current_a')]:
            self.g.set(name, value, force=False)
            self.assertIsNone(self.g.call(self.g.request('state.get', {}))['state'][phone]['value'])

    def test_acceptance_duplicate_readback_and_cached_result_after_deadline(self):
        self.g.bench(); req = self.g.request()
        self.assertEqual('accepted', self.g.call(req)['command']['phase'])
        self.assertEqual(1, self.g.read('request.climate.enabled'))
        self.assertEqual(1, self.g.read('request.climate.generation'))
        self.assertEqual(0, self.g.read('climate.state'))
        self.assertEqual('accepted', self.g.call(req)['command']['phase'])
        self.assertEqual(1, self.g.read('udp.command_count'))
        self.g.set('climate.state', 1); self.g.advance(10000)
        result = self.g.call(req)['command']
        self.assertEqual('succeeded', result['phase']); self.assertEqual(2, result['revision'])
        query = self.g.request('command.get', {'command_id': req['id']})
        self.assertEqual(result, self.g.call(query)['command'])
        query['client'] = 'b' * 32
        self.assertEqual('result_unknown', self.g.call(query)['error'])

    def test_identity_deadlines_unknown_keys_and_id_conflict(self):
        self.g.bench()
        for field, value, error in [('gw', 'wrong', 'wrong_gateway'), ('boot', 'f'*32, 'boot_changed'),
                                    ('deadline_ms', self.g.now, 'expired'), ('deadline_ms', self.g.now+5001, 'expired'),
                                    ('domain_rev', 1, 'conflict')]:
            req = self.g.request(); req[field] = value
            self.assertEqual(error, self.g.call(req)['command']['error'])
        req = self.g.request(); req['args']['extra'] = 1
        self.assertEqual('unsupported_argument', self.g.call(req)['command']['error'])
        req = self.g.request(); req['extra'] = 1
        self.assertEqual('unsupported_argument', self.g.call(req)['command']['error'])
        req = self.g.request(); self.g.call(req); req['args']['enabled'] = False
        self.assertEqual('id_conflict', self.g.call(req)['command']['error'])
        self.assertEqual(1, self.g.read('udp.command_count'))

    def test_supersession_timeout_disable_and_cache_retention(self):
        self.g.bench(); start = self.g.request(); self.g.call(start)
        stop = self.g.request(args={'enabled': False}, rev=1)
        self.assertEqual('accepted', self.g.call(stop)['command']['phase'])
        self.assertEqual('failed', self.g.call(start)['command']['phase'])
        self.assertEqual('succeeded', self.g.call(stop)['command']['phase'])
        pending = self.g.request(rev=2); self.g.call(pending); self.g.advance(30000)
        self.assertEqual('unknown', self.g.call(pending)['command']['phase'])
        self.g.advance(600000)
        self.assertEqual('result_unknown', self.g.call(self.g.request('command.get', {'command_id': pending['id']}))['error'])
        request = self.g.request(rev=3); self.g.call(request); self.g.set('test.enabled', 0); self.g.advance(100)
        self.assertEqual('test_mode_disabled', self.g.call(request)['command']['error'])

    def test_prerequisites_and_group_age(self):
        self.g.bench(); self.g.set('vehicle.parked', 0)
        result = self.g.call(self.g.request())['command']
        self.assertEqual('not_parked', result['reason'])
        self.g.set('vehicle.parked', 1); self.g.set('charge.connected', 0)
        req = self.g.request('charge.mode.set', {'mode': 'immediate'})
        self.assertEqual('cable_disconnected', self.g.call(req)['command']['reason'])
        self.g.set('charge.connected', 1); self.g.set('charge.power_w', 7200, force=False); self.g.advance(10000)
        q = self.g.call(self.g.request('state.get', {}))['state']['charge']
        self.assertEqual(10000, q['age_ms']); self.assertEqual(7.2, q['power_kw'])

    def test_deferred_mode_and_no_optimistic_state_change(self):
        self.g.bench()
        req = self.g.request('charge.mode.set', {'mode': 'deferred', 'start_local': '23:15', 'timezone': 'Europe/Prague'})
        self.assertEqual('accepted', self.g.call(req)['command']['phase'])
        self.assertEqual(1395, self.g.read('request.charge.start_minute'))
        self.assertEqual(0, self.g.read('charge.mode'))
        self.g.set('charge.mode', 1); self.g.set('charge.start_minute', 1394)
        self.assertEqual('accepted', self.g.call(req)['command']['phase'])
        self.g.set('charge.start_minute', 1395)
        self.assertEqual('succeeded', self.g.call(req)['command']['phase'])

    def test_normal_readback_must_be_observed_after_acceptance(self):
        self.g.bench()
        self.g.lib.varstore_force(self.g.ids['climate.state'], False, 0)
        self.g.set('climate.state', 1, force=False)
        self.g.advance(100)
        request = self.g.request()
        self.assertEqual('accepted', self.g.call(request)['command']['phase'])
        self.assertEqual('accepted', self.g.call(request)['command']['phase'])
        self.g.advance(1); self.g.set('climate.state', 1, force=False)
        self.assertEqual('succeeded', self.g.call(request)['command']['phase'])

    def test_malformed_and_duplicate_json_cannot_dispatch(self):
        self.g.bench(); req = self.g.request(); raw = json.dumps(req).encode()
        for invalid in [raw+b' trailing', raw+b'\0', b' '*1025,
                        raw[:-1]+b',"op":"climate.set"}', raw.replace(b'"enabled": true', b'"enabled": true,"enabled":false'),
                        raw.replace(b'"enabled": true', b'"enabled":NaN'), raw.replace(b'climate.set',b'climate.\xffset'),
                        raw.replace(b'climate.set',b'climate.set\\u0000suffix')]:
            self.assertIsNone(self.g.raw(invalid), invalid)
        self.assertEqual(0, self.g.read('udp.command_count'))
        for malformed in [raw.replace(b'"v": 0', b'"v": 00'), raw.replace(b'"v": 0', b'"v": 0.'),
                          raw.replace(b'"v": 0', b'"v": 0e'), raw.replace(b'climate.set', b'climate.\nset'),
                          b'{"a":' * 30 + b'0' + b'}'*30]:
            self.assertIsNone(self.g.raw(malformed))

    def test_capacity_does_not_evict_unexpired_commands(self):
        self.g.bench()
        for rev in range(256):
            req = self.g.request(args={'enabled': False}, rev=rev)
            self.assertEqual('accepted', self.g.call(req)['command']['phase'])
            self.g.advance(1)
        req = self.g.request(args={'enabled': False}, rev=256)
        self.assertEqual('busy', self.g.call(req)['command']['error'])
        self.assertEqual(256, self.g.read('udp.command_count'))

    def test_maximal_snapshot_fits_and_emit_android_fixtures(self):
        self.g.bench()
        for name, value in {'battery.soc_pct_x10':1000,'vehicle.range_km_x10':20000,'temperature.cabin_mc':-60000,
                            'temperature.outside_mc':100000,'battery.min_mc':-60000,'battery.max_mc':150000,
                            'vehicle.odometer_km':10000000,'battery.voltage_mv':1000000,'battery.current_ma':-2000000,
                            'charge.power_w':1000000,'charge.remaining_min':100000,'bsi.power_management':1}.items():
            self.g.set(name, value, force=False)
        self.g.advance(2**40)
        data = self.g.raw(json.dumps(self.g.request('state.get', {})).encode())
        self.assertLessEqual(len(data), 1024)
        self.assertEqual(1,json.loads(data)['state']['bsi']['power_management'])
        fixtures = ROOT / 'build-host-tests/udp-fixtures'; fixtures.mkdir(exist_ok=True)
        self.g = Codec(); self.g.bench(); self.g.set('battery.soc_pct_x10',682)
        for name, response in [('info', self.g.call(self.g.request('info.get',{}))),
                               ('state', self.g.call(self.g.request('state.get',{}))),
                               ('accepted', self.g.call(self.g.request()))]:
            (fixtures / (name+'.json')).write_text(json.dumps(response, separators=(',',':')),encoding='utf-8')

    def test_real_udp_loopback_dropped_ack_retries_only_once(self):
        self.g.bench(); request=self.g.request(); data=json.dumps(request).encode(); errors=[]
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as server, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as client:
            server.bind(('127.0.0.1',0));server.settimeout(2);client.settimeout(2)
            def worker():
                try:
                    first,peer=server.recvfrom(1025);self.g.raw(first)  # lose acceptance
                    second,peer=server.recvfrom(1025);self.assertEqual(first,second)
                    server.sendto(self.g.raw(second),peer)
                except Exception as error:errors.append(error)
            thread=threading.Thread(target=worker,daemon=True);thread.start()
            client.sendto(data,server.getsockname());client.sendto(data,server.getsockname())
            self.assertEqual('accepted',json.loads(client.recv(1025))['command']['phase'])
            thread.join(3);self.assertFalse(thread.is_alive());self.assertEqual([],errors)
            self.assertEqual(1,self.g.read('udp.command_count'))


if __name__ == '__main__':
    unittest.main()
