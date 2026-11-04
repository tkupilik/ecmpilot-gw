# AC cable / OBC wake and original cluster traffic

Owner event: AC cable inserted into the Corsa-e. Capture: `info_buzeni_obc.csv`.
Bus: INFO, 125 kbit/s. Unit004/original instrument cluster was connected.

## Observed wake sequence

Time zero is the first captured frame, not the measured instant of cable insertion.
All times below use this file's own microsecond clock; no files are time-aligned.

| CSV line | Time (ms) | CAN ID | Payload | Interpretation |
| ---: | ---: | --- | --- | --- |
| 2 | 0.000 | `412` | `00 20 00 00 00 00 00 00` | BSI wake cause, byte1=0x20: OBC cause / AC insertion |
| 3 | 10.156 | `412` | `00 20 00 00 00 00 00 00` | BSI wake cause, byte1=0x20: OBC cause / AC insertion |
| 4 | 20.074 | `036` | `0E 00 83 0F 13 04 01 A8` | BSI PowerManagement=3 |
| 5 | 20.980 | `412` | `00 20 00 00 00 00 00 00` | BSI wake cause, byte1=0x20: OBC cause / AC insertion |
| 6 | 30.198 | `412` | `00 20 00 00 00 00 00 00` | BSI wake cause, byte1=0x20: OBC cause / AC insertion |
| 7 | 39.995 | `412` | `00 20 00 00 00 00 00 00` | BSI wake cause, byte1=0x20: OBC cause / AC insertion |
| 8 | 45.196 | `5D2` | `00 00 00 00 00 01 00 00` | BSI version-layout identification, not an OBC transaction ACK |
| 9 | 54.750 | `036` | `0E 00 83 0F 11 04 01 A8` | BSI PowerManagement=1 |
| 63 | 223.931 | `317` | `13 BD 00 C4 B0` | First cluster configuration |
| 64 | 229.202 | `217` | `60 80 00 00 00 00 00 01` | First cluster odometer status |
| 71 | 242.450 | `5DF` | `1F 1C 01 15 4C 5E 3F 34` | Original cluster version |

BSI announces `00 20 00 00 00 00 00 00` five times over 39.995 ms, roughly 10 ms apart.
PowerManagement changes from Wake-up (3) at 20.074 ms to Normal (1) at 54.750 ms:
a 34.676 ms transition. It remains Normal through the last observed 036 at 34.120 s.

**OBC cause:** the observed full value `BsiWakeupInfo0=0x20` is labelled
`OBC cause (AC cable insertion)` in the DBC. This is the owner-labelled event
correlation, not an independently isolated cause-bit mapping. The original
8-bit field remains intact; `0x30` in the earlier `infocan.csv` remains unlabelled.

The first captured frame is already a BSI announcement. No preceding direct
OBC request or 401/420/424/43C announcement is captured. Thus a request/reply
ping-pong cannot be reconstructed on INFO. The 412 burst and 036 transition
are BSI's observable announcement/acceptance of waking, not a matched
application transaction ACK. The 5D2 payload uses the upstream version/identity
layout despite its misleading HybridControlRequests title; do not count it as
an OBC ACK. The physical OBC-to-BSI wake path is not identified by this file.

## Added identifiers and capture context

| Reference capture | Reference IDs | Added in OBC capture | Absent from OBC capture |
| --- | ---: | --- | --- |
| `info_standby.csv` | 85 | 17: 01C, 1D7, 217, 257, 2D7, 2E7, 317, 31C, 412, 4A4, 51F, 5C1, 5D2, 5DF, 5E4, 5FB, 5FC | 01E, 04D, 070 |
| `info_start_jizda.csv` | 93 | 17: 01C, 1D7, 217, 257, 2D7, 2E7, 317, 31C, 412, 4A4, 51F, 5C1, 5D2, 5DF, 5E4, 5FB, 5FC | 01D, 01E, 04D, 04F, 070, 073, 07F, 08D, 10B, 120, 328 |

Both standby and fully operational driving captures yield exactly the same 17 added IDs.
Comparing against the union of all three older files (including `infocan.csv`
and `info_apka.csv`) leaves 10 genuinely newly observed IDs: the nine cluster
messages below, plus 4A4. All 17 already had source definitions in this DBC;
none requires inventing a new raw-only identifier.

| ID | Current DBC name | Count | Median period (ms) | Seen in older files | Attribution |
| --- | --- | ---: | ---: | --- | --- |
| `01C` | `ICSV_MotionSensorCommandRight` | 1 | one frame | infocan.csv, info_apka.csv | Existing source retained; not assigned to IC |
| `1D7` | `IC_LeftLedHeadlampDriverData` | 339 | 100.061 | none | IC / unit004 (owner-approved) |
| `217` | `IC_OdometerStatus` | 340 | 100.055 | none | IC / unit004 (owner-approved) |
| `257` | `IC_Odometer` | 3 | 35.312 | none | IC / unit004 (owner-approved) |
| `2D7` | `IC_ClusterSettingsStatus` | 34 | 1000.725 | none | IC / unit004 (owner-approved) |
| `2E7` | `IC_DisplayModeRequest` | 2 | 11609.910 | none | IC / unit004 (owner-approved) |
| `317` | `IC_Config` | 34 | 1000.687 | none | IC / unit004 (owner-approved) |
| `31C` | `IC_TripComputerStatus` | 170 | 200.044 | none | IC / unit004 (owner-approved) |
| `412` | `BSI_WakeupCause` | 5 | 9.976 | infocan.csv | Existing source retained; not assigned to IC |
| `4A4` | `ICSV_TelematicsFaultEvent` | 18 | 99.990 | none | Existing source retained; not assigned to IC |
| `51F` | `IC_LaneDepartureWarningLe_3b5323` | 34 | 1000.719 | none | IC / unit004 (owner-approved) |
| `5C1` | `SDCM_BmlVersion` | 1 | one frame | infocan.csv, info_apka.csv | Existing source retained; not assigned to IC |
| `5D2` | `BSI_HybridControlRequests` | 1 | one frame | infocan.csv, info_apka.csv | Existing source retained; not assigned to IC |
| `5DF` | `IC_Version` | 1 | one frame | none | IC / unit004 (owner-approved) |
| `5E4` | `ICSV_BtelVersion` | 1 | one frame | infocan.csv, info_apka.csv | Existing source retained; not assigned to IC |
| `5FB` | `FMUX_LaneLeft1CurveData` | 1 | one frame | infocan.csv, info_apka.csv | Existing source retained; not assigned to IC |
| `5FC` | `MDS_Version` | 1 | one frame | infocan.csv, info_apka.csv | Existing source retained; not assigned to IC |

The nine IC messages use transmitter `IC` and prefix `IC_`, with unit004 and
capture-comparison provenance in comments/inventory. Original sender aliases
and layouts remain in source JSON; IC attribution does not validate signal meanings.
4A4 is still ambiguous (its source includes CMB among alternatives) and is not
assigned to IC. Seven of the 17 IDs also appeared in earlier wake/start captures:
01C,412,5C1,5D2,5E4,5FB,5FC. They cannot be attributed to the newly installed
cluster merely because they are absent from a driving/standby excerpt.

## Reproduce

Run `python tools/analyze_obc_wake.py` from the project root.
The adjacent JSON contains input SHA-256 hashes, counts, precise events and comparisons.
Original CSVs are read-only. No firmware, adapter or vehicle operation is performed.
