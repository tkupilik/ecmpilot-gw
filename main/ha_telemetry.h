// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define GW_HA_DIAGNOSTICS_PERIOD_S 60U
#define GW_HA_DIAGNOSTICS_ACTIVE_PERIOD_S 1U
#define GW_HA_BSI_ACTIVE_MS 2000U
#define GW_HA_HEARTBEAT_PERIOD_S 15U

// Publisher-owned last successful enqueue; independent of observation values.
bool gw_ha_diagnostics_due(uint64_t now_ms, uint64_t last_sent_ms, bool sent);
size_t gw_ha_retired_count(void);
const char *gw_ha_retired_key(size_t index);
const char *gw_ha_retired_component(size_t index);
size_t gw_ha_entity_count(void);
const char *gw_ha_entity_key(size_t index);
const char *gw_ha_entity_component(size_t index);
// Returned JSON uses the cJSON allocator; release with cJSON_free().
char *gw_ha_state_json(void);
char *gw_ha_diagnostics_json(void);
char *gw_ha_unavailable_json(void);
char *gw_ha_discovery_json(size_t index, const char *node, const char *state_topic,
                           const char *status_topic, const char *version,
                           const char *serial, unsigned expire_after_s);
