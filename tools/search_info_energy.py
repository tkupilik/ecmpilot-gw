#!/usr/bin/env python3
"""Find numeric bit-field hypotheses near an approximate energy reference.

Read-only analysis, no CAN I/O or DBC edits. A numeric match is NOT evidence
of an energy signal. Search: unsigned contiguous 8..16-bit BE/LE fields,
zero offset, explicitly listed scales. Require support in every input capture.
"""
import argparse
import collections
import json
from pathlib import Path

from generate_hscan1_dbc import positions
from info_can_capture import read_capture

SCALES = [.001, .005, .01, 1 / 64, .016, .02, .025, 1 / 32,
          .032, .05, .1, .125, .25, .5, 1]


def numeric_value(data, endian, start, width):
    shift = len(data) * 8 - start - width if endian == 'BE' else start
    return (int.from_bytes(data, 'big' if endian == 'BE' else 'little') >> shift) & ((1 << width) - 1)


def search(captures, source, reference, tolerance=.3, min_fraction=.9):
    grouped = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
    for index, capture in enumerate(captures):
        for frame in capture['frames']:
            grouped[frame['id']][index][frame['data']] += 1
    matches, searched_ids = [], []
    for ident, logs in sorted(grouped.items()):
        if len(logs) != len(captures):
            continue
        total = sum(sum(payloads.values()) for payloads in logs.values())
        length = min(len(data) for payloads in logs.values() for data in payloads)
        if total < 20 or length == 0:
            continue
        searched_ids.append(f'{ident:03X}')
        definition = source.get(f'{ident:03X}', {})
        source_spans = {}
        if definition.get('type') == 'can':
            for name, spec in definition.get('signals', {}).items():
                if spec.get('unused'):
                    continue
                first, last, _ = positions(spec['bits'])
                source_spans[name] = set(range(first, last + 1))
        for endian in ['BE', 'LE']:
            for width in range(8, min(16, length * 8) + 1):
                for start in range(length * 8 - width + 1):
                    histograms = {}
                    for index, payloads in logs.items():
                        histogram = collections.Counter()
                        for data, count in payloads.items():
                            histogram[numeric_value(data, endian, start, width)] += count
                        histograms[index] = histogram
                    combined = sum(histograms.values(), collections.Counter())
                    for scale in SCALES:
                        near = sum(count for raw, count in combined.items() if abs(raw * scale - reference) <= tolerance)
                        if near < 20 or near / total < min_fraction:
                            continue
                        if not all(any(abs(raw * scale - reference) <= tolerance for raw in h) for h in histograms.values()):
                            continue
                        span = (set(range(start, start + width)) if endian == 'BE' else
                                {bit // 8 * 8 + 7 - bit % 8 for bit in range(start, start + width)})
                        overlaps = sorted(name for name, bits in source_spans.items() if bits & span)
                        matches.append({'id_hex': f'{ident:03X}', 'endian': endian, 'linear_start': start,
                                        'length': width, 'scale_kwh': scale, 'near_reference_count': near,
                                        'total_count': total, 'min_kwh': min(combined) * scale,
                                        'max_kwh': max(combined) * scale, 'unique_raw_values': len(combined),
                                        'source_message': definition.get('name', ''),
                                        'source_nonreserved_overlaps': overlaps,
                                        'raw_histogram': dict(sorted(combined.items()))})
    matches.sort(key=lambda m: (bool(m['source_nonreserved_overlaps']),
                                -m['near_reference_count'] / m['total_count'], m['id_hex'],
                                m['endian'], m['linear_start'], m['length'], m['scale_kwh']))
    return {'reference_kwh': reference, 'tolerance_kwh': tolerance,
            'reference_precision': 'approximate owner recollection, not a synchronized diagnostic sample',
            'status': 'numeric_hypotheses_only_not_validated_energy_signals',
            'limits': {'width_bits': [8, 16], 'signed': False, 'offset': 0, 'scales_kwh': SCALES,
                       'minimum_matching_fraction': min_fraction, 'minimum_matching_samples': 20,
                       'required_capture_count': len(captures)},
            'start_convention': 'BE: zero-based MSB-first wire position. LE: zero-based LSB-first bit position.',
            'inputs': [{k: c[k] for k in ['name', 'path', 'sha256']} for c in captures],
            'searched_ids': searched_ids, 'match_count': len(matches),
            'matches_without_nonreserved_source_overlap': sum(not m['source_nonreserved_overlaps'] for m in matches),
            'matches': matches}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, action='append', required=True)
    parser.add_argument('--source', type=Path, default=Path('dbc/opel_corsa_e_info.source.json'))
    parser.add_argument('--reference-kwh', type=float, required=True)
    parser.add_argument('--tolerance-kwh', type=float, default=.3)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert args.reference_kwh > 0 and args.tolerance_kwh >= 0
    report = search([read_capture(p) for p in args.capture],
                    json.loads(args.source.read_text(encoding='utf-8')),
                    args.reference_kwh, args.tolerance_kwh)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ['matches', 'inputs', 'searched_ids']}, indent=2))
    print('IDs searched:', len(report['searched_ids']))
    for match in report['matches'][:5]:
        print(json.dumps({k: v for k, v in match.items() if k != 'raw_histogram'}))


if __name__ == '__main__':
    main()
