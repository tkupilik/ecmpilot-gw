// SPDX-License-Identifier: BSD-2-Clause
// RTOS/HTTP boundary substitutes; the actual task_stats.c serializer is used.
#include "task_stats.h"
#include "freertos/task.h"
#include <string.h>
static char body[32768], status[64], names[3][configMAX_TASK_NAME_LEN];
static int locked, errors, mode;
void prvTakeKernelLock(void) { if (locked) ++errors; locked = 1; }
void prvReleaseKernelLock(void)
{
    if (!locked) ++errors;
    locked = 0;
    // Model names becoming invalid as soon as the snapshot lock is released.
    memset(names, 'x', sizeof(names));
}
TaskHandle_t xTaskGetIdleTaskHandleForCore(BaseType_t core) { return (TaskHandle_t)(uintptr_t)(core + 1); }
TaskHandle_t xTaskGetCurrentTaskHandleForCore(BaseType_t core) { if (!locked) ++errors; return (TaskHandle_t)(uintptr_t)(core + 1); }
UBaseType_t uxTaskGetSystemState(TaskStatus_t *tasks, UBaseType_t capacity, configRUN_TIME_COUNTER_TYPE *time)
{
    if (!locked || capacity != 64) ++errors;
    if (mode == 1) return 0;
    *time = 6000000000ULL;
    strcpy(names[0], "IDLE0"); strcpy(names[1], "IDLE1"); strcpy(names[2], "worker");
    for (unsigned i = 0; i < 3; ++i) tasks[i] = (TaskStatus_t){
        .xHandle=(TaskHandle_t)(uintptr_t)(i+1), .pcTaskName=names[i], .xTaskNumber=i+10,
        .eCurrentState=i==2?eBlocked:eReady, .uxCurrentPriority=4, .uxBasePriority=3,
        .ulRunTimeCounter=5000000000ULL+i, .pxStackBase=(StackType_t *)(uintptr_t)0x3fc90100,
        .usStackHighWaterMark=1536, .xCoreID=i==2?tskNO_AFFINITY:(BaseType_t)i};
    return 3;
}
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *text) { (void)r; if (locked) ++errors; strcpy(status, text); return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *text) { (void)r; (void)text; if (locked) ++errors; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *key, const char *text) { (void)r; if (locked || strcmp(key,"Cache-Control") || strcmp(text,"no-store")) ++errors; return ESP_OK; }
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *text) { (void)r; if (locked) ++errors; strcpy(body,text); return ESP_OK; }
const char *gw_test_tasks(int scenario) { mode=scenario; errors=locked=0; strcpy(status,"200 OK"); body[0]=0; gw_tasks_get(0); return body; }
const char *gw_test_tasks_status(void) { return status; }
int gw_test_tasks_errors(void) { return errors+locked; }
