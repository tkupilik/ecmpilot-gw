# INFO CAN wake configuration

Current GW test profile: INFO, 125000bit/s, NORMAL; TX15/RX16. No GUI or LCD
hardware is imported. The physical interface must be suitable for INFO CAN;
bitrate selection alone does not validate the stock high-speed transceiver for
that network. Optional HSCAN1/500kbit/s remains listen-only, without wake TX.

## Varstore configuration

Filter the web Varstore tab for `can.wake_tx`. These are normal producer values
with independent Force overlays. The web numeric input accepts decimal values.
Forces are volatile: Release restores the defaults; reboot clears them.

| Signal | Default | Test / interpretation |
| --- | --- | --- |
| can.wake_tx_id |1084 (`0x43C`) | Force 1025 for `0x401`; only these two IDs are accepted |
| can.wake_tx_payload_u16 |128 (`0x0080`) | Packed`0xB0B1`; high byte is CAN byte0, low byte is byte1 |
| can.wake_tx_config_error |0 |0 OK;258 (`ESP_ERR_INVALID_ARG`) rejects the event without transmitting |

DLC is fixed at 2; standard 11-bit ID, non-RTR, single-shot. A single int32 sample
holds both payload bytes, so a concurrent payload Force cannot mix their values.
Examples: 128 sends`00 80`, 2 sends`00 02`, 256 sends`01 00`, 65535 sends`FF FF`.
The full unsigned16-bit payload is available for testing wake-cause bits. This
packing is transport configuration, not confirmation of each bit's meaning on
both IDs. Choose payload from the corresponding DBC/capture evidence.

Default `0x43C [00 80]` matches the latest local LCD source. The previous owner
wake test used`00 02`; the new default is not a new GW vehicle validation.

## Scheduling and ownership

The [vehicle protothread hierarchy](vehicle-control.md) owns all retries through
one shared countdown. Explicit accepted actions may issue up to 10 wake intents,
100 ms apart, then wait for settled fresh BSI Normal before 394/35B. Budgets are
shared by any re-wake within that operation; passive polling and HA never wake.

TaskCan has a single TX mailbox for wake and command frames, no independent
OnWakeTx/event-counter queue. It checks fresh raw Normal immediately before a
wake and suppresses an unnecessary frame. Configuration Force remains supported;
Normal/readback Force cannot manufacture transaction completion. 200 ms mailbox
expiry, pending authorization and generations prevent expired requests or late
submission callbacks from replaying a frame. No version 5DF TX exists.

The actual selected ID and bytes are logged with the driver submission result:

```text
BSI wake TX: 0x43C, DLC2, bytes=00 80, submit=ESP_OK
```

`ESP_OK` means driver acceptance, not proof of ACK or vehicle wake. Inspect new
RX traffic and fresh`bsi.power_management` to verify the response. Counters:
`can.wake_tx_request_count`,`.wake_tx_attempts`,`.wake_tx_submitted` and
`.wake_tx_last_error`. `can.wake_state`:0 idle,1 BSI Normal,2 new eligible wake,
4 inhibited/profile disabled/replay.

## Version transmitter removed

The synthetic500 ms`0x5DF` sender, its preparation API and TaskCan handler are
removed completely. The old numeric`can.version_tx_*` IDs remain reserved to
avoid renumbering existing signals; they have no transmitter or scheduler.
`can.version_rx_count` still counts physical5DF receptions for diagnostics.
Original-cluster traffic may therefore be observed without GW claiming its ID.

See [vehicle sleep limits](../doc/car_lowpower.md), [RX architecture](can.md),
[reset diagnostics](reset-diagnostics.md) and [verification](verification.md).
