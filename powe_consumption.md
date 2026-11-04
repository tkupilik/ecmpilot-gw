# Waveshare ESP32-S3-RS485-CAN: component power budget

Reviewed: 2026-10-28. Board: RS485 + one CAN. Parked-current target: <=10 mA at 12 V.

**Build the budget from component data first. Use the measured board current only as a checksum, never as a value to fit.** The actual PCB revision, fitted parts and isolation-bypass status still need recording.

## Component -> consumption

The 12 V column makes different supply rails comparable. Assumptions: 80% input-path incremental efficiency, 90% main 5-to-3.3 V conversion, 80% isolated conversion. Fixed converter losses are listed separately. These efficiencies are estimates, not measurements. D = datasheet typical; C = calculation; A = assumed reference state. Sources are linked below.

| Component | Supply / state | Consumption on its own rail | Equivalent at 12 V | Basis / notes |
| --- | --- | ---: | ---: | --- |
| **U3 B0505LS-1W isolated DC/DC** | Main 5 V; own no-load loss | **~100 mW**, equivalent to ~20 mA at 5 V | **~10.42 mA** | D/C: K-CUT note gives approximate no-load power ~10% of 1 W rating. Downstream loads are counted in their own rows. Always powered. |
| **ESP32-S3R8, U2** | Main 3.3 V; depends on CPU/radio mode | **See separate mode table** | **~10.5-24.5 mA for the listed 160 MHz modem-sleep examples** | D/C: generic S3 references, not the measured firmware average. R8 embedded PSRAM can increase consumption. |
| **XL1509 input buck** | 12 V input; enabled | **2 mA own current**; specified test maximum 10 mA | **~2.00 mA** | D: IC quiescent test, not a measured complete-converter no-load current. Conversion loss is accounted for by efficiency. |
| **U8 TJA1051T/3 CAN transceiver** | Isolated 5 V VCC + isolated 3.3 V VIO; Normal/recessive | **5 mA VCC + 0.08 mA VIO** | **~3.31 mA** | D/C: S grounded through R18. Controller listen-only does not select physical Silent mode. |
| **U9 pi163E31 digital isolator, both sides** | Main 3.3 V + isolated 3.3 V; static inputs | **~1.54 mA per side** | **~1.59 mA total** | D/C: ~0.59 mA primary + ~1.00 mA secondary input-equivalent. Count both supplies once. Activity can increase it. |
| **U7 SP3485EN RS485 transceiver** | Isolated 3.3 V; receiver enabled, driver disabled | **0.30 mA**; unloaded driver-enabled alternative 0.425 mA | **~0.20 mA**; alternative ~0.28 mA | D/C: reference uses receive state. A driven terminated bus can draw substantially more. Actual enable state unknown. |
| **U4 ME6217 LDO, own operating current** | Isolated 5 V -> 3.3 V; enabled | **0.10 mA** | **~0.065 mA** | D/C: only regulator own current. Downstream current and LDO voltage-drop loss are already included in the secondary-load conversions. |
| **U1 MP1605 buck, own quiescent current** | Main 5 V -> 3.3 V; enabled | **0.011 mA = 11 uA** | **~0.006 mA** | D/C: main-load conversion loss is accounted for through the assumed 90% efficiency. |
| **LED1 red power LED + R2 5.1 kOhm** | Main 3.3 V; continuously lit | **~0.24-0.29 mA**; reference 0.25 mA | **~0.09-0.11 mA** | C/A: assuming Vf 2.1-1.8 V. Reference contributes ~0.095 mA at 12 V. |
| **LED2 / LED3 CAN and RS485 indicators** | Main 3.3 V; 470 Ohm branches, logic dependent | **0 off; ~1.1-2.8 mA per lit die** | **0; ~0.41-1.06 mA per lit die** | C/A: Vf 2.8-2.0 V, output sinking low. Up to four colour dies; actual Vf and duty unknown. Not in subtotal. |
| **U6 W25Q128JV flash** | Main 3.3 V; deselected standby reference | **0.010 mA standby**; 0.001 mA power-down | **~0.004 mA standby** | D/C: active reads 8/10/12 mA typical at 50/80/104 MHz -> ~3.06/3.82/4.58 mA at 12 V while reading. Program/erase 20 mA -> ~7.64 mA. Duty unknown. |
| **Embedded PSRAM in ESP32-S3R8** | Inside U2 package, no separate PCB IC | **Not quantified** | **Not quantified** | Generic S3 reference data do not establish the full R8 memory contribution. PSRAM disabled in this build is not proof of zero memory draw. |
| **U10 PCF85063 RTC** | Main 3.3 V via D7; timekeeping, unloaded outputs | **~0.00022 mA = 0.22 uA** at vendor 3.3 V reference | **~0.00008 mA** | D/C: actual VDD is after D7. Loaded outputs, pull-ups and any RTC-battery charging are separate. |
| **XL1509 R8/R9 + MP1605 R4/R5 feedback dividers** | ~5.45 V before D3 and main 3.3 V | **~0.103 mA + ~0.0135 mA**, at respective voltages | **~0.063 mA combined** | C: resistor dissipation, counted once. |
| **RS485 R22 / R23 4.7 kOhm pull-ups** | Isolated 3.3 V; node held low | **~0.70 mA per low node** | **~0.46 mA per low node** | C: conditional, not in subtotal. R22 pulls the common /RE-DE line high; driving it low draws current. |
| **RS485 R27/R24 bias + optional R25 120 Ohm** | Isolated 3.3 V; termination connected, driver disabled | **~0.347 mA** | **~0.23 mA** | C: 3.3/(4700+120+4700), excluding receiver loading. Not in subtotal. |
| **Other pull-ups, e.g. 10 kOhm** | Main 3.3 V; each held-low node | **0.33 mA per low node** | **~0.126 mA per low node** | C: GPIO/RTC/reset dependent; a static high node does not draw 3.3/R. Not in subtotal. |
| **CAN R19 120 Ohm termination** | CANH-CANL, jumper selected | **Not a fixed load from our 12 V supply** | **No fixed term** | Bus energy is generally supplied by the transmitter. Do not add 5 V / 120 Ohm during passive reception. |
| **Diodes / switching components** | Supply paths; load dependent | **Conversion loss** | **Included in assumed path efficiencies** | Do not add diode losses again. Actual rail measurements must refine the efficiencies. |
| **TVS, capacitors, remaining leakage** | Various rails | **Not quantified** | **Not quantified** | Not a measured zero; investigate if the independent sum disagrees with measurement. |
| **Reference non-ESP subtotal** | CAN recessive, RS485 receive, flash standby, only power LED on | Defined reference rows above | **~17.7 mA** | **Calculated typical scenario, not a guaranteed hardware floor.** Excludes ESP, extra R8 memory and conditional/unquantified rows. |

The largest non-ESP terms are isolation DC/DC (~10.4 mA), CAN (~3.3 mA), input buck (~2 mA), and digital isolation (~1.6 mA). The hardwired power LED contributes only ~0.1 mA at 12 V; signal LEDs can contribute more when continuously lit.

## ESP32 operating modes: choose one, do not add them

Espressif tables 5-7, 5-9 and 5-10 provide independent reference states. The modem-sleep figures apply to generic S3 variants; S3R8 includes PSRAM and can consume more. Each pair of modem values represents two typical peripheral-clock configurations, not min/max guarantees. RF-active values replace the MCU modem reference; they are not added to it.

| MCU state | Chip current at 3.3 V | Equivalent at 12 V | Meaning |
| --- | ---: | ---: | --- |
| **160 MHz WAITI, RF clock-gated** | **27.6 / 42.3 mA** | **10.54 / 16.16 mA** | Peripheral clocks disabled/enabled. Not the measured Wi-Fi-connected average. |
| **160 MHz dual core, 32-bit data access, RF clock-gated** | **49.6 / 64.1 mA** | **18.94 / 24.48 mA** | Independent active-CPU references. |
| **Wi-Fi RX HT20** | **88 mA** | **~33.61 mA** | Vendor active-RF reference, not a time-averaged gateway reading. |
| **Wi-Fi TX 802.11b, 1 Mbps, 21 dBm** | **340 mA peak** | **~129.86 mA equivalent** | Peak condition. Actual input transient also depends on capacitors/converter response. |
| **Light sleep, vendor reference configuration** | **0.240 mA** | **~0.092 mA** | Chip only; manual light-sleep policy is now available, but the running TWAI driver prevents entry. |
| **Deep sleep, RTC memory/peripheral reference states** | **0.007-0.008 mA** | **~0.0027-0.0031 mA** | Chip only; no normal Wi-Fi/UDP service and no implied removal of board overhead. |

## Sum first, measurement checksum second

Add the ~17.7 mA reference non-ESP subtotal to one independently selected MCU reference. Keep flash activity, LED duty, pull-up states, PSRAM and efficiency uncertainty separate. No component is obtained by subtracting the model from measurement.

| Independent reference state | Estimated board input at 12 V, before unresolved additions |
| --- | ---: |
| 160 MHz WAITI references + non-ESP subtotal | **~28.3 / 33.9 mA** |
| 160 MHz dual-core data-access references + subtotal | **~36.7 / 42.2 mA** |
| RX HT20 reference + subtotal | **~51.3 mA** |
| TX peak reference + subtotal | **~147.6 mA equivalent peak**, not average |

**Owner's checksum, reported 2026-10-28:** approximately **60 mA at 12 V "without firmware"**; readings varying **30-60 mA with our firmware**. The independently calculated operating references are of the same order and make these readings plausible. They do not identify individual component contributions, the duty cycle, a 45 mA average, or the actual RF peak. "Without firmware" does not establish reset-held/MCU-off operation or a 60 mA hardware floor.

If a repeatable measurement disagrees, check real converter losses, interface logic, memory/LED activity, USB backfeed and instrument averaging. Keep the unexplained difference visible rather than adjusting ESP current to force agreement. Missing measurement details: exact image, PCB/modification state, Wi-Fi/AP state, USB/CAN/RS485 connections, termination and averaging interval.

For perspective, sustained 10 / 30 / 60 mA would consume **7.2 / 21.6 / 43.2 Ah per 30 days** without battery replenishment. This arithmetic is not an endurance prediction.

## Conversion rules and reproducible subtotal

```text
Main 3.3 V load:        I12 = I3V3 * 3.3 / (12 * .80 * .90) = I3V3 * .38194
Isolated 5 V load:      I12 = Iiso5 * 5 / (12 * .80 * .80)   = Iiso5 * .65104
Isolated 3.3 V via LDO: I12 = Iiso3 * 5 / (12 * .80 * .80)   = Iiso3 * .65104
Main 5 V load:          I12 = I5 * 5 / (12 * .80)           = I5 * .52083
Isolation fixed loss:  I12_mA = P0_mW / (12 * .80)
```

Use matching current units on both sides. Input-buck own current is added directly. Secondary 3.3 V loads pass through an LDO, so retain their current at 5 V. This assigns the LDO voltage-drop loss to those loads; add only LDO own current separately. Efficiencies must exclude the fixed losses separately added here. Using measured total efficiencies would require different accounting.

Subtotal includes: XL1509 own current; U3 no-load loss; U1 and U4 own currents; isolator 1.54 mA each side; CAN 5 mA VCC + 0.08 mA VIO; RS485 0.30 mA receive; power LED 0.25 mA; flash 0.010 mA standby; RTC 0.00022 mA; both feedback dividers. Unrounded sum is ~17.74 mA. Numerical precision is for reproducibility, not a claim of accuracy.

## Power paths and savings

```text
12 V -> D4 -> XL1509 -> D3 -> main 5 V
main 5 V -> U1 MP1605 buck -> main 3.3 V: MCU, flash, RTC, LEDs, isolator side 1
main 5 V -> U3 B0505LS-1W -> isolated 5 V: CAN VCC
isolated 5 V -> U4 ME6217 LDO -> isolated 3.3 V: isolator side 2, CAN VIO, RS485
USB 5 V -> D1 -> main 5 V
```

The MCU path has two buck stages. CAN/RS485 share isolation. These regulators remain powered; the XL1509 divider predicts ~5.45 V before D3, not necessarily exactly 5 V after D3.

| Proposed change | Reference saving at 12 V | Constraint |
| --- | ---: | --- |
| **Remove/bypass U3** | **~11.3 mA**: ~10.4 fixed + ~0.9 downstream conversion loss | Proposed, not measured. Remove U3 before feeding Relay-5V from main 5V and joining grounds; do not parallel its operating output. U9/U4/interface loads remain. |
| CAN physical Silent mode | **~2.6 mA** | Requires changing R18/S hardware. Receiver stays active. Fitted /3 variant lacks the /E variant's low-current Off control. |
| Remove power LED | **~0.1 mA** | Small contributor; do not confuse it with signal LEDs. |
| Reduce ESP 3.3 V average by 10 mA | **~3.82 mA** | Requires a verified useful operating mode. |
| RS485 shutdown | **~0.2 mA chip RX term**, plus possible driver/bias/LED savings | Tied /RE-DE cannot select shutdown /RE=1, DE=0 without modification. |

The model's non-ESP subtotal after U3 bypass is ~6.4 mA, before unresolved loads. This is not proof of a <=10 mA functioning gateway: even the generic 160 MHz WAITI MCU references add ~10.5-16.2 mA input-equivalent.

## Firmware audit and next measurement

Firmware audit updated2026-11-01, version0.5.0. `main/network.cpp` requests
WIFI_PS_MIN_MODEM; fallback AP stays on until reboot. PM/DFS, tickless idle and
successful-sleep diagnostics are now enabled. Home provides manual Normal
(160MHz fixed), DFS(40..160MHz, no light sleep) and Light-sleep policy
(40..80MHz, automatic sleep permitted). Startup always returns to Normal.
The current TaskCan services RX alerts with a10ms timer fallback. TWAI retains
an APB PM lock for its entire installed lifetime, preventing actual light sleep;
CAN is deliberately left running. No vehicle-triggered sleep policy exists.
A manual CAN-only deep-sleep test now stops Wi-Fi and uninstalls TWAI, then arms
GPIO16 LOW as the only wake source. No timer or automatic inactivity policy is enabled. See
[power-management details](docs/power-management.md). These changes have not yet
been measured on the board; historical30–60mA readings do not validate them.

[ESP-IDF power-management documentation](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/api-reference/system/power_management.html) distinguishes modem sleep, DFS, automatic light sleep, tickless idle and driver locks. Preserve continuous CAN reception/wake-frame requirements and CPU ownership/varstore architecture when optimizing. Deep sleep removes normal Wi-Fi/UDP reception.

For checksum testing, use the same 12.0 V supply with USB disconnected. Record image, modifications, rail voltages, AP/STA state and interface wiring/termination. Compare reset-held, explicit GPIO idle with Wi-Fi off, STA-only without web polling, AP fallback and deliberate sleep. Reset-held may change interface/LED states. Measure mean current/energy over at least 10 minutes and peaks separately. Compare U3 bypass before/after using the same firmware and reference state.

## Sources

- **Board:** [Waveshare schematic](https://files.waveshare.com/wiki/ESP32-S3-RS485-CAN/ESP32-S3-RS485-CAN-Schematic.pdf), visually reviewed, retrieved 2026-10-28. SHA-256 `c6620c0f318166c043733febd0c30835972c9cd09dcda4395fb39526fd5ea915`.
- **Input buck:** [XL1509 rev.2.6](https://www.xlsemi.com/datasheet/XL1509-EN.pdf), p.6: 2 mA typical / 10 mA maximum own current in the specified test.
- **Isolation DC/DC:** [K-CUT B_LS-1W manufacturer datasheet](https://taoic.oss-cn-hangzhou.aliyuncs.com/temp/product/18680668620_1575625657000.pdf), p.2: approximate no-load power 10% of rating, not a guaranteed lower bound.
- **CAN:** [NXP TJA1051](https://www.nxp.com/docs/en/data-sheet/TJA1051.pdf), pp.2,5,8: Normal/Silent currents, VIO and /3 modes.
- **RS485:** [MaxLinear SP3485](https://www.maxlinear.com/ds/sp3485.pdf), p.3: receive/driver currents and shutdown condition.
- **Digital isolator:** [2Pai pi163E manufacturer datasheet, distributor copy](https://cdn.promelec.ru/upload/items/2023/06/28/2Pai-Semi--160E-161E-162E-163E_.pdf), p.7: static 3.3 V current ~1.53-1.554 mA per side.
- **LDO:** [Microne manufacturer catalogue](https://www.microne.com.cn/en/ldo-linear-regulator): ME6217 operating own current 100 uA typical.
- **Main buck:** [MPS MP1605 datasheet](https://www.monolithicpower.com/en/documentview/productdocument/index/version/2/document_type/Datasheet/lang/en/sku/MP1605/document_id/1049/), p.4: 11 uA typical quiescent current.
- **Flash:** [Winbond W25Q128JV rev.J manufacturer datasheet, distributor copy](https://resources.ampheo.com/static/datasheets/winbond-electronics-corporation/w25q128jvsin.pdf), section 9.4: standby, power-down, read and program/erase currents. Verify fitted part/revision.
- **RTC:** [NXP PCF85063A rev.7.3](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf): 0.22 uA typical timekeeping at 3.3 V.
- **ESP32:** [Espressif S3 datasheet v2.2](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf), tables 5-7, 5-9, 5-10: CPU, RF, sleep reference states and embedded-PSRAM caveat.

Whole-board observations are owner-provided checksum data. No individual component current was measured in this review.
