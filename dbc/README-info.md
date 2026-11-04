# Corsa-e INFO-DIV CAN database

**2026-11-03 charging HMI:** the reviewed reserved span in INFO396 now exposes
`ChargingHmiState` (byte1 bits4..2), corroborated by two start/delay captures,
driving contrast and the matching HS3 source field. The INFO frame remains6
bytes; other HS3 fields are not imported. Raw0/1/3 are observed; fault/finished
are source-only.3E1 is now raw-only Unknown because its charge flag stays0
during confirmed charging. See [charging/energy review](../docs/info-charging-analysis.md).

**2026-11-03 preconditioning:** a scoped vehicle overlay replaces the reserved
first-byte span in `2E0` with ASAP and feedback fields, matching the observed
Start/Stop transaction and the older `002` header. Only feedback 0/1 is observed;
upstream source JSON is unchanged. See [the capture and implementation](../docs/preconditioning.md).

**2026-11-03 range correction:** this Corsa-e uses **2 km/count** for `31A`
`ZevRangeDisplay`. Raw 60 (`1E 00`) matches the owner's simultaneous MyOpel
reference of 120 km at 57% SOC. The generator's vehicle review overlay corrects
the scale; upstream factor 1 stays in source JSON. Raw 511 is still unavailable.
See [capture/reference calibration](../docs/range-calibration.md) for the evidence
and scope. This does not validate every range variant or the rest of `31A`.

**2026-11-01 OBC wake / cluster review:** label observed BSI412 cause byte0x20
as **OBC cause (AC cable insertion)** and rename the message `BSI_WakeupCause`.
The cause remains the original8-bit field, with an event-correlated value label;
no new cause-bit layout is invented. Attribute nine owner-approved messages to
unit004 with transmitter `IC` and prefix `IC_`:1D7,217,257,2D7,2E7,317,31C,51F,5DF.
Source aliases/layouts are preserved. All17 IDs added versus standby are also
added versus the fully operational driving capture, but seven existed in older
wake captures; only ten are genuinely newly observed.4A4 remains unassigned.
See [the wake timeline/comparison](../docs/info-obc-wake-2026-11-01.md) and the
[regenerable vehicle review](opel_corsa_e_info.vehicle-review.json).

**2026-10-31 EV review:**161 and3F0 are now raw-only Unknown messages;350 is
renamed FrontClimateStatus without changing its bit layout. See
[EV review](../docs/info-ev-review.md) and the persistent
[vehicle review overlay](opel_corsa_e_info.vehicle-review.json). The electric
inheritance is already active; these five IDs have no separate EV override.

Load **[opel_corsa_e_info.dbc](opel_corsa_e_info.dbc)** for the **INFO-DIV infotainment/telematics CAN, 125 kbit/s**. This is a separate bus database from the OBD/HS1 export. The current export covers **149,767 real frames / 116 observed IDs** across six INFO captures; see [capture validation and battery analysis](../docs/info-can-2026-10-28.md). Seven source DLCs are adapted and unknown ID `7A4` is included raw. This validates byte conversion and coverage, not every physical meaning or sender.

The owner independently confirmed the **charge-schedule time change from 03:02 to 03:01 in `0x35B` and `0x160`**; see [PSA telematics research](../PSA_telematic.md). SOC has a source-supported single-point candidate in `3E0`, byte 0, **70%** in all 124 samples, named `BatterySocCandidate`, consistent with the owner's recollection of 69 or 70%. The owner also recalls approximately **24.4 kWh**; a [numeric search](opel_corsa_e_info.energy-search.json) using this reference yields many coincidences but no supported energy signal. Remaining energy in kWh has not been identified on INFO. These confidence levels remain separate.

The export contains **377 IDs**: all 376 IDs resolved from the pinned source, plus raw-only `7A4`. It includes unobserved source definitions as well as captured traffic:

- **252 fixed-length source CAN messages**: 2,868 exported source/reviewed fields (including three charging/preconditioning status fields and25 opaque text/byte fields), plus74 raw fields covering gaps/reviewed unknown layouts.
- **One additional unknown message**, `Unknown_InfoMessage01` / `7A4`, with eight raw bytes and no invented sender or physical interpretation.
- **124 ISO-TP identifiers**: physical-frame transport fields, 40 signals per ID including conditional raw data and padding.
- **7,910 total DBC signals**. This includes raw/padding fields and mutually exclusive transport branches; it is not a count of vehicle measurements.
- **376 standard identifiers and one source-defined extended identifier**, `0x10000000`, which remains unobserved.
- ECU-prefixed names, all alternative source senders in transmitter metadata, original names/comments and source links. No HS1-specific aliases, confidence or corrections are applied just because the numeric ID matches.
- DBC global `Baudrate = 125000` and `BusType = CAN` metadata. Configure the adapter to the actual bus rate; a viewer need not apply these attributes automatically.

The source covers several software and equipment versions. An absent or constant message is not grounds to discard its definition. Every fitting canonical fixed-CAN field is retained except the three reviewed unknown layouts, whose full upstream fields remain in source JSON. Captured fixed DLCs replace source DLCs for `069` (2 -> 5), `0A8` (6 -> 4), `165` (5 -> 6), `317` (8 -> 5), `329` (7 -> 8), `364` (6 -> 7), `3DA` (4 -> 6), with additional bytes raw. No canonical source field needed removal in these captures. Any future out-of-payload source field is explicitly audited and retained in source JSON. New IDs in explicitly supplied captures are added raw. The separately reviewed IC source attribution is vehicle-specific; a difference between capture ID sets alone does not assign every added frame to a new ECU.

## Owner-verified fields — 2026-10-31

The owner confirms speed (`0B6`), selected gear and parking brake (`128`),
BSI_SlowData2 (`0F6`, currently consumed odometer/filtered outside temperature),
and ICSV_MaintenanceStatusEv (`3E7`, service reminder data) match the vehicle.
The DBC now marks **13 named fields** `OWNER_CONFIRMED`. Reserved fields and other
signals retain candidate status; the entire database is not marked verified.
These reports cover observed values, not exhaustive enums/invalidity boundaries.

[Owner-validation metadata](opel_corsa_e_info.owner-validation.json) is a separate
input to the generator. Confirmation requires matching source name/path, DLC,
bit layout and scale/offset. Original source JSON remains unchanged. Message
comments list confirmed fields; the inventory and validation report expose the
same scope. The generator performs no live vehicle I/O.

`3E0` traction SOC remains a candidate; it has not been confirmed by these reports.
`3E7` service reminder data does not replace it. Physical sender alternatives
remain as documented upstream.

## Telematics starting points

These are **source MDS sender claims**, not proof that MDS equals the physical BTA in this Corsa. The diagnostic BTA models and broadcast database use different aliases. In particular, the absence of these IDs on OBD/HS1 does not establish their absence on INFO-DIV.

| ID, hex | DBC name / fields to investigate |
| --- | --- |
| `394` | `MDS_RemoteActionRequest2`; `RemoteSendChargeDataRequest`, remote lock/unlock |
| `3E0` | `ICSV_BatterySocAndHistory`; `BatterySocCandidate`, direct byte-0 percent candidate |
| `43C` | `MDS_Wakeup`; wake-up announcement, eight unnamed cause flags |
| `3A9` | `MDS_TelematicsCallButtonStatus`; `NetworkMaintainRequestMdc`, SOS/assistance/mute |
| `31B`, `11B` | Remote climate schedule, parts 1 and 2 |
| `35B` | Remote charge schedule |
| `369` | Telematics HMI/connectivity status |
| `3D6` | Remote diagnosis request |
| `53C` | `MDS_Supervision`; diagnostic session and communication counters |
| `5FC` | `MDS_Version`; source title incorrectly says lane geometry, source fields describe version |
| `254`, `314` | Remote commands transported over ISO-TP; application layouts remain in source JSON |
| `67C`, `77C` | Diagnostic response from MDS / request to MDS, ISO-TP wire format |

`43C` describes a wake announcement. There is no measured evidence here that this particular frame is the physical wake trigger. The source does not name the eight cause flags more precisely.

The display titles of `11B`, `394`, `3E0`, `43C`, `5FC` and generic diagnostic blobs are adjusted for clarity, without changing source bit layouts. `3E0 BATTERY_STATUS` is displayed as `BatterySocCandidate`. All original titles/fields remain in source JSON, and original message titles remain in comments/inventory. Sender alternatives such as BTEL/OVIP are preserved, without claiming every alternative is fitted.

## ISO-TP and variable lengths

A DBC for raw CAN frames cannot directly decode an arbitrarily long, reassembled ISO-TP application payload. The source contains both transport layouts and application layouts whose offsets start at the first **logical PDU byte**, with text extending beyond eight bytes. Copying these offsets into a raw CAN DBC would mistake PCI bytes and partial payloads for application data.

For all 124 `can-tp` IDs this export therefore uses a **normal-addressing candidate** transport layout:

- `IsoTpFrameType`: Single / First / Consecutive / Flow Control frame.
- Declared lengths, consecutive-frame sequence number, flow status, block size and raw separation-time code.
- Separate raw data/padding bytes for each branch; unknown PCI values retain raw coverage.
- Maximum Classical CAN DLC 8. Actual recorded DLCs are in the inventory; most transport IDs remain unobserved. In these files `2D4` has one byte and `328` has eight bytes. Consumers must support truncated decoding; with cantools use `allow_truncated=True` for short physical frames. Do not pad absent bytes and treat the resulting fields as measured data. A static `2D4 00` alone does not validate normal ISO-TP framing or exclude a direct status interpretation on this car.

These fields do **not** validate a transport transaction, distinguish actual data from padding, reassemble streams, interpret text or decode diagnostic DIDs. `SeparationTimeRaw` is the encoded value, not a time in milliseconds. The addressing mode also needs checking against a capture. The complete original layouts, including every long text field, alternative and ISO-TP linkage, remain in **[source JSON](opel_corsa_e_info.source.json)**; the audit labels them `source_preserved_separate_layer`, not physically disproven definitions.

Opaque fixed-CAN `str`/`bytes` fields are retained as raw unsigned integers with explicit comments. No text encoding or string conversion is asserted.

## Provenance and alternatives

Source: [prototux/PSA-RE](https://github.com/prototux/PSA-RE), revision `67f7581e7622410efdbb29c7eda3b86982b9b450`, bus **`LS.INFO-DIV` only**, inheritance **`AEE2010.electric -> AEE2010.ev -> AEE2010.full`**. Here `ev` means the evolution of the electrical architecture. Definitions from other buses, including HS3 BSRF definitions from another profile, are not merged by matching numeric ID.

An exact `<ID>.yml` filename is preferred within a profile; otherwise the first sorted filename is canonical. Every other same-ID file is retained in `source_file_alternatives`; overridden parent definitions are retained there too. Message/signal `alternatives` are also preserved. This notably avoids losing the two differently labelled `741` source files. Choosing one canonical layout is not evidence against the alternatives.

Derived definitions remain **Apache-2.0**, with [license](LICENSE-PSA-RE.txt) and [modification notice](NOTICE-PSA-RE.txt). The generator remains under the project's BSD license. Enum quote characters are normalized for DBC syntax; original labels remain in source JSON. Files use UTF-8.

## Reproduce and verify

Use [the existing Python dependencies](../tools/requirements-can.txt) and the ZIP of the pinned source revision:

```powershell
python tools/generate_info_can_dbc.py `
  --psa-re C:\path\to\PSA-RE.zip `
  --capture logs/infocan.csv `
  --capture logs/info_apka.csv `
  --capture logs/info_start_jizda.csv `
  --capture logs/info_buzeni_obc.csv `
  --capture logs/info_standby.csv `
  --capture logs/info_charging.csv `
  --output dbc/opel_corsa_e_info.dbc

python -m unittest discover -s tests -p test_info_can_dbc.py -v
```

The generator reloads the written DBC with strict bounds/overlap checking, then checks independent MSB-first extraction, signed/scaled values, active mux branches, full bit coverage and lossless decode/encode for **32,578 generated payloads**. It also checks **13,888 short transport payloads**, all PCI states and lengths 1..7. These are synthetic conversion checks. Separately, all **53,971 actual captured payloads** and **986,175 signal values** are checked against independent extraction, without padding missing bytes. The unadapted source baseline is evaluated against the same captures for comparison. Seventeen database tests include per-field confidence and layout guards, capture DLC adaptation, unknown-ID coverage, timestamp regressions and independent clocks for files with identical names, as well as bus/transport/source checks. Three additional OBC tests check the original capture sequence, both17-ID comparisons and the nine-message source scope. Physical meanings remain subject to independent validation.

| File | Contents |
| --- | --- |
| `opel_corsa_e_info.dbc` | Raw-CAN viewer database |
| `opel_corsa_e_info.inventory.csv` | ID, ECU claims, mode, source DLC/minimum, exported DLC, source link |
| `opel_corsa_e_info.source.json` | Complete resolved source, all original fields and alternatives |
| `opel_corsa_e_info.audit.json` | Source/raw/transport fields and application fields retained in separate metadata |
| `opel_corsa_e_info.validation.json` | Synthetic check results, counts, pinned revision and hashes |
| `opel_corsa_e_info.capture-validation.json` | Actual-frame coverage, source baseline, observed IDs/DLCs, input hashes and review flags |
| `opel_corsa_e_info.capture-ranges.csv` | Per-capture signal/raw ranges, invalid/default values, enum and source-range discrepancies |
| `opel_corsa_e_info.energy-search.json` | Numeric-only hypotheses near the owner's approximate 24.4 kWh reference; not DBC signal discoveries |

Continue capturing the original bus order and actual DLCs for sleep, MyOpel status refresh, remote preconditioning and charging. Compare these definitions with observed data before refining sender identities, lengths or meanings. The first charge-time transaction is documented in [PSA telematics research](../PSA_telematic.md). This export changes no firmware and performs no vehicle I/O.
