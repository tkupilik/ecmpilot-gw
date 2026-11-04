// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "esp_err.h"
#ifdef __cplusplus
#include "runtime.h"
#include "can_wake.h"
#include "vehicle_control.h"

class TaskCan;
// RX network layer, owned and called only by TaskCan on CPU1.
class CanNet
{
public:
    CanNet();
    void Open(TaskCan *proc);
    void Close();
    void OnTick();
    void OnCanMsg();
private:
    TaskCan *m_proc;
    int64_t m_healthAt;
    uint32_t m_controlTx = 0;
    uint32_t m_wakeAttempts = 0, m_wakeSubmitted = 0;
};
extern "C" {
#endif
esp_err_t TaskCanStart(void);
#ifdef __cplusplus
}
#endif
