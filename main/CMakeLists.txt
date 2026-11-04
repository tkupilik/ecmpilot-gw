idf_component_register(SRCS "main.cpp" "console.cpp" "settings.c" "network.cpp" "web.c" "process_data.c" "udp.c" "udp_protocol.c" "log_buffer.c" "log_capture.c" "task_stats.c" "task_can.cpp" "can_rx_data.c" "can_decode.c" "can_hs1_decode.c" "can_info_decode.c" "can_wake.c" "vehicle_data.c" "vehicle_control.c" "ha_telemetry.c" "ha_bridge.c" "can_replay.c" "gvret.c" "gvret_protocol.c" "vars_http.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES runtime protothreads varstore console esp_wifi esp_netif esp_event
        nvs_flash esp_http_server app_update json mqtt esp_app_format esp_system lwip driver esp_pm esp_hw_support
    EMBED_TXTFILES "web/index.html")
target_sources(${COMPONENT_LIB} PRIVATE "power.cpp" "power_policy.c" "retained_log.c" "auto_refresh.c" "ha_settings.c")
