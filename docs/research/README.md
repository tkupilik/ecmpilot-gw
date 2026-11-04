# Capture evidence

These are supporting decoder investigations, not current operating instructions.
Some conclusions were tentative when written. Current request behavior, verified
observations and open questions are consolidated in [CAN requests](../can-requests.md).

| Evidence | Subject |
| --- | --- |
| [Initial INFO analysis](info-can-2026-10-28.md) | Original layout/capture comparison |
| [HSCAN1 analysis](obd-hscan1-2026-10-28.md) | Battery SOC/voltage and source identification |
| [EV layout review](info-ev-review.md) | Contradictory source layouts and unknown fields |
| [Energy search](info-energy-preconditioning.md) | Cloud 19.88 kWh reference; no supported INFO energy decoder |
| [4A4](info-4a4-diagnostics.md) | Unknown diagnostic-looking payload; no validated DTC protocol |
| [Cluster source inventory](cluster-tx.md) | Source-defined messages and attribution limits |
| [Charging review](../info-charging-analysis.md) | 396 HMI feedback and unsuccessful energy search |
| [Range calibration](../range-calibration.md) | Vehicle-specific 2 km/count reference |
| [OBC wake](../info-obc-wake-2026-11-01.md) | Plug-in wake sequence and ID comparison |

Original CSVs, machine-readable evidence and pinned DBC source files are retained.
The detailed annexes support calibration/provenance; they do not supersede the
current request catalogue or establish cloud upload from CAN receipt alone.
