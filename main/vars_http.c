// SPDX-License-Identifier: BSD-2-Clause
#include "vars_http.h"
#include "process_data.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    httpd_req_t *request;
    esp_err_t error;
    size_t used;
    char buffer[1024];
} json_stream_t;

static void flush(json_stream_t *stream)
{
    if (stream->error == ESP_OK && stream->used) {
        stream->error = httpd_resp_send_chunk(stream->request, stream->buffer, (int)stream->used);
        stream->used = 0;
    }
}
static void bytes(json_stream_t *stream, const char *text, size_t length)
{
    while (stream->error == ESP_OK && length) {
        size_t take = sizeof(stream->buffer) - stream->used;
        if (take > length) take = length;
        memcpy(stream->buffer + stream->used, text, take);
        stream->used += take;
        text += take;
        length -= take;
        if (stream->used == sizeof(stream->buffer)) flush(stream);
    }
}
static void text(json_stream_t *stream, const char *value)
{
    bytes(stream, value, strlen(value));
}
static void quoted(json_stream_t *stream, const char *value)
{
    static const char hex[] = "0123456789abcdef";
    text(stream, "\"");
    for (const unsigned char *p = (const unsigned char *)(value ? value : "");
         *p && stream->error == ESP_OK; ++p) {
        if (*p == '"' || *p == '\\') {
            const char escaped[] = {'\\', (char)*p};
            bytes(stream, escaped, sizeof(escaped));
        } else if (*p < 0x20) {
            const char escaped[] = {'\\', 'u', '0', '0', hex[*p >> 4], hex[*p & 15]};
            bytes(stream, escaped, sizeof(escaped));
        } else bytes(stream, (const char *)p, 1); // Preserve UTF-8 metadata.
    }
    text(stream, "\"");
}
static void number_or_null(json_stream_t *stream, bool present, long value)
{
    if (!present) { text(stream, "null"); return; }
    char buffer[24];
    const int length = snprintf(buffer, sizeof(buffer), "%ld", value);
    if (length < 0 || (size_t)length >= sizeof(buffer)) { stream->error = ESP_FAIL; return; }
    bytes(stream, buffer, (size_t)length);
}
static void age_or_null(json_stream_t *stream, bool present, uint32_t age)
{
    if (!present) { text(stream, "null"); return; }
    char buffer[24];
    const int length = snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)age);
    if (length < 0 || (size_t)length >= sizeof(buffer)) { stream->error = ESP_FAIL; return; }
    bytes(stream, buffer, (size_t)length);
}

esp_err_t gw_vars_get(httpd_req_t *request)
{
    esp_err_t error = httpd_resp_set_type(request, "application/json");
    if (error != ESP_OK) return error;
    error = httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (error != ESP_OK) return error;
    json_stream_t stream = {.request = request, .error = ESP_OK};
    const size_t count = varstore_count();
    text(&stream, "{\"signals\":[");
    for (size_t id = 0; id < count && stream.error == ESP_OK; ++id) {
        const varstore_definition_t *definition = varstore_definition(id);
        const varstore_snapshot_t sample = varstore_inspect(id, definition->max_age_ms);
        char prefix[64];
        const int length = snprintf(prefix, sizeof(prefix), "%s{\"id\":%lu,\"name\":", id ? "," : "", (unsigned long)id);
        if (length < 0 || (size_t)length >= sizeof(prefix)) { stream.error = ESP_FAIL; break; }
        bytes(&stream, prefix, (size_t)length);
        quoted(&stream, definition->name);
        text(&stream, ",\"description\":"); quoted(&stream, definition->description);
        text(&stream, ",\"unit\":"); quoted(&stream, definition->unit);
        text(&stream, ",\"max_age_ms\":"); age_or_null(&stream, true, definition->max_age_ms);
        text(&stream, ",\"writable\":"); text(&stream, gw_process_data_manual(id) ? "true" : "false");
        text(&stream, ",\"state\":"); quoted(&stream, varstore_state_name(sample.state));
        text(&stream, ",\"valid\":"); text(&stream, sample.state != VARSTORE_INVALID ? "true" : "false");
        text(&stream, ",\"forced\":"); text(&stream, sample.state == VARSTORE_FORCED ? "true" : "false");
        text(&stream, ",\"value\":"); number_or_null(&stream, sample.has_value, (long)sample.value);
        text(&stream, ",\"age_ms\":"); age_or_null(&stream, sample.has_value, sample.age_ms);
        text(&stream, ",\"raw_value\":"); number_or_null(&stream, sample.raw_has_value, (long)sample.raw_value);
        text(&stream, ",\"raw_age_ms\":"); age_or_null(&stream, sample.raw_has_value, sample.raw_age_ms);
        text(&stream, ",\"raw_valid\":"); text(&stream, sample.raw_valid ? "true" : "false");
        text(&stream, "}");
    }
    text(&stream, "]}");
    flush(&stream);
    // A failed chunk must abort the HTTP response, never complete partial JSON.
    if (stream.error != ESP_OK) return stream.error;
    return httpd_resp_send_chunk(request, NULL, 0);
}
