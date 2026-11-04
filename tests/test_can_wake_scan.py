"""Offline cleanup/control-flow tests; never load a DLL or access CAN hardware."""
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'tools/can_wake_scan.py'
spec = importlib.util.spec_from_file_location('can_wake_scan', SOURCE)
scan = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = scan
spec.loader.exec_module(scan)


def quiet():
    return {'data_frames': 0, 'ids': {}, 'bsi_seen': False, 'echoes': 0}


def awake():
    return {'data_frames': 1, 'ids': {'432': 1}, 'bsi_seen': True, 'echoes': 0}


class FakePcan:
    def __init__(self, responses=None, fail_phase=None, fail_write=False, fail_reset_after_tx=False):
        self.calls = []
        self.responses = list(responses or [])
        self.fail_phase = fail_phase
        self.fail_write = fail_write
        self.fail_reset_after_tx = fail_reset_after_tx
        self.pending = False

    def open(self):
        self.calls.append(('open',))

    def reset(self):
        self.calls.append(('reset',))
        if self.pending and self.fail_reset_after_tx:
            raise RuntimeError('Hardware reset failed')
        self.pending = False

    def write(self, frame):
        if self.pending:
            raise AssertionError('Previous transmission was not cleared')
        self.calls.append(('write', frame.ident))
        self.pending = True
        if self.fail_write:
            raise RuntimeError('Write failed after possible enqueue')

    def receive(self, seconds, phase, stop_on_data=False):
        self.calls.append(('receive', phase))
        if phase == self.fail_phase:
            raise KeyboardInterrupt()
        return self.responses.pop(0) if self.responses else quiet()

    def close(self):
        self.calls.append(('close',))
        self.pending = False


class WakeScanTests(unittest.TestCase):
    frames = [scan.Frame(0x112, bytes(8)), scan.Frame(0x432, bytes(8))]

    def run_scan(self, fake):
        return scan.scan(fake, self.frames, lambda *a, **kw: None)

    def test_recorded_sets(self):
        self.assertEqual(len(scan.frames_for('app')), 68)
        all_frames = scan.frames_for('all')
        self.assertEqual(len(all_frames), 89)
        self.assertEqual(len({f.ident for f in all_frames}), 89)
        self.assertEqual([f.ident for f in all_frames], sorted(f.ident for f in all_frames))
        self.assertEqual(next(f.data for f in all_frames if f.ident == 0x4FE), bytes.fromhex('A0 C0 08'))

    def test_one_write_and_reset_between_each_id(self):
        fake = FakePcan()
        results = self.run_scan(fake)
        self.assertEqual(len(results), 2)
        self.assertEqual([c for c in fake.calls if c[0] == 'write'], [('write', 0x112), ('write', 0x432)])
        for index, call in enumerate(fake.calls):
            if call[0] == 'write':
                self.assertEqual(fake.calls[index + 1], ('receive', 'tx'))
                self.assertEqual(fake.calls[index + 2], ('reset',))
                self.assertEqual(fake.calls[index + 3], ('receive', 'after_reset'))
        self.assertEqual(fake.calls[-1], ('close',))
        self.assertFalse(fake.pending)

    def test_initial_traffic_sends_nothing(self):
        fake = FakePcan([awake()])
        self.assertEqual(self.run_scan(fake), [])
        self.assertFalse(any(c[0] == 'write' for c in fake.calls))
        self.assertEqual(fake.calls[-1], ('close',))

    def test_late_baseline_traffic_sends_nothing(self):
        fake = FakePcan([quiet(), awake()])
        self.assertEqual(self.run_scan(fake), [])
        self.assertFalse(any(c[0] == 'write' for c in fake.calls))

    def test_tx_wake_stops_before_second_id(self):
        fake = FakePcan([quiet(), quiet(), awake(), awake()])
        results = self.run_scan(fake)
        self.assertEqual(len(results), 1)
        self.assertTrue(results[0]['candidate'])
        self.assertEqual(sum(c[0] == 'write' for c in fake.calls), 1)
        self.assertIn(('receive', 'after_reset'), fake.calls)

    def test_wake_after_reset_stops_before_second_id(self):
        fake = FakePcan([quiet(), quiet(), quiet(), awake()])
        results = self.run_scan(fake)
        self.assertEqual(len(results), 1)
        self.assertTrue(results[0]['candidate'])

    def test_write_exception_still_resets(self):
        fake = FakePcan(fail_write=True)
        with self.assertRaisesRegex(RuntimeError, 'Write failed'):
            self.run_scan(fake)
        self.assertEqual(fake.calls[-2:], [('reset',), ('close',)])
        self.assertFalse(fake.pending)

    def test_ctrl_c_in_tx_still_resets(self):
        fake = FakePcan(fail_phase='tx')
        with self.assertRaises(KeyboardInterrupt):
            self.run_scan(fake)
        self.assertEqual(fake.calls[-2:], [('reset',), ('close',)])
        self.assertFalse(fake.pending)

    def test_ctrl_c_after_reset_disconnects(self):
        fake = FakePcan(fail_phase='after_reset')
        with self.assertRaises(KeyboardInterrupt):
            self.run_scan(fake)
        self.assertEqual(fake.calls[-1], ('close',))
        self.assertFalse(fake.pending)

    def test_failed_reset_aborts_before_next_id(self):
        fake = FakePcan(fail_reset_after_tx=True)
        with self.assertRaisesRegex(RuntimeError, 'Hardware reset failed'):
            self.run_scan(fake)
        self.assertEqual(sum(c[0] == 'write' for c in fake.calls), 1)
        self.assertEqual(fake.calls[-1], ('close',))

    def test_local_echo_and_error_are_not_vehicle_response(self):
        self.assertTrue(scan.vehicle_data(0, 0))
        self.assertTrue(scan.vehicle_data(0, 2))  # A real extended Rx is activity too.
        for kind in [1, 0x20, 0x40, 0x80, 0x22]:
            self.assertFalse(scan.vehicle_data(0, kind))
        self.assertFalse(scan.vehicle_data(8, 0))

    def test_dirty_controller_aborts_before_any_transmission(self):
        dirty = {**quiet(), 'bus_status': '0x8'}
        fake = FakePcan([dirty])
        with self.assertRaisesRegex(RuntimeError, 'not clean'):
            self.run_scan(fake)
        self.assertFalse(any(c[0] == 'write' for c in fake.calls))

    def test_controller_not_recovered_prevents_second_id(self):
        dirty = {**quiet(), 'bus_status': '0x8'}
        fake = FakePcan([quiet(), quiet(), quiet(), dirty])
        with self.assertRaisesRegex(RuntimeError, 'did not recover'):
            self.run_scan(fake)
        self.assertEqual(sum(c[0] == 'write' for c in fake.calls), 1)

    def test_actual_adapter_close_disconnects_even_if_reset_fails(self):
        calls = []
        io = scan.Pcan.__new__(scan.Pcan)
        io.opened, io.hard_enabled, io.channel = True, True, 0x51
        io.emit = lambda *a, **kw: None
        io.api = SimpleNamespace(CAN_Uninitialize=lambda channel: calls.append(channel) or 0)

        def fail_reset():
            raise RuntimeError('Reset failed')

        io.reset = fail_reset
        with self.assertRaisesRegex(RuntimeError, 'Reset failed'):
            io.close()
        self.assertEqual(calls, [0x51])
        self.assertFalse(io.opened)

    def test_classic_pcan_abi(self):
        self.assertEqual(scan.ct.sizeof(scan.Msg), 16)
        self.assertEqual(scan.Msg.data.offset, 6)
        self.assertEqual(scan.ct.sizeof(scan.Timestamp), 8)

    def test_api_owned_channel_is_not_initialized_or_written(self):
        io = scan.Pcan.__new__(scan.Pcan)
        io.get_value = lambda parameter: 2
        io.api = SimpleNamespace()  # Any Initialize/Write call would fail this test.
        with self.assertRaisesRegex(RuntimeError, 'another API client'):
            io.open()


if __name__ == '__main__':
    unittest.main()
