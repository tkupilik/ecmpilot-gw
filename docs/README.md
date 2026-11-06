# Gateway documentation

Current state: **2026-11-04**. Start with the
[project README](../README.md) for hardware, build and first connection.

## Vehicle and CAN

| Guide | Use it for |
| --- | --- |
| [CAN requests](can-requests.md) | Wake, refresh, charging, preconditioning: exact frames, replies, limits and what is still unknown |
| [Vehicle control](vehicle-control.md) | Protothreads, timers, raw readback and sole TX ownership |
| [CAN architecture](can.md) / [INFO RX mappings](can-rx.md) | Tasks, queues, varstore signals and offline checks |
| [Wiring](hardware/vehicle-wiring.md) | Waveshare, IC91B INFO pair and IC95A power reference |
| [Vehicle low power](../doc/car_lowpower.md) | Sleep, wake refusal, charging telemetry and measured GW current |
| [Automatic sleep](low-power.md) / [GW power modes](power-management.md) | Inactivity countdown, settings and CAN-wake shutdown |
| [INFO DBC](../dbc/README-info.md) / [HSCAN1 DBC](../dbc/README.md) | Layouts, source licensing and confidence |
| [HSCAN1 telemetry](can-telemetry.md) | Alternative 500 kbit/s listen-only profile; not the current INFO default |

## Features and interfaces

| Guide | Use it for |
| --- | --- |
| [Automatic refresh](auto-refresh.md) / [charge limit](charge-limit.md) | Persistent GW automation and auxiliary-battery guard |
| [Preconditioning](preconditioning.md) / [weekly programs](preconditioning-programs.md) | HVAC state and program encoding |
| [Vehicle details](vehicle-details.md) | Doors, locks, trips and consumption |
| [Phone UDP](udp.md) / [correlation](udp-correlation.md) | Protocol and duplicate suppression |
| [Home Assistant](ha.md) / [HA commands](home-assistant/commands.md) | MQTT observations, explicit refresh requests and results |
| [SavvyCAN](gvret.md) | TCP 23, raw RX/local TX export, queue diagnostics |
| [Reset diagnostics](reset-diagnostics.md) / [network sockets](network-sockets.md) | Retained logs, reset causes and connection budget |
| [Testing](testing.md) | Current reproducible checks, without old test-run diaries |

The wire-level request catalogue is maintained in **can-requests.md**. Other
pages describe their feature or implementation rather than defining another
version of the request protocol. Vehicle observations, implemented behavior and
source-only hypotheses must remain distinguishable.

## Supporting evidence

[Capture research](research/README.md), the short calibration/charging/OBC
annexes and adjacent JSON reports retain decoder evidence. They are not operating
instructions. Original logs, pinned source definitions and licensing remain in
the repository. Dated verification JSON files are snapshots of particular test
runs, not a claim that the present firmware has been tested in every condition.

Superseded wake experiments, LCD-port diaries and cumulative verification notes
have been removed from the working documentation; their history remains in Git.
