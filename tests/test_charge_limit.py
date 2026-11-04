"""Parallel limiter -> shared production PT -> actual CAN mailboxes, no CAN IO."""
import unittest
import test_vehicle_control as vc
from test_udp import Codec


class ChargeLimitTests(unittest.TestCase):
    def setUp(self):
        Codec('udp-info-test.dll').lib.gw_test_nvs_reset()
        self.h = vc.VehicleControlTests('runTest'); self.h.setUp()
        self.g, self.lib = self.h.g, self.h.lib

    def tearDown(self):
        self.lib.gw_test_nvs_reset()

    def status(self):
        return self.g.call(self.g.request('auto_refresh.get', {}))['auto_refresh']

    def save(self, **changes):
        old = self.status()
        args = {k: old[k] for k in ('rev','enabled','standby_min','charging_min','min_aux_soc_pct','charge_limit_enabled','charge_limit_soc_pct')}
        args.update(changes)
        return self.g.call(self.g.request('auto_refresh.set', args))

    def enable(self, target=80):
        self.assertIn('auto_refresh', self.save(enabled=True, charge_limit_enabled=True, charge_limit_soc_pct=target))

    def data(self, soc=80, charging=True, schedule='80 B4'):
        self.h.frame(0x3E0, bytes([soc,0,0,0,0,0,0,0]))
        self.h.frame(0x396, bytes([4 if charging else 12,0x8E,0xA3,0,0xC8,9]))
        self.h.frame(0x160, schedule)
        self.lib.gw_vehicle_tick()

    def answer(self, f):
        if f[1] == 0x394: self.data(schedule='B9 C2')  # fresh 07:30 + reserved bits
        if f[1] == 0x35B: self.h.frame(0x160, f[2])

    def test_defaults_enable_dependency_range_and_reboot(self):
        self.assertEqual((False,80), tuple(self.status()[k] for k in ('charge_limit_enabled','charge_limit_soc_pct')))
        self.save(charge_limit_enabled=True)
        self.assertFalse(self.status()['charge_limit_enabled'])
        for invalid in (0,101,50.5,True):
            self.assertEqual('invalid_argument', self.save(charge_limit_soc_pct=invalid)['error'])
        self.enable(83)
        reloaded = Codec('udp-info-test.dll')
        status = reloaded.call(reloaded.request('auto_refresh.get', {}))['auto_refresh']
        self.assertTrue(status['charge_limit_enabled']); self.assertEqual(83,status['charge_limit_soc_pct'])
        self.save(enabled=False)
        self.assertFalse(self.status()['charge_limit_enabled'])

    def test_v1_nvs_migration_preserves_auto_refresh_and_starts_limit_disabled(self):
        self.lib.gw_test_nvs_auto_refresh_v1()
        self.assertEqual(0,self.lib.gw_auto_refresh_init())
        s=self.status()
        self.assertEqual((17,True,45,3,55,False,80), tuple(s[k] for k in ('rev','enabled','standby_min','charging_min','min_aux_soc_pct','charge_limit_enabled','charge_limit_soc_pct')))

    def test_at_target_wakes_settles_refreshes_and_preserves_fresh_schedule(self):
        self.enable(); self.data()
        self.h.step(bsi=None); self.h.pump(200,bsi=None)
        self.assertEqual(0x43C,self.h.frames[0][1])
        self.h.pump(1800,reply=self.answer)
        self.assertEqual(1,len(self.h.sent(0x394)));self.assertEqual(1,len(self.h.sent(0x35B)))
        self.assertEqual(bytes.fromhex('F9 C2'),self.h.sent(0x35B)[0][2])
        self.assertGreaterEqual(self.h.sent(0x35B)[0][0]-self.h.sent(0x394)[0][0],460)
        self.assertEqual(1,self.g.read('charge_limit.last_result'))
        self.assertEqual(3,self.g.read('control.source'))
        self.assertEqual(0,self.g.read('request.vehicle.generation'))
        self.h.pump(3000,reply=self.answer)
        self.assertEqual(1,self.g.read('charge_limit.request_count'))

    def test_disabled_below_target_noncharging_and_unknown_data_never_send(self):
        self.data();self.h.step();self.assertEqual([],self.h.frames)
        self.enable();self.data(79);self.h.pump(200);self.assertEqual([],self.h.frames)
        self.data(80,False);self.h.pump(200);self.assertEqual([],self.h.frames)
        self.lib.varstore_invalidate(self.g.ids['battery.soc_pct_x10'])
        self.h.pump(200);self.assertEqual([],self.h.frames)

    def test_stale_and_forced_values_do_not_trigger_real_actions(self):
        self.enable();self.data(40,False)
        self.g.set('battery.soc_pct_x10',900);self.g.set('bsi.charging_hmi_state',1)
        self.lib.gw_vehicle_tick();self.h.pump(500)
        self.assertEqual([],self.h.frames)
        self.data(80);self.h.step(ms=5100)
        self.assertEqual([],self.h.frames)
        self.assertEqual('waiting_data',self.status()['charge_limit_state'])

    def test_refresh_below_target_or_no_longer_charging_cancels_mode_request(self):
        for soc,charging in ((79,True),(80,False)):
            self.setUp();self.enable();self.data()
            def answer(f):
                if f[1]==0x394:self.data(soc,charging)
            self.h.pump(1500,reply=answer)
            self.assertEqual([],self.h.sent(0x35B))

    def test_disabling_or_raising_limit_revokes_pending_can_before_submission(self):
        for changes in (dict(enabled=False),dict(charge_limit_enabled=False),dict(charge_limit_soc_pct=90)):
            self.setUp();self.enable();self.data()
            self.h.step(bsi=None,serve=False)
            self.assertEqual(1,self.g.read('control.tx.pending'))
            self.save(**changes)
            self.assertIsNone(self.h.drain())
            self.h.pump(400,bsi=None)
            self.assertEqual([],self.h.frames)

    def test_existing_physical_modes_replay_suspend_and_low_aux_veto(self):
        for name,value in (('test.enabled',1),('can.replay_enabled',1),('request.can.suspend',1),('bsi.aux_battery_soc_pct_x10',490)):
            self.setUp();self.enable();self.data();self.g.set(name,value,force=False)
            self.h.pump(300)
            self.assertEqual([],self.h.frames)

    def test_explicit_request_has_priority_and_receipt_survives_background_work(self):
        self.enable();self.data()
        req=self.h.start(normal=True)
        self.h.pump(500,reply=lambda f: self.data() if f[1]==0x394 else None)
        self.assertEqual('succeeded',self.h.receipt(req)['phase'])
        self.h.pump(1700,reply=self.answer)
        self.assertEqual(1,self.g.read('charge_limit.request_count'))
        self.assertEqual('succeeded',self.h.receipt(req)['phase'])
        self.assertEqual(1,self.g.read('request.vehicle.generation'))

    def test_failed_stop_is_bounded_and_retries_only_after_cooldown_and_new_data(self):
        self.enable();self.data()
        self.h.pump(1300,bsi=None)
        self.assertEqual(10,len(self.h.sent(0x43C)))
        self.assertEqual(2,self.g.read('charge_limit.last_result'))
        self.data();self.h.pump(1000,bsi=None)
        self.assertEqual(1,self.g.read('charge_limit.request_count'))
        self.h.step(ms=61000,bsi=None)
        self.assertEqual(1,self.g.read('charge_limit.request_count'))
        self.data();self.h.step(bsi=None)
        self.assertEqual(2,self.g.read('charge_limit.request_count'))
