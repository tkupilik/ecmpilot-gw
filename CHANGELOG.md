# Changelog

## 0.1.0 — 2026-10-24

- Create an ESP-IDF 5.3.x ESP32-S3 gateway scaffold based on the `opel_dash` project structure.
- Port the standalone runtime and BSD-licensed protothreads without the separate UFRM framework or GUI.
- Pin network tasks, Wi-Fi, TCP/IP and HTTP to CPU0; application actors, console and runtime timer service to CPU1.
- Add hello/status/Wi-Fi/restart console commands and a protothread heartbeat.
- Persist an ordered list of up to eight known Wi-Fi networks; scan, connect, retry and expose a WPA2 setup AP for configuration/recovery.
- Embed an English web UI for status, profile editing/priority and firmware upload.
- Add two OTA slots, inactive-slot streaming updates, image validation and bootloader rollback with local startup confirmation.
- Add BSD-2-Clause project license, preserved dependency notices, build instructions and hardware verification steps.
- CAN, UDP, MQTT, Ethernet drivers, vehicle control and deep sleep remain future work.
