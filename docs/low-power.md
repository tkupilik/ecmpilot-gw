# Automatic deep sleep

Current state: **2026-11-04**. The owner has confirmed that deep sleep and wake
on CAN activity work on the installed GW. Automatic entry uses the same tested
CAN/network shutdown and RX GPIO16 wake path.

## Settings and countdown

Web: **Low power** tab. Phone: **Gateway → Low power**.

| Setting | Default | Range |
| --- | --- | --- |
| Sleep when the vehicle is inactive | Enabled | On/off |
| Sleep after | 48 hours (2 days) | 1–720 whole hours (30 days) |

Configuration is saved in NVS, separate from vehicle-refresh settings. Both UIs
show the time remaining until automatic sleep, rather than elapsed inactivity.
Before the first valid CAN sample they show **waiting for the first valid BSI
message**, without a fictitious countdown. Restart/deep-sleep wake clears the
volatile CAN cache; it must receive a valid sample again before arming.

The observation is **`bsi.power_management`**, from **`BSI_RadioCommands`, INFO
`0x036`, byte 4 bits 2..0**. Values 0–4 (Standby, Normal, Entering standby,
Wake-up, Communication off) are accepted. The policy observes the age of the
last valid report, not just a particular phase. Every new valid report restarts
the full inactivity period. Phone/HA polling does not touch its timestamp.

Automatic refresh naturally keeps the observation young while it wakes BSI.
The expected battery-protection sequence is: **12 V SOC below the refresh
threshold (default 50%) → refresh pauses → BSI reports cease → after 48 hours
without a report, GW sleeps**. Low SOC alone does not immediately put GW to sleep.
The two settings remain independent; no switch silently changes the other.

Wi-Fi, the phone, web and HA are unreachable during deep sleep. Only CAN RX
activity wakes the ESP; there is no periodic timer wake or phone wake mechanism.
Initial CAN frames can be missed during reboot. A busy bus may cause immediate wake.

## Age, validity and overflow

Varstore timestamps use a **64-bit monotonic millisecond clock**. Snapshot age
is `uint32_t`, **saturated at UINT32_MAX**, about 49.71 days, without wraparound.
The 48-hour default is 172,800,000 ms. The maximum 720-hour setting is below
the saturation point. Calculations use 64 bits; published `remaining_s` fits
the int32 store even at the 30-day maximum (2,592,000 seconds).

The PT reads `varstore_inspect(..., 0)`: zero disables expiration by age, while
`raw_valid` still distinguishes a valid old observation from invalid/never
received data. Explicit invalidation disarms the policy. Force cannot fabricate
the first sample, hide actual activity or reset the real age.

## Ownership and shutdown

`AutomaticSleep` is a parallel protothread called by the existing CPU1 application
process, with no additional RTOS task/stack. It derives the countdown from the
CAN timestamp, avoiding a drifting or separately reset inactivity timer.

At expiry it submits one serialized automatic power request. It waits for current
vehicle work, pending manual power work and OTA. CPU1 power policy rechecks
eligibility during the 1.5 s preparation interval, so new BSI activity, a new
vehicle request or disabling the feature cancels entry before CAN is stopped.

The existing peripheral owners stop/uninstall TWAI, then suspend the network;
only acknowledged shutdown permits deep sleep. The CAN owner's normal close
invalidates decoded observations. Once that stop is acknowledged, the power
owner retains the admitted inactivity decision while continuing to check
configuration, vehicle work and OTA. It must not mistake its own cache cleanup
for new activity or a reason to abort a valid handoff.

Failure restores the owners and publishes an error. Retries are limited to one
per minute while eligibility remains true. If shutdown invalidated the cache
and entry failed, a new valid BSI observation is required to re-arm. Manual
power controls remain available and use the same serialized mailbox.

## Interfaces and diagnostics

HTTP: `GET` / same-origin `POST /api/low-power`.
UDP capability: `info.fields` contains `low_power`; passive `low_power.get` and
boot-bound `low_power.set` use a dedicated settings envelope, not vehicle receipts.

```json
{"rev":1,"enabled":true,"idle_hours":48}
```

Writes compare the base revision. Repeating the same desired settings is
idempotent; conflicting edits return `settings_conflict`. NVS failure preserves
active settings. Saving settings does not transmit a vehicle command.

Replies add `config_valid`, `capable`, `state`, nullable `remaining_s`,
`request_count` and `last_error`. States are `disabled`, `waiting_data`,
`counting`, `busy`, `entering`, `retry`, `unavailable`, `invalid_config`.
Varstore group: **`low_power.*`**; existing `power.*`, CAN/network suspension
acknowledgements and boot wake cause describe physical preparation/result.

Host checks use the production PT/power state machine, real varstore and a virtual
clock: two-day expiry, 100-day ages, invalid/forced observations, cache cleanup,
new activity during preparation, owner ordering, failure/retry, low-12 V refresh
pause, settings persistence/CAS and passive/boot-bound network operations.
They do not substitute for measuring current on the physical installation.
