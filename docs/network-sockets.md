# Shared network socket budget

The owner observed GVRET state 1 (listening), zero streamed records and
`gvret.last_error=23` after adding web polling for the temporary UDS tests
(now removed). ESP-IDF 5.3.2's
lwIP `lwip_accept()` returns `ENFILE` (23) when `alloc_socket()` cannot allocate
a descriptor for an accepted connection; it deletes that new connection. This
explains why a TCP connect can succeed but the application sees an immediate
close/reset while the listener task stays alive. It does not by itself prove
a socket leak or identify which browser/client held each descriptor.

The previous configuration allowed only 10 sockets globally and left HTTP at
its default seven clients plus three internal/control sockets. That already
filled the documented capacity before accounting for GVRET, UDP, MQTT or DNS.
HTTP's own LRU purge works within its session limit; it does not reserve
descriptors for the other services.

Current settings: `CONFIG_LWIP_MAX_SOCKETS=16` in both sdkconfig and tracked
sdkconfig.defaults; `WEB_MAX_CLIENTS=4`, HTTP LRU purge enabled. The build checks
that the configured global limit accommodates the following conservative budget:

| Use | Sockets |
| --- | ---: |
| HTTP listening and control (SDK documented maximum) | 3 |
| HTTP clients / browser keep-alive connections | 4 |
| GVRET listener and active client | 2 |
| Phone UDP | 1 |
| MQTT connection | 1 |
| DNS lookup reserve | 1 |
| Temporary accepted/rejected connection reserve | 1 |
| Additional headroom | 3 |
| Total capacity | 16 |

Sockets are shared across tasks and both TCP and UDP. The table is a capacity
budget, not a live count or a claim that all slots allocate full TCP buffers at
startup. `GET /api/status` shows `socket_capacity` and `http_max_clients`; GVRET
still publishes last errno and connection/queue counters in Varstore. Repeated
identical accept errors are logged at most every five seconds; successful accept
clears the error. The 64-frame CAN FIFO and GVRET protocol are unchanged.

For existing firmware, closing extra GW browser tabs/clients and reconnecting
SavvyCAN can free enough sockets. One GVRET client is supported; close other
capture connections before testing. No reset was issued to the owner's GW during
this investigation. The new configuration requires an SDK/lwIP rebuild, not just
relinking old SDK objects. When Ninja cannot run in this environment, use:

```powershell
. ./dev.ps1 -EnvironmentOnly
python tools/build_sequential.py --rebuild-sdk
```

## Web restart

Home's **Restart gateway** button makes one explicit same-origin
`POST /api/restart` with body `{}`. A valid request logs the action and schedules
the existing software restart after 1.5 seconds, allowing the HTTP response to
complete. GET cannot trigger a restart. The button stays disabled after clicking;
an ambiguous network failure is not retried. Reopen the page after reconnecting.
This is a restart, not a settings erase. It uses the existing local-network web
access policy, without adding accounts or authentication.

## Verification

The actual C GVRET service passes 18 host tests including simulated accept
`ENFILE` and subsequent handshake/stream recovery. Fifty-six web tests pass, including
explicit-click restart, duplicate-click suppression and ambiguous-failure behavior.
These host tests do not establish live Wi-Fi behavior with the owner’s GW; the
new firmware still requires installation and an on-device reconnect check.
