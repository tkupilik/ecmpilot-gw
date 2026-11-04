// SPDX-License-Identifier: BSD-2-Clause
// Chunked HTTP boundary only; tests execute the actual vars_http.c serializer.
#include "vars_http.h"
#include <stddef.h>
#include <string.h>

static char body[262144];
static size_t used;
static int calls, largest, ended, fail_at;

esp_err_t httpd_resp_send_chunk(httpd_req_t *request, const char *data, int length)
{
    (void)request;
    ++calls;
    if (fail_at == calls) return ESP_FAIL;
    if (!data && length == 0) { ++ended; return ESP_OK; }
    if (!data || length <= 0 || used + (size_t)length >= sizeof(body)) return ESP_FAIL;
    if (length > largest) largest = length;
    memcpy(body + used, data, (size_t)length);
    used += (size_t)length;
    body[used] = 0;
    return ESP_OK;
}
int gw_test_vars(int fail_chunk)
{
    used = 0; calls = largest = ended = 0; fail_at = fail_chunk; body[0] = 0;
    httpd_resp_set_status(NULL, "200 OK");
    return gw_vars_get(NULL);
}
const char *gw_test_vars_body(void) { return body; }
int gw_test_vars_chunks(void) { return calls; }
int gw_test_vars_max_chunk(void) { return largest; }
int gw_test_vars_ended(void) { return ended; }
