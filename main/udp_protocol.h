// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stddef.h>
#include <stdint.h>
#define GW_UDP_PORT 42840
#define GW_UDP_MAX_BYTES 1024
#define GW_UDP_COMMAND_CAPACITY 256
#ifdef __cplusplus
extern "C" {
#endif
// Single owner: UDP task on CPU0. No sockets or ESP APIs in the codec.
void gw_udp_protocol_init(const char *gateway, const char *boot, const char *firmware);
void gw_udp_protocol_tick(uint64_t now_ms);
// Returns response length, or zero to drop. Output needs MAX_BYTES + 1 bytes.
size_t gw_udp_protocol_handle(const char *request, size_t length, uint64_t now_ms,
                              char response[GW_UDP_MAX_BYTES + 1]);
#ifdef __cplusplus
}
#endif
