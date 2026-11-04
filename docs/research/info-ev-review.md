# INFO CAN EV layout review — 2026-10-31

> Capture evidence recorded at the time of this analysis. For current firmware behavior and request status, use [CAN requests](../can-requests.md) and the [documentation index](../README.md).

The export already uses AEE2010.electric -> AEE2010.ev -> AEE2010.full. In
PSA-RE, ev means evolution; electric is the electrified variant. The GitHub API
reported master at the same pinned revision67f7581e7622410efdbb29c7eda3b86982b9b450
on this review. The archive has **no AEE2010.electric/LS.INFO-DIV files**. All five
queried definitions therefore come from AEE2010.full/LS.INFO-DIV. Searching all
bus/profile copies finds13 matching definitions, with no alternative EV layout
for these messages. Other-bus copies are not silently imported by numeric ID.

[Architecture/inheritance source](https://github.com/prototux/PSA-RE/blob/67f7581e7622410efdbb29c7eda3b86982b9b450/architectures.yml).
[Machine-readable evidence](../info-ev-review.json) records capture hashes, byte
ranges, source paths, alternate locations and counter/checksum statistics.

## Findings

Follow-up: [charging review](../info-charging-analysis.md) adds3E1 to unknown vehicle
layouts. In the owner's confirmed continuous AC-charge capture its source
CHARGE_STATUS remains0 throughout. It is not a usable external-charge indicator
on this Corsa-e; no unverified active-charging flag is substituted.

| ID / old name | Four-capture evidence | Decision |
| --- | --- | --- |
|161 BSI_Gauges |353 identical frames:00 00 3C 00 00 00 FD. Oil/fuel data has no supported EV meaning;253 also exceeds the source oil-level range. |BSI_Unknown161, raw bytes only. Constancy does not establish a battery field. |
|221 BSI_TripGeneralInfos |175 identical frames:00 FF FF FF FF 00 00. Consumption and range are FFFF unavailable, notwithstanding the zero check flags. |Retain source layout; unavailable codes are not actual consumption/range. No electric energy interpretation found. |
|276 BSI_VsmSlowData |183 frames,9 payloads; decoded date/time and lock states vary plausibly. AdBlue range is always3FFE, the source default/unavailable code. |Retain usable clock/lock layout; do not reinterpret AdBlue range as battery range. |
|2AD BSI_ClimateHmiCommand |376 frames,4 payloads; center code3 ->21°C reference throughout. |Reference/configuration, not measured cabin temperature or necessarily the left selected temperature. |
|3F0 ICSV_EngineHybridDynamicData18 |3532 frames,189 payloads. Byte2 behaves as counter/checksum rather than torque; first two bytes alwaysFFFF. |ICSV_Unknown3F0, raw only; upstream torque/engine claims suppressed from the viewer. |

## 3F0 contradiction

With stationary payload fields, byte2 repeatedly cycles:
24 D9 8E 33 E8 9D 42 F7 AC 51 06 BB 60 15 CA 7F.
Its low nibble advances **+5 modulo16 in all3528 adjacent within-file pairs**.
For **all3532 payloads**, including the moving capture, the sum of all sixteen
payload nibbles is **5 modulo16**. Equivalently, the high nibble of byte2 equals
`(5 - low_nibble(byte2) - sum(other_payload_nibbles)) &15` in these captures.
This is strong counter/checksum evidence, not an OEM-certified checksum algorithm
or complete layout. In particular, the entire byte must not be scaled as Nm.
Byte5 changes0..16 in the driving capture while it is zero in the other files;
its upstream reserved label is also questionable, but its physical meaning is open.

## Climate: 2AD reference +350 relative zone values

The source title CSS_SUSPENSION_STATUS for350 contradicts its own climate fields.
The viewer title is now ICSV_FrontClimateStatus; original title/layout remains in
source metadata. Source sender alternatives are preserved.

- 2AD byte0 bits4..2: reference X =18+code°C (code0..7).
- 350 byte3 low5 bits: front-left code; byte4 low5 bits: front-right code.
- Codes1..21 describe nonlinear offsets from X, not absolute°C. Code9 is X-1,
 code10 X-0.5, code11 X, code12 X+0.5, code13 X+1; outer offsets go to +/-7°C.
- Code0 is labelled Local upstream,22 HI; neither is exported as numeric°C.
 Reserved23..31 are also unknown. A LO interpretation of code0 is not assumed.
- 350 bytes1..2 contain a separate evaporator reading: raw*0.1-40°C, withFFFF
 unavailable; values outside the source-physical range-40..85°C are rejected.

Example from the captured layout:2AD0E... gives X=21°C;350...09 0B... gives
**20°C left /21°C right**. No measured cabin temperature follows from this pair.
The evaporator is not the cabin sensor. The selected setpoint/layout and
single-zone behavior still need a synchronized comparison on this vehicle.

## Viewer changes and preservation

The generator reads dbc/opel_corsa_e_info.vehicle-review.json. On an exact
source name/path/DLC match,161,3F0 and3E1 are renamed Unknown and exported only as
RawByteN, without oil, fuel, torque or battery units. All source fields are retained
unchanged in source JSON and audited as source_preserved_unknown_vehicle_layout.
Every payload remains losslessly covered. Owner-confirmed unrelated fields retain
their previous confidence. Nothing here adds CAN TX or changes firmware wake-up.

A normal regeneration retains these decisions. The12 generator tests cover the
raw-only overlay, provenance and no accidental override on a different bus/path.
Capture validation is rerun on the same three original export inputs; the fourth
standby capture is included in this separate review and firmware decoder tests.
