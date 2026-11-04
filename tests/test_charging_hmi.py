"""396 RX -> source varstore -> vehicle task -> existing UDP/HA contracts."""
import ctypes as c
import csv
import json
import unittest
from test_udp import Codec, ROOT


class ChargingHmiTests(unittest.TestCase):
    def setUp(self):
        self.g=Codec('udp-info-test.dll'); self.lib=self.g.lib
        self.lib.gw_can_info_decode.argtypes=[c.c_uint32,c.c_void_p,c.c_size_t,c.c_bool,c.c_bool]
        self.lib.gw_can_info_init(); self.lib.gw_vehicle_init()
        self.lib.gw_ha_state_json.restype=c.c_void_p
        self.lib.cJSON_free.argtypes=[c.c_void_p]

    def frame(self, ident, data):
        return self.lib.gw_can_info_decode(ident,bytes(data),len(data),False,False)

    def value(self, name):
        return self.lib.varstore_read(self.g.ids[name],0,-99999)

    def status(self):
        return self.g.call(self.g.request('state.get',{}))['state']['charge']

    def test_all_source_enums_and_reserved_codes_keep_owners_separate(self):
        expected=[('disconnected',0,False),('charging',2,True),('fault',4,True),
                  ('waiting',1,True),('complete',3,True)]
        self.frame(0x160,bytes.fromhex('80 B4'))
        for raw in range(8):
            # All unrelated first-byte fields set: only bits4..2 are HMI.
            self.assertEqual(1,self.frame(0x396,[0xE3|(raw<<2),0x8E,0xA3,0,0xC8,9]))
            self.assertEqual(raw,self.value('bsi.charging_hmi_state'))
            self.lib.gw_vehicle_tick()
            out=self.status()
            if raw<5:
                label,state,connected=expected[raw]
                self.assertEqual(label,out['state']); self.assertEqual(connected,out['connected'])
                self.assertEqual(state,self.value('charge.state'))
            else:
                self.assertEqual('unknown',out['state']); self.assertIsNone(out['connected'])
            self.assertEqual(12600,self.value('bsi.aux_battery_voltage_mv'))
            self.assertEqual(815,self.value('bsi.aux_battery_soc_pct_x10'))

    def test_rx_stores_source_without_running_policy(self):
        self.frame(0x396,bytes.fromhex('04 9C AD 00 58 E0'))
        self.assertEqual(1,self.value('bsi.charging_hmi_state'))
        self.assertEqual(-99999,self.value('charge.state'))
        self.lib.gw_vehicle_tick()
        self.assertEqual(2,self.value('charge.state'))
        self.assertFalse(self.lib.gw_process_data_manual(self.g.ids['charge.state']))
        self.assertFalse(self.lib.gw_process_data_manual(self.g.ids['charge.connected']))

    def test_cached_age_force_release_and_source_reset(self):
        self.frame(0x160,bytes.fromhex('80 B4'))
        self.frame(0x396,bytes.fromhex('04 9C AD 00 58 E0'))
        self.lib.gw_vehicle_tick(); self.g.advance(60000)
        self.lib.gw_vehicle_tick()
        self.assertEqual(60000,self.status()['age_ms'])
        self.g.set('bsi.charging_hmi_state',3)
        self.lib.gw_vehicle_tick()
        self.assertEqual('waiting',self.status()['state'])
        self.lib.varstore_force(self.g.ids['bsi.charging_hmi_state'],False,0)
        self.lib.gw_vehicle_tick()
        self.assertEqual('charging',self.status()['state'])
        self.assertEqual(60000,self.status()['age_ms'])
        self.lib.gw_can_info_reset(); self.lib.gw_vehicle_tick()
        self.assertEqual('unknown',self.status()['state'])
        self.assertIsNone(self.status()['connected'])

    def test_charge_mode_alone_never_reports_charging(self):
        for last in (0xA0,0xA4):
            self.frame(0x31A,[0x1E,0,0,0,0x3F,0xE0,5,last])
            self.frame(0x160,[0x80 if last==0xA0 else 0xC0,0xB4])
            self.lib.gw_vehicle_tick()
            self.assertEqual('unknown',self.status()['state'])
        self.frame(0x396,bytes.fromhex('03 86 9E 00 C8 09'))  # Driving sample.
        self.lib.gw_vehicle_tick()
        self.assertEqual('disconnected',self.status()['state'])

    def test_both_real_charging_to_delayed_captures_feed_phone_and_ha(self):
        for name in ('info_charging.csv','info_start_odlozeni.csv'):
            self.lib.gw_can_info_reset(); self.lib.gw_vehicle_tick()
            seen=set()
            for row in csv.DictReader((ROOT/'logs'/name).read_text(encoding='utf-8-sig').splitlines()):
                ident=int(row['ID'],16)
                if ident not in (0x396,0x160): continue
                data=bytes(int(row[f'D{i}'],16) for i in range(1,int(row['LEN'])+1))
                self.assertEqual(1,self.frame(ident,data)); self.lib.gw_vehicle_tick()
                if ident==0x396:
                    raw=(data[0]>>2)&7;seen.add(raw)
                    self.assertEqual({0:'disconnected',1:'charging',3:'waiting'}[raw],self.status()['state'])
            self.assertEqual({0,1,3},seen)
            self.assertEqual('waiting',self.status()['state'])
            self.assertEqual('deferred',self.status()['mode'])
            pointer=self.lib.gw_ha_state_json()
            try:
                ha=json.loads(c.string_at(pointer))
                self.assertEqual('waiting',ha['charge_state'])
                self.assertTrue(ha['charge_connected'])
            finally: self.lib.cJSON_free(pointer)

    def test_wrong_dlc_and_unrelated_frame_do_not_change_source(self):
        self.frame(0x396,bytes.fromhex('04 9C AD 00 58 E0'))
        self.g.advance(125)
        self.assertEqual(-1,self.frame(0x396,bytes(8)))  # Do not accept HS3 DLC8.
        self.frame(0x3E0,[58,0,0,0,0,0,0,0]);self.lib.gw_vehicle_tick()
        self.assertEqual(1,self.value('bsi.charging_hmi_state'))
        self.assertEqual(1,self.lib.varstore_read(self.g.ids['bsi.charging_hmi_state'],125,-1))
        self.assertEqual(-1,self.lib.varstore_read(self.g.ids['bsi.charging_hmi_state'],124,-1))


if __name__=='__main__': unittest.main()
