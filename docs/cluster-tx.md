# Instrument cluster transmit candidates on INFO CAN

2026-10-31. Generated inventory from the pinned PSA-RE LS.INFO-DIV export,
revision `67f7581e7622410efdbb29c7eda3b86982b9b450`. `CMB` is the cluster alias;
0004 is the equipment number, not a CAN ID. Selection uses **CMB in transmitters**,
not just a `CMB_` display-name prefix. There are27 IDs:8 with CMB as sole source
and19 with alternatives. This is a generic cross-variant source inventory, not
a claim that every frame must be emitted by this Corsa-e cluster. No TX added.

The DBC first-transmitter prefix is a naming convention. Other senders remain
in message transmitter metadata; receivers are a separate direction. Source
period strings are reproduced literally; `100075ms` is ambiguous, not a validated
100075ms period. ISO-TP DLC8 is a maximum physical frame length, not a guarantee
that every diagnostic response uses8 bytes. Triggered entries have no periodic
send obligation established here.

## CMB as sole listed transmitter

| CAN ID | DBC name | DLC | Source period | Transmitters | Meaning | Frames in four INFO captures |
| --- | --- | --- | --- | --- | --- | --- |
| `257` | `CMB_Odometer` | 6 | trigger | CMB | Stored cluster odometer; source field naming is misleading | 0 |
| `317` | `CMB_Config` | 8 | 10000ms | CMB | Installed warning/telltale configuration | 0 |
| `31F` | `CMB_DisplayLvdsControl` | 3 | trigger | CMB | LVDS frame acknowledgements / retransmission requests | 0 |
| `49F` | `CMB_InstrumentClusterFaultEvent` | 8 | trigger | CMB | Cluster fault event | 0 |
| `51F` | `CMB_LaneDepartureWarningL_c400b5` | 8 | 100075ms | CMB | Network supervision / diagnostic session; misleading lane-warning title | 0 |
| `5DF` | `CMB_Version` | 8 | trigger | CMB | Software/calibration identification | 0 |
| `65F` | `CMB_DiagResponse` | 8 | trigger | CMB | CMB diagnostic response, ISO-TP | 0 |
| `718` | `CMB_DiagResponse_2` | 8 | trigger | CMB | LIN_CMB diagnostic response variant, ISO-TP | 0 |

## CMB among alternative transmitters

| CAN ID | DBC name | DLC | Source period | Transmitters | Meaning | Frames in four INFO captures |
| --- | --- | --- | --- | --- | --- | --- |
| `01C` | `ICSV_MotionSensorCommandRight` | 8 | trigger | ICSV, BTEL, MATT, EMF, OVIP, CMB | Preconditioning/cruise menu requests | 2 |
| `0E9` | `ICSV_NavExportFlowControl` | 8 | trigger | ICSV, CMB | Navigation export ISO-TP flow control | 0 |
| `122` | `CMB_MultiplexedButtonsStatus2` | 8 | 200ms, trigger | CMB, CRT, EMF, FMUX, ICSV | Button panel states and knob counters | 886 |
| `15B` | `EMF_ProfileSettingsScreen` | 8 | trigger | EMF, RAD, MATT, BTEL, CMB, ICSV, OVIP | Vehicle/profile settings, part1 | 0 |
| `19B` | `CMB_SpeedLimiterPresetsStatus` | 7 | trigger | CMB, ICSV, BTEL, OVIP | Speed limiter presets | 0 |
| `1D7` | `CMB_LeftLedHeadlampDriverData` | 1 | 100ms, trigger | CMB, OVIP | Assistance buttons / driving-mode selector; misleading LED-driver title | 0 |
| `1DB` | `CMB_CruiseControlPresetsStatus` | 7 | trigger | CMB, ICSV, BTEL, OVIP | Cruise-control presets | 0 |
| `217` | `CMB_OdometerStatus` | 8 | 100ms, trigger | CMB, BTEL | Cluster status, brightness, buttons, displayed speed | 0 |
| `21B` | `RAD_ProfileSettingsScreen2` | 5 | trigger | RAD, MATT, BTEL, CMB, ICSV, OVIP, EMF | Vehicle/profile settings, part2 | 0 |
| `269` | `MATT_NavExportFlowControl2` | 8 | trigger | MATT, CMB, VTH, ICSV | Navigation export ISO-TP flow control | 0 |
| `297` | `VTH_LedDriverRightLevelli_bf1e84` | 2 | 200ms, trigger | VTH, CMB | HUD state / memory / displayed speed | 0 |
| `2D7` | `MATT_ClusterSettingsStatus` | 3 | 1000ms, trigger | MATT, CMB | Available cluster display fields / configuration | 0 |
| `2DC` | `ICSV_AudioExportFlowControl` | 8 | trigger | ICSV, MATT, CMB | Audio export ISO-TP flow control | 0 |
| `2E7` | `ICSV_DisplayModeRequest` | 8 | trigger | ICSV, CMB | Appearance list, part1 | 0 |
| `31C` | `CMB_TripComputerStatus` | 5 | 200ms, trigger | CMB, MATT | Trip-computer and acknowledgement/reset requests | 0 |
| `39B` | `ICSV_ClockUpdateRequest` | 5 | trigger | ICSV, BTEL, OVIP, CMB, EMF, MATT | Clock setting request | 0 |
| `3A7` | `ICSV_DisplayAppearanceList` | 8 | trigger | ICSV, CMB | Appearance list, part2 | 0 |
| `3E9` | `CMB_GearboxData` | 1 | trigger | CMB, ICSV, MATT | Navigation transfer acknowledgement; misleading gearbox title | 0 |
| `4A4` | `ICSV_TelematicsFaultEvent` | 8 | trigger | ICSV, BTEL, OVIP, CMB | Telematics fault event; sender alternative | 0 |

## Capture evidence

The four `logs/info*.csv` captures contain **886 frames of122 and2 frames of01C**.
No other candidate in this inventory appears. Both observed IDs have several
source sender alternatives, so their presence does not identify the physical
cluster as transmitter. In particular,217/257/317/51F being absent does not
establish that emulation of them is required or that this cluster is faulty.
Physical attribution needs a suitable capture/topology check on the vehicle.

For fields and provenance use `../dbc/opel_corsa_e_info.dbc` and its source JSON.
For firmware reception see [CAN RX](can-rx.md).
