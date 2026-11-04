# CAN wake-up: MyOpel capture and missing ACK

Later in the same day, [live PCAN tests](can-wakeup-live-2026-10-28.md) tried three diagnostic requests against the sleeping vehicle, with explicit hardware resets between cases. The observations below remain the earlier offline-capture analysis; the live report records the actual transmissions and their limits.

## Observations

The owner recorded `logs/opel_apka.csv` on OBD pins 6/14 while MyOpel caused the car to wake. `logs/sleep_2_odemceni.csv` is the comparison capture labelled sleep-to-unlock. The precise MyOpel action and timing relative to logger startup are not recorded. No vehicle frames were transmitted during this offline analysis.

| Property | MyOpel capture | Sleep-to-unlock comparison |
| --- | --- | --- |
| Frames | 46,338 | 59,433 |
| Relative duration, first to last recorded frame | 82.294539 s | 76.294335 s |
| Unique standard IDs | 68 | 68, exactly the same set |
| Directions | All Rx | All Rx |
| First frame | `432`, `42 19 40 FE 00 08 30 02` | `432`, `42 1B 40 FE 04 08 30 02` |
| `112` first/only transmission | 4.950 ms | 4.864 ms |
| `12A` first/only transmission | 138.878 ms | 138.668 ms |
| `102` first/only transmission | 270.716 ms | 270.423 ms |
| First `3CD`, brake ECU family | 154.750 ms | 161.537 ms |
| First `262`, powertrain state | 276.135 ms | 275.799 ms |
| First nonzero pack-voltage/SOC payload at `5C3` | 1.291914 s | 1.291813 s |
| Last `50D`, brake ECU family | 6.058432 s | 37.064595 s |
| Last `262` | 65.164824 s | 65.564617 s |
| Remaining traffic at the end | `782` once per second | `782` once per second |

All times are relative to the first recorded frame, not to the user's action or an observed electrical wake pulse. The CSV timestamps have the same implausible negative epoch representation as the READY capture; relative deltas are interpreted as microseconds. There is no recorded pre-trigger silent duration, physical voltage waveform, or ACK/error-frame information.

Input SHA-256:

- `opel_apka.csv`: `51fcb51d95760478d1dbe61ab1fd403f052827848e4dcba1fd4139ce0cc4c320`
- `sleep_2_odemceni.csv`: `9ca248dfdce86c9233db4a4f46523411f15b8a28fcba8dbfbf75f76a7024cb1f`

### What the early frames mean

Compared with the READY capture, three IDs appear additionally: `112`, `12A`, `102`. PSA-RE labels them startup software/calibration identification messages from BSI, ESM and VCU respectively. In both wake captures each occurs exactly once, with **identical payloads across the two captures**:

| ID | Source identification | Payload |
| --- | --- | --- |
| `112` | BSI version | `00 00 00 00 00 01 00 00` |
| `12A` | ESM version | `74 09 02 15 05 04 0E 01` |
| `102` | VCU/SCDT version | `99 1F 07 23 01 12 B2 01` |

These are good markers of ECU startup/wake sequencing; there is no evidence that replaying them will wake the vehicle. Sender attribution is a source claim, not physical identification. References: [112](https://github.com/prototux/PSA-RE/blob/67f7581e7622410efdbb29c7eda3b86982b9b450/buses/AEE2010.full/HS.HS1/112.yml), [12A](https://github.com/prototux/PSA-RE/blob/67f7581e7622410efdbb29c7eda3b86982b9b450/buses/AEE2010.full/HS.HS1/12A.yml), [102](https://github.com/prototux/PSA-RE/blob/67f7581e7622410efdbb29c7eda3b86982b9b450/buses/AEE2010.electric/HS.HS1/102.yml).

The first `432` has the source-defined main-wake field equal to **1: no main wake-up request**, throughout both captures. `262` is always `E0`, source-defined powertrain-at-rest rather than READY. These observations are consistent with partial wake-up for information exchange, not the full driving state.

For MyOpel, `792` starts in source-defined operating phase 1 (partial/ComLatch), then changes to phase 3 around 59.990 s. `782` has the same payload sequence in both captures, including `00 00 00 00 00 00 02 00` after startup and `50 00 00 00 00 00 00 00` after approximately 65 s. Source wake/status fields are **status reports**, not demonstrated externally injectable wake commands. The source's individual partial-wake labels need vehicle validation; do not infer a physical wire from an `RCD_LINE_STATUS` signal name.

This comparison does **not** isolate a MyOpel-specific initiating message. The first recorded frame is already a BSI message. The initiating event may be on the separate telematics CAN port or ENT INFO, may precede the logged valid traffic, or may not be representable as a normal CSV data-frame entry. These remain alternatives, not conclusions.

### Startup validity is distinct from a DBC sentinel

`5C3` initially contains eight zero bytes in both captures. At approximately 1.292 s it becomes `D0 00 02 C1 00 07 D0 00`, decoding to **416 V and 70.5% SOC**. Do not publish the earlier numeric zero as a measured empty battery. The existing DBC describes raw fields and known sentinels; process-level availability/startup gating must still be established before publishing valid varstore observations.

Similarly, initial `334` cell payloads are zero-filled; source group/slot extraction is not sufficient to mark them valid. This is an additional limitation beyond the first READY-only DBC validation. No firmware validity rule is implemented or claimed by this note.

## How CAN wake-up can work without ACK

There are distinct layers:

1. A low-power transceiver/SBC can detect a **wake-up pattern (WUP)** while the MCU and CAN controller are asleep. This is electrical bus activity, not an application service or a universal message ID.
2. The ECU powers/clocks up and initializes its normal CAN controller. A first transmission may therefore wake it without being received or acknowledged as a normal message. [NXP AN2255](https://www.nxp.com/docs/en/application-note/AN2255.pdf) describes this behavior.
3. With selective wake/partial networking, WUP may only activate a small receiver. A matching **wake-up frame (WUF)** with the configured identifier and possibly DLC/data conditions is then needed. TI's [selective-wake application note, section 3.2.5](https://www.ti.com/lit/an/slla521b/slla521b.pdf) explicitly allows the ACK slot to remain recessive because all nodes may be asleep; the ACK delimiter must still be recessive.

Thus **missing ACK does not prove that the electrical wake-up detector saw nothing**. Conversely, a successful ACK is not an application response or proof that BSI acted on the message.

### TJA1043 pattern

The TJA1043 detects sufficiently long **dominant → recessive → dominant** phases within its wake timeout. See [TJA1043 datasheet, section 7.2.5](https://www.nxp.com/docs/en/data-sheet/TJA1043.pdf). The applicable timing depends on the actual receiving transceiver and revision, which have not been identified in BSI.

NXP's [AH1014 section 6.4](https://www.nxp.com/docs/en/supporting-information/AH1014.pdf) warns that a single arbitrary frame at 500 kbit/s need not satisfy its wake detection; it discusses two consecutive frames or a specifically designed single frame. This is a property of the **receiving detector**, not a requirement for a special wake-capable transmitter.

A regular high-speed PCAN transmitter can generate these dominant/recessive runs using ordinary CAN frames. For example, data containing `00 FF 00` provides runs of zeros and ones despite CAN bit stuffing; at 500 kbit/s a five-bit run lasts 10 microseconds. This illustrates waveform construction only: **no CAN identifier/payload has been qualified as the Corsa-e wake command**. Do not assume generic WUP will satisfy selective wake or reach a sleeping telematics-side port through OBD.

## PCAN and ESP32 options

The owner has a conventional PCAN-USB. A reported ACK error occurs **after physical transmission has begun**; it does not prevent the preceding bits from reaching the bus. However, normal automatic retries can leave an unsuccessful frame pending, and application-level “send once” is not necessarily controller single-shot.

PEAK says conventional PCAN-USB `IPEH-002021/002022` lacks **Tx Self-Acknowledge**; this option requires supported CAN FD-capable hardware. On supported hardware it is exposed in PCAN-View. Windows PCAN-Basic does not expose the same configuration according to PEAK support. See [hardware support](https://forum.peak-system.com/viewtopic.php?t=7426), [API support](https://forum.peak-system.com/viewtopic.php?t=7918), and [API comparison](https://www.peak-system.com/know-how/technologies/can-apis/). This does not prevent a conventional PCAN-USB from generating ordinary CAN traffic/WUP.

The ESP32-S3 TWAI driver in **ESP-IDF 5.3.2** offers two useful test arrangements:

| Arrangement | Function | What it proves |
| --- | --- | --- |
| ESP32 `TWAI_MODE_NORMAL`, application TX disabled | Active receiver ACKs valid PCAN traffic; differs from listen-only | Removes the missing-ACK obstacle for the PCAN; ACK may come solely from ESP32, so it does not establish vehicle wake-up |
| ESP32 `TWAI_MODE_NO_ACK`, explicitly bounded test transmissions | Own transmission completion does not require another controller's ACK | Allows controlled physical wake-pattern experiments; success status is not evidence of BSI receipt |

The driver's per-message single-shot option suppresses automatic retry on error/arbitration loss; it does not invent an ACK. See [ESP-IDF 5.3.2 TWAI operating modes and message flags](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/api-reference/peripherals/twai.html).

The **current GW firmware is listen-only** and will therefore provide neither ACKs nor wake transmissions. No test mode, flashing, runtime change or vehicle transmission was performed here. A future experiment should use an explicitly enabled bounded mode and record actual vehicle RX, not count ESP32 ACK/TX status as vehicle success. Publish test state and counters through varstore and preserve the acquisition/control process separation.

## Most useful next measurement

Record the electrical start of activity at OBD and, ideally, the telematics-to-BSI CAN pair during an original remote wake, with error/status logging alongside decoded frames. For an injection test, observe actual CAN-H/CAN-L waveform and vehicle-origin RX: this separates missing/incorrect physical pattern, adapter retry behavior, selective-wake filtering and a BSI port that is not wake-enabled. The present CSV alone cannot distinguish those causes.
