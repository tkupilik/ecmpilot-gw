"""Real NVS config validation, secret redaction, atomic persistence and retries."""
import ctypes as c
import json
import unittest
from test_udp import Codec


class HaSettingsTests(unittest.TestCase):
    def setUp(self):
        self.g = Codec('udp-info-test.dll')
        self.lib = self.g.lib
        self.lib.gw_test_nvs_reset()
        self.lib.gw_ha_settings_save.argtypes = [c.c_void_p, c.POINTER(c.c_char_p)]
        self.lib.gw_ha_settings_json.restype = c.c_void_p
        self.lib.cJSON_Parse.argtypes = [c.c_char_p]
        self.lib.cJSON_Parse.restype = c.c_void_p
        self.lib.cJSON_Delete.argtypes = [c.c_void_p]
        self.lib.cJSON_PrintUnformatted.argtypes = [c.c_void_p]
        self.lib.cJSON_PrintUnformatted.restype = c.c_void_p
        self.lib.cJSON_free.argtypes = [c.c_void_p]
        self.assertEqual(0, self.lib.gw_ha_settings_init())

    def tearDown(self):
        self.lib.gw_test_nvs_reset()

    def read(self):
        ptr = self.lib.gw_ha_settings_json()
        self.assertTrue(ptr)
        text = self.lib.cJSON_PrintUnformatted(ptr)
        try:
            return json.loads(c.string_at(text))
        finally:
            self.lib.cJSON_free(text)
            self.lib.cJSON_Delete(ptr)

    def save(self, **changes):
        old = self.read()
        payload = {k: old[k] for k in ('rev', 'enabled', 'host', 'port', 'username', 'period_s')}
        payload.update(changes)
        ptr = self.lib.cJSON_Parse(json.dumps(payload).encode())
        reason = c.c_char_p()
        try:
            result = self.lib.gw_ha_settings_save(ptr, c.byref(reason))
            return result, reason.value
        finally:
            self.lib.cJSON_Delete(ptr)

    def test_defaults_disabled_and_never_emit_vehicle_requests(self):
        self.assertEqual(dict(rev=1, enabled=False, host='', port=1883, username='', has_password=False, period_s=1), self.read())
        self.save(enabled=True, host='homeassistant.lan', username='pilot', password='secret')
        self.assertEqual(0, self.g.read('request.vehicle.generation'))

    def test_save_reboot_password_preserve_and_clear_without_disclosure(self):
        self.assertEqual((0, None), self.save(enabled=True, host='192.168.1.2', password='private'))
        self.assertTrue(self.read()['has_password'])
        self.assertNotIn('password', self.read())
        self.assertNotIn('private', json.dumps(self.read()))
        self.save(period_s=5)
        self.assertEqual(0, self.lib.gw_ha_settings_init())
        self.assertEqual(5, self.read()['period_s'])
        self.assertTrue(self.read()['has_password'])
        self.save(password='')
        self.assertFalse(self.read()['has_password'])

    def test_retry_is_idempotent_and_stale_edit_conflicts(self):
        self.assertEqual((0, None), self.save(host='ha.lan', rev=1))
        self.assertEqual(2, self.read()['rev'])
        self.assertEqual((0, None), self.save(host='ha.lan', rev=1))
        self.assertEqual(b'settings_conflict', self.save(host='other.lan', rev=1)[1])
        self.assertEqual('ha.lan', self.read()['host'])

    def test_invalid_inputs_and_failed_commit_do_not_change_live_configuration(self):
        initial = self.read()
        for changes in [dict(enabled=True), dict(host='mqtt://ha:1883'), dict(host='ha/commands'),
                        dict(port=0), dict(port=65536), dict(port=1.5), dict(period_s=0),
                        dict(period_s=61), dict(host='a'*128), dict(username='u'*65),
                        dict(password='p'*129), dict(unknown=True)]:
            self.assertNotEqual(0, self.save(**changes)[0], changes)
            self.assertEqual(initial, self.read())
        self.lib.gw_test_nvs_fail_commit(1)
        self.assertEqual(b'storage_error', self.save(host='ha.lan')[1])
        self.assertEqual(initial, self.read())
        self.lib.gw_test_nvs_fail_commit(0)
        self.assertEqual(0, self.lib.gw_ha_settings_init())
        self.assertEqual(initial, self.read())
