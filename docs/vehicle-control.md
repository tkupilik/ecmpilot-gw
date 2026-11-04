# Explicit vehicle refresh and charge control

2026-11-03. Protothread implementation is ready for vehicle testing. Captured acceptance of
deferred mode is evidence; independent GW transmission, immediate mode and the
sufficiency of 394 alone still need a controlled vehicle test.

## Passive connection and cached data

`info.get`, `state.get` and `command.get` never request CAN TX, including opening
the phone app, reconnecting, polling a pending action and HA sensor publishing.
The gateway returns the last valid observations in varstore with their original
CAN receipt ages. Transport replies never refresh those timestamps. An optional
[automatic refresh timer](auto-refresh.md) defaults off; explicitly enabling it
adds timed requests to the same sequence. Passive reads never create those
events. No automatic BSI keepalive or network-maintenance frames are generated.

Values remain in RAM during vehicle standby and network reconnects; they do not
survive an ESP restart/deep-sleep reboot. Invalid/sentinel observations stay
unknown. A CAN receipt age does not establish when the source ECU last measured
the battery: repeated cached source values cannot be distinguished without a
source timestamp/counter. No charge-current/power/cable state is invented from
the schedule mode, plug lamp or BSI Normal.

Android combines battery and charging in one card, dims stale values, shows SOC
age in hours:minutes and the stored deferred setting/time. Fresh deferred SOC is
blue. The horizontal switch reflects observed mode; a tap sends a request rather than
optimistically changing it. Editing uses the existing charging sheet. Availability
comes from BSI160; the real backend rechecks a fresh raw schedule before TX.

## UDP operations

`vehicle.refresh` is a write operation with empty `args`, current boot,
`deadline_ms` (at most 5000 ms ahead), `domain_rev:0`, client ID and command ID.
It uses the same command receipts and `command.get` cache as other writes.
Retries with the same client/ID return the original receipt without another
execution; conflicting reuse is rejected. New explicit actions are rejected as
busy while one physical action is pending. Automatic polling remains passive.

`charge.mode.set` keeps its existing contract: `mode: immediate`, or `mode:
deferred` with `start_local: HH:mm` and `timezone: Europe/Prague`, plus the current
charge domain revision. This is vehicle wall-clock time, not a UTC timestamp.

Capabilities use `control_backend: info-can-experimental`, `verified:false` and
`confirmation:true`. Android explicitly understands this backend; charging still
requires the existing experimental-controls opt-in. Explicit Refresh is allowed
without fresh vehicle data or that charging opt-in, and its side effect is stated
beside the button: **refresh can activate charging**. Unsupported/older gateways
do not acquire a refresh capability merely because an app button was added.

`charge.mode_age_ms` and `charge.schedule_available` are optional additions to
`state.get`. `start_local` is now also returned in immediate mode when known,
since BSI retains a preset time. `charge.age_ms` continues to describe actual
charge-flow/status data; unknown physical state does not invalidate a known
schedule. All replies remain within 1024 bytes; existing optional metric pruning
still applies. Old clients can ignore the new fields.

## Protothread hierarchy and shared countdown

The CPU1 application process schedules the controller every 20 ms through a
separate runtime timer signal. Existing 100 ms derived-data, PM and heartbeat work
keep their previous cadence. No new FreeRTOS task or stack is allocated.

```text
RunRequest
  RefreshVehicle / SetChargeMode
    WakeBsi
      SubmitFrame
    SubmitFrame
```

Every child runs through `PT_SPAWN`. This hierarchy shares **one `c.tmr`** in its private context, not a
timer per helper. Its owner subtracts actual elapsed milliseconds each tick:
`c.tmr > tick ? c.tmr -= tick : c.tmr = 0`. Delayed ticks saturate at zero and never
cause a burst of missed transmissions. Waits use the owner's preferred form:

```c
for (c.tmr = duration_ms; c.tmr; ) {
    if (expected_condition()) break;
    PT_YIELD(&child_pt);
}
```

A parent suspended in `PT_SPAWN` does not use/reset `c.tmr`; its child owns the
shared countdown until returning. SubmitFrame leaves its unspent countdown to
WakeBsi, preserving 100 ms wake cadence including the submission wait. Separate
`operation_tmr` is the whole-request watchdog and is never renewed by a retry.
All persistent counters, targets and PT continuations live in the controller's
private context. There are no blocking sleeps, new mutexes or UI/RX callbacks.

## Limits and state transitions

| Step | Limit / completion condition |
| --- | --- |
| Wake | At most 10 wake intents total per operation, 100 ms apart while asleep |
| BSI ready | Raw036 Normal, age <= 300 ms, continuous 200 ms settling and a newer 036 RX generation during that interval |
| TX hand-off | 200 ms; wake hand-off shares its 100 ms cadence window |
| Refresh | At most 3 total 394/10 intents, each with 3000 ms response wait |
| Charge mode | At most 3 total 35B intents, each with 1000 ms response wait |
| Preamble | At least 460 ms after successful 394 submission before 35B |
| Entire operation | 25000 ms for refresh/charge, 30000 ms for preconditioning; UDP guard 35000 ms |

Wake defaults to telematics MDS43C/DLC2 [00 80]. Force can configure401/43C and
the packed two-byte payload; 5DF never has a transmitter. Fresh Normal stops wake
transmission immediately, including a final TaskCan recheck before submission.
No command is released by a cached/forced Normal or a single startup sample.
If BSI drops out during a response wait, the hierarchy re-enters WakeBsi using
the **remaining** wake/refresh budget. Ten is a whole-operation limit; it is not
reset on each child invocation. A consumed wake intent suppressed by a newly
received Normal may result in fewer than ten actual driver calls.

Refresh requires a raw valid SOC observation with a new 3E0 RX generation and
receipt timestamp at/after 394 submission. Charging preamble instead requires a
new valid 160 schedule. Timestamp alone cannot distinguish two observations in
the same millisecond, so RX generations are committed after decoded fields.
Malformed frames do not commit; invalid payload samples commit but cannot satisfy
a valid-value predicate. Force never substitutes for raw physical readback.
These conditions establish new CAN reception, not the source ECU's measurement
age or a unique transaction ACK.

First 35B encodes from fresh available BSI160. Preserve reserved/target flags;
deferred replaces mode/time, immediate clears mode and preserves stored time.
A lost response retries the **same prepared target**, without sending another
394 while BSI remains ready. After a genuine readiness loss, refresh the
preamble before resuming. Success needs a subsequent raw 160 matching availability,
mode and time; unrelated reserved/target flags are not an acknowledgement key.
31A stays a secondary diagnostic observation. Physical charge flow/current/cable
state is never inferred from this mode confirmation.

## Process and TX ownership

UDP on CPU0 validates/deduplicates and commits `request.vehicle.*`; passive reads
are silent. The CPU1 vehicle PT owns sequencing and publishes `control.tx.*`.
Only TaskCan transmits: **one physical mailbox path handles wakes and commands**.
`can_wake.c` is now configuration/encoding only; the old event-counter transport
and independent OnWakeTx sender are removed. Existing cumulative wake counters
remain available; legacy-named UDP wake count is now just a PT diagnostic.

The mailbox has a generation, a raw pending authorization and 200 ms maximum age.
Every hand-off timeout or terminal outcome revokes pending authorization. Late
callbacks cannot complete a newer generation. TaskCan checks raw values,
profile, replay/test/suspend state, phase, identifier/DLC/payload and readiness.
Frames remain standard, non-RTR and single-shot, with no driver TX queue backlog.
`ESP_OK` is driver submission, not a BSI/VCU acknowledgement. Missing wake ACK
does not prevent observation of Normal or another bounded wake attempt.

Varstore exposes `control.timer_ms`, `control.remaining_ms`, per-operation
`.wake_attempts`, `.refresh_attempts`, `.mode_attempts`, `.phase`, `.result`, and
transport `.tx.*`. Results distinguish wake, hand-off, reply and overall timeout,
transport failure, configuration, moving vehicle and unavailable backend. UDP
receipts expose `bsi_wake_timeout`, `can_tx_timeout`, `can_tx_failed`,
`vehicle_reply_timeout`, `operation_timeout` and corresponding explicit failures.
Retries are confined to the accepted operation; terminal unknown never starts a
new operation by itself, and duplicate UDP requests do not reset its budgets.

WakeBsi and SubmitFrame are shared by the charge and preconditioning PTs.
Immediate preconditioning now uses SetPreconditioning with the same children and c.tmr;
see [capture, feedback and30-second limit](preconditioning.md).
HSCAN1 stays listen-only; replay/test mode and CAN suspension inhibit all real TX.

## Capture evidence and test procedure

`logs/info_start_odlozeni.csv` contains 394/10 at 51.757635 s, 35B/C0 B4 at 52.217149 s,
31A reflecting deferred 03:00 at 52.340259 s and 160/C0 B4 at 52.691080 s. The 394-to-35B
gap is 459.514 ms; the previous time-change capture has 460.097 ms. Exact preamble
necessity and timing tolerance are not established.

At the vehicle, first verify opening/reconnecting the app produces no wake TX.
Then explicitly Refresh while asleep: inspect bounded 43C retries, settled Normal, 394 and
SOC age. Observe whether charging starts. Enable deferred 03:00 and compare 35B
C0 B4 and subsequent 160/31A; then independently validate returning to immediate.
Operate one telematics client at a time while testing these MDS IDs. The new
firmware has not been uploaded or exercised against a vehicle by these tests.

`tests/test_vehicle_control.py` exercises the actual C codec, policy and native
varstore, including all 240 BSI160 samples from the capture, stale/forced readback,
bounded wake/re-wake budgets, shared countdowns, passive polling, duplicate/busy
requests, delayed ticks, submission errors, timeouts,
sentinels, replay inhibition and preserved payload flags. Host tests use no CAN
adapter and transmit nothing to the vehicle.
