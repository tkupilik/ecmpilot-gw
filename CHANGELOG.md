# Changelog

## 0.5.0 — 2026-10-27

- Add the current setup AP SSID/password to Wi-Fi setup, with password reveal/edit, validation, persistent storage and restart after an actual password change. Preserve existing credentials and older API callers; keep station passwords hidden.
- Raise the CPU1 `Tmr Svc` task to priority 24 (highest), with a compile-time guard. Keep firmware version unchanged for these additions.

- Enable FreeRTOS trace/runtime stats with 64-bit ESP timer counters and core affinity reporting.
- Add read-only `/api/tasks` snapshots with name/ID, state, priorities, affinity, cumulative CPU time, idle-task identity and stack minimum-free bytes/base address. Copy task names under the SDK kernel lock; bound snapshots to 64 tasks.
- Add a Tasks tab with interval task/core/device load, visible-only polling every two seconds, manual refresh and JSON export. Reset sampling after reboot, tab re-entry and counter changes.
- Add native snapshot adapter and UI calculation/polling tests.

## 0.4.1 — 2026-10-27

- Expand the Log panel to the full content width.
- Compact Varstore into single-line rows with smaller controls; move descriptions to signal-name tooltips and remove the Invalidate button. Keep description filtering and the invalidation API available.

## 0.4.0 — 2026-10-27

- Capture normal ESP logs from `app_main` into a fixed 64-record RAM ring while preserving serial output and existing log levels. Bound each record to 255 text bytes with UTF-8-safe truncation.
- Add read-only, cursor-based `GET /api/log` with boot identity, paging and overwritten-record reporting.
- Add a Log tab with visibility-aware live updates, pause, manual refresh, follow output, local Clear view and text download. Bound browser history and show restart/loss markers.
- Keep the setup AP password on serial only, outside the captured ESP log stream.
- Add native ring/capture/concurrency tests and web log logic checks.

## 0.3.1 — 2026-10-27

- Add persistent hostname to Wi-Fi setup, the Wi-Fi API and `wifi hostname [name]`; default to `ecmpilot-gw`.
- Validate 1–32 ASCII letters/digits/hyphens and normalize to lowercase. Apply before initial Wi-Fi start and on reconnect; show in status.
- Keep the version-1 Wi-Fi blob unchanged using a separate hostname NVS key. Existing profiles/older API callers remain compatible; clearing profiles preserves hostname.
- Add UI and native settings checks for defaults, legacy storage, reload, validation and failed-commit behaviour.

## 0.3.0 — 2026-10-27

- Add Android UDP v0 on CPU0, port 42840, with a transport-independent C codec and 1024-byte messages.
- Map observations, incoming command targets/generations and UDP counters to varstore.
- Add identity/boot/deadline checks, generations, duplicate suppression, a 256-entry result cache, client-scoped queries and readback confirmation/timeout.
- Start read-only; expose `varstore-test` controls only after `test.enabled=1`. Acceptance never changes observations or sends CAN.
- Add a bench setup tool, real-code native protocol tests and C-generated Android fixtures. Android 0.2.0 labels this backend TEST.

## 0.2.0 — 2026-10-27

- Add a standalone BSD-2-Clause global varstore with int32 values, monotonic freshness, explicit invalidation and independent force overrides.
- Use native atomic 32-bit words and sequence-validated snapshots; retry reads until consistent without BUSY results, semaphores or shared spinlocks.
- Keep each producer/force publication non-preemptible on its local core to prevent reader/writer priority inversion.
- Publish application and Wi-Fi process signals and add a manual test signal.
- Add an English `/vars` viewer with filtering, real/effective values, Write/Force/Release/Touch/Invalidate and JSON download, plus `/api/vars`.
- Organize the embedded UI into Home, Varstore, Wi-Fi setup and OTA tabs; preserve unfinished edits and pause signal polling while its tab is hidden. Redirect `/vars` to `/#varstore`.
- Add native multithreaded C tests and embedded-page logic tests. CAN and UDP remain future work.

## 0.1.0 — 2026-10-24

- Create an ESP-IDF 5.3.x ESP32-S3 gateway scaffold based on the `opel_dash` project structure.
- Port the standalone runtime and BSD-licensed protothreads without the separate UFRM framework or GUI.
- Pin network tasks, Wi-Fi, TCP/IP and HTTP to CPU0; application actors, console and runtime timer service to CPU1.
- Add hello/status/Wi-Fi/restart console commands and a protothread heartbeat.
- Persist an ordered list of up to eight known Wi-Fi networks; scan, connect, retry and expose a WPA2 setup AP for configuration/recovery.
- Embed an English web UI for status, profile editing/priority and firmware upload.
- Add two OTA slots, inactive-slot streaming updates, image validation and bootloader rollback with local startup confirmation.
- Add BSD-2-Clause project license, preserved dependency notices, build instructions and hardware verification steps.
- CAN, UDP, MQTT, Ethernet drivers, vehicle control and deep sleep remain future work.
