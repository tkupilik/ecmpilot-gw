idf_component_register(SRCS "main.cpp" "console.cpp" "settings.c" "network.cpp" "web.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES runtime protothreads console esp_wifi esp_netif esp_event
        nvs_flash esp_http_server app_update json esp_app_format esp_system
    EMBED_TXTFILES "web/index.html")
