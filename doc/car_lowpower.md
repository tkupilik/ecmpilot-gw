# Vehicle low power and gateway consumption

Current state: **2026-11-04**, owner's Opel Corsa-e. Vehicle sleep and ESP32 sleep
are separate. This page retains current observations, not a chronology of trials.
CAN frames and transaction limits are defined in [CAN requests](../docs/can-requests.md).

## Vehicle observations

| Situation | Observed result | Consequence |
| --- | --- | --- |
| Wake through OBD HSCAN1, 500 kbit/s | Tested transmissions do not wake BSI | No working HSCAN1 wake path is established |
| Wake through INFO, 125 kbit/s | `401` and `43C` provide working wake paths | Current GW uses `43C [00 80]` |
| AC cable insertion | BSI `412` announces cause value `0x20`, then `036` reaches Normal | OBC/plug-in is another wake source; direct OBC request is not captured |
| AC charging | BSI can sleep while charging continues; both monitored buses can become silent | Bus silence is not evidence of charge completion |
| Repeated waking | BSI can ignore subsequent requests | Retry count, cooldown and re-arm conditions are not characterized |
| Explicit/automatic GW refresh | Fresh data promptly reaches GW and phone | Cloud upload is not reliably triggered |
| Stored charging-time change | Owner confirms the time is saved and cloud data updates, even without active charging | Verified with the wake/394/35B sequence; unchanged-time writes are untested |
| Preconditioning active | INFO stays awake; owner sees normal cloud updates | Exact upload trigger/status remains unidentified |

These findings concern the tested vehicle and paths. They do not rule out every
possible HSCAN1 diagnostic/electrical wake mechanism or imply all vehicle networks
are asleep when the two monitored buses are silent.

## Wake and standby behavior

BSI `036`, byte 4 bits 2..0, reports:

| Value | State |
| --- | --- |
| 0 | Standby |
| 1 | Normal |
| 2 | Entering standby |
| 3 | Wake-up |
| 4 | Communication off |

Current control waits for fresh, settled Normal before sending a request.
Wake is bounded to ten intents per operation, 100 ms apart. Opening/reconnecting
the phone, polling and HA publishing are passive. Optional automatic refresh
creates bounded refresh operations; it is off by default. No CAN keepalive,
periodic version `5DF` or vehicle sleep command is generated.

The refusal behavior means repeated waking cannot be assumed to keep the vehicle
available indefinitely. No measured refusal threshold or cooldown is available.
The current retry bounds are GW policy, not a characterized BSI acceptance limit.

## AC plug-in / OBC wake

In `logs/info_buzeni_obc.csv`, relative to the first captured frame:

| Time | Observation |
| --- | --- |
| 0–39.995 ms | Five `412 [00 20 00 00 00 00 00 00]` announcements, about 10 ms apart |
| 20.074 ms | `036` reports Wake-up (3) |
| 54.750 ms | `036` reports Normal (1) |
| 242.450 ms | Original cluster `5DF` identification appears |

DBC labels the **full observed cause value `0x20`** as OBC cause / AC cable
insertion; this does not establish an isolated cause-bit layout. No preceding
OBC request or `401`/`43C` appears, so there is no captured complete request/reply
ping-pong. The `412` burst and Normal report acknowledge BSI's awake state,
not a matched application transaction. See [the OBC evidence](../docs/info-obc-wake-2026-11-01.md).

Nine approved added IDs are attributed to cluster/unit 004: `1D7`, `217`, `257`,
`2D7`, `2E7`, `317`, `31C`, `51F`, `5DF`. An ID-set difference alone does not
justify assigning all new traffic to the cluster.

## Telemetry while charging

Preserve the last value with its original CAN receipt time. Wi-Fi reachability,
UDP replies and MQTT publication do not make vehicle measurements fresh. Silence
must not become zero current, disconnected cable, empty battery or charge complete.
Source ECUs can repeat cached values; CAN receipt age is not source measurement age.

INFO supplies SOC/range while active; HV voltage, battery temperatures and remaining
energy have no validated INFO decoder. Optional HSCAN1 supplies `5C3` SOC/voltage
while active, but also loses the observed path during the reported charging sleep.
Refresh restores GW observations; cloud-update behavior is tracked separately in
[CAN requests](../docs/can-requests.md).

## Gateway input-current measurements

Waveshare ESP32-S3-RS485-CAN, **12 V whole-board input**, with the isolated DC/DC
converter removed. This hardware modification is optional and these numbers do
not describe the stock board. Measurements are approximate; power/daily charge
are calculated from measured input current.

| Mode | Current at 12 V | Input power | Charge per 24 h |
| --- | ---: | ---: | ---: |
| Fixed CPU 160 MHz | 22 mA | 264 mW | 0.528 Ah |
| Dynamic CPU frequency | 20 mA | 240 mW | 0.480 Ah |
| Manual deep sleep, CAN RX wake armed | 6.3 mA | 75.6 mW | 0.151 Ah |

The running TWAI driver holds a PM lock. Normal/DFS/light policies keep CAN
installed, so actual automatic light sleep remains blocked. DFS measurement
is not a light-sleep measurement.

Manual **Deep sleep · CAN wake** suspends network services and uninstalls TWAI,
then arms GPIO16/RX LOW wake. It loses Wi-Fi/phone/HA access and reboots on wake.
**The owner confirmed deep sleep / CAN wake on the installed vehicle on
2026-11-04.** Automatic entry now uses the age of the last valid BSI report,
with a configurable 48-hour default and web/phone countdown. See
[automatic sleep](../docs/low-power.md). See [power modes](../docs/power-management.md) and
[component budget](../powe_consumption.md).

## Power stability

Standalone GW survives BSI wake. The LCD installation restarted with retained
reset reason **BROWNOUT (9)** when supplied from OBD pin 16 through a generic
7–24 V to 5 V module. This points to the LCD supply path, without isolating the
converter, wiring or local regulation as the exact failing element. It does not
prove the common 12 V supply has no transient.

INFO requires the appropriate physical CAN interface; a bitrate change alone
does not establish compatibility of a high-speed transceiver. Wiring references
are in [vehicle wiring](../docs/hardware/vehicle-wiring.md).
