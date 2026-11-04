# Corsa-e INFO-DIV CAN database

Current state: **2026-11-04**. Load [opel_corsa_e_info.dbc](opel_corsa_e_info.dbc)
for **INFO-DIV, 125 kbit/s**. This is distinct from the OBD/HSCAN1 database.
For transmitted commands use **[CAN requests](../docs/can-requests.md)**; this
page describes database scope, confidence and regeneration.

## Vehicle interpretation and confidence

| Area | Current interpretation | Evidence / boundary |
| --- | --- | --- |
| Speed, gear, parking brake, 0F6 odometer/outside temperature, 3E7 service data | Selected fields owner-confirmed; 13 fields formally marked `OWNER_CONFIRMED` | [Owner-validation metadata](opel_corsa_e_info.owner-validation.json); not every enum or message field |
| `3E0` SOC | Byte 0 percent; GW stores ×10 | Multiple reference captures match, including 57%; source DBC name remains `BatterySocCandidate`, not a blanket promotion of the message |
| `31A` range | **2 km/count**, raw 511 unavailable | Owner screenshot 57% / 120 km, raw 60; [calibration](../docs/range-calibration.md) |
| `396` charging HMI | Byte 1 bits 4..2; observed states 0/1/3 | [Charging review](../docs/info-charging-analysis.md); fault/finished labels remain source-only |
| `2E0` preconditioning | ASAP bit 2, feedback bits 6..3; observed feedback 0 off / 1 active | [Start/Stop capture](../docs/preconditioning.md); scoped vehicle overlay, upstream JSON unchanged |
| `412` wake cause | Full observed value `0x20` labelled OBC / AC cable insertion | [Plug-in capture](../docs/info-obc-wake-2026-11-01.md); not an invented cause-bit layout |
| Cluster/unit 004 | `IC_` prefix for 1D7,217,257,2D7,2E7,317,31C,51F,5DF | Nine owner-approved source assignments; not all added IDs |
| `161`, `3F0`, `3E1` | Contradictory vehicle layouts exported raw/Unknown | [EV review](../docs/research/info-ev-review.md) and charging review |
| `350` | FrontClimateStatus; setpoints/evaporator, not measured cabin temperature | Existing bit layout retained |
| `7A4`, `4A4` | Unknown/raw evidence; no validated UDS/DTC interpretation | An incidental 22 byte or diagnostic-looking layout is insufficient |
| Remaining energy / battery capacity / HV voltage and temperatures on INFO | No validated producer | [Energy search](../docs/research/info-energy-preconditioning.md); cloud values alone do not identify an INFO field |

Formal owner confirmation requires matching name/path, DLC, bit layout and
scale/offset. Reserved fields and source-only candidates retain their confidence.
Live GW handlers can use an observed field without marking every related source
signal verified. Raw CAN reception also does not establish source measurement
age or cloud upload.

The [vehicle review overlay](opel_corsa_e_info.vehicle-review.json) records
corrections; complete original definitions remain in source JSON. A difference
between capture ID sets does not independently prove a physical sender.
MDS and other ECU labels are source attributions, with alternatives retained.

## Export scope

The export contains **377 IDs**: 376 resolved pinned-source IDs plus unknown
`7A4`. It includes 252 fixed-length source messages, one additional raw unknown
and 124 transport IDs; **7,910 signals** include raw/padding and mutually
exclusive branches, not 7,910 vehicle measurements. One source extended ID
`0x10000000` is unobserved; all others are standard identifiers.

The current generation's six-capture validation covers **149,767 frames / 116
observed IDs**. Counts describe those explicit generator inputs, not every log
subsequently added to the repository. Seven fixed DLC adaptations are 069
(2→5), 0A8 (6→4), 165 (5→6), 317 (8→5), 329 (7→8), 364 (6→7), 3DA (4→6).
Additional bytes are raw. Conversion/coverage checks do not validate all source
physical meanings, senders or availability codes.

Absent/constant fields are retained; contradictory vehicle layouts are replaced
by reviewed unknown/raw exports with originals preserved separately. Bus rate
metadata is 125000, but configure the adapter explicitly. Same numeric IDs on
different buses do not justify merging definitions.

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

Continue capturing the original bus order and actual DLCs for sleep, MyOpel status refresh, remote preconditioning and charging. Compare these definitions with observed data before refining sender identities, lengths or meanings. Current request layouts and outcomes are summarized in [CAN requests](../docs/can-requests.md). This export changes no firmware and performs no vehicle I/O.
