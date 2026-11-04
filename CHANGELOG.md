# Changelog

## Unreleased — 2026-11-04 (firmware version unchanged)

- Add an opt-in Charge limit dependent on automatic refresh, target default 80%.
  A parallel CPU1 PT uses fresh CAN observations and the serialized deferred-mode
  sequence, preserves the native start time, respects explicit requests, and
  bounds retries/cancels changed settings. Expose web/UDP/varstore/HA diagnostics;
  migrate original refresh settings with limiter initially off.
- Bring MQTT observations into line with the phone: retain last known CAN data,
  expose per-field age/stale attributes, BSI, doors/locking, trips/consumption and
  native preconditioning programs. Keep the bridge strictly publish-only.
- Add a Home Assistant web tab with persistent broker hostname/IP, port,
  credentials, enable switch and publication interval. Apply changes on CPU0
  without reboot; redact stored passwords and preserve omitted credentials.

## Unreleased — 2026-11-03 (firmware version unchanged)

- Carry a session-scoped uint32 correlation counter through UDP receipts,
  varstore, controller children and CAN logs. Deduplicate a logical operation
  even with a new message ID; allow result queries by correlation. Retain Busy
  refusals and guard full-cache admission against queued duplicates executing
  later. Echo correlation on passive reads without requesting vehicle CAN TX.

- Add an optional automatic-refresh timer to the existing CPU1 control process,
  configured through varstore, web and Android UDP settings. Default off, 60 min
  standby / 5 min charging / minimum 50% auxiliary SOC. Clamp on charging changes,
  pause for low SOC, prioritize explicit actions, and reload the interval only
  after a manual/automatic refresh sequence finishes. Persist configuration with
  revision checks and retain explicit receipts before reusing the controller.

- Analyze the owner-labelled continuous AC-charge capture against the final
  delayed-mode transition and cloud energy references. Document why31A is mode,
  not active charging; review3E1 as an unknown EV layout and preserve raw bytes.
  Add reproducible bounded energy/float searches; no HV-energy signal is inferred
  from an unverified numerical coincidence.
- Decode396 byte1 bits4..2 as reported charging HMI, corroborated by two
  start/delay captures, driving contrast and the matching HS3 source field.
  Normalize through the separate vehicle task to the existing phone/HA states,
  retaining source age and Force behavior. Unknown codes stay unknown; fault
  and finished values are source-only until physically observed. This fixes the
  missing charge-state producer; it does not eliminate BSI cached/default data.

- Expose four stored preconditioning programs through passive UDP reads and an
  explicit compare-and-set operation. Reuse the bounded wake/PT sequence,
  preserve ASAP/reserved bits, and confirm edits from both BSI status frames.
  The captured 07:30 Mon/Wed/Thu slot matches MyOpel and is disabled. Actual
  programmed operation after a new write still needs vehicle validation.
- Decode BSI aperture/locking and trip/consumption fields into source-grouped
  varstore signals. Add passive vehicle.details.get without increasing the
  existing 1024-byte UDP envelope; keep unknown/stale readings distinct.
  Source layouts are covered by host tests; physical field validation remains.

- Add immediate preconditioning Start/Stop from explicit climate.set requests.
  Preserve all four BSI program slots, wake/settle BSI and send31B plus11B through
  the single TaskCan mailbox. Confirm from new raw2E0 ASAP and active/inactive
  feedback; an ASAP acknowledgement alone is not success. Bound the operation
  to30 seconds, with at most3 request pairs and10 wake intents.
- Decode2E0/0E0 into bsi.precond.* and climate.state; update the scoped DBC status
  overlay while retaining upstream source. Cabin temperature remains unknown.

- Export actual BSI PowerManagement and its independent CAN receipt age in the
  optional UDP bsi object. Preserve aged numeric climate setpoints with original
  timestamps so Android can show cached values in grey instead of losing them.
- Correct Corsa-e INFO 31A range from 1 to 2 km/count: the 57% MyOpel reference
  and capture require 120 km from raw 60. Update the reviewed DBC export and
  cover the real capture through CAN RX, varstore and UDP; Android/HA units stay
  unchanged. Preserve upstream source definitions and unavailable range codes.
- Replace manual vehicle state dispatch with cooperative RunRequest, WakeBsi,
  RefreshVehicle, SetChargeMode and SubmitFrame protothreads. Share one saturating
  task countdown across spawned children; keep a separate 25 s operation watchdog.
- Retry wake up to 10 times at 100 ms, require fresh raw Normal plus 200 ms settling
  and a new RX generation, and bound refresh/mode retries to 3 each. Preserve
  one wake budget across re-wakes and one TX path through TaskCan.
- Revoke timed-out mailboxes, ignore late callbacks, expose diagnostic countdowns,
  per-stage attempt counts and explicit failure reasons. Lost mode readback retries
  the same 35B without another refresh while BSI stays ready. Preconditioning remains
  unimplemented; shared PT children are ready for its later sequence.

## Unreleased — 2026-11-02 (firmware version unchanged)

- Make all UDP information/state/result reads passive, including reconnects.
  Add explicit idempotent `vehicle.refresh` and experimental INFO charging-mode
  control through varstore and an independent vehicle policy process.
- Wake BSI once per explicit action when needed, wait for Normal, send394/10,
  then for schedule changes send35B after at least460ms. Preserve fresh BSI160
  schedule flags/time, and require subsequent raw CAN readback for success.
- Decode BSI160 schedule availability/mode/time and31A secondary mode/time.
  Export cached mode/start time with independent mode age; unknown physical
  charging flow remains unknown. See docs/vehicle-control.md.

## Unreleased — 2026-11-01 (firmware version unchanged)

- Add persistent "Enable setup AP fallback" checkbox, default false for fresh
  and legacy installations. Disable AP startup/fallback when off, and stop an
  active AP after saving. Keep station profiles, hostname/password and existing
  settings APIs compatible; configure a fresh Wi-Fi profile over serial.

- Analyze AC plug-in/OBC wake: five BSI412 cause0x20 announcements, then036
  Wake-up -> Normal in34.676ms; no preceding direct OBC request or transaction
  ACK is captured. Document exact timing and standby/driving comparisons.
- Label the observed BSI cause byte in the INFO DBC without changing its layout.
  Attribute nine owner-approved unit004 messages to IC; preserve upstream sender
  provenance and keep ambiguous4A4 and older startup frames separate. Regenerate
  against five captures,53971 frames/115 IDs; adapt317 DLC8 ->5. Firmware unchanged.

- Fix the blank Varstore page: restore its controller and14-signal pagination
  after the retained-log integration replaced the wrong script. Remove the
  duplicate log controller. Add full-page tab loading and pagination/export
  regression tests; identify individual script tests by their owned DOM prefix.

- Add manual Home "Deep sleep · CAN wake" test. Coordinate CPU1 CAN uninstall
  and CPU0 Wi-Fi stop through raw varstore owner acknowledgements, then arm
  GPIO16 LOW/EXT0 only and hold TX15 recessive. No timer or automatic sleep rule.
  Restore peripherals on hand-off/entry errors; log CAN wake after fresh boot.

- Backport LCD reset diagnostics: named reset reason/code on Home/Log and a
  checksummed two-bank RTC tail of12 records from the previous boot, frozen
  before startup logs. Preserve the64-record live ring; no flash writes.
- Prepare INFO/125 kbit/s normal-mode testing. Default wake is43C/DLC2,00 80,
  matching the latest LCD source. Force its ID (401/43C) and packed payload
  through native varstore; log actual submitted bytes. Preserve validated-UDP
  one-shot policy, BSI freshness/rechecks and replay inhibition.
- Remove synthetic version5DF TX completely, including its scheduler, to avoid
  collision with the original instrument cluster. Retain old signal IDs as
  reserved descriptors; optional HSCAN1 remains passive.

- Record vehicle sleep behavior in doc/car_lowpower.md: tested HSCAN1 wake
  failure, two INFO wake paths, both monitored buses unavailable during BSI
  sleep in AC charging, and refusal after repeated wakes. No firmware change.

- Adapt LCD-bench PM/DFS/tickless-idle support with manual Home controls for
  Normal160MHz, DFS40–160MHz and light-sleep policy40–80MHz. Keep CAN running;
  its driver lock still prevents actual sleep. Publish successful-sleep counters,
  raw request/completion and errors through12 appended varstore descriptors.
- Prepare timer wake-up for future deep sleep, with no entry call or active
  trigger. Reboot always returns to Normal; power requests do not use CAN state.

## Unreleased — 2026-10-31 (firmware version unchanged)

- Make HSCAN1/500kbit/s the GW default, including the active local configuration.
  Decode HV SOC/voltage from5C3 into phone/HA fields. Keep INFO/125 optional;
  wake401/version5DF are hard-disabled on HSCAN1 and TWAI stays listen-only.

- Complete latest LCD CAN parity:12 INFO handlers, BSI power/lighting,12V battery,
  climate HMI and front-climate temperatures through native GW varstore. Preserve
  source age in derived setpoints and append45 stable signal IDs.
- Add one Internal401 per eligible valid UDP request and fixed5DF/500ms only in
  normal BSI phase1, single-shot/no TX backlog, wake log and separate counters.
  INFO uses normal mode; legacy HS1 remains passive. GVRET reports actual mode.
- Add optional CPU0 HA/MQTT discovery and24 vehicle entities with bounded outbox;
  retain no-PSRAM hardware and64-frame GVRET FIFO. No physical climate/charge control.


- Backport LCD TaskCan/CanNet acquisition to the GW BSD runtime and native global
  varstore. Keep CAN/alerts on CPU1 with Waveshare TX15/RX16.
  Retain both INFO and HS1 decoder profiles; the final default is HSCAN1/500kbit/s.
- Retain the initial nine INFO receive handlers, wheel state/press counters and capture replay
  from `info_start_jizda.csv`; live is the default. Preserve Force, freshness and
  phone units, including GW's km×10 range. No physical climate/charging control.
- Add single-client receive-only GVRET TCP23 on CPU0, a static 64-frame (1536-byte)
  internal-RAM FreeRTOS FIFO and a 1024-byte transmit buffer. Ignore incoming CAN
  TX/configuration; publish queue/loss/session diagnostics. PSRAM stays disabled.
- Remove GPIO wake-test implementation, Home controls, routes and request signals.
- Add Tasks heap capability views from LCD: total/allocated/free/minimum/largest
  block/block count for internal RAM, internal DMA RAM and absent PSRAM.
- Record LCD/GW parity and remaining MQTT/HA gap, provenance and host/device checks.

## 0.5.0 — 2026-10-27

- 2026-10-28, version unchanged: validate the INFO DBC against 31,275 real frames / 105 IDs, adapt six DLCs, add raw-only 7A4 and expose the 3E0 byte-0 SOC candidate at 70 percent. Retain the full source baseline and originals; report per-field ranges/sentinels and source discrepancies. Remaining energy on INFO is unresolved. Add reproducible capture checks without firmware changes.
- 2026-10-28, version unchanged: document INFO telematics captures in `PSA_telematic.md`, including the independently owner-confirmed 03:02 -> 03:01 charge-time update in `0x35B` and `0x160`, 82.303 ms request-to-status timing, wake/hold candidates and remaining validation. Update documentation confidence without changing firmware or generated DBC layouts.
- 2026-10-28, version unchanged: standardize process-data voltage/current/temperature on mV/mA/mC, keeping power in W and percentages in tenths. Rename affected signals/enums and update descriptors, exact CAN conversions, UDP scales/ranges, bench seeds and tests. Phone wire units remain unchanged; quarter-volt CAN readings retain full precision in mV.
- 2026-10-28, version unchanged: fix Varstore HTTP out-of-memory after adding CAN signals. Stream `/api/vars` JSON in bounded 1 KiB chunks without building a full cJSON tree/output copy; preserve the API, browser pagination/filtering and complete export. Verify the full registry with JSON allocation disabled, escaping, large registries and failed HTTP chunks.
- 2026-10-28, version unchanged: add a separate 125 kbit/s INFO-DIV DBC for all 376 source IDs, with ECU-prefixed names, 252 fixed-CAN layouts and 124 ISO-TP wire layouts. Preserve original application/alternative layouts and Apache provenance; check strict reload, synthetic bit coverage, roundtrips and short transport frames. No INFO vehicle capture validation or firmware change.
- 2026-10-28, version unchanged: decode owner-confirmed HS1 0x5C3 SOC and cell-sum-supported voltage into phone varstore inputs at 500 kbit/s, with startup voltage gating, unavailable/range checks and CAN producer ownership. Add diagnostic-only candidate HVAC/outside/odometer handlers and document the phone field inventory. Verify captured payloads through the real varstore/UDP path; physical heating/charging commands remain unimplemented.
- 2026-10-28, version unchanged: add the explicit Home CAN wake-up pulse test. Queue one request through varstore, stop TWAI on CPU1, drive TXD with three 10 us ROM busy-waits inside an IRAM critical section, then restore listen-only routing/reception. Publish completion/error and RX pause, expire delayed requests, and verify request/UI behaviour offline. No flashing or vehicle transmission during development.
- 2026-10-28, version unchanged: document physical CAN wake-up timing and a proposed ESP32 RMT test through the Waveshare transceiver; record the owner-run 68-ID scan with no vehicle RX. No firmware change or new vehicle transmission.
- 2026-10-28, version unchanged: replace the HS1 reviewed-field whitelist with a database-first export for all three captures: 89 IDs, 1,161 signals, ECU-prefixed names, unverified/default-only fields and in-bounds prefixes retained, 12 proven out-of-payload fields excluded. Preserve alternatives, cell/temperature corrections, Apache provenance and a field audit; verify all 181,002 frames. Firmware unchanged.
- 2026-10-28, version unchanged: add a standalone PCAN recorded-ID wake scanner with hardcoded 68/89-ID capture profiles, bounded one-enqueue trials, finally-protected hardware resets, wake-candidate stop and JSONL evidence. Verify cleanup/stop behavior offline; the owner later completed the 68-ID profile without vehicle RX.
- 2026-10-28, version unchanged: analyse the passive OBD 6/14 HS1 capture and add a reproducible, capture-scoped DBC with 19 reviewed messages and raw fallbacks for all remaining observed IDs. Record sender candidates, field confidence, source corrections, Apache-2.0 provenance and full-capture decoding checks. No firmware mappings or vehicle commands are added.
- 2026-10-28, version unchanged: paginate Varstore at 14 matching signals per page, with Previous/Next and page/range indicators. Reset the page when filtering; retain page and edited inputs during polling. JSON export still contains all signals.
- Add a component-based Waveshare board power budget, using the owner's whole-board readings only as a checksum.

- 2026-10-28, version unchanged: add a CPU1 passive CAN RX runtime process (priority 8), listen-only TWAI with no transmit API, 128-frame RX queue and bounded batches. Default Waveshare GPIO15/16 and configurable 500 kbit/s starting rate.
- Publish receiver health/loss counters and diagnostic last-frame data through varstore. Reserve an acquisition-only vehicle mapping module; no vehicle signals or commands are inferred yet.
- Record the architectural rule: every process exchanges application data through varstore; control/derived-data processing is a separate consumer process, never a direct CAN handler callback.

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
