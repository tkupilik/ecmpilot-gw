"""Reproduce the passive OBC plug-in wake/capture comparison; no vehicle IO."""
import collections
import hashlib
import json
from pathlib import Path

import cantools
from info_can_capture import read_capture, summarize

ROOT = Path(__file__).resolve().parents[1]
NAMES = ['info_buzeni_obc.csv', 'info_standby.csv', 'info_start_jizda.csv',
         'infocan.csv', 'info_apka.csv']
CLUSTER = {0x1D7, 0x217, 0x257, 0x2D7, 0x2E7, 0x317, 0x31C, 0x51F, 0x5DF}


def analyze():
    captures = {name: read_capture(ROOT / 'logs' / name) for name in NAMES}
    ids = {name: {f['id'] for f in cap['frames']} for name, cap in captures.items()}
    wake = captures[NAMES[0]]
    start = wake['frames'][0]['raw_us']
    db = cantools.database.load_file(ROOT / 'dbc/opel_corsa_e_info.dbc', encoding='utf-8', strict=True)
    source = json.loads((ROOT / 'dbc/opel_corsa_e_info.source.json').read_text(encoding='utf-8'))
    stats = summarize([wake])
    old_ids = ids['infocan.csv'] | ids['info_apka.csv'] | ids['info_start_jizda.csv']
    added = ids[NAMES[0]] - ids[NAMES[1]]
    compared = {}
    for name in NAMES[1:3]:
        compared[name] = dict(added_ids=[f'{i:03X}' for i in sorted(ids[NAMES[0]] - ids[name])],
                              absent_ids=[f'{i:03X}' for i in sorted(ids[name] - ids[NAMES[0]])])

    def event(frame):
        return dict(line=frame['line'], raw_us=frame['raw_us'],
                    relative_ms=round((frame['raw_us']-start)/1000, 3),
                    id=f"{frame['id']:03X}", payload=frame['data'].hex(' ').upper())

    wake_frames = [event(f) for f in wake['frames'] if f['id'] == 0x412]
    phases, previous = [], None
    for frame in wake['frames']:
        if frame['id'] != 0x036:
            continue
        phase = frame['data'][4] & 7
        if phase != previous:
            phases.append(dict(**event(frame), power_management=phase))
        previous = phase
    sequence = [dict(**e, meaning='BSI wake cause, byte1=0x20: OBC cause / AC insertion') for e in wake_frames]
    sequence += [dict(**e, meaning=f"BSI PowerManagement={e['power_management']}") for e in phases]
    for ident, meaning in [(0x5D2, 'BSI version-layout identification, not an OBC transaction ACK'),
                           (0x317, 'First cluster configuration'),
                           (0x217, 'First cluster odometer status'),
                           (0x5DF, 'Original cluster version')]:
        sequence.append(dict(**event(next(f for f in wake['frames'] if f['id'] == ident)), meaning=meaning))
    sequence.sort(key=lambda e: e['line'])
    rows = []
    for ident in sorted(added):
        frames = [f for f in wake['frames'] if f['id'] == ident]
        message = db.get_message_by_frame_id(ident)
        upstream = source.get(f'{ident:03X}', {})
        rows.append(dict(id=f'{ident:03X}', name=message.name, dbc_senders=message.senders,
                         upstream_senders=upstream.get('senders', []),
                         cluster_attribution=ident in CLUSTER,
                         older_captures=[name for name in ['infocan.csv', 'info_apka.csv', 'info_start_jizda.csv']
                                         if ident in ids[name]],
                         count=len(frames), dlcs=stats[ident]['dlcs'],
                         first_ms=event(frames[0])['relative_ms'], last_ms=event(frames[-1])['relative_ms'],
                         median_period_ms=stats[ident]['median_period_ms'],
                         first_payload=frames[0]['data'].hex(' ').upper()))
    return dict(date='2026-11-01',
                reference='Times relative to first captured 0x412, not physical plug-in time; files never share a clock',
                dbc_sha256=hashlib.sha256((ROOT/'dbc/opel_corsa_e_info.dbc').read_bytes()).hexdigest(),
                captures={name: dict(sha256=cap['sha256'], frames=len(cap['frames']), ids=len(ids[name]),
                                    duration_s=cap['duration_s'], timestamp_regressions=cap['timestamp_regressions'])
                          for name, cap in captures.items()},
                comparisons=compared,
                new_vs_all_older=[f'{i:03X}' for i in sorted(ids[NAMES[0]]-old_ids)],
                cluster_ids=[f'{i:03X}' for i in sorted(CLUSTER)], added_id_details=rows,
                sequence=sequence, bsi_phase_changes=phases,
                wake_frames=wake_frames,
                wake_repetition_intervals_ms=[round((b['raw_us']-a['raw_us'])/1000, 3)
                                               for a, b in zip(wake_frames, wake_frames[1:])],
                wake_to_normal_ms=next(p['relative_ms'] for p in phases if p['power_management']==1),
                bsi_phase3_to_phase1_ms=round((phases[1]['raw_us']-phases[0]['raw_us'])/1000, 3),
                upstream_wake_request_ids_observed=[f'{i:03X}' for i in [0x401,0x420,0x424,0x43C]
                                                   if i in ids[NAMES[0]]],
                direct_obc_request_observed=False, explicit_transaction_ack_observed=False,
                vehicle_io=False)


def markdown(report):
    lines = ['# AC cable / OBC wake and original cluster traffic', '',
             'Owner event: AC cable inserted into the Corsa-e. Capture: `info_buzeni_obc.csv`.',
             'Bus: INFO, 125 kbit/s. Unit004/original instrument cluster was connected.', '',
             '## Observed wake sequence', '',
             'Time zero is the first captured frame, not the measured instant of cable insertion.',
             'All times below use this file\'s own microsecond clock; no files are time-aligned.', '',
             '| CSV line | Time (ms) | CAN ID | Payload | Interpretation |',
             '| ---: | ---: | --- | --- | --- |']
    for e in report['sequence']:
        lines.append(f"| {e['line']} | {e['relative_ms']:.3f} | `{e['id']}` | `{e['payload']}` | {e['meaning']} |")
    lines += ['',
        'BSI announces `00 20 00 00 00 00 00 00` five times over 39.995 ms, roughly 10 ms apart.',
        'PowerManagement changes from Wake-up (3) at 20.074 ms to Normal (1) at 54.750 ms:',
        'a 34.676 ms transition. It remains Normal through the last observed 036 at 34.120 s.', '',
        '**OBC cause:** the observed full value `BsiWakeupInfo0=0x20` is labelled',
        '`OBC cause (AC cable insertion)` in the DBC. This is the owner-labelled event',
        'correlation, not an independently isolated cause-bit mapping. The original',
        '8-bit field remains intact; `0x30` in the earlier `infocan.csv` remains unlabelled.', '',
        'The first captured frame is already a BSI announcement. No preceding direct',
        'OBC request or 401/420/424/43C announcement is captured. Thus a request/reply',
        'ping-pong cannot be reconstructed on INFO. The 412 burst and 036 transition',
        'are BSI\'s observable announcement/acceptance of waking, not a matched',
        'application transaction ACK. The 5D2 payload uses the upstream version/identity',
        'layout despite its misleading HybridControlRequests title; do not count it as',
        'an OBC ACK. The physical OBC-to-BSI wake path is not identified by this file.', '',
        '## Added identifiers and capture context', '',
        '| Reference capture | Reference IDs | Added in OBC capture | Absent from OBC capture |',
        '| --- | ---: | --- | --- |']
    for name, diff in report['comparisons'].items():
        lines.append(f"| `{name}` | {report['captures'][name]['ids']} | {len(diff['added_ids'])}: " +
                     ', '.join(diff['added_ids']) + ' | ' + ', '.join(diff['absent_ids']) + ' |')
    lines += ['',
        'Both standby and fully operational driving captures yield exactly the same 17 added IDs.',
        'Comparing against the union of all three older files (including `infocan.csv`',
        'and `info_apka.csv`) leaves 10 genuinely newly observed IDs: the nine cluster',
        'messages below, plus 4A4. All 17 already had source definitions in this DBC;',
        'none requires inventing a new raw-only identifier.', '',
        '| ID | Current DBC name | Count | Median period (ms) | Seen in older files | Attribution |',
        '| --- | --- | ---: | ---: | --- | --- |']
    for row in report['added_id_details']:
        period = row['median_period_ms']
        attribution = 'IC / unit004 (owner-approved)' if row['cluster_attribution'] else 'Existing source retained; not assigned to IC'
        lines.append(f"| `{row['id']}` | `{row['name']}` | {row['count']} | " +
                     (f'{period:.3f}' if period is not None else 'one frame') + ' | ' +
                     (', '.join(row['older_captures']) or 'none') + ' | ' + attribution + ' |')
    lines += ['',
        'The nine IC messages use transmitter `IC` and prefix `IC_`, with unit004 and',
        'capture-comparison provenance in comments/inventory. Original sender aliases',
        'and layouts remain in source JSON; IC attribution does not validate signal meanings.',
        '4A4 is still ambiguous (its source includes CMB among alternatives) and is not',
        'assigned to IC. Seven of the 17 IDs also appeared in earlier wake/start captures:',
        '01C,412,5C1,5D2,5E4,5FB,5FC. They cannot be attributed to the newly installed',
        'cluster merely because they are absent from a driving/standby excerpt.', '',
        '## Reproduce', '',
        'Run `python tools/analyze_obc_wake.py` from the project root.',
        'The adjacent JSON contains input SHA-256 hashes, counts, precise events and comparisons.',
        'Original CSVs are read-only. No firmware, adapter or vehicle operation is performed.', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    report = analyze()
    base = ROOT/'docs/info-obc-wake-2026-11-01'
    base.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    base.with_suffix('.md').write_text(markdown(report), encoding='utf-8')
    print(json.dumps(dict(added_vs_standby=report['comparisons'][NAMES[1]]['added_ids'],
                         new_vs_all_older=report['new_vs_all_older'],
                         cluster_ids=report['cluster_ids'],
                         phase3_to_phase1_ms=report['bsi_phase3_to_phase1_ms']), indent=2))
