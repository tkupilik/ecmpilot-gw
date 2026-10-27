# eCMPilot Gateway

ESP-IDF firmware for the local ESP32-S3 gateway used by the eCMPilot Android application. Initial hardware target: Waveshare ESP32-S3-RS485-CAN. The two-channel CAN kit remains an alternative; this scaffold does not access either board's transceivers or GPIOs.

Version 0.5.0 includes runtime, protothreads, serial console, persistent Wi-Fi profiles and hostname, a tabbed web UI with buffered ESP logs and FreeRTOS task diagnostics, two-slot OTA, varstore and the Android UDP v0 binding. UDP monitoring and an explicit varstore test backend work without CAN. CAN, physical vehicle commands, MQTT, Ethernet drivers and GUI are not implemented.

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
| UDP socket, protocol, command cache and request-signal producer | Future CAN observation producer |

These affinities are explicit in task creation and `sdkconfig.defaults`, with compile-time guards for the critical settings and runtime assertions in task/event entry points. ESP-IDF's `esp_timer` infrastructure remains on CPU0; idle and IPC tasks remain per-core system tasks. Future network/Ethernet workers must be pinned to CPU0, future CAN/application workers to CPU1. Firmware flash operations can temporarily stall execution on both cores; pinning does not provide real-time isolation from OTA writes.

## Wi-Fi setup

1. On first boot, connect to the WPA2 setup AP `eCMPilot-XXXXXX`. A randomly generated, persistent 16-character password is printed on the serial console at startup.
2. Open **http://192.168.4.1/** and enter up to **eight** Wi-Fi profiles, in priority order. Use the arrows to change priority. Passwords may be left unchanged when editing an existing SSID; an explicit “Open network” option removes the saved password.
3. Save. The complete list is validated and committed to NVS before reconnecting. It survives resets and normal OTA updates. Saved station-network passwords are not returned by the web API or the `wifi list` command.
4. The gateway scans for visible SSIDs and selects the first available configured network. A working connection is retained even if a higher-priority network later appears. A failed candidate is held off for 60 seconds so lower-priority networks can be tried. Idle scans repeat every 15 seconds; a pending connection/DHCP attempt is limited to 20 seconds.
5. When a configured gateway stays offline for 30 seconds, it enables the same setup AP. An activated setup AP remains available until reboot, including after the station reconnects. At the next boot it starts disabled if there are configured profiles.

The web UI is also available at the gateway's DHCP address on the joined LAN; the address is printed on serial and in `status`. No captive portal or mDNS discovery is provided yet. Hidden SSIDs, enterprise Wi-Fi and multiple credentials for one SSID are not supported. SSIDs are limited to 32 UTF-8 bytes; passwords are empty for open networks, 8–63 bytes for a passphrase, or 64 hexadecimal digits for a PSK.

In **Wi-Fi setup → Hostname**, set the DHCP hostname (default **`ecmpilot-gw`**). Enter one label of 1–32 ASCII letters/digits/hyphens, starting and ending with a letter or digit; uppercase is normalized to lowercase. Save and reconnect applies it before the next DHCP exchange. Home/`status` display the applied hostname.

The router determines whether to register this name and which suffix to use: for example, `ecmpilot-gw.lan` can then be entered in Android. DNS records/caches may take time to update. This does not add mDNS/`.local` discovery or rename the setup AP SSID. The numeric IP and setup AP `192.168.4.1` remain available; the UDP gateway identity stays MAC-based.

Hostname uses a separate NVS key; the original version-1 Wi-Fi blob is unchanged. Existing profiles load without migration and older OTA images can still read them. Wi-Fi list edits/clears and older API requests omitting hostname preserve its saved value.

This initial interface is **local HTTP without authentication or signed firmware**. The setup AP uses WPA2; saved credentials are not encrypted in NVS. Browser writes reject a different `Origin`, but this does not authenticate other LAN clients. Use a trusted development network; do not expose the interface through port forwarding. These properties are recorded here so later pairing/authentication work has a clear starting point.

Under **Wi-Fi setup → Setup access point**, the current AP SSID/password are shown. The password is masked until **Show password** is selected and can be replaced with 8–63 printable ASCII characters. Blank input retains the existing password. Saving an actual password change commits settings and schedules a gateway restart after the HTTP response; AP clients must reconnect using the new password. Ordinary profile/hostname edits reconnect without restarting. Existing randomly generated AP passwords remain valid, and clearing station profiles preserves the AP password.

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

`wifi add` appends a new SSID or replaces its existing credentials. Omitting its password explicitly configures an open network. `wifi clear` removes all station profiles and enables the setup AP; it preserves the setup AP password. Quotes are needed for SSIDs or passphrases containing spaces. Console credentials can appear in terminal scrollback and the in-memory line history; history is not persisted.

## Web API and OTA

| Endpoint | Purpose |
| --- | --- |
| `GET /` | Embedded UI with Home, Varstore, Wi-Fi setup, OTA, Log and Tasks tabs |
| `GET /vars` | Redirect to the Varstore tab (`/#varstore`) |
| `GET /api/vars` | Metadata, effective/raw values, validity and age for each signal |
| `POST /api/vars` | Write, force, release, touch or invalidate a signal |
| `GET /api/status` | Firmware, running slot, uptime, heap, heartbeat and network state |
| `GET /api/log` | Cursor-based pages of buffered ESP log records |
| `GET /api/tasks` | FreeRTOS task snapshot, core affinity, CPU counters and stack watermarks |
| `GET /api/wifi` | Saved hostname, AP SSID/password, ordered station SSIDs and password-present flags |
| `POST /api/wifi` | Save profiles, optional hostname/AP password; reconnect or restart on AP password change |
| `POST /api/ota` | Stream a raw application binary to the inactive slot |

Wi-Fi request example:

```json
{"hostname":"ecmpilot-gw","networks":[{"ssid":"Home","password":"example-passphrase"},{"ssid":"Workshop"}]}
```

Optional `ap_password` replaces the AP passphrase; omit it to preserve the current value (an explicit empty string is invalid). A changed AP password returns `restarting: true`; other successful saves return `reconnecting: true`.

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

The global [varstore component](components/varstore/README.md) starts with every signal invalid. Producers write once per second on their assigned cores; readers supply their own age limit and fallback:

```c
varstore_write(GW_VAR_HEARTBEAT, counter);
int32_t heartbeat = varstore_read(GW_VAR_HEARTBEAT, 3000, -1);
varstore_force(GW_VAR_HEARTBEAT, true, 123);
varstore_force(GW_VAR_HEARTBEAT, false, 0);
```

Open the **Varstore** tab (or `/#varstore`). The table shows heartbeat, uptime, free heap, Wi-Fi connection/RSSI and `demo.value`, including real producer data underneath a force. Rows are compact; signal-name tooltips show descriptions and filtering still matches them. The web controls offer Write, Force, Release and Touch; explicit invalidation remains available through the API. Forces are always valid for consumers, remain active until Release or reboot and never replace producer data. Normal reads retry a concurrent publication until consistent; fallback means invalid or expired data. No semaphores are used.

The web UI uses one page with six tabs: **Home** for gateway status, **Varstore** for process data, **Wi-Fi setup** for network profiles, **OTA** for firmware upload, **Log** for ESP application logs, and **Tasks** for FreeRTOS diagnostics. Switching tabs preserves unfinished edits. Varstore refreshes on entry and polls once per second only while visible and automatic refresh is enabled. Tabs support arrow/Home/End keys and direct links `/#home`, `/#varstore`, `/#wifi`, `/#ota`, `/#log` and `/#tasks`; the older `/vars` URL redirects to its tab.

`demo.value` permits Write, Touch and Invalidate and expires after five seconds. Vehicle observations and `test.enabled` are manual inputs until CAN has a producer. Automatic system/network and UDP signals permit only Force/Release through the web, preserving single-producer ownership. Touch refreshes/revalidates an existing sample; it cannot create a never-written sample. All values and forces are volatile. Status/OTA heartbeat checks use the actual application heartbeat, unaffected by diagnostic forcing.

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

Capture begins at the start of `app_main`: application and subsequent normal ESP-IDF logs are included. ROM/bootloader/earlier startup output, panic output and direct `printf`/console text are not captured. In particular, the setup AP password is printed directly to serial and is excluded from the web buffer. Avoid logging credentials in future `ESP_LOG*` calls.

`GET /api/log?after=0` returns at most 16 records and the fields `boot`, `reset`, `oldest`, `latest`, `next`, `lost`, and `entries` (`seq`, `text`). Continue with `after=<next>&boot=<boot>`. A changed boot ID resets the cursor; `lost` reports records overwritten before this page. The UI marks restarts and overwritten records and retains its last visible output on connection errors. Reboot clears the gateway buffer. The endpoint follows the same local HTTP access model as the other read APIs.

## FreeRTOS task diagnostics

Open **Tasks** (`/#tasks`) for task name/ID, snapshot state, current/base priority, CPU affinity, cumulative CPU time, interval CPU load, minimum free stack bytes and stack base address. Auto-refresh runs every two seconds only while the tab is visible. Manual Refresh and Download JSON are also available. The first sample after entry/reboot collects a baseline and shows `--` for load.

Runtime stats and tracing are enabled in `sdkconfig.defaults`, with the ESP timer clock and **64-bit microsecond counters**. Per-task load is `100 * delta(runtime_us) / delta(sampled_us)`: 100% means one fully occupied core. Each core's load is 100% minus its idle task share; total device load is the average of CPU0 and CPU1. A task with `Any` affinity may run on either core; affinity is not a last-executed-core measurement. New tasks or counters that restart wait for another sample before showing load.

The stack watermark is the **minimum free reserve since task creation**, not current free space or total allocation. It is converted using the port's stack element size and exported in bytes. Runtime accounting is updated on context switches; the readings are diagnostic estimates and taking a snapshot adds overhead.

`GET /api/tasks` returns `boot`, `sampled_us`, `cores`, `capacity` and `tasks`. Each task has `id`, `name`, `state`, `priority`, `base_priority`, `core` (null for Any), `idle_core` (null for non-idle tasks), `runtime_us`, `stack_min_free_bytes` and `stack_base`. The browser adds interval percentages to its downloaded JSON. The endpoint is read-only and uses the same local HTTP access model as the other read APIs.

The snapshot supports up to **64 tasks**, returning HTTP 503 rather than a partial table if capacity is exceeded. Static snapshot storage belongs to the single HTTP server task. The ESP-IDF 5.3 private kernel-lock wrappers keep task names valid until copied; JSON formatting, allocation and HTTP transfer occur after releasing the lock. This adapter is tied to the project's pinned ESP-IDF 5.3.x series. Changing the stats configuration changes RTOS structures: recompile the SDK and all application components together.

## Android UDP

UDP **42840** runs on CPU0 and serves varstore observations, with duplicate suppression, boot/deadline/generation checks and cached results. Initial mode is monitoring only. For a no-CAN bench test with Android 0.2.0:

```powershell
python tools/udp_bench.py --host 192.168.1.195 seed
```

Configure the phone with the board's actual IP and port 42840, enable experimental local controls and Refresh. It shows TEST. Climate requests update `request.climate.enabled`/`.generation`; confirm manually by forcing observed `climate.state` to its target. Acceptance never updates observations. See [UDP mappings and complete bench workflow](docs/udp.md).

## Runtime and licensing

The structure follows the existing ESP-IDF/S3 `opel_dash` project, with its small `runtime` component ported here. The large `components/ufrm` framework, UI and application modules were not imported. `runtime`'s historical `ufrm.h/.cpp` filenames were renamed to `runtime_process.h/.cpp` to make that separation explicit. See [runtime notes](components/runtime/README.md) and [third-party notices](THIRD_PARTY_NOTICES.md).

New project code uses [BSD-2-Clause](LICENSE). The original T.Kupilik runtime files and Adam Dunkels/SICS protothreads retain their BSD-3-Clause notices. ESP-IDF and its bundled dependencies retain their own licenses.

Research reference: [PSA-RE](https://github.com/prototux/PSA-RE) documents community findings about PSA/Stellantis CAN and diagnostics. No definitions from it are included or assumed verified for this firmware yet.

## Development plan

See [the verification record](docs/verification.md) for build/image checks and the restricted-session build workaround, and [testing instructions](docs/testing.md) for native varstore/protocol tests, UI checks and board verification. Version 0.3.1 has not been installed or tested on the gateway yet.

1. Verify first boot, CPU affinity, Wi-Fi failover, setup recovery and OTA on the selected board using [the hardware checks](docs/testing.md).
2. Establish the board's actual idle current and power policy. Wi-Fi modem power save is requested, but this scaffold does not implement deep sleep or claim a sub-10 mA board draw.
3. Add a read-only CAN actor on CPU1 after pin/transceiver wiring is confirmed.
4. Verify the implemented UDP/varstore binding with the physical phone and gateway; MQTT remains a later integration.
5. Add validated vehicle functions and appropriate local authentication/update signing before broader deployment.

## Intended use

Experimental open-source software for development, research and interoperability with equipment you own or are authorised to test. **Not approved for use on public roads.** Installation, vehicle approval and use in traffic require a separate assessment by the installer/operator. No vehicle control is implemented in this version. This project is not affiliated with or endorsed by Opel, Stellantis or Waveshare. The software is provided “as is” under the applicable licenses.
