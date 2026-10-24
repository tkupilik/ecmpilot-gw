# Runtime

Ported from the project-local `components/runtime` in `opel_dash` (working tree based on commit `1211f2f3b4e3b2b5d1ac3fca2470f9c742eafd03`). The owner confirmed this runtime originates from their earlier SourceForge project and may be reused. Original T.Kupilik notices are preserved. This component has no dependency on `components/ufrm`.

- `Rntm::Process`: virtual or callback actors using FreeRTOS notification bits. Signal IDs are 0–31, with `SIG_KILL = 1`, user signals starting at `SIG_USER = 10`. Repeated signals coalesce; use a queue when every message matters, as the network actor does.
- `Rntm::Timer`: periodic FreeRTOS timer notifying an actor, with accumulated tick count. `Set()` is called from a normal actor/task context, not a timer callback. `GetTicks()` drains the count under the same spinlock as the callback.
- `Rntm::TmrQue`: intrusive, relative timer queue. Operate a queue from its owning actor; it does not provide cross-task synchronization.
- `RuntimeSignalStore`: integer values, validity/age, error flags and overrides, protected by a FreeRTOS spinlock. This initial application does not yet publish vehicle signals.
- `ostypes.h`: original intrusive list implementation used by `TmrQue`.

Gateway changes rename historical `ufrm` filenames, require an explicit valid CPU (default CPU1), preserve ESP-IDF's **byte-based** static/dynamic stack size API, avoid overflow in microsecond-to-tick conversion and protect timer tick draining. Actors and their timers must have static/process lifetime; stopping/destroying an actor with an active timer is not supported by this port. Do not copy actor/timer objects. Retain this constraint until lifecycle support is added deliberately.

```cpp
#include "runtime.h"

class VehicleProcess final : public Rntm::Process {
    int Main(int signal) override {
        // Handle user signals here; return nonzero to stop.
        return Process::Main(signal);
    }
};

static VehicleProcess vehicle;
static StackType_t stack[4096 / sizeof(StackType_t)];
// vehicle.Start("vehicle", 5, stack, sizeof(stack), 1);
```

The BSD-3-Clause terms in [LICENSE](LICENSE) apply to the original actor/list files. The signal-store module and new project changes use the repository BSD-2-Clause license, with original notices retained where present.
