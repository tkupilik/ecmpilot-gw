# Installed gateway: Waveshare and vehicle wiring

Installation reference for the owner's Opel / PSA Corsa-e P2JO, 2019–2021 wiring family. Updated 2026-11-04. This is a connection summary, not a complete factory pinout or a universal installation guide.

## Board

The project targets **Waveshare ESP32-S3-RS485-CAN**, with one CAN channel and RS485. Firmware CAN TX is **GPIO15**, RX **GPIO16**. See the [manufacturer schematic](https://files.waveshare.com/wiki/ESP32-S3-RS485-CAN/ESP32-S3-RS485-CAN-Schematic.pdf) and [power budget](../../powe_consumption.md).

The stock design has a **TJA1051T/3** high-speed transceiver and isolated interface power. Firmware bitrate selection does not change the physical transceiver. Existing research notes call for checking the INFO physical interface separately; the complete adapter/modification details for the installed prototype have not been recorded here. Verify that interface, polarity and termination before reproducing the installation. ESP GPIOs connect to transceiver logic, never directly to vehicle CAN conductors.

The measured low-power prototype has its isolated DC/DC removed. Its current measurements do not describe an unmodified board; this document does not prescribe an undocumented bypass circuit.

## Connector location and identification

The investigated black and yellow interconnections are beside each other near the driver's left knee, in front of the BSI area. Cavity numbering must be read on the actual connector housing. A wire-entry view and a mating-face view are mirrored; this note intentionally does not invent a face drawing.

| Connector | Factory designation | Identification |
| --- | --- | --- |
| **IC91B** | **22V NR** | Black, 22 cavities; dashboard harness **50 P/B** to body/interior **46 HAB** |
| **IC95A** | **22V JN** | Yellow, 22 cavities; dashboard **50 P/B** to roof **52 PLAF** |
| IC91A | 22V BE | Blue; a different interconnection |

`V` denotes connector ways: **22V means 22 pins**, not a supply voltage. Colour abbreviations used below: `RS` pink, `VI` violet, `BA` white, `VE` green, `VJ` green/yellow, `NR` black, `JN` yellow, `BE` blue.

IC95A identification was cross-checked by the owner against all five consecutive dashboard-side colours: pin 5 pink, 6 beige, 7 violet, 8 grey, 9 green. Another yellow 22-way connector, ICV4B, exists elsewhere; colour alone is not sufficient identification.

## INFO connection: black IC91B

| Pin | Dashboard side | Body/interior side | Network |
| ---: | --- | --- | --- |
| **14** | `9034A`, **pink / RS** | `9034K`, **white / BA** | **ENT INFO** conductor `9034` |
| **15** | `9035A`, **violet / VI** | `9035K`, **green / VE** | **ENT INFO** conductor `9035` |
| 19 | `9038C`, blue / BE | `9038K`, beige / BG | Passenger-compartment CAN; **different network** |
| 20 | `9039C`, violet / VI | `9039K`, blue / BE | Passenger-compartment CAN; **different network** |

Use the **14/15 pair** for this project's INFO profile, **125 kbit/s**. Pins 19/20 are included to prevent confusing the two adjacent network pairs. CAN H/L polarity was not established in the source connector notes; do not assign it solely from `9034`/`9035` or insulation colour. No IC91B ground pin was established.

The INFO pair reaches the hands-free ECU 8240 (26V BE pins 21/22) and the E934/E935 dashboard branches. This is the network used by the cluster 0004, infotainment and telematics research. It is separate from HSCAN1 on OBD 6/14.

## Permanent supply: adjacent yellow IC95A

| Reference | Evidence / role |
| --- | --- |
| **IC95A / 16**, pink conductor | **Owner-reported permanent +12 V connection in the current installation.** The earlier wiring search did not find a factory pin-16 assignment. Its wire code, fuse and complete feed route remain undocumented. |
| IC95A / 1 | Owner saw green; the diagram shows `M8602`, green/yellow on the dashboard side, in an alarm-sensor earth circuit via EM20A. This is a circuit reference, not confirmation of an appropriate gateway ground/current capacity. |
| Gateway GND | Use a separately verified vehicle ground. Do not infer ground from an unoccupied cavity or colour alone. |

The owner's current permanent-supply report supersedes the earlier *suspected 12 V* observation for the installed car, but does not turn it into a factory-confirmed pin assignment for other cars. Check supply voltage in awake/asleep states and protect a new power branch appropriately. Never apply vehicle 12 V to ESP32 3.3 V/GPIO terminals.

## Source trail

This summary transcribes the relevant facts from the locally reviewed **IC91B.md** and **IC95A.md** connector notes in the Opel PSA e-Corsa eP2JO 2019–2021 wiring collection, plus the owner's installation report. Those source notes are dated 2026-10-31. Original service-manual PDF scans are not reproduced here.

- IC91B 14/15: *ENT INFO CAN Interconnection* and *Hands-Free Access and Starting* circuit references.
- IC91B 19/20: *Multiplexed Passenger Compartment CAN Interconnection* and *Seat Belts/Airbags*.
- IC95A identification/console circuits: *Independent Telematic Unit*.
- IC95A pin 1 earth circuit: *Alarm*.
- IC95A pin 16 supply: owner report; no matching factory diagram reference found.

Keep these evidence levels when extending the pin table. Relevant firmware notes: [CAN setup](../can.md), [vehicle sleep](../../doc/car_lowpower.md), [manual power tests](../power-management.md).
