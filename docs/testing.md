# Verification

## Workstation checks

Run the dependency-free UI logic tests with `node tests/web-ui.test.cjs`. They execute the page's actual JavaScript using a small DOM/HTTP harness, covering profile priority, password retention/removal, renamed SSIDs, profile limits and the OTA request. They do not test browser rendering or the ESP HTTP server.

Build against ESP-IDF 5.3.2 with `idf.py build` / `dev.ps1 build`. Inspect `build/ecmpilot_gw.bin`, `build/partition_table/partition-table.bin`, generated `sdkconfig`, and the map/component list. The application must fit both 1984 KiB slots; no separate `ufrm` or LVGL component may be linked.

The first implementation is not hardware-verified. Record board revision, flash size, IDF version, serial port, application hash and observations when running the checks below. Do not infer Wi-Fi/OTA correctness from compilation alone.

## Board checks

1. **Empty configuration:** serial-flash a board with the supplied layout and empty NVS. Confirm `hello`, `status` and advancing heartbeat; console must report CPU1. Verify the printed AP SSID/password work and the UI loads at `192.168.4.1`.
2. **Profiles and persistence:** save two known SSIDs, reorder them, reboot and confirm priority when both are in range. Check a full 32-byte SSID, a quoted SSID, an open network and retained passwords. Invalid/duplicate profiles must not replace the previous configuration. Web GET and `wifi list` must not reveal passwords.
3. **Failover:** put the first SSID out of range and confirm the second is used. Give the first a wrong password and confirm its backoff allows the second. Remove all APs for at least 30 seconds and confirm setup recovery; restore a known AP and check reconnection. Verify no task watchdog/restart loop over repeated cycles. A healthy connection should not roam just because a preferred SSID reappears.
4. **Affinity:** inspect task affinity with a debugger/FreeRTOS task view: `wifi`, `tcpip`, `sys_evt`, `network` and `httpd` on CPU0; `application`, REPL and `Tmr Svc` on CPU1. Runtime event handlers assert their expected core.
5. **OTA success:** build a distinguishable version, upload only its application binary, and confirm slot alternation, new version and local startup confirmation. Reboot again to ensure the updated image stays selected. Repeat in the opposite direction.
6. **OTA failure:** reject a wrong-project image and an oversized upload. Interrupt upload and verify the old image still boots. On a bench device with serial recovery available, upload a deliberately failing startup image and verify rollback to the previously confirmed slot. Initial rollback requires a previous valid image.
7. **Web failure:** send malformed/oversized configuration, wrong-origin browser writes and truncated OTA requests; verify rejection without corrupting profiles or changing boot selection.
8. **Recovery and power:** verify serial reflashing remains possible. Measure whole-board current independently. This scaffold requests modem power save but does not implement vehicle sleep, CAN wakeup or a complete low-power policy.

No CAN transceiver or vehicle is needed for these checks. Run destructive OTA interruption/rollback experiments on the bench.
