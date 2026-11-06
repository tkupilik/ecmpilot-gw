param([string]$PythonPath = 'python', [string]$IdfPath = 'C:/Espressif/frameworks/esp-idf-v5.3.2')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
$compiler = Get-ChildItem 'C:/Espressif/tools/esp-clang/*/esp-clang/bin/clang.exe' | Select-Object -Last 1
$toolBin = Split-Path $compiler.FullName
$out = Join-Path $projectRoot 'build-host-tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$json = Join-Path $IdfPath 'components/json/cJSON'
& "$toolBin/llvm-dlltool.exe" -m i386:x86-64 -d "$PSScriptRoot/host/msvcrt.def" -l "$out/msvcrt.lib"
if ($LASTEXITCODE) { throw 'MSVCRT import library failed.' }
$flags = @('--target=x86_64-pc-windows-msvc','-std=c11','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-DCJSON_HIDE_SYMBOLS',"-I$PSScriptRoot/host/include", "-I$projectRoot/main", "-I$projectRoot/components/varstore/include", "-I$projectRoot/components/protothreads/include", "-I$json",'-c')
$sources = @('main/power_policy.c','tests/host/power_adapter.c','components/varstore/varstore.c','main/process_data.c','main/can_rx_data.c','main/can_decode.c','main/can_hs1_decode.c','main/can_info_decode.c','main/can_wake.c','main/vehicle_data.c','main/vehicle_control.c','main/ha_telemetry.c','main/gvret_protocol.c','main/vars_http.c','main/udp_protocol.c','main/settings.c','main/log_buffer.c','main/retained_log.c','main/log_capture.c','main/task_stats.c','tests/host/clock.c','tests/host/settings_storage.c','tests/host/log_sink.c','tests/host/task_snapshot.c','tests/host/vars_http_sink.c')
$sources = @('main/auto_refresh.c','main/ha_settings.c','main/low_power.c') + $sources
$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $out ((Split-Path $source -Leaf) + '.obj')
    & $compiler.FullName @flags -Wall -Wextra -Werror "$projectRoot/$source" -o $object
    if ($LASTEXITCODE) { throw "Compilation failed: $source" }
    $objects += $object
}
& $compiler.FullName @flags "$json/cJSON.c" -o "$out/cJSON.obj"
if ($LASTEXITCODE) { throw 'cJSON compilation failed.' }
$exports = @('gw_vehicle_control_init','gw_vehicle_control_tick','gw_vehicle_control_prepare','gw_vehicle_control_submitted','gw_test_set_time','gw_process_data_init','gw_process_data_manual','gw_udp_protocol_init','gw_udp_protocol_tick','gw_udp_protocol_handle','varstore_count','varstore_definition','varstore_read','varstore_write','varstore_force','varstore_touch','varstore_invalidate') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_power_policy_init','gw_power_policy_tick','gw_power_request','gw_test_power_reset','gw_test_power_fail','gw_test_power_value','gw_process_data_manual') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_gvret_protocol_reset','gw_gvret_protocol_feed') | ForEach-Object { '/export:' + $_ }
$exports += '/export:gw_auto_refresh_init'
$exports += '/export:gw_ha_unavailable_json'
$exports += '/export:gw_ha_diagnostics_due'
$exports += @('gw_low_power_init','gw_low_power_tick','gw_low_power_eligible','gw_low_power_json','gw_low_power_save','gw_power_auto_request','gw_vehicle_control_busy') | ForEach-Object { '/export:' + $_ }
$exports += '/export:gw_test_nvs_auto_refresh_v1'
$exports += @('gw_udp_protocol_boot_id','gw_udp_protocol_mqtt_handle','gw_udp_protocol_mqtt_poll') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_ha_settings_init','gw_ha_settings_get','gw_ha_settings_save','gw_ha_settings_json','cJSON_Parse','cJSON_Delete','cJSON_PrintUnformatted') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_can_wake_reset','gw_can_wake_encode','gw_vehicle_init','gw_vehicle_tick','varstore_inspect','varstore_write_at','gw_ha_retired_count','gw_ha_retired_key','gw_ha_retired_component','gw_ha_entity_count','gw_ha_entity_key','gw_ha_entity_component','gw_ha_state_json','gw_ha_diagnostics_json','gw_ha_discovery_json','cJSON_free') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_settings_init','gw_settings_get','gw_settings_save','gw_settings_save_network','gw_hostname_get','gw_hostname_valid','gw_test_nvs_reset','gw_test_nvs_fail_commit','gw_test_nvs_blob_size','gw_test_nvs_hostname') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_settings_save_wifi','gw_ap_password_get','gw_ap_password_valid','gw_test_nvs_ap_password') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_settings_save_wifi_options','gw_ap_enabled_get','gw_test_nvs_ap_enabled') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_can_rx_data_init','gw_can_rx_accept','gw_can_rx_publish_health','gw_can_info_decode','gw_can_info_init','gw_can_info_reset') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_log_reset_reason','gw_log_reset_name','gw_retained_log_boot','gw_retained_log_read','gw_retained_log_count','gw_test_reset_reason') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_log_buffer_reset','gw_log_buffer_append','gw_log_buffer_read','gw_log_start','gw_log_boot_id','gw_test_log_emit','gw_test_log_calls') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_test_tasks','gw_test_tasks_status','gw_test_tasks_errors') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_test_vars','gw_test_vars_body','gw_test_vars_chunks','gw_test_vars_max_chunk','gw_test_vars_ended','varstore_init','cJSON_InitHooks') | ForEach-Object { '/export:' + $_ }
& "$toolBin/lld-link.exe" /dll /noentry /nodefaultlib /machine:x64 "/out:$out/udp-test.dll" @exports @objects "$out/cJSON.obj" "$out/msvcrt.lib"
if ($LASTEXITCODE) { throw 'Protocol test library link failed.' }
$infoObjects = @()
foreach ($source in $sources) {
    $original = Join-Path $out ((Split-Path $source -Leaf) + '.obj')
    if ($source -in @('main/process_data.c','main/can_rx_data.c','main/can_decode.c','main/can_wake.c','main/vehicle_data.c','main/vehicle_control.c','main/low_power.c','main/udp_protocol.c','main/gvret_protocol.c')) {
        $original = Join-Path $out ('info-' + (Split-Path $source -Leaf) + '.obj')
        & $compiler.FullName @flags -DCONFIG_GW_CAN_INFO=1 -Wall -Wextra -Werror "$projectRoot/$source" -o $original
        if ($LASTEXITCODE) { throw "INFO compilation failed: $source" }
    }
    $infoObjects += $original
}
& "$toolBin/lld-link.exe" /dll /noentry /nodefaultlib /machine:x64 "/out:$out/udp-info-test.dll" @exports @infoObjects "$out/cJSON.obj" "$out/msvcrt.lib"
if ($LASTEXITCODE) { throw 'INFO test library link failed.' }
& $PythonPath "$PSScriptRoot/build_gvret_host.py"
if ($LASTEXITCODE) { throw 'GVRET test library build failed.' }
& $PythonPath "$PSScriptRoot/build_retained_log_host.py"
if ($LASTEXITCODE) { throw 'RTC log test library build failed.' }
$env:PYTHONDONTWRITEBYTECODE = '1'
& $PythonPath -m unittest discover -s "$PSScriptRoot" -p 'test_*.py' -v
if ($LASTEXITCODE) { throw 'UDP tests failed.' }
