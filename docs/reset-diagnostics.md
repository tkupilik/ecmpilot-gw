# Reset diagnostics without USB

Home and Log expose the ESP-IDF reset reason/code, including BROWNOUT(9), PANIC,
SOFTWARE and watchdog categories. The Log tab automatically prepends up to the
last 12 application records from the immediately previous boot, before the current
startup/live log. Download log includes both. Each record holds up to 255 text
bytes. Startup log also reports the reason and previous-tail count.

## Retained storage

`main/retained_log.c` uses 6360 bytes of RTC_NOINIT memory, mapped to RTC slow RAM
at 0x50000000 in the checked build. It makes no heap allocation or flash write.
The existing short cross-core live-log critical section serializes publication.
The ordinary 64-record live RAM ring is unchanged.

Two RTC banks rotate at boot: the previous bank stays frozen while startup/live
messages enter the other one. Versioned headers, inverse boot counters, record
checksums and atomic commit words reject torn/corrupted entries. A reset during
publication may lose that slot without accepting partial data. A normal power-on
clears history; only one previous boot is retained. RTC RAM is not guaranteed to
survive a loss of power or a severe brownout, but readable retained data is used
when the SDK reports BROWNOUT. The reason remains independently available.

Capture starts at `app_main`, before other application initialization. It records
normal ESP_LOG output, not ROM panic text or full backtraces. Direct console
printf output and the setup AP password remain outside capture. A full core dump
would need a separate facility.

## Read-only API

- `/api/status`: reset_reason, reset_reason_code, rtc_boot_number, previous_log_entries.
- `/api/log?after=0`: current log plus reset/retention metadata.
- `/api/log?previous=1&after=0`: frozen previous tail, with the current boot identity
  so the browser can detect a restart during archive loading.

## Vehicle check

Upload the new application BIN, reproduce the phone/BSI wake, wait for Wi-Fi to
return and save Download log before repeating. Look for the reset reason and
last wake submission/PowerManagement transition in Previous boot. BROWNOUT is
an electrical reset indication; the log alone cannot locate a voltage drop.
SOFTWARE, PANIC and watchdog reasons distinguish other reset classes without
identifying the offending component by themselves.

The CAN bus-off path requests TWAI recovery rather than restarting ESP. This
change does not alter that behavior or promise recovery from an electrical dip.

Owner comparison, 2026-11-01: GW survives BSI wake without restarting, whereas
the earlier LCD test reported BROWNOUT. This strongly implicates the LCD supply
path, without identifying an individual faulty component. See the
[vehicle test record](../doc/car_lowpower.md#supply-stability-during-bsi-wake).

Host checks exercise actual capture/RTC sources across simulated resets, startup
overwrite, consecutive boots, corruption, interrupted publication, paging and
concurrent writers. The browser is tested for archive loading/restart display.
GW current measurements are recorded in the vehicle low-power document. Actual
reset/retention across a GW reset still needs a device test.
