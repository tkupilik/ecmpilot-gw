"""Exercise the production timer PT, NVS settings, UDP codec and CAN mailbox."""
import ctypes as c
import unittest
import test_vehicle_control as vc
from test_udp import Codec


class AutoRefreshTests(unittest.TestCase):
    def setUp(self):
        lib = Codec('udp-info-test.dll').lib
        lib.gw_test_nvs_reset()
        self.h = vc.VehicleControlTests('runTest')
        self.h.setUp()
        self.g, self.lib = self.h.g, self.h.lib

    def tearDown(self):
        self.lib.gw_test_nvs_reset()

    def status(self):
        return self.g.call(self.g.request('auto_refresh.get', {}))['auto_refresh']

    def save(self, **changes):
        current = self.status()
        args = {k: current[k] for k in ('rev', 'enabled', 'standby_min', 'charging_min', 'min_aux_soc_pct')}
        args.update(changes)
        return self.g.call(self.g.request('auto_refresh.set', args))

    def tick(self, ms=20, **kwargs):
        return self.h.step(ms=ms, **kwargs)

    def enable(self, **kwargs):
        self.assertIn('auto_refresh', self.save(enabled=True, **kwargs))
        self.tick(bsi=None)

    def test_defaults_and_passive_reads_never_create_a_vehicle_event(self):
        s = self.status()
        self.assertEqual((False, 60, 5, 50), tuple(s[k] for k in ('enabled', 'standby_min', 'charging_min', 'min_aux_soc_pct')))
        for _ in range(10):
            self.status(); self.tick(ms=3_600_000, bsi=None)
        self.assertEqual([], self.h.frames)
        self.assertEqual(0, self.g.read('request.vehicle.generation'))
        self.assertEqual('disabled', self.status()['state'])

    def test_countdown_clamps_on_charging_without_restarting_or_extending(self):
        self.enable()
        self.assertEqual(3_600_000, self.status()['remaining_ms'])
        self.tick(ms=600_000, bsi=None)
        self.assertEqual(3_000_000, self.status()['remaining_ms'])
        self.g.set('charge.state', 2)
        self.tick(bsi=None)
        self.assertEqual(300_000, self.status()['remaining_ms'])
        self.tick(ms=120_000, bsi=None)
        self.g.set('charge.state', 0)
        self.tick(bsi=None)
        self.assertEqual(179_980, self.status()['remaining_ms'])
        self.g.set('charge.state', 2)
        self.tick(bsi=None)
        self.assertEqual(179_960, self.status()['remaining_ms'])

    def test_unknown_charge_uses_standby_and_shorter_configuration_clamps(self):
        self.enable(standby_min=10, charging_min=1)
        self.tick(ms=90_000, bsi=None)
        self.assertEqual(510_000, self.status()['remaining_ms'])
        self.save(standby_min=2)
        self.tick(bsi=None)
        self.assertEqual(120_000, self.status()['remaining_ms'])
        self.save(standby_min=20)
        self.tick(bsi=None)
        self.assertEqual(119_980, self.status()['remaining_ms'])

    def test_unknown_aux_soc_allows_one_cycle_then_known_low_soc_pauses(self):
        self.enable(standby_min=1)
        self.tick(ms=60_000, bsi=None)
        self.assertEqual(1, self.status()['request_count'])
        self.assertEqual(2, self.g.read('control.source'))
        self.h.pump(1300, bsi=None)
        self.assertEqual(10, len(self.h.sent(0x43C)))
        self.assertEqual(1, self.status()['request_count'])
        self.assertGreater(self.status()['remaining_ms'], 58_000)
        self.g.set('bsi.aux_battery_soc_pct_x10', 495, force=False)
        self.tick(bsi=None)
        remaining = self.status()['remaining_ms']
        self.tick(ms=24*3_600_000, bsi=None)
        self.assertEqual('low_aux_battery', self.status()['state'])
        self.assertEqual(remaining, self.status()['remaining_ms'])
        self.assertEqual(1, self.status()['request_count'])
        self.g.set('bsi.aux_battery_soc_pct_x10', 500, force=False)
        self.tick(bsi=None)
        self.assertEqual('counting', self.status()['state'])
        self.tick(ms=1000, bsi=None)
        self.assertEqual(remaining-1000, self.status()['remaining_ms'])

    def test_no_reload_on_admission_timer_paused_until_manual_sequence_finishes(self):
        self.enable(standby_min=1)
        self.tick(ms=30_000, bsi=None)
        req = self.h.start(normal=True)
        before = self.status()['remaining_ms']
        self.assertLess(before, 31_000)
        self.h.pump(500)
        self.assertEqual(before, self.status()['remaining_ms'])
        self.assertEqual('refreshing', self.status()['state'])
        self.assertEqual(1, len(self.h.sent(0x394)))
        self.h.frame(0x3E0, '41 00 00 00 00 00 00 00')
        self.tick()
        self.assertEqual('succeeded', self.h.receipt(req)['phase'])
        self.assertEqual(60_000, self.status()['remaining_ms'])
        self.tick()
        self.tick(ms=59_000, bsi=None)
        self.assertEqual(0, self.status()['request_count'])

    def test_manual_failure_also_reloads_at_sequence_end(self):
        self.enable(standby_min=1)
        self.tick(ms=59_000, bsi=None)
        req = self.h.start()
        self.h.pump(1300, bsi=None)
        self.assertEqual('unknown', self.h.receipt(req)['phase'])
        self.assertGreater(self.status()['remaining_ms'], 59_000)
        self.assertEqual(0, self.status()['request_count'])

    def test_no_countdown_during_automatic_request_and_full_interval_after_failure(self):
        self.enable(standby_min=1)
        self.tick(ms=60_000, bsi=None)
        self.assertEqual(0, self.status()['remaining_ms'])
        self.h.pump(500, bsi=None)
        self.assertEqual(0, self.status()['remaining_ms'])
        self.h.pump(800, bsi=None)
        self.assertGreater(self.status()['remaining_ms'], 59_000)
        self.assertNotEqual(0, self.status()['last_result'])
        self.assertEqual(1, self.status()['request_count'])

    def test_explicit_request_preempts_auto_and_cannot_be_overwritten_before_receipt(self):
        self.enable(standby_min=1)
        self.tick(ms=60_000, bsi=None)
        req = self.h.start(normal=True)
        self.assertEqual(1, self.g.read('control.source'))
        self.h.pump(500)
        self.h.frame(0x3E0, '41 00 00 00 00 00 00 00'); self.tick()
        self.assertEqual('succeeded', self.h.receipt(req)['phase'])
        self.assertEqual(1, self.status()['request_count'])
        self.assertEqual(60_000, self.status()['remaining_ms'])

    def test_disabled_or_unavailable_never_submits_and_disable_clears_countdown(self):
        self.enable(standby_min=1)
        self.g.set('can.replay_enabled', 1)
        self.tick(bsi=None)
        remaining = self.status()['remaining_ms']
        self.tick(ms=600_000, bsi=None)
        self.assertEqual('unavailable', self.status()['state'])
        self.assertEqual(remaining, self.status()['remaining_ms'])
        self.save(enabled=False); self.tick(bsi=None)
        self.assertEqual(0, self.status()['remaining_ms'])
        self.assertEqual([], self.h.frames)

    def test_persistence_cas_idempotence_validation_and_failed_commit(self):
        original = self.status()
        changed = self.save(enabled=True, standby_min=13, charging_min=2, min_aux_soc_pct=65)['auto_refresh']
        self.assertEqual(original['rev']+1, changed['rev'])
        stale = dict(rev=original['rev'], enabled=True, standby_min=13, charging_min=2, min_aux_soc_pct=65)
        self.assertEqual(changed['rev'], self.g.call(self.g.request('auto_refresh.set', stale))['auto_refresh']['rev'])
        stale['standby_min'] = 14
        self.assertEqual('settings_conflict', self.g.call(self.g.request('auto_refresh.set', stale))['error'])
        for key, value in [('standby_min', 0), ('charging_min', 1441), ('min_aux_soc_pct', 101), ('enabled', 1), ('rev', 1.5)]:
            bad = dict(stale, rev=changed['rev']); bad[key] = value
            self.assertEqual('invalid_argument', self.g.call(self.g.request('auto_refresh.set', bad))['error'])
        bad = dict(stale, rev=changed['rev']); bad['extra'] = 1
        self.assertEqual('invalid_argument', self.g.call(self.g.request('auto_refresh.set', bad))['error'])
        self.lib.gw_test_nvs_fail_commit(1)
        self.assertEqual('storage_error', self.save(standby_min=14)['error'])
        self.assertEqual(13, self.status()['standby_min'])
        self.lib.gw_test_nvs_fail_commit(0)
        rebooted = Codec('udp-info-test.dll')
        result = rebooted.call(rebooted.request('auto_refresh.get', {}))['auto_refresh']
        self.assertEqual((True, 13, 2, 65, changed['rev']), tuple(result[k] for k in ('enabled','standby_min','charging_min','min_aux_soc_pct','rev')))

    def test_wrong_boot_cannot_change_settings(self):
        args = dict(rev=self.status()['rev'], enabled=True, standby_min=60, charging_min=5, min_aux_soc_pct=50)
        req = self.g.request('auto_refresh.set', args); req['boot'] = 'f'*32
        self.assertNotIn('auto_refresh', self.g.call(req))
        self.assertFalse(self.status()['enabled'])

    def test_invalid_force_pauses_without_breaking_the_configuration_response(self):
        self.enable(standby_min=1)
        # Public configuration is whole percent, stored in standard 0.1% units.
        self.g.set('auto_refresh.min_aux_soc_pct_x10', 505)
        self.tick(ms=60_000, bsi=None)
        s = self.status()
        self.assertFalse(s['config_valid'])
        self.assertEqual('invalid_config', s['state'])
        self.assertEqual(50, s['min_aux_soc_pct'])
        self.assertEqual([], self.h.frames)

    def test_large_identity_and_firmware_keep_capability_and_settings_within_1k(self):
        self.lib.gw_udp_protocol_init(b'g'*80, b'0'*32, b'f'*80)
        req = self.g.request('info.get', {}); req['gw'] = 'g'*80
        info = self.g.call(req)
        self.assertIn('auto_refresh', info['info']['fields'])
        req = self.g.request('auto_refresh.get', {}); req['gw'] = 'g'*80
        self.assertEqual(60, self.g.call(req)['auto_refresh']['standby_min'])

    def test_manual_result_is_retained_before_an_expired_auto_timer_can_reuse_controller(self):
        self.enable(standby_min=1)
        req = self.h.start(normal=True); self.h.pump(500)
        self.h.frame(0x3E0, '41 00 00 00 00 00 00 00'); self.tick()
        self.assertNotEqual(self.g.read('request.vehicle.generation'), self.g.read('control.receipt_generation'))
        # Deliberately stall CPU0 receipt retention while CPU1 continues to tick.
        for ms in (20, 60_000, 20):
            self.g.now += ms; self.lib.gw_test_set_time(self.g.now); self.lib.gw_vehicle_control_tick()
        self.assertEqual(0, self.g.read('auto_refresh.request_count'))
        self.assertEqual(4, self.g.read('auto_refresh.state'))  # busy, not overwritten
        self.assertEqual('succeeded', self.h.receipt(req)['phase'])
        self.tick(bsi=None)
        self.assertEqual(1, self.status()['request_count'])


if __name__ == '__main__': unittest.main()
