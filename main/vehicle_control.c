// SPDX-License-Identifier: BSD-2-Clause
#include "vehicle_control.h"
#include "auto_refresh.h"
#include "can_config.h"
#include "can_wake.h"
#include "process_data.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "pt.h"
#include <limits.h>
#include <string.h>

// One CPU1 process schedules this hierarchy. Parents suspended in PT_SPAWN
// share c.tmr with their child; no two timed subthreads run concurrently.
static uint64_t last_tick;
static uint32_t tx_generation, wake_requests, background_correlation;
static struct {
    struct pt request_pt, wake_pt, refresh_pt, charge_pt, precond_pt, tx_pt;
    uint64_t now, refresh_at;
    uint32_t request, correlation, session[4], tmr, operation_tmr, settle_sequence;
    uint32_t soc_baseline, schedule_baseline, mode_baseline;
    unsigned wake_attempts, refresh_attempts, mode_attempts, precond_attempts;
    int kind, minute, phase, result;
    bool active, automatic, limited, wake_ok, refresh_ok, charge_ok, need_refresh, mode_payload_valid;
    uint32_t limit_revision;
    bool precond_ok, precond_pair_submitted;
    uint32_t programs12_hi, programs12_lo, programs34_hi, programs34_lo, precond_baseline;
    uint64_t precond_at;
    uint32_t program_base[4], program_target[4], programs34_baseline;
    bool programs_valid;
    uint16_t mode_payload;
    struct {
        uint32_t id, generation, wait_ms;
        uint16_t payload;
        uint32_t data_hi24, data_lo24;
        uint8_t length;
        esp_err_t error;
        uint64_t at;
        bool ok;
    } tx;
} c;

// Independent parallel timer context. Starting a vehicle operation clears c,
// but must not reset this countdown or its protothread continuation.
static struct {
    struct pt pt;
    gw_auto_refresh_config_t config;
    uint32_t refresh_tmr, requests;
    bool armed, counting;
} automatic;

static struct {
    struct pt pt;
    uint32_t revision, seen_soc, retry_tmr, requests;
} limiter;

#define source_name() (c.limited ? "charge_limit" : c.automatic ? "timer" : "phone")

// Physical automatic decisions use actual, recent CAN samples. Force remains
// useful for UI diagnostics but cannot fabricate a reason to stop charging.
static bool limit_data(const gw_auto_refresh_config_t *settings)
{
    varstore_snapshot_t soc = varstore_inspect(GW_VAR_SOC_PCT_X10, 5000);
    varstore_snapshot_t charge = varstore_inspect(GW_VAR_CHARGE_STATE, 5000);
    varstore_snapshot_t hmi = varstore_inspect(GW_VAR_BSI_CHARGING_HMI_STATE, 5000);
    return soc.raw_valid && soc.raw_value >= (int32_t)settings->charge_limit_soc_pct_x10 &&
        soc.raw_value <= 1000 && charge.raw_valid && charge.raw_value == 2 &&
        hmi.raw_valid && hmi.raw_value == 1;
}

static bool limit_permitted(bool require_data)
{
    gw_auto_refresh_config_t settings;
    return gw_auto_refresh_config(&settings) && settings.enabled && settings.charge_limit_enabled &&
        settings.revision == c.limit_revision && (!require_data || limit_data(&settings));
}

static uint32_t refresh_period(void)
{
    return (varstore_read(GW_VAR_CHARGE_STATE, 0, -1) == 2 ?
        automatic.config.charging_min : automatic.config.standby_min) * 60000U;
}

static uint64_t clock_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static uint32_t next(uint32_t n) { return n == INT32_MAX ? 1 : n + 1; }
static void countdown(uint32_t *value, uint64_t tick)
{
    if (*value > tick) *value -= (uint32_t)tick;
    else *value = 0;
}
static int32_t raw(size_t id, uint32_t age, int32_t fallback)
{
    varstore_snapshot_t s = varstore_inspect(id, age);
    return s.raw_valid ? s.raw_value : fallback;
}
static uint32_t rx_sequence(size_t id) { return (uint32_t)raw(id, 0, 0); }
static bool normal(void)
{
    return rx_sequence(GW_VAR_BSI_STATUS_RX_GENERATION) != 0 &&
        raw(GW_VAR_BSI_POWER_MANAGEMENT, GW_CONTROL_BSI_MAX_AGE_MS, -1) == 1;
}
static bool received(size_t id, size_t generation_id, uint32_t baseline,
                     uint64_t since, int32_t lo, int32_t hi)
{
    varstore_snapshot_t s = varstore_inspect(id, 1000);
    uint32_t seq = rx_sequence(generation_id);
    return seq && seq != baseline && s.raw_valid && s.raw_value >= lo &&
        s.raw_value <= hi && since <= c.now && s.raw_age_ms <= c.now - since;
}
static bool schedule_valid(void)
{
    int32_t value = raw(GW_VAR_BSI_CHARGE_SCHEDULE_RAW, 1000, -1);
    return value >= 0 && (value & 0x8000) && (value & 0x7FF) <= 1439;
}
static bool refresh_reply(void)
{
    if (c.kind == GW_CONTROL_REFRESH)
        return received(GW_VAR_SOC_PCT_X10, GW_VAR_SOC_RX_GENERATION,
                        c.soc_baseline, c.refresh_at, 0, 1000);
    return schedule_valid() && received(GW_VAR_BSI_CHARGE_SCHEDULE_RAW,
        GW_VAR_BSI_SCHEDULE_RX_GENERATION, c.schedule_baseline, c.refresh_at, 0, 65535);
}
static bool mode_reply(void)
{
    return received(GW_VAR_BSI_CHARGE_SCHEDULE_RAW, GW_VAR_BSI_SCHEDULE_RX_GENERATION,
        c.mode_baseline, c.tx.at, 0, 65535) &&
        (raw(GW_VAR_BSI_CHARGE_SCHEDULE_RAW, 1000, -1) & 0xC7FF) == (c.tx.payload & 0xC7FF);
}
bool gw_vehicle_control_available(void)
{
    return GW_CAN_ENABLED && GW_CAN_IS_INFO && CONFIG_GW_CAN_BITRATE == 125000 &&
        !varstore_read(GW_VAR_CAN_REPLAY_ENABLED, 0, 0) &&
        !varstore_read(GW_VAR_TEST_ENABLED, 0, 0) &&
        !varstore_read(GW_VAR_REQUEST_CAN_SUSPEND, 0, 0);
}
static void phase(int value)
{
    if (value != c.phase)
        ESP_LOGI("vehicle", "corr=%08lx%08lx%08lx%08lx/%lu source=%s gen=%lu phase=%d",
            (unsigned long)c.session[0], (unsigned long)c.session[1], (unsigned long)c.session[2], (unsigned long)c.session[3],
            (unsigned long)c.correlation, source_name(), (unsigned long)c.request, value);
    c.phase = value;
    varstore_write(GW_VAR_CONTROL_PHASE, value);
    // Timer work never impersonates an explicit UDP receipt generation.
    varstore_write(GW_VAR_CONTROL_GENERATION, c.automatic ? 0 : c.request);
    varstore_write(GW_VAR_CONTROL_SOURCE, c.limited ? 3 : c.automatic ? 2 : c.request ? 1 : 0);
    varstore_write(GW_VAR_CONTROL_CORRELATION, (int32_t)c.correlation);
    for (unsigned i = 0; i < 4; ++i) varstore_write(GW_VAR_CONTROL_SESSION0 + i, (int32_t)c.session[i]);
}
static void diagnostics(void)
{
    varstore_write(GW_VAR_CONTROL_TIMER_MS, c.tmr);
    varstore_write(GW_VAR_CONTROL_REMAINING_MS, c.operation_tmr);
    varstore_write(GW_VAR_CONTROL_WAKE_ATTEMPTS, c.wake_attempts);
    varstore_write(GW_VAR_CONTROL_REFRESH_ATTEMPTS, c.refresh_attempts);
    varstore_write(GW_VAR_CONTROL_MODE_ATTEMPTS, c.mode_attempts);
    varstore_write(GW_VAR_CONTROL_PRECOND_ATTEMPTS, c.precond_attempts);
    varstore_write(GW_VAR_CONTROL_RESULT, c.result);
}
static void finish(int result)
{
    c.result = result; c.active = false; c.tmr = 0;
    if (c.limited) {
        limiter.seen_soc = rx_sequence(GW_VAR_SOC_RX_GENERATION);
        limiter.retry_tmr = 60000;
        varstore_write(GW_VAR_CHARGE_LIMIT_LAST_RESULT, result);
    } else if (c.automatic) varstore_write(GW_VAR_AUTO_REFRESH_LAST_RESULT, result);
    if ((c.kind == GW_CONTROL_REFRESH || c.limited) && automatic.armed) {
        // A completed sequence (including failure) starts the next interval.
        // Never generate another event while this refresh is still running.
        automatic.refresh_tmr = refresh_period();
        automatic.counting = false;
        varstore_write(GW_VAR_AUTO_REFRESH_REMAINING_MS, automatic.refresh_tmr);
    }
    // Revoke an unconsumed mailbox before publishing a terminal result.
    varstore_write(GW_VAR_CONTROL_TX_PENDING, 0);
    varstore_write(GW_VAR_CAN_WAKE_STATE, gw_vehicle_control_available() ? (normal() ? 1 : 0) : 4);
    bool failed = result == GW_CONTROL_RESULT_CONFIG || result == GW_CONTROL_RESULT_UNAVAILABLE ||
                  result == GW_CONTROL_RESULT_MOVING || result == GW_CONTROL_RESULT_PRECOND_UNAVAILABLE ||
                  result == GW_CONTROL_RESULT_PRECOND_REJECTED || result == GW_CONTROL_RESULT_PROGRAMS_CONFLICT;
    diagnostics(); // Result must be visible before its terminal phase is published.
    phase(result == GW_CONTROL_RESULT_OK ? GW_CONTROL_SUCCEEDED : failed ? GW_CONTROL_FAILED : GW_CONTROL_UNCONFIRMED);
    ESP_LOGI("vehicle", "corr=%08lx%08lx%08lx%08lx/%lu source=%s result=%d wake=%u refresh=%u mode=%u",
        (unsigned long)c.session[0], (unsigned long)c.session[1], (unsigned long)c.session[2], (unsigned long)c.session[3],
        (unsigned long)c.correlation, source_name(), result, c.wake_attempts, c.refresh_attempts, c.mode_attempts);
    diagnostics();
}

/*****************************************************************************/
// Shared transport child: wait only for TaskCan submission, not vehicle ACK.
// It leaves the unspent c.tmr available to its caller (wake cadence).

static PT_THREAD(SubmitFrame(void))
{
    PT_BEGIN(&c.tx_pt);
    (void)PT_YIELD_FLAG;
    c.tx.ok = false; c.tx.error = ESP_ERR_TIMEOUT;
    varstore_write(GW_VAR_CONTROL_TX_PENDING, 0);
    varstore_write(GW_VAR_CONTROL_TX_ID, c.tx.id);
    varstore_write(GW_VAR_CONTROL_TX_LENGTH, c.tx.length);
    varstore_write(GW_VAR_CONTROL_TX_PAYLOAD, c.tx.payload);
    varstore_write(GW_VAR_CONTROL_TX_DATA_HI24, c.tx.data_hi24);
    varstore_write(GW_VAR_CONTROL_TX_DATA_LO24, c.tx.data_lo24);
    c.tx.generation = tx_generation = next(tx_generation);
    varstore_write(GW_VAR_CONTROL_TX_GENERATION, tx_generation);
    varstore_write(GW_VAR_CONTROL_TX_PENDING, 1);
    ESP_LOGI("vehicle", "corr=%08lx%08lx%08lx%08lx/%lu CAN intent=0x%03lx tx=%lu attempt, DLC%u",
        (unsigned long)c.session[0], (unsigned long)c.session[1], (unsigned long)c.session[2], (unsigned long)c.session[3],
        (unsigned long)c.correlation, (unsigned long)c.tx.id, (unsigned long)c.tx.generation, c.tx.length);
    for (c.tmr = c.tx.wait_ms; c.tmr; ) {
        if ((uint32_t)raw(GW_VAR_CONTROL_TX_COMPLETED, 0, 0) == c.tx.generation) {
            c.tx.error = raw(GW_VAR_CONTROL_TX_ERROR, 0, ESP_FAIL);
            c.tx.ok = c.tx.error == ESP_OK;
            c.tx.at = c.now - varstore_inspect(GW_VAR_CONTROL_TX_COMPLETED, 0).raw_age_ms;
            break;
        }
        PT_YIELD(&c.tx_pt);
    }
    varstore_write(GW_VAR_CONTROL_TX_PENDING, 0);
    if (!c.tx.ok) c.result = c.tx.error == ESP_ERR_TIMEOUT ?
        GW_CONTROL_RESULT_TX_TIMEOUT : GW_CONTROL_RESULT_TX_FAILED;
    PT_END(&c.tx_pt);
}

/*****************************************************************************/
// At most ten wake intents across the entire operation, including re-wakes.
// A fresh Normal stops waking; a second Normal during the settle window is
// required before a command. Force cannot manufacture readiness/readback.

static PT_THREAD(WakeBsi(void))
{
    PT_BEGIN(&c.wake_pt);
    (void)PT_YIELD_FLAG;
    c.wake_ok = false;
    phase(GW_CONTROL_WAIT_BSI);
    while (true) {
        if (normal()) {
            c.settle_sequence = rx_sequence(GW_VAR_BSI_STATUS_RX_GENERATION);
            for (c.tmr = GW_CONTROL_SETTLE_MS; c.tmr; ) {
                if (!normal()) break;
                PT_YIELD(&c.wake_pt);
            }
            if (!c.tmr && normal() && c.settle_sequence &&
                rx_sequence(GW_VAR_BSI_STATUS_RX_GENERATION) != c.settle_sequence) {
                c.wake_ok = true;
                PT_EXIT(&c.wake_pt);
            }
        }
        if (c.wake_attempts >= GW_CONTROL_WAKE_MAX) {
            c.result = GW_CONTROL_RESULT_WAKE_TIMEOUT;
            PT_EXIT(&c.wake_pt);
        }
        if (!gw_can_wake_encode(&c.tx.id, &c.tx.payload)) {
            varstore_write(GW_VAR_CAN_WAKE_TX_CONFIG_ERROR, ESP_ERR_INVALID_ARG);
            c.result = GW_CONTROL_RESULT_CONFIG;
            PT_EXIT(&c.wake_pt);
        }
        varstore_write(GW_VAR_CAN_WAKE_TX_CONFIG_ERROR, ESP_OK);
        ++c.wake_attempts;
        wake_requests = next(wake_requests);
        varstore_write(GW_VAR_CAN_WAKE_TX_REQUEST_COUNT, wake_requests);
        varstore_write(GW_VAR_UDP_WAKE_REQUEST_COUNT, wake_requests); // Legacy diagnostic name.
        c.tx.length = 2; c.tx.wait_ms = GW_CONTROL_WAKE_INTERVAL_MS;
        PT_SPAWN(&c.wake_pt, &c.tx_pt, SubmitFrame());
        // Missing ACK/submission is not evidence of a failed wake. Observe BSI.
        for (; c.tmr; ) {
            if (normal()) break;
            PT_YIELD(&c.wake_pt);
        }
    }
    PT_END(&c.wake_pt);
}

/*****************************************************************************/
// Refresh preamble is for charge data only. Preconditioning never emits 394.
// The requested operation selects which vehicle observation is expected.

static PT_THREAD(RefreshVehicle(void))
{
    PT_BEGIN(&c.refresh_pt);
    (void)PT_YIELD_FLAG;
    c.refresh_ok = false;
    while (c.refresh_attempts < GW_CONTROL_REFRESH_MAX) {
        PT_SPAWN(&c.refresh_pt, &c.wake_pt, WakeBsi());
        if (!c.wake_ok) PT_EXIT(&c.refresh_pt);
        ++c.refresh_attempts;
        c.soc_baseline = rx_sequence(GW_VAR_SOC_RX_GENERATION);
        c.schedule_baseline = rx_sequence(GW_VAR_BSI_SCHEDULE_RX_GENERATION);
        c.tx.id = 0x394; c.tx.length = 1; c.tx.payload = 0x10;
        c.tx.wait_ms = GW_CONTROL_TX_TIMEOUT_MS;
        phase(GW_CONTROL_REFRESH_TX);
        PT_SPAWN(&c.refresh_pt, &c.tx_pt, SubmitFrame());
        if (!c.tx.ok) continue;
        c.refresh_at = c.tx.at;
        phase(GW_CONTROL_WAIT_DATA);
        for (c.tmr = GW_CONTROL_REFRESH_TIMEOUT_MS; c.tmr; ) {
            if (refresh_reply()) { c.refresh_ok = true; PT_EXIT(&c.refresh_pt); }
            if (!normal()) break;
            PT_YIELD(&c.refresh_pt);
        }
        c.result = GW_CONTROL_RESULT_REPLY_TIMEOUT;
    }
    PT_END(&c.refresh_pt);
}

/*****************************************************************************/
// Charge-mode sequence.

static PT_THREAD(SetChargeMode(void))
{
    PT_BEGIN(&c.charge_pt);
    (void)PT_YIELD_FLAG;
    c.charge_ok = false; c.need_refresh = true;
    for (c.mode_attempts = 0; c.mode_attempts < GW_CONTROL_MODE_MAX; ) {
        if (c.need_refresh || !normal()) {
            PT_SPAWN(&c.charge_pt, &c.refresh_pt, RefreshVehicle());
            if (!c.refresh_ok) PT_EXIT(&c.charge_pt);
            c.need_refresh = false;
            c.mode_payload_valid = false;
            // Shared countdown preserves >=460 ms from the actual submission.
            for (c.tmr = c.now - c.refresh_at < GW_CONTROL_PREAMBLE_MS ?
                     GW_CONTROL_PREAMBLE_MS - (uint32_t)(c.now - c.refresh_at) : 0; c.tmr; ) {
                if (!normal()) break;
                PT_YIELD(&c.charge_pt);
            }
        }
        if (!normal() || (!c.mode_payload_valid && !schedule_valid())) { c.need_refresh = true; continue; }
        if (c.limited) {
            // A refresh may reveal that charging stopped or SOC is below the
            // target. Never act on the older snapshot that started this PT.
            if (!limit_permitted(true)) { c.result = GW_CONTROL_RESULT_NOT_READY; PT_EXIT(&c.charge_pt); }
            c.minute = raw(GW_VAR_BSI_CHARGE_SCHEDULE_RAW, 1000, 0) & 0x7FF;
        }
        if (raw(GW_VAR_VEHICLE_PARKED, 1000, 1) == 0 || raw(GW_VAR_SPEED_KMH, 1000, 0) > 0) {
            c.result = GW_CONTROL_RESULT_MOVING; PT_EXIT(&c.charge_pt);
        }
        if (!c.mode_payload_valid) {
            c.mode_payload = (uint16_t)raw(GW_VAR_BSI_CHARGE_SCHEDULE_RAW, 1000, 0);
            c.mode_payload = (c.mode_payload & 0xB800) |
                (c.kind == GW_CONTROL_DEFERRED ? 0x4000 | (unsigned)c.minute : c.mode_payload & 0x7FF);
            c.mode_payload_valid = true;
        }
        // A lost response must retry the same target. Do not activate another
        // refresh merely because the readback being waited for is now stale.
        c.tx.payload = c.mode_payload;
        c.mode_baseline = rx_sequence(GW_VAR_BSI_SCHEDULE_RX_GENERATION);
        ++c.mode_attempts;
        c.tx.id = 0x35B; c.tx.length = 2; c.tx.wait_ms = GW_CONTROL_TX_TIMEOUT_MS;
        phase(GW_CONTROL_MODE_TX);
        PT_SPAWN(&c.charge_pt, &c.tx_pt, SubmitFrame());
        if (!c.tx.ok) continue;
        phase(GW_CONTROL_WAIT_MODE);
        for (c.tmr = GW_CONTROL_MODE_TIMEOUT_MS; c.tmr; ) {
            if (mode_reply()) { c.charge_ok = true; PT_EXIT(&c.charge_pt); }
            if (!normal()) { c.need_refresh = true; break; }
            PT_YIELD(&c.charge_pt);
        }
        c.result = GW_CONTROL_RESULT_REPLY_TIMEOUT;
    }
    PT_END(&c.charge_pt);
}

/*****************************************************************************/
// Preserve the actual BSI program bytes. Commit counters are invalid while RX
// publishes, so a reader cannot assemble a partly updated program image.

static bool programs_snapshot(void)
{
    uint32_t a = rx_sequence(GW_VAR_BSI_PRECOND_RX_GENERATION);
    uint32_t b = rx_sequence(GW_VAR_BSI_PRECOND_PROGRAMS34_RX_GENERATION);
    int32_t hi12 = raw(GW_VAR_BSI_PRECOND_PROGRAMS12_HI24, 1000, -1);
    int32_t lo12 = raw(GW_VAR_BSI_PRECOND_PROGRAMS12_LO24, 1000, -1);
    int32_t hi34 = raw(GW_VAR_BSI_PRECOND_PROGRAMS34_HI24, 1000, -1);
    int32_t lo34 = raw(GW_VAR_BSI_PRECOND_PROGRAMS34_LO24, 1000, -1);
    if (!a || !b || raw(GW_VAR_BSI_PRECOND_AVAILABLE, 1000, -1) != 1 ||
        hi12 < 0 || lo12 < 0 || hi34 < 0 || lo34 < 0 ||
        a != rx_sequence(GW_VAR_BSI_PRECOND_RX_GENERATION) ||
        b != rx_sequence(GW_VAR_BSI_PRECOND_PROGRAMS34_RX_GENERATION)) return false;
    c.programs12_hi = hi12; c.programs12_lo = lo12;
    c.programs34_hi = hi34; c.programs34_lo = lo34;
    return true;
}
static bool precond_observation(void)
{
    return received(GW_VAR_BSI_PRECOND_STATUS_RAW, GW_VAR_BSI_PRECOND_RX_GENERATION,
                    c.precond_baseline, c.precond_at, 0, 255);
}
static bool precond_accepted(void)
{
    return precond_observation() && raw(GW_VAR_BSI_PRECOND_AVAILABLE, 1000, -1) == 1 &&
        raw(GW_VAR_BSI_PRECOND_ASAP, 1000, -1) == (c.kind == GW_CONTROL_PRECOND_START);
}
static bool precond_complete(void)
{
    return precond_accepted() && raw(GW_VAR_BSI_PRECOND_FEEDBACK, 1000, -1) ==
        (c.kind == GW_CONTROL_PRECOND_START ? 1 : 0);
}
static bool precond_rejected(void)
{
    int feedback = raw(GW_VAR_BSI_PRECOND_FEEDBACK, 1000, -1);
    return precond_observation() &&
        (raw(GW_VAR_BSI_PRECOND_AVAILABLE, 1000, -1) == 0 || (feedback >= 4 && feedback <= 7));
}

// Updates all four slots atomically at the API level. Preserve reserved bits and
// the independently reported ASAP flag. A retry accepts already applied slots,
// but cannot silently overwrite a different edit made through another client.
static bool program_targets(void)
{
    uint32_t observed[] = {c.programs12_hi, c.programs12_lo, c.programs34_hi, c.programs34_lo};
    for (unsigned i = 0; i < 4; ++i) {
        if (observed[i] != c.program_base[i] && observed[i] != c.program_target[i]) return false;
        observed[i] = (observed[i] & 0x00F800U) | (c.program_target[i] & 0xFF07FFU);
    }
    c.programs12_hi = observed[0]; c.programs12_lo = observed[1];
    c.programs34_hi = observed[2]; c.programs34_lo = observed[3];
    return true;
}
static bool programs_complete(void)
{
    return received(GW_VAR_BSI_PRECOND_PROGRAMS12_HI24, GW_VAR_BSI_PRECOND_RX_GENERATION,
                    c.precond_baseline, c.precond_at, 0, 0xFFFFFF) &&
        received(GW_VAR_BSI_PRECOND_PROGRAMS34_HI24, GW_VAR_BSI_PRECOND_PROGRAMS34_RX_GENERATION,
                 c.programs34_baseline, c.precond_at, 0, 0xFFFFFF) &&
        programs_snapshot() && c.programs12_hi == c.program_target[0] &&
        c.programs12_lo == c.program_target[1] && c.programs34_hi == c.program_target[2] &&
        c.programs34_lo == c.program_target[3];
}

static PT_THREAD(SetPrograms(void))
{
    PT_BEGIN(&c.precond_pt);
    (void)PT_YIELD_FLAG;
    c.precond_ok = false;
    while (c.precond_attempts < GW_CONTROL_PRECOND_MAX) {
        PT_SPAWN(&c.precond_pt, &c.wake_pt, WakeBsi());
        if (!c.wake_ok) PT_EXIT(&c.precond_pt);
        phase(GW_CONTROL_WAIT_PROGRAMS);
        for (c.tmr = GW_CONTROL_PROGRAMS_TIMEOUT_MS; c.tmr; ) {
            if (raw(GW_VAR_BSI_PRECOND_AVAILABLE, 1000, -1) == 0) {
                c.result = GW_CONTROL_RESULT_PRECOND_UNAVAILABLE; PT_EXIT(&c.precond_pt);
            }
            if (programs_snapshot()) break;
            PT_YIELD(&c.precond_pt);
        }
        if (!c.tmr) { c.result = GW_CONTROL_RESULT_PROGRAMS_TIMEOUT; PT_EXIT(&c.precond_pt); }
        if (!normal()) continue;
        if (!program_targets()) { c.result = GW_CONTROL_RESULT_PROGRAMS_CONFLICT; PT_EXIT(&c.precond_pt); }
        if (raw(GW_VAR_VEHICLE_PARKED, 1000, 1) == 0 || raw(GW_VAR_SPEED_KMH, 1000, 0) > 0) {
            c.result = GW_CONTROL_RESULT_MOVING; PT_EXIT(&c.precond_pt);
        }
        ++c.precond_attempts;
        c.precond_baseline = rx_sequence(GW_VAR_BSI_PRECOND_RX_GENERATION);
        c.programs34_baseline = rx_sequence(GW_VAR_BSI_PRECOND_PROGRAMS34_RX_GENERATION);
        c.tx.id = 0x31B; c.tx.length = 7;
        c.tx.payload = 0x80 | (raw(GW_VAR_BSI_PRECOND_ASAP, 1000, 0) ? 0x40 : 0);
        c.tx.data_hi24 = c.programs12_hi; c.tx.data_lo24 = c.programs12_lo;
        c.tx.wait_ms = GW_CONTROL_TX_TIMEOUT_MS;
        phase(GW_CONTROL_PRECOND_TX);
        PT_SPAWN(&c.precond_pt, &c.tx_pt, SubmitFrame());
        if (!c.tx.ok) continue;
        c.tx.id = 0x11B; c.tx.payload = 0;
        c.tx.data_hi24 = c.programs34_hi; c.tx.data_lo24 = c.programs34_lo;
        phase(GW_CONTROL_PRECOND_AUX_TX);
        PT_SPAWN(&c.precond_pt, &c.tx_pt, SubmitFrame());
        if (!c.tx.ok) continue;
        c.precond_at = c.tx.at;
        phase(GW_CONTROL_WAIT_PRECOND);
        for (c.tmr = GW_CONTROL_PRECOND_ACK_MS; c.tmr; ) {
            if (programs_complete()) { c.precond_ok = true; PT_EXIT(&c.precond_pt); }
            if (!normal()) break;
            PT_YIELD(&c.precond_pt);
        }
        c.result = GW_CONTROL_RESULT_REPLY_TIMEOUT;
    }
    for (c.tmr = GW_CONTROL_PRECOND_TIMEOUT_MS; c.tmr; ) {
        if (programs_complete()) { c.precond_ok = true; PT_EXIT(&c.precond_pt); }
        PT_YIELD(&c.precond_pt);
    }
    PT_END(&c.precond_pt);
}

/*****************************************************************************/
// Start/stop preserves all four weekly programs. One request emits 31B then
// 11B, as captured. ASAP acknowledgement alone does not confirm running/stopped.
// All children still share c.tmr; the whole request is bounded to30 seconds.

static PT_THREAD(SetPreconditioning(void))
{
    PT_BEGIN(&c.precond_pt);
    (void)PT_YIELD_FLAG;
    c.precond_ok = false;
    while (c.precond_attempts < GW_CONTROL_PRECOND_MAX) {
        PT_SPAWN(&c.precond_pt, &c.wake_pt, WakeBsi());
        if (!c.wake_ok) PT_EXIT(&c.precond_pt);
        phase(GW_CONTROL_WAIT_PROGRAMS);
        for (c.tmr = GW_CONTROL_PROGRAMS_TIMEOUT_MS; c.tmr; ) {
            if (raw(GW_VAR_BSI_PRECOND_AVAILABLE, 1000, -1) == 0) {
                c.result = GW_CONTROL_RESULT_PRECOND_UNAVAILABLE; PT_EXIT(&c.precond_pt);
            }
            if (programs_snapshot()) break;
            PT_YIELD(&c.precond_pt);
        }
        if (!c.tmr) { c.result = GW_CONTROL_RESULT_PROGRAMS_TIMEOUT; PT_EXIT(&c.precond_pt); }
        if (!normal()) continue;
        if (c.kind == GW_CONTROL_PRECOND_START &&
            (raw(GW_VAR_VEHICLE_PARKED, 1000, 1) == 0 || raw(GW_VAR_SPEED_KMH, 1000, 0) > 0)) {
            c.result = GW_CONTROL_RESULT_MOVING; PT_EXIT(&c.precond_pt);
        }
        ++c.precond_attempts;
        c.precond_baseline = rx_sequence(GW_VAR_BSI_PRECOND_RX_GENERATION);
        c.tx.id = 0x31B; c.tx.length = 7;
        c.tx.payload = c.kind == GW_CONTROL_PRECOND_START ? 0xC0 : 0x80;
        c.tx.data_hi24 = c.programs12_hi; c.tx.data_lo24 = c.programs12_lo;
        c.tx.wait_ms = GW_CONTROL_TX_TIMEOUT_MS;
        phase(GW_CONTROL_PRECOND_TX);
        PT_SPAWN(&c.precond_pt, &c.tx_pt, SubmitFrame());
        if (!c.tx.ok) continue;
        c.tx.id = 0x11B; c.tx.payload = 0;
        c.tx.data_hi24 = c.programs34_hi; c.tx.data_lo24 = c.programs34_lo;
        phase(GW_CONTROL_PRECOND_AUX_TX);
        PT_SPAWN(&c.precond_pt, &c.tx_pt, SubmitFrame());
        if (!c.tx.ok) continue;
        c.precond_at = c.tx.at;
        c.precond_pair_submitted = true;
        phase(GW_CONTROL_WAIT_PRECOND);
        for (c.tmr = GW_CONTROL_PRECOND_ACK_MS; c.tmr; ) {
            if (precond_rejected()) { c.result = GW_CONTROL_RESULT_PRECOND_REJECTED; PT_EXIT(&c.precond_pt); }
            if (precond_accepted()) break;
            if (!normal()) break;
            PT_YIELD(&c.precond_pt);
        }
        if (!normal()) continue;
        if (!c.tmr) { c.result = GW_CONTROL_RESULT_REPLY_TIMEOUT; continue; }
        // BSI has accepted ASAP. Do not repeatedly restart an accepted cycle.
        for (c.tmr = GW_CONTROL_PRECOND_TIMEOUT_MS; c.tmr; ) {
            if (precond_complete()) { c.precond_ok = true; PT_EXIT(&c.precond_pt); }
            if (precond_rejected()) { c.result = GW_CONTROL_RESULT_PRECOND_REJECTED; PT_EXIT(&c.precond_pt); }
            if (!normal()) break;
            PT_YIELD(&c.precond_pt);
        }
        if (normal()) { c.result = GW_CONTROL_RESULT_REPLY_TIMEOUT; PT_EXIT(&c.precond_pt); }
    }
    // After the bounded retry budget, keep listening until the whole30 s
    // deadline. A slow status response can still complete the same request.
    if (c.precond_pair_submitted) {
        phase(GW_CONTROL_WAIT_PRECOND);
        for (c.tmr = GW_CONTROL_PRECOND_TIMEOUT_MS; c.tmr; ) {
            if (precond_complete()) { c.precond_ok = true; PT_EXIT(&c.precond_pt); }
            if (precond_rejected()) { c.result = GW_CONTROL_RESULT_PRECOND_REJECTED; PT_EXIT(&c.precond_pt); }
            PT_YIELD(&c.precond_pt);
        }
    }
    PT_END(&c.precond_pt);
}

static PT_THREAD(RunRequest(void))
{
    PT_BEGIN(&c.request_pt);
    (void)PT_YIELD_FLAG;
    if (c.kind == GW_CONTROL_REFRESH) {
        PT_SPAWN(&c.request_pt, &c.refresh_pt, RefreshVehicle());
        if (c.refresh_ok) c.result = GW_CONTROL_RESULT_OK;
    } else if (c.kind == GW_CONTROL_PRECOND_PROGRAMS) {
        PT_SPAWN(&c.request_pt, &c.precond_pt, SetPrograms());
        if (c.precond_ok) c.result = GW_CONTROL_RESULT_OK;
    } else if (c.kind == GW_CONTROL_PRECOND_START || c.kind == GW_CONTROL_PRECOND_STOP) {
        PT_SPAWN(&c.request_pt, &c.precond_pt, SetPreconditioning());
        if (c.precond_ok) c.result = GW_CONTROL_RESULT_OK;
    } else {
        PT_SPAWN(&c.request_pt, &c.charge_pt, SetChargeMode());
        if (c.charge_ok) c.result = GW_CONTROL_RESULT_OK;
    }
    finish(c.result);
    PT_END(&c.request_pt);
}

/*****************************************************************************/
// A second source for RunRequest, not another CAN transmitter. This PT runs
// every control tick even while RunRequest is yielded in one of its children.

static PT_THREAD(AutoRefresh(void))
{
    PT_BEGIN(&automatic.pt);
    (void)PT_YIELD_FLAG;
    for (;;) {
        int state = GW_AUTO_COUNTING;
        automatic.counting = false;
        if (!gw_auto_refresh_config(&automatic.config)) state = GW_AUTO_INVALID_CONFIG;
        else if (!automatic.config.enabled) {
            state = GW_AUTO_DISABLED; automatic.armed = false; automatic.refresh_tmr = 0;
        } else {
            uint32_t period = refresh_period();
            if (!automatic.armed) { automatic.refresh_tmr = period; automatic.armed = true; }
            // Clamp on every tick: a shorter charging/configuration interval
            // takes effect immediately, without restarting an elapsed timer.
            if (automatic.refresh_tmr > period) automatic.refresh_tmr = period;
            int32_t soc = varstore_read(GW_VAR_BSI_AUX_BATTERY_SOC_PCT_X10, 0, -1);
            if (soc >= 0 && soc <= 1000 && (uint32_t)soc < automatic.config.min_aux_soc_pct_x10)
                state = GW_AUTO_LOW_BATTERY;
            else if (!gw_vehicle_control_available()) state = GW_AUTO_UNAVAILABLE;
            else {
                automatic.counting = !(c.active && c.kind == GW_CONTROL_REFRESH);
                if (c.active && c.kind == GW_CONTROL_REFRESH) state = GW_AUTO_EXECUTING;
                else if (!automatic.refresh_tmr) {
                    // Preserve explicit terminal results until UDP has retained
                    // its receipt. A new explicit request is consumed first.
                    if (c.active || (uint32_t)raw(GW_VAR_CONTROL_RECEIPT_GENERATION, 0, 0) != c.request)
                        state = GW_AUTO_BUSY;
                    else {
                        uint32_t request = c.request;
                        memset(&c, 0, sizeof(c));
                        c.request = request; c.kind = GW_CONTROL_REFRESH; c.automatic = c.active = true;
                        c.now = clock_ms(); c.operation_tmr = GW_CONTROL_OPERATION_TIMEOUT_MS;
                        PT_INIT(&c.request_pt);
                        automatic.requests = next(automatic.requests);
                        background_correlation = next(background_correlation);
                        c.correlation = background_correlation; // Shared background scope, gateway boot.
                        automatic.counting = false;
                        varstore_write(GW_VAR_AUTO_REFRESH_REQUEST_COUNT, automatic.requests);
                        state = GW_AUTO_EXECUTING;
                        ESP_LOGI("vehicle", "Automatic refresh #%lu: %s interval, 12V SOC=%ld (0.1%%; -1 unknown)",
                            (unsigned long)automatic.requests, varstore_read(GW_VAR_CHARGE_STATE, 0, -1) == 2 ? "charging" : "standby", (long)soc);
                    }
                }
            }
        }
        varstore_write(GW_VAR_AUTO_REFRESH_REMAINING_MS, automatic.refresh_tmr);
        varstore_write(GW_VAR_AUTO_REFRESH_STATE, state);
        PT_YIELD(&automatic.pt);
    }
    PT_END(&automatic.pt);
}

/*****************************************************************************/
// Parallel observer, sharing the existing serialized vehicle operation/TaskCan
// path. Explicit requests win. At most one new operation per new SOC sample
// and one minute; each operation retains the normal bounded CAN retries.
static PT_THREAD(ChargeLimit(void))
{
    PT_BEGIN(&limiter.pt);
    (void)PT_YIELD_FLAG;
    for (;;) {
        int state = GW_LIMIT_MONITORING;
        gw_auto_refresh_config_t settings;
        if (!gw_auto_refresh_config(&settings) || !settings.enabled || !settings.charge_limit_enabled) {
            state = GW_LIMIT_DISABLED;
            limiter.seen_soc = limiter.retry_tmr = limiter.revision = 0;
        } else {
            if (limiter.revision != settings.revision) {
                limiter.revision = settings.revision; limiter.seen_soc = limiter.retry_tmr = 0;
            }
            if (!gw_vehicle_control_available()) state = GW_LIMIT_UNAVAILABLE;
            else if (c.active && c.limited) state = GW_LIMIT_REQUESTING;
            else {
                int32_t aux = raw(GW_VAR_BSI_AUX_BATTERY_SOC_PCT_X10, 0, -1);
                if (aux >= 0 && aux <= 1000 && aux < (int32_t)settings.min_aux_soc_pct_x10)
                    state = GW_LIMIT_LOW_BATTERY;
                else if (!limit_data(&settings) || !rx_sequence(GW_VAR_SOC_RX_GENERATION)) {
                    int32_t soc = raw(GW_VAR_SOC_PCT_X10, 5000, -1);
                    int32_t charge = raw(GW_VAR_CHARGE_STATE, 5000, -1);
                    state = soc < 0 || charge < 0 ? GW_LIMIT_WAIT_DATA :
                        soc >= (int32_t)settings.charge_limit_soc_pct_x10 && charge != 2 ? GW_LIMIT_REACHED : GW_LIMIT_MONITORING;
                } else if (limiter.retry_tmr || limiter.seen_soc == rx_sequence(GW_VAR_SOC_RX_GENERATION)) {
                    state = raw(GW_VAR_CHARGE_LIMIT_LAST_RESULT, 0, 0) == GW_CONTROL_RESULT_OK ? GW_LIMIT_REACHED : GW_LIMIT_RETRY;
                } else if (c.active || (uint32_t)raw(GW_VAR_CONTROL_RECEIPT_GENERATION, 0, 0) != c.request) {
                    state = GW_LIMIT_BUSY;
                } else {
                    uint32_t request = c.request;
                    memset(&c, 0, sizeof(c));
                    c.request = request; c.kind = GW_CONTROL_DEFERRED; c.minute = -1;
                    c.automatic = c.limited = c.active = true;
                    c.limit_revision = settings.revision;
                    c.now = clock_ms(); c.operation_tmr = GW_CONTROL_OPERATION_TIMEOUT_MS;
                    PT_INIT(&c.request_pt);
                    limiter.requests = next(limiter.requests);
                    background_correlation = next(background_correlation); c.correlation = background_correlation;
                    limiter.seen_soc = rx_sequence(GW_VAR_SOC_RX_GENERATION);
                    varstore_write(GW_VAR_CHARGE_LIMIT_REQUEST_COUNT, limiter.requests);
                    state = GW_LIMIT_REQUESTING;
                    ESP_LOGI("vehicle", "Charge limit #%lu: SOC=%ld, target=%lu (0.1%%); request delayed charging",
                        (unsigned long)limiter.requests, (long)raw(GW_VAR_SOC_PCT_X10, 5000, -1),
                        (unsigned long)settings.charge_limit_soc_pct_x10);
                }
            }
        }
        varstore_write(GW_VAR_CHARGE_LIMIT_STATE, state);
        PT_YIELD(&limiter.pt);
    }
    PT_END(&limiter.pt);
}

void gw_vehicle_control_init(void)
{
    memset(&c, 0, sizeof(c));
    memset(&automatic, 0, sizeof(automatic)); PT_INIT(&automatic.pt);
    memset(&limiter, 0, sizeof(limiter)); PT_INIT(&limiter.pt);
    tx_generation = wake_requests = background_correlation = 0; last_tick = clock_ms();
    varstore_write(GW_VAR_CONTROL_TX_GENERATION, 0);
    varstore_write(GW_VAR_CONTROL_TX_PENDING, 0);
    varstore_write(GW_VAR_UDP_WAKE_REQUEST_COUNT, 0);
    varstore_write(GW_VAR_CONTROL_GENERATION, 0);
    varstore_write(GW_VAR_AUTO_REFRESH_REMAINING_MS, 0);
    varstore_write(GW_VAR_AUTO_REFRESH_STATE, GW_AUTO_DISABLED);
    varstore_write(GW_VAR_AUTO_REFRESH_REQUEST_COUNT, 0);
    varstore_write(GW_VAR_AUTO_REFRESH_LAST_RESULT, 0);
    varstore_write(GW_VAR_CHARGE_LIMIT_STATE, GW_LIMIT_DISABLED);
    varstore_write(GW_VAR_CHARGE_LIMIT_REQUEST_COUNT, 0);
    varstore_write(GW_VAR_CHARGE_LIMIT_LAST_RESULT, 0);
    phase(GW_CONTROL_IDLE); diagnostics();
}
void gw_vehicle_control_tick(void)
{
    uint64_t now = clock_ms();
    uint64_t tick = now >= last_tick ? now - last_tick : 0;
    last_tick = now;
    countdown(&c.tmr, tick); countdown(&c.operation_tmr, tick);
    countdown(&limiter.retry_tmr, tick);
    if (automatic.counting) countdown(&automatic.refresh_tmr, tick);
    uint32_t incoming = (uint32_t)raw(GW_VAR_REQUEST_VEHICLE_GENERATION, 0, 0);
    if (incoming != c.request) {
        int kind = raw(GW_VAR_REQUEST_VEHICLE_KIND, 0, -1);
        int minute = raw(GW_VAR_REQUEST_VEHICLE_MINUTE, 0, -1);
        uint32_t correlation = (uint32_t)raw(GW_VAR_REQUEST_VEHICLE_CORRELATION, 0, 0), session[4];
        for (unsigned i = 0; i < 4; ++i) session[i] = (uint32_t)raw(GW_VAR_REQUEST_VEHICLE_SESSION0 + i, 0, 0);
        if (incoming != (uint32_t)raw(GW_VAR_REQUEST_VEHICLE_GENERATION, 0, 0)) return;
        uint32_t base[4], target[4]; bool valid = true;
        for (unsigned i = 0; i < 4; ++i) {
            int32_t a = raw(GW_VAR_REQUEST_PRECOND_BASE1 + i, 0, -1);
            int32_t b = raw(GW_VAR_REQUEST_PRECOND_TARGET1 + i, 0, -1);
            if (a < 0 || a > 0xFFFFFF || b < 0 || b > 0xFFFFFF) valid = false;
            base[i] = (uint32_t)a; target[i] = (uint32_t)b;
        }
        if (incoming != (uint32_t)raw(GW_VAR_REQUEST_VEHICLE_GENERATION, 0, 0)) return;
        if (c.active && c.automatic) ESP_LOGI("vehicle", "Automatic refresh superseded by explicit request");
        varstore_write(GW_VAR_CONTROL_TX_PENDING, 0);
        memset(&c, 0, sizeof(c));
        c.request = incoming; c.kind = kind; c.minute = minute;
        c.correlation = correlation; memcpy(c.session, session, sizeof(session));
        c.now = now; c.active = incoming != 0;
        memcpy(c.program_base, base, sizeof(base)); memcpy(c.program_target, target, sizeof(target));
        c.programs_valid = valid;
        c.operation_tmr = kind >= GW_CONTROL_PRECOND_START ?
            GW_CONTROL_PRECOND_TIMEOUT_MS : GW_CONTROL_OPERATION_TIMEOUT_MS;
        PT_INIT(&c.request_pt);
        if (c.active && (kind < GW_CONTROL_REFRESH || kind > GW_CONTROL_PRECOND_PROGRAMS ||
            (kind == GW_CONTROL_PRECOND_PROGRAMS && !c.programs_valid) ||
            (kind == GW_CONTROL_DEFERRED && (minute < 0 || minute > 1439)))) {
            finish(GW_CONTROL_RESULT_CONFIG); return;
        }
    }
    c.now = now;
    if (c.active && c.limited && !limit_permitted(false)) finish(GW_CONTROL_RESULT_NOT_READY);
    (void)PT_SCHEDULE(ChargeLimit());
    (void)PT_SCHEDULE(AutoRefresh());
    if (!c.active) return;
    if (!gw_vehicle_control_available()) { finish(GW_CONTROL_RESULT_UNAVAILABLE); return; }
    if (!c.operation_tmr) { finish(GW_CONTROL_RESULT_OPERATION_TIMEOUT); return; }
    (void)PT_SCHEDULE(RunRequest());
    varstore_write(GW_VAR_CAN_WAKE_STATE, normal() ? 1 : c.phase == GW_CONTROL_WAIT_BSI ? 2 : 0);
    diagnostics();
}

/*****************************************************************************/
// TaskCan consumes one raw committed mailbox, with bounded lifetime. There is
// a single physical TX path for both wake frames and subsequent commands.

static void tx_result(uint32_t generation, esp_err_t error)
{
    if (generation != (uint32_t)raw(GW_VAR_CONTROL_TX_GENERATION, 0, 0) ||
        raw(GW_VAR_CONTROL_TX_PENDING, 0, 0) != 1) return;
    varstore_write(GW_VAR_CONTROL_TX_ERROR, error);
    varstore_write(GW_VAR_CONTROL_TX_COMPLETED, generation);
}
bool gw_vehicle_control_prepare(uint32_t *consumed, uint32_t *id, uint8_t *length, uint8_t data[8])
{
    uint32_t gen = (uint32_t)raw(GW_VAR_CONTROL_TX_GENERATION, 0, 0);
    if (!gen || gen == *consumed || raw(GW_VAR_CONTROL_TX_PENDING, 0, 0) != 1) return false;
    int identifier = raw(GW_VAR_CONTROL_TX_ID, 200, -1);
    int len = raw(GW_VAR_CONTROL_TX_LENGTH, 200, -1);
    int payload = raw(GW_VAR_CONTROL_TX_PAYLOAD, 200, -1);
    int hi24 = raw(GW_VAR_CONTROL_TX_DATA_HI24, 200, -1);
    int lo24 = raw(GW_VAR_CONTROL_TX_DATA_LO24, 200, -1);
    int current_phase = raw(GW_VAR_CONTROL_PHASE, 0, -1);
    // Pending is committed last. Do not consume a generation while its writer
    // is still publishing fields, or combine fields from adjacent mailboxes.
    if (raw(GW_VAR_CONTROL_TX_PENDING, 0, 0) != 1 ||
        gen != (uint32_t)raw(GW_VAR_CONTROL_TX_GENERATION, 0, 0)) return false;
    *consumed = gen;
    bool wake = (identifier == CAN_WAKE_ID || identifier == CAN_WAKE_ALT_ID) &&
        len == 2 && payload >= 0 && payload <= 65535 && current_phase == GW_CONTROL_WAIT_BSI;
    bool precond = len == 7 && hi24 >= 0 && hi24 <= 0xFFFFFF && lo24 >= 0 && lo24 <= 0xFFFFFF &&
        ((identifier == 0x31B && (payload == 0x80 || payload == 0xC0) && current_phase == GW_CONTROL_PRECOND_TX) ||
         (identifier == 0x11B && payload == 0 && current_phase == GW_CONTROL_PRECOND_AUX_TX));
    bool command = normal() && (precond || ((identifier == 0x394 && len == 1 && payload == 0x10 && current_phase == GW_CONTROL_REFRESH_TX) ||
        (identifier == 0x35B && len == 2 && payload >= 0x8000 && payload <= 65535 &&
         (payload & 0x7FF) <= 1439 && current_phase == GW_CONTROL_MODE_TX)));
    if ((c.limited && !limit_permitted(identifier == 0x35B)) ||
        !gw_vehicle_control_available() || !raw(GW_VAR_CONTROL_TX_GENERATION, 200, 0) ||
        (!command && !(wake && !normal()))) {
        tx_result(gen, ESP_ERR_INVALID_STATE); return false;
    }
    *id = (uint32_t)identifier; *length = (uint8_t)len;
    if (len == 7) {
        data[0] = payload;
        data[1] = hi24 >> 16; data[2] = hi24 >> 8; data[3] = hi24;
        data[4] = lo24 >> 16; data[5] = lo24 >> 8; data[6] = lo24;
        return true;
    }
    data[0] = len == 1 ? (uint8_t)payload : (uint8_t)(payload >> 8);
    data[1] = (uint8_t)payload;
    return true;
}
void gw_vehicle_control_submitted(uint32_t generation, esp_err_t error)
{
    tx_result(generation, error);
}
