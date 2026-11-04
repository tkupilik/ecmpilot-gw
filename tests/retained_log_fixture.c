// Host-only reset/corruption injection; production storage/algorithms are real.
#include "../main/retained_log.c"

void test_cold_boot(void)
{
    memset(banks, 0, sizeof(banks));
    gw_retained_log_boot(false);
    gw_log_buffer_reset();
}
void test_warm_boot(void)
{
    gw_retained_log_boot(true);
    gw_log_buffer_reset();
}
void test_invalid_header(void) { banks[current].boot_inverse ^= 1; }
void test_corrupt_latest(void)
{
    banks[current].entries[(next - 2) % GW_RETAINED_LOG_CAPACITY].text[0] ^= 1;
}
void test_interrupted_append(void)
{
    // Simulate reset after invalidating the target slot and partially writing.
    RetainedEntry *entry = &banks[current].entries[(next - 1) % GW_RETAINED_LOG_CAPACITY];
    entry->sequence = 0;
    memcpy(entry->text, "partial", 8);
}
unsigned test_storage_size(void) { return sizeof(banks); }
