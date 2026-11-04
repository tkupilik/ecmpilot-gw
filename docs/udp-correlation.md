# UDP correlation and loss recovery

The Android/GW protocol identifies a logical operation separately from each UDP
exchange. The app uses this directly; there is no capability negotiation.

| Field | Meaning |
| --- | --- |
| `client` | Persistent application/client identity (32 lowercase hex characters) |
| `client_session` | Random 128-bit scope created for the app process |
| `correlation_id` | Free-running unsigned 32-bit operation counter, 1–4294967295 |
| `id` / `reply_to` | Message ID of one UDP exchange; this is the wire representation of a message ID |
| `boot` | Gateway boot identity; an old boot cannot admit a new vehicle command |

One operation shares its correlation across capability/state preflight, vehicle
command, wake/CAN sequence and result queries. Standalone monitoring reads are
separate logical operations. Retransmission sends identical bytes, preserving
both IDs. A different exchange, such as a result query, gets a new message ID
while retaining the operation correlation. Counter wrap rotates the client
session before reusing 1. Android persists the correlation with its pending
receipt, so a restarted app can query an older session without replaying it.

Example command metadata (other request fields omitted):

```json
{
  "id":"11111111111111111111111111111111",
  "client":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "client_session":"1234567890abcdef87654321fedcba98",
  "correlation_id":1042,
  "op":"vehicle.refresh",
  "args":{}
}
```

To query the result, send `command.get` with empty `args`, the same client,
session and correlation, and a new message `id`. The root reply echoes the
incoming `correlation_id` and `reply_to`. The command receipt includes its
correlation/session and the original canonical command message `id`.

## Deduplication and admission

GW looks up vehicle operations by `(client, client_session, correlation_id)`.
Reusing the message ID is also checked. A duplicate returns the current receipt
without publishing another `request.vehicle.generation`. A different message ID
can therefore refer to the same operation without generating another CAN action.
Changed arguments, operation kind, deadline or domain revision conflict with the
recorded intent and return `id_conflict`.

The cache retains accepted work and terminal results/refusals for 10 minutes
after completion. Remembering Busy and other state-dependent refusals prevents
an already-refused queued duplicate becoming executable after conditions change.
Capacity is 256 records. If a refusal cannot be retained because the cache is
full, admission remains blocked until its deadline passes, even if an earlier
record expires in the meantime. Passive queries remain available.

The admission deadline remains at most 5 seconds ahead of GW time. Duplicate
lookup precedes expiry checks, so an admitted operation can still return its
receipt after that deadline. An expired packet with no retained record cannot
start work. The cache is RAM state; `boot` prevents replay across GW restart.
Unscoped bench requests are still representable as correlation 0 internally.

## Trace through the controller

UDP commits `request.vehicle.correlation_id` and `request.vehicle.session0..3`
before the existing request-generation commit. The CPU1 controller latches them
with the operation, publishes `control.correlation_id` / `control.session0..3`
and retains them through all protothread children. Session words are ordered
from most significant to least significant; uint32 values use int32 varstore bit
storage. Metadata uses raw producer values, so Force cannot forge its identity.

Logs include `corr=<32-hex-session>/<counter>` on UDP admission, controller
phases/results, CAN intents and actual TaskCan submissions. `tx=<generation>`
links the mailbox intent to its driver-submission result. CAN payloads themselves
are unchanged: correlation is internal metadata, not an extra vehicle frame.

Automatic timer operations use their own per-boot counter, zero session words
and `control.source=2`; phone operations use source 1. Gateway boot and source
distinguish the timer scope. The timer still cannot overwrite a manual result
before UDP retains its receipt.

## Phone timeout policy

| Policy | Value |
| --- | --- |
| Read response budget | 5 s |
| Write acknowledgement budget | 6 s |
| Sends relative to first attempt | 0, 700, 1800, 3500 ms |
| Offline threshold since any valid reply | 15 s |
| Vehicle result watch | 35 s from original command send |

A single read timeout keeps the socket and cached observations. An optional
details/program/settings timeout skips remaining optional reads that iteration,
giving core state polling priority. After 15 s without valid replies the app
marks offline and permits reconnect/DNS resolution. Reconnect retains the last
snapshot within the same GW boot; a changed boot clears it.

Lost write acknowledgement or an ordinary transport interruption leaves the
original operation pending. The app queries its correlation rather than creating
another vehicle command. Result polling/reconnect does not restart the 35 s
watch. A semantic refusal or watch expiry produces an error; transient packet
loss alone does not immediately produce an unconfirmed-action popup. Unsent
actions are discarded on hard connection failure rather than waking a vehicle
later when connectivity returns. The preflight no longer imposes a separate
1-second RTT limit; the original 5-second GW admission deadline still applies.

The GW socket's 200 ms receive timeout is a housekeeping wake-up for the UDP
task. It is not an end-to-end reply deadline, and its value is unchanged.

## Verification

Native production-code tests cover deduplication with changed message IDs,
changed-intent conflicts, queries by correlation, session changes, uint32 bounds,
reboot protection, cached refusals/full-cache admission and 1 KiB responses.
Android JVM loopback tests drop replies, delay one by 3.1 s, inject a matching
message with the wrong correlation, and reuse a socket after a full read timeout
while delivering an old reply. No physical CAN requests or OTA uploads are part
of these checks. Real weak-Wi-Fi operation still needs an on-device test.
