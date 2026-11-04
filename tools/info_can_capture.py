"""Read-only SavvyCAN capture accounting and independent DBC checks.

File order is authoritative; clocks never bridge files. Timestamp regressions
start a new timing segment, so no period is fabricated across a reset/wrap.
Absolute timestamps are retained only as input evidence, not wall-clock dates.
"""
import collections
import csv
import hashlib
import math
from pathlib import Path
import statistics

from generate_hscan1_dbc import extract, positions


def read_capture(path):
    path = Path(path)
    frames, segment, previous, regressions = [], 0, None, []
    with path.open(encoding='utf-8-sig', newline='') as stream:
        for index, row in enumerate(csv.DictReader(stream)):
            timestamp = int(row['Time Stamp'])
            if previous is not None and timestamp < previous:
                segment += 1
                regressions.append({'frame_index': index, 'previous_us': previous, 'raw_us': timestamp})
            previous = timestamp
            length = int(row['LEN'])
            assert 0 <= length <= 8, (path, index, length)
            frames.append({'index': index, 'line': index + 2, 'raw_us': timestamp, 'segment': segment,
                           'id': int(row['ID'], 16), 'extended': row['Extended'].lower() == 'true',
                           'direction': row['Dir'], 'bus': int(row['Bus']),
                           'data': bytes(int(row[f'D{i}'], 16) for i in range(1, length + 1))})
    assert frames, f'Empty capture: {path}'
    assert all(f['direction'] == 'Rx' for f in frames), 'Expected passive Rx captures.'
    return {'name': path.name, 'path': path.as_posix(), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'frames': frames, 'timestamp_regressions': regressions,
            'duration_s': (frames[-1]['raw_us'] - frames[0]['raw_us']) / 1e6 if not regressions else None}


def summarize(captures):
    grouped = collections.defaultdict(list)
    for capture_index, capture in enumerate(captures):
        for frame in capture['frames']:
            grouped[frame['id']].append((capture_index, capture['name'], frame))
    result = {}
    for ident, samples in grouped.items():
        periods, last = [], {}
        for capture_index, name, frame in samples:
            key = capture_index, frame['bus'], frame['segment']
            if key in last:
                periods.append((frame['raw_us'] - last[key]) / 1000)
            last[key] = frame['raw_us']
        payloads = collections.Counter(frame['data'] for _, _, frame in samples)
        result[ident] = {'count': len(samples), 'captures': sorted({name for _, name, _ in samples}),
                         'dlcs': sorted({len(frame['data']) for _, _, frame in samples}),
                         'extended_flags': sorted({frame['extended'] for _, _, frame in samples}),
                         'bus_indices': sorted({frame['bus'] for _, _, frame in samples}),
                         'unique_payloads': len(payloads),
                         'median_period_ms': round(statistics.median(periods), 6) if periods else None,
                         'first_payload': samples[0][2]['data'].hex(' ').upper(),
                         'last_payload': samples[-1][2]['data'].hex(' ').upper(),
                         'common_payloads': [{'data': data.hex(' ').upper(), 'count': count}
                                             for data, count in payloads.most_common(5)]}
    return result


def validate(database, captures, origins):
    """Check every real payload against its available bits; never zero-pad.

    Origins are the generator's byte/bit definitions, independently extracted
    from actual bytes. Range/default/enum statistics are evidence for review,
    not automatic grounds to reject the physical meaning of a source field.
    """
    reports, all_ranges = [], []
    total = collections.Counter()
    for capture in captures:
        stats = collections.Counter()
        ranges, observed_ids, missing = {}, set(), collections.Counter()
        for frame in capture['frames']:
            ident, data = frame['id'], frame['data']
            observed_ids.add(ident)
            stats['frames'] += 1
            try:
                message = database.get_message_by_frame_id(ident)
            except KeyError:
                missing[f'{ident:03X}'] += 1
                continue
            assert message.is_extended_frame == frame['extended'], (capture['name'], frame['index'], 'ID format')
            stats['matched_frames'] += 1
            if len(data) != message.length:
                stats['dlc_different_from_dbc_frames'] += 1
            decoded = message.decode(data, decode_choices=False, scaling=True, allow_truncated=True)
            raw = message.decode(data, decode_choices=False, scaling=False, allow_truncated=True)
            selectors = [s for s in message.signals if s.is_multiplexer]
            assert len(selectors) <= 1
            selector = selectors[0] if selectors else None
            mid = extract(data, origins[ident, selector.name]['bits']) if selector else None
            expected_names, covered = set(), set()
            for signal in message.signals:
                spec = origins[ident, signal.name]
                first, last, _ = positions(spec['bits'])
                mux = spec.get('mux')
                if last >= len(data) * 8 or (mux and mid not in mux['values']):
                    continue
                expected_names.add(signal.name)
                unsigned = extract(data, spec['bits'])
                width, value = last - first + 1, unsigned
                if spec.get('type') == 'sint' and value & (1 << (width - 1)):
                    value -= 1 << width
                expected = value * spec.get('factor', 1) + spec.get('offset', 0)
                assert raw[signal.name] == value, (capture['name'], frame['index'], signal.name)
                assert math.isclose(decoded[signal.name], expected, rel_tol=1e-10, abs_tol=1e-9)
                span = set(range(first, last + 1))
                assert not covered & span, (capture['name'], frame['index'], signal.name, 'overlap')
                covered.update(span)
                stats['signal_values_checked'] += 1
                key = ident, signal.name
                row = ranges.setdefault(key, {'capture': capture['name'], 'id_hex': f'{ident:03X}',
                    'message_name': message.name, 'signal': signal.name, 'bits': spec['bits'],
                    'unit': signal.unit or '', 'raw_field': bool(spec.get('_raw')),
                    'count': 0, 'raw_min': value, 'raw_max': value, 'min': expected, 'max': expected,
                    'invalid_count': 0, 'default_equal_count': 0, 'outside_source_range_count': 0,
                    'undefined_enum_count': 0, 'source_unused_enum_count': 0,
                    'nonzero_reserved_count': 0})
                row.update(count=row['count'] + 1, raw_min=min(row['raw_min'], value), raw_max=max(row['raw_max'], value),
                           min=min(row['min'], expected), max=max(row['max'], expected))
                row['default_equal_count'] += int('default' in spec and unsigned == spec['default'])
                row['nonzero_reserved_count'] += int(bool(spec.get('unused')) and unsigned != 0)
                if 'invalid' in spec and unsigned == spec['invalid']:
                    row['invalid_count'] += 1
                elif not spec.get('_raw'):
                    row['outside_source_range_count'] += int(
                        ('min' in spec and expected < spec['min'] - 1e-9) or
                        ('max' in spec and expected > spec['max'] + 1e-9))
                    if spec.get('values'):
                        choice = spec['values'].get(str(unsigned), spec['values'].get(unsigned))
                        row['undefined_enum_count'] += int(choice is None)
                        row['source_unused_enum_count'] += int(isinstance(choice, dict) and choice.get('unused', False))
            assert set(raw) == set(decoded) == expected_names
            if covered == set(range(len(data) * 8)):
                stats['full_payload_coverage_frames'] += 1
            else:
                stats['uncovered_bits'] += len(set(range(len(data) * 8)) - covered)
            stats['decoded_frames'] += 1
        range_rows = [ranges[k] for k in sorted(ranges)]
        all_ranges.extend(range_rows)
        report = {k: v for k, v in capture.items() if k != 'frames'}
        report.update(counts=dict(stats), observed_ids=[f'{i:03X}' for i in sorted(observed_ids)],
                      missing_ids=dict(missing),
                      range_issue_fields=[r for r in range_rows if any(r[n] for n in
                          ('outside_source_range_count', 'undefined_enum_count', 'source_unused_enum_count', 'nonzero_reserved_count'))])
        reports.append(report)
        total.update(stats)
    return {'totals': dict(total), 'captures': reports,
            'physical_meanings_validated': False,
            'scope': 'Actual byte decoding, bit coverage, lengths and value statistics; physical meanings require independent evidence.'}, all_ranges
