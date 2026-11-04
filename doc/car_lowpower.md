# Vehicle low-power behavior and CAN availability

Updated: 2026-11-01. Vehicle: the owner's Opel Corsa-e/eCMP.

These are findings from the owner's vehicle tests and the captures/research in
this repository. They describe the tested vehicle; timing, retry limits and
re-arm conditions have not been characterized. All CAN IDs/payloads below are
hexadecimal. Vehicle/BSI sleep and the ESP32 gateway's own power policy are
separate; the GW measurement below records its current hardware baseline.

## Main findings

| Observation | Result on the tested vehicle | Consequence |
| --- | --- | --- |
| Wake through HSCAN1, OBD6/14,500kbit/s | The tested transmissions do not wake BSI | An HSCAN1-only gateway cannot currently wake the vehicle |
| Wake through INFO CAN,125kbit/s | Two wake-up messages can wake BSI:0x401 and0x43C | INFO provides a working wake path, subject to the refusal behavior below |
| AC cable insertion / OBC event | BSI announces cause0x20 on INFO412 and enters Normal | Cable insertion is another observed vehicle wake source; the direct OBC-to-BSI request is not captured |
| Vehicle connected and AC charging | BSI can enter sleep while charging continues | An active charging session does not guarantee available CAN telemetry |
| BSI asleep during AC charging | Both observed buses, HSCAN1 and INFO, become silent/unavailable | Neither bus provides continuous live charging-state monitoring in that state |
| Repeated wake requests | After repeated wake-ups, BSI starts ignoring further wake requests | Wake-on-demand is not indefinitely repeatable; repeated polling is not a reliable keep-awake strategy |

HSCAN1 wake failure is the current operational conclusion from the tests, not a
claim that every possible diagnostic/electrical wake mechanism has been ruled
out. The earlier PCAN results are recorded in
[the HSCAN wake experiments](../docs/can-wakeup-live-2026-10-28.md).

## INFO wake messages

| ID | Database name | Current evidence |
| --- | --- | --- |
|0x401 | SDCM_WakeupBml | Previously owner-confirmed wake path; the Internal experiment uses standard CAN,DLC2,payload00 02 |
|0x43C | MDS_Wakeup | Second owner-reported wake path; current LCD/GW source selects standard CAN,DLC2,payload00 80; the preceding LCD test used00 02 |
|0x412 | BSI_WakeupCause | BSI announcement00 20 00 00 00 00 00 00 during owner-labelled OBC/AC plug-in wake; not a separately tested injectable wake request |

Names come from [the INFO DBC](../dbc/opel_corsa_e_info.dbc). Database sender names
alone do not prove which physical ECU originated a captured frame. The latest
LCD log confirms0x43C submissions and subsequent BSI traffic. The latest GW
build now targets INFO/125 and defaults to0x43C with00 80, matching the LCD source.
Its ID/payload can be forced for tests; see [wake configuration](../docs/info-can-wake.md).
The00 80 payload is the current source default, not a new vehicle validation.

0x5DF is a separate synthetic version/identity experiment. It is **not** one of
these two wake requests: it previously woke BSI only enough to request standby,
without achieving the intended vehicle wake. The latest GW source **removes all
version5DF TX**, to avoid colliding with the original cluster. Earlier LCD/GW
images included a500ms synthetic version sender while fresh PowerManagement was1.

BSI PowerManagement is decoded from0x036,data[4]&7:

| Value | Current interpretation |
| --- | --- |
|0 | Standby |
|1 | Normal |
|2 | Entering standby |
|3 | Wake-up |
|4 | Communication off |

A submitted wake frame or initial traffic is not proof that BSI reached and
stayed in Normal. For example, the latest LCD test progressed from stale status
to phase2 after a wake submission. ESP_OK from twai_transmit only means local
driver acceptance. Confirm an actual wake using newly received vehicle frames
and PowerManagement state/freshness.

## AC cable insertion / OBC wake sequence

`logs/info_buzeni_obc.csv` records wake after the owner inserted the AC cable,
with the original instrument cluster/unit004 connected. Time zero below is
the first captured frame; the physical plug-in instant is not timestamped.

| Time from first frame | Observed INFO traffic | Meaning |
| ---: | --- | --- |
| 0.000–39.995 ms | Five 412 frames:00 20 00 00 00 00 00 00 | BSI announces wake cause; approximately 10 ms repetition |
| 20.074 ms | 036, data[4]&7 = 3 | BSI reports Wake-up |
| 45.196 ms | 5D2:00 00 00 00 00 01 00 00 | Upstream version/identification layout; not a captured OBC transaction ACK |
| 54.750 ms | 036, data[4]&7 = 1 | BSI reports Normal; 34.676 ms after the Wake-up report |
| 242.450 ms | 5DF:1F 1C 01 15 4C 5E 3F 34 | Original cluster identification; GW does not transmit this ID |

DBC labels the observed full value `BsiWakeupInfo0=0x20` as **OBC cause (AC cable
insertion)**. Its eight-bit layout is preserved; this is an event-correlated
value, not an isolated verified cause bit. The earlier `infocan.csv` also has
412 announcements with 0x30, whose meaning remains unassigned.

The capture begins with BSI's announcement and contains no preceding direct
OBC request or 401/420/424/43C wake announcement. Thus there is no complete
OBC-request/BSI-reply ping-pong to reconstruct on INFO. The 412 burst followed by
036 Normal is BSI's observable acknowledgement of being awake, **not** a matched
application transaction ACK. The physical OBC-to-BSI wake path remains unknown.
Normal persists through the last036 at approximately 34.120 s; this file does not
show the later charging/sleep behavior.

Compared with both `info_standby.csv` and the fully operational
`info_start_jizda.csv`, this capture has the same 17 added IDs. Seven already
occurred in older wake/start captures (`infocan.csv`/`info_apka.csv`), leaving 10
newly observed IDs against the union of the older captures. Only the nine
owner-approved cluster IDs are assigned to unit004/IC in DBC:
**1D7,217,257,2D7,2E7,317,31C,51F,5DF**. The remaining new 4A4 is not assigned.

See the [full timeline and all 17 ID comparisons](../docs/info-obc-wake-2026-11-01.md)
and its adjacent JSON for exact CSV lines, counts, timing and input hashes.

## AC charging and monitoring limits

The owner observed BSI sleeping during AC charging, with both monitored buses
off. This does **not** mean that charging stopped, that the traction battery is
offline internally, or that every other vehicle network is inactive. It means
the gateway loses the observed telemetry paths.

Therefore an always-powered, Wi-Fi-connected GW cannot promise continuous live
SOC, voltage or charging progress solely from HSCAN1/INFO broadcasts. HSCAN1
provides the implemented HV battery SOC/voltage frame0x5C3 while that bus is
active; those readings must age when the source disappears.

- Preserve the last measured value and its original timestamp as history.
- Expired data must become stale/unknown in phone/HA rather than being refreshed
  by UDP/MQTT polling. Distinguish GW reachability from vehicle-data availability.
- Silence is not proof of charge completion, zero charging power, disconnection
  or an empty/full battery. Do not synthesize those states from a timeout.
- Waking through INFO may restore traffic temporarily, but it does not currently
  provide a dependable continuous monitoring solution.

## Repeated-wake refusal

BSI starts ignoring wake requests after repeated wake-ups. The number of accepted
wakes, relevant interval, cooldown, recovery event and differences between the
two IDs are not yet measured. Do not document guessed thresholds or a permanent
lockout as established behavior.

The current INFO policy can request one wake frame per eligible valid
UDP request while PowerManagement is not1 or is stale. That implementation must
not be mistaken for a vehicle-approved retry policy: repeated phone polling can
produce repeated wake attempts, and more attempts need not restore availability.
A future controller should use bounded attempts/backoff and expose refusal or
unavailability, after the vehicle's recovery behavior is measured. No retry or
CAN power-management behavior is changed by these notes.

## Gateway hardware and measured consumption

Owner measurements, 2026-11-01: Waveshare ESP32-S3-RS485-CAN, powered from
**12 V**, with the isolated DC/DC converter removed. Current is measured at the
whole-board input; power and daily consumption below are calculated from it.
Values are approximate.

| Selected firmware mode | Measured input current at 12 V | Calculated input power | Calculated consumption per 24 hours |
| --- | ---: | ---: | ---: |
| Normal, fixed CPU frequency 160 MHz | 22 mA | 264 mW | 0.528 Ah |
| Variable CPU frequency (DFS) | 20 mA | 240 mW | 0.480 Ah |
| Manual deep sleep, CAN RX wake armed | 6.3 mA | 75.6 mW | 0.151 Ah |

Deep-sleep input current has now been measured. **Wake-up through CAN has not yet
been tested; the owner will verify it in the vehicle.** The current reduction
does not establish wake reliability or complete the sleep/wake validation.
The DFS result is not an automatic-light-sleep measurement.

The earlier approximately 20 mA observation is consistent with the variable-
frequency row. The earlier unmodified-board observations were approximately
60 mA without application firmware and 30–60 mA with the GW firmware. See the
[component power budget](../powe_consumption.md) for those historical measurements
and the independent component estimates. These operating-point measurements do
not replace that component analysis or isolate individual component losses.

## Supply stability during BSI wake

Owner vehicle test, 2026-11-01: **GW survives BSI wake without restarting**.
In the earlier LCD test, BSI wake was followed by a reset recorded as
**BROWNOUT (9)**. The LCD was powered from OBD pin16 through a separate generic
7–24 V to5 V switching module.

| Device / test | Observed result |
| --- | --- |
| LCD, earlier BSI wake test | Restart; retained log reports BROWNOUT |
| Standalone GW, current BSI wake test | Continues running without a restart |

Together these findings strongly point to the LCD power path as the cause of
its reset. They do not isolate the exact failing element: the external converter,
wiring/connectors or local5 V/3.3 V regulation/decoupling may contribute. GW
survival does not establish that the shared12 V feed has no transient at all.

This test establishes GW stability while BSI wakes; it is **not** confirmation
that CAN activity wakes GW from deep sleep. That separate test remains pending.

## Separate gateway power constraints

Vehicle bus silence does not itself release the ESP-IDF TWAI driver's PM lock.
Normal/DFS/light profiles keep CAN installed/running even when no frames arrive;
the automatic-light-sleep policy therefore remains blocked by that driver.
The separate manual **Deep sleep · CAN wake** test stops Wi-Fi and uninstalls TWAI,
then wakes from GPIO16/CAN RX activity. It has no48-hour or other automatic timeout.
See [power controls and test procedure](../docs/power-management.md). Vehicle sleep
and ESP sleep remain separate: a sleeping vehicle does not itself request GW sleep.

INFO uses a different physical CAN interface from HSCAN1. Switching bitrate
alone is not a validated hardware adaptation. Any future dual-bus gateway must
use the appropriate interfaces for both networks.

## Follow-up measurements

Record simultaneous HSCAN1/INFO traffic across AC-charge startup, BSI sleep and
charging completion. For isolated wake trials, record the selected ID/payload,
time since the previous wake, observed PowerManagement transitions and which
bus/data actually returns. Once refusal occurs, measure what restores acceptance
without assuming that a longer burst will help.

Related research: [PSA telematics](../PSA_telematic.md),
[LCD/GW CAN behavior](../docs/lcd-backport.md),
[HSCAN1 battery decoding](../docs/can-telemetry.md).
