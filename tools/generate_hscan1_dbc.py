#!/usr/bin/env python3
"""Build a database-first HS1 DBC for every observed ID from pinned PSA-RE.

Requires Python 3.10+, PyYAML and cantools. This script only reads local files;
it does not open a CAN interface. Generator: project BSD-2-Clause license.
Derived database definitions: PSA-RE, Apache-2.0; see dbc/NOTICE-PSA-RE.txt.
"""
import argparse
import collections
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import zipfile

import cantools
from cantools.database.can import Message, Node, Signal
from cantools.database.conversion import BaseConversion
import yaml

COMMIT = '67f7581e7622410efdbb29c7eda3b86982b9b450'
SOURCE = f'https://github.com/prototux/PSA-RE/blob/{COMMIT}/'

# Preserve aliases and confidence for previously reviewed fields. This is NOT
# an import whitelist: every in-bounds canonical source field is now retained.
REVIEWED_ALIASES = {
    0x262: {'HYBRID_POWERTRAIN_OPERATING_STATE': 'PowertrainState'},
    0x305: {'STEERING_WHEEL_ANGLE': 'SteeringAngle',
            'STEERING_WHEEL_ROTATION_SPEED': 'SteeringRotationSpeed'},
    0x30D: {'FRONT_LEFT_WHEEL_SPEED_UNFILTERED': 'WheelSpeedFL',
            'FRONT_RIGHT_WHEEL_SPEED_UNFILTERED': 'WheelSpeedFR',
            'REAR_LEFT_WHEEL_SPEED_UNFILTERED': 'WheelSpeedRL',
            'REAR_RIGHT_WHEEL_SPEED_UNFILTERED': 'WheelSpeedRR'},
    0x349: {'GEAR_LEVER_POSITION': 'GearPosition'},
    0x3CD: {'MASTER_CYLINDER_PRESSURE': 'MasterCylinderPressure',
            'BRAKING_IN_PROGRESS': 'BrakingInProgress'},
    0x3CE: {'EVAPORATOR_UPSTREAM_AIR_TEMPERATURE': 'EvaporatorUpstreamAirTemp',
            'HEATER_CORE_OUTLET_WATER_TEMPERATURE': 'HeaterOutletWaterTemp',
            'HEATER_CORE_TEMPERATURE_SETPOINT': 'HeaterWaterTempSetpoint',
            'EVAPORATOR_TEMPERATURE_SETPOINT': 'EvaporatorTempSetpoint',
            'EVAPORATOR_PROBE_TEMPERATURE': 'EvaporatorProbeTemp',
            'HVAC_BLOWER_AIRFLOW': 'HvacAirflow'},
    0x44D: {'FRONT_WHEELS_AVERAGE_SPEED': 'FrontAverageSpeed',
            'REAR_LEFT_WHEEL_SPEED': 'WheelSpeedRL',
            'REAR_RIGHT_WHEEL_SPEED': 'WheelSpeedRR',
            'FRONT_WHEELS_AVERAGE_RPM': 'FrontAverageWheelRpm'},
    0x454: {'HV_BATTERY_SOH_CAPACITY': 'BatterySohCapacity',
            'HV_BATTERY_SOH_RESISTANCE': 'BatterySohResistance'},
    0x489: {'GEAR_LEVER_POSITION_DISPLAY': 'GearPositionDisplay'},
    0x4E9: {'GEARBOX_PARK_STATE': 'ParkingLockState'},
    0x4FE: {'MONOSTABLE_LEVER_POSITION': 'LeverPosition',
            'GEAR_LEVER_MOVEMENT': 'LeverMovement',
            'PROCESS_COUNTER_4B_4FE': 'RollingCounter',
            'CHECKSUM_FRAME_4FE': 'Checksum'},
    0x50D: {'BATTERY_VOLTAGE_UC_BRAKE': 'BrakeEcuSupplyVoltage'},
    0x552: {'ODOMETER': 'Odometer'},
    0x572: {'REVERSE_STATUS': 'ReverseStatus'},
    0x5A3: {'HV_BATTERY_INSULATION_RESISTANCE': 'BatteryInsulationResistance',
            'HV_BATTERY_CELL_NUMBER_VOLT_MAX': 'MaxVoltageCellIndexCandidate',
            'HV_BATTERY_CELL_VOLTAGE_MAX': 'MaxCellVoltage',
            'HV_BATTERY_CELL_NUMBER_VOLT_MIN': 'MinVoltageCellIndexCandidate',
            'HV_BATTERY_CELL_VOLTAGE_MIN': 'MinCellVoltage'},
    0x5C1: {'PEM_INVERTER_TEMPERATURE': 'TractionInverterTemperature',
            'PEM_STATOR_TEMPERATURE': 'TractionStatorTemperature'},
    0x5C3: {'HV_BATTERY_ACTUAL_VOLTAGE': 'BatteryVoltage',
            'HV_BATTERY_SOC_2': 'BatterySoc'},
}
NAMES = {0x334: 'BatteryCellVoltages', 0x3F4: 'BatteryTemperatureSlots',
         0x5C1: 'TractionTemperatures', 0x552: 'Odometer'}


def english(value):
    if isinstance(value, dict):
        if 'en' in value:
            return value['en']
        return {str(k): english(v) for k, v in value.items()}
    if isinstance(value, list):
        return [english(v) for v in value]
    return value


def positions(bits):
    points = bits.split('-')
    start_byte, start_bit = map(int, points[0].split('.'))
    end_byte, end_bit = map(int, points[-1].split('.'))
    first = (start_byte - 1) * 8 + 7 - start_bit
    last = (end_byte - 1) * 8 + 7 - end_bit
    assert 0 <= first <= last < 64, bits
    return first, last, (start_byte - 1) * 8 + start_bit


def extract(payload, bits):
    first, last, _ = positions(bits)
    assert last < len(payload) * 8
    width = last - first + 1
    return (int.from_bytes(payload, 'big') >> (len(payload) * 8 - 1 - last)) & ((1 << width) - 1)


def make_signal(name, spec, confidence, mux_name=None, mux_ids=None, selector=False):
    first, last, start = positions(spec['bits'])
    width = last - first + 1
    scale, offset = spec.get('factor', 1), spec.get('offset', 0)
    signed = spec.get('type') == 'sint'
    minimum = spec.get('min', (-(1 << (width - 1)) if signed else 0) * scale + offset)
    maximum = spec.get('max', ((1 << (width - (1 if signed else 0))) - 1) * scale + offset)
    # cantools' VAL_ writer does not escape quote characters inside source labels.
    # Cosmetic normalization keeps every enum value while producing parseable DBC.
    choices = {int(k): v.replace('"', "'").replace('\\', '/')
               for k, v in spec.get('values', {}).items() if isinstance(v, str)} or None
    invalid = spec.get('invalid')
    if isinstance(invalid, int):
        choices = dict(choices or {})
        choices[invalid] = 'Not_available'
    comment = f"{confidence}. PSA-RE bits {spec['bits']}. {spec.get('comment', '')}"
    if invalid is not None:
        comment += f' Raw invalid={invalid}; numeric DBC decoders may still scale it: check raw before use.'
    if 'default' in spec:
        comment += f" Upstream default raw={spec['default']} (not necessarily valid data)."
    return Signal(name, start, width, byte_order='big_endian', is_signed=signed,
                  raw_invalid=invalid, conversion=BaseConversion.factory(scale, offset, choices),
                  minimum=minimum, maximum=maximum, unit=spec.get('units'), comment=comment,
                  is_multiplexer=selector, multiplexer_signal=mux_name, multiplexer_ids=mux_ids)


def capture(path):
    frames = []
    with path.open(newline='', encoding='utf-8-sig') as stream:
        for row in csv.DictReader(stream):
            length = int(row['LEN'])
            assert row['Extended'].lower() == 'false' and row['Dir'] == 'Rx' and row['Bus'] == '0'
            assert 0 <= length <= 8
            frames.append((int(row['Time Stamp']), int(row['ID'], 16),
                           bytes(int(row[f'D{i}'], 16) for i in range(1, length + 1))))
    first = frames[0][0]
    assert all(b[0] >= a[0] for a, b in zip(frames, frames[1:]))
    return [((ts - first) / 1e6, ident, data) for ts, ident, data in frames]


def load_source(path):
    archive = zipfile.ZipFile(path)
    root = archive.namelist()[0]
    assert root == f'PSA-RE-{COMMIT}/', 'Use the documented pinned source revision.'
    loader = getattr(yaml, 'CSafeLoader', yaml.SafeLoader)
    arch = yaml.load(archive.read(root + 'architectures.yml'), Loader=loader)
    variant, chain = 'electric', []
    while variant:
        chain.append('AEE2010.' + variant)
        variant = arch['AEE2010'][variant].get('parent')
    definitions = {}
    for profile in reversed(chain):
        prefix = root + 'buses/' + profile + '/HS.HS1/'
        for name in archive.namelist():
            if not name.startswith(prefix) or not name.endswith('.yml'):
                continue
            spec = english(yaml.load(archive.read(name), Loader=loader))
            if not isinstance(spec, dict) or not isinstance(spec.get('id'), int):
                continue
            spec['path'] = name[len(root):]
            definitions[spec['id']] = spec
    return definitions, archive.read(root + 'LICENCE')


def short_name(value, used):
    """Portable DBC identifier; a source-name hash avoids truncation collisions."""
    result = re.sub(r'[^A-Za-z0-9_]', '_', value)
    if not result or result[0].isdigit():
        result = 'Field_' + result
    if len(result) > 32:
        result = result[:25] + '_' + hashlib.sha256(value.encode()).hexdigest()[:6]
    base, index = result, 2
    while result in used:
        suffix = f'_{index}'
        result = base[:32-len(suffix)] + suffix
        index += 1
    used.add(result)
    return result


def camel(value):
    if '_' not in value and re.search(r'[a-z][A-Z]', value):
        return value[0].upper() + value[1:]
    return ''.join(word.capitalize() for word in re.findall(r'[A-Za-z0-9]+', value))


def signal_name(ident, source_name, used):
    alias = REVIEWED_ALIASES.get(ident, {}).get(source_name)
    if ident == 0x334:
        if source_name == 'BATTERY_CELL_VOLTAGE_GROUP_ID':
            alias = 'CellGroup'
        match = re.fullmatch(r'BATTERY_CELL_(\d+)_VOLTAGE', source_name)
        if match:
            alias = f'CellVoltage{int(match[1]):03}'
    return short_name(alias or camel(source_name), used)


def message_name(ident, definition, used, unknown_index):
    senders = list(definition.get('senders') or [])
    # Electric-VCU is one of the upstream alternatives, not independently proven TX identity.
    sender = 'E_VCU' if 'E_VCU' in senders else senders[0] if senders else 'Unknown'
    if not definition:
        return short_name(f'Unknown_Message{unknown_index:02}', used), ['Vector__XXX']
    source_name = NAMES.get(ident, definition.get('name', 'Message'))
    for prefix in ('E_VCU_', 'VCU_', 'BSI_', 'ABS_', 'CMM_', 'ESM_', 'LVNSD_', 'CVM_'):
        if source_name.startswith(prefix):
            source_name = source_name[len(prefix):]
            break
    description = camel(source_name)
    for old, new in (('Synchronisation', 'Sync'), ('Temperature', 'Temp'),
                     ('Temperatures', 'Temps'), ('Dynamic', 'Dyn'), ('Battery', 'Batt'),
                     ('Management', 'Mgmt')):
        description = description.replace(old, new)
    # Message names are ECU + description, never Raw_<CAN-ID> / Hs1_<CAN-ID>.
    return short_name(f'{sender}_{description}'[:32], used), [sender] + [s for s in senders if s != sender]


def bit_spec(first, last):
    return f'{first // 8 + 1}.{7 - first % 8}-{last // 8 + 1}.{7 - last % 8}'


def confidence(ident, name):
    if ident in (0x349, 0x489, 0x4E9, 0x4FE, 0x572) and name in REVIEWED_ALIASES.get(ident, {}).values():
        return 'SUPPORTED_CAPTURE: coherent changes across independent messages'
    if (ident in (0x30D, 0x44D) and name in REVIEWED_ALIASES.get(ident, {}).values()) or (ident == 0x5C3 and name == 'BatterySoc'):
        return 'OWNER_CONFIRMED in READY capture: speed/SOC agree with vehicle; startup values need validity gating'
    if ident == 0x334:
        return 'SUPPORTED_CAPTURE: coherent nonzero 108-cell sweeps; startup zero payloads are not valid battery readings'
    return 'DATABASE_CANDIDATE: layout imported; physical meaning not independently verified'


def build(captures, definitions):
    by_id = collections.defaultdict(list)
    for label, frames in captures:
        for time, ident, payload in frames:
            by_id[ident].append((label, time, payload))
    messages, inventory, origins, audit = [], [], {}, []
    message_names, unknown_index = set(), 0
    for ident, samples in sorted(by_id.items()):
        lengths = {len(data) for _, _, data in samples}
        assert len(lengths) == 1, f'Variable DLC on {ident:03X}: {lengths}'
        length = lengths.pop()
        definition = definitions.get(ident, {})
        match = ('no_HS1_definition' if not definition else
                 'DLC_unspecified' if definition.get('length') is None else
                 'ID_DLC_match' if definition['length'] == length else 'DLC_mismatch')
        signals, used_names, aliases = [], set(), {}
        source_signals = definition.get('signals', {})
        for source_name in source_signals:
            aliases[source_name] = signal_name(ident, source_name, used_names)

        def add(name, spec, certainty, source_name='', status='retained_source', reason=''):
            spec = dict(spec)
            spec['units'] = str(spec.get('units', '')).replace('\ufffdC', 'degC').replace('\u00b0C', 'degC')
            mux = spec.get('mux', {})
            signal = make_signal(name, spec, certainty,
                                 aliases.get(mux.get('selector'), mux.get('selector')),
                                 mux.get('values'), spec.get('mux_selector', False))
            signal.receivers = list(definition.get('receivers') or [])
            if spec.get('alternatives'):
                signal.comment += ' Unresolved source alternatives: ' + json.dumps(spec['alternatives'], ensure_ascii=True, sort_keys=True)
            if match == 'DLC_mismatch':
                signal.comment += f" WARNING: upstream DLC {definition.get('length')} differs from observed {length}; this in-bounds prefix is an unverified candidate."
            signals.append(signal)
            spec['_raw'] = bool(spec.get('unused')) or status == 'raw_coverage'
            origins[ident, name] = spec
            audit.append({'id_hex': f'{ident:03X}', 'source_signal': source_name,
                          'dbc_signal': name, 'status': status, 'reason': reason,
                          'bits': spec['bits'], 'mux': spec.get('mux', {})})

        for source_name, original in source_signals.items():
            spec, name = dict(original), aliases[source_name]
            if ident == 0x3F4:
                audit.append({'id_hex': '3F4', 'source_signal': source_name, 'dbc_signal': '',
                              'status': 'replaced_temperature_slot_mapping',
                              'reason': 'Preserved group/byte values as logical slots; source unconditional last-byte probe and repeated physical numbering are not valid group-specific identities.',
                              'bits': spec['bits'], 'mux': spec.get('mux', {})})
                continue
            first, last, _ = positions(spec['bits'])
            if last >= length * 8:
                audit.append({'id_hex': f'{ident:03X}', 'source_signal': source_name,
                              'dbc_signal': '', 'status': 'excluded_out_of_payload',
                              'reason': f'Bit range {spec["bits"]} exceeds every captured DLC={length}.',
                              'bits': spec['bits'], 'mux': spec.get('mux', {})})
                continue
            status, reason = 'retained_source', ''
            if ident == 0x5A3 and source_name in ('HV_BATTERY_CELL_NUMBER_VOLT_MAX', 'HV_BATTERY_CELL_NUMBER_VOLT_MIN'):
                spec.update(factor=1, min=1, max=108)
                status = 'corrected_cell_index_scale'
                reason = 'Upstream factor 11 produces indices outside a demonstrated 108-cell pack; retain raw factor 1 as candidate, physical ordering unverified.'
                spec['comment'] = str(spec.get('comment', '')) + ' ' + reason
            add(name, spec, confidence(ident, name), source_name, status, reason)

        if ident == 0x3F4:
            aliases['BATTERY_TEMP_PROBE_GROUP_ID'] = 'TemperatureGroup'
            selector = {'bits': '1.7-1.5', 'type': 'uint', 'mux_selector': True,
                        'comment': 'Source group index; logical slots do not assert installed sensor identities.'}
            add('TemperatureGroup', selector, 'SUPPORTED_CAPTURE: group counter',
                'BATTERY_TEMP_PROBE_GROUP_ID', 'temperature_slot_mapping')
            for index in range(54):
                byte = 2 + index % 7
                spec = {'bits': f'{byte}.7-{byte}.0', 'factor': 1, 'offset': -40,
                        'units': 'degC', 'invalid': 255,
                        'mux': {'selector': 'TemperatureGroup', 'values': [index // 7]},
                        'comment': 'Logical group/byte temperature slot. Physical sensor numbering and repeated values unverified. Group-7 padding is raw, not a -40 degC sensor.'}
                add(f'BatteryTempSlot{index + 1:02}', spec, confidence(ident, ''),
                    status='temperature_slot_mapping')

        # Cover all payload bits on every multiplexer branch without overlapping named fields.
        selector = next((s for s in signals if s.is_multiplexer), None)
        mux_values = range(1 << selector.length) if selector else [None]
        gaps = collections.defaultdict(list)
        for mid in mux_values:
            occupied = set()
            for signal in signals:
                if signal.multiplexer_ids is not None and mid not in signal.multiplexer_ids:
                    continue
                first, last, _ = positions(origins[ident, signal.name]['bits'])
                occupied.update(range(first, last + 1))
            bit = 0
            while bit < length * 8:
                if bit in occupied:
                    bit += 1
                    continue
                first = bit
                while bit + 1 < length * 8 and (bit + 1) % 8 and bit + 1 not in occupied:
                    bit += 1
                gaps[first, bit].append(mid)
                bit += 1
        for (first, last), mids in sorted(gaps.items()):
            all_states = len(mids) == len(mux_values)
            field = f'RawByte{first // 8 + 1}' if last-first == 7 else f'RawBits{first}_{last}'
            if not all_states:
                field += '_Mux' + '_'.join(map(str, mids))
            name = short_name(field, used_names)
            spec = {'bits': bit_spec(first, last), 'units': 'raw',
                    'comment': 'Raw unmapped/padding bits; no physical interpretation.'}
            if not all_states:
                spec['mux'] = {'selector': selector.name, 'values': mids}
            add(name, spec, 'RAW', status='raw_coverage')

        if not definition:
            unknown_index += 1
        name, senders = message_name(ident, definition, message_names, unknown_index)
        source_path = definition.get('path', '')
        comment = (f'Observed OBD 6/14 HS1. CAN ID 0x{ident:03X}; {match}. '
                   f"PSA-RE name: {definition.get('name', 'unknown')}. {definition.get('comment', '')} "
                   f'Source: {SOURCE + source_path if source_path else "none in HS.HS1"}. '
                   f"Database sender alternatives: {','.join(definition.get('senders') or []) or 'unknown'}. "
                   'ECU labels are database claims; forwarding/physical transmitter not independently identified. '
                   'All fitting source fields retained, including default/unavailable-only candidates. See audit/source JSON for exclusions and unresolved alternatives.')
        if ident == 0x563:
            comment += ' WARNING: capture alternates payload families; source has no selector. Candidate fields are retained for research, not validated as one fixed physical layout.'
        messages.append(Message(ident, name, length, signals, comment=comment,
                                senders=senders, strict=True))
        per_file = collections.defaultdict(list)
        for label, time, _ in samples:
            per_file[label].append(time)
        periods = sorted((b-a)*1000 for times in per_file.values() for a,b in zip(times,times[1:]))
        inventory.append({'id_hex': f'{ident:03X}', 'id_decimal': ident, 'message_name': name,
                          'dbc_sender': senders[0], 'count': len(samples), 'dlc': length,
                          'captures': ','.join(per_file),
                          'median_period_ms': round(periods[len(periods)//2], 3) if periods else '',
                          'unique_payloads': len({b for _, _, b in samples}), 'source_match': match,
                          'dbc_mode': 'database_fields' if source_signals else 'raw_only',
                          'signal_count': len(signals), 'source_signal_count': len(source_signals),
                          'source_name': definition.get('name', ''),
                          'source_senders': ','.join(definition.get('senders') or []),
                          'source_url': SOURCE + source_path if source_path else '',
                          'first_payload': samples[0][2].hex(' ').upper()})
    nodes = sorted({n for m in messages for n in m.senders if n != 'Vector__XXX'} |
                   {n for m in messages for s in m.signals for n in s.receivers})
    database = cantools.database.Database(messages=messages, nodes=[
        Node(name, comment='PSA-RE ECU alias, not independently identified physical transmitter/receiver.')
        for name in nodes], strict=True)
    return database, inventory, origins, audit


def verify(database, frames, origins):
    compared, raw_compared = 0, 0
    ranges = {}
    decode_count = collections.Counter()
    sweeps, latest_cells, last_group = [], {}, None
    defaults, invalids = collections.Counter(), collections.Counter()
    widths = {key: positions(spec['bits'])[1]-positions(spec['bits'])[0]+1
              for key, spec in origins.items()}
    for time, ident, payload in frames:
        message = database.get_message_by_frame_id(ident)
        decoded = message.decode(payload, decode_choices=False, scaling=True)
        raw_decoded = message.decode(payload, decode_choices=False, scaling=False)
        decode_count[ident] += 1
        covered = 0
        for name, value in decoded.items():
            spec = origins[ident, name]
            covered += widths[ident, name]
            raw_unsigned = extract(payload, spec['bits'])
            raw = raw_unsigned
            if spec.get('type') == 'sint':
                first, last, _ = positions(spec['bits'])
                width = last-first+1
                if raw & (1 << (width-1)):
                    raw -= 1 << width
            expected = raw * spec.get('factor', 1) + spec.get('offset', 0)
            assert math.isclose(value, expected, rel_tol=1e-10, abs_tol=1e-9), (ident, name, value, expected)
            assert raw_decoded[name] == raw, (ident, name, raw_decoded[name], raw)
            if spec.get('_raw'):
                raw_compared += 1
                continue
            compared += 1
            if raw_unsigned == spec.get('invalid'):
                invalids[name] += 1
                continue
            if raw_unsigned == spec.get('default'):
                defaults[name] += 1
            key = ident, name
            item = ranges.setdefault(key, {'count': 0, 'min': value, 'max': value})
            item.update(count=item['count']+1, min=min(item['min'],value), max=max(item['max'],value))
        # Strict reload rules out overlap; this rules out any lost payload bits,
        # including mux padding and branches not described in the upstream layout.
        assert covered == len(payload)*8, (ident, covered, len(payload)*8)
        if ident == 0x334:
            group = decoded['CellGroup']
            if group == 0 or last_group is None or group != last_group+1:
                latest_cells = {}
            latest_cells.update({n:v for n,v in decoded.items() if n.startswith('CellVoltage')})
            last_group = group
            if group == 26 and len(latest_cells) == 108:
                # Keep zero/startup sweeps separate; numerical decoding alone is not availability.
                sweeps.append((time, dict(latest_cells)))
    assert decode_count == collections.Counter(ident for _, ident, _ in frames)
    assert sum(decode_count.values()) == len(frames)
    nonzero = [(t, cells) for t,cells in sweeps if all(0 < v < 16.383 for v in cells.values())]
    sums = [sum(cells.values()) for _,cells in nonzero]
    return {'frames_decoded': len(frames), 'full_bit_coverage_frames': len(frames), 'compared_signal_values': compared,
            'raw_field_values_checked': raw_compared, 'complete_108_cell_sweeps': len(sweeps),
            'nonzero_108_cell_sweeps': len(nonzero),
            'zero_or_unavailable_cell_sweeps': len(sweeps)-len(nonzero),
            'cell_sum_V_min': min(sums) if sums else None,
            'cell_sum_V_max': max(sums) if sums else None,
            'invalid_samples': sum(invalids.values()), 'source_default_samples': sum(defaults.values()),
            'ranges': [{'id_hex':f'{i:03X}','signal':n,**v} for (i,n),v in sorted(ranges.items())]}, nonzero


def write_csv(path, rows):
    with path.open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--psa-re', type=Path, required=True, help='Pinned PSA-RE ZIP archive')
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--capture', type=Path, action='append', help='Repeat for independent captures')
    source.add_argument('--capture-dir', type=Path, help='All top-level CSV captures in a directory')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    paths = args.capture or sorted(args.capture_dir.glob('*.csv'))
    if not paths:
        parser.error('No CSV captures selected')
    labels = [p.name for p in paths]
    if len(labels) != len(set(labels)):
        parser.error('Capture basenames must be unique')
    captures = [(p.name,capture(p)) for p in paths]
    definitions, license_text = load_source(args.psa_re)
    database, inventory, origins, audit = build(captures, definitions)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    database.version = f'eCMPilot HS1 database-first 2026-10-28; PSA-RE {COMMIT}; Apache-2.0'
    cantools.database.dump_file(database, str(args.output), database_format='dbc', encoding='utf-8')
    loaded = cantools.database.load_file(str(args.output), strict=True, encoding='utf-8')
    assert len({m.name for m in loaded.messages}) == len(loaded.messages)
    results, all_sweeps = [], []
    for path, (label,frames) in zip(paths,captures):
        result, sweeps = verify(loaded, frames, origins)
        result.update(capture=label, sha256=hashlib.sha256(path.read_bytes()).hexdigest(), duration_s=frames[-1][0])
        results.append(result)
        all_sweeps.extend((label,t,cells) for t,cells in sweeps)
    total_keys = ('frames_decoded','full_bit_coverage_frames','compared_signal_values','raw_field_values_checked',
                  'complete_108_cell_sweeps','nonzero_108_cell_sweeps','zero_or_unavailable_cell_sweeps',
                  'invalid_samples','source_default_samples')
    validation = {key:sum(r[key] for r in results) for key in total_keys}
    validation.update(source_commit=COMMIT, source_archive_sha256=hashlib.sha256(args.psa_re.read_bytes()).hexdigest(),
                      dbc_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
                      cantools_version=cantools.__version__, observed_ids=len(inventory),
                      messages_with_database_fields=sum(r['dbc_mode']=='database_fields' for r in inventory),
                      raw_only_messages=sum(r['dbc_mode']=='raw_only' for r in inventory),
                      signals_in_dbc=sum(len(m.signals) for m in loaded.messages),
                      audit_status_counts=dict(collections.Counter(r['status'] for r in audit)),
                      source_match_counts=dict(collections.Counter(r['source_match'] for r in inventory)),
                      captures=results)
    args.output.with_suffix('.validation.json').write_text(json.dumps(validation,indent=2)+'\n',encoding='utf-8')
    write_csv(args.output.with_suffix('.inventory.csv'),inventory)
    args.output.with_suffix('.audit.json').write_text(json.dumps(audit,indent=2)+'\n',encoding='utf-8')
    rejected = [r for r in audit if r['status'].startswith('excluded_')]
    if rejected:
        write_csv(args.output.with_suffix('.rejections.csv'), rejected)
    observed = {int(r['id_decimal']) for r in inventory}
    args.output.with_suffix('.source.json').write_text(json.dumps(
        {f'{i:03X}':spec for i,spec in sorted(definitions.items()) if i in observed},indent=2)+'\n',encoding='utf-8')
    if all_sweeps:
        cells=[]
        for name in sorted(all_sweeps[0][2]):
            values=[c[name] for _,_,c in all_sweeps]
            cells.append({'cell_slot':name,'first_V':values[0],'min_V':min(values),'max_V':max(values),'samples':len(values)})
        write_csv(args.output.with_suffix('.cells.csv'),cells)
    (args.output.parent/'LICENSE-PSA-RE.txt').write_bytes(license_text)
    (args.output.parent/'NOTICE-PSA-RE.txt').write_text(
        'Derived definitions: prototux/PSA-RE, https://github.com/prototux/PSA-RE\n'
        f'Source revision: {COMMIT}\nLicense: Apache License 2.0 (LICENSE-PSA-RE.txt).\n'
        'Modified by eCMPilot: all observed HS1 messages; all fitting canonical source fields, ECU-first names, '
        'raw coverage, documented payload-bound exclusions, corrected cell-index factor and logical temperature-slot multiplexing. '
        'Unresolved source alternatives retained in source JSON and signal comments. No physical validation implied.\n'
        'Derived definitions are not relicensed under the firmware BSD license.\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in validation.items() if k!='captures'},indent=2))


if __name__ == '__main__':
    main()
