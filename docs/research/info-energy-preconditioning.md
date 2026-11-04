# Remaining battery energy after preconditioning

> Capture evidence recorded at the time of this analysis. For current firmware behavior and request status, use [CAN requests](../can-requests.md) and the [documentation index](../README.md).

Analysis date: 2026-11-03. Passive files only; no adapter/socket access, CAN
transmission, firmware changes or new DBC signal assignments.

**No convincing INFO CAN remaining-energy field was identified.** The owner's
cloud reading changed from approximately 3 kWh to **19.88 kWh** after starting
preconditioning, while both captures report **57% SOC**. This establishes a
cloud observation, not its vehicle source or an exact CAN/cloud timestamp.

## Inputs and comparisons

| Capture | Frames | IDs | Duration | SOC |
| --- | ---: | ---: | ---: | ---: |
| `info_soc57_refresh.csv` | 12,355 | 98 | 65.829567 s | 57% |
| `info_precond.csv` | 36,565 | 100 | 149.039358 s | 57% |

Hashes and complete payload differences are in the
[machine-readable report](../info-energy-preconditioning.json). The preconditioning
Start request is at 22.388686 s, BSI ASAP acknowledgement at 22.804201 s, and
active feedback at 28.804328 s. Time is relative to the first frame of that file.

The second capture has three IDs absent from the first: `11B`, `31B` and `4A4`.
The first two are the captured preconditioning command pair. The pinned PSA-RE
source describes `4A4` as a telematics fault event; its observed burst runs from
7.527515 to 11.227281 s, **before Start**. The first capture alone has `4BC`,
source-labelled diagnostic-socket fault events. New IDs therefore provide no
identified energy response.

## Relevant payloads

| ID | Earlier 57% capture | Preconditioning capture | Assessment |
| --- | --- | --- | --- |
| `3E0` | `39 00 00 00 00 00 00 00` | Same | HV SOC remains 57%, 45 and 135 samples respectively |
| `31A` | Mostly `1E 00 00 00 3F E0 05 A0` | Mostly `1D 80 00 00 3F E0 05 A0` | Verified range conversion gives 120 to 118 km; new range already present at 2.450613 s, before Start |
| `39A` | `59 55 17 55 60 00 00 00` | Same | Consumption/history payload, no change at Start |
| `3A8` | `FC FC FC 00 00 00 00 00` | Same | INFO trip-duration layout; do not apply another bus's layout by ID alone |
| `3DA` | Includes `0E 31 8E 38 FF E0` | Same dominant payload | No energy refresh transition; a numerical match in trailing source-unlabelled bits is insufficient |
| `396` | Tail `00 58 E8` | Same tail | Leading fields track auxiliary 12 V battery; no evidence that tail is HV energy |

### Unknown `7A4`

This is the one source-unknown ID in the compared files and was checked
separately at payload level:

| Capture | Nonzero data first seen | Payload | Final byte becomes `04` |
| --- | ---: | --- | ---: |
| Earlier 57% | 35.782828 s | `00 00 9D 7C 02 00 00 00` | 36.782809 s |
| Preconditioning | 25.973140 s | `00 00 4B C9 01 00 00 00` | 26.972996 s |

The latter is 3.584454 s after Start, so the timing initially looks interesting.
However, the earlier capture has the same zero -> nonzero -> final-byte change
without a preconditioning command. At 8% SOC, three other captures have markedly
different bodies: `C6 E1 02`, `35 54 02`, and `58 97 01`. At 70% SOC, other logs
also have different bodies and sometimes ASCII-like payloads. These observations
do not establish a fixed direct encoding of remaining energy. The source,
multiplexing and meaning remain unknown; keep raw bytes in the DBC.

## Bounded numeric search

The search checks unsigned contiguous **8..32-bit** fields at every start bit,
both big- and little-endian, zero offset, using the 28 scales listed in the JSON.
These include 0.001 kWh/Wh, 0.01 kWh, **1/64 kWh**, **0.032 kWh/32 Wh**, and
0.035 kWh. A transition must match approximately **3 +/- 0.5 kWh** before and
**19.88 +/- 0.06 kWh** after, on at least 80% of each group's samples.

| Comparison | Shared IDs | Field positions/endian/width combinations | Transition matches |
| --- | ---: | ---: | ---: |
| Earlier 57% file -> preconditioning file | 97 | 166,170 | 0 |
| Preconditioning before Start -> 30..80 s | 88 | 146,720 | 0 |
| Three pooled 8% charging captures -> preconditioning file | 85 | 146,562 | 0 |

The 8% comparison is a secondary hypothesis check: no synchronized energy
measurement is supplied for those files, so approximately 3 kWh there is not
claimed as measured fact. Pooling does not prove consistency in each file.

A separate 8..16-bit search for **19.88 kWh already present in both 57% files**
produces 399 numeric hypotheses on 32 IDs, mostly inside source-labelled
schedules, counters, flags or consumption fields. Seven hypotheses avoid
nonreserved source fields: six overlapping variants in the same `396` tail and
one in the `3DA` tail. Neither the timing nor the low-SOC contrast validates
these as energy. This search uses the older tool's 15 scales, listed separately
in the JSON. It demonstrates why a plausible constant is not a decoder.

These bounds do not exclude transient/multiplexed or noncontiguous fields,
signed values, floating-point data, different units/offsets, or traffic absent
from the captures. Stale cloud energy also need not equal the current energy
already present on CAN. A negative transition search cannot prove absence.

## HSCAN and diagnostic alternative

The local Battery-Emulator CMP decoder has a remaining-energy candidate on
HSCAN1 `543`:

```c
remaining_energy_Wh = ((data[0] << 3) | (data[1] >> 5)) * 32;
```

Source: local CMP decoder (`CMP-SMART-CAR-BATTERY.cpp`, separate local comparison checkout).
The Android dashboard's UDS profile (`android_dash/EcmpProfile.kt`, separate local comparison checkout)
reads DID `D865` as `raw / 64` kWh. Numerically, raw 1272 gives 19.875 kWh,
which rounds to 19.88; this is compatible with the cloud's precision but does
not demonstrate that the cloud uses D865. Do not infer a CAN field from rounding.

Neither `543`, `5C3` nor alternative HS3 `22F` occurs in either INFO capture.
Telematics obtaining fresh energy from another bus or diagnostics remains
plausible, but the INFO files do not show which path it uses. They also do not
distinguish a direct energy report from a cloud-derived estimate.

The most discriminating next measurement is simultaneous passive INFO and
HSCAN1 recording around one preconditioning Start, with the exact cloud reading
and its observation time. Inspect `543` and compare with a diagnostic D865
reading if available. A second SOC/energy operating point would reject static
coincidences. Do not substitute SOC multiplied by nominal capacity for an
independent energy measurement.

## Reproduce

```powershell
python tools/analyze_info_energy_transition.py
python -m unittest discover -s tests -p test_info_energy_transition.py -v
```

Five focused tests verify recovery of planted 11-bit/32 Wh, 16-bit/div64 and
little-endian Wh fields, rejection of a static false transition, and explicit
sample counts for rare events. No firmware rebuild is needed for this analysis.
