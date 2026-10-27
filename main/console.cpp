// SPDX-License-Identifier: BSD-2-Clause
#include "gateway.h"
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include "esp_app_desc.h"
#include "esp_console.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int Hello(int, char **)
{
    printf("Hello eCMPilot GW! Console CPU%d, application CPU%d, network CPU%d.\n",
           xPortGetCoreID(), GW_APP_CORE, GW_NETWORK_CORE);
    return 0;
}

static int Status(int, char **)
{
    gw_network_status_t s;
    gw_network_status(&s);
    printf("eCMPilot GW %s / IDF %s\nUptime: %" PRId64 " s; heap: %" PRIu32
           "; heartbeat: %" PRIu32 "\nPartition: %s\n",
           esp_app_get_description()->version, esp_get_idf_version(), esp_timer_get_time() / 1000000,
           esp_get_free_heap_size(), gw_heartbeat_count(), esp_ota_get_running_partition()->label);
    printf("Wi-Fi: %s; SSID: %s; IP: %s; RSSI: %d; disconnect reason: %d\n",
           s.connected ? "connected" : "offline", s.ssid, s.ip, s.rssi, s.last_disconnect_reason);
    printf("Hostname: %s\n", s.hostname);
    printf("Setup AP: %s (%s); web: http://192.168.4.1/ while AP is active\n",
           s.setup_ap ? "active" : "inactive", s.ap_ssid);
    return 0;
}

static int Wifi(int argc, char **argv)
{
    gw_settings_t settings;
    gw_settings_get(&settings);
    if ((argc == 2 || argc == 3) && !strcmp(argv[1], "hostname")) {
        char hostname[GW_HOSTNAME_SIZE];
        if (argc == 2) { gw_hostname_get(hostname); puts(hostname); return 0; }
        const esp_err_t err = gw_settings_save_network(&settings, argv[2]);
        if (err != ESP_OK) { printf("Save failed: %s\n", esp_err_to_name(err)); return 1; }
        gw_network_reconfigure(); puts("Hostname saved; reconnect scheduled"); return 0;
    }
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "list"))) {
        for (unsigned i = 0; i < settings.count; ++i)
            printf("%u: %s (%s)\n", i + 1, settings.networks[i].ssid,
                   settings.networks[i].password[0] ? "password saved" : "open");
        return 0;
    }
    if (argc >= 3 && argc <= 4 && !strcmp(argv[1], "add")) {
        if (!argv[2][0] || strlen(argv[2]) > 32 || (argc == 4 && strlen(argv[3]) > 64)) return 1;
        unsigned i = 0;
        for (; i < settings.count; ++i) if (!strcmp(settings.networks[i].ssid, argv[2])) break;
        if (i == GW_MAX_NETWORKS) { puts("Wi-Fi list is full"); return 1; }
        if (i == settings.count) ++settings.count;
        strlcpy(settings.networks[i].ssid, argv[2], GW_SSID_SIZE);
        strlcpy(settings.networks[i].password, argc == 4 ? argv[3] : "", GW_PASSWORD_SIZE);
    } else if (argc == 3 && !strcmp(argv[1], "remove")) {
        unsigned i = 0;
        for (; i < settings.count; ++i) if (!strcmp(settings.networks[i].ssid, argv[2])) break;
        if (i == settings.count) { puts("SSID not found"); return 1; }
        memmove(&settings.networks[i], &settings.networks[i + 1],
                (settings.count - i - 1) * sizeof(settings.networks[0]));
        memset(&settings.networks[--settings.count], 0, sizeof(settings.networks[0]));
    } else if (argc == 2 && !strcmp(argv[1], "clear")) {
        memset(&settings, 0, sizeof(settings));
        settings.version = 1;
    } else {
        puts("wifi list | wifi add <ssid> [password] | wifi remove <ssid> | wifi clear | wifi hostname [name]");
        return 1;
    }
    const esp_err_t err = gw_settings_save(&settings);
    if (err != ESP_OK) { printf("Save failed: %s\n", esp_err_to_name(err)); return 1; }
    gw_network_reconfigure();
    puts("Saved; reconnect scheduled");
    return 0;
}

static int Restart(int, char **) { gw_restart_soon(); puts("Restart scheduled"); return 0; }

extern "C" esp_err_t gw_console_start(void)
{
    esp_console_repl_t *repl;
    esp_console_repl_config_t cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    cfg.prompt = "ecmpilot-gw> ";
    cfg.task_core_id = GW_APP_CORE;
    cfg.task_stack_size = 6144;
    cfg.max_cmdline_length = 256;
    cfg.max_history_len = 1;
    esp_err_t err;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t device = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    err = esp_console_new_repl_usb_serial_jtag(&device, &cfg, &repl);
#elif CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
    esp_console_dev_uart_config_t device = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    err = esp_console_new_repl_uart(&device, &cfg, &repl);
#else
#error "Select USB Serial/JTAG or UART console"
#endif
    if (err != ESP_OK) return err;
    const esp_console_cmd_t commands[] = {
        {.command="hello", .help="Hello world and CPU affinity", .hint=nullptr, .func=Hello, .argtable=nullptr, .func_w_context=nullptr, .context=nullptr},
        {.command="status", .help="Firmware, runtime, Wi-Fi and OTA status", .hint=nullptr, .func=Status, .argtable=nullptr, .func_w_context=nullptr, .context=nullptr},
        {.command="wifi", .help="Wi-Fi: list/add/remove/clear/hostname; quote SSIDs with spaces", .hint=nullptr, .func=Wifi, .argtable=nullptr, .func_w_context=nullptr, .context=nullptr},
        {.command="restart", .help="Restart in 1.5 seconds", .hint=nullptr, .func=Restart, .argtable=nullptr, .func_w_context=nullptr, .context=nullptr},
    };
    for (const auto &command : commands) ESP_ERROR_CHECK(esp_console_cmd_register(&command));
    return esp_console_start_repl(repl);
}
