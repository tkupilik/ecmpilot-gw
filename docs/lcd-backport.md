# Gateway / LCD parity — 2026-11-01

LCD CAN, phone telemetry and reset diagnostics are adapted to native GW varstore
and BSD runtime. No ufrm, GUI or LCD hardware code is imported. Version stays0.5.0,
UDP staysv0. The latest GW image has not been uploaded or tested on the vehicle.

| Area | ecmpilot-gw | opel_dash reference |
| --- | --- | --- |
| Hardware | Waveshare RS485-CAN, TX15/RX16; no PSRAM | LCD TX20/RX19; PSRAM/LVGL |
| Default CAN | INFO125kbit/s, NORMAL,no TX queue | INFO125kbit/s, NORMAL |
| Optional profile | HSCAN1/500kbit/s,listen-only; HV SOC/voltage5C3 | INFO mappings |
| CAN acquisition | CanTask CPU1/priority5,alerts priority6 | TaskCan/CanNet |
| RX / processing |12 INFO handlers; separate vehicle actor CPU1/priority4 | Corresponding INFO layouts; vehicle actor |
| Storage | Native global GW varstore, independent Force, source ages | LCD VS; same integer units |
| Range | kmx10 storage,phone/HA km | Whole-km storage |
| Wake | One eligible UDP request,default43C/DLC2,00 80; ID/payload Force | Latest source43C/DLC2,00 80 |
| Version TX | **Removed**, no5DF transmitter | Reference includes old500 ms5DF experiment |
| TX transport | Single-shot, no driver backlog/retry; actual ID/bytes logged | Same one-shot model |
| Phone / HA | Same optional telemetry; MQTT24 entities,CPU0,disabled by default | Vehicle telemetry publisher |
| Demo | Live default; can.replay_enabled disables TX | Live default; DASH_DEMO_ENABLED |
| GVRET | TCP23 RX-only,64x24-byte internal FIFO | Larger PSRAM FIFO |
| Reset log | Named reason;12-record frozen RTC tail,64 live records | Corresponding implementation |
| Physical climate/charge commands | Not implemented | Not implemented |

CAN RX handlers only validate and publish varstore. The application actor combines
2AD/350 temperatures, preserves source ages and publishes wake requests. TaskCan
alone owns TWAI. UDP only publishes validated events and reads observations;
periodic HA publication does not wake BSI. INFO supplies no pack voltage; select
HSCAN1 for5C3 at the cost of the INFO wake path. One profile runs at a time.

Existing numeric signal IDs stay stable; the three wake-configuration descriptors
are appended (173 total after the preceding power-policy additions). Old version
TX IDs are reserved and cannot trigger TX. The separate physical5DF RX counter
remains available. Live values continue updating underneath Force.

See [wake configuration/policy](info-can-wake.md), [reset diagnostics](reset-diagnostics.md),
[RX mappings](can-rx.md), [UDP](udp.md), [HA](ha.md), [GVRET](gvret.md) and the latest
[evidence](info-can-wake-verification.json). Older LCD-backport reports describe
historical builds and their former HS1 default/version transmitter.
