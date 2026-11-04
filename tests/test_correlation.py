"""Real UDP codec, varstore and controller: correlation never duplicates CAN work."""
import unittest
import test_vehicle_control as vc
from test_udp import Codec, CLIENT

SESSION='1234567890abcdef87654321fedcba98'

class CorrelationTests(unittest.TestCase):
    def setUp(self):
        self.h=vc.VehicleControlTests('runTest');self.h.setUp();self.g=self.h.g
    def tagged(self,request,ident=1042,session=SESSION):
        return dict(request,correlation_id=ident,client_session=session)
    def test_echo_all_passive_reads_without_a_vehicle_event(self):
        for op in ('info.get','state.get','vehicle.details.get','climate.programs.get','auto_refresh.get'):
            request=self.tagged(self.g.request(op,{}),0xffffffff)
            reply=self.g.call(request)
            self.assertEqual(request['id'],reply['reply_to']);self.assertEqual(0xffffffff,reply['correlation_id'])
        self.assertEqual(0,self.g.read('request.vehicle.generation'))
    def test_invalid_metadata_never_creates_an_action(self):
        for value in (0,-1,0x100000000,1.5,True,'42',None):
            request=self.tagged(self.h.action(),value)
            self.assertEqual('invalid_argument',self.g.call(request)['command']['error'])
        for change in ({'client_session':None},{'client_session':'G'*32},{'client_session':'a'*31}):
            request=self.tagged(self.h.action());request.update(change)
            self.assertEqual('invalid_argument',self.g.call(request)['command']['error'])
        request=self.tagged(self.h.action());del request['client_session']
        self.assertEqual('invalid_argument',self.g.call(request)['command']['error'])
        self.assertEqual(0,self.g.read('request.vehicle.generation'))
    def test_same_operation_new_message_never_runs_can_twice_and_queries_keep_context(self):
        request=self.tagged(self.h.action(),0xffffffff)
        self.h.start(request,normal=True);self.h.pump(500)
        self.assertEqual(0xffffffff,self.g.read('control.correlation_id')&0xffffffff)
        for i in range(4): self.assertEqual(int(SESSION[i*8:i*8+8],16),self.g.read('control.session'+str(i))&0xffffffff)
        duplicate=dict(request,id='f'*32)
        result=self.g.call(duplicate)
        self.assertEqual(duplicate['id'],result['reply_to']);self.assertEqual(request['id'],result['command']['id'])
        self.assertEqual(1,self.g.read('request.vehicle.generation'))
        self.assertEqual(1,len(self.h.sent(0x394)))
        self.h.frame(0x3E0,'41 00 00 00 00 00 00 00');self.h.step()
        query=self.tagged(self.g.request('command.get',{'command_id':request['id']}),0xffffffff)
        receipt=self.g.call(query)['command']
        self.assertEqual(('succeeded',SESSION,0xffffffff),(receipt['phase'],receipt['client_session'],receipt['correlation_id']))
        query_by_correlation=self.tagged(self.g.request('command.get',{}),0xffffffff)
        self.assertEqual(receipt,self.g.call(query_by_correlation)['command'])
        self.assertEqual(receipt,self.g.call(duplicate)['command']);self.h.pump(500)
        self.assertEqual(1,len(self.h.sent(0x394)))
        wrong=dict(query,correlation_id=1)
        self.assertEqual('id_conflict',self.g.call(wrong)['error'])
    def test_changed_intent_with_same_correlation_and_changed_correlation_with_same_message_are_rejected(self):
        request=self.tagged(self.h.action());self.h.start(request)
        changed=self.tagged(self.h.action('climate.set',{'enabled':True}))
        self.assertEqual('id_conflict',self.g.call(changed)['command']['error'])
        self.assertEqual('id_conflict',self.g.call(dict(request,correlation_id=1043))['command']['error'])
        self.assertEqual(1,self.g.read('request.vehicle.generation'))
    def test_restart_scope_allows_reusing_counter_for_a_new_operation(self):
        first=self.tagged(self.h.action());self.h.start(first,normal=True);self.h.pump(500)
        self.h.frame(0x3E0,'41 00 00 00 00 00 00 00');self.h.step()
        self.assertEqual('succeeded',self.g.call(first)['command']['phase'])
        second=self.tagged(self.h.action(),session='a'*32)
        self.assertEqual('accepted',self.g.call(second)['command']['phase'])
        self.assertEqual(2,self.g.read('request.vehicle.generation'))
    def test_payload_limits_with_largest_counter_and_long_endpoint_names(self):
        self.g.lib.gw_udp_protocol_init(b'g'*80,b'0'*32,b'f'*80)
        for op in ('info.get','state.get','vehicle.details.get','climate.programs.get','auto_refresh.get'):
            request=self.tagged(self.g.request(op,{}),0xffffffff);request['gw']='g'*80
            reply=self.g.call(request);self.assertIsNotNone(reply,op);self.assertEqual(0xffffffff,reply['correlation_id'])

    def test_busy_refusal_is_cached_so_delayed_retry_cannot_start_after_another_action_finishes(self):
        first=self.tagged(self.h.action(),1);self.h.start(first,normal=True);self.h.pump(500)
        refused=self.tagged(self.h.action(),2)
        result=self.g.call(refused)['command'];self.assertEqual('busy',result['error'])
        self.h.frame(0x3E0,'41 00 00 00 00 00 00 00');self.h.step()
        self.assertEqual('succeeded',self.g.call(first)['command']['phase'])
        duplicate=dict(refused,id='f'*32)
        self.assertEqual(result,self.g.call(duplicate)['command'])
        self.h.pump(500);self.assertEqual(1,len(self.h.sent(0x394)))

    def test_expired_rejection_keeps_the_callers_correlation(self):
        request=self.tagged(self.h.action());request['deadline_ms']=self.g.now
        reply=self.g.call(request)['command']
        self.assertEqual(('rejected','expired',SESSION,1042),(reply['phase'],reply['error'],reply['client_session'],reply['correlation_id']))

    def test_full_receipt_cache_does_not_turn_a_refused_packet_into_a_late_action(self):
        first=self.tagged(self.h.action(),1);self.h.start(first)
        for i in range(2,257):
            request=self.tagged(self.h.action(),i)
            self.assertEqual('busy',self.g.call(request)['command']['error'])
        self.g.advance(599999)
        refused=self.tagged(self.h.action(),257)
        self.assertEqual('busy',self.g.call(refused)['command']['error'])
        self.g.advance(2)  # Earlier refusals now leave free slots in the cache.
        self.assertEqual('busy',self.g.call(dict(refused,id='f'*32))['command']['error'])
        self.assertEqual(1,self.g.read('request.vehicle.generation'))

    def test_gateway_reboot_cannot_replay_a_previous_boots_operation(self):
        request=self.tagged(self.h.action());self.h.start(request)
        self.g.lib.gw_udp_protocol_init(b'ecmpilot-test',b'f'*32,b'0.5.0')
        self.g.lib.gw_vehicle_control_init()
        result=self.g.call(request)['command']
        self.assertEqual('boot_changed',result['error'])
        self.assertEqual(1042,result['correlation_id'])
        self.assertEqual(0,self.g.read('request.vehicle.generation'))

    def test_populated_stale_snapshot_with_maximum_context_fits_without_losing_soc(self):
        self.g.lib.gw_udp_protocol_init(b'g'*80,b'0'*32,b'f'*80)
        self.g.bench()
        for name,value in {'battery.soc_pct_x10':1000,'vehicle.range_km_x10':20000,'temperature.cabin_mc':-60000,
            'temperature.outside_mc':100000,'battery.min_mc':-60000,'battery.max_mc':150000,'vehicle.odometer_km':10000000,
            'battery.voltage_mv':1000000,'battery.current_ma':-2000000,'charge.power_w':1000000,'charge.remaining_min':100000,
            'bsi.aux_battery_soc_pct_x10':1000,'bsi.aux_battery_voltage_mv':32000,'bsi.power_management':1}.items():
            self.g.set(name,value,force=False)
        self.g.advance(2**40)
        request=self.tagged(self.g.request('state.get',{}),0xffffffff);request['gw']='g'*80
        reply=self.g.call(request)
        self.assertIsNotNone(reply)
        self.assertEqual(100,reply['state']['soc_pct']['value'])
        self.assertEqual(100,reply['state']['aux_battery_soc_pct']['value'])

if __name__=='__main__':unittest.main()
