# Manual power policies and CAN-only deep-sleep test

Firmware version0.5.0, updated2026-11-01. The GW now uses ESP-IDF PM/DFS and
tickless idle, adapted from lcd_pwr_tst. **Normal/DFS/light policies keep CAN running.**
No vehicle state, incoming UDP request or CAN handler changes power policy.

Vehicle-side BSI sleep and telemetry loss are recorded separately in
[car_lowpower.md](../doc/car_lowpower.md). AC charging does not guarantee that
HSCAN1 or INFO remains available.

## Web controls

Home -> Power measurement provides three volatile profiles:

| Profile | CPU policy | Automatic light sleep |
| --- | --- | --- |
| Normal (startup default) |160..160MHz | Disabled |
| DFS only |40..160MHz | Disabled |
| Light sleep |40..80MHz | Permitted, subject to driver locks |

This is automatic idle sleep, not a blocking esp_light_sleep_start call from an
HTTP handler. Wi-Fi/UDP/web remain running. Wi-Fi keeps WIFI_PS_MIN_MODEM.
The actual minimum CPU frequency can be higher than the selected minimum due
to active driver locks. In particular TWAI uses the APB clock and retains its
ESP_PM_APB_FREQ_MAX lock for the **whole installed driver lifetime**. That lock
prevents actual light sleep and keeps APB timing stable for500kbit/s CAN.

Selecting Light sleep therefore does **not** currently mean the chip sleeps.
The CPU ceiling becomes80MHz, but successful sleep counters should remain zero
while the TWAI lock is held. The web explicitly displays this distinction.
The separate manual deep-sleep test below suspends reception and uninstalls TWAI
through its owner. These three CPU policies do not change CAN lifecycle.

Requests go HTTP CPU0 -> raw varstore -> existing application process CPU1.
Application waits1.5seconds after observing a request before applying it. A second
request is rejected while one is pending. Configuration failure preserves the
previous applied profile and publishes the error. Force overlays on diagnostic
or request signals cannot trigger or block transitions. Reboot returns to Normal.

## Diagnostics and measurement

Filter Varstore for power.* or request.power.*. Existing IDs are preserved;
Power, owner hand-off and wake diagnostics are appended without renumbering existing IDs. GET /api/power reports raw applied policy,
request/completion generations, last error, CAN build/state and sleep counters.
POST /api/power accepts mode strings normal, dfs, light_sleep or can_deep_sleep.
The last is the manual CAN-only deep-sleep test. Same-origin checks apply as elsewhere.

Successful-sleep counters use PM entry/exit callbacks and the actual sleep-request
result, through the pinned IDF5.3.2 CONFIG_ESP_SLEEP_DEBUG context. Rejected/skipped
attempts are excluded. Cumulative call time includes sleep transitions, not pure
residency. Sub-ms time is accumulated before converting to ms; displayed counters
wrap at2^31. This private SDK diagnostic API needs review when upgrading IDF.

For initial measurements use the same12V supply, disconnect USB and compare
Normal, DFS only and the Light-sleep policy with no web/log/GVRET polling during
the settled interval. Refresh afterwards to read results. Record board hardware
modifications, AP/STA state and CAN activity. These profiles do not remove the
board's regulator/isolation overhead, and no new current figures are claimed.

## Manual CAN-only deep sleep

Home -> Power measurement -> **Deep sleep · CAN wake** requests mode3 through
`POST /api/power` with `{"mode":"can_deep_sleep"}`. There is no inactivity timer,
48-hour rule, automatic CAN trigger or periodic wake-up. Normal startup is unchanged.

The HTTP handler publishes a raw varstore request. CPU1 application observes it
and waits1.5seconds for the response before coordinating these stages:

1. `request.can.suspend=1`: CPU1 CanTask stops its alert reader, stops/uninstalls
   TWAI and publishes the result followed by `can.suspend_completed=1`. Only
   successful uninstall permits the next stage; STOP alone retains the PM lock.
2. `request.network.suspend=1`: CPU0 Network stops Wi-Fi (STA and setup AP), clears
   connection state and prevents scanning/reconnect. It publishes the result and
   `network.suspend_completed=1`. HTTP/UDP/GVRET/MQTT worker tasks need no heap
   teardown before deep sleep; Wi-Fi is off and deep sleep discards their state.
3. CPU1 checks raw owner acknowledgements, disables all existing wake sources and
   enables **EXT0 LOW on GPIO16**, the existing CAN RXD path. It holds TX GPIO15
   HIGH/recessive and enters deep sleep. No timer wake source is configured.

RXD already LOW aborts preparation and restores operation. Activity starting
later may cause an immediate wake instead. Hand-off errors/timeouts also cancel
entry and request CAN/network restoration. Each stage has a5second diagnostic
limit; failed recovery is reported without retrying sleep. An owner that has not
acknowledged resume blocks another deep request. No unsupported PM lock bypass
or remote CAN frame is used to put ESP to sleep.

Requests/results use raw producer data throughout: Force overlays cannot trigger
sleep, fake an owner acknowledgement or prevent restoration. Owner completion is
published last, so an old result cannot be consumed as a new hand-off result.

### Wake and hardware requirements

Keep the transceiver and the RXD path (including any remaining digital isolator)
powered. This test does not switch the transceiver supply or Silent/Standby pins.
GPIO16 is an ESP32-S3 RTC GPIO and can wake from deep sleep. TX hold is released
at the next boot after configuring its digital output HIGH, before TWAI starts.

Wake performs a new boot, initializes CAN and rejoins Wi-Fi; volatile Force/demo
state is lost. The log shows `DEEP_SLEEP` and `Wake cause: 2 (CAN RX)`. The retained
previous tail includes the final entry diagnostic. The frame that triggers wake,
and subsequent traffic during boot, may be lost. Pulse-width/wake reliability
must be tested on this board; stock TJA1051 has an ordinary RXD output rather
than a latched standby wake indication.

While asleep, **phone, web and HA cannot reach or wake GW**. CAN activity is the
only configured wake source; physical reset/power cycling can still restart it.
A quiet bus is needed for a stable current measurement. A stuck-low RXD may abort
entry or cause an immediate boot. The owner measured approximately6.3mA at12V after selecting deep sleep, compared
with22mA at fixed160MHz and20mA with variable frequency, on the board with the
isolated DC/DC removed. See the [measurement table](../doc/car_lowpower.md#gateway-hardware-and-measured-consumption).
CAN wake-up remains untested until the vehicle trial.

### Diagnostics and test procedure

`GET /api/power` includes `deep_stage`, `can_suspended`, `network_suspended`,
`wake_cause` and normal request/completion/error fields. Varstore additionally has
`can.suspend_error`, `network.suspend_error` and both `*.suspend_completed` fields.
`power.deep_stage`:0 idle,1 stopping CAN,2 stopping network,3 entering sleep,
4 restoring after an error. Successful entry never returns a completed HTTP
status; the browser reports intentional loss of reachability without claiming
that entry was physically measured.

1. Upload `build/ecmpilot_gw.bin` through OTA and let it reconnect.
2. For a12V consumption measurement, disconnect USB and let vehicle CAN go quiet.
3. Press **Deep sleep · CAN wake**. After the short hand-off delay Wi-Fi disappears.
   Measure the settled12V input current; a small or absent drop alone does not
   establish which sleep state was reached.
4. Wake the car / generate CAN traffic. Wait for GW Wi-Fi to return and refresh
   Home/Log. Check `DEEP_SLEEP`, the CAN wake cause and normal live CAN reception.
5. Repeat while watching for immediate wake on a busy bus and clean reconnection.

## Verification

Native tests execute the production policy and varstore with the hardware PM
boundary substituted: startup, delay, pending requests, failed configuration,
Force isolation, generation rollover, owner completion ordering, hand-off errors/timeouts,
refused entry and restoration. The deep-entry hardware boundary is substituted. Browser tests
exercise the actual embedded controls, pending/error states and CAN sleep warning.
The ESP target build verifies the real PM/sleep API integration. The owner has now measured board current in Normal, variable-frequency and deep-
sleep modes; CAN wake-up and successful sleep/wake cycling still require the
vehicle test.
