// SPDX-License-Identifier: BSD-2-Clause
// Backport of the LCD TaskCan / CanNet RX architecture to the GW runtime/varstore.
#include "can_config.h"
#include "can_pins.h"
#include "task_can.h"
#include "gateway.h"
#include "can_rx_data.h"
#include "can_decode.h"
#include "process_data.h"
#include "gvret.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <inttypes.h>
#include <limits.h>
#include "driver/twai.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"

// -----------------------------------------------------------------------------

namespace {
const char *const TAG = "task_can";
constexpr gpio_num_t TX_GPIO = GW_CAN_TX_GPIO;
constexpr gpio_num_t RX_GPIO = GW_CAN_RX_GPIO;
constexpr unsigned RX_BATCH = 64;
}

// -----------------------------------------------------------------------------
// CAN process. No GUI/framework or control consumers are called from RX.

class TaskCan final : public Rntm::Process
{
    friend class CanNet;
public:
    enum { SIG_TMR = SIG_USER + 1, SIG_CAN = SIG_USER + 2 };
    TaskCan();
    int Init() override;
    int Main(int sig) override;
    int Terminate() override;
    static void CanAlertTask(void *par);
private:
    esp_err_t OpenCan();
    esp_err_t CloseCan();
    esp_err_t StartAlerts();
    bool PowerStep();
    void OnAlerts();
    void PublishHealth();
    Rntm::Timer m_timer;
    CanNet m_net;
    twai_handle_t m_driver;
    TaskHandle_t m_alertTask;
    std::atomic<bool> m_alertStop, m_alertStopped;
    std::atomic<uint32_t> m_alerts;
    gw_can_health_t m_health;
    bool m_sleepRequested = false;
};

static TaskCan s_canTask;
static StackType_t s_canStack[4096 / sizeof(StackType_t)];

// -----------------------------------------------------------------------------
// Constructor.

TaskCan::TaskCan()
: m_timer(this), m_driver(nullptr), m_alertTask(nullptr),
  m_alertStop(false), m_alertStopped(true), m_alerts(0), m_health{}
{
}

// -----------------------------------------------------------------------------
// Initialize TWAI/interrupts on CPU1, then service notifications in this process.

int TaskCan::Init()
{
    configASSERT(xPortGetCoreID() == GW_APP_CORE);
    m_net.Open(this);
    varstore_write(GW_VAR_CAN_SUSPENDED, 0);
    varstore_write(GW_VAR_CAN_SUSPEND_ERROR, ESP_OK);
    varstore_write(GW_VAR_CAN_SUSPEND_COMPLETED, 0);
    m_health.state = GW_CAN_DISABLED;
#if CONFIG_GW_CAN_RX_ENABLED
    m_health.error = OpenCan();
    m_health.state = m_health.error == ESP_OK ? GW_CAN_LISTENING : GW_CAN_FAULT;
    if (m_health.error != ESP_OK)
        ESP_LOGE(TAG, "CAN unavailable: %s; network diagnostics remain available", esp_err_to_name(m_health.error));
    else
        ESP_LOGI(TAG, "%s: %d bit/s, %s, TX15/RX16, CPU1",
                 GW_CAN_IS_INFO ? "ENT INFO" : "HSCAN1", CONFIG_GW_CAN_BITRATE,
                 GW_CAN_IS_INFO ? "normal; configurable wake; no version TX" : "listen-only; no CAN TX");
#endif
    PublishHealth();
    m_timer.Set(SIG_TMR, 10000); // 10 ms fallback; timer callback only sends a signal.
    return 0;
}

// -----------------------------------------------------------------------------
// Process signal handler.

int TaskCan::Main(int sig)
{
    configASSERT(xPortGetCoreID() == GW_APP_CORE);
    if (sig == SIG_TMR) m_timer.GetTicks();
    if (PowerStep()) return 0;
    switch (sig) {
    case SIG_TMR:
        OnAlerts();
        m_net.OnCanMsg();
        m_net.OnTick();
        return 0;
    case SIG_CAN:
        OnAlerts();
        m_net.OnCanMsg();
        return 0;
    default:
        return Process::Main(sig);
    }
}

// -----------------------------------------------------------------------------
// Process termination.

int TaskCan::Terminate()
{
    esp_err_t error = CloseCan();
    m_net.Close();
    return error;
}

// -----------------------------------------------------------------------------
// Install the Waveshare TWAI receiver; v2 handle stays private to this process.

esp_err_t TaskCan::OpenCan()
{
    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO, RX_GPIO,
        GW_CAN_IS_INFO ? TWAI_MODE_NORMAL : TWAI_MODE_LISTEN_ONLY);
    general.tx_queue_len = 0;
    general.rx_queue_len = 128;
    general.intr_flags = ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM;
    general.alerts_enabled = TWAI_ALERT_RX_DATA | TWAI_ALERT_RX_QUEUE_FULL |
        TWAI_ALERT_RX_FIFO_OVERRUN | TWAI_ALERT_TX_FAILED | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;
    twai_timing_config_t timing{};
    switch (CONFIG_GW_CAN_BITRATE) {
    case 125000: timing = TWAI_TIMING_CONFIG_125KBITS(); break;
    case 250000: timing = TWAI_TIMING_CONFIG_250KBITS(); break;
    case 500000: timing = TWAI_TIMING_CONFIG_500KBITS(); break;
    case 1000000: timing = TWAI_TIMING_CONFIG_1MBITS(); break;
    default: return ESP_ERR_INVALID_ARG;
    }
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t error = twai_driver_install_v2(&general, &timing, &filter, &m_driver);
    if (error != ESP_OK) return error;
    error = twai_start_v2(m_driver);
    if (error != ESP_OK) {
        twai_driver_uninstall_v2(m_driver); m_driver = nullptr;
        return error;
    }
    error = StartAlerts();
    if (error != ESP_OK) CloseCan();
    return error;
}

// -----------------------------------------------------------------------------

esp_err_t TaskCan::StartAlerts()
{
    if (!m_alertStopped.load()) return ESP_OK;
    m_alertStop.store(false);
    m_alertStopped.store(false);
    if (xTaskCreatePinnedToCore(CanAlertTask, "canAlert", 3072, this,
            tskIDLE_PRIORITY + 6, &m_alertTask, GW_APP_CORE) != pdPASS) {
        m_alertStopped.store(true);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Stop the alert reader before uninstalling its TWAI handle.

esp_err_t TaskCan::CloseCan()
{
    m_alertStop.store(true);
    int64_t deadline = esp_timer_get_time() + 1000000;
    while (!m_alertStopped.load()) {
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    m_alertTask = nullptr;
    if (m_driver) {
        twai_status_info_t status{};
        esp_err_t error = twai_get_status_info_v2(m_driver, &status);
        if (error != ESP_OK) return error;
        if (status.state == TWAI_STATE_RUNNING) {
            error = twai_stop_v2(m_driver);
            if (error != ESP_OK) return error;
        }
        // Uninstall releases the PM lock. STOP alone is insufficient in IDF5.3.
        error = twai_driver_uninstall_v2(m_driver);
        if (error != ESP_OK) return error;
        m_driver = nullptr;
        // Disconnect the powered-down TWAI output from TXD immediately. GPIO
        // direction alone does not remove the peripheral's matrix routing.
        error = gpio_reset_pin(TX_GPIO);
        if (error == ESP_OK) error = gpio_set_level(TX_GPIO, 1);
        if (error == ESP_OK) error = gpio_set_direction(TX_GPIO, GPIO_MODE_OUTPUT);
        if (error != ESP_OK) return error;
    }
    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Only the owner suspends TWAI. Application coordination crosses raw varstore.

bool TaskCan::PowerStep()
{
    const auto request = varstore_inspect(GW_VAR_REQUEST_CAN_SUSPEND, 0);
    bool suspend = request.raw_valid && request.raw_value == 1;
    if (suspend != m_sleepRequested) {
        esp_err_t error = ESP_OK;
        m_sleepRequested = suspend;
        if (suspend) {
            error = CloseCan();
            m_alerts.store(0);
            m_health.state = error == ESP_OK ? GW_CAN_STOPPED : GW_CAN_FAULT;
        } else {
            if (!m_driver) error = OpenCan();
            else {
                twai_status_info_t status{};
                error = twai_get_status_info_v2(m_driver, &status);
                if (error == ESP_OK && status.state == TWAI_STATE_STOPPED)
                    error = twai_start_v2(m_driver);
                if (error == ESP_OK) error = StartAlerts();
            }
            m_health.state = error == ESP_OK ? GW_CAN_LISTENING : GW_CAN_FAULT;
        }
        m_health.error = error;
        varstore_write(GW_VAR_CAN_STATE, m_health.state);
        varstore_write(GW_VAR_CAN_LAST_ERROR, error);
        varstore_write(GW_VAR_CAN_SUSPEND_ERROR, error);
        varstore_write(GW_VAR_CAN_SUSPENDED, m_driver ? 0 : 1);
        varstore_write(GW_VAR_CAN_SUSPEND_COMPLETED, suspend);
        ESP_LOGI(TAG, "CAN power %s: %s", suspend ? "suspend/uninstall" : "resume", esp_err_to_name(error));
    }
    // Do not run RX/TX/recovery against an uninstalled or suspended driver.
    return suspend || !m_driver;
}

// -----------------------------------------------------------------------------
// Alert waiter does not decode, control the vehicle or access network sockets.

void TaskCan::CanAlertTask(void *par)
{
    auto *proc = static_cast<TaskCan *>(par);
    configASSERT(xPortGetCoreID() == GW_APP_CORE);
    while (!proc->m_alertStop.load()) {
        uint32_t alerts = 0;
        if (twai_read_alerts_v2(proc->m_driver, &alerts, pdMS_TO_TICKS(100)) == ESP_OK && alerts) {
            proc->m_alerts.fetch_or(alerts);
            proc->SendSig(SIG_CAN);
        }
    }
    proc->m_alertStopped.store(true);
    vTaskDelete(nullptr);
}

// -----------------------------------------------------------------------------
// Handle controller state changes on the owning process only.

void TaskCan::OnAlerts()
{
    uint32_t alerts = m_alerts.exchange(0);
    if (!m_driver) return;
    if (alerts & TWAI_ALERT_BUS_OFF) {
        ESP_LOGW(TAG, "CAN bus-off; initiating recovery");
        m_health.error = twai_initiate_recovery_v2(m_driver);
    }
    if (alerts & TWAI_ALERT_BUS_RECOVERED) {
        m_health.error = twai_start_v2(m_driver);
        ESP_LOGI(TAG, "CAN recovery: %s", esp_err_to_name(m_health.error));
    }
    if (alerts & TWAI_ALERT_ERR_PASS) ESP_LOGW(TAG, "CAN error-passive");
    if (alerts & (TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_RX_FIFO_OVERRUN))
        ESP_LOGW(TAG, "CAN RX overflow");
}

// -----------------------------------------------------------------------------
// Publish controller counters through the native GW varstore.

void TaskCan::PublishHealth()
{
    if (m_driver) {
        twai_status_info_t status{};
        esp_err_t error = twai_get_status_info_v2(m_driver, &status);
        if (error == ESP_OK) {
            switch (status.state) {
            case TWAI_STATE_RUNNING: m_health.state = GW_CAN_LISTENING; break;
            case TWAI_STATE_STOPPED: m_health.state = GW_CAN_STOPPED; break;
            case TWAI_STATE_BUS_OFF: m_health.state = GW_CAN_BUS_OFF; break;
            case TWAI_STATE_RECOVERING: m_health.state = GW_CAN_RECOVERING; break;
            }
            m_health.missed = status.rx_missed_count;
            m_health.overrun = status.rx_overrun_count;
            m_health.bus_errors = status.bus_error_count;
            m_health.rx_errors = status.rx_error_counter;
            m_health.pending = status.msgs_to_rx;
        } else { m_health.state = GW_CAN_FAULT; m_health.error = error; }
    }
    gw_can_rx_publish_health(&m_health);
}

// -----------------------------------------------------------------------------
// Start process, with the LCD stack's CPU1 affinity and priorities.

extern "C" esp_err_t TaskCanStart(void)
{
    return s_canTask.Start("CanTask", tskIDLE_PRIORITY + 5, s_canStack, sizeof(s_canStack), GW_APP_CORE);
}
extern "C" esp_err_t gw_can_rx_start(void) { return TaskCanStart(); }

// -----------------------------------------------------------------------------
// CAN network layer.

CanNet::CanNet() : m_proc(nullptr), m_healthAt(0) {}
void CanNet::Open(TaskCan *proc)
{
    m_proc = proc;
    gw_can_rx_data_init();
    varstore_write(GW_VAR_CAN_WAKE_TX_ATTEMPTS, 0);
    varstore_write(GW_VAR_CAN_WAKE_TX_SUBMITTED, 0);
    varstore_write(GW_VAR_CAN_WAKE_TX_LAST_ERROR, 0);
}
void CanNet::Close()
{
    gw_can_decode_reset();
    varstore_write(GW_VAR_CAN_STATE, GW_CAN_STOPPED);
    m_proc = nullptr;
}
void CanNet::OnTick()
{
    int64_t now = esp_timer_get_time();
    gw_can_rx_tick(static_cast<uint32_t>(now / 1000));
    if (m_proc->m_driver) {
        twai_message_t message{};
        if (gw_vehicle_control_prepare(&m_controlTx, &message.identifier, &message.data_length_code, message.data)) {
            // Capture the committed context before any driver/log call. Both
            // processes use CPU1; CanTask outranks its application producer.
            const auto trace_word = [](size_t id) -> uint32_t {
                const auto value = varstore_inspect(id, 0);
                return value.raw_valid ? (uint32_t)value.raw_value : 0;
            };
            char scope[44];
            const uint32_t source = trace_word(GW_VAR_CONTROL_SOURCE);
            std::snprintf(scope, sizeof(scope), "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "/%" PRIu32,
                trace_word(GW_VAR_CONTROL_SESSION0), trace_word(GW_VAR_CONTROL_SESSION1),
                trace_word(GW_VAR_CONTROL_SESSION2), trace_word(GW_VAR_CONTROL_SESSION3), trace_word(GW_VAR_CONTROL_CORRELATION));
            message.flags = TWAI_MSG_FLAG_SS;
            esp_err_t error = twai_transmit_v2(m_proc->m_driver, &message, 0);
            // Local TX observation, not an ACK/completion indication. Both RX
            // and TX taps run in TaskCan; the FIFO producer remains single-task.
            if (error == ESP_OK)
                gw_gvret_capture(message.identifier, message.data, message.data_length_code,
                                 message.extd, message.rtr, static_cast<uint64_t>(esp_timer_get_time()));
            bool wake = message.identifier == CAN_WAKE_ID || message.identifier == CAN_WAKE_ALT_ID;
            if (wake) {
                if (m_wakeAttempts < INT32_MAX) ++m_wakeAttempts;
                varstore_write(GW_VAR_CAN_WAKE_TX_ATTEMPTS, m_wakeAttempts);
                varstore_write(GW_VAR_CAN_WAKE_TX_LAST_ERROR, error);
                if (error == ESP_OK && m_wakeSubmitted < INT32_MAX) ++m_wakeSubmitted;
                varstore_write(GW_VAR_CAN_WAKE_TX_SUBMITTED, m_wakeSubmitted);
            }
            char payload[24]{};
            for (unsigned i = 0; i < message.data_length_code; ++i)
                std::snprintf(payload + i * 3, sizeof(payload) - i * 3, "%02X%s",
                              message.data[i], i + 1 == message.data_length_code ? "" : " ");
            ESP_LOGI(TAG, "corr=%s source=%" PRIu32 " %s TX: 0x%03" PRIX32 ", DLC%u, bytes=%s, submit=%s, tx=%lu",
                     scope, source, wake ? "BSI wake" : "Vehicle", message.identifier, (unsigned)message.data_length_code,
                     payload, esp_err_to_name(error), (unsigned long)m_controlTx);
            gw_vehicle_control_submitted(m_controlTx, error);
        }
    }
    if (now >= m_healthAt) { m_proc->PublishHealth(); m_healthAt = now + 500000; }
}
void CanNet::OnCanMsg()
{
    if (!m_proc->m_driver) return;
    for (unsigned n = 0; n < RX_BATCH; ++n) {
        twai_message_t message{};
        esp_err_t error = twai_receive_v2(m_proc->m_driver, &message, 0);
        if (error == ESP_ERR_TIMEOUT) break;
        if (error != ESP_OK) { m_proc->m_health.error = error; break; }
        // Raw physical copies are independent of decoder and local replay mode.
        gw_gvret_capture(message.identifier, message.data, message.data_length_code,
                         message.extd, message.rtr, static_cast<uint64_t>(esp_timer_get_time()));
        gw_can_frame_t frame{};
        frame.id = message.identifier; frame.dlc = message.data_length_code;
        frame.extended = message.extd; frame.rtr = message.rtr;
        std::memcpy(frame.data, message.data, sizeof(frame.data));
        gw_can_rx_accept(&frame);
    }
}

// All wake/command transmission is handled by the single OnTick mailbox above.
