# PSA telematics on INFO CAN

**2026-11-03 update:** [Immediate preconditioning Start/Stop capture and implementation](docs/preconditioning.md) now documents31B/11B requests, BSI2E0 readback and the30-second limit. The charge-schedule investigation below is historical.

Research notes for the owner's Opel Corsa-e, recorded on 2026-10-28. All CAN identifiers and payload bytes below are hexadecimal. Times are seconds relative to the first frame of each capture.

Later owner tests, updated2026-11-01, are summarized in
[vehicle low-power behavior](doc/car_lowpower.md):0x401 and0x43C provide INFO wake
paths, repeated waking can lead to refusal, and BSI/both monitored buses can
sleep during AC charging. The capture-only conclusions below remain historical.

## Confirmed charge-schedule time update

The MyOpel capture contains a single `0x35B` request with payload `80 B5`, followed **82.303 ms later** by the first `0x160` status carrying the same bytes. Before the request, the status was `80 B6`.

The low 11 bits decode as minutes since midnight: **03:02 -> 03:01**. The owner deliberately withheld the selected times until after the analysis, then independently confirmed that this was exactly the change made in MyOpel. This validates the time field in this request/status pair on this vehicle.

It does not yet validate the other flags, switching immediate/deferred charging, physical charging, or acceptance of an independently replayed request.

## Evidence and source

| Capture | Role | Frames | Distinct IDs | Duration |
| --- | --- | ---: | ---: | ---: |
| [logs/info_apka.csv](logs/info_apka.csv) | MyOpel charge-schedule update | 7,618 | 94 | 31.304387 s |
| [logs/infocan.csv](logs/infocan.csv) | Baseline INFO activity for comparison | 14,137 | 97 | 61.519853 s |

SHA-256:

```text
info_apka.csv  2a90da0c09e3f31bafda5bb4b4ba9977ebd09f317a29fbac5dd762a2a15a4849
infocan.csv    51971e3a0c72125082d2c8524a5244d1bd944bfba51a553a15fb6a907441de13
```

Both files contain Rx records. Their absolute timestamps are unsuitable as wall-clock dates; relative microsecond differences are used here. There is no separate timestamp for the user's tap in MyOpel, so the timeline does not measure cloud latency.

Names, sender/receiver claims and candidate flag meanings come from [PSA-RE](https://github.com/prototux/PSA-RE), pinned revision `67f7581e7622410efdbb29c7eda3b86982b9b450`, profile `AEE2010.electric -> AEE2010.ev -> AEE2010.full`, bus `LS.INFO-DIV`. See the local [INFO DBC guide](dbc/README-info.md), [DBC](dbc/opel_corsa_e_info.dbc) and [resolved source JSON](dbc/opel_corsa_e_info.source.json).

`MDS` and `BSI` below are database sender claims. A passive capture alone does not identify the physical transmitter or prove that the MDS alias is the installed telematics unit.

## Observed MyOpel transaction

| Relative time | ID / DLC | Payload | Observation / interpretation |
| ---: | --- | --- | --- |
| 0.000000-0.090025 | `43C` / 2 | `00 80` | Ten wake announcements, approximately 10 ms apart; source flag `WakeupCause1 = 1`, precise cause unknown. |
| 0.348420-17.348434 | `160` / 2 | `80 B6` | 35 status frames with the previous stored start time, 182 min = 03:02. |
| 1.099981 | `3A9` / 2 | `04 00` | First asserted `NetworkMaintainRequestMdc`; preceding frames were `00 00`. |
| 2.200367 | `53C` / 8 | `08 00 00 00 00 00 00 00` | First source-defined network-management-enabled supervision state; first two frames were all zero. |
| 16.997988 | `394` / 1 | `10` | Single source-defined `RemoteSendChargeDataRequest = 1`; distinct from the schedule update. |
| **17.458085** | **`35B` / 2** | **`80 B5`** | **Single remote charge-schedule request, 181 min = 03:01.** |
| **17.540388** | **`160` / 2** | **`80 B5`** | **First updated BSI schedule status, 82.303 ms after the request.** |
| 17.540388-30.848202 | `160` / 2 | `80 B5` | 28 status frames retain the new value through the final observed schedule status. |

The hold request `3A9 04 00` continues through 31.298456 s. The capture ends at 31.304387 s, without recording its release or a subsequent return to sleep.

The baseline contains 114 `0x160` frames, all `80 B6`; no `0x35B`, `0x394` or `0x43C` occurs. Baseline `0x3A9` remains `00 00`. The transition in `0x53C` also occurs in the baseline, so that transition alone does not identify a remote application command.

## Charge-schedule payload layout

Both messages have DLC 2:

- [`0x35B`](https://github.com/prototux/PSA-RE/blob/67f7581e7622410efdbb29c7eda3b86982b9b450/buses/AEE2010.full/LS.INFO-DIV/35B.yml): `MDS_ChargeSchedRemote`, source sender MDS, receiver BSI.
- [`0x160`](https://github.com/prototux/PSA-RE/blob/67f7581e7622410efdbb29c7eda3b86982b9b450/buses/AEE2010.full/LS.INFO-DIV/160.yml): `BSI_ChargingSchedule`, source sender BSI.

Byte indices start at zero; bit 7 is the most significant bit.

| Location | Request `35B` | Status `160` | Confidence |
| --- | --- | --- | --- |
| Byte 0, bit 7 | Schedule-menu availability | Schedule-status availability | Source candidate; remains 1 in this experiment. |
| Byte 0, bit 6 | Charge-type selection | Charge-type menu status | Source labels 0 immediate / 1 delayed; remains 0, mode switching unverified. |
| Byte 0, bit 5 | Unspecified/raw | Unused | Remains 0; preserve, meaning unverified. |
| Byte 0, bits 4..3 | Target-level code | Target-level code | Remains 0; physical meaning unverified. |
| Byte 0, bits 2..0 + byte 1 | Start time, 11-bit unsigned minutes | Start time, 11-bit unsigned minutes | **Owner-confirmed for 182 -> 181.** |

Decode:

```c
unsigned minutes = ((data[0] & 0x07u) << 8) | data[1];
unsigned hour = minutes / 60u;
unsigned minute = minutes % 60u;
```

```text
80 B6 -> 0x0B6 -> 182 minutes -> 03:02
80 B5 -> 0x0B5 -> 181 minutes -> 03:01
```

The source range includes 1440. Normal clock times use 0..1439; the meaning of 1440 is unverified and it must not silently wrap to midnight. The observed values match the owner's selected UI times without an offset in this experiment; a general UTC/local-time or daylight-saving policy is not established.

For a future encoder, the time-field operation would preserve the other five bits:

```c
// Proposed field packing only; not a verified standalone CAN command.
data[0] = (previous_flags & 0xF8u) | ((minutes >> 8) & 0x07u);
data[1] = minutes & 0xFFu;
```

The upper flags remain `0x80` in both observed values. Editing the stored deferred-start time therefore does not, by itself, demonstrate enabling deferred charging. The target-level labels differ between the upstream request and status definitions; raw code zero must not be presented as a confirmed 10% charge target.

Neither layout defines a counter or checksum, and none is visible in these two-byte samples. This does not prove that sending `35B 80 B5` alone will work: network state, preceding traffic or repeated requests may matter.

## Wake-up and network hold

The owner reports that an ordinary CAN message on INFO wakes traffic including `0x036`. In the MyOpel capture, `0x036` changes from source-defined wake-up phase (`data[4] & 7 = 3`) at 0.045909 s to normal phase (`= 1`) at 0.090964 s. The baseline also contains wake-up and normal phases, followed by a standby phase near its end.

`0x43C` is a wake announcement, not a proven unique wake trigger. Its ten repetitions and the subsequent `0x3A9` hold request are useful candidates for further investigation, but the trace does not establish which electrical event first woke BSI. The eight upstream wake-cause flags have no precise physical cause descriptions.

The INFO bus uses a low-speed fault-tolerant CAN interface, distinct from the gateway's current HSCAN interface. A bitrate change alone is not a validated hardware adaptation. The firmware currently remains an HS1 listener at 500 kbit/s; this document adds no INFO driver or vehicle commands.

## Remaining validation and gateway architecture

Useful controlled captures, with one setting changed at a time:

1. Switch immediate/deferred charging while keeping the stored time fixed; check bit 6 in `35B` and `160`.
2. Change to another time, including a value above 255 minutes, to exercise the high three time bits independently.
3. Capture a status-only refresh and compare `394`, wake announcements and the hold-request lifetime.
4. Capture remote climate start/stop separately. `31B`/`11B` are source candidates for climate scheduling, not yet confirmed start/stop commands.
5. Only after defining a bounded transmit experiment, test request acceptance and return to sleep independently of MyOpel. No such replay has been performed for this analysis.

IDs `254`, `314` and `2DB` are absent from both captures; no application transaction on them can be inferred here.

Future integration must preserve the gateway's process boundary: CAN RX publishes decoded status and freshness to varstore; a separate control process consumes requests and status through varstore. CAN receive handlers must not directly execute heating or charging actions. The confirmed `160` time field is a candidate status input; a future `35B` transmitter still needs its own validation.
