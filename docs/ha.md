# Home Assistant / MQTT

## Setup

Open the gateway web UI → **Home Assistant**. Set the MQTT broker hostname or
IPv4 address, port (default **1883**), optional username/password, and publication
interval (**1–60 seconds**, default **1 second**). Enable MQTT and save.
It is **disabled by default**. Settings are stored in NVS (`gateway/ha_mqtt`),
survive OTA and reconnect MQTT on CPU0 without restarting the gateway.
The host field accepts a DNS name/IPv4 address, not a URI; transport is local
MQTT over TCP. Old compile-time broker/credential options are replaced by the web.

Status shows disabled, connecting, connected or connection failure. Passwords
are never returned in responses. An empty input preserves the saved password;
**Remove saved password** explicitly clears it. Failed validation/NVS commit
leaves live settings unchanged. Revision checks prevent overwriting another
browser's edits; retrying the same desired settings is idempotent.

## Vehicle observations

There are **55 discovery entities**. The original 24 unique IDs remain: SOC,
range, odometer, available electrical/thermal readings, reported preconditioning,
charging state/mode/cable/power/time, stored charging start, 12 V SOC/voltage,
front selected temperatures, evaporator temperature and demo flag.

Added: BSI state, battery-data age, charging-schedule availability,
preconditioning availability/accepted immediate request, five open closures,
locking state, trip 1/2 average consumption/distance/speed, instantaneous
consumption, six consumption-history samples and four native preconditioning
programs, plus charge-limit enable/target/status. Example program: `07:30 off Mon Wed Thu`; empty slots read
`Not configured`. Times use the vehicle-local clock. These are vehicle reports,
not independent confirmation of physical HVAC output. Trip/consumption fields
retain the same candidate-validation status as the phone and DBC.

Values use effective global varstore observations and the same scaling as UDP
(km, V, A, °C, kW, %, kWh/100km). Last known values stay published when CAN sleeps.
Each sensor has **`age_s`** and **`stale`** attributes (older than 300 seconds).
**Battery data age** uses the original SOC observation. Force is effective-valid
with age zero; releasing it restores the producer value and its original age.
Unknown/invalid/out-of-range values become JSON null; only that entity becomes
unavailable. Unsupported INFO fields such as HV voltage/current or measured cabin
temperature stay unknown. No battery energy/capacity is invented. The stored
charging start stays visible in immediate mode too, matching the phone.

## Topics and availability

Node: `ecmpilot_gw_<12-digit station MAC>`, separate from LCD. Default topics:

- `ecmpilot/<node>/state`: JSON values and an `age_s` object; QoS0, unretained.
  Old retained state is cleared on connect.
- `ecmpilot/<node>/status`: retained online/offline, QoS1, offline last will.
- `homeassistant/<sensor|binary_sensor>/<node>/<key>/config`: retained QoS1
  discovery, stable unique IDs and one device group.

The state/status prefix remains a build-time option, default `ecmpilot`.
Availability combines gateway online with a non-null value. Expiry is
**3 × publication interval + 15 seconds** and detects a missing gateway/MQTT link,
not old vehicle data. See the official [MQTT sensor](https://www.home-assistant.io/integrations/sensor.mqtt/)
and [MQTT binary sensor](https://www.home-assistant.io/integrations/binary_sensor.mqtt/)
contracts used for discovery and attributes.

**GW → HA only**: no subscriptions, command topics, HA controls or physical
commands. MQTT reads/configuration never wake CAN or request refresh.
Independently enabled automatic vehicle refresh retains its own behavior.

## Runtime and verification

MQTT client: CPU0, priority 3, stack 6144. Configuration/publisher: CPU0,
priority 1, stack 6144. No PSRAM. Discovery advances one entity per 100 ms and
retries a full 8192-byte outbox. Publications use nonblocking enqueue, including
`store=true` for QoS0. ESP-MQTT fragments larger payloads over its 1024-byte IO
buffer. Only the publisher owns client lifecycle; HTTP/event callbacks never
stop/start it. `/api/ha` GET/POST uses redacted responses and same-origin POST.

Tests exercise real telemetry/discovery, source ages, Force/release, no CAN
requests, persistence/failure, revisions, password redaction/preservation/removal
and the actual embedded editor. ESP-IDF 5.3.2 compiles/links the real MQTT branch
in the normal firmware. No live broker, CAN transmission or device flash is
part of this verification.
