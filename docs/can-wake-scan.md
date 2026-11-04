# Recorded-ID wake-up scan

[`tools/can_wake_scan.py`](../tools/can_wake_scan.py) tries one actual captured payload per standard CAN ID and stops on incoming vehicle data. The frame sets are hardcoded, so the original CSV files are not needed to run it.

| Profile | Frames | Provenance |
| --- | --- | --- |
| `app` (default) | 68 unique IDs | First observed Rx payload per ID in `logs/opel_apka.csv` |
| `all` | 89 unique IDs | The 68 above plus 21 IDs only seen in `logs/obd_hscan1.csv` (READY) |

IDs are tested in ascending order. Actual DLC and payload bytes are preserved, including counters/checksums as captured; they are not regenerated. This tests a particular recorded payload for each ID, **not every possible data pattern, rolling counter, multiplexer value or multi-message sequence**. A negative scan does not establish that CAN wake-up is impossible.

## Run

Python 3.10+; no Python packages required. Preview does not load a DLL or access hardware:

```powershell
python tools/can_wake_scan.py --profile all
```

On Windows, execute against PCAN-USB channel 1, Classical CAN at 500 kbit/s:

```powershell
python tools/can_wake_scan.py --profile all --execute
```

The PCAN-Basic x64 DLL (API 4.8 or later) is resolved from `--dll`, environment variable `PCAN_BASIC_DLL`, the local ignored `build-host-tests/pcan-sdk/PCANBasic.dll`, or the system library search. The DLL must match the Python process architecture. A matching DLL is cached in that ignored directory on this workstation; vendor code is not added to Git. Obtain the SDK from [PEAK](https://www.peak-system.com/products/software/development-packages/pcan-basic/) elsewhere.

An existing PCAN-Basic client must disconnect first. The scanner checks channel availability before initialization; PCAN-View's shared-client state is accepted, an API-owned channel is not.

To repeat one candidate after allowing the car to return to sleep:

```powershell
python tools/can_wake_scan.py --profile all --only-id 432 --execute
```

`--start-id 400` resumes at IDs >= hexadecimal 400. `--channel 0x52` selects USB channel 2. Defaults are `--tx-ms 200 --observe-ms 1000 --quiet-ms 1000`; the TX window is limited to at most 1000 ms.

## Per-ID behavior

1. Initialize normal mode, enable and read back `PCAN_HARD_RESET_STATUS`, enable local-echo marking and open the receive filter. Refuse transmission if required setup fails or the channel is listen-only.
2. Hardware-reset and require an initially silent receive window of at least 2 s. Require another quiet window before every ID. Existing traffic stops the scan without sending the next frame.
3. Call `CAN_Write` **once** for the selected ID/payload. Observe RX for up to 200 ms by default, ending early on vehicle data.
4. In a `finally` block, hardware-reset PCAN to clear the pending transmission and stop controller retries. A plain queue flush is insufficient for this purpose.
5. Observe RX for another second after reset. Incoming vehicle data marks that ID as a **wake candidate**, stops the scan and is recorded for inspection. A candidate needs an isolated repeat to distinguish causation from an unrelated unlock, cloud wake or timer.
6. On normal completion, an exception or Ctrl+C, perform a final hardware reset and disconnect this API client.

**One write is not necessarily one physical transmission.** Without ACK, PCAN can automatically retransmit within the bounded TX window. The script does not claim hardware single-shot support or count physical retries. It does not synthesize an ACK. It stops if reset fails rather than progressing to another ID. A driver/disconnect/reset failure can require manual recovery; ESC in PCAN-View is the owner's known way to cancel its transmission. PCAN-View can remain open for observation, but concurrent manual/periodic transmission would invalidate attribution and must be stopped.

Local TX echoes, PCAN status/error frames and RTR records are not considered vehicle wake evidence. Real standard/extended data frames are counted. BSI IDs `432` or `792` are separately flagged when present. A TX echo or successful enqueue is not proof that BSI received or acted on anything.

## Evidence

Every run creates a new, flushed JSONL journal under `logs/wake-scan/`, including selected profile, timing, attempted ID/payload, received data/status/echo records, adapter timestamps, resets, candidate and cleanup. `--output` selects another new journal path. Existing files are never overwritten. Force-killing the process or losing power cannot guarantee Python cleanup; use Ctrl+C for a normal stop.

The script replays sampled vehicle broadcasts as well as supervision/identification messages; they are not qualified control commands. Keep this experiment separate from the gateway's firmware architecture and its currently passive CAN RX process.

Offline cleanup tests:

```powershell
python -m unittest discover -s tests -p test_can_wake_scan.py -v
```

See also [previous bounded live tests](can-wakeup-live-2026-10-28.md) and [wake-up analysis](can-wakeup-2026-10-28.md).

## Verification, 2026-10-28

The hardcoded ID/DLC/payload sets were compared against the first samples in both source captures: all 89 entries match exactly. Offline tests cover one write per ID, reset ordering, candidate stop, active baseline, dirty controller status, write errors, Ctrl+C, reset failure, final disconnect, echo rejection and the C ABI layouts. Preview was also run with all 89 IDs, without hardware access.

The first execution attempt stopped at channel initialization with PCAN status `0x4000000`, before any write. An independent channel query reported condition `0x2` (owned by another API client). That zero-TX attempt is recorded in [`20261028T120733.415090Z-all.jsonl`](../logs/wake-scan/20261028T120733.415090Z-all.jsonl). The full scan has not yet run.
