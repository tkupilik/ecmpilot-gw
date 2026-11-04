"""Passive Start/Stop capture report; no adapter, socket or firmware access."""
import json
from pathlib import Path
from collections import Counter
from info_can_capture import read_capture

ROOT = Path(__file__).resolve().parents[1]


def analyze():
    capture = read_capture(ROOT / 'logs/info_precond.csv')
    assert not capture['timestamp_regressions']
    frames = capture['frames']; origin = frames[0]['raw_us']

    def event(f):
        return dict(line=f['line'], relative_s=(f['raw_us']-origin)/1e6,
                    raw_us=f['raw_us'], id=f"{f['id']:03X}", payload=f['data'].hex(' ').upper())

    states = []
    previous = None
    for f in frames:
        if f['id'] != 0x2E0: continue
        byte = f['data'][0]
        if byte == previous: continue
        states.append(dict(**event(f), available=byte >> 7, asap=(byte >> 2) & 1,
                           feedback=(byte >> 3) & 15))
        previous = byte
    requests = []
    for f in frames:
        if f['id'] != 0x31B: continue
        following = [q for q in frames if q['raw_us'] >= f['raw_us']]
        aux = next(q for q in following if q['id'] == 0x11B)
        target = (f['data'][0] >> 6) & 1
        ack = next(q for q in following if q['id'] == 0x2E0 and ((q['data'][0] >> 2) & 1) == target)
        complete = next(q for q in following if q['id'] == 0x2E0 and
                        ((q['data'][0] >> 2) & 1) == target and ((q['data'][0] >> 3) & 15) == target)
        requests.append(dict(action='start' if target else 'stop', request=event(f), aux=event(aux),
                             aux_delay_ms=(aux['raw_us']-f['raw_us'])/1000,
                             ack=event(ack), ack_delay_ms=(ack['raw_us']-f['raw_us'])/1000,
                             complete=event(complete), complete_delay_ms=(complete['raw_us']-f['raw_us'])/1000))
    return dict(date='2026-11-03', capture=capture['name'], sha256=capture['sha256'],
                frames=len(frames), ids=len({f['id'] for f in frames}), duration_s=capture['duration_s'],
                requests=requests, status_changes=states,
                program_bytes_unchanged=len({f['data'][1:] for f in frames if f['id'] == 0x2E0}) == 1,
                refresh394_count=sum(f['id'] == 0x394 for f in frames),
                hold3a9_payloads=[dict(payload=d.hex(' ').upper(), count=n) for d,n in
                    Counter(f['data'] for f in frames if f['id'] == 0x3A9).items()],
                outside0f6_raw=[dict(raw=d, count=n, celsius=d*.5-40 if d <= 250 else None) for d,n in
                    Counter(f['data'][6] for f in frames if f['id'] == 0x0F6).items()],
                cabin_temperature_verified=False, physical_hvac_operation_verified=False,
                owner_observation='Owner confirms application acknowledgement only; initial wake came from our app.',
                physical_can_io=False)


if __name__ == '__main__':
    report = analyze()
    (ROOT/'docs/preconditioning-capture.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
