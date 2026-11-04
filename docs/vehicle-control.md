# Vehicle-control architecture

Current state: **2026-11-04**. The wire-level reference is
[INFO CAN requests](can-requests.md); this page describes ownership, sequencing
and failure handling in the current implementation.

## Process boundary

CAN RX handlers only validate and publish observations to the native GW
varstore. CPU0 UDP validates/deduplicates explicit actions and publishes requests.
The existing CPU1 application process runs the controller every **20 ms**.
**TaskCan is the sole physical transmitter**, consuming one varstore mailbox.

```text
UDP explicit action ─┐
AutoRefresh timer ───┼─> serialized RunRequest
ChargeLimit watcher ┘      ├─ RefreshVehicle → WakeBsi → SubmitFrame
                          ├─ SetChargeMode → RefreshVehicle / SubmitFrame
                          └─ Preconditioning / programs → WakeBsi / SubmitFrame
CAN RX → raw varstore observations → readiness and readback predicates
```

`info.get`, `state.get`, command polling, reconnecting, MQTT publishing and
SavvyCAN connection never wake the car. Automatic refresh is a separately
enabled event source; default is off. The gateway sends no periodic network
maintenance frame and no synthetic cluster version `5DF`.

## Protothreads and timers

Children run through `PT_SPAWN`, sharing **`c.tmr` in private controller context**.
The owning tick decrements by actual elapsed milliseconds, saturating at zero:

```c
if (c.tmr > tick) c.tmr -= tick;
else c.tmr = 0;

for (c.tmr = duration_ms; c.tmr; ) {
    if (expected_condition()) break;
    PT_YIELD(&child_pt);
}
```

A parent suspended in a child does not touch that countdown. All state surviving
a yield, including continuations, targets and retry counters, lives in the
context. `operation_tmr` is an independent whole-operation watchdog, never
renewed by retries. There are no blocking sleeps or extra RTOS task stacks.
Parallel automation owns separate countdown state, including
`automatic.refresh_tmr`; it submits work to the same serialized controller.

| Step | Current bound |
| --- | --- |
| Wake | 10 intents total per operation, 100 ms cadence |
| BSI readiness | Raw 036 Normal, age ≤300 ms, 200 ms settling with a newer reception |
| TX hand-off | 200 ms; wake uses its 100 ms cadence window |
| Refresh | 3 attempts, 3000 ms reply wait each |
| Charge mode | 3 attempts, 1000 ms reply wait each |
| Charge preamble | At least 460 ms after 394 submission, fresh 160 required |
| Preconditioning | 3 request pairs, 1500 ms program/ASAP waits |
| Whole operation | 25000 ms refresh/charge; 30000 ms preconditioning/programs |
| UDP pending guard | 35000 ms |

Readiness loss re-enters wake using the **remaining** budget, not a new ten
attempts. Settling prevents a single startup sample from releasing a command.
TaskCan rechecks raw readiness immediately before wake/command submission.

## Mailbox and transport ownership

The mailbox carries generation, raw pending authorization and a maximum age of
200 ms. Terminal outcomes and hand-off timeouts revoke authorization. Late
callbacks cannot complete a newer request. TaskCan checks profile, phase,
identifier, DLC, payload, test mode/suspension and physical readiness.

Frames are standard, non-RTR and single-shot; there is no driver TX queue
backlog. Driver acceptance (`ESP_OK`) and the local GVRET TX echo do not mean
physical ACK or ECU acceptance. A missing wake ACK does not prevent bounded
observation/retry while waiting for BSI Normal.

Only live INFO supports these actions. HSCAN1 remains listen-only. Test mode and CAN suspension inhibit real vehicle-control transmissions.
Force can change configured wake ID/payload, but cannot manufacture a raw
physical readback or transaction success.

## Completion and cached data

The readback key depends on the operation:

- Refresh: new valid raw `3E0` SOC generation after request submission.
- Charge-mode preamble: new valid raw `160`; final mode change needs matching
  availability, mode and local time in another new `160`.
- Preconditioning: new `2E0` ASAP acknowledgement followed by requested active/
  inactive feedback; acknowledgement alone does not finish the operation.
- Programs: new matching slot values from both `2E0` and `0E0`, with conflict
  detection against the editor's base values.

RX generation is committed after decoded fields so a coherent reader cannot
mistake a partial publication for a new observation. Malformed frames do not
commit; invalid payload values cannot satisfy a valid-readback predicate.
Schedule retries preserve the prepared target; native program/reserved bytes
are preserved rather than copied from fixed capture templates.

Last known values and original CAN receipt times stay in RAM through vehicle
standby and Wi-Fi reconnects. Polling never renews them. ESP restart/deep sleep
loses this cache. Source ECUs may themselves repeat old measurements: a new CAN
receipt is not proof of a new battery measurement or cloud upload.

**Owner-confirmed, 2026-11-04:** phone and timer refreshes update GW/phone data,
but standalone refresh does not update the cloud in the tested scenario. Both
execute the same wake/394 sequence. A successful stored charging-time change
(wake/394/35B) also updates cloud data, including with delay off and no active
charging. Preconditioning is likewise associated with cloud updates. A separate
upload-only command and unchanged-time writes remain unvalidated. Controller
completion sends no vehicle sleep/cancel frame. See [request evidence](can-requests.md).

## Requests, automation and diagnostics

The UDP [correlation/idempotency contract](udp-correlation.md) binds all retries
of an action to the same session/request ID. Repeated transport attempts return
the existing receipt; they do not repeat physical work or reset retry budgets.
Explicit requests have priority over background work; terminal receipts are
retained before a background operation can reuse controller state.

[Automatic refresh](auto-refresh.md) and [charge limit](charge-limit.md) have
their own policies, but use the same physical sequences. Manual or automatic
refresh completion, including failure, reloads the selected refresh interval.
The timer only generates refresh events; it cannot independently send CAN.

Filter Varstore for `control.`, `can.wake_tx`, `auto_refresh.` and `charge_limit.`.
`control.source` is 1 phone, 2 timer, 3 charge limit. Logs carry correlation,
generation, phase, TX intent and final result. `control.timer_ms` is the shared
step countdown and `control.remaining_ms` the whole-operation remainder.
Results distinguish wake, hand-off, reply and operation timeouts, transport
failure, unavailable data/backend, moving vehicle and program conflicts.

Source: [vehicle_control.c](../main/vehicle_control.c),
[timing constants](../main/vehicle_control.h),
[TaskCan](../main/task_can.cpp). Reproducible checks are in [Testing](testing.md).
