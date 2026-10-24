# eCMPilot Gateway

ESP-IDF firmware for the local ESP32-S3 gateway used by the eCMPilot Android application. Initial hardware target: Waveshare ESP32-S3-RS485-CAN. The two-channel CAN kit remains an alternative; this scaffold does not access either board's transceivers or GPIOs.

Version 0.1.0 is a development scaffold: runtime, protothreads, serial console, a persistent Wi-Fi list, local configuration web UI and two-slot OTA. CAN, vehicle commands, UDP, MQTT, Ethernet drivers and GUI are not implemented.

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
| HTTP server, configuration and OTA receive/write | FreeRTOS timer service used by runtime timers |

These affinities are explicit in task creation and `sdkconfig.defaults`, with compile-time guards for the critical settings and runtime assertions in task/event entry points. ESP-IDF's `esp_timer` infrastructure remains on CPU0; idle and IPC tasks remain per-core system tasks. Future network/Ethernet workers must be pinned to CPU0, future CAN/application workers to CPU1. Firmware flash operations can temporarily stall execution on both cores; pinning does not provide real-time isolation from OTA writes.

## Wi-Fi setup

1. On first boot, connect to the WPA2 setup AP `eCMPilot-XXXXXX`. A randomly generated, persistent 16-character password is printed on the serial console at startup.
2. Open **http://192.168.4.1/** and enter up to **eight** Wi-Fi profiles, in priority order. Use the arrows to change priority. Passwords may be left unchanged when editing an existing SSID; an explicit “Open network” option removes the saved password.
3. Save. The complete list is validated and committed to NVS before reconnecting. It survives resets and normal OTA updates. Saved passwords are not returned by the web API or the `wifi list` command.
4. The gateway scans for visible SSIDs and selects the first available configured network. A working connection is retained even if a higher-priority network later appears. A failed candidate is held off for 60 seconds so lower-priority networks can be tried. Idle scans repeat every 15 seconds; a pending connection/DHCP attempt is limited to 20 seconds.
5. When a configured gateway stays offline for 30 seconds, it enables the same setup AP. An activated setup AP remains available until reboot, including after the station reconnects. At the next boot it starts disabled if there are configured profiles.

The web UI is also available at the gateway's DHCP address on the joined LAN; the address is printed on serial and in `status`. No captive portal or mDNS discovery is provided yet. Hidden SSIDs, enterprise Wi-Fi and multiple credentials for one SSID are not supported. SSIDs are limited to 32 UTF-8 bytes; passwords are empty for open networks, 8–63 bytes for a passphrase, or 64 hexadecimal digits for a PSK.

This initial interface is **local HTTP without authentication or signed firmware**. The setup AP uses WPA2; saved credentials are not encrypted in NVS. Browser writes reject a different `Origin`, but this does not authenticate other LAN clients. Use a trusted development network; do not expose the interface through port forwarding. These properties are recorded here so later pairing/authentication work has a clear starting point.

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
restart
```

`wifi add` appends a new SSID or replaces its existing credentials. Omitting its password explicitly configures an open network. `wifi clear` removes all station profiles and enables the setup AP; it preserves the setup AP password. Quotes are needed for SSIDs or passphrases containing spaces. Console credentials can appear in terminal scrollback and the in-memory line history; history is not persisted.

## Web API and OTA

| Endpoint | Purpose |
| --- | --- |
| `GET /` | Embedded configuration UI |
| `GET /api/status` | Firmware, running slot, uptime, heap, heartbeat and network state |
| `GET /api/wifi` | Ordered SSIDs and password-present flags |
| `POST /api/wifi` | Replace the saved profile list and reconnect |
| `POST /api/ota` | Stream a raw application binary to the inactive slot |

Wi-Fi request example:

```json
{"networks":[{"ssid":"Home","password":"example-passphrase"},{"ssid":"Workshop"}]}
```

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

## Runtime and licensing

The structure follows the existing ESP-IDF/S3 `opel_dash` project, with its small `runtime` component ported here. The large `components/ufrm` framework, UI and application modules were not imported. `runtime`'s historical `ufrm.h/.cpp` filenames were renamed to `runtime_process.h/.cpp` to make that separation explicit. See [runtime notes](components/runtime/README.md) and [third-party notices](THIRD_PARTY_NOTICES.md).

New project code uses [BSD-2-Clause](LICENSE). The original T.Kupilik runtime files and Adam Dunkels/SICS protothreads retain their BSD-3-Clause notices. ESP-IDF and its bundled dependencies retain their own licenses.

Research reference: [PSA-RE](https://github.com/prototux/PSA-RE) documents community findings about PSA/Stellantis CAN and diagnostics. No definitions from it are included or assumed verified for this firmware yet.

## Development plan

The initial build/image checks and six UI logic tests passed; see [the verification record](docs/verification.md) for results and the restricted-session build workaround. No hardware tests have been performed.

1. Verify first boot, CPU affinity, Wi-Fi failover, setup recovery and OTA on the selected board using [the hardware checks](docs/testing.md).
2. Establish the board's actual idle current and power policy. Wi-Fi modem power save is requested, but this scaffold does not implement deep sleep or claim a sub-10 mA board draw.
3. Add a read-only CAN actor on CPU1 after pin/transceiver wiring is confirmed.
4. Implement and test the Android UDP contract on CPU0; MQTT remains a later integration.
5. Add validated vehicle functions and appropriate local authentication/update signing before broader deployment.

## Intended use

Experimental open-source software for development, research and interoperability with equipment you own or are authorised to test. **Not approved for use on public roads.** Installation, vehicle approval and use in traffic require a separate assessment by the installer/operator. No vehicle control is implemented in this version. This project is not affiliated with or endorsed by Opel, Stellantis or Waveshare. The software is provided “as is” under the applicable licenses.
