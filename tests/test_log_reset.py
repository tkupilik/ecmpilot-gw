"""Reset reason + production capture hook retain the previous boot independently."""
import ctypes as c
import unittest
from test_udp import Codec
from test_log import Entry, Page


class LogResetTests(unittest.TestCase):
    def setUp(self):
        self.lib = Codec().lib
        self.lib.gw_log_reset_name.restype = c.c_char_p
        self.lib.gw_log_buffer_append.argtypes = [c.c_char_p]
        self.lib.gw_retained_log_read.argtypes = [c.c_uint64, c.POINTER(Entry), c.c_size_t]
        self.lib.gw_retained_log_read.restype = Page
        self.lib.gw_test_reset_reason(1)
        self.lib.gw_log_start()

    def test_brownout_keeps_previous_tail_despite_new_startup_logs(self):
        self.lib.gw_log_buffer_append(b'last wake before reset\n')
        self.lib.gw_test_reset_reason(9)
        self.lib.gw_log_start()
        self.assertEqual(9, self.lib.gw_log_reset_reason())
        self.assertEqual(b'BROWNOUT', self.lib.gw_log_reset_name())
        for n in range(100):
            self.lib.gw_log_buffer_append(f'new boot {n}\n'.encode())
        entries = (Entry * 16)()
        page = self.lib.gw_retained_log_read(0, entries, 16)
        self.assertEqual(1, page.count)
        self.assertEqual(b'last wake before reset\n', bytes(entries[0].text))

    def test_reset_names_distinguish_software_panic_and_watchdogs(self):
        for reason, name in [(3, 'SOFTWARE'), (4, 'PANIC'),
                             (5, 'INTERRUPT_WATCHDOG'), (6, 'TASK_WATCHDOG')]:
            self.lib.gw_test_reset_reason(reason)
            self.lib.gw_log_start()
            self.assertEqual(name.encode(), self.lib.gw_log_reset_name())


if __name__ == '__main__':
    unittest.main()
