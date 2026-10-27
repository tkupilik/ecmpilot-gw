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
$flags = @('--target=x86_64-pc-windows-msvc','-std=c11','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-DCJSON_HIDE_SYMBOLS',"-I$PSScriptRoot/host/include", "-I$projectRoot/main", "-I$projectRoot/components/varstore/include", "-I$json",'-c')
$sources = @('components/varstore/varstore.c','main/process_data.c','main/udp_protocol.c','main/settings.c','main/log_buffer.c','main/log_capture.c','main/task_stats.c','tests/host/clock.c','tests/host/settings_storage.c','tests/host/log_sink.c','tests/host/task_snapshot.c')
$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $out ((Split-Path $source -Leaf) + '.obj')
    & $compiler.FullName @flags -Wall -Wextra -Werror "$projectRoot/$source" -o $object
    if ($LASTEXITCODE) { throw "Compilation failed: $source" }
    $objects += $object
}
& $compiler.FullName @flags "$json/cJSON.c" -o "$out/cJSON.obj"
if ($LASTEXITCODE) { throw 'cJSON compilation failed.' }
$exports = @('gw_test_set_time','gw_process_data_init','gw_process_data_manual','gw_udp_protocol_init','gw_udp_protocol_tick','gw_udp_protocol_handle','varstore_count','varstore_definition','varstore_read','varstore_write','varstore_force','varstore_touch','varstore_invalidate') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_settings_init','gw_settings_get','gw_settings_save','gw_settings_save_network','gw_hostname_get','gw_hostname_valid','gw_test_nvs_reset','gw_test_nvs_fail_commit','gw_test_nvs_blob_size','gw_test_nvs_hostname') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_settings_save_wifi','gw_ap_password_get','gw_ap_password_valid','gw_test_nvs_ap_password') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_log_buffer_reset','gw_log_buffer_append','gw_log_buffer_read','gw_log_start','gw_log_boot_id','gw_test_log_emit','gw_test_log_calls') | ForEach-Object { '/export:' + $_ }
$exports += @('gw_test_tasks','gw_test_tasks_status','gw_test_tasks_errors') | ForEach-Object { '/export:' + $_ }
& "$toolBin/lld-link.exe" /dll /noentry /nodefaultlib /machine:x64 "/out:$out/udp-test.dll" @exports @objects "$out/cJSON.obj" "$out/msvcrt.lib"
if ($LASTEXITCODE) { throw 'Protocol test library link failed.' }
$env:PYTHONDONTWRITEBYTECODE = '1'
& $PythonPath -m unittest discover -s "$PSScriptRoot" -p 'test_*.py' -v
if ($LASTEXITCODE) { throw 'UDP tests failed.' }
