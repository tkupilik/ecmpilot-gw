// runtime_process.h
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

#ifndef RUNTIME_PROCESS_H_
#define RUNTIME_PROCESS_H_

// ----------------------------------------------------------------------------

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "ostypes.h"

// ----------------------------------------------------------------------------
// namespace declaration

namespace Rntm {

// ----------------------------------------------------------------------------

/// Application process class.

class Process
{
public:
    /// Signal codes. User signals should be greater than SIG_USER.
    enum {
        SIG_KILL    = 1,
        SIG_USER    = 10,
    };

    typedef int (*InitFn)(Process *proc, void *ctx);
    typedef int (*SignalFn)(Process *proc, void *ctx, int sig);
    typedef int (*TerminateFn)(Process *proc, void *ctx);

    struct Config
    {
        const char *name;
        void *ctx;
        InitFn init;
        SignalFn signal;
        TerminateFn terminate;
        UBaseType_t priority;
        StackType_t *stack;
        uint32_t stackSize;
        BaseType_t core;
    };

public:
    /// Constructor.
    Process() throw();

    /// Destructor.
    virtual ~Process() throw();

    /// Start process as a FreeRTOS task.
    /// @param name       Task name
    /// @param priority   FreeRTOS priority
    /// @param stack      Optional static stack buffer
    /// @param stackSize  Static stack buffer size [bytes] or dynamic stack size [bytes]
    /// @param core       Explicit ESP-IDF core id (0 or 1).
    esp_err_t Start(const char *name, UBaseType_t priority,
                    StackType_t *stack = nullptr, uint32_t stackSize = 4096,
                    BaseType_t core = 1);

    /// Start process using callback configuration.
    esp_err_t Start(const Config &cfg);
    
    /// Send signal to process.
    /// @retval 0 Success
    /// @retval -1 Invalid signal or process is not running
    int SendSig(int sig);

    /// Send signal to process from ISR.
    int SendSigFromIsr(int sig, BaseType_t *yield = nullptr);

    /// Get task handle.
    TaskHandle_t Task() const { return m_task; }

    /// Returns true when task is running inside Process::TaskMain().
    bool IsRunning() const { return m_running; }
    
    /// FreeRTOS task entry point.
    static void TaskMain(void *par);

    /// User hooks.
    virtual int Init() {return 0;}
    virtual int Main(int sig);
    virtual int Terminate() {return 0;}

protected:
    Config m_cfg;
    TaskHandle_t m_task;
    StaticTask_t m_staticTask;
    bool m_running;
};

// -----------------------------------------------------------------------------

class Timer
{
public:
    /// Constructor.
    Timer(Process * proc);

    /// Set up and start SW timer.
    /// @param signal   Signal to send when timer period elapses.
    /// @param period   Expiration period [us]
    void Set(int signal, uint32_t period);

    /// Get ticks of last timer signal.
    int GetTicks();

protected:
    /// Timer's expiration handler.
    static void OnTimer(TimerHandle_t timer);

protected:
    Process    *m_proc;
    int         m_sig;
    int         m_ticks;
    StaticTimer_t m_staticTimer;
    TimerHandle_t m_timer;
};

// -----------------------------------------------------------------------------

class TmrQue
{
public:
    /// Definition of timer's expiration handler.
    typedef void (*Hnd)(void * par);

protected:
    struct Node
    {
        uint32_t    ticks;              ///< Ticks to go (time left).
        Hnd         hnd;                ///< Callback handler.
        void       *par;                ///< Parameter of handler.

        // Default constructor.
        Node()
        : ticks((uint32_t)-1), hnd(NULL), par(NULL)
        {}
    };

public:
        /// Timer queue node.
    class Tqn : public Rntm::OsList<Node>::Node
    {
        friend class TmrQue;
    public:
        Tqn()
        {}

        /// Set up timer queue node.
        /// @param hnd      Expiration callback handler
        /// @param par      Parameter of handler
        void Setup(Hnd hnd, void * par)
        {
            val.ticks = 0;
            val.hnd = hnd;
            val.par = par;
        }

        /// Timer's state.
        /// @retval true - Active timer.
        /// @retval false - Stand-by timer.
        bool IsRunning() const
        {
            return (next != this);
        }
    };

public:
    /// Start timer.
    /// @param hnd      Expiration callback handler
    /// @param period   Expiration period [us]
    void Start(Tqn & tqn, uint32_t ticks);

    /// Stop timer.
    /// Method removes timer from active timer list.
    void Stop(Tqn & tqn);

    /// Periodic tick handler.
    /// Handler is called from kernel each system tick. So timer's resolution
    /// depends on system tick setting.
    /// On some systems, this can be also called from high priority task.
    /// @param u32TickPeriod     Tick's period [us].
    void OnTick(uint32_t ticks);

protected:
    Rntm::OsList<Node> m_list;      ///< List of active timers.
};

// -----------------------------------------------------------------------------
// end of namespace

} // namespace Rntm

// -----------------------------------------------------------------------------

#endif // RUNTIME_PROCESS_H_

// -----------------------------------------------------------------------------
