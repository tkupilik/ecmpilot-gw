"""INFO-DIV conversion and transport-layer boundary checks; no vehicle I/O."""
from pathlib import Path
import io
import sys
import unittest
from unittest.mock import patch
import zipfile

import cantools
import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import generate_info_can_dbc as g
import info_can_capture as captures


def source(name, signals, *, kind='can', length=2, **extra):
    return {'name': name, 'type': kind, 'length': length, 'signals': signals,
            'senders': ['MDS'], 'receivers': ['BSI'], 'path': 'fixture.yml', **extra}


def exported(definitions):
    database, inventory, audit, origins = g.build(definitions)
    loaded = cantools.database.load_string(database.as_dbc_string(), strict=True)
    return loaded, inventory, audit, origins


class InfoCanDatabaseTest(unittest.TestCase):
    def test_charge_hmi_reserved_overlay_matches_capture_and_preserves_source(self):
        import copy
        import json
        definition=json.loads((ROOT/'dbc/opel_corsa_e_info.source.json').read_text())['396']
        before=copy.deepcopy(definition)
        database,_,_,origins=exported({0x396:definition})
        message=database.get_message_by_frame_id(0x396)
        for first,expected in [(0x01,0),(0x03,0),(0x04,1),(0x0C,3),(0xFC,7)]:
            payload=bytes([first])+bytes.fromhex('8E A3 00 C8 09')
            decoded=message.decode(payload,decode_choices=False)
            self.assertEqual(expected,decoded['ChargingHmiState'])
            self.assertAlmostEqual(12.6,decoded['AuxBatteryVoltage'])
            self.assertEqual(81.5,decoded['AuxBatterySoc'])
            self.assertEqual(payload,message.encode(decoded,strict=False))
        self.assertEqual(before,definition)
        g.verify(database,{0x396:definition},origins)
        for key,value in [('path','another/bus/396.yml'),('length',8)]:
            other=copy.deepcopy(definition);other[key]=value
            self.assertNotIn('CHARGING_HMI_STATE',g.reviewed_signals(0x396,other))

    def test_preconditioning_status_reserved_span_matches_capture_without_altering_source(self):
        import copy
        import json
        definition=json.loads((ROOT/'dbc/opel_corsa_e_info.source.json').read_text())['2E0']
        before=copy.deepcopy(definition)
        database,_,_,origins=exported({0x2E0:definition})
        message=database.get_message_by_frame_id(0x2E0)
        for byte,asap,feedback in [(0x80,0,0),(0x84,1,0),(0x8C,1,1),(0x88,0,1)]:
            payload=bytes([byte])+bytes.fromhex('58 01 C2 00 07 FF')
            decoded=message.decode(payload,decode_choices=False)
            self.assertEqual(asap,decoded['AcPrecondAsapModeActive'])
            self.assertEqual(feedback,decoded['AcPrecondStatusFeedback'])
            # Unused slot2 keeps the captured unavailable time2047 unchanged.
            self.assertEqual(payload,message.encode(decoded,strict=False))
        self.assertEqual(before,definition)
        g.verify(database,{0x2E0:definition},origins)
        changed=copy.deepcopy(definition);changed['path']='another/profile/2E0.yml'
        other,_,_,_=exported({0x2E0:changed})
        self.assertNotIn('AcPrecondStatusFeedback',[s.name for s in other.messages[0].signals])

    def test_vehicle_range_scale_matches_120km_reference_without_mutating_upstream(self):
        import copy
        import json
        source_definition=json.loads((ROOT/'dbc/opel_corsa_e_info.source.json').read_text(encoding='utf-8'))['31A']
        before=copy.deepcopy(source_definition)
        database,_,audit,origins=exported({0x31A:source_definition})
        message=database.get_message_by_frame_id(0x31A)
        payload=bytes.fromhex('1E 00 00 00 3F E0 05 A0')
        self.assertEqual(60,message.decode(payload,scaling=False,decode_choices=False)['ZevRangeDisplay'])
        self.assertEqual(120,message.decode(payload,decode_choices=False)['ZevRangeDisplay'])
        self.assertIn('VEHICLE_SCALE_OBSERVED',message.get_signal_by_name('ZevRangeDisplay').comment)
        self.assertTrue(any(r['status']=='vehicle_scale_override' for r in audit))
        self.assertEqual(before,source_definition)
        g.verify(database,{0x31A:source_definition},origins)
        changed=copy.deepcopy(source_definition);changed['path']='another/profile/31A.yml'
        other,_,_,_=exported({0x31A:changed})
        self.assertEqual(60,other.get_message_by_frame_id(0x31A).decode(payload,decode_choices=False)['ZevRangeDisplay'])
        changed=copy.deepcopy(source_definition);changed['signals']['ZEV_RANGE_DISPLAY']['factor']=3
        other,_,_,_=exported({0x31A:changed})
        self.assertEqual(180,other.get_message_by_frame_id(0x31A).decode(payload,decode_choices=False)['ZevRangeDisplay'])
        # Also check the actual serialized project DBC, not just the generator.
        actual=cantools.database.load_file(ROOT/'dbc/opel_corsa_e_info.dbc',encoding='utf-8',strict=True)
        self.assertEqual(120,actual.get_message_by_frame_id(0x31A).decode(payload,decode_choices=False)['ZevRangeDisplay'])

    def test_unknown_vehicle_layouts_export_only_raw_and_preserve_source(self):
        import copy
        import json
        definitions = json.loads((ROOT / 'dbc/opel_corsa_e_info.source.json').read_text(encoding='utf-8'))
        selected = {int(k,16): definitions[k] for k in ['161','3F0','3E1','276','2AD','350']}
        database, inventory, audit, origins = exported(selected)
        for ident, size in [(0x161,7),(0x3F0,8),(0x3E1,7)]:
            message = database.get_message_by_frame_id(ident)
            self.assertIn('Unknown',message.name)
            self.assertIn('VEHICLE_LAYOUT_UNKNOWN',message.comment)
            self.assertEqual([f'RawByte{i+1}' for i in range(size)], [s.name for s in message.signals])
            data=bytes(range(size))
            self.assertEqual(data,message.encode(message.decode(data,decode_choices=False)))
            self.assertTrue(any(r['id_hex']==f'{ident:03X}' and r['status']=='source_preserved_unknown_vehicle_layout' for r in audit))
        self.assertEqual('BSI_VsmSlowData',database.get_message_by_frame_id(0x276).name)
        self.assertEqual('ICSV_FrontClimateStatus',database.get_message_by_frame_id(0x350).name)
        self.assertIn('FrontLeftTempSetpointValue',[s.name for s in database.get_message_by_frame_id(0x350).signals])
        altered=copy.deepcopy(selected);altered[0x3F0]['path']='another/bus/3F0.yml'
        self.assertIsNone(g.unknown_layout(0x3F0,altered[0x3F0]))
        g.verify(database,selected,origins)

    def test_obc_event_labels_full_bsi_cause_byte_without_inventing_a_wake_bit(self):
        import copy
        import json
        definitions = json.loads((ROOT / 'dbc/opel_corsa_e_info.source.json').read_text(encoding='utf-8'))
        source_definition = definitions['412']
        before = copy.deepcopy(source_definition)
        database, _, _, _ = exported({0x412: source_definition})
        message = database.get_message_by_frame_id(0x412)
        self.assertEqual('BSI_WakeupCause', message.name)
        payload = bytes.fromhex('00 20 00 00 00 00 00 00')
        self.assertEqual(32, message.decode(payload, decode_choices=False)['BsiWakeupInfo0'])
        self.assertIn('OBC cause', str(message.decode(payload)['BsiWakeupInfo0']))
        self.assertEqual(48, message.decode(bytes.fromhex('00 30 00 00 00 00 00 00'))['BsiWakeupInfo0'])
        self.assertEqual(payload, message.encode(message.decode(payload, decode_choices=False)))
        self.assertEqual(8, message.get_signal_by_name('BsiWakeupInfo0').length)
        self.assertIn('OWNER_EVENT_OBSERVED', message.get_signal_by_name('BsiWakeupInfo0').comment)
        self.assertEqual(before, source_definition)
        changed = copy.deepcopy(source_definition)
        changed['path'] = 'another/bus/412.yml'
        changed_database, _, _, _ = exported({0x412: changed})
        self.assertIsNone(changed_database.get_message_by_frame_id(0x412).get_signal_by_name('BsiWakeupInfo0').choices)

    def test_cluster_source_overlay_is_scoped_and_keeps_original_sender_provenance(self):
        import copy
        import json
        definitions = json.loads((ROOT / 'dbc/opel_corsa_e_info.source.json').read_text(encoding='utf-8'))
        reviewed = {0x1D7, 0x217, 0x257, 0x2D7, 0x2E7, 0x317, 0x31C, 0x51F, 0x5DF}
        selected = {int(k,16):definitions[k] for k in ['1D7','217','257','2D7','2E7','317','31C','51F','5DF','412','4A4','5C1','5D2','5E4','5FB','5FC','01C']}
        before = copy.deepcopy(selected)
        database, inventory, _, _ = exported(selected)
        for message in database.messages:
            if message.frame_id in reviewed:
                self.assertTrue(message.name.startswith('IC_'))
                self.assertEqual(['IC'], message.senders)
                self.assertIn('VEHICLE_SOURCE_REVIEW', message.comment)
                self.assertIn('CMB', message.comment)
            else:
                self.assertFalse(message.name.startswith('IC_'))
        self.assertEqual('IC_Version', database.get_message_by_frame_id(0x5DF).name)
        row = next(row for row in inventory if row['id_hex']=='5DF')
        self.assertEqual('IC', row['dbc_sender'])
        self.assertEqual('CMB', row['source_senders'])
        self.assertEqual(before, selected)
        altered=copy.deepcopy(selected)
        altered[0x5DF]['path']='another/bus/5DF.yml'
        changed,_,_,_=exported(altered)
        self.assertFalse(changed.get_message_by_frame_id(0x5DF).name.startswith('IC_'))

    def test_owner_confirmation_is_per_field_and_exact_reviewed_layout(self):
        import copy
        import json
        definitions = json.loads((ROOT / 'dbc/opel_corsa_e_info.source.json').read_text(encoding='utf-8'))
        selected = {int(key, 16): definitions[key] for key in ['0B6', '0F6', '128', '3E7', '3E0']}
        database, inventory, _, _ = exported(selected)
        expected = {0x0B6: {'Speed'}, 0x0F6: {'TotalMileage', 'ExternalFilteredTemperature'},
                    0x128: {'GearPositionToDisplay', 'ParkingBrakes'}}
        for ident, names in expected.items():
            message = database.get_message_by_frame_id(ident)
            actual = {s.name for s in message.signals if s.comment.startswith('OWNER_CONFIRMED:')}
            self.assertEqual(actual, names)
            self.assertIn('OWNER_CONFIRMED_FIELDS', message.comment)
        maintenance = database.get_message_by_frame_id(0x3E7)
        self.assertEqual(sum(s.comment.startswith('OWNER_CONFIRMED:') for s in maintenance.signals), 8)
        self.assertIn('DATABASE_CANDIDATE', database.get_message_by_frame_id(0x3E0).get_signal_by_name('BatterySocCandidate').comment)
        self.assertTrue(all(not row['vehicle_validated'] for row in inventory))
        # A different bus/profile or changed layout cannot inherit the confirmation.
        altered = copy.deepcopy(selected)
        altered[0x0B6]['signals']['SPEED']['factor'] = .02
        altered[0x128]['path'] = 'different/profile/128.yml'
        changed, _, _, _ = exported(altered)
        for ident in [0x0B6, 0x128]:
            self.assertFalse(any(s.comment.startswith('OWNER_CONFIRMED:') for s in changed.get_message_by_frame_id(ident).signals))

    def test_info_id_never_acquires_hs1_alias_or_confidence(self):
        definitions = {0x305: source('DASHCAM_LIST', {
            'CAMERA_VALUE': {'bits': '1.7-1.0', 'factor': .5, 'offset': -40, 'invalid': 255},
        })}
        database, _, _, origins = exported(definitions)
        message = database.messages[0]
        self.assertEqual(message.name, 'MDS_DashcamList')
        self.assertNotIn('Steering', message.name)
        signal = message.get_signal_by_name('CameraValue')
        self.assertIn('DATABASE_CANDIDATE', signal.comment)
        self.assertNotIn('OWNER_CONFIRMED', signal.comment)
        self.assertEqual(message.decode(b'\xff\x00', decode_choices=False)['CameraValue'], 87.5)
        self.assertIn('invalid=255', signal.comment)
        self.assertEqual(database.dbc.attributes['Baudrate'].value, 125000)
        g.verify(database, definitions, origins)

    def test_signed_and_opaque_fields_survive_with_exact_bit_coverage(self):
        definitions = {0x101: source('DATA', {
            'SIGNED_VALUE': {'bits': '1.7-1.0', 'type': 'sint', 'factor': .5},
            'TEXT_BYTE': {'bits': '2.7-2.0', 'type': 'str'},
        })}
        database, _, audit, origins = exported(definitions)
        decoded = database.decode_message(0x101, b'\xfeA', decode_choices=False)
        self.assertEqual(decoded, {'SignedValue': -1, 'TextByte': 65})
        self.assertEqual(sum(r['status'] == 'opaque_source' for r in audit), 1)
        g.verify(database, definitions, origins)

    def test_application_did_is_not_decoded_at_physical_wire_offset(self):
        definitions = {0x77C: source('DIAG_REQUEST', {
            'APPLICATION_DID': {'bits': '2.7-3.0', 'type': 'uint'},
            'LONG_TEXT': {'bits': '4.7-100.0', 'type': 'str'},
        }, kind='can-tp', length=None)}
        database, inventory, audit, origins = exported(definitions)
        message = database.messages[0]
        decoded = message.decode(bytes.fromhex('03 22 D4 10 00 00 00 00'), decode_choices=False)
        self.assertEqual(decoded['IsoTpFrameType'], 0)
        self.assertEqual(decoded['SingleFrameLength'], 3)
        self.assertEqual([decoded[f'SingleData{i}'] for i in range(1, 4)], [0x22, 0xD4, 0x10])
        self.assertNotIn('ApplicationDid', decoded)
        self.assertNotIn('LongText', decoded)
        self.assertEqual(inventory[0]['dbc_mode'], 'isotp_wire')
        self.assertEqual(sum(r['status'] == 'source_preserved_separate_layer' for r in audit), 2)
        g.verify(database, definitions, origins)

    def test_isotp_headers_unknown_types_and_unpadded_short_frames(self):
        definitions = {0x67C: source('DIAG_RESPONSE', {}, kind='can-tp', length=None)}
        database, _, _, origins = exported(definitions)
        message = database.messages[0]
        ff = message.decode(bytes.fromhex('12 34 AA BB CC DD EE FF'), decode_choices=False)
        self.assertEqual(ff['FirstFrameLength'], 0x234)
        self.assertEqual(ff['FirstData1'], 0xAA)
        cf = message.decode(bytes.fromhex('2F 55'), decode_choices=False, allow_truncated=True)
        self.assertEqual(cf, {'IsoTpFrameType': 2, 'ConsecutiveSequence': 15, 'ConsecutiveData1': 0x55})
        fc = message.decode(bytes.fromhex('30 08 F1'), decode_choices=False, allow_truncated=True)
        self.assertEqual(fc['BlockSize'], 8)
        self.assertEqual(fc['SeparationTimeRaw'], 0xF1)
        unknown = message.decode(bytes.fromhex('AF 55'), decode_choices=False, allow_truncated=True)
        self.assertEqual(unknown, {'IsoTpFrameType': 10, 'UnknownPciLow': 15, 'UnknownData1': 0x55})
        result = g.verify(database, definitions, origins)
        self.assertEqual(result['short_transport_frames_checked'], 16 * 7)

    def test_extended_identifier_roundtrips_as_extended(self):
        definitions = {0x10000000: source('EXTENDED_DATA', {}, extended=True)}
        database, _, _, origins = exported(definitions)
        self.assertTrue(database.messages[0].is_extended_frame)
        self.assertEqual(database.messages[0].frame_id, 0x10000000)
        g.verify(database, definitions, origins)

    def test_source_resolution_preserves_variants_and_excludes_other_buses(self):
        root = f'PSA-RE-{g.COMMIT}/'
        architecture = {'AEE2010': {'full': {}, 'ev': {'parent': 'full'}, 'electric': {'parent': 'ev'}}}
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, 'w') as archive:
            archive.writestr(root, '')
            archive.writestr(root + 'architectures.yml', yaml.safe_dump(architecture))
            archive.writestr(root + 'LICENCE', b'fixture license')
            for bus, profile, filename, name in [
                ('LS.INFO-DIV', 'full', '394.yml', 'PARENT'),
                ('LS.INFO-DIV', 'ev', '394.yml', 'EVOLUTION'),
                ('LS.INFO-DIV', 'full', '741_A.yml', 'ALTERNATE_A'),
                ('LS.INFO-DIV', 'full', '741_B.yml', 'ALTERNATE_B'),
                ('HS.HS1', 'electric', '394.yml', 'WRONG_BUS'),
            ]:
                definition = source(name, {}, id=int(filename.split('_')[0].split('.')[0], 16))
                archive.writestr(root + f'buses/AEE2010.{profile}/{bus}/{filename}', yaml.safe_dump(definition))
        definitions, license_text, chain = g.load_source(buffer)
        self.assertEqual(definitions[0x394]['name'], 'EVOLUTION')
        self.assertEqual(definitions[0x394]['source_file_alternatives'][0]['name'], 'PARENT')
        self.assertEqual(definitions[0x741]['name'], 'ALTERNATE_A')
        self.assertEqual(definitions[0x741]['source_file_alternatives'][0]['name'], 'ALTERNATE_B')
        self.assertEqual(chain, ['AEE2010.electric', 'AEE2010.ev', 'AEE2010.full'])
        self.assertEqual(license_text, b'fixture license')

    def test_actual_export_contains_critical_telematics_fields(self):
        database = cantools.database.load_file(ROOT / 'dbc/opel_corsa_e_info.dbc', encoding='utf-8', strict=True)
        self.assertEqual(len(database.messages), 377)
        self.assertEqual(database.decode_message(0x3E0, b'\x46' + bytes(7), decode_choices=False)['BatterySocCandidate'], 70)
        charge = database.decode_message(0x394, b'\x10', decode_choices=False)
        self.assertEqual(charge['RemoteSendChargeDataRequest'], 1)
        hold = database.decode_message(0x3A9, b'\x04\x00', decode_choices=False)
        self.assertEqual(hold['NetworkMaintainRequestMdc'], 1)
        wake = database.decode_message(0x43C, b'\x00\x80', decode_choices=False)
        self.assertEqual(wake['WakeupCause1'], 1)
        self.assertTrue(database.get_message_by_frame_id(0x67C).name.startswith('MDS_'))
        self.assertEqual(database.get_message_by_frame_id(0x77C).name, 'BSI_DiagRequestToMDS')

    def test_capture_lengths_preserve_fitting_fields_and_cover_unknown_identifier(self):
        definitions = {0xA8: source('STATUS', {
            'PREFIX': {'bits': '1.7-1.0'}, 'ABSENT_SUFFIX': {'bits': '6.7-6.0'}}, length=6)}
        frame = lambda ident, data: {'index': 0, 'line': 2, 'raw_us': -100, 'segment': 0, 'id': ident,
                                    'extended': False, 'bus': 0, 'direction': 'Rx', 'data': data}
        capture = {'name': 'short.csv', 'frames': [frame(0xA8, b'\x42\x00\x00\x00'), frame(0x7A4, bytes(range(8)))],
                   'timestamp_regressions': [], 'duration_s': 0}
        adapted = g.adapt_to_captures(definitions, [capture])
        database, _, audit, origins = exported(adapted)
        self.assertEqual(definitions[0xA8]['length'], 6)
        self.assertEqual(database.get_message_by_frame_id(0xA8).length, 4)
        self.assertEqual(database.decode_message(0xA8, capture['frames'][0]['data'])['Prefix'], 0x42)
        self.assertNotIn('AbsentSuffix', [s.name for s in database.get_message_by_frame_id(0xA8).signals])
        self.assertEqual(sum(r['status'] == 'excluded_out_of_captured_payload' for r in audit), 1)
        self.assertTrue(database.get_message_by_frame_id(0x7A4).name.startswith('Unknown_'))
        report, _ = captures.validate(database, [capture], origins)
        self.assertEqual(report['totals']['full_payload_coverage_frames'], 2)

    def test_timestamp_reset_preserves_order_and_never_fabricates_a_period(self):
        text = ('Time Stamp,ID,Extended,Dir,Bus,LEN,D1\n'
                '1000,000003E0,false,Rx,0,1,46\n'
                '5,000003E0,false,Rx,0,1,45\n'
                '105,000003E0,false,Rx,0,1,44\n')
        with patch.object(Path, 'open', return_value=io.StringIO(text)), patch.object(Path, 'read_bytes', return_value=text.encode()):
            capture = captures.read_capture(Path('fixture.csv'))
        self.assertEqual([f['data'][0] for f in capture['frames']], [70, 69, 68])
        self.assertEqual(len(capture['timestamp_regressions']), 1)
        self.assertIsNone(capture['duration_s'])
        self.assertEqual(captures.summarize([capture])[0x3E0]['median_period_ms'], .1)

    def test_same_filename_captures_have_independent_period_clocks(self):
        def capture(interval):
            return {'name': 'same.csv', 'frames': [
                {'id': 0x3E0, 'bus': 0, 'extended': False, 'segment': 0, 'data': b'\x46', 'raw_us': time}
                for time in [0, interval]]}
        summary = captures.summarize([capture(100000), capture(200000)])[0x3E0]
        self.assertEqual(summary['count'], 4)
        self.assertEqual(summary['median_period_ms'], 150)


if __name__ == '__main__':
    unittest.main()
