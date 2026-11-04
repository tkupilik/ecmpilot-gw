# Optional HSCAN1 telemetry

This page applies **only to the alternative HSCAN1/500 kbit/s listen-only
profile**, using Waveshare TX GPIO15 / RX GPIO16. The current default is
[INFO/125 kbit/s](can-rx.md), with a different decoder and control path.
HSCAN1 does not execute INFO wake, refresh, charging or preconditioning requests.
Select the profile together with its bitrate; the mappings below are not INFO data.

Default integer units in varstore are **mV**, **mA**, **mC** (millicelsius), **W**, and **0.1 %**. Signal suffixes and unit descriptors use those stored units; the UDP serializer converts them into the existing phone units V, A, C, kW and percent. CAN/DBC wire scales are independent of this storage convention.

## Live phone data

Standard data frame `0x5C3`, DLC exactly 8:

| Field | Extraction (zero-based bytes) | Varstore / UDP |
| --- | --- | --- |
| SOC | `((data[2] & 3) << 8) | data[3]`, unit 0.1% | `battery.soc_pct_x10` / `soc_pct` |
| Pack voltage | `(data[0] << 3) | (data[1] >> 5)`, unit 0.25 V | `battery.voltage_mv` = raw × 250 mV / UDP `voltage_v` = mV ÷ 1000; exact, no rounding |

The owner confirmed SOC against the car. Pack voltage agrees with independent complete cell sums in the recordings. Layout provenance is the [pinned HS1 DBC](../dbc/README.md); this is not a claim that every imported DBC signal is verified.

The original MyOpel wake capture starts with zero-filled `0x5C3`; real voltage/SOC appear about 1.292 seconds later. As a provisional availability gate, require a decoded pack voltage between **200 and 500 V inclusive** before publishing either field. No OEM availability bit has been verified. SOC must also be in 0–100%, so raw 1001–1023 invalidate SOC; raw 1023 is the source unavailable marker. Genuine 0%/100% with plausible live voltage are accepted. Bad voltage invalidates both fields; unavailable SOC does not invalidate an otherwise live voltage.

Examples from the recordings: `CF 80 32 C3 00 07 D0 00` → 415 V / 70.7%; `D0 00 02 C1 00 07 D0 00` → 416 V / 70.5%. Zero-filled startup frames produce unavailable values, not an empty battery reading.

Only received frames refresh these signals. Their freshness limit is 5 seconds. The existing UDP binding retains the true sample age, so the phone can show a stale last observation after CAN stops. Producer invalidation sends unavailable values. Ignore extended-ID aliases, RTRs and unexpected DLCs; they do not refresh a good sample. All application data goes through varstore, and the HTTP/UDP tasks never call the decoder to acquire data.

Direct web Write/Touch/Invalidate is disabled for SOC/voltage because CAN now owns them. Force/Release still works without overwriting the real producer data. No Android change is required: release any old bench overrides, select Local gateway and refresh. `test.enabled` can remain disabled; it controls the simulated command backend, not passive telemetry.

## Candidate handlers for web validation

These are intentionally separate from phone observations. A valid varstore sample here means the candidate field was decoded from a recent frame without its known unavailable marker; it does not establish its physical meaning or OEM availability. Metadata/tooltips identify candidates. Filter **`can.hvac`** or **`can.vehicle`** on the Varstore tab.

| ID / DLC | Varstore name | Meaning / caution |
| --- | --- | --- |
| `3CE` / 8 | `can.hvac.upstream_mc` | Air upstream of evaporator; not cabin temperature |
| `3CE` / 8 | `can.hvac.heater_outlet_mc` | Heater outlet water; raw zero may be inactive/default (-40 C after scaling) |
| `3CE` / 8 | `can.hvac.heater_setpoint_mc` | Heater water target, not a measurement |
| `3CE` / 8 | `can.hvac.evaporator_setpoint_mc` | Evaporator target, not a measurement |
| `3CE` / 8 | `can.hvac.evaporator_mc` | Evaporator probe temperature |
| `3CE` / 8 | `can.hvac.airflow_gps_x10` | Airflow, 0.1 g/s |
| `56E` / 6 | `can.hvac.compressor_current_ma` | Candidate electric compressor current, mA; constant zero in recordings, not independently validated |
| `412` / 8 | `can.hvac.heating_request_raw` | Heating-request bit, not an observed active heater |
| `5A2` / 7 | `can.hvac.preconditioning_state_raw` | Source labels 0 ON / 1 OFF; constant zero even in READY, meaning unverified. All-zero startup payload is suppressed |
| `5B2` / 8 | `can.hvac.outside_mc_candidate` | Outside temperature; source unavailable marker and sensor diagnostic checked |
| `552` / 8 | `can.vehicle.odometer_km_candidate` | Odometer; compare with cluster before promoting to phone |

Temperature fields use mC (1000 = 1 C), current uses mA, SOC uses 0.1 %, and airflow retains its explicitly named 0.1 g/s scale. Known all-ones unavailable codes invalidate individual fields (8/11/5/9-bit as applicable), rather than publishing a scaled sentinel. Outside temperature is invalidated when its diagnostic bits are nonzero. Candidate freshness is 5 seconds except odometer at 60 seconds. No candidate invokes control code or changes `climate.state`/`charge.state`.

The READY recording decodes to upstream air 8 C, heater outlet 42.5–44.5 C, evaporator probe 11.7–12.2 C, airflow 35.5 g/s, outside 8 C and odometer 58,992 km. These are references for validation, not new owner confirmations. Check the cluster temperature/odometer and change HVAC settings while recording before promoting candidates. Heating requests, actual heating, compressor operation and remote preconditioning are separate concepts.

## Phone inventory in the HSCAN1 profile

| Phone field | Current CAN support |
| --- | --- |
| SOC, pack voltage | Live from `5C3` |
| Estimated range | Not mapped |
| Cabin temperature | Not mapped; upstream/evaporator temperatures are not substitutes |
| Outside temperature | Candidate on web only |
| Minimum / maximum battery temperature | Not mapped; logical temperature slots require additional selection/availability work |
| Odometer | Candidate on web only |
| Battery current | Not mapped |
| Vehicle awake / parked | Not inferred from arbitrary traffic |
| Climate on/off | Not mapped from request bits or unverified preconditioning state |
| Charge state, cable, mode, power, remaining time, deferred start | Not mapped; existing manual/force bench inputs remain available |

Mapped fields reach the phone through UDP 42840. Unknown fields stay unavailable.
This profile advertises no physical vehicle-control capability. The active INFO
profile has its own [RX mappings](can-rx.md) and [control process](vehicle-control.md).
