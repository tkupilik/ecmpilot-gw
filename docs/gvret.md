# SavvyCAN diagnostic receiver

The gateway exports physical CAN RX traffic using the GVRET binary protocol over
**TCP port 23**. There is one `gvret` task on **CPU0**, priority 3, with a 6144-byte
internal stack and one active client. It does not create a session task per
connection. The listener accepts and immediately closes additional connections
while the client slot is occupied.

## Connecting

Add a GVRET network connection in SavvyCAN and enter the gateway's IP address
(or its router-resolvable hostname, usually `ecmpilot-gw.lan`). The expected port
is 23. The adapter reports **one bus, CAN0,125000 bit/s, normal for the default INFO profile**. Optional HSCAN1 reports500000 bit/s, listen-only. SavvyCAN's
normal binary-mode negotiation, device/bus queries, time sync and keepalive are
implemented. No serial-console or phone UDP configuration needs to change.

Only **gateway-to-SavvyCAN CAN data** is implemented. Incoming CAN TX, echo and
bus/digital-output configuration commands are consumed and ignored; they cannot
call TWAI transmit, change bitrate or change the operating mode. The actual profile configuration is reported again on the next query. Unknown commands
close the connection cleanly, rather than interpreting their payload as new
commands. There is no network auto-discovery service.

`can.replay_enabled` controls decoded observations only and defaults to 0/live.
GVRET always exports **physical traffic**; local replay frames are never mixed
into this stream. Release telemetry forces and disable replay for live monitoring.

## Data path and memory

```text
TWAI RX -> TaskCan on CPU1 -> raw FIFO -> gvret on CPU0 -> TCP -> SavvyCAN
                         -> existing RX decoder -> GW varstore -> phone / web
```

The raw tap is immediately after `twai_receive()`, before the decoder's ID,
extended-frame and demo filters. All structurally valid classic standard and
extended data frames are exported, including IDs with no decoder. RTR is ignored.
CAN FD is not implemented by this classic TWAI receiver.

`xQueueCreateStatic()` uses a static internal-DRAM control block and **64 ×24-byte
records = 1536 bytes of internal RAM**. Every record holds a 64-bit timestamp,
ID/extended flag, session/DLC tag and up to 8 data bytes. No pointer to a local
TWAI message is retained. Synchronization uses the standard FreeRTOS queue.

TaskCan calls `xQueueSend(...,0)`: no waiting for space, socket calls or per-frame
heap allocation. Full FIFO drops the new diagnostic copy and increments
`gvret.dropped`; normal decoding continues. Queue operations use normal short
kernel critical sections. Peak depth is a sampled high-water estimate.

A separate static internal-RAM structure holds the **1024-byte transmit buffer**,
128-byte receive buffer, protocol state and frame-completion markers (about
1.4 KiB total). The task uses a 6 KiB internal stack; sockets retain their normal
SDK allocations. PSRAM is disabled. FIFO and session buffers are initialized once
and reused. No negotiated binary client means no capture history is accumulated.

The worker polls nonblocking sockets, processes at most 128 incoming command
bytes and 32 queued frames per service turn, and sleeps 5 ms with a client or 50 ms
without one. Frames are batched into TCP writes. Partial sends retain the exact
remaining byte offset; control replies and data frames share an ordered stream.

Disconnect clears the parser, pending output and queue. Each FIFO record also
carries a session tag: a producer already in flight during disconnect cannot
leak an old frame into the next client. Binary negotiation has a 5-second limit,
an incomplete command a 1-second limit, and pending output with no send progress
a 2-second limit. Closing a stalled client releases the slot. An established,
quiet client is not required to send keepalives if it has disabled validation;
socket keepalive is enabled.

## Time and protocol

Frame time is captured from `esp_timer_get_time()` immediately after TWAI dequeue.
It is independent of FIFO/network delay, but is **not a hardware timestamp at the
wire**; queued driver frames already include dequeue latency.

The internal record keeps 64-bit microseconds. Legacy GVRET carries the low 32
bits on the wire (wrap approximately every 71.6 minutes); time-sync replies use the
same clock. Standard/extended IDs, DLC, data and trailing-zero framing follow
GVRET. Consumers must handle the protocol's timestamp wrap when interpreting a
long session. TCP read/write boundaries are not GVRET record boundaries.

Supported query commands: time sync 1, bus parameters 6, device info 7, keepalive 9,
bus count 12, extended-bus slots 13. Binary mode is enabled by `E7`. Legacy bus
parameter replies retain the required two-slot shape, but slot1 is disabled;
extra SWCAN/LIN slots are disabled. Unused optional digital/analog query commands
are not implemented. Write commands 0/4/5/8/10/11/14 are consumed without action.

Protocol references: [SavvyCAN client](https://github.com/collin80/SavvyCAN/blob/master/connections/gvretserial.cpp)
and [GVRET binary format](https://github.com/collin80/M2RET/blob/master/CommProtocol.txt).
The project implementation is independent BSD-2-Clause code; upstream source
code is not imported.

## VarStore diagnostics

Filter the web Varstore tab for `gvret.`. The worker publishes every 500 ms.
Values are raw int32 and cumulative counters saturate at INT32_MAX.

| Signal | Meaning |
| --- | --- |
| `gvret.state` |1 listening,2 client before binary negotiation,3 streaming,4 startup fault |
| `gvret.queued` | Current FIFO frame count |
| `gvret.queue_peak` | Sampled peak FIFO depth since boot |
| `gvret.enqueued` | Physical data frames admitted during an active binary session |
| `gvret.dropped` | New diagnostic copies lost because the FIFO was full |
| `gvret.sent` | Complete CAN records accepted by the local TCP socket |
| `gvret.discarded` | Queued/pending records flushed on disconnect or belonging to an old session |
| `gvret.rejected_clients` | Additional connections closed while busy |
| `gvret.ignored_commands` | TX/configuration commands consumed without execution |
| `gvret.protocol_errors` | Unknown command/feed failure closures |
| `gvret.tx_bytes` | All bytes accepted by TCP, including control replies |
| `gvret.last_error` | Last socket/protocol errno; 0 initially |

`gvret.sent` confirms acceptance by the local TCP stack, not receipt/processing
by SavvyCAN. There is no application frame acknowledgement in this protocol.
Check `can.rx.missed_count`/`can.rx.overrun_count` separately: those represent losses before
the GVRET tap. An unattached client is not counted as a capture loss. Queue reset
counts are diagnostic snapshots; an in-flight old-session item is counted when
discarded later.

The additional `gvret.queue_capacity` and `gvret.memory_external` diagnostics
report 64 and 0. Listener startup failure leaves web/UDP services available and
publishes fault state/errno. The task appears in Tasks.

## Verification

```powershell
python tests/build_gvret_host.py
python tests/test_gvret.py
```

Seventeen tests compile the actual C worker, protocol, native GW varstore and variable
definitions. Socket calls delegate to real loopback TCP; only IDF task/queue,
clock and memory-capability APIs are host adapters. Coverage includes the
fragmented SavvyCAN handshake, all 9520 frames of `info_start_jizda.csv` in order,
standard/extended/DLC 0 records, RTR rejection, receive timestamps/wrap, partial
writes, full-FIFO losses, slow-client timeout, second-client rejection,
disconnect/reconnect, stale producer session tags, read-only command consumption
and mixed control/data output. These tests do not establish FreeRTOS cross-core
timing, physical Wi-Fi performance or compatibility with the user's installed
SavvyCAN build.

On the board, connect SavvyCAN while vehicle traffic is active. Confirm one
125 kbit/s INFO bus (or500 kbit/s when HSCAN1 is selected), compare counts/bytes with a second capture tool, and check FIFO
losses and upstream CAN losses under Wi-Fi load. Test a second client and
disconnect/reconnect; the next session must not start with old records. The 64-frame FIFO is finite: record observed drops and check heap/stack margins.

INFO uses NORMAL mode for [configurable wake TX](info-can-wake.md);
optional HSCAN1 remains listen-only, with wake TX disabled. Version5DF TX is removed. The TCP adapter still ignores all incoming CAN TX
and configuration requests, regardless of the actual controller mode.
