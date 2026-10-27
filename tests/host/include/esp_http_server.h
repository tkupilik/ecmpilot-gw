// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "esp_err.h"
typedef struct { int unused; } httpd_req_t;
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
esp_err_t httpd_resp_sendstr(httpd_req_t *, const char *);
