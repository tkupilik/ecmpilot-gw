idf_component_register(SRCS "src/runtime_process.cpp" "src/runtime_signal_store.c"
    INCLUDE_DIRS "include" REQUIRES freertos esp_timer)
