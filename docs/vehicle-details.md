# Passive vehicle details: apertures, locks and trip data

All receive handlers publish the global varstore. Network tasks read snapshots;
there is no door/locking control, trip reset, CAN request or implicit wake.

| ID / DLC | BSI varstore group | Source layout |
| --- | --- | --- |
| `236` / 8 | `bsi.door_*_open`, `bsi.trunk_open` | Driver byte5 bit6; passenger byte7 bit1; rear-left byte7 bit4; rear-right byte7 bit5; boot byte5 bit7. Zero-based bytes. 1=open |
| `276` / 8 | `bsi.locking_state` | Byte1 bits7..5. 1 locked, 2 superlocked, 3 unlocked, 4 driver unlocked, 5 cab unlocked, 6 load area unlocked. 0/7 invalidate |
| `31A` / 8 | `bsi.trip1.average_x10`, `bsi.trip2.average_x10`, `bsi.trip.instant_x10` | Three packed 9-bit values in 0.1 kWh/100 km; source range 0..500; reserved/default codes invalidate |
| `2A1`, `261` / 7 | `bsi.trip1/2.distance_km`, `.speed_kmh` | Byte0 mean speed; bytes1..2 BE distance. FF/FFFF unavailable. Fuel fields ignored |
| `39A` / 8 | `bsi.consumption.history1_x10` .. `history6_x10` | First six packed 6-bit values, 1 kWh/100 km per raw count, stored times10. Source range 0..50; other codes invalidate |

Definitions come from the existing pinned PSA-RE INFO snapshot, with the same
attribution/license as `can_info_decode.c`. Do not confuse the configuration
flags in `2E1` with the actual locking state in `276`.

The observed `info_standby.csv` lock values transition 3 -> 1. All aperture bits
in the reviewed captures are closed, so each physical open panel still needs a
vehicle check. The driving capture trip candidates decode to 21.5/20.7 kWh/100 km,
164/2232 km and 72/88 km/h. Source decoding and plausible values are not physical
calibration; these fields have not been promoted to owner-verified DBC signals.
History slot duration/order and a dated trip archive are not known.

## UDP v0 extension

`info.fields` contains `vehicle_details`. Send the ordinary bound read request
with `op: "vehicle.details.get"`, `args: {}`. The regular envelope is unchanged;
its new `details` member contains:

```json
{
  "body": {
    "open_mask": 0, "known_mask": 31, "age_ms": 50,
    "locking_state": 1, "lock_age_ms": 20
  },
  "trips": {
    "avg1": {"value": 21.5, "age_ms": 80},
    "avg2": {"value": 20.7, "age_ms": 80},
    "instant": {"value": null, "age_ms": null},
    "distance1": {"value": 164, "age_ms": 40},
    "distance2": {"value": 2232, "age_ms": 40},
    "speed1": {"value": 72, "age_ms": 40},
    "speed2": {"value": 88, "age_ms": 40},
    "history": [50, 50, 50, 50, 37, null], "history_age_ms": 90
  }
}
```

Mask bits0..4 are driver, front passenger, rear left, rear right, boot. A missing
known bit means unknown, never closed. Body age is the oldest available panel
age; locking has its own age. History slots share the oldest valid slot age.
Both regular state and details datagrams retain the 1024-byte ceiling. Old
clients can ignore the capability; new clients do not call unsupported firmware.
Cached values remain readable with increasing ages; only source invalidation,
decoder reset or reboot clears them. Force overlays are visible for bench tests.

Five native tests exercise individual door bits, reserved lock codes, independent
ages, actual captured trip payloads, unavailable values, reset, and full-sized
cached replies at a long age. The details query does not activate vehicle control.
