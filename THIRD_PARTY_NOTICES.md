# Third-party notices

The repository's root BSD-2-Clause license applies to new eCMPilot Gateway code; it does not replace licenses on imported code.

| Code | Origin | License / notice |
| --- | --- | --- |
| `components/runtime/include/runtime_process.h`, `ostypes.h`, `src/runtime_process.cpp` | T.Kupilik, 2011; project-local runtime in `opel_dash`, originating from the owner's SourceForge project | BSD-3-Clause; complete notice in each file and [runtime/LICENSE](components/runtime/LICENSE) |
| `components/runtime/include/runtime_signal_store.h`, `src/runtime_signal_store.c` | Project-local `opel_dash` runtime, reuse authorised by the owner | Repository BSD-2-Clause |
| `components/protothreads/include/{pt.h,pt-sem.h,lc.h,lc-switch.h}` | Adam Dunkels; Swedish Institute of Computer Science, 2004–2005; copied from `opel_dash` protothreads | BSD-3-Clause; complete notices in each header and [protothreads/LICENSE](components/protothreads/LICENSE); `pt-sem.h` carries a 2004-only notice |
| ESP-IDF 5.3.2 and bundled components | Espressif and upstream contributors; supplied by the developer's SDK installation | Each SDK file/component retains its own license (including Apache-2.0 and other upstream licenses) |

For binary redistribution, include this file, the root LICENSE and both component LICENSE files, as well as applicable notices from the ESP-IDF components actually linked into the firmware. This source scaffold does not constitute a complete binary redistribution notice bundle for every SDK dependency.

No source from the separate `opel_dash/components/ufrm` framework, CAN/application managers or GUI is included. The historical runtime filenames `ufrm.h/.cpp` have been renamed; their preserved BSD notices identify exactly which code was ported.

The local `lc-switch.h` port adds an intentional fall-through annotation for modern GCC; the original copyright and license are unchanged.
