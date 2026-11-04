# INFO CAN wake configuration

The current sequence is defined in [CAN requests](can-requests.md), with
[controller ownership and timing](vehicle-control.md). Default hardware profile:
INFO, **125 kbit/s, normal mode, TX15/RX16**. HSCAN1 stays listen-only without wake.

## Varstore overrides

Filter Varstore for `can.wake_tx`. Force changes the effective configuration;
Release restores producer defaults. Forces are volatile and clear at reboot.
The web numeric editor takes decimal values.

| Signal | Default | Meaning |
| --- | --- | --- |
| `can.wake_tx_id` | 1084 (`0x43C`) | Only 1084 / `43C` and 1025 / `401` are accepted |
| `can.wake_tx_payload_u16` | 128 (`0x0080`) | Packed `0xB0B1`: high byte is CAN byte 0, low byte is byte 1 |
| `can.wake_tx_config_error` | 0 | 0 OK; 258 / `ESP_ERR_INVALID_ARG` rejects invalid configuration |

DLC is fixed at 2, standard ID, non-RTR and single-shot. Examples: 128 sends
`00 80`; 2 sends `00 02`; 256 sends `01 00`. A packed sample prevents mixed
payload bytes during a concurrent override. Cause meanings must be checked for
the selected ID; an arbitrary 16-bit override is not a validated vehicle command.

Changing these values does **not** itself send a frame. The controller samples
them during an admitted operation requiring wake. There is no GPIO pulse test,
independent wake sender or periodic `5DF` version transmitter.

## Diagnostics

TX logs show the actual ID/payload, correlation and driver result:

```text
BSI wake TX: 0x43C, DLC2, bytes=00 80, submit=ESP_OK
```

Inspect new raw BSI `036` PowerManagement observations for wake confirmation;
driver submission alone does not establish it. Counters include
`can.wake_tx_request_count`, `.wake_tx_attempts`, `.wake_tx_submitted` and
`.wake_tx_last_error`. `can.wake_state`: 0 idle, 1 Normal, 2 eligible wake,
4 inhibited/profile disabled.

See [vehicle sleep/refusal](../doc/car_lowpower.md),
[reset diagnostics](reset-diagnostics.md) and [Testing](testing.md).
