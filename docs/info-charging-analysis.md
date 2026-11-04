# INFO CAN charging capture — 2026-11-03

The Android **Unavailable** label came from missing INFO producers of
`charge.state` and `charge.connected`, not a failed range/SOC conversion. The
owner's `396/Unused1` observation led to a corroborated **reported charging HMI**
field. It now feeds the existing UDP/HA state contract through varstore and the
vehicle task. Charging mode/time remain separate. Charging power, ETA and HV
energy remain unknown; no percentage/ETA is converted into invented watts.

Input: [info_charging.csv](../logs/info_charging.csv), 95,796 frames, 99 IDs,
495.768779 seconds. The owner confirms continuous AC charging before the final
delayed-mode command, at approximately 2.5 kW. At that power the whole recording
represents about **0.344 kWh AC input**, consistent with the approximate 0.4 kWh
observation. This is not a measurement of net energy stored in the battery.
The owner reports that BSI battery values update only following refresh.

[Machine-readable evidence](info-charging-analysis.json) contains the input
SHA-256, payload change times/CSV lines, cross-capture mode counts, complete
inventory, search bounds and numerical hypotheses. All work is passive; no
vehicle commands were sent.

## ChargingType is mode, not actual power flow

`31A` byte8 bit2 (zero-based `data[7] & 0x04`) is the source
`CHARGING_TYPE`: **0 immediate, 1 postponed**. It is already stored in
`bsi.trip.charge_mode` as a secondary indication; authoritative schedule
readback comes from `160`.

| Capture | ChargingType=0 samples | ChargingType=1 samples |
| --- | ---: | ---: |
| info_charging.csv | 361 | 15 |
| info_start_jizda.csv, driving capture | 36 | 0 |
| info_standby.csv | 53 | 0 |
| info_precond.csv | 135 | 0 |
| info_start_odlozeni.csv | 51 | 69 |
| info_nabijeni_8soc.csv | 83 | 0 |

The same zero occurs during driving. It cannot mean “currently charging”. A
stored delayed mode likewise does not prove a cable is connected.

Final changes in the new log, relative to the first frame:

| Time (s) | CAN ID | Change | Interpretation |
| ---: | --- | --- | --- |
| 480.753583 | 1E7 | power gauge raw75 →100 | Gauge moves from -12.5% to0%; not independently a charging acknowledgement. |
| 480.965441 | 160 | `80 B4` → `C0 B4` | Immediate → postponed; stored start remains180 minutes, **03:00**. |
| 481.003092 | 3E0 | byte1 `39` → `3A` | HV SOC **57 →58%**. |
| 481.113326 | 31A | last byte `A0` → `A4` | Matching secondary postponed-mode indication. |
| 482.115386 | 3DA | final bytes `28 00` → `1D 60` | Unverified extended tail changes; no energy or ETA decoder established. |

These updates are consistent with a refresh around the final operation, but
their ordering does not prove which request caused each update. No `394`/`35B`
RX appears in this file. The GVRET stream is RX-only and need not contain the
gateway's own transmitted requests; absence is not proof that no refresh ran.

## Reported charging HMI in396

The reserved INFO field contains a strong candidate in **byte1 bits4..2**:

```c
hmi = (data[0] >> 2) & 7;
```

This is a3-bit field, not just a boolean bit3. Cross-capture results:

| Capture / condition | HMI0 | HMI1 | HMI3 |
| --- | ---: | ---: | ---: |
| info_charging.csv, confirmed continuous charge then delay | 1 | 362 | 13 |
| info_start_odlozeni.csv, refresh/start then delay | 31 | 23 | 65 |
| info_apka_refresh.csv | 10 | 156 | 0 |
| info_buzeni_obc.csv, cable insertion | 31 | 3 | 0 |
| info_start_jizda.csv, driving | 36 | 0 | 0 |
| info_standby.csv | 52 | 0 | 0 |
| info_precond.csv | 135 | 0 | 0 |
| info_soc57_refresh.csv | 45 | 0 | 0 |
| Three older info_nabijeni_8soc captures, combined | 149 | 0 | 0 |

`info_charging.csv` first byte is00 at0.940s,04 at1.940s and0C at483.603s.
`info_start_odlozeni.csv` repeats00→04→0C at1.830/32.830/55.830s. Driving uses
01/03, whose changing low alert bits are outside this field. The complete
per-capture counts, transition lines and hashes are in the JSON.

The pinned PSA-RE archive also defines **exactly these bits** as
[`CHARGING_HMI_STATE` in HS3/396](https://github.com/prototux/PSA-RE/blob/67f7581e7622410efdbb29c7eda3b86982b9b450/buses/AEE2010.full/HS.HS3/396.yml).
This is corroboration, not permission to import the complete HS3 frame: HS3's
source DLC is8, whereas the vehicle's INFO frame is6. The scoped INFO overlay
replaces only the reviewed reserved span, preserves all other bits/metadata and
keeps the original INFO source JSON unchanged.

| Raw HMI | Source meaning | Normalized `charge.state` | Evidence on this vehicle |
| ---: | --- | --- | --- |
| 0 | Disconnected | 0 disconnected | Driving/standby/preconditioning contrast; initial/default or cached samples can disagree with actual charging. |
| 1 | In progress | 2 charging | Owner-confirmed charging capture; repeated start/delay contrast. |
| 2 | Failure detected | 4 fault | Source enum only; not observed. |
| 3 | Stopped | 1 waiting | Two captures after postponed-mode requests; the separate mode marks this as delayed charging. |
| 4 | Finished | 3 complete | Source enum only; not observed. |
| 5..7 | Unused | invalid/unknown | Never treat unknown codes as disconnected. |

`bsi.charging_hmi_state` retains the source enum, including unknown codes for
debugging. RX stores only that source reading. The separate application tick
owns normalization to `charge.state` and `charge.connected` and preserves the
source observation time; repeated ticks do not refresh cached data. The latter
is a **derived HMI connection indication** (0 false,1..4 true), not an independent
physical cable sensor. Force on the source can exercise the whole phone/HA path;
releasing it restores the real source data and age. INFO manual writes to the
derived signals are disabled to retain one producer.

The older charging-named8% captures still report0. Their exact physical charging
and refresh timing is not independently established here. Together with the
owner's observation about BSI caching, this prevents claiming HMI0 always proves
physical cable removal. The UI displays the **reported** BSI state; reconnecting
does not wake/refresh the vehicle. States2/4 and unplugged transitions still
need physical confirmation. No change to vehicle command sequencing is made.

## Other active charging candidates

| Field | Evidence | Decision |
| --- | --- | --- |
| `3E1` source `CHARGE_STATUS`, byte5 bits7..6 | All376 payloads are `00 00 00 80 00 00 00`; source status stays0 throughout confirmed charging. | Not usable as an external AC-charge flag on this Corsa-e. Export `BSI_Unknown3E1` raw bytes; preserve upstream definitions in source JSON. |
| `1E7` plug-lamp request, byte7 bits5..4 | Raw1 after the first2 frames; remains1 after delayed mode. Earlier owner-labelled low-SOC charging captures have raw0. | A lamp request is not a reliable independent connected/charging state. |
| `1E7` traction/power gauge, source9-bit field | Raw100/0% from0.49 to65.58s despite continuous charging; raw75/-12.5% until480.75s. Negative power may also be regeneration while driving. | Do not convert this alone into physical charging state. |
| `39A` source range-gain charging rate | Only2 of378 frames end in03 (source interpretation6 km/h); the other376 report0 during known continuous charging and final delay. | A positive sample is worth investigating, but zero cannot mean stopped and no kW conversion is established. |
| `31A` coarse remaining time | Raw126 at startup, usually0 later, brief raw2 samples. | Does not track continuous charging reliably; do not infer stopped/complete from zero. |

These fields remain unsuitable substitutes for the396 HMI status. Neither
`mode==immediate`, BSI awake, negative gauge nor nonzero ETA alone is used as an
active-charging flag. The existing Android parser and HA entities already
understand the normalized state, so this fix needs GW firmware rather than an
Android layout/protocol change.

## Energy and fixed capacity

Apply the owner's **4/3 correction to energies only**:

| Reference | HA value (kWh) | Corrected value (kWh) |
| --- | ---: | ---: |
| Battery capacity | 32.29 | 43.053333… |
| Battery residual | 15.04 | 20.053333… |

The screenshot's exact position in the capture is unknown. These are search
targets, not calibrated CAN readings. SOC remains a percentage and range remains
kilometres; neither receives this multiplier. This project does not expose a
decoded HV energy/capacity value to HA yet; the displayed cloud integration's
energy conversion is separate from this firmware.

The search compares stable windows5..45s and484..500s. It checks **146,720**
unsigned contiguous 8..32-bit BE/LE positions, zero offset,52 scales listed in
the JSON, >=80% modal support in each window, final-value tolerance50 Wh and
growth150..650 Wh. The deliberately broad growth interval allows cache age,
approximate AC power, losses and screenshot timing uncertainty; it is not an
estimate of expected net battery gain. Also checked byte-aligned IEEE754 float32
in both byte orders, as kWh/Wh, with and without the4/3 correction.

- **Eight integer growth matches**, all overlapping fragments of `236` vehicle
  time counter or `396` already-decoded **12V battery SOC**. These are rejected
  as HV energy candidates.
- **1,671 constant capacity coincidences**. Overlapping fields/alternative
  scales are not independent discoveries; constancy and numerical proximity do
  not establish battery capacity.
- **No float32 match** satisfying the same window/support/reference checks.

One especially misleading match is `3DA`: `(BE16(data[4:6]) >>5)` ends at235.
Multiplying235 by0.064 gives exactly **15.04**; correcting that gives20.0533.
However the earlier value is320, then0 for most of the capture, before returning
to320 and falling to235. It does not follow the observed energy increase. Its
source message also mentions extended charging time, but that tail is not
defined by the source layout. Neither the energy interpretation nor an exact
time unit is accepted from this coincidence.

`161` remains constant, `3E7` is maintenance data, and `3F0` byte2 is the previously
identified counter/checksum pattern. Unknown `7A4` has changes but no supported
monotonic energy relation or encoding. None is promoted to an energy signal.

This bounded negative result does not prove energy is absent from INFO CAN.
Intermediate/multiplexed updates, noncontiguous fields, signed/offset encodings,
different scales or stale endpoints can escape this search. Telematics reading
another bus or diagnostics remains plausible; see the earlier
[energy investigation](info-energy-preconditioning.md).

## Next discriminating capture

Keep the present file as a known-charging example. To strengthen the396 mapping,
record cable removal and
reinsertion plus explicit refreshes while stationary, with a timestamped physical
charging observation (EVSE power or vehicle charging indication) for each phase.
Capture at least one plugged-but-delayed interval and one unplugged interval.
This separates cable, stored mode and actual charging. Record the cloud energy
and capacity at the corresponding refreshes; simultaneous HSCAN1 would allow
comparison with the already known energy candidate `543` rather than assuming
its same numeric ID applies on INFO.

## Reproduce

```powershell
python tools/analyze_info_charging.py
python -m unittest discover -s tests -p test_info_charging_analysis.py -v
python -m unittest discover -s tests -p test_info_can_dbc.py -v
```

Six planted-field tests check the bounded search: packed32 Wh and little-endian
Wh growth, static-energy rejection, separate capacity matches, float32 and
rejection of an isolated numerical hit. DBC tests cover raw preservation and
scope guards for unknown vehicle layouts and the396 reserved-span overlay.
Six additional host tests exercise the actual396 decoder, varstore producer
ownership, derived age/Force/reset behavior, reserved enums, wrong DLC, both
real charging/delay captures and the resulting UDP/HA state. Enum tests validate
code handling; they do not physically validate unobserved vehicle states2/4.
