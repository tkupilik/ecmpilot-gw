# eCMPilot Gateway

ESP-IDF firmware for the local ESP32-S3 gateway used by the eCMPilot Android application. Initial hardware target: Waveshare ESP32-S3-RS485-CAN. CAN uses this board's GPIO15/GPIO16. The two-channel CAN kit requires a separate hardware profile.

See [board power consumption](powe_consumption.md) for the reported 12 V measurements, schematic analysis, firmware power-management audit and proposed isolation-supply bypass evaluation.

See [vehicle sleep and CAN availability](doc/car_lowpower.md) for the observed
HSCAN1/INFO wake behavior, loss of both telemetry buses during AC charging and
BSI refusal after repeated wake requests.

[Automatic vehicle refresh](docs/auto-refresh.md) reuses the existing sequence
with persistent web/phone settings, standby/charging periods and a 12 V SOC gate.
It defaults off (60 min / 5 min / 50%). A completed manual or automatic refresh
starts the next full interval.

[Passive vehicle details](docs/vehicle-details.md) exposes door/boot openings,
locking, Trip 1/2 and consumption-history candidates through source-grouped
varstore and a separate, capability-gated phone read. No wake or control occurs.

Varstore HTTP snapshots stream through a fixed 1 KiB buffer, preserving the complete JSON export while avoiding a full in-memory JSON tree. The browser displays 14 matching signals per page.

Default varstore measurement units are mV, mA, mC (millicelsius), W and tenths of a percent. Signal names/descriptors reflect the integer storage scale; UDP keeps the phone-facing V/A/C/kW/percent units.

The first passive capture on OBD pins 6/14 is documented in [the HS1 analysis](docs/obd-hscan1-2026-10-28.md). The [database-first HS1 DBC](dbc/README.md) now covers all three captures, 89 IDs and 1,161 signals, with ECU-prefixed names and explicit exclusions; vehicle command reachability remains unverified.

The separate [INFO-DIV DBC](dbc/README-info.md) contains 377 IDs for the 125 kbit/s infotainment/telematics bus, covering all 53,971 captured frames and 115 observed IDs. The [OBC wake/cluster comparison](docs/info-obc-wake-2026-11-01.md) adds the AC plug-in wake cause and nine unit004/IC sender attributions. The [capture analysis](docs/info-can-2026-10-28.md) records DLC corrections, the 70% SOC candidate and the unresolved remaining-energy search. [PSA telematics research](PSA_telematic.md) records the independently confirmed charge-schedule time update in `0x35B` and `0x160`. Byte-level coverage does not validate every physical meaning or sender.

The [energy comparison after preconditioning](docs/info-energy-preconditioning.md) checks the cloud's update from approximately 3 to 19.88 kWh against INFO captures; no new energy field is validated.

The [MyOpel wake-up comparison](docs/can-wakeup-2026-10-28.md) covers startup traffic, missing ACK, physical CAN wake patterns and PCAN/ESP32 test options. It also records zero-filled startup telemetry that needs process-level validity gating.

For a bounded PCAN experiment, the [recorded-ID wake scan](docs/can-wake-scan.md) has hardcoded 68-ID and 89-ID profiles, a hardware reset after every attempt, RX logging and automatic stop on a wake candidate. It is separate from the firmware and previews without hardware access unless `--execute` is supplied.

The current build targets **INFO CAN at 125 kbit/s, normal mode**, with fifteen
RX handlers, explicit refresh/charge/preconditioning protothreads and a configurable wake frame, default `0x43C [00 80]`.
ID/payload can be forced through varstore; **version `0x5DF` TX is removed** to
avoid a collision with the original cluster. HSCAN1/500 kbit/s remains an optional
listen-only profile for HV battery SOC and voltage (`0x5C3`). See
[INFO wake configuration](docs/info-can-wake.md),
[CAN architecture](docs/can.md), [HS1 mappings](docs/can-telemetry.md),
[INFO mappings/replay](docs/can-rx.md) and [LCD/GW comparison](docs/lcd-backport.md).
The GPIO wake-up test has been removed from firmware, web and API; historical
research remains in the documentation.

[UDP correlation and loss recovery](docs/udp-correlation.md) follows each logical
operation from the phone through wake/CAN to its receipt. Retries preserve the
operation identity and cannot republish an accepted CAN request. The phone now
tolerates isolated lost or multi-second-late replies without an immediate reconnect.

Version 0.5.0 includes runtime, protothreads, serial console, persistent Wi-Fi
profiles/hostname, web logs, FreeRTOS task **and heap** diagnostics, two-slot OTA,
global varstore, INFO RX/configurable wake (optional passive HSCAN1 RX), phone UDP v0 and receive-only
[SavvyCAN GVRET over TCP23](docs/gvret.md). GVRET uses a static **64-frame internal-RAM
FIFO**; PSRAM stays disabled. Live is the default; local capture replay is optional.
Decoder profile and bitrate are selected at build time through menuconfig.
Optional [Home Assistant MQTT telemetry](docs/ha.md) publishes 55 vehicle entities
on CPU0. Enable it and configure its broker on the web **Home Assistant** tab;
settings persist in NVS and apply without reboot. It is disabled by default.
The optional [Charge limit](docs/charge-limit.md) depends on automatic refresh
and requests delayed charging at a configurable SOC target (default 80%).
Explicit INFO refresh and charging-mode commands use bounded protothread sequences;
see [control sequencing and timeouts](docs/vehicle-control.md). This backend is
experimental and awaits vehicle validation of the new sequencing. Immediate preconditioning uses captured31B/11B requests and BSI2E0 feedback;
see [preconditioning](docs/preconditioning.md). Ethernet drivers and GUI are not implemented in GW. All application
observations and requests use GW varstore.

Manual [power-policy controls](docs/power-management.md) are available on Home:
Normal160MHz, DFS40–160MHz and automatic-light-sleep policy40–80MHz. CAN remains
running for these three profiles; its TWAI PM lock prevents actual light sleep.
A separate **Deep sleep · CAN wake** button stops Wi-Fi and uninstalls TWAI,
then sleeps with GPIO16/CAN RX as the only wake source. No automatic timeout is
installed; phone/HA are unavailable until CAN activity causes a fresh boot.
Startup always uses Normal. See the linked measurement procedure.

## Build and first installation

Use ESP-IDF **5.3.2** (the locally verified installation), targeting `esp32s3`. The component manifest limits the project to the 5.3 series. No downloaded application components are required. The configuration uses a conservative **4 MiB flash layout**, DIO mode and no PSRAM; boards with larger flash can use this layout too.

From an activated ESP-IDF shell:

```sh
idf.py build
idf.py -p YOUR_SERIAL_PORT flash monitor
```

On this Windows workstation:

```powershell
.\dev.ps1 build
.\dev.ps1 size
.\dev.ps1 flash -Port COMx
.\dev.ps1 monitor -Port COMx
```

`dev.ps1` discovers the installed tools under `C:\Espressif`; override `IDF_PATH` / `IDF_TOOLS_PATH` or pass `-IdfPath` / `-ToolsPath`. Flashing and monitoring require an explicit port. USB Serial/JTAG is the default console, using the S3's native USB port. For a USB-to-UART bridge, select UART in `idf.py menuconfig` (the console code supports both).

First installation requires serial flashing of the **bootloader, partition table, OTA data and application**. A previously installed image with a different partition map cannot be migrated by uploading this application alone. The OTA upload file for subsequent updates is `build/ecmpilot_gw.bin`, not a merged flash image or the bootloader binary.

## CPU ownership

ESP-IDF numbers the S3's two cores **CPU0** and **CPU1**. Network on CPU0 and technology on CPU1 is the agreed split:

| CPU0 — network | CPU1 — application / future vehicle technology |
| --- | --- |
| Wi-Fi driver task | `app_main` and startup |
| lwIP TCP/IP task | `Rntm::Process` application actor |
| Default ESP-IDF event loop (`sys_evt`) | Protothread heartbeat |
| Network actor: scan / connect / retry | Serial console REPL |
| HTTP server, configuration and OTA receive/write | FreeRTOS timer service used by runtime timers, priority **24** (highest) |
| UDP socket/protocol and GVRET TCP task (priority 3) | `CanTask` acquisition/publication (priority 5), `canAlert` (priority 6) |

These affinities are explicit in task creation and `sdkconfig.defaults`, with compile-time guards for the critical settings and runtime assertions in task/event entry points. ESP-IDF's `esp_timer` infrastructure remains on CPU0; idle and IPC tasks remain per-core system tasks. Future network/Ethernet workers must be pinned to CPU0, future CAN/application workers to CPU1. Firmware flash operations can temporarily stall execution on both cores; pinning does not provide real-time isolation from OTA writes.

## Wi-Fi setup

1. Setup AP fallback is **disabled by default**, including after upgrade when no explicit AP preference exists. For a fresh device without saved station profiles, configure one through the serial console: `wifi add "Home WiFi" "your-password"`. Existing saved station profiles remain valid.
2. Open the web UI at its DHCP address (or router-resolvable hostname) and enter up to **eight** Wi-Fi profiles, in priority order. Use the arrows to change priority. Passwords may be left unchanged when editing an existing SSID; an explicit “Open network” option removes the saved password.
3. Save. The complete list is validated and committed to NVS before reconnecting. It survives resets and normal OTA updates. Saved station-network passwords are not returned by the web API or the `wifi list` command.
4. The gateway scans for visible SSIDs and selects the first available configured network. A working connection is retained even if a higher-priority network later appears. A failed candidate is held off for 60 seconds so lower-priority networks can be tried. Idle scans repeat every 15 seconds; a pending connection/DHCP attempt is limited to 20 seconds.
5. If **Wi-Fi setup → Setup access point → Enable setup AP fallback** is checked and saved, the WPA2 setup AP `eCMPilot-XXXXXX` starts when no station profiles exist or the gateway stays offline for 30 seconds. Once activated it stays available until reboot or explicit disable. With the checkbox off, neither condition starts AP. Disabling it switches an active AP off after the settings response; the station interface remains available on its configured network.

The web UI is also available at the gateway's DHCP address on the joined LAN; the address is printed on serial and in `status`. No captive portal or mDNS discovery is provided yet. Hidden SSIDs, enterprise Wi-Fi and multiple credentials for one SSID are not supported. SSIDs are limited to 32 UTF-8 bytes; passwords are empty for open networks, 8–63 bytes for a passphrase, or 64 hexadecimal digits for a PSK.

In **Wi-Fi setup → Hostname**, set the DHCP hostname (default **`ecmpilot-gw`**). Enter one label of 1–32 ASCII letters/digits/hyphens, starting and ending with a letter or digit; uppercase is normalized to lowercase. Save and reconnect applies it before the next DHCP exchange. Home/`status` display the applied hostname.

The router determines whether to register this name and which suffix to use: for example, `ecmpilot-gw.lan` can then be entered in Android. DNS records/caches may take time to update. This does not add mDNS/`.local` discovery or rename the setup AP SSID. The station numeric IP remains available; setup AP `192.168.4.1` is available only when enabled and active; the UDP gateway identity stays MAC-based.

Hostname uses a separate NVS key; the original version-1 Wi-Fi blob is unchanged. Existing profiles load without migration and older OTA images can still read them. Wi-Fi list edits/clears and older API requests omitting hostname preserve its saved value.

This initial interface is **local HTTP without authentication or signed firmware**. The setup AP uses WPA2; saved credentials are not encrypted in NVS. Browser writes reject a different `Origin`, but this does not authenticate other LAN clients. Use a trusted development network; do not expose the interface through port forwarding. These properties are recorded here so later pairing/authentication work has a clear starting point.

Under **Wi-Fi setup → Setup access point**, the persistent enable checkbox is unchecked by default and the current AP SSID/password are shown. Toggle the checkbox and use **Save and reconnect** to apply it without reboot. The flag uses its own NVS key; the original station-profile blob stays version1/792 bytes. Omitting the flag in older requests or changing Wi-Fi through the serial console preserves the saved AP policy. The password is masked until **Show password** is selected and can be replaced with 8–63 printable ASCII characters. Blank input retains the existing password. Saving an actual password change commits settings and schedules a gateway restart after the HTTP response; AP clients must reconnect using the new password. Ordinary profile/hostname edits reconnect without restarting. Existing randomly generated AP passwords remain valid, and clearing station profiles preserves the AP password.

The setup AP password is intentionally returned by `GET /api/wifi` for this configuration UI; station-network passwords remain hidden. It is not included in the buffered web log. Images predating this setting read AP passwords through a 17-byte buffer; a manual downgrade to such an image requires an AP password of at most 16 characters.

## Console

```text
help
hello
status
wifi list
wifi add "Home WiFi" "your-password"
wifi add "Open test network"
wifi remove "Home WiFi"
wifi clear
wifi hostname
wifi hostname my-corsa
restart
```

`wifi add` appends a new SSID or replaces its existing credentials. Omitting its password explicitly configures an open network. `wifi clear` removes all station profiles and enables the setup AP only if fallback was explicitly allowed; it preserves the setup AP password. Quotes are needed for SSIDs or passphrases containing spaces. Console credentials can appear in terminal scrollback and the in-memory line history; history is not persisted.

## Web API and OTA

| Endpoint | Purpose |
| --- | --- |
| `GET /` | Embedded UI with Home, Varstore, Wi-Fi setup, OTA, Log and Tasks tabs |
| `GET /vars` | Redirect to the Varstore tab (`/#varstore`) |
| `GET /api/vars` | Metadata, effective/raw values, validity and age for each signal |
| `POST /api/vars` | Write, force, release, touch or invalidate a signal |
| `GET /api/status` | Firmware, running slot, reset reason, uptime, heap, heartbeat and network state |
| `GET /api/log` | Live ESP log pages; `?previous=1` selects the frozen previous-boot tail |
| `GET /api/tasks` | FreeRTOS task snapshot, core affinity, CPU counters and stack watermarks |
| `GET /api/wifi` | Saved hostname, AP SSID/password, ordered station SSIDs and password-present flags |
| `POST /api/wifi` | Save profiles, optional hostname/AP password/AP enable flag; reconnect or restart on AP password change |
| `POST /api/ota` | Stream a raw application binary to the inactive slot |

Wi-Fi request example:

```json
{"hostname":"ecmpilot-gw","networks":[{"ssid":"Home","password":"example-passphrase"},{"ssid":"Workshop"}]}
```

Optional boolean `ap_enabled` explicitly enables/disables setup AP fallback; omission preserves the saved policy, whose initial default is false. `GET /api/wifi` returns it. Optional `ap_password` replaces the AP passphrase; omit it to preserve the current value (an explicit empty string is invalid). A changed AP password returns `restarting: true`; other successful saves return `reconnecting: true`.

Omitting `password` retains the password for an existing matching SSID. An explicit empty string selects an open network. A new SSID with an omitted password is also open at API level. Duplicate SSIDs and invalid lengths are rejected without replacing the existing list.

OTA can be initiated from the browser, or:

```sh
curl --fail -H "Content-Type: application/octet-stream" --data-binary @build/ecmpilot_gw.bin http://GATEWAY_IP/api/ota
```

The upload checks the project name, writes only the inactive slot, and uses ESP-IDF image/chip validation before changing the boot selection. A failed/interrupted upload leaves boot selection unchanged. Successful uploads schedule a restart after the HTTP response. Bootloader rollback is enabled. A new image confirms itself after settings/network/web/console initialization and two application heartbeats; reaching an external AP is deliberately not a confirmation requirement. A failed first boot resets without confirmation and the bootloader can return to the previously valid slot. There is no independent factory recovery image; recovery through native USB remains available. Changing the layout/bootloader requires serial flashing.

| Partition | Offset | Size |
| --- | --- | --- |
| `nvs` | `0x9000` | `0x6000` |
| `otadata` | `0xf000` | `0x2000` |
| `phy_init` | `0x11000` | `0x1000` |
| `ota_0` | `0x20000` | `0x1f0000` (1984 KiB) |
| `ota_1` | `0x210000` | `0x1f0000` (1984 KiB) |

## Process data and forcing

The global [varstore component](components/varstore/README.md) starts with every signal invalid. Producers publish on their assigned cores when their source updates; readers supply their own age limit and fallback:

```c
varstore_write(GW_VAR_HEARTBEAT, counter);
int32_t heartbeat = varstore_read(GW_VAR_HEARTBEAT, 3000, -1);
varstore_force(GW_VAR_HEARTBEAT, true, 123);
varstore_force(GW_VAR_HEARTBEAT, false, 0);
```

Open the **Varstore** tab (or `/#varstore`). The table shows heartbeat, uptime, free heap, Wi-Fi connection/RSSI and `demo.value`, including real producer data underneath a force. Rows are compact; signal-name tooltips show descriptions and filtering still matches them. The web controls offer Write, Force, Release and Touch; explicit invalidation remains available through the API. Forces are always valid for consumers, remain active until Release or reboot and never replace producer data. Normal reads retry a concurrent publication until consistent; fallback means invalid or expired data. No semaphores are used.

The web UI uses one page with six tabs: **Home** for gateway status, **Varstore** for process data, **Wi-Fi setup** for network profiles, **OTA** for firmware upload, **Log** for ESP application logs, and **Tasks** for FreeRTOS diagnostics. Switching tabs preserves unfinished edits. Varstore shows **14 matching signals per page**, with Previous/Next controls and a page/range indicator. Changing the filter returns to the first page; polling retains the selected page and edited values. Download JSON includes all signals. Varstore refreshes on entry and polls once per second only while visible and automatic refresh is enabled. Tabs support arrow/Home/End keys and direct links `/#home`, `/#varstore`, `/#wifi`, `/#ota`, `/#log` and `/#tasks`; the older `/vars` URL redirects to its tab.

`demo.value` permits Write, Touch and Invalidate and expires after five seconds. CAN-owned INFO observations and diagnostic signals permit Force/Release only. Remaining vehicle observations, `test.enabled` and `can.replay_enabled` are manual bench inputs; see [producer ownership](docs/can-rx.md). Automatic system/network and UDP signals permit only Force/Release through the web, preserving single-producer ownership. Touch refreshes/revalidates an existing sample; it cannot create a never-written sample. All values and forces are volatile. Status/OTA heartbeat checks use the actual application heartbeat, unaffected by diagnostic forcing.

Commands use an existing numeric signal ID from `GET /api/vars`:

```json
{"id":5,"action":"write","value":42}
{"id":5,"action":"force","value":123}
{"id":5,"action":"release"}
```

Other actions are `touch` and `invalidate`. The API validates IDs, actions and signed int32 values before modifying a signal. It shares the local HTTP access model described above.

## Buffered web log

Open **Log** (`/#log`) to read the gateway's ESP logs. Live updates poll once per second while the tab is visible. Pause live updates, refresh manually, toggle following the output, clear the browser view or download its visible text as `ecmpilot-log.txt`. Clearing the view leaves the gateway buffer intact. Browser history is bounded to 512 chunks / 65,536 characters.

An `esp_log_set_vprintf` hook retains the latest **64 records**, each with up to **255 text bytes**; longer messages end with `[truncated]`. This is a volatile, fixed-size RAM ring, independent of varstore. A short cross-core critical section protects publication and page copies; formatting, serial output and HTTP work happen outside it. The ring uses 16,896 bytes plus metadata, with a separate 4,224-byte HTTP page scratch buffer. Normal serial output and existing log levels are preserved.

A separate two-bank RTC buffer freezes the previous boot's last **12 records**
at startup. Home and Log show the reset reason/code; Log automatically prepends
that frozen tail and includes it in the download. It uses 6360 bytes of RTC RAM,
without flash writes or heap allocation. See [reset diagnostics](docs/reset-diagnostics.md).

Capture begins at the start of `app_main`: application and subsequent normal ESP-IDF logs are included. ROM/bootloader/earlier startup output, panic output and direct `printf`/console text are not captured. In particular, the setup AP password is printed directly to serial and is excluded from the web buffer. Avoid logging credentials in future `ESP_LOG*` calls.

`GET /api/log?after=0` returns at most 16 records and the fields `boot`, `reset`, `oldest`, `latest`, `next`, `lost`, and `entries` (`seq`, `text`). Continue with `after=<next>&boot=<boot>`. A changed boot ID resets the cursor; `lost` reports records overwritten before this page. The UI marks restarts and overwritten records and retains its last visible output on connection errors. Reboot clears the gateway buffer. The endpoint follows the same local HTTP access model as the other read APIs.

## FreeRTOS task diagnostics

Open **Tasks** (`/#tasks`) for task name/ID, snapshot state, current/base priority, CPU affinity, cumulative CPU time, interval CPU load, minimum free stack bytes and stack base address. Auto-refresh runs every two seconds only while the tab is visible. Manual Refresh and Download JSON are also available. The first sample after entry/reboot collects a baseline and shows `--` for load.

Runtime stats and tracing are enabled in `sdkconfig.defaults`, with the ESP timer clock and **64-bit microsecond counters**. Per-task load is `100 * delta(runtime_us) / delta(sampled_us)`: 100% means one fully occupied core. Each core's load is 100% minus its idle task share; total device load is the average of CPU0 and CPU1. A task with `Any` affinity may run on either core; affinity is not a last-executed-core measurement. New tasks or counters that restart wait for another sample before showing load.

The stack watermark is the **minimum free reserve since task creation**, not current free space or total allocation. It is converted using the port's stack element size and exported in bytes. Runtime accounting is updated on context switches; the readings are diagnostic estimates and taking a snapshot adds overhead.

`GET /api/tasks` returns `boot`, `sampled_us`, `cores`, `capacity`, `heap` and `tasks`. The heap table shows total, allocated, free, minimum free, largest free block and free-block count for Internal RAM, Internal DMA RAM and PSRAM. DMA is an overlapping capability view of internal RAM, so do not add the rows; PSRAM is zero on this board. Heap sampling happens outside the RTOS snapshot lock, before allocating the JSON response. Each task has `id`, `name`, `state`, `priority`, `base_priority`, `core` (null for Any), `idle_core` (null for non-idle tasks), `runtime_us`, `stack_min_free_bytes` and `stack_base`. The browser adds interval percentages to its downloaded JSON. The endpoint is read-only and uses the same local HTTP access model as the other read APIs.

The snapshot supports up to **64 tasks**, returning HTTP 503 rather than a partial table if capacity is exceeded. Static snapshot storage belongs to the single HTTP server task. The ESP-IDF 5.3 private kernel-lock wrappers keep task names valid until copied; JSON formatting, allocation and HTTP transfer occur after releasing the lock. This adapter is tied to the project's pinned ESP-IDF 5.3.x series. Changing the stats configuration changes RTOS structures: recompile the SDK and all application components together.

## CAN input architecture

**All process/vehicle data exchange goes through varstore.** CAN reception and protocol decoding only publish input values. Control and derived-data processing belong to a separate process reading varstore; CAN ID handlers never directly invoke/notify a controller. Future command and acknowledgement data also cross varstore.

`CanTask` owns TWAI and decoding on CPU1, with a separate alert waiter. Defaults
are Waveshare TX GPIO15 / RX GPIO16, **125 kbit/s INFO**, normal mode and no TX queue.
The driver has a 128-frame RX queue. Raw physical frames also enter the nonblocking
64-frame FreeRTOS GVRET FIFO for CPU0/TCP23 diagnostics; full FIFO drops only that
diagnostic copy. RTR is ignored by GVRET.

Filter Varstore for `battery.`, `can.` or `gvret.`. The optional HSCAN1 profile
supplies HV SOC and pack voltage; HVAC candidates remain diagnostic fields awaiting
validation. The current INFO profile supplies range, odometer, outside temperature, speed/gear/status and buttons,
plus its SOC observation, but no pack voltage. Its captured local demo uses
`can.replay_enabled=1`; it never transmits or enters GVRET. See
[CAN details](docs/can.md), [RX fields](docs/can-rx.md) and [SavvyCAN setup](docs/gvret.md).

## Android UDP

UDP **42840** runs on CPU0 and serves varstore observations, with duplicate suppression, boot/deadline/generation checks and cached results. Reads and reconnects are passive; the [explicit INFO control backend](docs/vehicle-control.md) provides Refresh and charging-mode requests with BSI readback. For a no-CAN bench test with Android 0.2.0:

```powershell
python tools/udp_bench.py --host 192.168.1.195 seed
```

Configure the phone with the board's actual IP and port 42840, enable experimental local controls and Refresh. It shows TEST. Climate requests update `request.climate.enabled`/`.generation`; confirm manually by forcing observed `climate.state` to its target. Acceptance never updates observations. See [UDP mappings and complete bench workflow](docs/udp.md).

## Runtime and licensing

The structure follows the existing ESP-IDF/S3 `opel_dash` project, with its small `runtime` component ported here. The large `components/ufrm` framework and GUI were not imported. TaskCan architecture, RX mappings/replay and independent GVRET code were adapted to the BSD runtime and native GW varstore. `runtime`'s historical `ufrm.h/.cpp` filenames were renamed to `runtime_process.h/.cpp` to make that separation explicit. See [runtime notes](components/runtime/README.md) and [third-party notices](THIRD_PARTY_NOTICES.md).

New project code uses [BSD-2-Clause](LICENSE). The original T.Kupilik runtime files and Adam Dunkels/SICS protothreads retain their BSD-3-Clause notices. The INFO signal definitions/decoder retain Apache-2.0 attribution to PSA-RE; see [CAN notice](licenses/NOTICE-CAN.txt). ESP-IDF and its bundled dependencies retain their own licenses.

Research reference: [PSA-RE](https://github.com/prototux/PSA-RE) documents community findings about PSA/Stellantis CAN and diagnostics. No definitions from it are included or assumed verified for this firmware yet.

## Development plan

See [the verification record](docs/verification.md) for build/image checks and the restricted-session build workaround, and [testing instructions](docs/testing.md) for native varstore/protocol tests, UI checks and board verification. Version 0.3.1 has not been installed or tested on the gateway yet.

1. Verify first boot, CPU affinity, Wi-Fi failover, setup recovery and OTA on the selected board using [the hardware checks](docs/testing.md).
2. Establish the board's actual idle current and power policy. Wi-Fi modem power save and manual PM/DFS policy are available; the running TWAI driver still prevents light sleep. Manual CAN-only deep sleep is available for measurements. No sub-10 mA board draw is claimed.
3. Add a read-only CAN actor on CPU1 after pin/transceiver wiring is confirmed.
4. Verify the implemented UDP/varstore binding with the physical phone and gateway; MQTT remains a later integration.
5. Add validated vehicle functions and appropriate local authentication/update signing before broader deployment.

## Intended use

Experimental open-source software for development, research and interoperability with equipment you own or are authorised to test. **Not approved for use on public roads.** Installation, vehicle approval and use in traffic require a separate assessment by the installer/operator. Experimental explicit INFO refresh/charge-mode control requires vehicle validation; physical preconditioning operation still requires vehicle verification. This project is not affiliated with or endorsed by Opel, Stellantis or Waveshare. The software is provided “as is” under the applicable licenses.
