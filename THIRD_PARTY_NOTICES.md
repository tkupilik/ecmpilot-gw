# Third-party notices

The repository's root BSD-2-Clause license applies to new eCMPilot Gateway code; it does not replace licenses on imported code.

| Code | Origin | License / notice |
| --- | --- | --- |
| `components/runtime/include/runtime_process.h`, `ostypes.h`, `src/runtime_process.cpp` | T.Kupilik, 2011; project-local runtime in `opel_dash`, originating from the owner's SourceForge project | BSD-3-Clause; complete notice in each file and [runtime/LICENSE](components/runtime/LICENSE) |
| `components/runtime/include/runtime_signal_store.h`, `src/runtime_signal_store.c` | Project-local `opel_dash` runtime, reuse authorised by the owner | Repository BSD-2-Clause |
| `components/protothreads/include/{pt.h,pt-sem.h,lc.h,lc-switch.h}` | Adam Dunkels; Swedish Institute of Computer Science, 2004–2005; copied from `opel_dash` protothreads | BSD-3-Clause; complete notices in each header and [protothreads/LICENSE](components/protothreads/LICENSE); `pt-sem.h` carries a 2004-only notice |
| `main/can_info_decode.c`, `docs/can-source-reference.json` | INFO-DIV definitions from prototux/PSA-RE revision `67f7581e7622410efdbb29c7eda3b86982b9b450`, adapted through the owner's LCD project to GW varstore | Apache-2.0; [CAN notice](licenses/NOTICE-CAN.txt), [license](licenses/Apache-2.0-PSA-RE.txt) |
| `main/task_can.cpp`, replay integration and `main/gvret*` | Owner's LCD TaskCan architecture, owner capture and independently implemented GVRET; adapted to BSD GW runtime/native varstore | Repository BSD-2-Clause; no upstream SavvyCAN/M2RET implementation copied |
| `main/can_wake.*`, `vehicle_data.*`, `ha_telemetry.*`, `ha_bridge.*` | Independently implemented LCD/GW process policy and telemetry, adapted to native GW varstore; no ufrm code | Repository BSD-2-Clause |
| ESP-IDF 5.3.2 and bundled components | Espressif and upstream contributors; supplied by the developer's SDK installation | Each SDK file/component retains its own license (including Apache-2.0 and other upstream licenses) |

For binary redistribution, include this file, the root LICENSE and both component LICENSE files, `licenses/NOTICE-CAN.txt`, `licenses/Apache-2.0-PSA-RE.txt`, applicable DBC notices, as well as applicable notices from the ESP-IDF components actually linked into the firmware. This source scaffold does not constitute a complete binary redistribution notice bundle for every SDK dependency.

No source from the separate `opel_dash/components/ufrm` framework or GUI is included. CAN acquisition and independent GVRET/replay code are adapted to the existing GW BSD runtime and varstore; INFO definition provenance is retained above. Legacy HS1 DBC provenance remains documented under `dbc/`. The historical runtime filenames `ufrm.h/.cpp` have been renamed; their preserved BSD notices identify exactly which code was ported.

The local `lc-switch.h` port adds an intentional fall-through annotation for modern GCC; the original copyright and license are unchanged.
