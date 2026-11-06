# Testing and validation

This is the current check guide. Old cumulative test-run diaries are not a
firmware specification. For expected behavior use [CAN requests](can-requests.md),
[vehicle control](vehicle-control.md) and the feature pages in the [index](README.md).

## Host checks

Run from the repository root. These commands do not open a vehicle CAN adapter:

```powershell
./tests/run-varstore.ps1
./tests/run-udp.ps1 -PythonPath python

node tests/web-ui.test.cjs
node tests/varstore-ui.test.cjs
node tests/log-ui.test.cjs
node tests/tasks-ui.test.cjs
```

The native suite compiles production C/C++ boundaries with host substitutes for
ESP/RTOS hardware. Coverage includes settings/NVS, UDP parsing and idempotency,
controller PT timing and budgets, raw readback, RX→varstore→UDP conversions,
automatic refresh/limiter policy, logging, task/heap JSON, HTTP varstore streaming
and loopback GVRET. The JavaScript checks exercise actual embedded-page logic;
they do not establish browser rendering or physical ESP scheduling.
Install `tools/requirements-can.txt` for the Python suite, including the Jinja2
vehicle-card checks. MQTT regression checks keep advancing source ages out of
ordinary state/discovery payloads and render diagnostic freshness separately.
`test_ha_commands.py` drives MQTT requests through the real shared admission
cache and controller. It covers physical readback, duplicate/Busy retention,
rejected stale/retained requests, isolation from phone receipts and button IDs.

DBC checks require [CAN Python dependencies](../tools/requirements-can.txt):

```powershell
python -m unittest discover -s tests -p test_info_can_dbc.py -v
```

Decoder-generation commands, pinned source inputs and confidence scope are in
the [INFO DBC guide](../dbc/README-info.md) and [HS1 guide](../dbc/README.md).
Capture analysis helpers are offline; preserve original CSVs and input hashes.

Build with ESP-IDF 5.3.2 using `idf.py build` / `./dev.ps1 build`. Inspect the
application size/map and both OTA slots. Host tests and build success do not
establish electrical CAN compatibility or physical vehicle response.

## Current board checks

| Area | Check |
| --- | --- |
| First connection | AP is disabled by default; configure station Wi-Fi over serial. If AP is enabled, verify its saved password and 192.168.4.1 access |
| Task/heap | CPU0 network, CPU1 CAN/application, timer service priority 24; inspect free/largest internal/DMA blocks, stack reserves and loss counters |
| Varstore | 14 signals/page, filter/export, force/release without changing producer values; repeated refreshes must not grow heap usage |
| Passive operation | Opening phone, reconnecting, polling, HA publication and SavvyCAN connection generate no vehicle-control TX |
| INFO RX | Default 125 kbit/s normal, TX15/RX16; compare data/units/ages with captures and vehicle; there is no firmware capture playback |
| Vehicle actions | Explicit wake/refresh/charge/preconditioning follow bounded request/readback rules; timeout or new unrelated traffic cannot count as success |
| Automation | Default off, 60 min standby/5 min charging/50% auxiliary SOC; manual refresh reloads timer at completion; low SOC inhibits further automatic work |
| Charge limit | Requires refresh, fresh raw SOC/charging, delayed-mode readback; acceptance is not a measurement of physical charge cessation |
| GVRET | TCP 23, single client, 64-frame queue; physical RX plus accepted local TX submissions; no remote TX/reconfiguration |
| Network/HA | Reconnect and saved profiles/settings, HTTP/UDP reachability, MQTT discovery, original observation ages and explicit refresh receipts; passive traffic never requests vehicle control |
| Reset/log | Live cursor/loss markers, previous-boot retained entries and actual reset reason |
| Power | Measure whole-board current independently; deep sleep removes Wi-Fi; CAN wake is owner-confirmed on the installed GW; verify the automatic inactivity sequence separately |
| OTA/recovery | Validate images/slot selection; interruption and rollback tests belong on a bench with serial recovery |

Only enable live control intentionally for a vehicle test. Offline decoder checks and
forced values are useful UI/codec checks but cannot prove a raw ECU acknowledgement.
Record build identity, settings, selected profile and the relevant capture when
reporting a failure; compare the expected raw reply, not only driver `ESP_OK`.

## Known limits

- INFO energy, HV voltage, battery temperatures and measured cabin temperature
  remain unidentified; available setpoints are not cabin measurements.
- Standalone refresh updates GW/phone data but not cloud in the owner's test.
  A successful stored charging-time change also updates cloud data (owner-confirmed
  2026-11-04); preconditioning is likewise associated with updates. Upload-only
  commands and unchanged-time writes remain unvalidated.
- Vehicle wake-refusal thresholds remain uncharacterized. GW deep-sleep CAN wake
  is owner-confirmed; automatic entry uses the new [inactivity policy](low-power.md).
- Dated `*-verification.json` files retain results of specific checks. Rerun the
  relevant suite after a change instead of treating those files as a current pass.
