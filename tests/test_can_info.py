"""Backported INFO RX handlers + actual GW varstore/UDP + capture replay."""
import csv
import ctypes as c
import hashlib
import json
from pathlib import Path
import unittest
from test_udp import Codec, ROOT

CAPTURE = ROOT/'logs/info_start_jizda.csv'
NAMES = {'SPEED': 'vehicle.speed_kmh', 'ODO': 'vehicle.odometer_km', 'SOC_PCT_X10': 'battery.soc_pct_x10', 'RANGE_KM': 'vehicle.range_km_x10', 'OUTSIDE_MC': 'temperature.outside_mc', 'GEAR': 'vehicle.gear', 'DRIVE_READY': 'vehicle.drive_ready', 'LIGHT_LOW': 'vehicle.low_beam', 'SEATBELT_WARNING': 'vehicle.seatbelt_warning', 'PARKING_BRAKE': 'vehicle.parking_brake', 'SPEED_LIMIT_KMH': 'vehicle.speed_limit_kmh', 'TRACTION_POWER_W': 'vehicle.traction_power_w', 'VEHICLE_AWAKE': 'vehicle.awake', 'VEHICLE_PARKED': 'vehicle.parked', 'BATTERY_POWER_W': 'battery.power_w', 'DRIVE_POWER_PCT_X10': 'vehicle.drive_power_pct_x10', 'CAN_GEAR_CODE': 'can.gear_code', 'CAN_POWER_RAW': 'can.power_raw', 'CAN_NETWORK_PHASE': 'can.network_phase', 'SW_BUTTONS': 'steering.buttons', 'SW_NAV_COUNTER': 'steering.nav_counter', 'SW_VOLUME_COUNTER': 'steering.volume_counter', 'CMB_NAV_COUNTER': 'steering.cluster_nav_counter', 'SW_OVERFLOW_FLAGS': 'steering.overflow_flags', 'SW_PRESS_COUNT': 'steering.press.ok', 'CLIMATE_STATE': 'climate.state', 'CHARGE_STATE': 'charge.state'}
IDS = {0x036,0x0A2,0x0B6,0x0F6,0x11C,0x128,0x1E7,0x31A,0x3E0}

class CanRxTests(unittest.TestCase):
    def setUp(self):
        self.g = Codec('udp-info-test.dll')
        self.lib = self.g.lib
        self.lib.gw_can_info_decode.argtypes = [c.c_uint32,c.c_void_p,c.c_size_t,c.c_bool,c.c_bool]
        self.lib.gw_can_replay_step.argtypes = [c.c_uint32]
        self.lib.gw_can_info_init(); self.lib.gw_can_replay_reset()

    def ident(self, name): return self.g.ids[NAMES.get(name, name)]
    def value(self, name): return self.lib.varstore_read(self.ident(name),0,-99999)
    def check(self, name, expected): self.assertEqual(-99999 if expected is None else expected,self.value(name),name)
    def frame(self, ident, payload, ext=False, rtr=False):
        data=bytes(payload)
        return self.lib.gw_can_info_decode(ident,data,len(data),ext,rtr)

    def test_capture_every_selected_frame_against_independent_byte_extraction(self):
        count=0
        for row in csv.DictReader(CAPTURE.read_text().splitlines()):
            ident=int(row['ID'],16)
            if ident not in IDS: continue
            d=bytes(int(row[f'D{i}'],16) for i in range(1,int(row['LEN'])+1))
            self.assertEqual(1,self.frame(ident,d));count+=1
            if ident==0x036:
                p=d[4]&7
                self.check('VEHICLE_AWAKE',int(p in (1,3)) if p<=4 else None)
            elif ident==0x0A2:
                self.check('SW_NAV_COUNTER',int.from_bytes(d[0:1],'big',signed=True))
                self.check('CMB_NAV_COUNTER',int.from_bytes(d[3:4],'big',signed=True))
                self.check('SW_VOLUME_COUNTER',int.from_bytes(d[5:6],'big',signed=True))
            elif ident==0x0B6:
                raw=int.from_bytes(d[2:4],'big')
                self.check('SPEED',(raw+50)//100 if raw!=65535 else None)
            elif ident==0x0F6:
                raw=int.from_bytes(d[2:5],'big')
                self.check('ODO',raw//10 if raw!=0xFFFFFF else None)
                self.check('OUTSIDE_MC',d[6]*500-40000 if d[6]<=250 else None)
            elif ident==0x128:
                raw=d[1]>>4;gear=raw if raw<=3 else 4 if raw==15 else None
                self.check('GEAR',gear);self.check('DRIVE_READY',(d[7]>>2)&1)
                self.check('PARKING_BRAKE',(d[3]>>1)&1)
                self.check('LIGHT_LOW',(d[0]>>6)&1);self.check('SEATBELT_WARNING',(d[5]>>6)&1)
            elif ident==0x1E7:
                pct=((d[3]&1)*256+d[4])*5-500
                self.check('DRIVE_POWER_PCT_X10',pct if pct<=2000 else None)
                raw=d[3]>>1
                self.check('BATTERY_POWER_W',(raw-50)*1000 if raw<=100 else None)
            elif ident==0x31A:
                raw=d[0]*2+d[1]//128;self.check('RANGE_KM',raw*20 if raw<=500 else None)
            elif ident==0x3E0: self.check('SOC_PCT_X10',d[0]*10 if d[0]<=100 else None)
        self.assertEqual(1974,count)
        self.check('CLIMATE_STATE',None);self.check('CHARGE_STATE',None)
        self.check('TRACTION_POWER_W',None)

    def test_format_dlc_and_bus_layout_do_not_touch_existing_values(self):
        self.frame(0x3E0,[70,0,0,0,0,0,0,0]);self.g.advance(123)
        for ext,rtr,data in [(True,False,[10]*8),(False,True,[10]*8),(False,False,[10]*7),(False,False,[10]*9)]:
            self.assertEqual(-1,self.frame(0x3E0,data,ext,rtr))
        self.assertEqual(0,self.frame(0x5C3,[0,0,3,232,0,0,0,0])) # HS1 layout must not leak onto INFO.
        self.check('SOC_PCT_X10',700)
        self.assertEqual(123,self.g.call(self.g.request('state.get',{}))['state']['soc_pct']['age_ms'])

    def test_sentinels_and_bounds_invalidate_instead_of_zeroing(self):
        for raw,expected in [(0,0),(100,1000),(101,None),(127,None),(255,None)]:
            self.frame(0x3E0,[raw]+[0]*7);self.check('SOC_PCT_X10',expected)
        self.frame(0x0F6,[0,0,255,255,255,100,252,0]);self.check('ODO',None);self.check('OUTSIDE_MC',None)
        self.frame(0x0F6,[0,0,0,0,10,100,0,0]);self.check('ODO',1);self.check('OUTSIDE_MC',-40000)
        self.frame(0x31A,[255,128,0,0,0,0,0,0]);self.check('RANGE_KM',None)
        self.frame(0x1E7,[0,0,0,255,255,0,0,0]);self.check('BATTERY_POWER_W',None);self.check('DRIVE_POWER_PCT_X10',None)

    def test_gears_raw_diagnostics_and_ready_are_independent(self):
        for raw,gear in [(0,0),(1,1),(2,2),(3,3),(15,4),(10,None),(11,None),(4,None)]:
            self.frame(0x128,[0,raw<<4,0,0,0,0,0,4])
            self.check('GEAR',gear);self.check('DRIVE_READY',1);self.check('CAN_GEAR_CODE',raw)
            self.check('VEHICLE_PARKED',int(raw==0) if gear is not None else None)

    def test_sign_absent_end_and_unavailable_codes(self):
        for speed,kind,want in [(50,0,50),(90,1,None),(255,0,None),(254,0,None),(0,0,None),(130,3,None)]:
            self.assertEqual(1,self.frame(0x11C,[0,0,speed,kind<<6]))
            self.check('SPEED_LIMIT_KMH',want)

    def test_button_press_totals_no_repeat_startup_or_gap_events(self):
        base=self.ident('SW_PRESS_COUNT')
        before=self.lib.varstore_read(base,0,-1)
        self.frame(0x0A2,[0,0x20,0,0,0,0]) # Already held at startup: baseline.
        self.frame(0x0A2,[0,0,0,0,0,0]);self.frame(0x0A2,[0,0x20,0,0,0,0])
        self.frame(0x0A2,[0,0x20,0,0,0,0])
        self.assertEqual(before+1,self.lib.varstore_read(base,0,-1));self.check('SW_BUTTONS',1)
        self.frame(0x0A2,[0,0,0,0,0,0]);self.g.advance(1001)
        self.frame(0x0A2,[0,0x20,0,0,0,0])
        self.assertEqual(before+1,self.lib.varstore_read(base,0,-1))

    def test_counters_are_raw_signed_no_guessed_navigation_actions(self):
        self.frame(0x0A2,[0x80,0xC0,0,0xFF,0xC3,0x7F])
        self.check('SW_NAV_COUNTER',-128);self.check('CMB_NAV_COUNTER',-1)
        self.check('SW_VOLUME_COUNTER',127);self.assertGreater(self.value('SW_OVERFLOW_FLAGS'),0)

    def test_force_overlay_and_freshness_survive_real_producer_writes(self):
        self.g.set('battery.soc_pct_x10',682)
        self.frame(0x3E0,[70,0,0,0,0,0,0,0]);self.check('SOC_PCT_X10',682)
        self.lib.varstore_force(self.ident('SOC_PCT_X10'),False,0);self.check('SOC_PCT_X10',700)
        self.g.advance(5100)
        self.assertEqual(-1,self.lib.varstore_read(self.ident('SOC_PCT_X10'),5000,-1))
        self.frame(0x036,[0,0,0,0,1,0,0,0])
        self.assertEqual(-1,self.lib.varstore_read(self.ident('SOC_PCT_X10'),5000,-1))

    def test_generated_replay_is_exact_filtered_capture_with_original_order(self):
        meta=json.loads((ROOT/'docs/can-replay.json').read_text())
        self.assertEqual(meta['sha256'],hashlib.sha256(CAPTURE.read_bytes()).hexdigest())
        self.assertEqual(1974,self.lib.gw_can_replay_count());self.assertEqual(36117,self.lib.gw_can_replay_duration())
        import re
        text=(ROOT/'main/can_replay_data.inc').read_text()
        emitted=[]
        for ms,ident,length,payload in re.findall(r'\{(\d+), 0x([0-9A-F]+), (\d+), \{([^}]+)\}\}',text):
            emitted.append((int(ms),int(ident,16),int(length),bytes(int(v,16) for v in payload.split(','))))
        rows=list(csv.DictReader(CAPTURE.read_text().splitlines()));origin=int(rows[0]['Time Stamp']);expected=[]
        for r in rows:
            ident=int(r['ID'],16)
            if ident in IDS:
                n=int(r['LEN']);expected.append(((int(r['Time Stamp'])-origin)//1000,ident,n,bytes(int(r[f'D{i}'],16) for i in range(1,n+1))))
        self.assertEqual(expected,emitted)

    def test_full_timed_replay_loop_and_source_reset(self):
        total=0
        for ms in range(0,36117,10):
            self.lib.gw_test_set_time(ms);n=self.lib.gw_can_replay_step(ms)
            self.assertLessEqual(n,32);total+=n
        self.lib.gw_test_set_time(36116);total+=self.lib.gw_can_replay_step(36116)
        self.assertEqual(1974,total);self.check('SOC_PCT_X10',700);self.check('RANGE_KM',1560)
        self.lib.gw_test_set_time(36117);self.lib.gw_can_replay_step(36117)
        self.check('SOC_PCT_X10',None);self.check('SPEED',0)
        self.lib.gw_can_info_reset();self.check('GEAR',None)

    def test_capture_readings_are_exported_to_phone(self):
        for ms in range(0,1100,10):
            self.lib.gw_test_set_time(ms);self.lib.gw_can_replay_step(ms)
        phone=self.g.call(self.g.request('state.get',{}))['state']
        self.assertEqual(70,phone['soc_pct']['value']);self.assertEqual(158,phone['range_km']['value'])
        self.assertEqual(11.5,phone['outside_c']['value'])

class InfoPipelineTests(unittest.TestCase):
    def setUp(self):
        from test_can import Frame, Health
        self.Frame, self.Health = Frame, Health
        self.g = Codec('udp-info-test.dll'); self.lib=self.g.lib
        self.lib.gw_can_rx_accept.argtypes=[c.POINTER(Frame)]
        self.lib.gw_can_rx_publish_health.argtypes=[c.POINTER(Health)]
        self.lib.gw_can_rx_tick.argtypes=[c.c_uint32]
        self.lib.gw_process_data_manual.argtypes=[c.c_size_t]
        self.lib.gw_process_data_manual.restype=c.c_bool
        self.lib.gw_can_rx_data_init()
    def frame(self,ident,payload,rtr=False,ext=False):
        f=self.Frame(id=ident,dlc=len(payload),rtr=rtr,extended=ext)
        f.data[:len(payload)]=payload;self.lib.gw_can_rx_accept(c.byref(f))
    def health(self): self.lib.gw_can_rx_publish_health(c.byref(self.Health(2,0,0,0,0,0,0)))
    def state(self): return self.g.call(self.g.request('state.get',{}))['state']
    def test_info_can_to_real_gw_udp_units_and_profile_isolation(self):
        self.frame(0x3E0,bytes([70]+[0]*7))
        self.frame(0x31A,bytes([39,128]+[0]*6))
        self.frame(0x0F6,bytes([0,0,0,0,10,0,103,0]))
        state=self.state()
        self.assertEqual(70,state['soc_pct']['value']);self.assertEqual(158,state['range_km']['value'])
        self.assertEqual(11.5,state['outside_c']['value']);self.assertEqual(1,state['odometer_km']['value'])
        self.frame(0x5C3,bytes.fromhex('D0 00 02 C1 00 07 D0 00'))
        self.assertEqual(70,self.state()['soc_pct']['value'])
        self.assertIsNone(self.state()['voltage_v']['value']);self.health()
        self.assertEqual(3,self.g.read('can.decode_count'))
    def test_soc57_reference_capture_matches_myopel_120km_through_real_rx_and_udp(self):
        rows=list(csv.DictReader((ROOT/'logs/info_soc57_refresh.csv').read_text().splitlines()))
        origin=int(rows[0]['Time Stamp']);previous=0;valid_range=invalid_range=soc=0
        for row in rows:
            ident=int(row['ID'],16)
            if ident not in (0x31A,0x3E0):continue
            ms=(int(row['Time Stamp'])-origin)//1000
            self.g.advance(ms-previous);previous=ms
            payload=bytes(int(row[f'D{i}'],16) for i in range(1,int(row['LEN'])+1))
            self.frame(ident,payload)
            if ident==0x3E0:
                soc+=1;self.assertEqual(57,self.state()['soc_pct']['value'])
            elif payload[:2]==b'\xFF\x80':
                invalid_range+=1;self.assertIsNone(self.state()['range_km']['value'])
            else:
                valid_range+=1;self.assertEqual(b'\x1E\x00',payload[:2])
                self.assertEqual(1200,self.g.read('vehicle.range_km_x10'))
                self.assertEqual(120,self.state()['range_km']['value'])
        self.assertEqual((44,1,45),(valid_range,invalid_range,soc))
        state=self.state()
        self.assertEqual(57,state['soc_pct']['value']);self.assertEqual(120,state['range_km']['value'])
    def test_replay_switch_clears_observations_and_preserves_force(self):
        self.frame(0x3E0,bytes([60]+[0]*7));self.g.set('battery.soc_pct_x10',900)
        self.g.set('can.replay_enabled',1);self.lib.gw_can_rx_tick(1000)
        self.frame(0x3E0,bytes([20]+[0]*7)) # Real traffic remains raw-only while replay is active.
        self.assertEqual(90,self.state()['soc_pct']['value'])
        for ms in range(1010,2110,10):self.lib.gw_can_rx_tick(ms)
        self.lib.varstore_force(self.g.ids['battery.soc_pct_x10'],False,0)
        self.assertEqual(70,self.state()['soc_pct']['value'])
        self.g.set('can.replay_enabled',0);self.lib.gw_can_rx_tick(2200)
        self.assertIsNone(self.state()['soc_pct']['value'])
        self.frame(0x3E0,bytes([55]+[0]*7));self.assertEqual(55,self.state()['soc_pct']['value'])
        self.health();self.assertGreater(self.g.read('can.demo_frame_count'),0)
    def test_bad_dlc_rtr_and_unrelated_frames_do_not_refresh(self):
        self.frame(0x3E0,bytes([70]+[0]*7));self.g.advance(200)
        self.frame(0x3E0,bytes([30]+[0]*6));self.frame(0x3E0,bytes(8),rtr=True)
        self.frame(0x123,bytes(8));self.health()
        self.assertEqual(70,self.state()['soc_pct']['value'])
        self.assertEqual(200,self.state()['soc_pct']['age_ms'])
        self.assertEqual(1,self.g.read('can.decode_count'));self.assertEqual(1,self.g.read('can.decode_rejected_count'))
        self.assertEqual(1,self.g.read('can.rx.rtr_count'))
    def test_can_owned_phone_fields_only_allow_force_in_web(self):
        for name in ('vehicle.awake','vehicle.parked','vehicle.range_km_x10','temperature.outside_mc','vehicle.odometer_km'):
            self.assertFalse(self.lib.gw_process_data_manual(self.g.ids[name]),name)
        self.assertTrue(self.lib.gw_process_data_manual(self.g.ids['can.replay_enabled']))

if __name__=='__main__': unittest.main()
