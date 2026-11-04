// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "esp_http_server.h"

// HTTP server task only. Stream the existing complete /api/vars JSON schema
// using bounded scratch space; no cJSON tree or full response allocation.
esp_err_t gw_vars_get(httpd_req_t *request);
