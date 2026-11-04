# INFO CAN requests

Current state: **2026-11-04**, owner's Opel Corsa-e. All identifiers and payload
bytes below are hexadecimal. This is the reference for requests; implementation
details are in [Vehicle control](vehicle-control.md). The default GW connection
is **INFO CAN, 125 kbit/s**, not OBD HSCAN1.

## Implemented operations

| Operation | Request | Expected observation | What success establishes |
| --- | --- | --- | --- |
| Wake BSI | `43C`, DLC 2, `00 80` by default | `036`, byte 4 bits 2..0 = 1 (Normal), fresh and settled | BSI reports Normal; not that every ECU or cellular connection is ready |
| Refresh vehicle data | `394`, DLC 1, `10` | A new valid `3E0` SOC reception after submission | New CAN reception; no cloud-upload acknowledgement |
| Immediate / delayed charging and start time | `35B`, DLC 2, prepared from fresh `160` | New `160` matching availability, mode and requested time | Vehicle schedule accepted; not a measurement of charge current |
| Start / stop preconditioning | `31B` + `11B`, both DLC 7 | New `2E0` ASAP acknowledgement and active/inactive feedback | BSI reports the requested state; not a measured cabin temperature |
| Edit four preconditioning programs | `31B` + `11B`, both DLC 7 | New matching programs from both `2E0` and `0E0` | Native vehicle programs read back, not a phone/cloud timer |

The owner has observed working wake/refresh and uses preconditioning. The
recordings corroborate charge-time changes, delayed mode and preconditioning
Start/Stop feedback. This does not validate every source-defined flag or every
vehicle condition. Observations and source-only candidates are kept separate below.

## Wake: `43C`

The controller sends the telematics-style wake payload `00 80` only when BSI is
not ready. There are at most **10 wake intents per operation**, **100 ms apart**.
Ready means raw `036` Normal no older than **300 ms**, sustained for **200 ms**,
with another `036` reception during settling. A forced/cached Normal does not
release a command. BSI readiness loss can re-enter wake within the same budget.

`401`, DLC 2, `00 02` is the other owner-observed INFO wake path. For controlled
experiments, the ID and two bytes can be overridden through varstore; see
[wake configuration](info-can-wake.md). This does not establish that the same
cause-bit layout applies to both IDs. GW does not transmit cluster ID `5DF`.

Opening/reconnecting the phone, polling, MQTT publishing and SavvyCAN connection
are passive. Only an explicit action or enabled GW automation starts a sequence.
HSCAN1 wake attempts have not succeeded on this vehicle.

## Refresh: `394 [10]`

Source name: `MDS_RemoteActionRequest2`. Byte 0 bit 4 is
`RemoteSendChargeDataRequest`. Both **phone Refresh and automatic refresh use
the same sequence and the same CAN request**, including wake when necessary.
There are at most **3 refresh attempts**, each with a **3 s** response wait;
the whole wake/refresh operation has a **25 s** deadline.

Completion requires a raw valid SOC sample with a newer `3E0` RX generation
and a receipt timestamp at/after request submission. The value need not change.
That proves reception, not the age of the source ECU's underlying measurement.
For the charge-mode preamble, the expected new observation is instead `160`.

**Owner-confirmed, 2026-11-04:** standalone refresh updates GW/phone data but
does **not** update the cloud in the tested scenario, including repeated refreshes.
Changing the stored charging start time does update the cloud; see `35B` below.
Finishing the sequence does not send a vehicle sleep command.

## Charging: `35B`

Source name: `MDS_ChargeSchedRemote`; status: `BSI_ChargingSchedule` (`160`).
Treat the two bytes as a big-endian word:

| Bits | Meaning / handling |
| --- | --- |
| 15 | Schedule availability; obtain from fresh vehicle status |
| 14 | 0 immediate, 1 delayed |
| 13..11 | Reserved/target flags; preserve from status, no verified native SOC cap |
| 10..0 | Vehicle-local minutes after midnight, 0..1439 |

Examples: `80 B5` = immediate with stored 03:01; `C0 B4` = delayed at 03:00.
The vehicle retains a stored time even in immediate mode. Do not interpret the
low bits as energy or convert this wall-clock time into UTC.

The app saves the edited time with either switch position. An immediate-mode
request with `start_local`/`timezone` changes the stored time while keeping delay
off; it does not require active charging. An immediate-mode request without a
time preserves the vehicle's stored time. Confirmation checks the requested
time as well as mode, so unchanged mode alone cannot falsely confirm an edit.

Sequence: wake/settle → `394 [10]` → obtain fresh `160` → wait until at least
**460 ms after 394 submission** → send `35B` → wait for matching new `160`.
There are at most **3 mode attempts**, **1 s** response wait each, within the
same **25 s** operation deadline. A lost reply retries the prepared target;
a real BSI readiness loss requires the preamble again. `31A` supplies secondary
schedule observations, not the primary acknowledgement.

**Owner-confirmed, 2026-11-04:** changing the stored start time through the
corrected app/GW works and **also updates vehicle data in the cloud**, including
when the vehicle is not charging and delayed charging is off. The observed
working path is the sequence above (wake if needed, 394 preamble, then 35B time
change). This does not establish that a standalone 35B or resending the unchanged
time triggers an upload. Matching 160 confirms the stored setting; cloud update
was checked separately by the owner, not acknowledged by that CAN frame.

Actual charging indication comes from **`396` ChargingHmiState**, corroborated
against captures. Schedule mode, BSI Normal, the plug indication and source
`31A` charging-type fields are not substitutes for actual charging feedback.
The source `3E1` charge flag contradicts confirmed charging and is kept unknown.

[Charge limit](charge-limit.md) is a GW software feature: at/above target SOC
while charging, it requests delayed mode through this same sequence. There is
no verified indefinite Stop or native BMS charge-limit command. Charging may
resume at the vehicle's stored time.

## Preconditioning: `31B` + `11B`

`31B` byte 0 bit 7 is availability; bit 6 requests immediate preconditioning.
Start uses `C0`, Stop uses `80`, followed by six bytes preserving native programs
1/2. `11B` starts with `00` and carries programs 3/4. These are a request pair;
do not replay the captured owner's schedule as a hardcoded template.

The controller wakes/settles BSI, reads fresh coherent `2E0`/`0E0`, sends the
pair and waits for new feedback. **It does not send 394.** `2E0` byte 0 bit 2 is
ASAP acknowledgement; bits 6..3 are feedback (observed 0 inactive / 1 active).
ASAP acknowledgement alone does not complete Start/Stop.

Limits: **10 wake intents**, **3 request pairs**, **1.5 s** to obtain programs
or await ASAP acknowledgement, and **30 s** for the whole operation. Once ASAP
is accepted, wait for state feedback rather than repeatedly restarting it.

For weekly programs, each three-byte slot has enable bit 23, weekday bits
22..16 (Monday..Sunday), reserved bits 15..11 and local minutes in bits 10..0.
Program edits preserve reserved bits and ASAP state. Both status frames must
read back the target. See [preconditioning](preconditioning.md) and
[program encoding and conflict checks](preconditioning-programs.md).

## Telematics traffic: observe, do not take over

These identifiers already belong to other units. A source definition is not
permission to make GW transmit another unit's periodic frames.

| ID | Known role / observation | Limit of interpretation |
| --- | --- | --- |
| `3A9` | `NetworkMaintainRequestMdc`; `04 00` holds the CAN network awake in MyOpel refresh captures | Not proof of GSM registration or a cloud-upload command |
| `53C` | Telematics supervision/network-management state; `08 00 ...` observed | Counters/state concern CAN supervision, not a verified cellular-ready signal |
| `3D6` | Periodic tele-diagnosis request; reviewed captures contain `00` | Source defines a function-request field; no demonstrated cloud-update action |
| `369` | Source-defined telematics HMI/connectivity status | Not observed in the reviewed captures; no verified cloud-online flag |
| `2A9` | Source-defined connected-phone status | Not a verified status of the vehicle's cellular modem; not observed in the reviewed captures |

Preconditioning Start/Stop capture `info_precond.csv` contains no `394`; all
`3A9` samples there are `00 00`. The owner observes normal cloud updates while
preconditioning keeps INFO awake. An event-triggered upload or a broader awake
vehicle state is plausible, but the responsible mechanism is **not identified**.
Changing the stored charging time is now an owner-confirmed path to a cloud data
update, unlike standalone refresh. The exact internal upload mechanism and a
separate upload-only command remain unidentified.

## Source-only alternatives, not implemented commands

| ID | Source description | Current status |
| --- | --- | --- |
| `394` bits 7..5 | Remote lock/unlock selection | No vehicle validation; GW does not implement it |
| `25B` + `29B` | Local-head-unit preconditioning programs | Not observed in the reviewed captures; not used |
| `2DB` | Local-head-unit charge schedule | Not observed; not used |
| `254` | Legacy remote actions over a transport layer | Application transaction not established; not used |
| `314` | `REMOTE_TELEMATICS_ORDER`, segmented/opaque command payload | No validated action opcodes; not used |

The INFO UDS experiments returned no matching ECU reply, including with the
vehicle awake. Test endpoints/buttons have been removed. `7A4` remains raw
unknown; its incidental `22 ...` bytes do not establish UDS framing.

## Sources and confirmation boundaries

Layouts derive from pinned PSA-RE revision
`67f7581e7622410efdbb29c7eda3b86982b9b450`, INFO-DIV only; see
[DBC provenance and confidence](../dbc/README-info.md). MDS/BSI labels are source
attributions, not independent identification of a physical sender.

| Evidence | Supports |
| --- | --- |
| `logs/info_apka.csv` | MyOpel 394 preamble, 35B/160 time change 03:02 → 03:01 |
| `logs/info_start_odlozeni.csv` | Refresh then delayed 03:00 request/readback; about 460 ms preamble |
| `logs/info_precond.csv` | 31B/11B Start/Stop and 2E0 feedback; four stored programs |
| `logs/info_soc57_refresh.csv` | SOC/range reference: 57%, 120 km |
| `logs/info_charging.csv` | Charging-HMI comparison, energy search without a supported energy decoder |
| Owner GW logs/tests; 2026-11-04 confirmation | Wake/refresh promptly updates GW/phone, not cloud; successfully changing the stored charging time also updates cloud data |

`ESP_OK` in a TX log means driver submission. A GVRET local TX echo has that same
limit: neither proves physical ACK, vehicle acceptance or a cloud transaction.
Only the operation-specific raw readback can complete a controller request.
