# Automatic vehicle refresh

Optional [Charge limit](charge-limit.md) shares this configuration/revision and
requires refresh to remain enabled. Completion of a limiter operation also
restarts the refresh interval; the limiter itself never starts charging.

Automatic refresh is a second event source for the existing CPU1 vehicle-control
process. Its parallel `AutoRefresh` protothread owns `automatic.refresh_tmr`;
the bounded wake/refresh hierarchy still owns `c.tmr`. There is no extra RTOS
task, CAN transmitter or phone-side timer. Process inputs and diagnostics use
the gateway varstore.

| Setting | Default | Range |
| --- | --- | --- |
| Enabled | Off | On/off |
| Standby period | 60 min | 1–1440 whole minutes |
| Charging period | 5 min | 1–1440 whole minutes |
| Minimum 12 V SOC | 50% | 0–100 whole percent |

Configuration survives reboot in a separate `gateway/auto_refresh` NVS blob.
The countdown, received vehicle data and request counter are RAM state and reset
on reboot/deep-sleep wake. Enabling starts a full interval, without an immediate
vehicle wake. The feature requires the INFO CAN control profile; HS1, replay,
test mode and suspended CAN cannot perform an automatic refresh.

## Countdown and sequence completion

- Use the last known normalized `charge.state`: `2` selects the charging period;
  all other/unknown values select standby. Cached data remains usable here.
- Every control tick clamps the remaining countdown to the selected period.
  For example, 40 minutes remaining becomes 5 minutes when charging is detected;
  2 minutes remaining stays 2 minutes. Returning to standby never extends it.
- Expiry admits one `GW_CONTROL_REFRESH` event through the existing controller.
  It never selects charge mode or preconditioning.
- **The countdown pauses throughout any explicit or automatic refresh sequence.**
  Admission does not reload it. At the end of the whole sequence, success or
  failure reloads the full currently selected interval. This coalesces a manual
  refresh with the next cycle and prevents immediate retry loops after failure.
  Duplicate UDP retries do not restart a sequence or timer.
- A due timer waits while another explicit action is active. New explicit
  requests can supersede automatic work. The controller cannot reuse an
  explicit terminal result until CPU0 has retained its command receipt.
- A delayed control tick admits at most one event, without catching up missed
  periods in a burst.

Known `bsi.aux_battery_soc_pct_x10` below the configured threshold pauses the
remaining countdown. Equality permits counting. Unknown/out-of-range SOC does
not block: the next refresh may obtain the missing value, and subsequently
reported low SOC pauses future refreshes. A paused low-SOC gateway cannot
actively discover battery recovery; later CAN observations or a manual refresh
can provide it. Pausing/disabling does not cancel an already admitted sequence.

## Configuration interfaces

Web: **Home → Automatic vehicle refresh**, with explicit Save and Reload.

Android: **Home → Gateway → Automatic vehicle refresh**, with its own Apply
and Reload. Phone connection and appearance stay under the Settings gear. The phone capability-checks the
gateway and only reads settings on connection/polling. Saving configuration does
not itself request a wake. Existing gateways without this capability still work.

HTTP: `GET /api/auto-refresh`; same-origin `POST /api/auto-refresh`.
UDP v0: `auto_refresh.get` with empty arguments; `auto_refresh.set` with current
gateway identity and boot. Capability field: `auto_refresh`.

Both writes use the same five-field compare-and-set payload:

```json
{"rev":1,"enabled":true,"standby_min":60,"charging_min":5,"min_aux_soc_pct":50}
```

A changed configuration increments `rev` after NVS commit. Identical repeated
desired settings are idempotent. A differing stale revision returns
`settings_conflict` (HTTP 409); invalid/extra/duplicate fields return
`invalid_argument`; persistence failure returns `storage_error` without changing
active configuration. Web/phone editors preserve drafts and their base revision
during polling so simultaneous edits cannot silently overwrite each other.

The reply is the configuration plus `config_valid`, `capable`, `state`,
`remaining_ms`, `request_count` and `last_result`; UDP wraps it under
`auto_refresh`. These are configuration replies, not vehicle command receipts.

## Varstore and checks

`auto_refresh.*` contains configuration, countdown, state, request count and last
automatic control result. Configuration is published with revision last; the
control task reads it without a semaphore. Only the NVS settings save path uses
a configuration mutex. Force applies to inputs, charging state and 12 V SOC;
overrides last until released/rebooted and do not persist. Use whole-percent
increments (10 internal units) for the SOC threshold.

States: 0 disabled, 1 counting, 2 low auxiliary SOC, 3 unavailable, 4 busy,
5 invalid configuration, 6 refreshing. `control.source` is 1 for explicit work,
2 for automatic work and 0 before any request. Automatic work uses control
generation 0 and cannot impersonate the latest UDP receipt.

Native tests use the real PT hierarchy, varstore, NVS validation and TX mailbox.
`tests/test_auto_refresh.py` covers intervals, charge transitions, unknown/low
SOC, completion, failed wake, preemption, receipt retention, persistence/CAS and
failure, boot checks and 1 KiB responses. Web tests exercise draft/poll/save
behavior. Vehicle and physical phone operation still need an on-device check;
these tests send no live refreshes.
