# Verification record

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
