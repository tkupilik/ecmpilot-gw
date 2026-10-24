// runtime_process.cpp
//
// Created by T.Kupilik on $Date: $.
//
// Copyright (c) 2011 T.Kupilik
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// Neither the name of the project's author nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Microframework for user applications.

// -----------------------------------------------------------------------------

#include "runtime_process.h"
#include <climits>

// ----------------------------------------------------------------------------
// namespace declaration

namespace Rntm {

static portMUX_TYPE s_timerMux = portMUX_INITIALIZER_UNLOCKED;

static TickType_t PeriodUsToTicks(uint32_t period)
{
    TickType_t ticks = static_cast<TickType_t>((static_cast<uint64_t>(period) * configTICK_RATE_HZ + 999999U) / 1000000U);
    return ticks == 0 ? 1 : ticks;
}

// ----------------------------------------------------------------------------
// Process

Process::Process() throw()
: m_cfg(),
  m_task(nullptr),
  m_running(false)
{
}

Process::~Process() throw()
{
}

esp_err_t Process::Start(const char *name, UBaseType_t priority,
                         StackType_t *stack, uint32_t stackSize, BaseType_t core)
{
    Config cfg = {};

    cfg.name = name;
    cfg.priority = priority;
    cfg.stack = stack;
    cfg.stackSize = stackSize;
    cfg.core = core;

    return Start(cfg);
}

esp_err_t Process::Start(const Config &cfg)
{
    if (cfg.name == nullptr || cfg.stackSize == 0 || m_task != nullptr)
        return ESP_ERR_INVALID_ARG;

    m_cfg = cfg;

    BaseType_t core = cfg.core;
    if (core != 0 && core != 1)
        return ESP_ERR_INVALID_ARG;

    if (cfg.stack != nullptr) {
        m_task = xTaskCreateStaticPinnedToCore(Process::TaskMain, cfg.name,
                                              cfg.stackSize,
                                              this, cfg.priority, cfg.stack,
                                              &m_staticTask, core);
        return m_task != nullptr ? ESP_OK : ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(Process::TaskMain, cfg.name, cfg.stackSize,
                                            this, cfg.priority, &m_task, core);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

int Process::SendSig(int sig)
{
    if (m_task == nullptr || sig < 0 || sig >= 32)
        return -1;

    return xTaskNotify(m_task, 1UL << sig, eSetBits) == pdPASS ? 0 : -1;
}

int Process::SendSigFromIsr(int sig, BaseType_t *yield)
{
    if (m_task == nullptr || sig < 0 || sig >= 32)
        return -1;

    BaseType_t localYield = pdFALSE;
    BaseType_t *yieldPtr = yield != nullptr ? yield : &localYield;

    return xTaskNotifyFromISR(m_task, 1UL << sig, eSetBits, yieldPtr) == pdPASS ? 0 : -1;
}

void Process::TaskMain(void *par)
{
    Process *proc = static_cast<Process *>(par);
    bool end = false;

    proc->m_running = true;

    if (proc->m_cfg.init != nullptr) {
        if (proc->m_cfg.init(proc, proc->m_cfg.ctx) != 0)
            end = true;
    } else if (proc->Init() != 0) {
        end = true;
    }

    while (!end) {
        uint32_t flags = 0;

        xTaskNotifyWait(0, 0xffffffffUL, &flags, portMAX_DELAY);

        while (flags != 0) {
            int sig = __builtin_ctz(flags);
            flags &= ~(1UL << sig);

            int res;
            if (proc->m_cfg.signal != nullptr)
                res = proc->m_cfg.signal(proc, proc->m_cfg.ctx, sig);
            else
                res = proc->Main(sig);

            if (res != 0) {
                end = true;
                break;
            }
        }
    }

    if (proc->m_cfg.terminate != nullptr)
        proc->m_cfg.terminate(proc, proc->m_cfg.ctx);
    else
        proc->Terminate();
    proc->m_running = false;
    proc->m_task = nullptr;
    vTaskDelete(nullptr);
}

int Process::Main(int sig)
{
    return sig == SIG_KILL ? 1 : 0;
}

// -----------------------------------------------------------------------------
// Constructor

Timer::Timer(Process * proc)
{
    m_proc = proc;
    m_sig = 0;
    m_ticks = 0;
    m_timer = nullptr;
}

// -----------------------------------------------------------------------------
// Set up and start SW timer.

void Timer::Set(int signal, uint32_t period)
{
    configASSERT(signal >= 0 && signal < 32);
    portENTER_CRITICAL(&s_timerMux);
    m_sig = signal;
    m_ticks = 0;
    portEXIT_CRITICAL(&s_timerMux);

    if (m_timer == nullptr) {
        m_timer = xTimerCreateStatic("runtimeTimer", PeriodUsToTicks(period),
                                     pdTRUE, this, Timer::OnTimer,
                                     &m_staticTimer);
    } else {
        const BaseType_t changed = xTimerChangePeriod(m_timer, PeriodUsToTicks(period), portMAX_DELAY);
        ESP_ERROR_CHECK(changed == pdPASS ? ESP_OK : ESP_FAIL);
    }

    ESP_ERROR_CHECK(m_timer != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
    const BaseType_t started = xTimerStart(m_timer, portMAX_DELAY);
    ESP_ERROR_CHECK(started == pdPASS ? ESP_OK : ESP_FAIL);
}

int Timer::GetTicks()
{
    portENTER_CRITICAL(&s_timerMux);
    int ticks = m_ticks;
    m_ticks = 0;
    portEXIT_CRITICAL(&s_timerMux);
    return ticks;
}

// ----------------------------------------------------------------------------
// Timer's expiration handler.

void Timer::OnTimer(TimerHandle_t timer)
{
    Timer * tmr = static_cast<Timer *>(pvTimerGetTimerID(timer));

    int ticks;
    int sig;
    
    // Check if there is already a message in the event queue.
    portENTER_CRITICAL(&s_timerMux);
    ticks = tmr->m_ticks;
    if (tmr->m_ticks < INT_MAX) ++tmr->m_ticks;
    sig = tmr->m_sig;
    portEXIT_CRITICAL(&s_timerMux);

    if (ticks)
        return;        
    
    // Send signal to process.
    if (tmr->m_proc)
        tmr->m_proc->SendSig(sig);
}

// ----------------------------------------------------------------------------
// Start timer.

void TmrQue::Start(Tqn & tqn, uint32_t ticks)
{
    if (ticks == 0)
        return;

    Stop(tqn);

    Rntm::OsList<Node>::Node *tmr = m_list.Head();

    // Find node where to place this timer.
    while (tmr != m_list.Tail() && tmr->val.ticks < ticks)
    {
        ticks -= tmr->val.ticks;
        tmr = tmr->next;
    }

    tmr->AppendBefore(tqn);
    tqn.val.ticks = ticks;

    // Update ticks of next item (if any)
    if (tmr != m_list.Tail())
        tmr->val.ticks -= ticks;
}

// ----------------------------------------------------------------------------
// Stop timer.

void TmrQue::Stop(Tqn & tqn)
{
    if (!tqn.IsRunning())
        return;

    // Update tick to go of next item if exists
    if (tqn.next != m_list.Tail())
        tqn.next->val.ticks += tqn.val.ticks;

    tqn.Remove();
}

// -----------------------------------------------------------------------------
// Periodic tick handler.

void TmrQue::OnTick(uint32_t ticks)
{
    while (!m_list.IsEmpty())
    {
        Rntm::OsList<Node>::Node *tmr = m_list.Head();

        // End if time has not elapsed yet.
        if (ticks < tmr->val.ticks)
        {
            tmr->val.ticks -= ticks;
            return;
        }

        // time elapsed, remove timer from list and call timer's handler
        ticks -= tmr->val.ticks;
        tmr->Remove();
        if (tmr->val.hnd != nullptr)
            (*tmr->val.hnd)(tmr->val.par);
    }
}

// -----------------------------------------------------------------------------
// end of namespace

} // namespace Rntm

// -----------------------------------------------------------------------------
