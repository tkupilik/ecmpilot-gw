"""Production PT hierarchy, shared countdown, real codec and varstore, no CAN IO."""
import csv
import ctypes as c
import unittest
from test_udp import Codec, ROOT

class VehicleControlTests(unittest.TestCase):
    def setUp(self):
        self.g=Codec('udp-info-test.dll');self.lib=self.g.lib
        self.lib.gw_can_wake_reset();self.lib.gw_vehicle_control_init();self.lib.gw_can_info_init()
        self.lib.gw_can_info_decode.argtypes=[c.c_uint32,c.c_void_p,c.c_size_t,c.c_bool,c.c_bool]
        self.lib.gw_vehicle_control_prepare.argtypes=[c.POINTER(c.c_uint32),c.POINTER(c.c_uint32),c.POINTER(c.c_uint8),c.c_void_p]
        self.lib.gw_vehicle_control_prepare.restype=c.c_bool
        self.consumed=c.c_uint32();self.frames=[];self.last_bsi=0
        self.precond_data=bytes.fromhex("80 58 01 C2 00 07 FF");self.precond34=bytes.fromhex("00 00 07 FF 00 07 FF");self.last_precond=0
    def frame(self,ident,payload):
        d=bytes.fromhex(payload) if isinstance(payload,str) else bytes(payload)
        return self.lib.gw_can_info_decode(ident,d,len(d),False,False)
    def action(self,op='vehicle.refresh',args=None,rev=0):
        req=self.g.request(op,args or {},rev);req.update(deadline_ms=self.g.now+5000,domain_rev=rev)
        return req
    def start(self,req=None,normal=False):
        req=req or self.action()
        if normal:self.frame(0x036,'00 00 00 00 01 00 00 00');self.last_bsi=self.g.now
        self.assertEqual('accepted',self.g.call(req)['command']['phase'])
        self.lib.gw_vehicle_control_tick();return req
    def drain(self,error=0):
        ident,length,data=c.c_uint32(),c.c_uint8(),(c.c_uint8*8)()
        if not self.lib.gw_vehicle_control_prepare(c.byref(self.consumed),c.byref(ident),c.byref(length),data):return None
        result=(self.g.now,ident.value,bytes(data)[:length.value],self.consumed.value)
        self.frames.append(result);self.lib.gw_vehicle_control_submitted(self.consumed.value,error);return result
    def step(self,ms=20,bsi=1,serve=True,error=0,reply=None,programs=False):
        self.g.advance(ms)
        if bsi is not None and self.g.now-self.last_bsi>=100:
            self.frame(0x036,bytes([0,0,0,0,bsi,0,0,0]));self.last_bsi=self.g.now
        if programs and self.g.now-self.last_precond>=500:
            self.program_frames();self.last_precond=self.g.now
        self.lib.gw_vehicle_control_tick()
        frame=self.drain(error) if serve else None
        if frame and reply:reply(frame)
        return frame
    def pump(self,ms,**kwargs):
        for _ in range(ms//20):self.step(**kwargs)
    def sent(self,ident):return [f for f in self.frames if f[1]==ident]
    def receipt(self,req):return self.g.call(self.g.request('command.get',{'command_id':req['id']}))['command']
    def state(self):return self.g.call(self.g.request('state.get',{}))['state']
    def program_frames(self):
        self.frame(0x2E0,self.precond_data);self.frame(0x0E0,self.precond34)
    def precond_start(self,enabled=True,rev=0,normal=True):
        self.program_frames()
        return self.start(self.action('climate.set',{'enabled':enabled},rev),normal=normal)
    def test_precond_start_preserves_programs_and_waits_for_running_not_just_asap(self):
        req=self.precond_start();self.pump(500,programs=True)
        self.assertEqual(b'\xC0'+self.precond_data[1:],self.sent(0x31B)[0][2])
        self.assertEqual(self.precond34,self.sent(0x11B)[0][2]);self.assertEqual([],self.sent(0x394))
        self.precond_data=bytes.fromhex('84 58 01 C2 00 07 FF');self.pump(6000,programs=True)
        self.assertEqual('accepted',self.receipt(req)['phase']);self.assertEqual('off',self.state()['climate']['state'])
        self.assertEqual(1,len(self.sent(0x31B)));self.assertEqual(1,len(self.sent(0x11B)))
        self.precond_data=bytes.fromhex('8C 58 01 C2 00 07 FF');self.pump(600,programs=True)
        self.assertEqual('succeeded',self.receipt(req)['phase']);self.assertEqual('on',self.state()['climate']['state'])
    def test_precond_stop_reports_on_until_feedback_inactive(self):
        self.precond_data=bytes.fromhex('8C 58 01 C2 00 07 FF')
        req=self.precond_start(False);self.pump(500,programs=True)
        self.assertEqual(b'\x80'+self.precond_data[1:],self.sent(0x31B)[0][2])
        self.precond_data=bytes.fromhex('88 58 01 C2 00 07 FF');self.pump(600,programs=True)
        self.assertEqual('accepted',self.receipt(req)['phase']);self.assertEqual('on',self.state()['climate']['state'])
        self.precond_data=bytes.fromhex('80 58 01 C2 00 07 FF');self.pump(600,programs=True)
        self.assertEqual('succeeded',self.receipt(req)['phase']);self.assertEqual('off',self.state()['climate']['state'])
    def test_precond_accepted_asap_is_bounded_by_30s_without_restarting_cycle(self):
        req=self.precond_start();self.pump(500,programs=True)
        self.precond_data=bytes.fromhex('84 58 01 C2 00 07 FF');self.pump(28500,programs=True)
        self.assertEqual('accepted',self.receipt(req)['phase'])
        self.pump(1100,programs=True)
        self.assertEqual('unknown',self.receipt(req)['phase']);self.assertEqual('operation_timeout',self.receipt(req)['error'])
        self.assertEqual(1,len(self.sent(0x31B)));self.assertIsNone(self.drain())
    def test_precond_no_ack_uses_three_pairs_and_stops(self):
        req=self.precond_start();self.pump(7000,programs=True)
        self.assertEqual(3,len(self.sent(0x31B)));self.assertEqual(3,len(self.sent(0x11B)))
        self.assertEqual('accepted',self.receipt(req)['phase'])
        self.pump(24000,programs=True)
        self.assertEqual('unknown',self.receipt(req)['phase']);self.assertEqual([],self.sent(0x394))
    def test_precond_missing_programs_cannot_erase_existing_vehicle_schedules(self):
        self.frame(0x2E0,self.precond_data)
        req=self.start(self.action('climate.set',{'enabled':True}),normal=True);self.pump(2500)
        self.assertEqual('unknown',self.receipt(req)['phase']);self.assertEqual('preconditioning_programs_timeout',self.receipt(req)['error'])
        self.assertEqual([],self.sent(0x31B));self.assertEqual([],self.sent(0x394))
    def test_precond_arbitrary_program_bytes_are_preserved_in_both_payloads(self):
        self.precond_data=bytes.fromhex('80 FF 05 A0 80 03 E8');self.precond34=bytes.fromhex('00 A1 04 38 C0 01 C2')
        self.precond_start();self.pump(500,programs=True)
        self.assertEqual(bytes.fromhex('C0 FF 05 A0 80 03 E8'),self.sent(0x31B)[0][2])
        self.assertEqual(self.precond34,self.sent(0x11B)[0][2])
    def test_precond_forced_state_or_status_does_not_confirm_physical_action(self):
        req=self.precond_start();self.g.set('climate.state',1);self.g.set('bsi.precond.asap',1);self.g.set('bsi.precond.feedback',1)
        self.pump(31000,programs=True)
        self.assertEqual('unknown',self.receipt(req)['phase'])
    def test_precond_unavailable_or_rejected_feedback_finishes_without_success(self):
        req=self.precond_start();self.pump(500,programs=True)
        self.precond_data=bytes.fromhex('A4 58 01 C2 00 07 FF');self.pump(600,programs=True)
        self.assertEqual('failed',self.receipt(req)['phase']);self.assertEqual('preconditioning_rejected',self.receipt(req)['error'])
        self.setUp();self.precond_data=bytes.fromhex('00 58 01 C2 00 07 FF')
        req=self.precond_start();self.pump(500,programs=True)
        self.assertEqual('failed',self.receipt(req)['phase']);self.assertEqual([],self.sent(0x31B))
    def test_precond_wakes_bsi_and_duplicate_queries_never_restart_it(self):
        req=self.precond_start(normal=False);self.drain();self.pump(200,bsi=None,programs=True)
        self.g.call(req);self.pump(700,programs=True)
        self.assertGreater(len(self.sent(0x43C)),0);self.assertEqual(1,len(self.sent(0x31B)))
        self.assertEqual(1,self.g.read('request.vehicle.generation'))
        stop=self.action('climate.set',{'enabled':False},rev=1)
        self.assertEqual('busy',self.g.call(stop)['command']['error'])
    def test_precond_capture_readback_timeline_uses_actual_2e0_feedback(self):
        count=0;changes=[];previous=None
        with (ROOT/'logs/info_precond.csv').open(newline='') as stream:
            for row in csv.DictReader(stream):
                if int(row['ID'],16)!=0x2E0:continue
                data=bytes(int(row[f'D{i}'],16) for i in range(1,8));self.frame(0x2E0,data);count+=1
                q=self.state()['climate']['state'];self.assertEqual('on' if data[0] in (0x8C,0x88) else 'off',q)
                if data[0]!=previous:changes.append(data[0]);previous=data[0]
        self.assertEqual(271,count);self.assertEqual([0x80,0x84,0x8C,0x88,0x80],changes)

    def test_passive_reads_reconnect_and_command_queries_are_silent(self):
        for _ in range(15):
            for op,args in [('info.get',{}),('state.get',{}),('command.get',{'command_id':'0'*32})]:self.g.call(self.g.request(op,args))
            self.step(bsi=None)
        self.assertEqual([],self.frames);self.assertEqual(0,self.g.read('request.vehicle.generation'))
    def test_ten_wakes_100ms_apart_then_terminal_and_no_late_replay(self):
        req=self.start();self.drain();self.pump(1200,bsi=None)
        wakes=self.sent(0x43C);self.assertEqual(10,len(wakes))
        self.assertTrue(all(f[2]==b'\x00\x80' for f in wakes))
        self.assertEqual([100]*9,[b[0]-a[0] for a,b in zip(wakes,wakes[1:])])
        self.assertEqual('unknown',self.receipt(req)['phase']);self.assertEqual(2,self.g.read('control.result'))
        self.pump(1000);self.assertEqual(10,len(self.frames));self.assertIsNone(self.drain())
    def test_normal_settle_requires_a_new_rx_generation_before_command(self):
        req=self.start();self.drain();self.pump(300,bsi=3)
        self.assertEqual([],self.sent(0x394))
        now=self.g.now;self.pump(180,bsi=1)
        self.assertEqual([],self.sent(0x394));wakes=len(self.sent(0x43C))
        self.pump(140,bsi=1)
        self.assertEqual(wakes,len(self.sent(0x43C)));self.assertEqual(1,len(self.sent(0x394)))
        self.assertGreaterEqual(self.sent(0x394)[0][0]-now,200)
        self.frame(0x3E0,'41 00 00 00 00 00 00 00');self.step()
        self.assertEqual('succeeded',self.receipt(req)['phase'])
    def test_already_normal_skips_wake_but_checks_stability(self):
        self.start(normal=True);self.pump(180)
        self.assertEqual([],self.frames);self.pump(40)
        self.assertEqual([],self.sent(0x43C));self.assertEqual(1,len(self.sent(0x394)))
    def test_force_or_one_old_normal_cannot_manufacture_readiness(self):
        for forced in [False,True]:
            self.setUp();self.g.set('bsi.power_management',1,force=forced)
            self.start();self.pump(4000,bsi=None)
            self.assertEqual([],self.sent(0x394));self.assertEqual(10,len(self.sent(0x43C)))
    def test_shared_timer_saturates_and_delayed_tick_does_not_burst(self):
        self.start();self.drain();self.step(450,bsi=None)
        self.assertLessEqual(len(self.frames),2)
        self.assertGreaterEqual(self.g.read('control.timer_ms'),0)
        self.step(25000,bsi=None);self.assertEqual(7,self.g.read('control.result'))
        self.assertEqual(0,self.g.read('control.timer_ms'));self.assertIsNone(self.drain())
    def test_missing_submit_feedback_is_bounded_and_mailbox_revoked(self):
        req=self.start(normal=True);self.pump(240,serve=False)
        self.assertEqual(1,self.g.read('control.tx.pending'));self.pump(200,serve=False)
        self.assertEqual(0,self.g.read('control.tx.pending'));self.assertIsNone(self.drain())
        self.pump(3000,serve=False)
        self.assertEqual('unknown',self.receipt(req)['phase']);self.assertEqual(3,self.g.read('control.refresh_attempts'))
    def test_uncommitted_mailbox_is_not_consumed_before_pending_commit(self):
        self.start()
        self.g.set('control.tx.pending',0,force=False)
        self.assertIsNone(self.drain());self.assertEqual(0,self.consumed.value)
        self.g.set('control.tx.pending',1,force=False)
        frame=self.drain();self.assertIsNotNone(frame);self.assertEqual(0x43C,frame[1])
    def test_lost_refresh_is_retried_and_second_response_completes_once(self):
        req=self.start(normal=True)
        def answer(f):
            if f[1]==0x394 and len(self.sent(0x394))==2:self.frame(0x3E0,'41 00 00 00 00 00 00 00')
        self.pump(4500,reply=answer)
        self.assertEqual(2,len(self.sent(0x394)));self.assertEqual('succeeded',self.receipt(req)['phase'])
        self.pump(1000);self.assertEqual(2,len(self.frames))
    def test_no_response_uses_three_refresh_attempts_then_timeout(self):
        req=self.start(normal=True);self.pump(11000)
        self.assertEqual(3,len(self.sent(0x394)));self.assertEqual('unknown',self.receipt(req)['phase'])
        self.assertEqual(4,self.g.read('control.result'))
    def test_submission_error_is_retried_not_reported_as_vehicle_success(self):
        req=self.start(normal=True);self.pump(2000,error=0x103)
        self.assertEqual(3,len(self.sent(0x394)));self.assertEqual('unknown',self.receipt(req)['phase'])
        self.assertEqual('can_tx_failed',self.receipt(req)['error'])
    def test_unchanged_or_forced_soc_does_not_confirm(self):
        self.frame(0x3E0,'41 00 00 00 00 00 00 00');req=self.start(normal=True)
        self.g.set('battery.soc_pct_x10',900);self.pump(400)
        self.assertEqual('accepted',self.receipt(req)['phase'])
        self.pump(11000);self.assertEqual('unknown',self.receipt(req)['phase'])
    def schedule_run(self,kind='deferred',flags='80 B4',drop=0):
        self.frame(0x160,flags)
        args={'mode':kind}
        if kind=='deferred':args.update(start_local='03:00',timezone='Europe/Prague')
        req=self.start(self.action('charge.mode.set',args),normal=True)
        def answer(f):
            if f[1]==0x394:self.frame(0x160,flags)
            if f[1]==0x35B and len(self.sent(0x35B))>drop:self.frame(0x160,f[2])
        self.pump(5000,reply=answer)
        return req
    def test_charge_capture_payload_preamble_and_actual_new_readback(self):
        req=self.schedule_run();self.assertEqual('succeeded',self.receipt(req)['phase'])
        self.assertEqual(b'\xC0\xB4',self.sent(0x35B)[0][2])
        self.assertGreaterEqual(self.sent(0x35B)[0][0]-self.sent(0x394)[0][0],460)
        self.assertEqual(1,len(self.sent(0x35B)))
    def test_lost_mode_frame_retries_same_target_without_restarting_operation(self):
        req=self.schedule_run(drop=1)
        self.assertEqual(2,len(self.sent(0x35B)));self.assertEqual(1,len(self.sent(0x394)))
        self.assertEqual('succeeded',self.receipt(req)['phase'])
        self.assertEqual(1,self.g.read('request.vehicle.generation'))
    def test_immediate_preserves_time_reserved_flags_and_target_level(self):
        req=self.schedule_run('immediate','F8 B4')
        self.assertEqual(b'\xB8\xB4',self.sent(0x35B)[0][2]);self.assertEqual('succeeded',self.receipt(req)['phase'])
    def test_three_lost_mode_frames_end_without_optimistic_observation(self):
        req=self.schedule_run(drop=3)
        self.assertEqual(3,len(self.sent(0x35B)));self.assertEqual('unknown',self.receipt(req)['phase'])
        self.assertEqual('immediate',self.state()['charge']['mode'])
    def test_disappearing_bsi_reenters_wake_with_one_shared_ten_attempt_budget(self):
        req=self.start(normal=True);self.pump(400)
        self.assertEqual(1,len(self.sent(0x394)))
        self.pump(2500,bsi=0)
        self.assertEqual(10,len(self.sent(0x43C)));self.assertEqual('unknown',self.receipt(req)['phase'])
    def test_initial_wakes_and_later_rewake_share_the_same_budget(self):
        req=self.start();self.drain();self.pump(200,bsi=None)
        initial=len(self.sent(0x43C));self.assertGreater(initial,0)
        self.pump(400,bsi=1);self.assertEqual(1,len(self.sent(0x394)))
        self.pump(2500,bsi=0)
        self.assertEqual(10,len(self.sent(0x43C)));self.assertEqual(10,self.g.read('control.wake_attempts'))
        self.assertEqual('bsi_wake_timeout',self.receipt(req)['error'])
    def test_force_cannot_forge_physical_completion_or_rx_generations(self):
        req=self.start(normal=True);self.pump(220)
        self.g.set('control.phase',6);self.g.set('battery.soc_rx_generation',100)
        self.g.set('battery.soc_pct_x10',900)
        self.assertEqual('accepted',self.receipt(req)['phase'])
        self.frame(0x3E0,'41 00 00 00 00 00 00 00');self.step()
        self.assertEqual('succeeded',self.receipt(req)['phase'])
    def test_unrelated_160_flags_do_not_prevent_confirmation_of_mode_time(self):
        self.frame(0x160,'80 B4')
        req=self.start(self.action('charge.mode.set',{'mode':'deferred','start_local':'03:00','timezone':'Europe/Prague'}),normal=True)
        def answer(f):
            if f[1]==0x394:self.frame(0x160,'80 B4')
            if f[1]==0x35B:self.frame(0x160,'F8 B4')
        self.pump(1000,reply=answer)
        self.assertEqual('succeeded',self.receipt(req)['phase'])
    def test_duplication_and_busy_do_not_reset_attempts_or_timers(self):
        req=self.start();self.drain();self.pump(220,bsi=None)
        before=self.g.read('control.wake_attempts');self.g.call(req)
        self.assertEqual(before,self.g.read('control.wake_attempts'))
        self.assertEqual('busy',self.g.call(self.action())['command']['error'])
        self.assertEqual(1,self.g.read('request.vehicle.generation'))
    def test_replay_test_mode_suspend_and_global_timeout_revoke_pending(self):
        for key in ['can.replay_enabled','test.enabled','request.can.suspend']:
            self.setUp();req=self.start();self.g.set(key,1,False);self.step(20,bsi=None)
            self.assertEqual('failed',self.receipt(req)['phase']);self.assertIsNone(self.drain())
    def test_wake_payload_force_and_invalid_configuration(self):
        self.g.set('can.wake_tx_id',0x401);self.g.set('can.wake_tx_payload_u16',0xA55A)
        self.start();self.drain();self.assertEqual(b'\xA5\x5A',self.sent(0x401)[0][2])
        self.setUp();self.g.set('can.wake_tx_id',0x5DF);req=self.start();self.pump(200,bsi=None)
        self.assertEqual([],self.frames);self.assertEqual('failed',self.receipt(req)['phase'])
    def test_late_completion_after_timeout_cannot_change_another_attempt(self):
        req=self.start(normal=True);self.pump(220,serve=False)
        old=self.g.read('control.tx.generation');self.pump(220,serve=False)
        self.lib.gw_vehicle_control_submitted(old,0)
        self.assertNotEqual(old,self.g.read('control.tx.completed'))
        self.assertEqual('accepted',self.receipt(req)['phase'])
    def test_cached_schedule_without_new_160_cannot_enable_mode_tx(self):
        self.frame(0x160,'80 B4')
        req=self.start(self.action('charge.mode.set',{'mode':'immediate'}),normal=True)
        self.pump(11000)
        self.assertEqual([],self.sent(0x35B));self.assertEqual('unknown',self.receipt(req)['phase'])
    def test_wrong_and_invalid_reply_waits_until_matching_160(self):
        self.frame(0x160,'80 B4');req=self.start(self.action('charge.mode.set',{'mode':'deferred','start_local':'03:00','timezone':'Europe/Prague'}),normal=True)
        def answer(f):
            if f[1]==0x394:self.frame(0x160,'80 B4')
            if f[1]==0x35B:self.frame(0x160,'87 FF')
        self.pump(800,reply=answer)
        self.assertEqual('accepted',self.receipt(req)['phase'])
        self.frame(0x160,'C0 B4');self.step()
        self.assertEqual('succeeded',self.receipt(req)['phase'])
    def test_moving_cancels_schedule(self):
        self.frame(0x160,'80 B4');self.frame(0x128,'00 30 00 00 00 00 00 04')
        req=self.start(self.action('charge.mode.set',{'mode':'immediate'}),normal=True)
        self.pump(1000,reply=lambda f:self.frame(0x160,'80 B4') if f[1]==0x394 else None)
        self.assertEqual('failed',self.receipt(req)['phase']);self.assertEqual([],self.sent(0x35B))
    def test_capture_schedule_and_independent_mode_age(self):
        count=0
        with (ROOT/'logs/info_start_odlozeni.csv').open(newline='') as f:
            for row in csv.DictReader(f):
                if int(row['ID'],16)!=0x160:continue
                d=bytes(int(row[f'D{i}'],16) for i in (1,2));self.frame(0x160,d);count+=1
                self.assertEqual(180,self.g.read('charge.start_minute'))
                self.assertEqual(int(bool(d[0]&0x40)),self.g.read('charge.mode'))
        self.assertEqual(240,count)
        self.g.advance(7200000);q=self.state()['charge']
        self.assertEqual(7200000,q['mode_age_ms']);self.assertIsNone(q['age_ms'])

if __name__=='__main__':unittest.main()
