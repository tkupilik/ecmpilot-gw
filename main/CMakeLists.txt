idf_component_register(SRCS "main.cpp" "console.cpp" "settings.c" "network.cpp" "web.c" "process_data.c" "udp.c" "udp_protocol.c" "log_buffer.c" "log_capture.c" "task_stats.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES runtime protothreads varstore console esp_wifi esp_netif esp_event
        nvs_flash esp_http_server app_update json esp_app_format esp_system lwip
    EMBED_TXTFILES "web/index.html")
