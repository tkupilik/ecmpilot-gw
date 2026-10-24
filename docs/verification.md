# Initial scaffold verification — 2026-10-24

Target: ESP32-S3. SDK: the workstation's ESP-IDF 5.3.2 installation, reported as `v5.3.2-dirty`; Xtensa GCC 13.2.0, esptool 4.8.1. Project version: 0.1.0.

## Completed

- CMake configured the reduced component graph, containing `runtime` and `protothreads`, without the separate `ufrm`, LVGL, `esp_lcd`, MQTT or Ethernet components.
- All seven application/runtime C/C++ translation units compiled with the generated ESP-IDF compiler arguments. The application linked and esptool generated a valid ESP32-S3 image; checksum and validation hash passed `image_info`.
- The bootloader compiled, linked and generated its image with application rollback enabled.
- The actual generated configuration was checked: main on CPU1, Wi-Fi and TCP/IP on CPU0, FreeRTOS timer service on CPU1. HTTP/REPL/network/application task affinities are additionally explicit in source.
- The generated binary partition table was decoded and checked for two application slots at `0x20000` and `0x210000`, each `0x1f0000` bytes. The initial OTA data file is 8192 bytes.
- All six tests in `node tests/web-ui.test.cjs` passed. These cover page logic through a minimal DOM/HTTP harness; they are not real-browser or device integration tests.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/ecmpilot_gw.bin` | 872576 | `c46367d0d972a4433bda0f5f000a044c285fb3de7c0e62ee20f1e287c14390df` |
| `build/bootloader/bootloader.bin` | 21568 | `455e91651c8e766054508239f788a5a4986ed2a5f87d0a25ba528bd22634fd3c` |
| `build/partition_table/partition-table.bin` | 3072 | `16f78deb46fa168ea3f8dcfa64e369e3cbae7489f54a6a362ffb8f3d3a1ae529` |

The application leaves 57% free in either OTA slot. Build artifacts are local and ignored by Git.

## Build-environment limitation

The normal `idf.py`/Ninja process driver encountered Windows named-pipe permission failures in the agent's restricted execution environment. The initial build attempt compiled the SDK objects but also encountered a compiler crash in an unused LCD component, which was subsequently excluded by restricting the project component graph.

Verification was completed by configuring CMake with the already-established cross-compiler/pthread probe results, then executing its generated compiler/archive/link/image commands sequentially with inherited output. Existing SDK objects were reused; application/runtime sources were recompiled after the final changes. Bootloader commands were also executed directly. This confirms the final source compilation, linking, binary generation and image size; it is **not** a claim that a normal clean `idf.py build` completed in that restricted session. Standard ESP-IDF build instructions remain the supported workflow in a normal developer shell. No SDK sources were patched to achieve the CPU split.

## Still requiring a board

No flashing or live hardware tests were performed. Native USB enumeration, real CPU task placement, Wi-Fi scanning/failover, setup AP reachability, web requests against the ESP HTTP server, interrupted OTA, boot rollback, watchdog behaviour and power consumption remain unverified. Follow [the board checks](testing.md) before treating the scaffold as device-tested.
