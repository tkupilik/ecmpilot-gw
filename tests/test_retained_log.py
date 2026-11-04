"""Actual RTC-bank code and live log integration, with simulated warm resets."""
import ctypes as C
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
lib = C.CDLL(str(ROOT / 'build-host-log' / 'retained-log.dll'))


class Entry(C.Structure):
    _fields_ = [('sequence', C.c_uint64), ('text', C.c_char * 256)]


class Page(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ('oldest', 'latest', 'next', 'lost')] + [('count', C.c_size_t)]


for name in ('gw_retained_log_read', 'gw_log_buffer_read'):
    fn = getattr(lib, name)
    fn.argtypes = [C.c_uint64, C.POINTER(Entry), C.c_size_t]
    fn.restype = Page
lib.gw_log_buffer_append.argtypes = [C.c_char_p]


def read(previous=True, after=0, capacity=16):
    entries = (Entry * 16)()
    fn = lib.gw_retained_log_read if previous else lib.gw_log_buffer_read
    page = fn(after, entries, capacity)
    return page, [(entry.sequence, bytes(entry.text).decode()) for entry in entries[:page.count]]


class RetainedLogTests(unittest.TestCase):
    def setUp(self):
        lib.test_cold_boot()

    def append(self, text):
        lib.gw_log_buffer_append(text.encode())

    def test_warm_reset_preserves_tail_and_startup_cannot_overwrite_it(self):
        for i in range(30): self.append(f'before {i}\n')
        lib.test_warm_boot()
        for i in range(100): self.append(f'startup {i}\n')
        page, entries = read()
        self.assertEqual([text for _, text in entries], [f'before {i}\n' for i in range(18, 30)])
        self.assertEqual((page.oldest, page.latest, page.lost), (19, 30, 18))
        self.assertEqual(read(False)[1][0][1], 'startup 36\n')

    def test_successive_resets_rotate_to_immediately_previous_boot(self):
        self.append('first')
        lib.test_warm_boot()
        self.append('second')
        lib.test_warm_boot()
        self.assertEqual(read()[1], [(1, 'second')])
        self.assertEqual(lib.gw_retained_log_boot_number(), 3)

    def test_cold_start_and_empty_log_have_no_previous_entries(self):
        self.assertEqual(read()[1], [])
        self.append('old')
        lib.gw_retained_log_boot(False)
        self.assertEqual(lib.gw_retained_log_count(), 0)

    def test_corrupt_record_is_discarded_without_losing_other_entries(self):
        self.append('good'); self.append('bad')
        lib.test_corrupt_latest(); lib.test_warm_boot()
        self.assertEqual(read()[1], [(1, 'good')])

    def test_interrupted_ring_overwrite_retains_other_complete_entries(self):
        for i in range(12): self.append(str(i))
        lib.test_interrupted_append(); lib.test_warm_boot()
        self.assertEqual([text for _, text in read()[1]], [str(i) for i in range(1, 12)])

    def test_invalid_bank_header_is_ignored(self):
        self.append('invalid bank'); lib.test_invalid_header(); lib.test_warm_boot()
        self.assertEqual(read()[1], [])

    def test_paging_and_long_text_remain_terminated(self):
        for _ in range(12): self.append('X' * 500)
        lib.test_warm_boot()
        first, entries = read(capacity=5)
        self.assertEqual((first.next, len(entries)), (5, 5))
        self.assertTrue(all(len(text) == 255 for _, text in entries))
        self.assertEqual(len(read(after=first.next)[1]), 7)
        self.assertEqual(read(after=100)[0].next, 12)

    def test_concurrent_logging_keeps_coherent_records(self):
        def writer(n):
            for i in range(1000): self.append(f'{n}:{i}:' + str(n) * 200)
        with ThreadPoolExecutor(max_workers=2) as pool:
            list(pool.map(writer, (1, 2)))
        lib.test_warm_boot()
        entries = read()[1]
        self.assertEqual(len(entries), 12)
        for _, text in entries:
            writer_id, _, payload = text.split(':')
            self.assertEqual(payload, writer_id * 200)
        self.assertLessEqual(lib.test_storage_size(), 7168)


if __name__ == '__main__':
    unittest.main()
