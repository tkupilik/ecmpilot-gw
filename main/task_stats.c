// SPDX-License-Identifier: BSD-2-Clause
#include "task_stats.h"
#include "log_buffer.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_private/freertos_idf_additions_priv.h"

#if !CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS || !CONFIG_FREERTOS_USE_TRACE_FACILITY || \
    !CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID || !CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64 || \
    !CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER || CONFIG_FREERTOS_SMP || CONFIG_FREERTOS_UNICORE
#error "Task diagnostics require ESP-IDF dual-core FreeRTOS with trace, core IDs and 64-bit ESP timer stats"
#endif

#define TASK_CAPACITY 64
typedef struct {
    char name[configMAX_TASK_NAME_LEN];
    UBaseType_t id, priority, base_priority;
    eTaskState state;
    BaseType_t core, idle_core;
    uint64_t runtime_us;
    size_t stack_min_free_bytes;
    uintptr_t stack_base;
} task_row_t;

// Owned by the single HTTP server task; never allocated on its stack.
static TaskStatus_t snapshot[TASK_CAPACITY];
static task_row_t rows[TASK_CAPACITY];

static const char *state_name(eTaskState state)
{
    switch (state) {
    case eRunning: return "running";
    case eReady: return "ready";
    case eBlocked: return "blocked";
    case eSuspended: return "suspended";
    case eDeleted: return "deleted";
    default: return "unknown";
    }
}

static esp_err_t error(httpd_req_t *request, const char *status, const char *text)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "text/plain");
    return httpd_resp_sendstr(request, text);
}

esp_err_t gw_tasks_get(httpd_req_t *request)
{
    configRUN_TIME_COUNTER_TYPE sampled_us = 0;
    TaskHandle_t idle[2] = {xTaskGetIdleTaskHandleForCore(0), xTaskGetIdleTaskHandleForCore(1)};
    // ESP-IDF 5.3 private kernel-lock wrappers keep task names alive until
    // copied. uxTaskGetSystemState re-enters this recursive lock internally.
    // No allocation/JSON/network/formatting inside the lock. Stack watermark
    // scanning is performed by the RTOS snapshot API itself.
    prvTakeKernelLock();
    UBaseType_t count = uxTaskGetSystemState(snapshot, TASK_CAPACITY, &sampled_us);
    TaskHandle_t running[2] = {xTaskGetCurrentTaskHandleForCore(0), xTaskGetCurrentTaskHandleForCore(1)};
    for (UBaseType_t i = 0; i < count; ++i) {
        const TaskStatus_t *s = &snapshot[i];
        task_row_t *row = &rows[i];
        memcpy(row->name, s->pcTaskName, sizeof(row->name));
        row->name[sizeof(row->name) - 1] = 0;
        row->id = s->xTaskNumber;
        row->priority = s->uxCurrentPriority;
        row->base_priority = s->uxBasePriority;
        // The SDK snapshot marks only its caller as running. Correct the
        // other core's current task while the same kernel lock is held.
        row->state = s->xHandle == running[0] || s->xHandle == running[1] ? eRunning : s->eCurrentState;
        row->core = s->xCoreID;
        row->idle_core = s->xHandle == idle[0] ? 0 : s->xHandle == idle[1] ? 1 : -1;
        row->runtime_us = s->ulRunTimeCounter;
        row->stack_min_free_bytes = (size_t)s->usStackHighWaterMark * sizeof(StackType_t);
        row->stack_base = (uintptr_t)s->pxStackBase;
    }
    prvReleaseKernelLock();
    if (!count) return error(request, "503 Service Unavailable", "Task snapshot capacity exceeded (64 tasks)");

    cJSON *json = cJSON_CreateObject();
    cJSON *tasks = cJSON_CreateArray();
    if (!json || !tasks) { cJSON_Delete(json); cJSON_Delete(tasks); return error(request, "500 Internal Server Error", "Out of memory"); }
    if (!cJSON_AddItemToObject(json, "tasks", tasks)) {
        cJSON_Delete(tasks); cJSON_Delete(json);
        return error(request, "500 Internal Server Error", "Out of memory");
    }
    bool ok = true;
    ok = ok && cJSON_AddStringToObject(json, "boot", gw_log_boot_id()) &&
        cJSON_AddNumberToObject(json, "sampled_us", (double)sampled_us) &&
        cJSON_AddNumberToObject(json, "cores", 2) &&
        cJSON_AddNumberToObject(json, "capacity", TASK_CAPACITY);
    for (UBaseType_t i = 0; ok && i < count; ++i) {
        const task_row_t *row = &rows[i];
        char address[2 + sizeof(uintptr_t) * 2 + 1];
        snprintf(address, sizeof(address), "0x%08lx", (unsigned long)row->stack_base);
        cJSON *item = cJSON_CreateObject();
        if (!item) { ok = false; break; }
        ok = cJSON_AddNumberToObject(item, "id", row->id) &&
            cJSON_AddStringToObject(item, "name", row->name) &&
            cJSON_AddStringToObject(item, "state", state_name(row->state)) &&
            cJSON_AddNumberToObject(item, "priority", row->priority) &&
            cJSON_AddNumberToObject(item, "base_priority", row->base_priority) &&
            (row->core == tskNO_AFFINITY ? cJSON_AddNullToObject(item, "core") : cJSON_AddNumberToObject(item, "core", row->core)) &&
            (row->idle_core < 0 ? cJSON_AddNullToObject(item, "idle_core") : cJSON_AddNumberToObject(item, "idle_core", row->idle_core)) &&
            cJSON_AddNumberToObject(item, "runtime_us", (double)row->runtime_us) &&
            cJSON_AddNumberToObject(item, "stack_min_free_bytes", row->stack_min_free_bytes) &&
            cJSON_AddStringToObject(item, "stack_base", address);
        if (ok) ok = cJSON_AddItemToArray(tasks, item);
        if (!ok) cJSON_Delete(item);
    }
    char *text = ok ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    if (!text) return error(request, "500 Internal Server Error", "Out of memory");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_sendstr(request, text);
    free(text);
    return result;
}
