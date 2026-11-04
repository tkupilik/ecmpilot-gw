# Live PCAN wake-up tests, 2026-10-28

## Result

**No wake-up was observed from the three diagnostic requests tested on sleeping OBD 6/14.** A subsequent owner unlock restored normal broadcast reception, confirming the receive connection and 500 kbit/s bus setting. However, the tested diagnostic requests also received no application-level answers in the unlocked state. A positive diagnostic control with ignition/READY is therefore still required; this session does not prove that the OBD port can never wake BSI.

The owner could not enable ignition/READY during this session and reported that the car subsequently returned to sleep. The final receive-only test observed three seconds with zero data frames. All test transmissions were stopped by explicit hardware resets, and our API client disconnected successfully.

## Equipment and procedure

- Owner-authorized live testing on the connected **PCAN-USB**, channel `PCAN_USBBUS1` / `0x51`, standard CAN at **500 kbit/s**.
- PCAN-View remained open as another client. Its pending transmit can be cancelled with ESC according to the owner. This session used the PCAN-Basic API instead of automating PCAN-View.
- Official PEAK PCAN-Basic **5.1.0.1194**, x64 DLL, loaded from workspace scratch without installing or replacing system libraries. The pre-existing workspace DLL was x86 version 4.7.1 and could not be loaded in the x64 Python process.
- `PCAN_HARD_RESET_STATUS` was enabled and read back as 1 before every test. Each case reset the controller before baseline, after the 0.8-second TX window, and during final cleanup. No reliance on a queue-only `CAN_Reset`.
- Normal controller mode, no receive-ID filter added. The three sleeping tests required a two-second silent baseline; otherwise they would abort before sending.
- One `CAN_Write` per case, with a **0.8 s** observation window. Automatic controller retries during missing ACK were allowed within that window; the actual number of physical transmissions was not measured.
- After the TX-window reset, capture continued for three seconds. Final hardware reset and `CAN_Uninitialize` were protected by cleanup paths.
- Only TesterPresent and ReadDataByIdentifier requests were issued. No actuator commands, diagnostic-session changes, security access, ECU reset, or programming services were sent.

PEAK documents that ordinary `Reset` clears channel queues without necessarily resetting the controller. `HardResetStatus` makes it perform a hardware reset even with other clients attached: [Reset](https://www.peak-system.com/documentation/API/PCAN-Basic.Net/html/9ca6aa06-164b-c163-3d69-9a8155514c2b.htm), [HardResetStatus](https://www.peak-system.com/documentation/API/PCAN-Basic.Net/html/78242dd8-d869-4ca1-9161-f9b28c4a2b64.htm).

## Sleeping tests

| Start UTC | Request ID | Payload | Meaning / provenance | Baseline / TX / post-reset received data frames |
| --- | --- | --- | --- | --- |
| 11:47:21 | `752` | `02 3E 00 00 00 00 00 00` | BSI TesterPresent; PSA-RE candidate address, expected `652` | **0 / 0 / 0** |
| 11:47:39 | `6A2` | `02 3E 00 00 00 00 00 00` | VCU TesterPresent; address used by Android dashboard, expected `682` | **0 / 0 / 0** |
| 11:47:58 | `6B4` | `03 22 D4 10 00 00 00 00` | Battery SOC read from working dashboard profile, expected `694` | **0 / 0 / 0** |

For each sleeping case, `CAN_Write` accepted the message into the local transmit queue. During the TX window, a PCAN status `0x8` (bus-heavy/warning) was reported. This is **not** an application response, nor a direct count or diagnosis of ACK errors. After hardware reset and the three-second observation window, controller status was `0x0` again. No vehicle-origin data frame was captured.

These results apply to the exact requests and durations above. There was no oscilloscope measurement, no identification of the receiving BSI transceiver, and no confirmed selective-wake frame. Do not infer an exhaustive test of every wake-up mechanism.

## Unlocked control tests

The owner then reported “unlocked, alive.” The same API received the normal broadcast traffic immediately. Each request was still a single local write with the same 0.8 s TX window and reset cleanup.

| Start UTC | Request | Baseline / TX / post-reset received data frames | Diagnostic response |
| --- | --- | --- | --- |
| 11:50:28 | `752: 02 3E 00 00 00 00 00 00` | **2173 / 879 / 3260** | No `652` |
| 11:50:44 | `6A2: 03 22 D8 CA 00 00 00 00` | **2169 / 879 / 3258** | No `682`; DID/address taken from dashboard profile |
| 11:51:14 | `6B4: 03 22 D4 10 00 00 00 00` | **2169 / 857 / 2870** | No `694` |

Controller status remained `0x0` in these unlocked cases. Normal broadcasts were present, but that alone is not a positive test of diagnostic addressing/routing or ECU diagnostic availability in this power state. Lack of a diagnostic answer must not be described as proven selective wake filtering.

### Final state

At **11:53:16 UTC**, after the owner reported the car had fallen asleep again, a final three-second receive window contained **zero data frames**. Hardware reset and disconnect both returned success. No pending transmission from our test client was left active.

## Evidence and next test

Complete per-case API events, relative host timestamps, adapter timestamps and received frames/status records are under [`logs/wakeup-2026-10-28/`](../logs/wakeup-2026-10-28/), with a [compact summary](../logs/wakeup-2026-10-28/summary.json). Failed setup attempts without a receive window or vehicle transmission are excluded from that summary. The test harness and vendor SDK remain in local workspace scratch at `C:\Users\tomas\Documents\Codex\2026-10-23\zda\work\pcan-wakeup`; no vendor DLL or firmware change is added to this repository.

Next establish a **positive diagnostic response in ignition/READY on the same PCAN connection**. If it works only in READY, document that power-state dependency before treating the request as a wake candidate. If even READY produces no response, compare the known-working ELM/dashboard traffic on this exact pair and investigate diagnostic addressing/routing/transport first.

For physical wake verification, compare CAN-H/CAN-L waveforms and the telematics-side BSI ports with OBD during an original remote wake. Ordinary repeated traffic can satisfy a basic WUP, while selective wake or a disabled/asymmetric OBD wake path remains possible. The present negative tests do not distinguish those possibilities.
