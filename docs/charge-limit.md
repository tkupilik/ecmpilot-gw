# Charge limit

Enable automatic refresh and **Charge limit** in Android **Home → Gateway**, then
Apply. Charging now shows the target slider; releasing it saves in the gateway.
Default **80%**, range **1–100%**, initially disabled. Turning off automatic refresh
also disables the limiter. The Home web editor exposes the same settings.
The phone does not need to stay connected.

When fresh CAN observations report charging and SOC **at or above** the target,
GW requests delayed charging using the existing wake/refresh/charge-mode sequence.
It preserves the **freshly reported vehicle start time and reserved bits**. A
separate indefinite Stop command is not established, so the car can resume at its
stored start time. The limiter remains enabled for later observations. Disabling
it or raising the target never automatically starts charging.

This is a sampled software limit, not a native BMS charge cap: refresh period,
SOC quantization and vehicle response permit overshoot. A matching `160` readback
confirms delayed mode, not physical stopping. Charging state remains the actual
`396` HMI observation. A real vehicle test is still required.

## Controller

Parallel `ChargeLimit()` runs in the existing CPU1 process next to `AutoRefresh()`.
It observes varstore only; no policy runs in CAN handlers. It has its own PT/retry
countdown and requests the serialized `RunRequest()` hierarchy. Children share
`c.tmr`; TaskCan remains the only transmitter. Phone requests have priority and
their receipts are retained before background work can reuse the controller.

Eligibility requires refresh + limiter enabled, supported live INFO backend,
actual SOC/charging/source-HMI data no older than **5 seconds**, valid SOC and a
native SOC RX generation. Known low 12 V SOC pauses admission; unknown auxiliary
SOC permits it. Force cannot fabricate physical SOC/charging observations.
Replay, test mode, HSCAN1 and suspended CAN cannot transmit.

The sequence wakes/settles BSI, refreshes the native schedule, waits the existing
preamble and rechecks charging/SOC before `35B`. Lower refreshed SOC, stopped
charging, disabling or changed configuration veto further mode TX. TaskCan also
checks the configuration before consuming an already queued limiter frame.

Existing bounds apply: 10 wake attempts, 3 refresh/mode attempts and a 25-second
deadline. For unchanged settings, repeated operations require a **new SOC RX generation after completion**
and **60-second cooldown**. This bounds retries even if delayed mode is accepted
but the HMI still reports charging; an internal refresh reply cannot immediately
retrigger the watcher. Completion restarts a full automatic refresh interval.

`control.source=3`, log `source=charge_limit`. Refresh timer and limiter share a
free-running background correlation counter, zero session, scoped to GW boot.
They never impersonate a phone request generation/receipt.

## Configuration and diagnostics

`info.fields` advertises `charge_limit`. `auto_refresh.get/set` adds configuration
`charge_limit_enabled` and `charge_limit_soc_pct`; GET also returns read-only
`charge_limit_state` and `charge_limit_last_result`. These share refresh settings'
revision/NVS transaction. Omitted new fields preserve saved values; disabling
refresh clears enable. CAS and same-desired retry rules remain unchanged.
Legacy v1 NVS refresh settings retain their revision/periods/minimum, with limiter
off/80%; later saves use v2. Old firmware cannot read the new v2 configuration.

Varstore group `charge_limit.*`: enabled, soc_pct_x10, state, request_count,
last_result. States: disabled, monitoring, waiting_data, busy, requesting,
limit_reached, retry, unavailable, low_aux_battery. Result uses the controller enum.
HA receives read-only enable/target/status, for **55 entities** total.

Tests cover migration/persistence, exact threshold, stale/unknown/forced data,
wake/preamble ordering, schedule preservation, cancellation, low 12 V SOC,
explicit priority/receipts and bounded retries. Android/demo tests exercise the
switch dependency and slider save without creating vehicle commands. No physical
CAN commands or GW flash were performed during verification.
