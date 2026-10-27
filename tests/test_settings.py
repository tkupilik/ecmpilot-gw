"""Test actual settings code using the host DLL's deterministic NVS substitute."""
import ctypes as c
from pathlib import Path
import unittest


class Profile(c.Structure):
    _fields_ = [('ssid', c.c_char * 33), ('password', c.c_char * 65)]


class Settings(c.Structure):
    _fields_ = [('version', c.c_uint32), ('count', c.c_uint32), ('networks', Profile * 8)]


class HostnameSettingsTests(unittest.TestCase):
    def setUp(self):
        self.lib = c.CDLL(str(Path(__file__).resolve().parents[1] / 'build-host-tests/udp-test.dll'))
        self.lib.gw_settings_get.argtypes = [c.POINTER(Settings)]
        self.lib.gw_settings_save.argtypes = [c.POINTER(Settings)]
        self.lib.gw_settings_save_network.argtypes = [c.POINTER(Settings), c.c_char_p]
        self.lib.gw_hostname_get.argtypes = [c.c_char_p]
        self.lib.gw_hostname_valid.argtypes = [c.c_char_p]
        self.lib.gw_hostname_valid.restype = c.c_bool
        self.lib.gw_test_nvs_hostname.argtypes = [c.c_char_p]
        self.lib.gw_settings_save_wifi.argtypes = [c.POINTER(Settings), c.c_char_p, c.c_char_p]
        self.lib.gw_ap_password_get.argtypes = [c.c_char_p]
        self.lib.gw_test_nvs_ap_password.argtypes = [c.c_char_p]
        self.lib.gw_test_nvs_reset()
        self.assertEqual(0, self.lib.gw_settings_init())

    def settings(self, ssid=None):
        s = Settings(); self.lib.gw_settings_get(c.byref(s))
        if ssid is not None:
            s.count = 1; s.networks[0].ssid = ssid; s.networks[0].password = b'password123'
        return s

    def hostname(self):
        b = c.create_string_buffer(33); self.lib.gw_hostname_get(b); return b.value

    def test_defaults_and_legacy_wifi_blob_need_no_migration(self):
        self.assertEqual(b'ecmpilot-gw', self.hostname())
        old = self.settings(b'Home'); self.assertEqual(0, self.lib.gw_settings_save(c.byref(old)))
        self.assertEqual(792, c.sizeof(Settings))
        self.assertEqual(792, self.lib.gw_test_nvs_blob_size())
        self.assertEqual(0, self.lib.gw_settings_init())
        self.assertEqual(b'Home', self.settings().networks[0].ssid)
        self.assertEqual(b'password123', self.settings().networks[0].password)
        self.assertEqual(b'ecmpilot-gw', self.hostname())

    def test_hostname_survives_reload_and_original_wifi_save_api(self):
        s = self.settings(b'Home')
        self.assertEqual(0, self.lib.gw_settings_save_network(c.byref(s), b'My-Corsa'))
        self.assertEqual(b'my-corsa', self.hostname())
        self.assertEqual(0, self.lib.gw_settings_init())
        self.assertEqual(b'my-corsa', self.hostname())
        s.count = 0
        self.assertEqual(0, self.lib.gw_settings_save(c.byref(s)))
        self.assertEqual(0, self.lib.gw_settings_init())
        self.assertEqual(b'my-corsa', self.hostname())
        self.assertEqual(0, self.settings().count)

    def test_saved_hostname_without_a_wifi_blob_is_loaded(self):
        self.lib.gw_test_nvs_hostname(b'Garage-GW')
        self.assertEqual(0, self.lib.gw_settings_init())
        self.assertEqual(b'garage-gw', self.hostname())
        self.assertEqual(0, self.settings().count)

    def test_invalid_labels_do_not_replace_network_configuration(self):
        s = self.settings(b'Home'); self.lib.gw_settings_save(c.byref(s))
        changed = self.settings(b'Other')
        for name in [b'', b'-corsa', b'corsa-', b'corsa.lan', b'http://corsa', b'my_corsa', b'my corsa', b'a'*33, b'\xff']:
            self.assertEqual(0x102, self.lib.gw_settings_save_network(c.byref(changed), name), name)
        self.assertEqual(b'Home', self.settings().networks[0].ssid)
        self.assertEqual(b'ecmpilot-gw', self.hostname())

    def test_failed_commit_does_not_publish_new_ram_settings(self):
        s = self.settings(b'Home'); self.lib.gw_settings_save_network(c.byref(s), b'old-name')
        self.lib.gw_test_nvs_fail_commit(1)
        changed = self.settings(b'Other')
        self.assertNotEqual(0, self.lib.gw_settings_save_network(c.byref(changed), b'new-name'))
        self.assertEqual(b'old-name', self.hostname())
        self.assertEqual(b'Home', self.settings().networks[0].ssid)

    def test_label_boundaries_and_corrupt_saved_hostname(self):
        s = self.settings()
        for valid in [b'a', b'1', b'a-b', b'a'*32]:
            self.assertEqual(0, self.lib.gw_settings_save_network(c.byref(s), valid))
        self.lib.gw_test_nvs_hostname(b'invalid.name')
        self.assertEqual(0x103, self.lib.gw_settings_init())


    def ap_password(self):
        b = c.create_string_buffer(64)
        self.assertEqual(0, self.lib.gw_ap_password_get(b))
        return b.value

    def test_ap_password_legacy_reload_and_omission_preservation(self):
        self.lib.gw_test_nvs_ap_password(b'ABCDEFGH23456789')
        self.assertEqual(b'ABCDEFGH23456789', self.ap_password())
        s = self.settings(b'Home')
        self.assertEqual(0, self.lib.gw_settings_save_wifi(c.byref(s), b'My-GW', b'new AP password'))
        self.assertEqual(0, self.lib.gw_settings_init())
        self.assertEqual(b'new AP password', self.ap_password())
        s.count = 0
        self.assertEqual(0, self.lib.gw_settings_save_network(c.byref(s), None))
        self.assertEqual(b'new AP password', self.ap_password())
        self.assertEqual(b'my-gw', self.hostname())
        self.assertEqual(792, self.lib.gw_test_nvs_blob_size())

    def test_invalid_ap_password_and_failed_commit_preserve_settings(self):
        self.lib.gw_test_nvs_ap_password(b'original-password')
        s = self.settings(b'Home')
        self.assertEqual(0, self.lib.gw_settings_save(c.byref(s)))
        changed = self.settings(b'Other')
        for value in [b'', b'short', b'a'*64, b'bad\npassword', b'nonascii\xff']:
            self.assertEqual(0x102, self.lib.gw_settings_save_wifi(c.byref(changed), b'new-name', value))
        self.assertEqual(b'Home', self.settings().networks[0].ssid)
        self.assertEqual(b'original-password', self.ap_password())
        self.lib.gw_test_nvs_fail_commit(1)
        self.assertNotEqual(0, self.lib.gw_settings_save_wifi(c.byref(changed), b'new-name', b'new password'))
        self.assertEqual(b'original-password', self.ap_password())
        self.assertEqual(b'ecmpilot-gw', self.hostname())

    def test_ap_password_length_boundaries(self):
        for length in [8, 16, 63]:
            password = b'a'*length
            self.assertEqual(0, self.lib.gw_settings_save_wifi(c.byref(self.settings()), None, password))
            self.assertEqual(password, self.ap_password())
        self.lib.gw_test_nvs_ap_password(b'bad')
        buffer = c.create_string_buffer(64)
        self.assertEqual(0x103, self.lib.gw_ap_password_get(buffer))
        self.assertEqual(b'', buffer.value)


if __name__ == '__main__':
    unittest.main()
