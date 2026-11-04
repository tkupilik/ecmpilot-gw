# INFO CAN4A4 diagnostic event review — 2026-11-03

> Capture evidence recorded at the time of this analysis. For current firmware behavior and request status, use [CAN requests](../can-requests.md) and the [documentation index](../README.md).

The pinned PSA-RE definition names4A4 `TELEMATICS_FAULT_EVENT`, describes an
event logged in the **BSI fault log**, and lists BSI as receiver. It does not
establish that a particular ECU physically transmitted these captured frames,
or that their contents were uploaded to Stellantis cloud.

Source: [INFO4A4](https://github.com/prototux/PSA-RE/blob/67f7581e7622410efdbb29c7eda3b86982b9b450/buses/AEE2010.full/LS.INFO-DIV/4A4.yml),
read from the local archive at commit67f7581e7622410efdbb29c7eda3b86982b9b450.
The complete English source, including its alternative, remains in
[the source JSON](../../dbc/opel_corsa_e_info.source.json).

## Two documented layouts

The canonical8-byte layout is a compact event record:

| Wire field (one-based byte, bit7 MSB) | Source meaning |
| --- | --- |
| Byte1 bit3 |0 disappearance,1 appearance |
| Byte1 bit2 |BSI context relevance flag |
| Bytes2..3 |16-bit generic fault code |
| Byte4 bit7 |Local/BSI extended context type |
| Byte4 bits6..0 |Degraded-mode number |
| Bytes5..8 |Four additional context parameters |

However, the same source explicitly supplies an **alternative block format**:

- Byte1 bits7..6: number of frames,1..3;0 invalid.
- Byte1 bits5..4: frame index,1..3;0 invalid.
- Remaining60 bits: an opaque diagnostic block.

There is no documented inner schema/DTC offset for that alternative block.
It is not described as a normal UDS19 response or an ISO-TP transaction.

## Actual captures

Every observed4A4 frame in the reviewed files has one of these two payloads:

```text
99 95 03 13 05 AC 19 B2
A9 09 06 F9 00 11 44 20
```

Under the alternative header,99 means **2 frames, index1** andA9 means
**2 frames, index2**. They alternate, approximately100ms apart within each
pair. This strongly supports the alternative two-frame interpretation, but
does not by itself identify the opaque block's internal fields.

| Capture |4A4 frames |Occurrences of each payload |First..last time relative to capture start |
| --- | ---: | ---: | --- |
| info_buzeni_obc.csv |18 |9 |8.745..12.445s |
| info_precond.csv |18 |9 |7.528..11.227s |
| info_start_odlozeni.csv |18 |9 |103.155..106.855s |
| info_charging.csv |36 |18 |427.189..483.419s |

These are repeated identical payloads, not evidence of18/36 distinct faults.
In the preconditioning capture the entire burst precedes the Start command
at22.389s. The bytes also remain identical across cable insertion, charging and
preconditioning captures, so they are not a newly identified energy/status
response to those individual operations.

## What can be said about DTCs

If the **canonical single-frame interpretation** applied, the first payload's
fault-code word would be9503. If that word uses SAE J2012's usual P/C/B/U
encoding, its textual representation would be **B1503**. Both assumptions
remain unverified for this captured two-frame block. Do not report B1503 as a
confirmed vehicle fault, infer its OEM description, turn the second fragment
into a separate P0906 fault, or assume the following13 is a UDS symptom byte.

[DTC presentation reference](https://uds.readthedocs.io/en/stable/pages/knowledge_base/dtc.html)
describes the standardized representation; it does not define this proprietary
PSA event block. A meaningful validation would compare a contemporaneous BSI
fault-log/ECU diagnostic read with the raw event block, including the ECU identity,
reported DTC format, status and any symptom/context data.

The current project DBC exports the canonical per-frame interpretation. Its
`FaultCodeGeneric` and context fields are **source candidates, not validated DTCs
for these captures**. No DBC/firmware change or live diagnostic request is made
by this review. A future parser should retain raw bytes and frame count/index,
assemble/check pairs, and expose decoded DTCs only after their block layout is
corroborated. Missing/repeated fragments must not be presented as new faults.
