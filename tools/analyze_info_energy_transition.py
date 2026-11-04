"""Offline energy contrast search. Numeric matches are not decoded signals.

No CAN I/O. Compare owner-reported cloud values with independent captures;
the cloud readings are not assumed to be synchronized with CAN timestamps.
"""
import collections
import json
from pathlib import Path

from generate_hscan1_dbc import positions
from info_can_capture import read_capture
from search_info_energy import numeric_value, SCALES, search as absolute_search

ROOT = Path(__file__).resolve().parents[1]
SCALES_KWH = sorted(set(SCALES + [.0001, .0005, .002, .004, .008, 1 / 128, 1 / 256,
                                .035, .0625, .064, .128, .2, .256]))


def group(capture, interval=None):
    grouped = collections.defaultdict(collections.Counter)
    origin = capture['frames'][0]['raw_us']
    for frame in capture['frames']:
        t = (frame['raw_us'] - origin) / 1e6
        if interval is None or interval[0] <= t < interval[1]:
            grouped[frame['id']][frame['data']] += 1
    return grouped


def payloads(histogram):
    return [dict(data=data.hex(' ').upper(), count=count)
            for data, count in histogram.most_common()]


def search(old, new, source):
    """Enumerate contiguous unsigned 8..32-bit BE/LE, zero-offset fields.

    Keep even rare matches, reporting support rather than hiding event IDs.
    A reference match needs 80% of the samples in each group; a single
    sample is not confirmation. An ID must be present in both groups.
    """
    matches, checked = [], 0
    for ident in sorted(old.keys() & new.keys()):
        length = min(map(len, [*old[ident], *new[ident]]))
        for endian in ['BE', 'LE']:
            for width in range(8, min(32, length * 8) + 1):
                for start in range(length * 8 - width + 1):
                    checked += 1
                    histograms = []
                    for samples in [old[ident], new[ident]]:
                        hist = collections.Counter()
                        for data, count in samples.items():
                            hist[numeric_value(data, endian, start, width)] += count
                        histograms.append(hist)
                    for scale in SCALES_KWH:
                        fractions = [sum(n for raw, n in h.items() if abs(raw * scale - ref) <= tolerance)
                                     / sum(h.values()) for h, ref, tolerance in
                                     zip(histograms, [3.0, 19.88], [.5, .06])]
                        if min(fractions) < .8:
                            continue
                        span = (set(range(start, start + width)) if endian == 'BE' else
                                {bit // 8 * 8 + 7 - bit % 8 for bit in range(start, start + width)})
                        definition = source.get(f'{ident:03X}', {})
                        overlaps = []
                        for name, spec in definition.get('signals', {}).items():
                            first, last, _ = positions(spec['bits'])
                            if not spec.get('unused') and span & set(range(first, last + 1)):
                                overlaps.append(name)
                        matches.append(dict(id=f'{ident:03X}', endian=endian, start=start,
                                            width=width, scale_kwh=scale, fractions=fractions,
                                            counts=[sum(h.values()) for h in histograms],
                                            modes_kwh=[h.most_common(1)[0][0] * scale for h in histograms],
                                            source_name=definition.get('name'), overlaps=overlaps))
    return dict(shared_ids=len(old.keys() & new.keys()), checked_fields=checked,
                matches=matches, match_count=len(matches))


def main():
    captures = [read_capture(ROOT / 'logs' / name) for name in
                ['info_soc57_refresh.csv', 'info_precond.csv']]
    assert not any(c['timestamp_regressions'] for c in captures)
    source = json.loads((ROOT / 'dbc/opel_corsa_e_info.source.json').read_text(encoding='utf-8'))
    old, new = map(group, captures)
    before, after = group(captures[1], (0, 22.388686)), group(captures[1], (30, 80))
    low_captures = [read_capture(ROOT / 'logs' / (name + '.csv')) for name in
                    ['info_nabijeni_8soc', 'info_nabijeni_8soc_2', 'info_nabijeni_8soc_3']]
    low = collections.defaultdict(collections.Counter)
    for c in low_captures:
        for ident, histogram in group(c).items():
            low[ident].update(histogram)
    unknown_context = []
    for c in low_captures + [read_capture(ROOT / 'logs' / name) for name in
                             ['infocan.csv', 'info_apka.csv', 'info_start_jizda.csv']]:
        unknown_context.append(dict(capture=c['name'], sha256=c['sha256'],
            soc_raw=dict(collections.Counter(f['data'][0] for f in c['frames'] if f['id'] == 0x3E0)),
            payloads=payloads(collections.Counter(f['data'] for f in c['frames'] if f['id'] == 0x7A4))))
    absolute = absolute_search(captures, source, 19.88, .06, .8)
    changed = []
    for ident in sorted(old.keys() & new.keys()):
        if set(old[ident]) != set(new[ident]):
            changed.append(dict(id=f'{ident:03X}', source_name=source.get(f'{ident:03X}', {}).get('name'),
                                old=payloads(old[ident]), new=payloads(new[ident])))
    watch = [0x7A4, 0x3E0, 0x31A, 0x39A, 0x3A8, 0x3DA, 0x4A4, 0x4BC]
    watched = []
    for c, grouped in zip(captures, [old, new]):
        origin, last, events = c['frames'][0]['raw_us'], {}, []
        for f in c['frames']:
            if f['id'] not in watch or last.get(f['id']) == f['data']:
                continue
            last[f['id']] = f['data']
            events.append(dict(id=f"{f['id']:03X}", line=f['line'],
                               time_s=(f['raw_us'] - origin) / 1e6, data=f['data'].hex(' ').upper()))
        watched.append(dict(capture=c['name'], changes=events,
                            histograms={f'{i:03X}': payloads(grouped[i]) for i in watch if i in grouped}))
    report = dict(date='2026-11-03', physical_can_io=False,
                  cloud_reference=dict(old_kwh_approx=3, new_kwh=19.88,
                                       synchronized_to_capture=False, new_observed_after='preconditioning start'),
                  scope=dict(width_bits=[8, 32], unsigned=True, offset=0, scales_kwh=SCALES_KWH,
                             old_tolerance_kwh=.5, new_tolerance_kwh=.06, minimum_fraction=.8,
                             minimum_samples_per_id=1,
                             start_convention='BE MSB-first; LE LSB-first; zero-based linear position'),
                  inputs=[dict(name=c['name'], sha256=c['sha256'], frames=len(c['frames']),
                               duration_s=c['duration_s'], ids=len(g)) for c, g in zip(captures, [old, new])],
                  old_only_ids=[f'{i:03X}' for i in sorted(old.keys() - new.keys())],
                  new_only_ids=[f'{i:03X}' for i in sorted(new.keys() - old.keys())],
                  hscan_ids_present={f'{i:03X}': [sum(n for n in g.get(i, {}).values()) for g in [old, new]]
                                    for i in [0x543, 0x5C3, 0x22F]},
                  across_captures=search(old, new, source),
                  within_preconditioning=search(before, after, source),
                  low_soc_inputs=[dict(name=c['name'], sha256=c['sha256'], frames=len(c['frames']),
                                       soc_raw=dict(collections.Counter(f['data'][0] for f in c['frames']
                                                                       if f['id'] == 0x3E0))) for c in low_captures],
                  low_soc_combined_to_preconditioning=search(low, new, source),
                  absolute_reference_search=dict(width_bits=[8, 16], scales_kwh=SCALES,
                      match_count=absolute['match_count'],
                      ids=sorted({m['id_hex'] for m in absolute['matches']}),
                      matches_without_nonreserved_overlap=[m for m in absolute['matches']
                                                           if not m['source_nonreserved_overlaps']],
                      interpretation='Numeric coincidences, not energy validation; neither capture contrast nor low-SOC contrast supports them.'),
                  changed_payloads=changed, watched=watched,
                  unknown_7a4_other_captures=unknown_context,
                  limitations=['Cloud age/source is not independently known.',
                               'No simultaneous HSCAN or UDS recording.',
                               'Scan excludes signed/noncontiguous/multiplex-reassembled fields and other scales/offsets.',
                               '80% support can miss transient or alternating/multiplexed fields.',
                               'The 8% captures have no synchronized energy reference; 3 +/- 0.5 kWh there is only a hypothesis.',
                               'Numeric coincidence is not evidence of an energy field.'])
    output = ROOT / 'docs/info-energy-preconditioning.json'
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in
                      ['changed_payloads', 'watched', 'unknown_7a4_other_captures']}, indent=2))


if __name__ == '__main__':
    main()
