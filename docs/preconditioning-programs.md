# Native preconditioning weekly programs

2026-11-03. This extends the immediate Start/Stop operation. It is not a phone
alarm, cloud schedule or gateway cron job. Four program slots are reported by
the vehicle in INFO `2E0` (1/2) and `0E0` (3/4), DLC 7. Bytes 1..3 and 4..6 of each
frame each form one big-endian 24-bit slot.

| Bits in slot | Meaning |
| --- | --- |
|23 | This program enabled |
|22..16 | Monday through Sunday, respectively |
|15..11 | Reserved, always preserved |
|10..0 | Vehicle-local minutes from midnight; 0..1439 valid, 2047 unconfigured |

The captured slot `58 01 C2` is **07:30, Monday/Wednesday/Thursday, disabled**.
This matches the owner's MyOpel plan. The correspondence establishes that the
schedule is broadcast by the vehicle, independently of how MyOpel retrieves it.
Source layout: pinned PSA-RE `67f7581e7622410efdbb29c7eda3b86982b9b450`,
LS.INFO-DIV/002,0E0,31B,11B. Actual programmed HVAC operation after a new write
still requires vehicle validation. No schedule is transmitted during tests.

## UDP contract

`info.fields` includes `climate_programs`. Passive `climate.programs.get` with
empty arguments returns a separate <=1024-byte envelope:

```json
{"programs":{"rev":0,"slots":[{"raw":5767618,"age_ms":100},{"raw":2047,"age_ms":100},{"raw":2047,"age_ms":100},{"raw":2047,"age_ms":100}]}}
```

The envelope header is identical to other status replies. Unknown slots have
null raw/age, never a fabricated disabled program. Polling does not wake the car.
Raw values retain source ages. A slot has no derived timestamp or timezone
conversion; its time is the vehicle-local clock.

Native INFO firmware advertises `climate.programs.set` with `verified:false`,
`confirmation:true`, `persistent_schedule:true`, using the existing explicit
experimental control opt-in. It shares the **climate** command generation and
single controller with immediate Start/Stop. It is unavailable in varstore-test
mode. Request args contain exactly two arrays, each four 24-bit integers:

```json
{"base":[5767618,2047,2047,2047],"slots":[14156226,2047,2047,2047]}
```

`base` is the original vehicle image edited by the user; `slots` is the desired
image. Enabled slots require a valid time and at least one weekday. Reserved
bit changes are rejected. Receipt idempotency compares both complete arrays.
Domain revision, boot binding, request deadline and pending-operation checks
are the same as other writes. Inputs are committed into `request.precond.*`
varstore fields before the common vehicle-request generation.

## Controller and confirmation

The existing PT hierarchy wakes and settles BSI, obtains fresh coherent native
programs, and compares them with `base`. An external edit returns
`preconditioning_programs_conflict` instead of overwriting it. A partially
applied retry can continue when each observed slot equals its base or target.

The controller sends `31B` for slots 1/2 and `11B` for slots 3/4. It preserves
reserved bits and the current ASAP flag, so editing plans does not start or
stop immediate preconditioning. No charging-refresh 394 is emitted. TaskCan
remains the only transmitter, consuming the raw varstore mailbox.

Success requires matching **new native readbacks from both 2E0 and 0E0 after
submission**, with all four slot values matching. Force cannot confirm an edit.
There are at most 10 wake intents, 3 request pairs and a 30-second global deadline.
Missing/partial readback remains unconfirmed; neither driver submission nor
locally updated targets count as vehicle success.

The mobile master switch enables all configured slots or disables all slots,
preserving times/days. Individual slot switches select subsets. Switches edit
a draft; **Apply schedules** sends it. The disabled program section is collapsed
in the phone. Current vehicle observations remain separate and refresh only
from CAN. See [CAN requests](can-requests.md) for the shared wire-level reference.
