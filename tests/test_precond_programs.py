"""Stored program edits use real RX, PT, varstore and UDP; never hardware CAN."""
import json
import unittest
import test_vehicle_control as control_harness
from test_udp import BOOT

class PreconditioningProgramsTests(unittest.TestCase):
    # Reuse only the harness, not its already covered test cases.
    setUp = control_harness.VehicleControlTests.setUp
    frame = control_harness.VehicleControlTests.frame
    action = control_harness.VehicleControlTests.action
    start = control_harness.VehicleControlTests.start
    drain = control_harness.VehicleControlTests.drain
    step = control_harness.VehicleControlTests.step
    pump = control_harness.VehicleControlTests.pump
    sent = control_harness.VehicleControlTests.sent
    receipt = control_harness.VehicleControlTests.receipt
    program_frames = control_harness.VehicleControlTests.program_frames

    def get(self):
        return self.g.call(self.g.request('climate.programs.get', {}))['programs']

    def edit(self, base=None, slots=None):
        base = base or [0x5801C2, 0x7FF, 0x7FF, 0x7FF]
        return self.action('climate.programs.set', {'base': base, 'slots': slots or [base[0] | 0x800000] + base[1:]})

    def echo(self, frame):
        if frame[1] == 0x31B:
            self.precond_data = bytes([0x80 | ((frame[2][0] >> 6 & 1) << 2)]) + frame[2][1:]
        if frame[1] == 0x11B:
            self.precond34 = frame[2]

    def test_captured_0730_mon_wed_thu_is_vehicle_reported_but_disabled(self):
        self.program_frames()
        slot = self.get()['slots'][0]
        self.assertEqual(0x5801C2, slot['raw'])
        self.assertFalse(slot['raw'] & 0x800000)
        self.assertEqual(88, slot['raw'] >> 16 & 127)
        self.assertEqual(450, slot['raw'] & 2047)
        self.g.advance(300000)
        self.assertEqual(300000, self.get()['slots'][0]['age_ms'])

    def test_passive_reads_never_wake_and_missing_is_not_an_empty_schedule(self):
        self.assertTrue(all(v['raw'] is None for v in self.get()['slots']))
        self.assertEqual(0, self.g.read('request.vehicle.generation'))
        self.assertEqual([], self.frames)

    def test_enable_all_then_wait_for_both_bsi_readbacks(self):
        self.program_frames(); req = self.start(self.edit(), normal=True)
        self.pump(500, programs=True)
        self.assertEqual(bytes.fromhex('80 D8 01 C2 00 07 FF'), self.sent(0x31B)[0][2])
        self.assertEqual('accepted', self.receipt(req)['phase'])
        self.precond_data = bytes.fromhex('80 D8 01 C2 00 07 FF')
        self.pump(600, programs=True)
        self.assertEqual('succeeded', self.receipt(req)['phase'])
        self.assertEqual([], self.sent(0x394))

    def test_preserves_asap_and_reserved_bits_and_edits_all_four_slots(self):
        base = [0x58A1C2, 0x7FF, 0x7FF, 0x7FF]
        target = [0xD8A1C2, 0xA00258, 0x82032A, 0x8103C0]
        self.precond_data = bytes.fromhex('8C 58 A1 C2 00 07 FF')
        self.program_frames(); req = self.start(self.edit(base, target), normal=True)
        self.pump(1500, programs=True, reply=self.echo)
        self.assertEqual(bytes.fromhex('C0 D8 A1 C2 A0 02 58'), self.sent(0x31B)[0][2])
        self.assertEqual(bytes.fromhex('00 82 03 2A 81 03 C0'), self.sent(0x11B)[0][2])
        self.assertEqual('succeeded', self.receipt(req)['phase'])
        self.assertTrue(self.precond_data[0] & 4)

    def test_external_change_fails_instead_of_overwriting_myopel(self):
        self.precond_data = bytes.fromhex('80 58 01 D0 00 07 FF')
        self.program_frames(); req = self.start(self.edit(), normal=True)
        self.pump(600, programs=True)
        self.assertEqual('preconditioning_programs_conflict', self.receipt(req)['error'])
        self.assertEqual([], self.sent(0x31B))

    def test_force_cannot_confirm_a_schedule_edit(self):
        self.program_frames(); req = self.start(self.edit(), normal=True)
        self.g.set('bsi.precond.programs12_hi24', 0xD801C2)
        self.pump(31000, programs=True)
        self.assertEqual('unknown', self.receipt(req)['phase'])
        self.assertEqual(3, len(self.sent(0x31B)))

    def test_partial_ack_and_retry_converge_without_toggling(self):
        self.program_frames(); req = self.start(self.edit(), normal=True)
        self.pump(600, programs=True, reply=self.echo)
        self.assertEqual('succeeded', self.receipt(req)['phase'])
        self.assertEqual(1, len(self.sent(0x31B)))
        self.g.call(req); self.pump(600, programs=True)
        self.assertEqual(1, len(self.sent(0x31B)))
        modified = json.loads(json.dumps(req)); modified['args']['slots'][0] ^= 1
        self.assertEqual('id_conflict', self.g.call(modified)['command']['error'])

    def test_missing_aux_readback_cannot_confirm_primary_program(self):
        self.program_frames(); req = self.start(self.edit(), normal=True)
        self.pump(500, reply=self.echo)
        self.frame(0x2E0, self.precond_data)
        self.pump(300)
        self.assertEqual('accepted', self.receipt(req)['phase'])
        self.frame(0x2E0, self.precond_data); self.frame(0x0E0, self.precond34)
        self.pump(20)
        self.assertEqual('succeeded', self.receipt(req)['phase'])

    def test_disable_preserves_days_and_time_and_does_not_stop_asap(self):
        base = [0xD801C2, 0x7FF, 0x7FF, 0x7FF]
        self.precond_data = bytes.fromhex('8C D8 01 C2 00 07 FF')
        self.program_frames()
        req = self.start(self.edit(base, [0x5801C2] + base[1:]), normal=True)
        self.pump(1200, programs=True, reply=self.echo)
        self.assertEqual(bytes.fromhex('C0 58 01 C2 00 07 FF'), self.sent(0x31B)[0][2])
        self.assertEqual('succeeded', self.receipt(req)['phase'])

    def test_invalid_active_slots_and_reserved_bit_edits_rejected(self):
        for target in [0x8001C2, 0xD807FF, 0xD809C2, -1, 0x1000000]:
            req = self.edit(slots=[target, 0x7FF, 0x7FF, 0x7FF])
            self.assertEqual('rejected', self.g.call(req)['command']['phase'])
        self.assertEqual(0, self.g.read('request.vehicle.generation'))

    def test_udp_size_and_native_capability(self):
        info = self.g.call(self.g.request('info.get', {}))['info']
        self.assertIn('climate_programs', info['fields'])
        self.assertTrue(info['operations']['climate.programs.set']['persistent_schedule'])
        self.assertFalse(info['operations']['climate.programs.set']['verified'])
        self.program_frames()
        raw = self.g.raw(json.dumps(self.g.request('climate.programs.get', {})).encode())
        self.assertLessEqual(len(raw), 1024)
        self.g.lib.gw_udp_protocol_init(b'x' * 80, BOOT.encode(), b'f' * 80)
        request = self.g.request('info.get', {}); request['gw'] = 'x' * 80
        raw = self.g.raw(json.dumps(request).encode())
        self.assertIsNotNone(raw)
        self.assertLessEqual(len(raw), 1024)
        self.assertIn('climate_programs', json.loads(raw)['info']['fields'])
