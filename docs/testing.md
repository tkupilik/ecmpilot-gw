# Verification

The 2026-11-03 vehicle-control rewrite uses the actual protothread headers in host
checks. `tests/test_vehicle_control.py` advances a virtual clock through the real
C policy/codec/native varstore. It covers shared countdown saturation, 100 ms wake
spacing, 10-attempt budgets across re-wakes, 200 ms stable Normal, bounded retries,
exact charge payloads/preamble, stale/forced/wrong replies, duplicate UDP requests,
expired mailboxes, late completions and backend suspension. It uses no vehicle or
CAN adapter. See [current sequence/timing](vehicle-control.md).

`tests/test_vars_http.py` exercises the actual streamed `/api/vars` serializer with a substituted HTTP chunk sink: complete schema, all real signals, signed/forced/stale values and saturated ages, long escaped UTF-8 metadata, 400 synthetic signals, allocation-disabled cJSON, and failed-chunk cleanup. JSON scratch memory remains bounded as the registry grows; browser pagination/filtering/export keep the existing full-response contract.

`tests/test_can_decode.py` verifies captured HS1 battery/HVAC payloads through the actual RX → varstore → UDP path, producer ownership, startup/sentinel checks, freshness and separation of candidate fields from phone observations. The current mapping and device test instructions are in [CAN telemetry](can-telemetry.md).

`tests/test_can_info.py` exercises the INFO handlers and the actual RX → native
GW varstore → phone UDP pipeline, including units, sentinels, bad DLC, profile
separation, manual ownership, replay/live transitions and Force preservation.
Replay is checked against all selected frames of `logs/info_start_jizda.csv`.
`tests/test_gvret.py` exercises the real C protocol/worker through loopback TCP:
handshake, 9520 captured frames, partial sends, 64-slot overflow, stalled client,
second-client rejection, session isolation and ignored CAN TX/config commands.
The former GPIO wake firmware/API/UI tests were removed with that experiment.

## Workstation checks

Run the dependency-free UI logic tests with `node tests/web-ui.test.cjs`, `node tests/varstore-ui.test.cjs`, `node tests/log-ui.test.cjs` and `node tests/tasks-ui.test.cjs`. They execute the embedded page's actual JavaScript using a small DOM/HTTP harness, covering tab selection/direct links, preservation of unfinished edits, Wi-Fi/OTA controls and signal display, int32 validation, ownership controls, force/release, filtering, visibility-aware polling and HTTP error recovery. Log coverage includes cursor continuation, backlog paging, pause/visibility, restart/loss markers, ANSI removal, local clearing and connection failure. Task checks cover heap rendering/refresh and zero PSRAM, per-core normalization, 64-bit uptime, first/new/restarted samples, affinity, stack byte display and HTTP recovery. They do not test browser rendering or the ESP HTTP server.

On Windows, run `./tests/run-varstore.ps1`. This compiles the actual component with Clang from the installed ESP tools (or pass `-ClangPath`), without a Windows SDK or CRT. It checks initialization, fallback, age boundaries, Touch, Invalidate, force before/after writes, release, int32 limits, timestamp/sequence rollover, and a paused publication that must be retried. Real concurrent threads perform 150,000 producer updates and 225,000 force/release updates while two readers check 200,000 coherent snapshots, including both halves of timestamps crossing the 32-bit boundary. Generated files stay in ignored `build-host-tests/`.

Build against ESP-IDF 5.3.2 with `idf.py build` / `dev.ps1 build`. Inspect `build/ecmpilot_gw.bin`, `build/partition_table/partition-table.bin`, generated `sdkconfig`, and the map/component list. The application must fit both 1984 KiB slots; no separate `ufrm` or LVGL component may be linked.

Version 0.5.0 is not hardware-verified. Record board revision, flash size, IDF version, serial port, application hash and observations when running the checks below. Do not infer device correctness from host tests or compilation alone.

Use a Python environment with `tools/requirements-can.txt` installed for DBC/capture checks (the workstation Python312 has these dependencies). Run `./tests/run-udp.ps1 -PythonPath python` for native settings/hostname tests using an in-memory NVS substitute (defaults, legacy Wi-Fi blob, reload, omission preservation, validation and failed commit), plus the actual C codec/storage/parser tests, including loopback UDP, deduplication, deadlines, generations, cache capacity, readback and malformed input. The same runner compiles the actual log ring and capture hook: four log tests cover paging, overwritten records, UTF-8 truncation, serial sink preservation and simultaneous writers/readers (6,000 publications). SDK log installation is substituted on the host; ESP task scheduling and physical serial output require a board. Five CAN tests exercise the actual RX-to-varstore publication code, including malformed/RTR inputs, byte cleanup, freshness, Force preservation and no invented vehicle data; the physical driver is not simulated. Three task-adapter tests exercise the actual C snapshot/JSON handler with substituted RTOS/HTTP boundaries, including 64-bit fields, byte units, name lifetime across lock release capacity failure, heap capability views and zero PSRAM. See [the UDP bench guide](udp.md) for phone setup and test signal mappings.

## Board checks

1. **Empty configuration:** serial-flash a board with the supplied layout and empty NVS. Confirm `hello`, `status` and advancing heartbeat; console must report CPU1. Verify the printed AP SSID/password work and the UI loads at `192.168.4.1`.
2. **Profiles and persistence:** save two known SSIDs, reorder them, reboot and confirm priority when both are in range. Check a full 32-byte SSID, a quoted SSID, an open network and retained passwords. Invalid/duplicate profiles must not replace the previous configuration. Web GET and `wifi list` must not reveal station-network passwords. The setup AP password is deliberately available through Wi-Fi setup.
3. **Failover:** put the first SSID out of range and confirm the second is used. Give the first a wrong password and confirm its backoff allows the second. Remove all APs for at least 30 seconds and confirm setup recovery; restore a known AP and check reconnection. Verify no task watchdog/restart loop over repeated cycles. A healthy connection should not roam just because a preferred SSID reappears.
4. **Affinity:** inspect task affinity with a debugger/FreeRTOS task view: `wifi`, `tcpip`, `sys_evt`, `network` and `httpd` on CPU0; `application`, REPL and `Tmr Svc` on CPU1. `Tmr Svc` must have current/base priority 24 (highest valid priority); the build guards this configuration. Runtime event handlers assert their expected core.
5. **OTA success:** build a distinguishable version, upload only its application binary, and confirm slot alternation, new version and local startup confirmation. Reboot again to ensure the updated image stays selected. Repeat in the opposite direction.
6. **OTA failure:** reject a wrong-project image and an oversized upload. Interrupt upload and verify the old image still boots. On a bench device with serial recovery available, upload a deliberately failing startup image and verify rollback to the previously confirmed slot. Initial rollback requires a previous valid image.
7. **Web failure:** send malformed/oversized configuration, wrong-origin browser writes and truncated OTA requests; verify rejection without corrupting profiles or changing boot selection.
8. **Recovery and power:** verify serial reflashing remains possible. Measure whole-board current independently. This scaffold requests modem power save but does not implement vehicle sleep, CAN wakeup or a complete low-power policy.
9. **Varstore:** open the Varstore tab and confirm automatic values/ages update. Write `demo.value`, wait over five seconds and confirm invalidity. Touch must restore validity; API `invalidate` must clear it. Force a heartbeat, verify the effective value stays fixed while real heartbeat advances, then Release and confirm the latest real value appears. Force an expired manual sample, wait, Release and confirm it is still expired. Reboot and confirm forces clear. Check Wi-Fi RSSI becomes invalid when offline. Confirm no watchdogs, heap growth or affinity changes during repeated refresh/commands; invalid IDs, fractional/out-of-range values and writes to producer-owned signals must be rejected by the HTTP API.

10. **UDP:** configure the physical phone with the gateway IP/42840 and confirm monitoring-only defaults. Seed test inputs using `tools/udp_bench.py`, Refresh and check the phone's TEST indicator. Start climate, observe one `request.climate.generation` increment, then force observed `climate.state` to confirm. Test no-readback timeout, deferred mode/time, test-mode disable, client identity changes, reconnect and reboot. Verify the UDP task is on CPU0 and telemetry retains its real age. Monitor heap during repeated requests.

11. **Hostname:** upgrade with an existing Wi-Fi list and confirm default `ecmpilot-gw` is sent in DHCP without losing profiles. Set another name under Wi-Fi setup, save/reconnect, inspect router lease/DNS and verify the phone resolves it. Reboot and check persistence. Invalid/oversized/dotted names must not replace settings. Console `wifi hostname [name]` must agree with the UI; `wifi clear` must preserve hostname. Where router DNS registration is disabled, IP access remains the expected fallback. Verify the setup AP still works at `192.168.4.1` and the UDP gateway ID does not change.

12. **Web log:** open Log and compare normal ESP application/network messages against serial. Confirm earlier boot/ROM output and the setup AP password appear only on serial. Generate over 64 log records while paused, resume and check the overwritten-record marker; test Refresh, Follow output, Clear view and text download. Switch tabs and verify log polling stops; return and check cursor continuation. Disconnect/reconnect the browser and reboot the gateway to verify error recovery/restart markers. Monitor heap, stack margins and watchdogs during concurrent network/application logging, refresh and OTA.

13. **Tasks:** verify Internal RAM, overlapping Internal DMA RAM and zero PSRAM rows, all byte fields, minimum free and largest block across allocations. Do not sum capability rows. Open Tasks and verify names, priorities, CPU0/CPU1/Any affinity, both current tasks marked running and stack reserve bytes. The first sample must show unknown load; subsequent samples should show idle-dominated cores at low load. Apply a known pinned bench workload, check that 100% means one core and total load averages the two cores. Verify cumulative counters continue beyond 72 minutes, new/deleted tasks update correctly, re-entry/reboot resets the baseline, hidden/paused tabs stop polling and JSON export agrees with the table. Check heap, stack margins and watchdogs under repeated snapshots; the snapshot itself briefly locks the RTOS and scans unused stack space.

14. **AP password:** verify an existing generated password survives the upgrade, is masked/revealable in Wi-Fi setup and is absent from the web log. Leave blank/save unchanged and confirm no restart/password change. Enter a new 8–63 printable ASCII password, Save and confirm the response precedes restart; reconnect the AP with the new password and reboot again to check persistence. Reject invalid lengths/control/non-ASCII characters without replacing any settings. Clearing station profiles and older API calls omitting `ap_password` must preserve it. Check a 63-character password and recheck the Tasks priority after restart.

15. **CAN RX:** verify `CanTask` on CPU1/priority 5 and `canAlert` on CPU1/priority 6, `Tmr Svc` priority 24 and the selected bitrate/pins against the board/bus. Compare standard/extended data payloads with a second CAN tool; check short DLC zero-fill, RTR counting, INFO decoded observations against the vehicle, and ageing through a traffic pause. Start with `can.replay_enabled=0` and release old forces; enable replay separately to test local demo behaviour. Verify the default HSCAN1/500kbit/s listen-only profile and SOC/voltage from5C3; INFO normal mode and single-shot401/5DF rules below apply only when selecting INFO/125kbit/s. Load the bus, monitor queue/FIFO loss counters, other task responsiveness and stack margins, then repeat during OTA. See [can.md](can.md).

The CAN receiver checks require a second CAN tool or a suitable bench bus; all other checks can run without a vehicle. Run destructive OTA interruption/rollback experiments on the bench.

16. **GVRET:** connect SavvyCAN to gateway TCP23. Confirm one RX-exported CAN0 at
500 kbit/s by default (125kbit/s for optional INFO), physical payloads/timestamps and live capture while local replay is
enabled. Check `gvret.queue_capacity=64`, `gvret.memory_external=0`, upstream CAN
loss counters and `gvret.dropped` under Wi-Fi load. Reject a second client while
busy; reconnect without old records. Verify TX/config requests cannot transmit
or reconfigure CAN. Monitor both task stack reserve and Tasks heap rows.

17. **LCD parity (optional INFO profile):** with live INFO125kbit/s, verify12V voltage/SOC independently of
HV fields,2AD/350 setpoints and evaporator against the phone. Stop source traffic
and confirm derived timestamps do not refresh. With BSI not normal, each valid
UDP poll should produce one401 `00 02` and one timestamped BSI wake log. Once
phase1 arrives, wake attempts stop and5DF repeats500ms. Phase3/standby/stale BSI
must stop5DF. Replay disables all TX; a physical5DF inhibits only version TX.

18. **Optional HA:** configure and enable the broker on the web Home Assistant
tab and verify MQTT/haPub on CPU0, 55 discovery entities, online/LWT offline,
latched values with original age/stale attributes, null availability, reconnect
discovery, Force values and heap/stack margins. Save/reconnect without reboot;
verify password preservation/removal and disabled MQTT. HA publishing itself
must not increment vehicle/wake counters. See [HA configuration](ha.md).
No live broker test was performed here.


## Optional setup AP fallback

A fresh or upgraded device without the new `ap_enabled` key must run STA-only;
no AP beacons should appear, with or without station profiles. Configure a fresh
unit through `wifi add` over serial. Known profiles must continue connecting.
On Wi-Fi setup, check Enable setup AP fallback, save and verify that the setting
survives reboot. The AP starts with no profiles or after30seconds offline.
Uncheck/save while AP is active: it must stop and stay off across reboot and
station disconnects. The existing station interface must still reconnect.
Check that profile/password edits and legacy API requests omitting `ap_enabled`
preserve it. No current saving is claimed until the modified board is measured.
