// Native test boundary: only the hardware PM configuration is substituted.
#include "power_policy.h"
static int failure, calls, minimum, maximum, light, deep_calls;
void gw_test_power_reset(void) { failure = calls = minimum = maximum = light = deep_calls = 0; }
void gw_test_power_fail(int error) { failure = error; }
int gw_test_power_value(int field)
{
    switch (field) { case 0: return calls; case 1: return minimum; case 2: return maximum; case 4: return deep_calls; default: return light; }
}
esp_err_t gw_power_apply_profile(int low, int high, bool sleep)
{
    ++calls;
    if (!failure) { minimum = low; maximum = high; light = sleep; }
    return failure;
}
esp_err_t gw_power_enter_can_deep_sleep(void)
{
    ++deep_calls;
    return ESP_ERR_INVALID_STATE; // Simulated dominant RX / refused entry.
}
