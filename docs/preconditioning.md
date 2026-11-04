# Immediate preconditioning: capture and implementation

The [weekly-program editor](preconditioning-programs.md) now exposes the same
stored slots without changing the immediate Start/Stop semantics below.

2026-11-03. `logs/info_precond.csv` contains 36,565 frames, 100 IDs and
149.039358 seconds. The owner woke BSI with eCMPilot before using MyOpel. The
owner confirms app acknowledgement, not a physical heating/ventilation check.
Times below are relative to the first captured frame, not wall-clock times.

| Time (s) | CAN ID | Payload | Observation |
| ---: | --- | --- | --- |
| 22.388686 | 31B | C0 58 01 C2 00 07 FF | Start: request bit 6 in byte 0 is 1 |
| 22.389625 | 11B | 00 00 07 FF 00 07 FF | Companion request with programs 3/4 |
| 22.804201 | 2E0 | 84 58 01 C2 00 07 FF | ASAP accepted, feedback still inactive |
| 28.804328 | 2E0 | 8C 58 01 C2 00 07 FF | BSI reports active |
| 85.495114 | 31B | 80 58 01 C2 00 07 FF | Stop: request bit 6 is 0 |
| 85.505215 | 11B | 00 00 07 FF 00 07 FF | Companion request |
| 85.803940 | 2E0 | 88 58 01 C2 00 07 FF | ASAP cleared; BSI still reports active |
| 86.803864 | 2E0 | 80 58 01 C2 00 07 FF | BSI reports inactive |

Start-to-ASAP acknowledgement is 415.515 ms; Start-to-active is 6415.642 ms.
Stop-to-ASAP-clear is 308.826 ms; Stop-to-inactive is 1308.750 ms. `31B`/`11B`
gaps are 0.939 and 10.101 ms. There is no `394` in this capture; all 674 `3A9`
frames are `00 00`. This is evidence for the observed transaction, not proof
that every gateway or network state can dispense with a keepalive.

## Frame mapping and program preservation

`31B` is a seven-byte immediate-preconditioning/weekly-program request. Byte 0
uses availability bit 7 and ASAP target bit 6. Bytes 1-6 carry programs 1/2.
`11B` carries programs 3/4. BSI reports those program bytes in `2E0` and `0E0`.
The controller reads fresh raw copies of both status frames and preserves all
six program bytes in each request. The captured templates are examples, not
hardcoded schedules. A missing/stale program image prevents transmission.

For `2E0`, availability is bit 7, ASAP is bit 2 and feedback is bits 6-3 of byte 0.
Only feedback 0 inactive and 1 active are observed here. The header matches the
older source `002` layout; the canonical upstream `2E0` instead calls bits 6-0
unused. A scoped vehicle-review overlay fixes the exported DBC and preserves
the upstream source JSON. Candidate rejection codes 4-7 are handled as a generic
failed request; their individual door/battery/fuel/session descriptions have
not been validated on this car.

## Control architecture

The Android Start/Stop button creates `climate.set` with `enabled: true/false`.
UDP validates the command, revision and idempotency before committing
`request.vehicle.*`. The CPU1 controller runs SetPreconditioning through
`PT_SPAWN`, sharing **c.tmr** with WakeBsi and SubmitFrame. RX only publishes
observations. TaskCan alone transmits, through the existing raw varstore mailbox.

The sequence waits for settled BSI Normal, acquires fresh coherent program data,
sends 31B followed by 11B, and observes new 2E0 reports. It does not call the charging
refresh preamble. The normal 20 ms controller cadence makes the companion send
roughly one tick later; exact vehicle acceptance of that timing still needs a
GW test. The captured sub-tick gaps are not treated as a proven deadline.

At most 10 wakes and 3 request pairs are allowed. A missing ASAP acknowledgement
can trigger a bounded retry; an acknowledged ASAP request is not restarted while
waiting for active/inactive feedback. After the retry budget, a late response
can still complete the same command. The entire preconditioning operation has a
30-second deadline, with a 35-second UDP receipt guard. Driver submission and
ASAP acknowledgement alone are not reported as successful completion.

The app shows the BSI-reported state throughout, with cached-data freshness.
The button is disabled while its command is pending, until an answer/failure or
the 30-second timeout. The earlier proposed fixed 10-second lockout is not used.
Existing experimental-control opt-in and capability advertisement apply. Failed
and unconfirmed commands remain visible; successful confirmations auto-dismiss
after 3 seconds. Connection/polling never creates a preconditioning request.

## Temperature search

The source definitions on this INFO bus contain external and evaporator
temperatures, but no named measured cabin temperature. The external conversion
is `raw * 0.5 - 40 C`: both 0F6 temperature channels are mostly raw 93/0x5D,
matching 6.5 C. Filtered values also contain raw 92/6 C and startup unavailable 255.
`350`'s evaporator bytes are FFFF throughout this capture, so they are unavailable.

A bounded byte-level search tested 0.5 C/count with -40 offset, 1 C/count with -40,
and 0.1/0.5 C/count without offset near the cold-start reference. The apparent
matches mostly come from VIN ASCII, fixed trip/consumption data or flags. No
credible cabin-temperature signal was promoted. `2AD` byte3 changes through
nibble pairs 32,43,54,65,76,87; interpreting it as 0.1 C happens to produce a ramp,
but does not establish temperature and contradicts its current switch-state
description. It remains unbound. Packed/16-bit fields or unobserved bus traffic
are not excluded by this byte search. No cabin value is synthesized from outside,
setpoints, evaporator data or the fact that preconditioning was requested.

## Reproduce and validate

The separate [remaining-energy comparison](info-energy-preconditioning.md)
investigates the owner's cloud update from about 3 to 19.88 kWh after Start.
It identifies no supported new INFO energy decoder.

Run `python tools/analyze_preconditioning.py` to regenerate the adjacent capture
JSON. Native host tests exercise the real codec/varstore/PTs: delayed feedback,
program preservation, stale/missing inputs, Force isolation, bounded retries,
30-second timeout, passive reads and source profile separation. No hardware CAN
interface is opened by these checks. Build success and status acknowledgement
do not establish the physical HVAC result; the new GW transmitter awaits a
vehicle test.
