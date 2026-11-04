> Historical experiment: removed from the current firmware on 2026-10-31. No GPIO wake API or waveform is present.

# CAN wake-up pattern: ESP32 GPIO test

Research/implementation date: 2026-10-28. The explicit web GPIO experiment is implemented and built; it has not been flashed or qualified on the vehicle. Firmware version remains 0.5.0.

## Running the test

After installing the new firmware, open **Home → CAN wake-up test → Send wake-up pulse**. One click queues one fixed 10/10/10 us pattern. The button stays disabled while pending, or if fresh real CAN health does not report listening. There is no automatic transmission at boot, during polling or on tab changes, and no automatic POST retry. Watch the result and ESP Log, plus an independent passive PCAN capture for actual vehicle traffic.

The web endpoint `POST /api/can/wake-test` accepts an empty body and returns HTTP 202 plus `requested`, the generation queued through varstore. `GET` on the same URL returns readiness, request/completion generations, result state/error, phase length and receiver pause duration. Only the HTTP task writes `request.can_wake.generation`; only the CAN process writes `can.wake_test.*`. Their real producer values are used for this explicit physical test, so force overlays cannot initiate it or manufacture readiness/completion. An unconsumed request older than two seconds is completed as a timeout without sending a pulse.

## Timing and proposed waveform

The [NXP TJA1043 datasheet, revision 6, section 7.2.5 and table 8](https://www.nxp.com/docs/en/data-sheet/TJA1043.pdf) describes a bus wake-up pattern (WUP): dominant, recessive, dominant. Its dominant/recessive detection thresholds are 0.5–3 microseconds, typical 1.75 microseconds; the complete sequence must fit the receiver timeout of 0.5–2 milliseconds. These are receiver tolerances, **not** a requirement to keep transmitted pulses below 3 microseconds. For guaranteed timing against this specification, exceed the maximum filter threshold and finish before the minimum timeout.

[NXP AH1014, revision 1.50, section 6.4](https://www.nxp.com/docs/en/supporting-information/AH1014.pdf) gives a more conservative recipe: at least 5 microseconds per phase, complete within 0.5 milliseconds. Its filter range differs from the cited datasheet; the proposed 10-microsecond phases cover both references. The actual BSI receiver and its configuration have not been identified.

Start with this **single** sequence:

| Phase | TXD level | Bus state | Proposed duration |
| --- | --- | --- | --- |
| Before test | HIGH | Recessive / released | Until the bus has been quiet |
| 1 | LOW | Dominant | 10 us |
| 2 | HIGH | Recessive / released | 10 us |
| 3 | LOW | Dominant | 10 us |
| After test | HIGH | Recessive / released | Remain HIGH; observe traffic |

```text
Time from first edge:      0          10         20         30 us
TXD:           HIGH ------+          +----------+          +------ HIGH
                          |          |          |          |
               LOW        +----------+          +----------+
Bus:                         DOM         REC        DOM       REC
```

Total pattern duration is 30 us. A second, separate trial can use 20/20/20 us. Optional controlled phase lengths of 5, 10, 20, 50 and 100 us produce complete patterns of 15–300 us; these are proposed experiment settings, not Opel requirements. Do not add millisecond-long dominant holds or continuous repetition. Let the vehicle return to the same sleeping state between trials.

## Existing Waveshare hardware

The [Waveshare ESP32-S3-RS485-CAN schematic](https://files.waveshare.com/wiki/ESP32-S3-RS485-CAN/ESP32-S3-RS485-CAN-Schematic.pdf) shows U8 as **TJA1051T/3/1J**, with S tied LOW through R18. The current firmware uses GPIO15 for TX and GPIO16 for RX. GPIO15 drives TXD through the board's digital isolator; use this existing path, never connect an ESP32 GPIO directly to CANH/CANL. The isolated supply must be powered.

The [TJA1051 datasheet, revision 9, sections 7.1–7.2 and table 8](https://www.nxp.com/docs/en/data-sheet/TJA1051.pdf) confirms LOW TXD produces dominant and HIGH releases the bus. Its TXD dominant timeout is 0.3–5 ms, so the proposed 10–100 us dominant phases stay below the minimum timeout. No special wake-capable transmitting transceiver is needed: wake detection is a function of the receiving ECU. TJA1043 is a timing reference here, not an identified chip in BSI or on our board.

## Firmware implementation

The quick test uses an IRAM waveform routine, inline GPIO register writes and three `esp_rom_delay_us(10)` busy-waits. A private FreeRTOS critical section prevents normal task switching and masks local interrupts on CPU1 during approximately 30 us. It does not stop the scheduler or Wi-Fi on CPU0. Register writes/call overhead make the physical phases slightly longer than the requested delays; verify with an oscilloscope rather than treating them as calibrated pulses. The current configuration disables dynamic frequency scaling. There is no allocation, logging or RTOS sleep inside the critical section.

CPU1 test flow:

1. Accept one explicit test request. Exchange request/status/counters through varstore, following the existing acquisition/control separation.
2. The CAN process is the sole driver owner. Stop TWAI, preload the GPIO output latch HIGH, then switch its TX matrix routing to GPIO output. Keep the pad output-enabled; the RX pin/routing is unchanged. Setup and SDK calls are outside the critical section.
3. Generate LOW/HIGH/LOW/HIGH exactly once, leaving TXD HIGH before exiting the critical section. Restore the original controller-0 TX signal and restart TWAI in its existing listen-only mode, even if GPIO preparation failed. If restart fails, keep TXD routed to GPIO HIGH and publish the error. A failed stop sends no pulse and leaves the routing unchanged. No automatic driver recovery/repetition is added.
4. Publish elapsed receiver pause, result error/state and finally the completed generation through varstore. State 2 means pulse executed and listen-only restored; state 3 means failure. This result does not infer ECU wake-up. The SDK restart clears its receive queue and resets the hardware error counters; queued/early frames can be lost during this gap.

Observe vehicle-origin frames for an initial two-second window after clicking. This is an experiment setting, not the receiver's wake timeout; the firmware does not automatically declare a wake candidate. A future timing implementation could use the [ESP32-S3 RMT peripheral](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/api-reference/peripherals/rmt.html) to generate the waveform without a CPU critical section; RMT is not used by the current quick test.

Use an independent PCAN in **listen-only** mode during the experiment to capture early vehicle frames while TWAI is being restored. An oscilloscope on CANH/CANL, preferably with an additional TXD trace, verifies the real differential waveform and propagation through the board. A valid-frame CSV cannot show these raw pulses; an awake normal CAN controller may flag the unframed pattern as an error.

## What the result can establish

The owner-run journal `logs/wake-scan/20261028T120612.921087Z-app.jsonl` records 68/68 MyOpel-profile trials completed, with zero vehicle data frames during or after the trials and no wake candidate. This was the 68-ID profile, not the 89-ID union profile. It does not contain an electrical waveform measurement.

AH1014 explains that a single arbitrary frame at 500 kbit/s need not satisfy WUP, and discusses two consecutive frames. Our repeated transmissions likely already produced a basic WUP, but that remains an inference without a scope trace. GPIO/RMT testing removes uncertainty about the intended pulse timing; it does not guarantee BSI will wake from OBD.

**WUP is distinct from a selective wake-up frame (WUF).** In partial-networking transceivers, WUP can enable only a small receiver; a configured CAN identifier and possibly DLC/data match is then required to wake the ECU. Raw pulses cannot substitute for that valid CAN frame. See [TI SLLA521B, sections 2.2–2.3 and 3.2](https://www.ti.com/lit/an/slla521b/slla521b.pdf). There is no evidence yet that this BSI uses selective wake on this port.

A correctly measured WUP followed by silence would leave several explanations open: a port without enabled bus wake, selective wake requirements, or a different wake path. In particular, the OBD and telematics pairs terminate on different BSI connector pins; their sleep-time electrical connectivity is not established. Conversely, repeatable vehicle-origin broadcasts after the pulse would support wake capability on OBD, but would not prove heating/charging command routing.
