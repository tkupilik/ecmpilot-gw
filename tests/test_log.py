"""Actual native ring/capture hook with concurrent writers; no ESP socket mock."""
import ctypes as c
from pathlib import Path
import re
import threading
import unittest


class Entry(c.Structure):
    _fields_ = [('seq', c.c_uint64), ('text', c.c_char * 256)]


class Page(c.Structure):
    _fields_ = [('oldest', c.c_uint64), ('latest', c.c_uint64), ('next', c.c_uint64),
                ('lost', c.c_uint64), ('count', c.c_size_t)]


class LogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = c.CDLL(str(Path(__file__).resolve().parents[1] / 'build-host-tests/udp-test.dll'))
        cls.lib.gw_log_buffer_append.argtypes = [c.c_char_p]
        cls.lib.gw_log_buffer_read.argtypes = [c.c_uint64, c.POINTER(Entry), c.c_size_t]
        cls.lib.gw_log_buffer_read.restype = Page
        cls.lib.gw_log_boot_id.restype = c.c_char_p
        cls.lib.gw_test_log_emit.argtypes = [c.c_char_p]
        cls.lib.gw_test_log_calls.restype = c.c_uint
        cls.lib.gw_log_start()

    def setUp(self):
        self.lib.gw_log_buffer_reset()

    def page(self, after=0):
        entries = (Entry * 16)(); page = self.lib.gw_log_buffer_read(after, entries, 16)
        return page, [(entries[i].seq, bytes(entries[i].text)) for i in range(page.count)]

    def test_empty_cursor_and_paging(self):
        page, entries = self.page()
        self.assertEqual((0, 0, 0), (page.latest, page.next, page.count))
        self.lib.gw_log_buffer_append(b''); self.assertEqual(0, self.page()[0].latest)
        for n in range(20): self.lib.gw_log_buffer_append(f'{n}\n'.encode())
        page, entries = self.page()
        self.assertEqual((1, 20, 16, 0), (page.oldest, page.latest, page.next, page.lost))
        self.assertEqual(b'0\n', entries[0][1])
        page, entries = self.page(page.next)
        self.assertEqual(4, page.count); self.assertEqual(b'19\n', entries[-1][1])
        self.assertEqual(0, self.page(page.next)[0].count)
        self.assertEqual(1, self.page(1000)[1][0][0])

    def test_wrap_retains_last_64_and_reports_lost_entries(self):
        for n in range(100): self.lib.gw_log_buffer_append(f'entry {n}\n'.encode())
        page, entries = self.page()
        self.assertEqual((37, 100, 36, 16), (page.oldest, page.latest, page.lost, page.count))
        collected = entries
        while page.next < page.latest:
            page, entries = self.page(page.next); self.assertEqual(0, page.lost); collected += entries
        self.assertEqual(list(range(37, 101)), [seq for seq, _ in collected])

    def test_capture_truncates_safely_and_still_calls_previous_console_sink(self):
        before = self.lib.gw_test_log_calls()
        text = ('prefix:' + 'ě' * 300 + '\n').encode()
        self.assertEqual(len(text), self.lib.gw_test_log_emit(text))
        self.assertEqual(before + 1, self.lib.gw_test_log_calls())
        _, entries = self.page()
        captured = entries[0][1]
        self.assertLessEqual(len(captured), 255)
        self.assertTrue(captured.endswith(b' [truncated]\n'))
        self.assertIn('ě', captured.decode('utf-8'))
        self.assertRegex(self.lib.gw_log_boot_id().decode(), r'^[0-9a-f]{32}$')

    def test_concurrent_capture_and_snapshot_have_no_torn_records(self):
        errors = []
        def writer(letter):
            try:
                for n in range(3000): self.lib.gw_test_log_emit(f'{letter}:{n:06d}:'.encode() + letter.encode()*80 + b'\n')
            except Exception as error: errors.append(error)
        threads = [threading.Thread(target=writer, args=(letter,), daemon=True) for letter in 'AB']
        for thread in threads: thread.start()
        cursor = 0
        while any(thread.is_alive() for thread in threads):
            page, entries = self.page(cursor)
            for seq, text in entries:
                self.assertGreater(seq, cursor)
                self.assertRegex(text, rb'^[AB]:\d{6}:[AB]{80}\n$')
                self.assertEqual(text[:1]*80, text[9:89])
                cursor = seq
        for thread in threads: thread.join(2); self.assertFalse(thread.is_alive())
        self.assertEqual([], errors); self.assertEqual(6000, self.page()[0].latest)
