"""Passive INFO capture search; numeric hypotheses are never promoted to signals."""
import collections
import json
import math
import struct
from pathlib import Path
from info_can_capture import read_capture
from search_info_energy import numeric_value

ROOT = Path(__file__).resolve().parents[1]
BASE_SCALES = [.0001, .00025, .0005, .001, .002, .004, .005, .008, .01,
               1/64, .016, .02, .025, 1/32, .032, .04, .05, .064, .1,
               .125, .2, .25, .3, .4, .5, 1]
SCALES = sorted(set(BASE_SCALES + [x*4/3 for x in BASE_SCALES]))

def histogram(rows, start, end):
    return collections.Counter(f['data'] for t, f in rows if start <= t < end)

def candidates(grouped):
    """Zero-offset 8..32-bit BE/LE. A growing match must include a real step.

    Last cloud sample is not known to be synchronized; tolerate 50 Wh. Compare
    the first stable awake window with the final refresh. Do not interpret the
    number of retransmissions as independent observations of growth.
    """
    growth, capacity, checked = [], [], 0
    for ident, rows in sorted(grouped.items()):
        early = histogram(rows, 5, 45); late = histogram(rows, 484, 500)
        if not early or not late: continue
        size = min(len(d) for d in early.keys() | late.keys())
        if not size: continue
        for endian in ('BE', 'LE'):
            for width in range(8, min(32, size*8)+1):
                for bit in range(size*8-width+1):
                    checked += 1
                    old = collections.Counter(); new = collections.Counter()
                    for source, target in ((early, old), (late, new)):
                        for data, n in source.items(): target[numeric_value(data, endian, bit, width)] += n
                    a, na = old.most_common(1)[0]; b, nb = new.most_common(1)[0]
                    if na/sum(old.values()) < .8 or nb/sum(new.values()) < .8: continue
                    for scale in SCALES:
                        descriptor = dict(id=f'{ident:03X}', endian=endian, linear_start=bit,
                            width=width, scale_kwh=scale, early_raw=a, late_raw=b,
                            early_kwh=a*scale, late_kwh=b*scale)
                        for target in (15.04, 15.04*4/3):
                            if abs(b*scale-target) <= .05 and .15 <= (b-a)*scale <= .65:
                                growth.append(dict(descriptor, reference_kwh=target))
                        for target in (32.29, 32.29*4/3):
                            if a == b and abs(b*scale-target) <= .05:
                                capacity.append(dict(descriptor, reference_kwh=target))
    return dict(checked_fields=checked, growth=growth, constant_capacity_numeric_matches=capacity)

def float_candidates(grouped):
    """Separate byte-aligned IEEE754 float32 check with the same windows."""
    matches = []
    for ident, rows in sorted(grouped.items()):
        early, late = histogram(rows, 5, 45), histogram(rows, 484, 500)
        if not early or not late: continue
        size = min(len(d) for d in early.keys() | late.keys())
        for endian, fmt in [('BE', '>f'), ('LE', '<f')]:
            for byte in range(size-3):
                values = []
                for payloads in (early, late):
                    counts = collections.Counter()
                    for data, n in payloads.items():
                        value = struct.unpack(fmt, data[byte:byte+4])[0]
                        if math.isfinite(value): counts[value] += n
                    if not counts: break
                    value, n = counts.most_common(1)[0]
                    if n/sum(payloads.values()) < .8: break
                    values.append(value)
                if len(values) != 2: continue
                for scale in (1, .001, 4/3, .001*4/3):
                    a,b = (v*scale for v in values)
                    growing = any(abs(b-target) <= .05 and .15 <= b-a <= .65
                                  for target in (15.04,15.04*4/3))
                    capacity = a == b and any(abs(b-target) <= .05
                                             for target in (32.29,32.29*4/3))
                    if growing or capacity:
                        matches.append(dict(id=f'{ident:03X}',endian=endian,byte=byte,
                            scale_kwh=scale,early_kwh=a,late_kwh=b,growing=growing))
    return matches

def charging_type_comparison(paths):
    """Retain known-driving contrast; mode0 must not become 'charging'."""
    result=[]
    for path in paths:
        capture=read_capture(path)
        samples=[f for f in capture['frames'] if f['id']==0x31A and len(f['data'])==8]
        counts=collections.Counter((f['data'][7]>>2)&1 for f in samples)
        result.append(dict(name=capture['name'],sha256=capture['sha256'],
                           samples=len(samples),charging_type_counts=dict(counts)))
    return result

def charging_hmi_comparison(paths):
    result=[]
    for path in paths:
        capture=read_capture(path)
        samples=[f for f in capture['frames'] if f['id']==0x396 and len(f['data'])==6]
        counts=collections.Counter((f['data'][0]>>2)&7 for f in samples)
        origin=capture['frames'][0]['raw_us']; previous=None; changes=[]
        for frame in samples:
            byte=frame['data'][0]
            if byte==previous: continue
            previous=byte
            changes.append(dict(time_s=round((frame['raw_us']-origin)/1e6,6),
                line=frame['line'],first_byte=f'{byte:02X}',hmi=(byte>>2)&7))
        result.append(dict(name=capture['name'],sha256=capture['sha256'],
            samples=len(samples),hmi_counts=dict(counts),first_byte_changes=changes))
    return result

def main():
    capture = read_capture(ROOT/'logs/info_charging.csv')
    assert not capture['timestamp_regressions']
    origin = capture['frames'][0]['raw_us']; grouped = collections.defaultdict(list)
    for f in capture['frames']: grouped[f['id']].append(((f['raw_us']-origin)/1e6, f))
    source = json.loads((ROOT/'dbc/opel_corsa_e_info.source.json').read_text(encoding='utf-8'))
    changes = {}
    for ident in (0x036,0x160,0x1E7,0x31A,0x39A,0x3DA,0x3E0,0x3E1,0x396,0x7A4):
        previous = None; events = []
        for t, f in grouped[ident]:
            if f['data'] == previous: continue
            previous = f['data']; events.append(dict(time_s=round(t,6), line=f['line'], data=previous.hex(' ').upper()))
        changes[f'{ident:03X}'] = events
    scan = candidates(grouped)
    report = dict(date='2026-11-03', physical_can_io=False,
        input={k:v for k,v in capture.items() if k not in ('frames','path')},
        frame_count=len(capture['frames']), ids=len(grouped),
        owner_evidence=dict(charging_continuously_before_deferred=True,power_kw_approx=2.5,
            charged_kwh_approx=.4,energy_updates_stepwise_on_refresh=True,
            screenshot_capacity_ha_kwh=32.29,screenshot_residual_ha_kwh=15.04,
            owner_correction_factor=4/3,corrected_capacity_kwh=32.29*4/3,
            corrected_residual_kwh=15.04*4/3,screenshot_exact_time_unknown=True),
        scopes=dict(width_bits=[8,32],scales_kwh=SCALES,offset=0,unsigned=True,
            early_window_s=[5,45],late_window_s=[484,500],minimum_support=.8,
            final_tolerance_kwh=.05,growth_bounds_kwh=[.15,.65]),
        search=scan,float32_matches=float_candidates(grouped),changes=changes,
        charging_type_comparison=charging_type_comparison([
            ROOT/'logs'/name for name in ('info_charging.csv','info_start_jizda.csv',
                'info_standby.csv','info_precond.csv','info_start_odlozeni.csv',
                'info_nabijeni_8soc.csv')]),
        charging_hmi_comparison=charging_hmi_comparison([
            ROOT/'logs'/name for name in ('info_charging.csv','info_start_odlozeni.csv',
                'info_start_jizda.csv','info_standby.csv','info_precond.csv',
                'info_soc57_refresh.csv','info_apka_refresh.csv','info_buzeni_obc.csv',
                'info_apka.csv','infocan.csv','info_nabijeni_8soc.csv',
                'info_nabijeni_8soc_2.csv','info_nabijeni_8soc_3.csv')]),
        derived_reference=dict(input_energy_over_whole_capture_kwh=2.5*capture['duration_s']/3600,
            note='Rough AC input, not measured net battery gain; screenshot time and initial cache age are unknown.'),
        inventory=[dict(id=f'{i:03X}',source_name=source.get(f'{i:03X}',{}).get('name'),
            count=len(rows),unique_payloads=len({f['data'] for t,f in rows}),
            first=rows[0][1]['data'].hex(' ').upper(),last=rows[-1][1]['data'].hex(' ').upper())
            for i,rows in sorted(grouped.items())],
        limitations=['Screenshots and CAN samples are not synchronized.',
            'A constant numeric coincidence is not a capacity decoder.',
            'Window scan may miss intermediate/multiplexed updates, signed values, non-byte-aligned floats, offsets or other scales.',
            'RX-only GVRET does not necessarily record the gateway own transmitted refresh requests.',
            'No charging state is inferred merely from schedule, cable lamp or trip power gauge.'])
    (ROOT/'docs/info-charging-analysis.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(frames=len(capture['frames']),duration_s=capture['duration_s'],ids=len(grouped),
        growth_matches=len(scan['growth']),capacity_coincidences=len(scan['constant_capacity_numeric_matches']),
        growth=scan['growth'],float32_matches=report['float32_matches']),indent=2))

if __name__ == '__main__': main()
