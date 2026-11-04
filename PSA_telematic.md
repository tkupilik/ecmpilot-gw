# PSA telematics on INFO CAN

The current reference is **[INFO CAN requests](docs/can-requests.md)**. It covers
wake, data refresh, delayed charging, preconditioning, replies and timeouts.
[Vehicle control](docs/vehicle-control.md) explains the implementation;
[vehicle low power](doc/car_lowpower.md) explains bus availability.

Owner-confirmed, **2026-11-04**: standalone `394 [10]` refresh updates GW/phone
data but not the cloud in the tested scenario. Successfully changing the stored
charging start time through the wake/394/35B sequence **also updates cloud data**,
even without active charging or enabled delay. Preconditioning is also associated
with cloud updates. An unchanged-time write or standalone upload-only command
has not been validated. GW does not transmit periodic telematics status frames.

The former chronological experiment notes have been replaced by those current
references. Original captures and pinned source definitions are retained.
