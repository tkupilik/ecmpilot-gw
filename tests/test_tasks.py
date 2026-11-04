"""Actual task snapshot/JSON adapter with substituted RTOS and HTTP boundaries."""
import ctypes as c
import json
from pathlib import Path
import unittest


class TaskStatsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = c.CDLL(str(Path(__file__).resolve().parents[1] / 'build-host-tests/udp-test.dll'))
        cls.lib.gw_test_tasks.argtypes = [c.c_int]
        cls.lib.gw_test_tasks.restype = c.c_char_p
        cls.lib.gw_test_tasks_status.restype = c.c_char_p

    def test_snapshot_units_affinity_and_64_bit_counters(self):
        data = json.loads(self.lib.gw_test_tasks(0))
        self.assertEqual((6000000000, 2, 64), (data['sampled_us'], data['cores'], data['capacity']))
        self.assertEqual([0, 1, None], [task['core'] for task in data['tasks']])
        self.assertEqual([0, 1, None], [task['idle_core'] for task in data['tasks']])
        self.assertEqual(['running', 'running', 'blocked'], [task['state'] for task in data['tasks']])
        worker = data['tasks'][2]
        self.assertEqual(('worker', 'blocked', 5000000002, 1536, '0x3fc90100'),
                         (worker['name'], worker['state'], worker['runtime_us'], worker['stack_min_free_bytes'], worker['stack_base']))
        self.assertEqual(0, self.lib.gw_test_tasks_errors())

    def test_capacity_failure_releases_lock_and_returns_503(self):
        self.assertIn(b'capacity exceeded', self.lib.gw_test_tasks(1))
        self.assertEqual(b'503 Service Unavailable', self.lib.gw_test_tasks_status())
        self.assertEqual(0, self.lib.gw_test_tasks_errors())

    def test_heap_capability_views_and_absent_psram(self):
        data = json.loads(self.lib.gw_test_tasks(0))
        ram, dma, psram = data['heap']
        self.assertEqual('Internal RAM', ram['name'])
        self.assertEqual((300000, 200000, 100000, 90000, 64000, 7), tuple(ram[k] for k in ('total_bytes','allocated_bytes','free_bytes','minimum_free_bytes','largest_free_block_bytes','free_blocks')))
        self.assertEqual(50000, dma['free_bytes'])
        self.assertEqual(0, psram['total_bytes'])
        self.assertEqual(0, self.lib.gw_test_tasks_errors())
