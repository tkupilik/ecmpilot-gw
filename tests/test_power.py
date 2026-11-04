"""Real manual power-policy state machine and varstore; PM hardware substituted."""
import unittest
from test_udp import Codec


class PowerTests(unittest.TestCase):
    def setUp(self):
        self.g = Codec()
        self.lib = self.g.lib
        self.lib.gw_test_power_reset()
        self.assertEqual(self.lib.gw_power_policy_init(), 0)

    def tick(self, ms=0):
        self.g.advance(ms)
        self.lib.gw_power_policy_tick()

    def complete(self):
        self.tick()
        self.tick(1500)

    def test_boot_is_normal_and_no_automatic_can_or_udp_trigger_exists(self):
        self.assertEqual(self.g.read('power.mode'), 0)
        self.assertEqual(self.g.read('power.cpu_min_mhz'), 160)
        self.assertEqual(self.g.read('power.cpu_max_mhz'), 160)
        self.g.set('bsi.power_management', 0, False)
        self.g.set('udp.wake_request_count', 42, False)
        self.tick(100000)
        self.assertEqual(self.lib.gw_test_power_value(0), 1)

    def test_transition_is_delayed_and_pending_request_cannot_be_overwritten(self):
        self.assertEqual(self.lib.gw_power_request(2), 0)
        self.assertEqual(self.lib.gw_power_request(0), 0x103)
        self.tick(); self.tick(1499)
        self.assertEqual(self.g.read('power.mode'), 0)
        self.tick(1)
        self.assertEqual(self.g.read('power.mode'), 2)
        self.assertEqual(self.g.read('power.auto_light_sleep'), 1)
        self.assertEqual(self.g.read('power.cpu_min_mhz'), 40)
        self.assertEqual(self.g.read('power.cpu_max_mhz'), 80)
        self.assertEqual(self.g.read('power.completed'), 1)

    def test_dfs_and_return_to_normal(self):
        self.assertEqual(self.lib.gw_power_request(1), 0); self.complete()
        self.assertEqual((self.g.read('power.cpu_min_mhz'), self.g.read('power.cpu_max_mhz')), (40, 160))
        self.assertEqual(self.g.read('power.auto_light_sleep'), 0)
        self.assertEqual(self.lib.gw_power_request(0), 0); self.complete()
        self.assertEqual((self.g.read('power.cpu_min_mhz'), self.g.read('power.cpu_max_mhz')), (160, 160))

    def test_failed_pm_change_keeps_applied_policy_and_completes_request(self):
        self.lib.gw_test_power_fail(0x101)
        self.assertEqual(self.lib.gw_power_request(2), 0); self.complete()
        self.assertEqual(self.g.read('power.mode'), 0)
        self.assertEqual(self.g.read('power.last_error'), 0x101)
        self.assertEqual(self.g.read('power.completed'), 1)
        self.lib.gw_test_power_fail(0)
        self.assertEqual(self.lib.gw_power_request(2), 0); self.complete()
        self.assertEqual(self.g.read('power.mode'), 2)

    def test_force_overlays_cannot_trigger_or_block_hardware_transitions(self):
        self.g.set('request.power.mode', 2)
        self.g.set('request.power.generation', 999)
        self.g.set('power.completed', 77)
        self.tick(5000)
        self.assertEqual(self.lib.gw_test_power_value(0), 1)
        self.assertEqual(self.lib.gw_power_request(1), 0); self.complete()
        self.assertEqual(self.lib.gw_test_power_value(1), 40)
        self.assertEqual(self.lib.gw_test_power_value(2), 160)
        self.assertEqual(self.lib.gw_test_power_value(3), 0)

    def test_invalid_requests_are_rejected(self):
        for mode in (-1, 4, 99): self.assertEqual(self.lib.gw_power_request(mode), 0x102)
        self.tick(10000)
        self.assertEqual(self.lib.gw_test_power_value(0), 1)

    def deep(self):
        self.assertEqual(self.lib.gw_power_request(3), 0)
        self.complete()
        self.assertEqual(self.g.read('power.deep_stage'), 1)

    def acknowledge(self, name, value):
        self.g.set(name, value, False)
        owner = name.split('.')[0]
        if owner in ('can', 'network'):
            self.g.set(owner + '.suspend_completed', self.g.read('request.' + owner + '.suspend'), False)
        self.tick()

    def test_deep_sleep_waits_for_each_owner_then_recovers_refused_entry(self):
        self.deep()
        self.assertEqual(self.g.read('request.can.suspend'), 1)
        self.assertEqual(self.g.read('request.network.suspend'), 0)
        self.assertEqual(self.lib.gw_test_power_value(4), 0)
        self.acknowledge('can.suspended', 1)
        self.assertEqual(self.g.read('request.network.suspend'), 1)
        self.assertEqual(self.lib.gw_test_power_value(4), 0)
        self.acknowledge('network.suspended', 1)
        self.assertEqual(self.lib.gw_test_power_value(4), 1)
        self.assertEqual(self.g.read('power.deep_stage'), 4)
        self.assertEqual(self.g.read('request.can.suspend'), 0)
        self.assertEqual(self.g.read('request.network.suspend'), 0)
        self.assertEqual(self.lib.gw_power_request(0), 0x103)
        self.acknowledge('can.suspended', 0)
        self.acknowledge('network.suspended', 0)
        self.assertEqual(self.g.read('power.completed'), 1)
        self.assertEqual(self.g.read('power.deep_stage'), 0)
        self.assertEqual(self.g.read('power.last_error'), 0x103)
        self.assertEqual(self.g.read('power.mode'), 0)

    def test_deep_handoff_timeout_never_calls_sleep_and_releases_requests(self):
        self.deep()
        self.tick(5000)
        self.assertEqual(self.lib.gw_test_power_value(4), 0)
        self.assertEqual(self.g.read('request.can.suspend'), 0)
        self.tick()
        self.assertEqual(self.g.read('power.last_error'), 0x107)
        self.assertEqual(self.g.read('power.completed'), 1)

    def test_can_stop_failure_preserves_network_access(self):
        self.deep()
        self.acknowledge('can.suspend_error', 0x101)
        self.assertEqual(self.g.read('request.network.suspend'), 0)
        self.acknowledge('can.suspended', 0)
        self.assertEqual(self.g.read('power.last_error'), 0x101)
        self.assertEqual(self.lib.gw_test_power_value(4), 0)

    def test_network_stop_failure_resumes_can_without_entering_sleep(self):
        self.deep()
        self.acknowledge('can.suspended', 1)
        self.acknowledge('network.suspend_error', 0x101)
        self.assertEqual(self.g.read('request.can.suspend'), 0)
        self.assertEqual(self.g.read('request.network.suspend'), 0)
        self.acknowledge('can.suspended', 0)
        self.acknowledge('network.suspended', 0)
        self.assertEqual(self.g.read('power.completed'), 1)
        self.assertEqual(self.g.read('power.last_error'), 0x101)
        self.assertEqual(self.lib.gw_test_power_value(4), 0)

    def test_force_cannot_fake_suspend_acknowledgements_or_inject_error(self):
        self.deep()
        for name, value in [('can.suspended', 1), ('network.suspended', 1),
                            ('can.suspend_error', 0x101), ('network.suspend_error', 0x101)]:
            self.g.set(name, value)
        self.tick()
        self.assertEqual(self.g.read('power.deep_stage'), 1)
        self.assertEqual(self.lib.gw_test_power_value(4), 0)
        self.assertEqual(self.g.read('request.network.suspend'), 0)
        self.acknowledge('can.suspended', 1)
        self.acknowledge('network.suspended', 1)
        self.assertEqual(self.lib.gw_test_power_value(4), 1)

    def test_recovery_timeout_completes_without_a_second_entry(self):
        self.deep()
        self.acknowledge('can.suspended', 1)
        self.acknowledge('network.suspended', 1)
        self.tick(5000)
        self.assertEqual(self.g.read('power.completed'), 1)
        self.assertEqual(self.lib.gw_test_power_value(4), 1)


    def test_old_owner_error_is_not_consumed_before_new_acknowledgement(self):
        self.g.set('network.suspend_error', 0x101, False)
        self.deep()
        self.acknowledge('can.suspended', 1)
        self.tick(1000)
        self.assertEqual(self.g.read('power.deep_stage'), 2)
        self.g.set('network.suspend_error', 0, False)
        self.acknowledge('network.suspended', 1)
        self.assertEqual(self.lib.gw_test_power_value(4), 1)

    def test_deep_retry_rejected_until_owners_have_acknowledged_resume(self):
        self.deep()
        self.acknowledge('can.suspended', 1)
        self.acknowledge('network.suspended', 1)
        self.tick(5000)
        self.assertEqual(self.lib.gw_power_request(3), 0x103)


    def test_generation_rollover_and_owned_descriptors(self):
        self.g.set('request.power.generation', 0x7fffffff, False)
        self.g.set('power.completed', 0x7fffffff, False)
        self.assertEqual(self.lib.gw_power_request(1), 0); self.complete()
        self.assertEqual(self.g.read('power.completed'), 1)
        for name in self.g.ids:
            if name.startswith(('power.', 'request.power.')):
                self.assertEqual(self.lib.gw_process_data_manual(self.g.ids[name]), 0)


if __name__ == '__main__': unittest.main()
