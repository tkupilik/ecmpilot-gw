# Corsa-e INFO range calibration

2026-11-03. The owner supplied a MyOpel screenshot for the same vehicle state as
`logs/info_soc57_refresh.csv`: **57% SOC and 120 km range**.

| Observation | Capture evidence | Interpretation |
| --- | --- | --- |
| SOC | All 45 `0x3E0` frames start `39 00` | Byte 0 is 57 percent, consistent with MyOpel |
| Range | 44 `0x31A` frames are `1E 00 00 00 3F E0 05 A0` | Motorola bits `1.7-2.7` give raw 60; this car needs **2 km/count**, giving 120 km |
| Unavailable range | One startup `0x31A` is `FF 80 00 00 3F E0 05 A0` | Raw 511 remains unavailable; it is never exported as 1022 km |
| Other range candidate | All 44 `0x221` payloads have unavailable `FFFF` autonomy | This field cannot provide the reference range |

The firmware previously treated raw 60 as 60 km. The error was in the CAN
handler, before varstore; UDP, HA and Android conversion already used the correct
storage units. The corrected path is:

```text
31A [1E 00 ...] -> raw 60 -> 1200 in vehicle.range_km_x10
                            -> UDP/HA divide by 10 -> 120 km
3E0 [39 00 ...] -> raw 57 -> 570 in battery.soc_pct_x10 -> 57%
```

Android requires no change or reinstall for this correction. The local driving
replay now yields 156-158 km from raw 78-79, rather than 78-79 km. Those replay
values follow the corrected linear scale; they have no separate screenshot
reference. This simultaneous capture/app comparison anchors one operating point,
not every possible vehicle variant, rounding rule or upper limit. The existing
raw acceptance range 0-500 and unavailable code 511 are unchanged.

The DBC generator applies the factor 2 through the reviewed `signal_scales`
entry in `dbc/opel_corsa_e_info.vehicle-review.json`. It requires the exact source
name, path, length, bits and original scale/range/invalid code. The upstream
definition (factor 1 and alternate factor 0.1) stays unchanged in source JSON.
The serialized DBC records the vehicle-specific correction and its evidence.

`tests/test_can_info.py` replays the actual 90 SOC/range frames through CAN RX,
native varstore and UDP, checking 44 valid 120 km observations, one unavailable
startup observation and 45 SOC samples at 57%. The DBC test checks raw 60 versus
120 km, serialized output, unchanged upstream provenance and rejection of the
overlay for a different layout/profile.
