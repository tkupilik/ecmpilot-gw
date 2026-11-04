#!/usr/bin/env python3
"""Export INFO-DIV CAN definitions from pinned PSA-RE, optionally checked against captures.

Python 3.10+, PyYAML, cantools; local files only, no CAN interface is opened.
Generator: project BSD-2-Clause. Derived definitions: Apache-2.0, see dbc/.
Unlike the HS1 export, this database is not scoped to captured identifiers.
"""
import argparse
import copy
import collections
import csv
import hashlib
import json
import math
from pathlib import Path
import random
import zipfile

import cantools
from cantools.database.can import Message, Node
from cantools.database.can.attribute import Attribute
from cantools.database.can.attribute_definition import AttributeDefinition
from cantools.database.can.formats.dbc_specifics import DbcSpecifics
import yaml

# Share only syntax/bit-conversion helpers, never HS1 naming or capture rules:
# the same numeric CAN ID has different meanings on different buses.
from generate_hscan1_dbc import COMMIT, SOURCE, bit_spec, camel, english, extract, make_signal, positions, short_name
from info_can_capture import read_capture, summarize as summarize_captures, validate as validate_captures

BUS = 'LS.INFO-DIV'
BITRATE = 125000
CANDIDATE = 'DATABASE_CANDIDATE: physical interpretation unverified; capture/layout evidence reported separately'
OWNER_VALIDATION_PATH = Path(__file__).resolve().parents[1] / 'dbc/opel_corsa_e_info.owner-validation.json'
OWNER_VALIDATION = json.loads(OWNER_VALIDATION_PATH.read_text(encoding='utf-8'))
assert OWNER_VALIDATION['source_commit'] == COMMIT, 'Review owner evidence before changing the source revision.'
VEHICLE_REVIEW = json.loads((Path(__file__).resolve().parents[1] /
                           'dbc/opel_corsa_e_info.vehicle-review.json').read_text(encoding='utf-8'))
assert VEHICLE_REVIEW['source_commit'] == COMMIT


def unknown_layout(ident, definition):
    record = VEHICLE_REVIEW['unknown_messages'].get(f'{ident:03X}')
    return record if record and all(definition.get(k) == record[k]
                                   for k in ('name', 'path', 'length')) else None


def wake_cause_review(ident, definition, source_name, spec):
    record = VEHICLE_REVIEW.get('wake_causes', {}).get(f'{ident:03X}')
    if (not record or source_name != record['signal'] or spec.get('bits') != record['bits']
            or any(definition.get(k) != record[k] for k in ('name', 'path', 'length'))):
        return None
    return record


def message_source_review(ident, definition):
    record = VEHICLE_REVIEW.get('message_sources', {}).get(f'{ident:03X}')
    if not record or any(definition.get(k) != record[k] for k in ('name', 'path', 'length')):
        return None
    return record


def signal_scale_review(ident, definition, source_name, spec):
    record = VEHICLE_REVIEW.get('signal_scales', {}).get(f'{ident:03X}', {}).get(source_name)
    if (not record or any(definition.get(key) != record[key] for key in ('name', 'path', 'length'))
            or any(spec.get(key, 0 if key == 'offset' else 1 if key == 'factor' else None) != value
                   for key, value in record['source_spec'].items())):
        return None
    return record


def reviewed_signals(ident, definition):
    """Vehicle-specific replacement of a reviewed reserved-bit span."""
    signals = definition.get('signals', {})
    record = VEHICLE_REVIEW.get('signal_layouts', {}).get(f'{ident:03X}')
    if (not record or any(definition.get(key) != record[key] for key in ('name', 'path', 'length'))
            or any(signals.get(name, {}).get('bits') != bits for name, bits in record['source_bits'].items())):
        return signals
    result = copy.deepcopy(signals)
    result.update(copy.deepcopy(record['export_signals']))
    for name in record['export_signals']:
        result[name]['comment'] = (result[name].get('comment', '') +
            ' VEHICLE_CAPTURE_OBSERVED: ' + record['evidence'] + ' Scope: ' + record['scope'])
    return result


def owner_confirmation(ident, definition, source_name, spec):
    """Apply this car's field-level evidence only to the exact reviewed layout."""
    record = OWNER_VALIDATION['messages'].get(f'{ident:03X}', {})
    reviewed = record.get('signals', {}).get(source_name)
    if (not reviewed or definition.get('name') != record['source_name']
            or definition.get('length') != record['length']
            or definition.get('path') != record['path']):
        return None
    if any(spec.get(key, default) != reviewed[key] for key, default in
           [('bits', None), ('factor', 1), ('offset', 0)]):
        return None
    return (f"OWNER_CONFIRMED: {OWNER_VALIDATION['vehicle']}, {OWNER_VALIDATION['date']}; "
            f"{record['evidence']} Scope: {record['scope']}")

# These source titles contradict the source's own description/fields. Change
# display names only; preserve the original titles and all layouts in metadata.
MESSAGE_TITLES = {
    0x350: 'FrontClimateStatus',
    0x11B: 'ClimatePrecondSchedRemote2',
    0x394: 'RemoteActionRequest2',
    0x3E0: 'BatterySocAndHistory',
    0x43C: 'Wakeup',
    0x412: 'WakeupCause',
    0x5FC: 'Version',
}

SIGNAL_ALIASES = {(0x3E0, 'BATTERY_STATUS'): 'BatterySocCandidate'}


def adapt_to_captures(definitions, captures):
    """Keep the full source bus, use observed DLCs, and add unknown IDs raw."""
    result = copy.deepcopy(definitions)
    observed = summarize_captures(captures)
    unknown_index = 0
    for ident, summary in sorted(observed.items()):
        if ident not in result:
            unknown_index += 1
            assert len(summary['dlcs']) == len(summary['extended_flags']) == 1, (ident, summary)
            result[ident] = {'id': ident, 'name': f'InfoMessage{unknown_index:02}', 'type': 'can',
                             'length': summary['dlcs'][0], 'extended': summary['extended_flags'][0],
                             'signals': {}, 'path': '', 'senders': [], '_capture_only': True,
                             'comment': 'Observed INFO identifier absent from the source bus. Raw bytes only; sender and physical meaning unknown.'}
        definition = result[ident]
        assert summary['extended_flags'] == [bool(definition.get('extended'))], (ident, 'ID format mismatch')
        definition['_capture_summary'] = summary
        if definition['type'] == 'can':
            assert len(summary['dlcs']) == 1, f'Variable fixed-CAN DLC needs an explicit layout policy: {ident:03X}'
            definition['export_length'] = summary['dlcs'][0]
    return result


def load_source(path):
    """Resolve only this bus, retaining every alternate file and parent layout."""
    loader = getattr(yaml, 'CSafeLoader', yaml.SafeLoader)
    with zipfile.ZipFile(path) as archive:
        root = archive.namelist()[0]
        assert root == f'PSA-RE-{COMMIT}/', 'Use the documented pinned revision.'
        architecture = yaml.load(archive.read(root + 'architectures.yml'), Loader=loader)['AEE2010']
        variant, chain = 'electric', []
        while variant:
            chain.append('AEE2010.' + variant)
            variant = architecture[variant].get('parent')
        definitions = {}
        for profile in reversed(chain):
            prefix = root + 'buses/' + profile + '/' + BUS + '/'
            grouped = collections.defaultdict(list)
            for name in sorted(archive.namelist()):
                if not name.startswith(prefix) or not name.endswith('.yml'):
                    continue
                spec = english(yaml.load(archive.read(name), Loader=loader))
                spec['path'] = name[len(root):]
                grouped[spec['id']].append(spec)
            for ident, candidates in grouped.items():
                # Prefer an exact <ID>.yml; otherwise the first sorted filename.
                candidates.sort(key=lambda d: (Path(d['path']).stem.upper() != f'{ident:03X}', d['path']))
                canonical = dict(candidates[0])
                previous = definitions.get(ident)
                alternatives = candidates[1:]
                if previous:
                    previous = dict(previous)
                    alternatives += [previous]
                if alternatives:
                    canonical['source_file_alternatives'] = alternatives
                definitions[ident] = canonical
        return definitions, archive.read(root + 'LICENCE'), chain


def transport_layout():
    """Physical Classical CAN ISO-TP frames with normal addressing, not PDUs.

    Data bytes include any padding. Byte validity/ISO-TP lengths and stream
    reassembly need a transport decoder and cannot be represented by this DBC.
    Unknown PCI types retain raw coverage, without interpreting their contents.
    """
    fields = {'IsoTpFrameType': {'bits': '1.7-1.4', 'mux_selector': True,
                                'values': {0: 'SingleFrame', 1: 'FirstFrame',
                                           2: 'ConsecutiveFrame', 3: 'FlowControl'},
                                'comment': 'PCI high nibble; normal addressing candidate.'}}

    def add(name, bits, states, **extra):
        fields[name] = {'bits': bits, 'mux': {'selector': 'IsoTpFrameType', 'values': states},
                        'units': 'raw', **extra}

    add('SingleFrameLength', '1.3-1.0', [0], comment='Declared data length; not a validity check.')
    add('FirstFrameLength', '1.3-2.0', [1], comment='12-bit declared PDU length; not a reassembled value.')
    add('ConsecutiveSequence', '1.3-1.0', [2])
    add('FlowStatus', '1.3-1.0', [3], values={0: 'ContinueToSend', 1: 'Wait', 2: 'Overflow'})
    add('BlockSize', '2.7-2.0', [3])
    add('SeparationTimeRaw', '3.7-3.0', [3],
        comment='Raw STmin code; 0xF1..0xF9 are not milliseconds.')
    add('UnknownPciLow', '1.3-1.0', list(range(4, 16)))
    for byte in range(2, 9):
        add(f'SingleData{byte - 1}', f'{byte}.7-{byte}.0', [0])
        add(f'ConsecutiveData{byte - 1}', f'{byte}.7-{byte}.0', [2])
        add(f'UnknownData{byte - 1}', f'{byte}.7-{byte}.0', list(range(4, 16)))
        if byte >= 3:
            add(f'FirstData{byte - 2}', f'{byte}.7-{byte}.0', [1])
        if byte >= 4:
            add(f'FlowPadding{byte - 3}', f'{byte}.7-{byte}.0', [3])
    return fields


def message_title(ident, definition, used):
    senders = list(definition.get('senders') or ['Vector__XXX'])
    review = message_source_review(ident, definition)
    if review:
        senders = [review['sender']]
    sender = senders[0] if senders[0] != 'Vector__XXX' else 'Unknown'
    title = MESSAGE_TITLES.get(ident, definition.get('name', 'Message'))
    if unknown_layout(ident, definition):
        title = f'Unknown{ident:03X}'
    if definition.get('type') == 'can-tp' and set(definition.get('signals', {})) == {'DIAGNOSTIC_DATA'}:
        description = definition.get('comment', '').lower()
        target = (definition.get('receivers') or ['Unknown'])[0]
        title = ('DiagRequestTo' + target if 'request' in description else
                 'DiagResponse' if 'response' in description else 'DiagData')
    prefixes = [sender] + list(definition.get('senders') or []) if review else [sender]
    for prefix in [p + separator for p in prefixes for separator in ('_', ' ')]:
        if title.startswith(prefix):
            title = title[len(prefix):]
    title = camel(title)
    for old, new in [('Temperature', 'Temp'), ('Preconditioning', 'Precond'),
                     ('Information', 'Info'), ('ProgramScreen', 'Sched')]:
        title = title.replace(old, new)
    return short_name(sender + '_' + title, used), senders


def build(definitions):
    messages, inventory, audit, origins = [], [], [], {}
    used_messages = set()
    for ident, definition in sorted(definitions.items()):
        transport = definition['type'] == 'can-tp'
        assert definition['type'] in ('can', 'can-tp'), (ident, definition['type'])
        length = 8 if transport else definition.get('export_length', definition['length'])
        assert isinstance(length, int) and 0 < length <= 8
        source_signals = definition.get('signals', {})
        unknown = unknown_layout(ident, definition)
        exported = transport_layout() if transport else {} if unknown else reviewed_signals(ident, definition)
        used, aliases = set(), {}
        for source_name in exported:
            aliases[source_name] = short_name(SIGNAL_ALIASES.get((ident, source_name), camel(source_name)), used)
        signals, occupied, owner_fields = [], set(), []
        for source_name, original in exported.items():
            spec = dict(original)
            scale_review = signal_scale_review(ident, definition, source_name, spec)
            if scale_review:
                spec.update(scale_review['export_spec'])
                spec['comment'] = (spec.get('comment', '') + ' VEHICLE_SCALE_OBSERVED: ' +
                                   scale_review['evidence'] + ' Scope: ' + scale_review['scope'])
            wake_review = wake_cause_review(ident, definition, source_name, spec)
            if wake_review:
                spec['values'] = {**spec.get('values', {}), **wake_review['values']}
                spec['comment'] = (spec.get('comment', '') + ' OWNER_EVENT_OBSERVED: ' +
                                   wake_review['evidence'] + ' Scope: ' + wake_review['scope'])
            first, last, _ = positions(spec['bits'])
            if last >= length * 8:
                assert definition.get('_capture_summary'), (ident, source_name, spec['bits'], length)
                audit.append({'id_hex': f'{ident:03X}', 'source_signal': source_name, 'dbc_signal': '',
                              'status': 'excluded_out_of_captured_payload', 'bits': spec['bits'],
                              'mux': spec.get('mux', {}),
                              'reason': f'Entire source field does not fit observed fixed DLC {length}; retained in source JSON.'})
                continue
            opaque = spec.get('type') in ('str', 'bytes')
            if opaque:
                spec['comment'] = (spec.get('comment', '') +
                                   ' Source text/byte field exposed as raw integer; no string decoding.')
            spec['units'] = str(spec.get('units', 'raw' if opaque else '')).replace('\u00b0C', 'degC')
            mux = spec.get('mux', {})
            confirmed = None if transport else owner_confirmation(ident, definition, source_name, spec)
            signal = make_signal(aliases[source_name], spec,
                                 'TRANSPORT_CANDIDATE: normal-addressing ISO-TP' if transport else confirmed or CANDIDATE,
                                 aliases.get(mux.get('selector')), mux.get('values'),
                                 spec.get('mux_selector', False))
            if confirmed:
                owner_fields.append(signal.name)
            signal.receivers = list(definition.get('receivers') or [])
            if spec.get('alternatives'):
                signal.comment += ' Unresolved source alternatives: ' + json.dumps(spec['alternatives'], ensure_ascii=True)
            if ident == 0x3E0 and source_name == 'BATTERY_STATUS':
                signal.comment += (' Candidate direct percent in byte 0; see per-capture values and research notes. '
                                   'A constant operating point alone does not validate rounding, freshness or the full scale.')
            signals.append(signal)
            spec['_raw'] = transport or opaque or spec.get('unused', False)
            origins[ident, signal.name] = spec
            occupied.update(range(first, last + 1))
            audit.append({'id_hex': f'{ident:03X}', 'source_signal': '' if transport else source_name,
                          'dbc_signal': signal.name,
                          'status': 'transport_layout' if transport else 'opaque_source' if opaque else
                                    'vehicle_scale_override' if scale_review else 'retained_source',
                          'bits': spec['bits'], 'mux': mux})
        # Fixed CAN messages have no source selector in this pinned bus. Reject
        # unexpected source multiplexing rather than silently covering it wrong.
        if not transport:
            assert not any(s.is_multiplexer or s.multiplexer_signal for s in signals), ident
            bit = 0
            while bit < length * 8:
                if bit in occupied:
                    bit += 1
                    continue
                first = bit
                while bit + 1 < length * 8 and (bit + 1) % 8 and bit + 1 not in occupied:
                    bit += 1
                name = short_name(f'RawByte{first // 8 + 1}' if bit-first == 7 else f'RawBits{first}_{bit}', used)
                spec = {'bits': bit_spec(first, bit), 'units': 'raw', '_raw': True,
                        'comment': 'Unmapped/padding bits, no physical interpretation.'}
                signals.append(make_signal(name, spec, 'RAW'))
                origins[ident, name] = spec
                audit.append({'id_hex': f'{ident:03X}', 'source_signal': '', 'dbc_signal': name,
                              'status': 'raw_coverage', 'bits': spec['bits'], 'mux': {}})
                bit += 1
        if transport:
            for source_name, spec in source_signals.items():
                audit.append({'id_hex': f'{ident:03X}', 'source_signal': source_name, 'dbc_signal': '',
                              'status': 'source_preserved_separate_layer', 'bits': spec['bits'],
                              'mux': spec.get('mux', {}),
                              'reason': 'Original layout retained in source JSON. Export describes physical ISO-TP frames; logical PDU fields cannot be copied to wire offsets.'})
        if unknown:
            for source_name, spec in source_signals.items():
                audit.append({'id_hex': f'{ident:03X}', 'source_signal': source_name, 'dbc_signal': '',
                              'status': 'source_preserved_unknown_vehicle_layout', 'bits': spec['bits'],
                              'mux': spec.get('mux', {}), 'reason': unknown['reason']})
        name, senders = message_title(ident, definition, used_messages)
        sender_review = message_source_review(ident, definition)
        source_senders = list(definition.get('senders') or [])
        source_url = SOURCE + definition['path'] if definition['path'] else ''
        confidence = (f"OWNER_CONFIRMED_FIELDS: {', '.join(owner_fields)}; other fields remain candidates"
                      if owner_fields else CANDIDATE)
        if unknown:
            confidence = 'VEHICLE_LAYOUT_UNKNOWN: ' + unknown['reason'] + ' Raw bytes only; original interpretation retained in source JSON'
        comment = (f'{confidence}. INFO-DIV CAN, {BITRATE} bit/s; CAN ID 0x{ident:03X}. '
                   f"PSA-RE title: {definition['name']}. {definition.get('comment', '')} "
                   f"Source: {source_url or 'none; capture-only raw layout'}. "
                   f"Source sender alternatives: {','.join(source_senders)}; physical TX/gateway forwarding not verified. ")
        if sender_review:
            comment += ('VEHICLE_SOURCE_REVIEW: ' + sender_review['evidence'] +
                        ' Scope: ' + sender_review['scope'] + ' ')
        observed = definition.get('_capture_summary', {})
        if observed:
            comment += f"Observed {observed['count']} frames in {','.join(observed['captures'])}; DLCs {observed['dlcs']}. "
            if not transport and definition.get('length') != length:
                comment += f"SOURCE_DLC_MISMATCH: source {definition.get('length')}, observed/exported {length}. Fitting fields retained; additional bytes raw. "
        if transport:
            comment += ('WIRE ISO-TP ONLY: normal addressing candidate, maximum Classical CAN DLC 8; actual DLC may be shorter. '
                        'Data includes padding. No reassembly, text, DID or application-layer decoding. '
                        'Original application layouts are preserved in source JSON; do not decode them at wire offsets. ')
        if definition.get('alternatives') or definition.get('source_file_alternatives'):
            comment += 'Unresolved alternative source layouts preserved in source JSON. '
        messages.append(Message(ident, name, length, signals, senders=senders, comment=comment,
                                is_extended_frame=bool(definition.get('extended')), strict=True))
        inventory.append({'id_hex': f'{ident:03X}', 'id_decimal': ident,
                          'extended': bool(definition.get('extended')), 'message_name': name,
                          'dbc_sender': senders[0], 'source_senders': ','.join(source_senders),
                          'vehicle_sender_review': sender_review['evidence'] if sender_review else '',
                          'source_receivers': ','.join(definition.get('receivers') or []),
                          'dbc_mode': 'isotp_wire' if transport else 'vehicle_unknown_raw' if unknown else 'capture_raw' if definition.get('_capture_only') else 'source_fields',
                          'dbc_dlc': length, 'source_dlc': definition.get('length', ''),
                          'source_length_min': definition.get('length_min', ''),
                          'length_basis': 'Classical CAN transport maximum' if transport else 'observed fixed DLC' if observed else 'source fixed DLC, unobserved',
                          'source_name': definition['name'], 'signal_count': len(signals),
                          'source_signal_count': len(source_signals),
                          'source_url': source_url,
                          'has_alternatives': bool(definition.get('alternatives') or definition.get('source_file_alternatives')),
                          'observed_count': observed.get('count', 0),
                          'observed_dlcs': ','.join(map(str, observed.get('dlcs', []))),
                          'captures': ','.join(observed.get('captures', [])),
                          'median_period_ms': observed.get('median_period_ms'),
                          'unique_payloads': observed.get('unique_payloads', ''),
                          'vehicle_validated': False,
                          'owner_confirmed_fields': ','.join(owner_fields)})
    nodes = sorted({n for m in messages for n in m.senders + [r for s in m.signals for r in s.receivers]
                    if n != 'Vector__XXX'})
    attrs = {'BusType': AttributeDefinition('BusType', type_name='STRING'),
             'Baudrate': AttributeDefinition('Baudrate', type_name='INT', minimum=0, maximum=1000000)}
    dbc = DbcSpecifics(attribute_definitions=attrs, attributes={
        'BusType': Attribute('CAN', attrs['BusType']), 'Baudrate': Attribute(BITRATE, attrs['Baudrate'])})
    database = cantools.database.Database(messages=messages, nodes=[
        Node(n, comment=('Vehicle unit004 instrument cluster; see per-message capture attribution.'
                         if n == 'IC' else 'PSA-RE alias; physical transmitter identity unverified.')) for n in nodes],
        version=f"eCMP INFO-DIV vehicle review {VEHICLE_REVIEW['date']}; {BITRATE} bit/s; PSA-RE {COMMIT}; Apache-2.0",
        dbc_specifics=dbc, strict=True)
    return database, inventory, audit, origins


def sample_payloads(message):
    """Bit walks plus every mux state on several deterministic byte patterns."""
    length = message.length
    samples = {bytes(length), bytes([255] * length), bytes([0xAA] * length),
               bytes([0x55] * length), bytes(range(length))}
    samples.update((1 << bit).to_bytes(length, 'big') for bit in range(length * 8))
    rng = random.Random(message.frame_id)
    samples.update(bytes(rng.randrange(256) for _ in range(length)) for _ in range(4))
    selector = next((s for s in message.signals if s.is_multiplexer), None)
    if selector:
        first = (selector.start // 8) * 8 + 7 - selector.start % 8
        shift = length * 8 - first - selector.length
        mask = ((1 << selector.length) - 1) << shift
        for value in range(1 << selector.length):
            for data in (bytes(length), bytes([255] * length), bytes([0xAA] * length),
                         bytes([0x55] * length), bytes(range(length))):
                raw = (int.from_bytes(data, 'big') & ~mask) | (value << shift)
                samples.add(raw.to_bytes(length, 'big'))
    return sorted(samples)


def verify(database, definitions, origins):
    counts = collections.Counter()
    for message in database.messages:
        transport = definitions[message.frame_id]['type'] == 'can-tp'
        selector = next((s for s in message.signals if s.is_multiplexer), None)

        def check(payload, truncated=False):
            raw = message.decode(payload, scaling=False, decode_choices=False, allow_truncated=truncated)
            scaled = message.decode(payload, scaling=True, decode_choices=False, allow_truncated=truncated)
            expected_names, covered = set(), set()
            mid = extract(payload, origins[message.frame_id, selector.name]['bits']) if selector else None
            for signal in message.signals:
                spec = origins[message.frame_id, signal.name]
                first, last, _ = positions(spec['bits'])
                mux = spec.get('mux')
                if last >= len(payload) * 8 or (mux and mid not in mux['values']):
                    continue
                expected_names.add(signal.name)
                value = extract(payload, spec['bits'])
                width = last - first + 1
                if spec.get('type') == 'sint' and value & (1 << (width - 1)):
                    value -= 1 << width
                assert raw[signal.name] == value, (message.name, signal.name, raw[signal.name], value)
                expected = value * spec.get('factor', 1) + spec.get('offset', 0)
                assert math.isclose(scaled[signal.name], expected, rel_tol=1e-10, abs_tol=1e-9)
                span = set(range(first, last + 1))
                assert not covered & span, (message.name, signal.name, 'overlap')
                covered.update(span)
                counts['raw_values_checked' if spec.get('_raw') else 'source_values_checked'] += 1
            assert set(raw) == set(scaled) == expected_names, (message.name, 'active branch')
            if not truncated:
                assert covered == set(range(len(payload) * 8)), (message.name, 'lost payload bits')
                assert message.encode(raw, scaling=False, strict=False, padding=False) == payload
                counts['full_frames_checked'] += 1
            else:
                counts['short_transport_frames_checked'] += 1

        for payload in sample_payloads(message):
            check(payload)
        if transport:
            for pci in range(16):
                for length in range(1, 8):
                    check(bytes([pci << 4]) + bytes(range(1, length)), truncated=True)
        counts['messages_checked'] += 1
    return dict(counts)


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--psa-re', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('dbc/opel_corsa_e_info.dbc'))
    parser.add_argument('--capture', type=Path, action='append', default=[],
                        help='Repeat for INFO-only SavvyCAN CSVs; never mix HS1 files by matching numeric IDs')
    args = parser.parse_args()
    definitions, license_text, chain = load_source(args.psa_re)
    captures = [read_capture(path) for path in args.capture]
    export_definitions = adapt_to_captures(definitions, captures)
    database, inventory, audit, origins = build(export_definitions)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(database.as_dbc_string(), encoding='utf-8')
    # Validate the actual serialized export, not just in-memory constructors.
    loaded = cantools.database.load_file(args.output, encoding='utf-8', strict=True)
    assert len(loaded.messages) == len(export_definitions)
    validation = verify(loaded, export_definitions, origins)
    validation.update(source_commit=COMMIT, source_bus=BUS, source_chain=chain, bitrate=BITRATE,
                      source_archive_sha256=hashlib.sha256(args.psa_re.read_bytes()).hexdigest(),
                      dbc_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
                      cantools_version=cantools.__version__, vehicle_validated=False,
                      owner_validation=OWNER_VALIDATION,
                      vehicle_review=VEHICLE_REVIEW,
                      owner_confirmed_fields=sum(bool(s.comment and s.comment.startswith('OWNER_CONFIRMED:'))
                                                for m in loaded.messages for s in m.signals),
                      physical_can_io=False, source_can_messages=sum(d['type'] == 'can' for d in definitions.values()),
                      isotp_wire_messages=sum(d['type'] == 'can-tp' for d in definitions.values()),
                      extended_messages=sum(m.is_extended_frame for m in loaded.messages),
                      captured_ids=len(summarize_captures(captures)),
                      capture_only_ids=[f'{i:03X}' for i in export_definitions if i not in definitions],
                      signals_in_dbc=sum(len(m.signals) for m in loaded.messages),
                      audit_status_counts=dict(collections.Counter(r['status'] for r in audit)))
    if captures:
        capture_report, ranges = validate_captures(loaded, captures, origins)
        baseline, _, _, baseline_origins = build(definitions)
        baseline_report, _ = validate_captures(baseline, captures, baseline_origins)
        capture_report.update(dbc_sha256=validation['dbc_sha256'], source_commit=COMMIT,
                              source_baseline_counts=baseline_report['totals'],
                              source_baseline_missing_ids={c['name']: c['missing_ids'] for c in baseline_report['captures']},
                              observed_messages={f'{i:03X}': v for i, v in sorted(summarize_captures(captures).items())},
                              source_length_mismatches=[{'id_hex': r['id_hex'], 'source_dlc': r['source_dlc'],
                                                        'observed_dlcs': r['observed_dlcs'], 'dbc_dlc': r['dbc_dlc']}
                                  for r in inventory if r['observed_count'] and r['source_dlc'] != r['dbc_dlc']
                                  and r['dbc_mode'] == 'source_fields'])
        assert capture_report['totals']['frames'] == capture_report['totals']['full_payload_coverage_frames']
        assert capture_report['totals']['frames'] == capture_report['totals']['decoded_frames']
        write_json(args.output.with_suffix('.capture-validation.json'), capture_report)
        with args.output.with_suffix('.capture-ranges.csv').open('w', encoding='utf-8', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(ranges[0]))
            writer.writeheader()
            writer.writerows(ranges)
        validation['capture_counts'] = capture_report['totals']
    with args.output.with_suffix('.inventory.csv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(inventory[0]))
        writer.writeheader()
        writer.writerows(inventory)
    write_json(args.output.with_suffix('.source.json'), {f'{i:03X}': d for i, d in sorted(definitions.items())})
    write_json(args.output.with_suffix('.audit.json'), audit)
    write_json(args.output.with_suffix('.validation.json'), validation)
    license_path = args.output.parent / 'LICENSE-PSA-RE.txt'
    if license_path.exists():
        assert license_path.read_bytes() == license_text, 'Do not overwrite a different license.'
    else:
        license_path.write_bytes(license_text)
    print(json.dumps(validation, indent=2))


if __name__ == '__main__':
    main()
