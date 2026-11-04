# INFO CAN receive mappings and local replay

The gateway uses the LCD project's receive mappings adapted to the **GW global
varstore API**, with no VS/ufrm compatibility layer. Default source is physical
ENT INFO CAN at **125 kbit/s**, TX15/RX16. The optional legacy HS1 profile is
documented separately in [CAN telemetry](can-telemetry.md).

## Latest LCD parity

See [LCD/GW parity](lcd-backport.md) for configurable wake TX (without version5DF), application processing
and diagnostics. INFO now uses NORMAL mode; legacy HS1 stays listen-only.

New mappings added to the nine original handlers:

| CAN ID / DLC | GW varstore group | Meaning |
| --- | --- | --- |
|2E0 /7, 0E0 /7 | bsi.precond.*, climate.state | Reported ASAP/feedback and preserved weekly programs; no cabin-temperature inference |
|036 /8 (extended handler) | bsi.power_management, bsi.daynight_status, bsi.brightness | Network phase and lighting |
|396 /6 | bsi.aux_battery_* and bsi.remote_diag_authorization |12V battery, separate from HV |
|2AD /8 | bsi.climate_* | Raw climate HMI fields; temperature-reference code0..7 means18..25C |
|350 /8 | icsv.climate_* and icsv.evaporator_mc | Relative left/right setpoint codes and measured evaporator |

The independent application actor derives climate.left_setpoint_mc and
climate.right_setpoint_mc from2AD/350, preserving oldest input age. Local/HI and
reserved codes invalidate numeric temperatures. No cabin measurement is inferred.
Age alone does not invalidate cached setpoints: the phone keeps them visible in
grey, while HA and control consumers can still enforce their freshness limits.
Phone and HA receive these values without an Android rebuild.

## Mappings

Only classic standard data frames with the exact listed DLC reach these handlers.
Extended aliases, RTR, malformed lengths and unrelated IDs do not refresh good
observations. Source unavailable/range codes invalidate their own fields.

| CAN ID / DLC | GW varstore output | Conversion / interpretation |
| --- | --- | --- |
| `036` / 8 | `can.network_phase`, `vehicle.awake` | Byte4 bits0–2; phase1/3 awake, 0/2/4 not awake, other codes invalid |
| `0A2` / 6 | `steering.buttons`, navigation/volume/cluster counters, overflow flags, nine `steering.press.*` totals | Raw mask and signed 8-bit rolling counters; no UI/control actions |
| `0B6` / 8 | `vehicle.speed_kmh` | Source 0.01 km/h rounded to nearest whole km/h; 65535 unavailable |
| `0F6` / 8 | `vehicle.odometer_km`, `temperature.outside_mc` | Odometer 0.1 km truncated to km; filtered outside byte6 ×500−40000 mC |
| `11C` / 4 | `vehicle.speed_limit_kmh` | Byte2 1–250 with byte3 top two bits zero; absent from this capture |
| `128` / 8 | `vehicle.gear`, `.parked`, `.drive_ready`, `.low_beam`, `.parking_brake`, `.seatbelt_warning`, `can.gear_code` | Gear enum 0 P / 1 R / 2 N / 3 D / 4 B; raw15 maps B, other unknowns invalid; READY independent of gear |
| `1E7` / 8 | `battery.power_w`, `vehicle.drive_power_pct_x10`, `can.power_raw` | Battery candidate `(byte3>>1 −50)×1000 W`; drive gauge `raw9×5−500` in 0.1%, a separate quantity |
| `160` / 2 | `bsi.charge_schedule_raw`, `.charge_schedule_available`, `charge.mode`, `.start_minute` | Preserve both bytes; bit15 availability, bit14 mode, low11 bits minutes; require time <1440 |
| `31A` / 8 | `vehicle.range_km_x10`, `bsi.trip_charge_mode`, `.trip_charge_minute` | Corsa-e range: raw ×2 km, stored as raw ×20; secondary schedule mode/time for comparison. See [reference calibration](range-calibration.md). |
| `3E0` / 8 | `battery.soc_pct_x10` | Byte0 0–100% ×10; other codes invalid |

Storage uses mC, W and percentage ×10. Range uses GW's existing **km ×10**;
the LCD store uses whole km, so a direct unscaled copy would be wrong. Phone UDP
retains its existing C/percent/km units. Battery W has only 1 kW resolution and
is a validation candidate. Drive percentage is never presented as measured watts.
`vehicle.traction_power_w` remains invalid without a verified producer.

INFO currently has **no pack-voltage/current or measured cabin-temperature
handlers**. These fields remain unknown unless explicitly supplied as manual
bench inputs/forces. HS1 voltage/HVAC decoding does not leak onto INFO.
Reported preconditioning state comes from `2E0`; charging mode/schedule comes
from `160`. These do not manufacture measured charging power or cable state.
Explicit experimental refresh/charge-mode control is now implemented separately
in [the vehicle policy](vehicle-control.md). RX handlers only publish varstore.
Immediate preconditioning control is documented in [the captured sequence](preconditioning.md).

Additional `236`/`276` aperture/lock and `2A1`/`261`/`31A`/`39A` trip mappings
are documented in [passive vehicle details](vehicle-details.md), including their
separate phone datagram and current vehicle-validation limits.

`steering.buttons` bits0–8 are OK, back, menu, mode, left, voice, phone, cluster OK,
cluster back. Sampled rising edges increment the corresponding press totals up to
INT32_MAX. First frame, source transition and a gap over one second establish a
baseline without inventing a press. Totals survive replay/live changes; reboot
resets them. This sampled state cannot recover presses missed before reception.
Button functions, warning polarity and network-phase meaning still need vehicle
confirmation; database labels alone are not proof of physical behaviour.

## Replay

`can.replay_enabled` starts at **0 (live)**. Write or Force it to 1 on the web to
replay the owner's `logs/info_start_jizda.csv` locally. Release a force to return
to its real producer setting; write 0 if that setting was changed to 1.

The flash-resident array contains 1974 selected frames from 9520 source frames,
looping every 36117 ms with original ordering/timing rounded down to milliseconds.
Catch-up work is bounded to 32 frames per timer turn. Switching sources and loop
restart invalidate observations and reset state baselines, preserving Force.
All replay decoding is performed by the same CPU1 owner as physical RX.

Physical RX/health/raw diagnostics and GVRET continue during replay, but decoded
observations come exclusively from the selected source. **Replay never transmits
on CAN and never enters the SavvyCAN stream.** Disable replay and release old
telemetry forces before vehicle verification.

Regenerate from the capture with:

```powershell
python tools/make_can_replay.py logs/info_start_jizda.csv
```

[Replay metadata](can-replay.json) records the source SHA-256, counts and duration.
Tests compare every selected frame/order and check sentinel handling and the
actual RX → GW varstore → phone UDP path.

## Provenance and confidence

Definitions originate from [prototux/PSA-RE](https://github.com/prototux/PSA-RE),
revision `67f7581e7622410efdbb29c7eda3b86982b9b450`, under Apache-2.0.
[Source snapshot](can-source-reference.json) preserves layouts, possible senders
and the cluster-sender inventory; [notice](../licenses/NOTICE-CAN.txt) records the
adaptation. `11C` is not exercised by the capture, so tests use synthetic inputs.
Matching recorded payloads to those definitions checks software interpretation,
not sender attribution or all physical meanings. Confirm on the vehicle before
using observations for future control decisions.

## Owner validation — 2026-10-31

Speed (0B6), selected gear and parking brake (128), consumed BSI_SlowData2 fields
(0F6: odometer/filtered outside temperature), and MaintenanceStatusEv (3E7) have
owner confirmation on the vehicle. The shared DBC now records field-level
OWNER_CONFIRMED status; see ../dbc/README-info.md and its owner-validation JSON.
Other fields retain their existing confidence. Traction SOC is still 3E0 byte0,
raw percent times10 in the store; 3E7 is service data, not SOC.
