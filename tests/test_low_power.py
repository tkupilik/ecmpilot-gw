"""Automatic sleep PT + real varstore/settings, simulated clock and hardware boundary."""
import ctypes as c
import unittest
from test_udp import Codec

HOUR = 3_600_000

class LowPowerTests(unittest.TestCase):
    def setUp(self):
        self.g = Codec('udp-info-test.dll')
        self.lib = self.g.lib
        self.lib.gw_test_nvs_reset()
        self.assertEqual(0, self.lib.gw_low_power_init())
        self.lib.gw_vehicle_control_init()
        self.lib.gw_test_power_reset()
        self.assertEqual(0, self.lib.gw_power_policy_init())
        self.lib.gw_low_power_eligible.restype = c.c_bool
        self.lib.gw_low_power_tick()

    def tick(self, ms=0, power=False):
        self.g.advance(ms)
        self.lib.gw_low_power_tick()
        if power: self.lib.gw_power_policy_tick()

    def status(self):
        return self.g.call(self.g.request('low_power.get', {}))['low_power']

    def save(self, **changes):
        state = self.status()
        args = {k:state[k] for k in ('rev','enabled','idle_hours')}
        args.update(changes)
        return self.g.call(self.g.request('low_power.set', args))

    def sample(self, phase=0):
        self.lib.gw_can_info_decode(0x036, bytes([0,0,0,0,phase,0,0,0]), 8, False, False)

    def due(self):
        self.sample()
        self.tick(48*HOUR, power=True)
        self.assertEqual('entering', self.status()['state'])
        self.assertEqual(1, self.g.read('low_power.request_count'))

    def test_defaults_and_never_seen_or_forced_only_bsi_never_arm(self):
        self.assertEqual((True,48,None,'waiting_data'), tuple(self.status()[k] for k in ('enabled','idle_hours','remaining_s','state')))
        self.tick(100*24*HOUR)
        self.g.set('bsi.power_management', 1)
        self.tick(100*24*HOUR)
        self.assertEqual('waiting_data', self.status()['state'])
        self.assertEqual(0, self.g.read('request.power.generation'))

    def test_two_days_age_stays_valid_for_policy_and_countdown_restarts_on_real_sample(self):
        self.sample(1); self.tick()
        self.assertEqual(172800,self.status()['remaining_s'])
        self.tick(24*HOUR)
        self.assertEqual(86400,self.status()['remaining_s'])
        self.sample(2); self.tick()
        self.assertEqual(172800,self.status()['remaining_s'])
        self.tick(48*HOUR-1)
        self.assertEqual(1,self.status()['remaining_s'])
        self.tick(1)
        self.assertEqual('entering',self.status()['state'])

    def test_explicitly_invalid_sample_is_not_an_aged_valid_sample(self):
        self.sample(); self.lib.varstore_invalidate(self.g.ids['bsi.power_management'])
        self.tick(72*HOUR)
        self.assertEqual('waiting_data',self.status()['state'])
        self.assertIsNone(self.status()['remaining_s'])
        self.assertEqual(0,self.g.read('low_power.request_count'))

    def test_force_does_not_hide_real_age_or_fabricate_activity(self):
        self.sample()
        self.tick(47*HOUR)
        self.g.set('bsi.power_management',1)
        self.tick(HOUR)
        self.assertEqual('entering',self.status()['state'])

    def test_age_saturation_after_49_days_does_not_wrap_or_overflow_countdown(self):
        self.save(idle_hours=720)
        self.sample(); self.tick(29*24*HOUR)
        self.assertEqual(86400,self.status()['remaining_s'])
        self.tick(71*24*HOUR)
        self.assertEqual(0,self.status()['remaining_s'])
        self.assertEqual('entering',self.status()['state'])

    def test_disabled_policy_never_submits_and_reload_keeps_settings(self):
        self.save(enabled=False,idle_hours=24)
        self.sample(); self.tick(72*HOUR)
        self.assertEqual('disabled',self.status()['state'])
        self.assertEqual(0,self.g.read('request.power.generation'))
        self.assertEqual(0,self.lib.gw_low_power_init())
        self.assertEqual((False,24),tuple(self.status()[k] for k in ('enabled','idle_hours')))

    def test_new_bsi_during_preparation_cancels_before_can_stops(self):
        self.due()
        self.sample(3); self.tick(1500,power=True)
        self.assertEqual(0,self.g.read('request.can.suspend'))
        self.assertEqual(self.g.read('request.power.generation'),self.g.read('power.completed'))
        self.assertEqual(0,self.lib.gw_test_power_value(4))
        self.tick()
        self.assertEqual(172799,self.status()['remaining_s'])

    def test_disabling_after_admission_cancels_preparation(self):
        self.due(); self.save(enabled=False); self.tick(1500,power=True)
        self.assertEqual(0,self.g.read('request.can.suspend'))
        self.assertEqual(0,self.lib.gw_test_power_value(4))

    def test_automatic_sleep_uses_real_owner_handshake_and_recovers_refusal(self):
        self.due(); self.tick(1500,power=True)
        self.assertEqual(1,self.g.read('request.can.suspend'))
        self.assertEqual(0,self.g.read('request.network.suspend'))
        # The real CAN owner invalidates decoder observations as it closes TWAI.
        self.lib.gw_can_info_reset()
        self.g.set('can.suspended',1,False);self.g.set('can.suspend_completed',1,False)
        self.tick(power=True)
        self.assertEqual(1,self.g.read('request.network.suspend'))
        self.g.set('network.suspended',1,False);self.g.set('network.suspend_completed',1,False)
        self.tick(power=True)
        self.assertEqual(1,self.lib.gw_test_power_value(4))
        self.assertEqual(0,self.g.read('request.can.suspend'))
        self.assertEqual(0,self.g.read('request.network.suspend'))
        for key in ('can.suspended','can.suspend_completed','network.suspended','network.suspend_completed'):self.g.set(key,0,False)
        self.tick(power=True);self.tick()
        self.assertEqual('waiting_data',self.status()['state'])
        self.tick(2*HOUR)
        self.assertEqual(1,self.g.read('low_power.request_count'))
        self.sample();self.tick()
        self.assertEqual(172800,self.status()['remaining_s'])

    def test_failed_handoff_retries_no_faster_than_once_a_minute(self):
        self.due();self.tick(1500,power=True)
        self.g.set('can.suspend_error',0x103,False);self.g.set('can.suspend_completed',1,False)
        self.tick(power=True)
        self.g.set('can.suspend_completed',0,False);self.tick(power=True);self.tick()
        self.assertEqual('retry',self.status()['state'])
        self.tick(59999)
        self.assertEqual(1,self.g.read('low_power.request_count'))
        self.tick(1)
        self.assertEqual(2,self.g.read('low_power.request_count'))

    def test_pending_vehicle_work_and_ota_block_entry(self):
        self.sample()
        self.g.set('system.ota_active',1,False);self.tick(48*HOUR)
        self.assertEqual('busy',self.status()['state'])
        self.g.set('system.ota_active',0,False)
        self.g.set('request.vehicle.generation',1,False);self.tick()
        self.assertEqual('busy',self.status()['state'])
        self.assertEqual(0,self.g.read('low_power.request_count'))

    def test_config_cas_retry_failure_and_input_validation(self):
        before=self.status()
        changed=self.save(idle_hours=72)['low_power']
        self.assertEqual(before['rev']+1,changed['rev'])
        desired=dict(rev=before['rev'],enabled=True,idle_hours=72)
        self.assertEqual(changed['rev'],self.g.call(self.g.request('low_power.set',desired))['low_power']['rev'])
        self.assertEqual('settings_conflict',self.save(rev=before['rev'],idle_hours=24)['error'])
        self.lib.gw_test_nvs_fail_commit(1)
        self.assertEqual('storage_error',self.save(enabled=False)['error'])
        self.assertTrue(self.status()['enabled'])
        self.lib.gw_test_nvs_fail_commit(0)
        for value in (0,721,1.5,True):
            self.assertEqual('invalid_argument',self.save(idle_hours=value)['error'])

    def test_boot_binding_and_passive_queries_do_not_refresh_bsi_age(self):
        self.sample();self.tick(10*HOUR)
        for _ in range(3):self.status()
        req=self.g.request('low_power.set',dict(rev=1,enabled=False,idle_hours=48));req['boot']='f'*32
        self.assertEqual('wrong_boot',self.g.call(req)['error'])
        self.tick()
        self.assertEqual(38*3600,self.status()['remaining_s'])

    def test_low_aux_soc_pauses_refresh_then_inactivity_can_reach_sleep(self):
        automatic=self.g.call(self.g.request('auto_refresh.get',{}))['auto_refresh']
        args={k:automatic[k] for k in ('rev','enabled','standby_min','charging_min','min_aux_soc_pct')}
        args.update(enabled=True,min_aux_soc_pct=50)
        self.assertIn('auto_refresh',self.g.call(self.g.request('auto_refresh.set',args)))
        self.g.set('bsi.aux_battery_soc_pct_x10',490,False)
        self.sample();self.lib.gw_vehicle_control_tick()
        self.g.advance(48*HOUR);self.lib.gw_vehicle_control_tick();self.lib.gw_low_power_tick()
        self.assertEqual('low_aux_battery',self.g.call(self.g.request('auto_refresh.get',{}))['auto_refresh']['state'])
        self.assertEqual('entering',self.status()['state'])
        self.assertEqual(0,self.g.read('request.vehicle.generation'))

if __name__=='__main__': unittest.main()
