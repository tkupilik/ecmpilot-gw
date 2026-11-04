// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stddef.h>

size_t gw_ha_retired_count(void);
const char *gw_ha_retired_key(size_t index);
const char *gw_ha_retired_component(size_t index);
size_t gw_ha_entity_count(void);
const char *gw_ha_entity_key(size_t index);
const char *gw_ha_entity_component(size_t index);
// Returned JSON uses the cJSON allocator; release with cJSON_free().
char *gw_ha_state_json(void);
char *gw_ha_discovery_json(size_t index, const char *node, const char *state_topic,
                           const char *status_topic, const char *version,
                           const char *serial, unsigned expire_after_s);
