# Android UDP and varstore bench testing

Firmware **0.3.0** implements UDP v0 on IPv4 port **42840**. Use Android **0.2.0** for the explicit varstore test backend. No CAN access or physical vehicle command is implemented.

## Connect and test

The UDP task, socket, protocol, command cache and request-signal producer run on **CPU0**; application/vehicle work stays on CPU1. The listener binds station and setup-AP interfaces and replies to the actual sender's IP and ephemeral port. Android sends unicast requests to the configured IP. There is no discovery or MQTT binding.

Identity is `ecmpilot-` followed by the Wi-Fi station MAC in lowercase hex; a random 128-bit boot ID changes at restart. The app learns identity with `info.get`. Use **Forget saved gateway** when moving from the simulator or another board.

Default mode is monitoring only: `test.enabled` starts invalid/false, info has no control operations, and observations are unknown. UDP reads never refresh observations or wake a vehicle.

After installing the firmware and Android APK, seed reproducible test inputs:

```powershell
python tools/udp_bench.py --host 192.168.1.195 seed
python tools/udp_bench.py --host 192.168.1.195 state
```

Use the board's actual IP. The script uses HTTP `/api/vars` to force parked/awake status, SOC 68.2%, cabin 16 C, outside 9 C, climate off and a connected charging cable. It enables `test.enabled` last, then checks UDP info. All inputs are volatile test overrides.

In Android, choose **Local gateway**, IP/port 42840, grant local-network permission, opt into experimental local controls and Refresh. The app shows **TEST** and “Varstore test data · No CAN or vehicle commands”. The gateway advertises `control_backend: "varstore-test"`, `verified: false` and confirmation support. The app's test exception applies only to this exact backend, only for advertised operations and still requires lab-control opt-in. Deferred mode tests a time value; it does not install a persistent schedule.

1. Start climate from the phone. In the web Varstore tab, filter `request.climate`: enabled becomes 1 and generation increments once.
2. The observation `climate.state` stays 0. The phone waits for readback.
3. Force `climate.state` to 1 in the web tool, or:

   ```powershell
   python tools/udp_bench.py --host 192.168.1.195 set climate.state 1
   ```

4. The phone gets a succeeded result with wording identifying varstore confirmation. Stop requests 0; confirm it with an observed 0. No matching readback within 30 seconds produces unknown/unconfirmed.
5. Charge mode works through `request.charge.mode` (0 immediate, 1 deferred) and `request.charge.start_minute`. Confirm with `charge.mode`; deferred additionally needs the matching `charge.start_minute` (minutes since midnight). Mode confirmation does not claim current is flowing. Set charge state/power independently.
6. To test stale/invalid input, release a force and Write/Touch/Invalidate a manual observation. A normal old value retains its true age in UDP. A forced value is continuously valid and serializes with effective age zero; the actual force age stays visible in web diagnostics.

Release the seeded overrides with:

```powershell
python tools/udp_bench.py --host 192.168.1.195 release
```

Reboot clears all values/forces. Disabling test mode fails pending commands as `test_mode_disabled`. No test command can become a physical vehicle action.

## Varstore mapping

All values are int32; metadata is available through the web. Vehicle observations and `test.enabled` are manual inputs until CAN has a single producer. The UDP task owns `request.*` and `udp.*`; direct web writes are disabled for those signals.

| Varstore signal | UDP meaning |
| --- | --- |
| `vehicle.awake`, `vehicle.parked` | `state.vehicle.*`, 0/1 |
| `battery.soc_pct_x10` | SOC percent / 10 |
| `vehicle.range_km_x10` | Range km / 10 |
| `temperature.cabin_c_x10`, `temperature.outside_c_x10` | Temperature C / 10 |
| `battery.min_c_x10`, `battery.max_c_x10` | Battery temperature C / 10 |
| `vehicle.odometer_km` | Odometer km |
| `battery.voltage_v_x10`, `battery.current_a_x10` | V/A / 10; positive current is discharge |
| `climate.state` | 0 off, 1 on |
| `charge.state` | 0 disconnected, 1 waiting, 2 charging, 3 complete, 4 fault |
| `charge.mode`, `charge.connected` | 0 immediate / 1 deferred; cable 0/1 |
| `charge.power_w`, `charge.remaining_min` | Net HV battery charging power / 1000 kW; remaining minutes |
| `charge.start_minute` | 0–1439 converted to HH:mm, test timezone Europe/Prague |
| `test.enabled` | Exactly 1 enables test control; other/invalid values disable it |
| `request.climate.enabled`, `.generation` | Last accepted target and generation |
| `request.charge.mode`, `.start_minute`, `.generation` | Last accepted target; time invalid for immediate mode |
| `udp.rx_count`, `.tx_count`, `.drop_count`, `.command_count` | Saturating diagnostics; accepted-command count excludes retries |

Unknown, invalid and out-of-range values become null/unknown. Valid old values keep their age. Group age is the oldest contributing known observation; a missing required group input gives unknown age. Request targets are written before generation. Protocol generations are authoritative private counters with diagnostic copies; forcing a displayed generation does not authorize an old command.

## Protocol behaviour

Operations: `info.get`, `state.get`, `command.get`, `climate.set`, `charge.mode.set`. See the Android repository's `docs/protocol/udp-v0.md` for envelopes and examples.

- One JSON datagram, at most 1024 application bytes. IDs/client IDs are lowercase 32-hex. Current request vocabulary is ASCII. Duplicate keys, NULs, malformed numbers, excessive nesting, non-finite values, wrong types and unknown write arguments are rejected.
- Adapter processing is limited to 20 requests per one-second window. Oversized/malformed/limited packets and failed sends increment drop diagnostics. No unsolicited updates are sent.
- New writes require exact gateway/boot, a deadline in the next 1–5000 ms, the current domain generation, fresh awake/parked observations, and for charging a connected cable. Only the explicit test backend accepts writes for now.
- Acceptance is recorded before request publication. Deduplication by `(client,id)` returns the existing receipt without republishing. Changed command content gives `id_conflict`. Command queries are client-scoped; missing results give `result_unknown`, never replay.
- One active command per domain. Stop can supersede a pending climate start; other in-flight conflicts are busy. Generation increments on acceptance.
- Fixed cache: 256 accepted/in-flight/completed commands. Completed results stay ten minutes after completion. Never evict an unexpired entry to admit a new command. Restart clears cache and changes boot ID.
- Success requires matching fresh readback at/after acceptance; forced values count as current test input. Timeout after 30 seconds gives unknown. Observations are never updated optimistically.
- To fit large ages/numbers, optional current, voltage and battery temperatures can be omitted in that order. Core vehicle/climate/charge and basic readings take priority. Info describes readable fields, not guaranteed presence in every packet.

This uses the existing **lab-plain** local binding without authentication/encryption. IDs/deadlines/generations handle correlation/stale requests. Pairing/authenticated transport remains separate work.

## Verification

`tests/run-udp.ps1` compiles the actual C codec, varstore, signal definitions and SDK cJSON into a Windows DLL, then runs standard-library Python tests. Test-only headers declare the existing Windows CRT ABI and a controllable clock; codec/parser/storage are not mocked. Tests include real loopback UDP with a lost acknowledgement. The lwIP task adapter and physical phone-to-board Wi-Fi path still need device checks.

C-generated response fixtures are decoded by Android JVM tests. Keep Android `app/src/test/resources/gateway/*.json` synchronized with generated `build-host-tests/udp-fixtures/` when changing the wire schema.
