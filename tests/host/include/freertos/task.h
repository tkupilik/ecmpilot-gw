// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef unsigned UBaseType_t;
typedef int BaseType_t;
typedef uint8_t StackType_t;
typedef uint64_t configRUN_TIME_COUNTER_TYPE;
typedef void *TaskHandle_t;
#define configMAX_TASK_NAME_LEN 16
#define tskNO_AFFINITY 0x7fffffff
typedef enum { eRunning, eReady, eBlocked, eSuspended, eDeleted, eInvalid } eTaskState;
typedef struct {
    TaskHandle_t xHandle;
    const char *pcTaskName;
    UBaseType_t xTaskNumber;
    eTaskState eCurrentState;
    UBaseType_t uxCurrentPriority, uxBasePriority;
    configRUN_TIME_COUNTER_TYPE ulRunTimeCounter;
    StackType_t *pxStackBase;
    unsigned usStackHighWaterMark;
    BaseType_t xCoreID;
} TaskStatus_t;
TaskHandle_t xTaskGetIdleTaskHandleForCore(BaseType_t);
TaskHandle_t xTaskGetCurrentTaskHandleForCore(BaseType_t);
UBaseType_t uxTaskGetSystemState(TaskStatus_t *, UBaseType_t, configRUN_TIME_COUNTER_TYPE *);
