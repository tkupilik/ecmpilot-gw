# Verification record

## Optional setup AP fallback — 2026-11-01

Latest image evidence is [ap-policy-verification.json](ap-policy-verification.json).
Setup AP fallback defaults disabled for both fresh and legacy settings; the
checkbox persists an optional boolean in its own NVS key. The version1/792-byte
station blob and old settings APIs remain compatible. The network owner gates
startup/fallback and switches an active AP back to STA-only after saving.
165 native tests and46 web checks passed, including flag persistence, commit
failure, legacy omission, checkbox roundtrip and the complete Varstore/Log UI.
Target application/runtime rebuild, link and OTA image size checks passed.
No upload, physical radio test or new current measurement was performed.

## OBC wake and unit004 source review — 2026-11-01

The INFO DBC was regenerated from the pinned PSA-RE source plus five captures:
53971 frames,115 observed IDs,377 exported IDs and7913 signals. All53971 frames
have full physical-bit coverage and decode successfully;986175 captured signal
values were checked independently. Strict reload,32578 synthetic full payloads,
13888 short transport payloads and161 native/project tests passed.13 existing
owner-confirmed fields remain unchanged. Source JSON and original CSVs are
preserved; the vehicle overlay carries the new cause value and nine IC sender
attributions. See [capture evidence/timeline](info-obc-wake-2026-11-01.md), its
adjacent JSON, and the DBC capture-validation/validation JSON files for hashes
and counts. No firmware build, upload or vehicle operation was required.

## Varstore web regression fix — 2026-11-01

Latest image evidence is [varstore-ui-fix-verification.json](varstore-ui-fix-verification.json).
The retained-log integration had replaced the Varstore controller and left two
Log controllers. Restore one controller for each panel, including14-signal pages,
filtering, Force/edit preservation and complete JSON export.44 browser logic tests
passed across Varstore, full-page tabs/Wi-Fi/OTA, Log, Tasks and power controls.
The full-page test now verifies actual Varstore fetching/rendering; script tests
require exactly one controller owning their DOM prefix instead of trusting its
position. Rebuild/link/image checks passed. Backend/driver code is unchanged;
no upload or vehicle action was performed.

## Manual CAN-only deep-sleep test — 2026-11-01

Latest image evidence is [can-deep-sleep-verification.json](can-deep-sleep-verification.json).
156 native tests and28 browser checks passed. The production state machine is
checked for owner completion ordering, errors/timeouts, Force isolation and
restoration after refused entry. Physical GPIO wake/hold and current draw require
the owner's board test. Target application/runtime sources were rebuilt and
linked against ESP-IDF5.3.2, with image validation and OTA size checks. No upload
or vehicle transmission was performed. INFO/125, no version5DF TX, RTC reset log
and no-PSRAM configuration are retained. A manual web action is the only trigger;
no48-hour or other automatic sleep timer is enabled.

## INFO wake configuration and retained reset log — 2026-11-01

Latest image evidence is [info-can-wake-verification.json](info-can-wake-verification.json).
Both tracked defaults and active build select INFO/125000bit/s,NORMAL,TX15/RX16,
without PSRAM. Version5DF TX is removed; the wake ID/payload are Force-configurable
(default43C/00 80). HSCAN1 remains an optional passive profile. The previous boot's
last12 application log records are frozen in6360bytes of RTC slow RAM; Home and
Log show the reset reason.148 native tests and25 browser checks passed. An IDF
target build plus final project-source rebuild/link/image check passed. No upload,
CAN transmission or physical reset test was performed. Earlier sections/hashes
record historical profiles and are superseded by this entry.

## Manual PM/DFS preparation — 2026-11-01

That build's image evidence is [power-management-verification.json](power-management-verification.json).
A complete892-edge IDF target build passed with CAN enabled on HSCAN1/500kbit/s,
PM/DFS, tickless idle and qualified sleep counters. Sleep callbacks are linked in
IRAM.137 native checks and19 browser-control/regression checks passed. No flash,
physical sleep transition or current measurement was performed. Older image
hashes below describe their historical builds.

## Previous HSCAN1 default — 2026-10-31

Historical source/build/test evidence is [lcd-backport-verification.json](lcd-backport-verification.json).
Both tracked defaults and active sdkconfig select **HSCAN1/500000bit/s**, listen-only,
TX15/RX16, without PSRAM. `0x5C3` publishes HV SOC and pack voltage to native GW
varstore and phone/HA. INFO/125 remains optional; its401 wake and5DF version TX
are hard-disabled on HSCAN1. GVRET reports the selected500000bit/s listen-only bus.
All SDK compilation/archive edges were run from the current Ninja graph. The
Windows fallback then recompiled application sources, rematerialized archive/link
response files and completed the application link/image and OTA slot size check. Native
protocol/RX/HA tests cover both profiles, including HS1 battery scaling and the
HS1 TX guard. The optional enabled MQTT branch is separately compiled/linked.
No firmware upload or live vehicle test is performed for this profile change.
The entries below are historical snapshots. See [LCD parity](lcd-backport.md).

## INFO CAN / GVRET backport and Tasks heap — 2026-10-31

- Firmware version remains **0.5.0**. Both local `sdkconfig` and tracked defaults
  select **INFO / 125000 bit/s**, Waveshare TX15/RX16, listen-only. PSRAM remains
  disabled. TaskCan owns RX/decoding on CPU1; GVRET runs on CPU0. The GPIO wake
  implementation, API, web controls and signals are removed.
- Backport nine INFO handlers and captured replay to the actual **GW varstore**;
  preserve freshness, Force, phone units and km×10 range. Live is the default.
  INFO pack voltage and physical climate/charging control remain unavailable.
  [Comparison](lcd-backport.md) records the remaining MQTT/HA difference.
- **111 native tests passed**, compiled with Clang `-Wall -Wextra -Werror` against
  both INFO and legacy HS1 profiles. This includes 15 INFO handler/pipeline tests,
  capture-order/hash checks, source transitions/Force, units, old HS1 regressions,
  DBC validation, UDP and streamed varstore JSON. All 1974 selected replay frames
  match the 9520-frame owner's capture; 11C has synthetic-only coverage.
- Seventeen of those tests exercise the actual GVRET protocol/worker with the
  native GW store and real loopback TCP, including all 9520 source frames, partial
  writes, 64-frame overflow, timeouts, second clients, session isolation and
  ignored TX/config requests. Host task/queue/clock adapters do not establish
  physical FreeRTOS scheduling or TWAI/Wi-Fi performance. The fixed FIFO is
  **64×24 = 1536 bytes**, with static 1024-byte TX storage and no PSRAM allocation.
- **28 embedded-page tests passed** across web, varstore, log and Tasks. Three
  native Tasks adapter tests cover runtime snapshots and heap capability views;
  six Tasks UI tests include zero PSRAM and refreshed largest blocks. Internal
  DMA is a subset view of internal RAM, not additional capacity.
- ESP-IDF 5.3.2 configured the updated graph successfully. Normal `idf.py build`
  encountered Windows named-pipe access denial; direct Ninja did not complete and
  was interrupted. `tools/build_sequential.py` compiled all 26 project-owned
  translation units, regenerated embedded HTML/archives/linker sections, linked
  ELF and generated/size-checked the application. It **reused existing SDK objects**;
  this is an incremental verification, not a successful clean Ninja build.
  `build/sequential-build.json` lists exactly which project units were compiled.
- OTA size check passed: **984,256 bytes**, 52% of the smallest slot free.
  SHA-256: `7a87c389ae02c67c9b38de5f32ce291c0bae8d7b2eb62939ed18e23d8bef5c29`. Logs are in ignored `build-host-tests/can-backport-final.log`,
  `build-host-tests/can-backport-ui.log` and `build-can-backport-sequential.log`.
- No flashing, physical gateway request or vehicle bus transmission performed.
  Validate GPIO/rate, values/polarity, SavvyCAN compatibility, loss counters,
  FreeRTOS affinity, heap and stack margins on the actual board.


## Canonical varstore units — 2026-10-28

- Version remains 0.5.0. Voltage/current/temperature storage and descriptors now use mV/mA/mC (millicelsius), with corresponding `_mv`, `_ma`, `_mc` signal/enum names. Power remains W, SOC remains tenths of a percent (`0.1 %`). UDP field names and physical phone units remain V/A/C/kW/percent; range, distance, time and airflow keep their explicitly named units.
- Pack voltage now converts exactly as raw quarter-volts × 250 mV, removing the previous tenth-volt rounding. HVAC 0.5 C / 0.1 C fields convert directly to mC; compressor 0.1 A converts to mA. Updated bench seeds and current mapping documentation. Previous records below describe their historical builds/scales.
- 80 native tests passed, including unit-descriptor and milli-unit-to-UDP conversion regressions with negative/sub-degree temperatures, signed current, exact 416.25 V, unchanged SOC/power and out-of-range inputs. Replayed all 181,002 recorded frames against independent DBC decoding scaled into the new units: 7,282 field checks passed; the same two zero startup battery frames were suppressed.
- Rebuilt/relinked through the sequential ESP-IDF 5.3.2 compiler-command workaround and passed the OTA size check (54% free). `build/ecmpilot_gw.bin`: 940,976 bytes; SHA-256 `a062c42da913cfc7b9158a1055fe6e72149e0e8bd42a93c35eb0386274216c08`. No flashing or vehicle transmission performed; phone source changes are not required.

## Varstore HTTP out-of-memory fix — 2026-10-28

- The old `/api/vars` handler built a cJSON tree for the complete registry, then allocated the serialized response while keeping that tree alive. Browser pagination did not reduce this peak. Replace that handler with `main/vars_http.c`, which emits the same full JSON schema in HTTP chunks through a fixed 1,024-byte stack buffer and small scalar-formatting buffers. The serializer allocates neither JSON nodes nor a full response body; HTTP transport retains its normal SDK memory requirements.
- 78 native tests passed, including six new streamed-response regressions: all real registry fields, disabled cJSON allocation, escaped/long UTF-8 metadata, 400 signals with the same 1 KiB maximum chunk, signed/forced/stale values with saturated ages, and first/middle/final chunk failures. Failed sends propagate without falsely completing partial JSON.
- 23 relevant embedded-page tests passed; the existing filtering, 14-item browser pagination, force controls and full JSON download contract remain unchanged.
- Rebuilt project sources and regenerated the image using the sequential ESP-IDF 5.3.2 command workaround described below, including the new `vars_http.c` object. OTA size check passed (54% free).
- Version remains 0.5.0. `build/ecmpilot_gw.bin`: 940,992 bytes; SHA-256 `698f2299f0fe2c9f83c5e4c1fc9439918b4d6488d77cc7303a8ce1b3594e5ee4`. Not uploaded to the gateway; live HTTP reception remains to be checked after OTA.

## HS1 SOC and diagnostic HVAC handlers — 2026-10-28

- Firmware version remains 0.5.0; both sdkconfig and defaults select 500,000 bit/s. Standard `5C3` DLC 8 now supplies the existing phone SOC and pack-voltage fields through the CPU1 RX → varstore → CPU0 UDP path. No phone source changes are needed.
- 65 native tests passed, including eight new decoder cases. Tests use real captured payloads and the actual varstore/UDP serializer, covering zero-filled startup, valid zero/100% SOC, sentinel/range checks, quarter-volt rounding, stale data, extended/RTR/DLC rejection and CAN ownership with independent force overlays. Candidate HVAC/outside/odometer fields cannot populate phone cabin/climate state.
- Independently replayed all 181,002 frames from the three recordings through the native C RX handler and compared 7,282 mapped field observations against cantools decoding of the pinned DBC. All matched; 178 battery frames included two suppressed zero-filled startup payloads. The ignored replay script/report are `build-host-tests/replay-can-telemetry.py` and `can-telemetry-replay.json`; these perform no hardware I/O.
- 23 relevant embedded-page tests passed (general web, varstore and wake button). Rebuilt/relinked the firmware with the same sequential IDF 5.3.2 command workaround described below; OTA size check passed with 54% free.
- `build/ecmpilot_gw.bin`: 939,904 bytes; SHA-256 `1ca0d10ff26d797ef6442c22166cd0c7a6b77f67968938a05d86a2a92bdd202a`. No firmware upload or vehicle transmission performed. Live vehicle/phone reception and candidate physical meanings still need device validation.

## CAN GPIO wake-up experiment — 2026-10-28

- Firmware version remains 0.5.0. Implemented Home button, empty POST/GET `/api/can/wake-test`, varstore request/completion handoff and CPU1 TWAI-to-GPIO-to-TWAI test.
- 57 native tests passed, including five new request/expiry/force/readiness/wrap tests. 32 embedded-page JavaScript tests passed, including five new explicit-click/pending/failure/result tests. Node tests ran directly per file because child-process test isolation is denied in this environment.
- Recompiled project sources with the existing ESP-IDF 5.3.2 compiler/configuration commands, explicitly adding `can_wake_test.c` to the main archive, then regenerated the embedded page, linker sections, ELF and OTA image. The normal `idf.py`/CMake/Ninja path encountered Windows pipe permission restrictions; ignored `build-host-tests/verify-wake-build.py` performs the sequential command verification. This is not a successful clean CMake/Ninja build.
- Linked disassembly places `WakePulse()` at `0x403772e0` in `.iram0.text`: four inline GPIO register writes, three ROM delay calls with argument 10, and IRAM critical-section entry/exit. The final HIGH write precedes critical-section exit. No flash-resident call appears in the waveform routine.
- `build/ecmpilot_gw.bin`: 937,456 bytes; SHA-256 `b385f5c8e34b29e71d3a3a9c7136e7ac765239f68e120ea5ecc11a0fb0472a68`. OTA partition size check passed (54% free).
- No flashing, web POST to a physical gateway, CAN pulse generation or vehicle transmission performed. Physical timing/routing/restoration and vehicle wake remain unverified on hardware.

## Varstore pagination and component power budget — 2026-10-28

Firmware version remains **0.5.0**. The embedded Varstore UI shows 14 filtered signals per page, Previous/Next controls and page/range status. Filter changes reset to page 1; refresh/polling retains the selected page and existing editor elements. JSON download remains the complete response.

- The existing **18 Varstore/full-page JavaScript tests passed**. Their DOM inventories were updated for the three pagination controls.
- A separate execution of the actual Varstore script through the existing DOM/HTTP harness verified 57 signals as **14/14/14/14/1**, end-button states, description filtering, no matches, exact 14/15 boundaries, page clamping after a filter-result change, and edited-input retention across navigation/polling. These are logic checks, not browser rendering or live ESP tests.
- Rebuilt the embedded page, project/runtime sources, required archives and application using the existing sequential-command workaround. Image generation/size checks passed; 54% of the smallest OTA slot remains free. No flashing/OTA installation was performed.
- Restructured `powe_consumption.md` around component currents and 12 V equivalents. MCU scenarios come from documented reference states; the owner's readings serve only as an independent checksum. This document is not a physical component-current measurement.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/ecmpilot_gw.bin` (0.5.0, Varstore pagination) | 932480 | `47a117a2c3733d766d831dd2a2143e1764ac6a9a85b99b9d242bdedbf636e097` |

## Passive CAN RX process and varstore boundary — 2026-10-28

Firmware version remains **0.5.0**. Adds CPU1 `can_rx`, priority 8, 4096-byte stack, ESP-IDF TWAI v2 listen-only reception with no TX queue/API, bounded batches and varstore publication. Vehicle decoding is an empty acquisition-only mapping module. No controller process/physical commands were added; all future inter-process application data must cross varstore as recorded in [can.md](can.md).

- All **32 native tests** passed, including five new actual-code CAN publication cases: initialization, health, standard/extended data, short-frame cleanup, invalid ID/DLC, RTR handling, freshness, Force overlays, producer ownership and no fabricated vehicle/control updates. These are not physical TWAI/IRQ tests.
- CMake configured the new Kconfig options and IRAM ISR. The final config enables passive RX at 500 kbit/s (a starting setting requiring actual branch verification). Recompiled project/runtime code, the TWAI driver/HAL and newly required SDK objects; regenerated driver/HAL/project archives and linker sections, linked and generated the image via the existing sequential-command workaround. This does not claim a clean normal `idf.py build`.
- Inspected the CAN adapter object's dependencies: SDK receive/status/install/start APIs, runtime and varstore publisher; no transmit API or controller callback. Timer callbacks only notify `can_rx`; the process drains at most 32 frames per scheduling notification and publishes health every half second.
- esptool validated checksum/hash; version is `0.5.0`, CAN metadata are embedded, both OTA slots have 54% free. Prior `Tmr Svc` CPU1/priority-24 configuration is unchanged.
- Not flashed or OTA-installed. Physical pins/bitrate/reception/passivity, CPU/IRQ placement, queue losses under load/OTA, freshness through real traffic pauses and stack/heap margins remain bench checks. No real vehicle signal layouts were imported or verified by this change.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/ecmpilot_gw.bin` (0.5.0, passive CAN RX) | 931376 | `72fbfd40ba13179df52669ff7855e9f23c37d8c9ae46e1271dc85e391ec76b2a` |

## AP password setup and highest-priority timer service — 2026-10-27

Keep firmware version **0.5.0**, as requested. Wi-Fi setup now displays the AP SSID and masked/revealable password, permits a replacement and restarts after a committed password change. Empty browser input/omitted API fields preserve the existing password; station passwords remain hidden. `Tmr Svc` stays on CPU1 and uses **priority 24**, guarded against `configMAX_PRIORITIES - 1` at compile time.

- All **27 native tests** passed, including three new AP-password settings tests for legacy-key preservation, reload, omission/clear behaviour, 8/16/63-character boundaries, validation, failed commit and corrupt stored values. NVS remains a host substitute; no flash power-loss behaviour was tested.
- All **27 web logic tests** passed, including two AP tests for display/reveal, edits across tabs, restart handling, validation and blank-input preservation. This does not test rendered UI or actual ESP HTTP/network integration.
- CMake reconfigured; generated config has `CONFIG_FREERTOS_TIMER_TASK_PRIORITY=24`. Recompiled project/runtime sources and FreeRTOS (including `timers.c`), regenerated archives/linker sections, linked and generated the image using the existing sequential command workaround. The FreeRTOS stats ABI is unchanged from the preceding full SDK rebuild.
- esptool validated the image checksum/hash; version remains `0.5.0`, the AP password field is embedded, and both OTA slots have 55% free. Not flashed or OTA-installed. Real AP reconnection after password change and timer priority in Tasks still require the board checks in [testing.md](testing.md).

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/ecmpilot_gw.bin` (0.5.0, AP setup + timer priority) | 915344 | `452db131ba8d79259fe5660a3fd2797639a0b49c6504b93e7cc2f6e9b051e956` |

## FreeRTOS Tasks tab — 2026-10-27

Firmware **0.5.0** enables trace/runtime statistics, core IDs and 64-bit ESP timer microsecond counters. Adds `/api/tasks` and a Tasks tab with cumulative CPU time, interval task/core/device load, priorities, affinity and stack minimum-free bytes/base address. The first sample collects a baseline; automatic polling runs every two seconds only while visible.

- All **25 web logic tests** passed (11 combined-page, 5 varstore, 4 log, 5 tasks). Task coverage includes correct per-core/device load normalization, counters beyond 32-bit uptime, first/new/restarted samples, affinity, byte display, pause/visibility, tab re-entry and HTTP errors. No rendered-browser verification was performed.
- All **24 native tests** passed. Two new tests compile the actual C snapshot/JSON adapter against RTOS/HTTP substitutes: 64-bit fields, affinity/idle identity, states for both running cores, byte units, copying names before lock release and the capacity/503 path. These do not establish actual RTOS scheduling or lock timing.
- CMake reconfigured with stats enabled in both defaults and the generated local configuration. **All 775 application/SDK translation units were recompiled**, and all **64 source-built static archives** regenerated for the changed FreeRTOS ABI. Generated Ninja response files were materialized for archive/link commands. The sequential generated-command workaround remains necessary; this was not a normal clean `idf.py build`. Supplied SDK binary libraries remain the vendor binaries.
- Linked successfully; esptool generated the ESP32-S3 image and validated checksum/hash. Embedded version is `0.5.0`, the Tasks page/API are present, and both OTA slots retain 55% free.
- Not flashed/OTA-installed. Actual task/load readings, stack reserves, snapshot overhead, watchdogs and heap/stack margins remain board checks in [testing.md](testing.md). The adapter uses ESP-IDF 5.3 private kernel-lock wrappers to preserve task-name lifetime while copying; formatting/allocation/network happen after release.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/ecmpilot_gw.bin` (0.5.0) | 913456 | `39cf2de8dfb69c46bdcb1ec8fbe99453ffd0d8399c165600a03b64e09ff41bcb` |

## Compact web layout — 2026-10-27

Firmware **0.4.1** expands Log to the full content width and compacts Varstore rows/controls. Descriptions move to signal-name tooltips; the Invalidate button is removed, while description filtering and the invalidation API remain available.

- All **20 existing web logic tests** passed after adapting the manual-control check to the smaller button set. No firmware backend changes were made.
- CMake reconfigured and project sources/embedded HTML compiled and linked using the existing sequential generated-command workaround. esptool validated the ESP32-S3 image; embedded version is `0.4.1` and both OTA slots have 55% free.
- Checked that the image embeds the new width rules and omits the Invalidate button. No rendered-browser verification, OTA or flashing was performed.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/ecmpilot_gw.bin` (0.4.1) | 906688 | `040474afb71175356219697effbe2fb858f3f49ef713ca2132ff218a0ff5f588` |

## Buffered web log — 2026-10-27

Firmware **0.4.0** adds normal ESP log capture, a fixed RAM ring and the Log tab. Capture starts at `app_main`, preserves serial output and excludes direct console text; the setup AP password now uses serial-only `printf`. No LED/GPIO changes are included.

- All **22 native tests** passed: four actual ring/capture tests, six settings tests and twelve protocol tests. Logging coverage includes empty/paged reads, wrap/lost counts, UTF-8-safe truncation, previous serial sink invocation and simultaneous readers/two writers across 6,000 publications. Host substitutes the ESP log-hook installation and random source; this does not establish actual ESP logging integration.
- All **20 web logic tests** passed: eleven combined-page tests, five varstore tests and four log tests. Log checks cover visibility/pause, cursor continuation, backlog paging, ANSI removal, Clear view, overwritten records, reboot and connection errors. This is a DOM/HTTP harness, not a rendered browser or ESP HTTP server.
- CMake reconfigured; project/runtime sources, embedded HTML and application descriptor compiled and linked with generated Xtensa commands. esptool generated the ESP32-S3 application and verified checksum/validation hash. The image fits both OTA slots with 55% free. The restricted-session sequential build workaround below remains in use.
- Not flashed or OTA-installed. Board verification still needs real serial/log-hook output, web API integration, scheduling/critical-section timing, stack margins and heap/watchdog behaviour. See [testing.md](testing.md).

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/ecmpilot_gw.bin` (0.4.0) | 906688 | `dbfc6469f19a1c4aa0d81b52c52a67ae09a17036718d34be6feba79be9b126f1` |

## Configurable hostname — 2026-10-27

Firmware **0.3.1** adds persistent hostname (default `ecmpilot-gw`) through Wi-Fi setup, `GET/POST /api/wifi` and `wifi hostname [name]`. The ESP-IDF 5.3 limit is 32 bytes. The Wi-Fi v1 blob remains 792 bytes; hostname is a separate NVS string key. Network interfaces receive the name before initial start and after disconnect during reconfiguration. UDP identity remains independent.

- All **18 native tests** passed: six actual-settings tests with a deterministic NVS/mutex substitute, plus the existing twelve actual-code protocol tests. Settings coverage includes legacy Wi-Fi storage with no hostname key, hostname-only storage, reload, uppercase normalization, omission/clear preservation, invalid labels, corrupt stored names and failed-commit RAM behaviour. These are not flash power-loss or lock-concurrency tests.
- All **16 web logic tests** passed, including hostname load/validation/save and preserving unfinished hostname edits across tabs.
- Reconfigured CMake, compiled/linked the project with the generated Xtensa commands, generated the image and verified its checksum/hash. It fits both OTA slots with 56% free. The restricted-session sequential build workaround below remains in use.
- Not flashed/OTA-installed. Actual DHCP advertisement, router DNS registration/cache behaviour and phone name resolution remain device checks in [testing.md](testing.md).

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/ecmpilot_gw.bin` (0.3.1) | 900832 | `9079c5c8a3c6dbcbbcc266a7cd226fb95b4e8f3705854567315abab2c3fdbe3d` |

## UDP / Android varstore integration — 2026-10-27

Firmware **0.3.0**, Android **0.2.0**. ESP-IDF 5.3.2 / Xtensa GCC 13.2.0.

- All **12 native protocol tests** passed using the actual firmware C codec, signal definitions, varstore and SDK cJSON. Coverage includes identity, boot/deadline/generation checks, duplicate/changed requests, manual readback, stale versus forced ages, deferred time, supersession, timeout, disable, client-scoped queries, cache capacity/retention, malformed input and byte limits. A real Windows loopback UDP test drops acceptance and checks identical retransmission causes one publication.
- Native varstore semantic/concurrent tests and all **14 web logic tests** also passed after integration.
- Android's **14 JVM tests** passed, including parsing the C-generated response fixtures and explicit test-backend gating. Offline Gradle `testDebugUnitTest`, `lintDebug` and `assembleDebug` completed successfully. Lint has zero errors and three dependency-version warnings; unavailable Android analytics preferences did not prevent the build.
- All project/runtime sources and embedded HTML compiled and linked with the generated ESP-IDF commands. esptool generated and validated the application image; both OTA slots have 56% free. The fixed 256-entry command cache occupies **32768 bytes** in internal static storage. The restricted-session sequential build workaround below remains in use.
- No flashing, OTA or APK installation was performed. The physical phone/ESP Wi-Fi path, lwIP adapter, actual task placement and runtime stack/heap still need device checks. Host loopback tests do not establish device integration.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/ecmpilot_gw.bin` (0.3.0) | 898288 | `bb05df8eb37a76741e008d3c8b700682d726b837be7be355b8c11d14714e85a7` |
| Android `app/build/outputs/apk/debug/app-debug.apk` (0.2.0) | 30091309 | `3e53112604ae7747719d5b481928e1a99ad14d10d612a38ee52a022482190dba` |

See [UDP setup and mappings](udp.md) for the opt-in test workflow and its no-CAN scope.

## Tabbed web UI — 2026-10-27

- Consolidate the embedded UI into one page with Home, Varstore, Wi-Fi setup and OTA tabs. Keep unfinished edits during navigation; pause automatic varstore polling while hidden and refresh on re-entry. Preserve `/vars` through an HTTP redirect to `/#varstore`.
- All **14** page-logic tests passed: nine combined-page/Wi-Fi/OTA tests and five signal tests. Coverage includes direct tab links, keyboard navigation/focus, unfinished edits and hidden-tab polling. HTML IDs are unique and tab/panel ARIA references resolve.
- CMake reconfigured with one embedded HTML file. Project/runtime sources, application descriptor and page were recompiled, archives/linker sections regenerated, and the final application linked and converted to an ESP32-S3 image. The generated image fits both OTA slots with 56% free. The sequential build workaround below remains necessary in this restricted environment.
- No OTA, flashing or live-browser/device verification was performed.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/ecmpilot_gw.bin` (0.2.0, tabbed UI) | 885296 | `01439b13288e3f74255caf5fe61a6fda592be3945f415821cdb02f067d1bf584` |

## Varstore integration — 2026-10-27

Project version: **0.2.0**, target ESP32-S3, workstation ESP-IDF 5.3.2 / Xtensa GCC 13.2.0.

- Native C semantic tests passed, including invalid startup, fallback/freshness boundaries, explicit invalidation, Touch, independent force/release, signed int32 limits and timestamp/sequence rollover.
- A reader held across an artificially paused publication waited until completion and returned the real value, rather than a contention fallback.
- Real Windows threads completed 150,000 producer updates and 225,000 override updates alongside 200,000 checked snapshots from two readers. Value/timestamp coherence held across a 32-bit timestamp rollover.
- All ten embedded-page logic tests passed: six Wi-Fi/OTA checks and four varstore UI checks. These use a DOM/HTTP harness, not a live browser or ESP HTTP server.
- CMake reconfigured successfully with the standalone `varstore` component. All project/runtime translation units, embedded pages and the application descriptor were compiled with CMake-generated Xtensa arguments; updated archives and linker sections were generated, and the final application linked successfully.
- The varstore object has no external atomic, semaphore or spinlock helpers; its unresolved symbols are the Xtensa local interrupt-mask routine and `memset`.
- esptool generated the final application image and validated its checksum/hash. Embedded project/version fields are `ecmpilot_gw` / `0.2.0`. The image fits either OTA slot with 57% free.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/ecmpilot_gw.bin` | 882752 | `1b46ef86c880f4d5b16db41f41010b307466d2394cd6c22730b91de4d2f1909a` |

The named-pipe restriction described below still affects normal Ninja job execution. Verification used sequential CMake-generated compile/archive/link/image commands with inherited output and existing SDK objects. This was not a clean `idf.py build`. No flashing or OTA was performed; device scheduling, HTTP integration and the new signal page still need the board checks in [testing.md](testing.md).

## Initial scaffold — 2026-10-24

Target: ESP32-S3. SDK: the workstation's ESP-IDF 5.3.2 installation, reported as `v5.3.2-dirty`; Xtensa GCC 13.2.0, esptool 4.8.1. Project version: 0.1.0.

## Completed

- CMake configured the reduced component graph, containing `runtime` and `protothreads`, without the separate `ufrm`, LVGL, `esp_lcd`, MQTT or Ethernet components.
- All seven application/runtime C/C++ translation units compiled with the generated ESP-IDF compiler arguments. The application linked and esptool generated a valid ESP32-S3 image; checksum and validation hash passed `image_info`.
- The bootloader compiled, linked and generated its image with application rollback enabled.
- The actual generated configuration was checked: main on CPU1, Wi-Fi and TCP/IP on CPU0, FreeRTOS timer service on CPU1. HTTP/REPL/network/application task affinities are additionally explicit in source.
- The generated binary partition table was decoded and checked for two application slots at `0x20000` and `0x210000`, each `0x1f0000` bytes. The initial OTA data file is 8192 bytes.
- All six tests in `node tests/web-ui.test.cjs` passed. These cover page logic through a minimal DOM/HTTP harness; they are not real-browser or device integration tests.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/ecmpilot_gw.bin` | 872576 | `c46367d0d972a4433bda0f5f000a044c285fb3de7c0e62ee20f1e287c14390df` |
| `build/bootloader/bootloader.bin` | 21568 | `455e91651c8e766054508239f788a5a4986ed2a5f87d0a25ba528bd22634fd3c` |
| `build/partition_table/partition-table.bin` | 3072 | `16f78deb46fa168ea3f8dcfa64e369e3cbae7489f54a6a362ffb8f3d3a1ae529` |

The application leaves 57% free in either OTA slot. Build artifacts are local and ignored by Git.

## Build-environment limitation

The normal `idf.py`/Ninja process driver encountered Windows named-pipe permission failures in the agent's restricted execution environment. The initial build attempt compiled the SDK objects but also encountered a compiler crash in an unused LCD component, which was subsequently excluded by restricting the project component graph.

Verification was completed by configuring CMake with the already-established cross-compiler/pthread probe results, then executing its generated compiler/archive/link/image commands sequentially with inherited output. Existing SDK objects were reused; application/runtime sources were recompiled after the final changes. Bootloader commands were also executed directly. This confirms the final source compilation, linking, binary generation and image size; it is **not** a claim that a normal clean `idf.py build` completed in that restricted session. Standard ESP-IDF build instructions remain the supported workflow in a normal developer shell. No SDK sources were patched to achieve the CPU split.

## Still requiring a board

No flashing or live hardware tests were performed. Native USB enumeration, real CPU task placement, Wi-Fi scanning/failover, setup AP reachability, web requests against the ESP HTTP server, interrupted OTA, boot rollback, watchdog behaviour and power consumption remain unverified. Follow [the board checks](testing.md) before treating the scaffold as device-tested.
