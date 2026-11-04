"""Wake encoding remains independent of scheduling; no CAN adapter IO."""
import ctypes as c
import unittest
from test_udp import Codec

class WakeConfigTests(unittest.TestCase):
    def setUp(self):
        self.g=Codec('udp-info-test.dll');self.g.lib.gw_can_wake_reset()
        self.g.lib.gw_can_wake_encode.argtypes=[c.POINTER(c.c_uint32),c.POINTER(c.c_uint16)]
        self.g.lib.gw_can_wake_encode.restype=c.c_bool
    def encode(self):
        ident,payload=c.c_uint32(),c.c_uint16()
        if self.g.lib.gw_can_wake_encode(c.byref(ident),c.byref(payload)):return ident.value,payload.value
    def test_default_telematics_id_and_payload(self):
        self.assertEqual((0x43C,0x80),self.encode())
    def test_force_and_release_preserve_two_byte_atomic_configuration(self):
        self.g.set('can.wake_tx_id',0x401);self.g.set('can.wake_tx_payload_u16',0xA55A)
        self.assertEqual((0x401,0xA55A),self.encode())
        for key in ['can.wake_tx_id','can.wake_tx_payload_u16']:self.g.lib.varstore_force(self.g.ids[key],False,0)
        self.assertEqual((0x43C,0x80),self.encode())
    def test_payload_boundaries(self):
        for n in [0,2,128,256,65535]:
            self.g.set('can.wake_tx_payload_u16',n);self.assertEqual((0x43C,n),self.encode())
        for n in [-1,65536]:
            self.g.set('can.wake_tx_payload_u16',n);self.assertIsNone(self.encode())
    def test_cluster_version_and_other_identifiers_are_rejected(self):
        for n in [0x5DF,0x036,-1,0x800]:
            self.g.set('can.wake_tx_id',n);self.assertIsNone(self.encode())
    def test_test_mode_inhibits_encoding(self):
        for key in ['test.enabled']:
            self.g.set(key,1,False);self.assertIsNone(self.encode());self.g.set(key,0,False)
