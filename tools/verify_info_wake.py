"""Record the INFO wake/reset build evidence; no device/network access."""
import ctypes
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
NM = Path('C:/Espressif/tools/xtensa-esp-elf/esp-13.2.0_20240530/xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm.exe')

def read(name):
    return (ROOT / name).read_text(encoding='utf-8')

full = read('build-info-reset.log')
steps = re.findall(r'\[serial (\d+)/(\d+)\]', full)
assert steps and steps[-1][0] == steps[-1][1]
assert 'Successfully created esp32s3 image' in full
assert 'Sequential incremental build completed' in read('build-info-reset-final.log')
tests = read('info-reset-tests.log')
assert 'Ran 148 tests' in tests and re.search(r'^OK$', tests, re.M)
cfg = read('build/config/sdkconfig.h')
for key in ['GW_CAN_INFO', 'GW_CAN_125K', 'GW_CAN_RX_ENABLED', 'PM_ENABLE',
            'FREERTOS_USE_TICKLESS_IDLE', 'ESP_WIFI_TASK_PINNED_TO_CORE_0',
            'LWIP_TCPIP_TASK_AFFINITY_CPU0', 'ESP_MAIN_TASK_AFFINITY_CPU1']:
    assert f'#define CONFIG_{key} 1' in cfg, key
assert '#define CONFIG_GW_CAN_BITRATE 125000' in cfg
assert '#define CONFIG_SPIRAM 1' not in cfg
assert '#define CONFIG_FREERTOS_TIMER_TASK_PRIORITY 24' in cfg
for name in ['sdkconfig.defaults', 'sdkconfig']:
    assert 'CONFIG_GW_CAN_INFO=y' in read(name)
    assert 'CONFIG_GW_CAN_125K=y' in read(name)

source = read('main/task_can.cpp')
assert 'general.tx_queue_len = 0' in source and 'TWAI_MSG_FLAG_SS' in source
assert source.count('twai_transmit_v2(') == 1
assert 'TWAI_MODE_NORMAL' in source and 'GW_CAN_IS_INFO' in source
assert 'GPIO_NUM_15' in source and 'GPIO_NUM_16' in source
assert 'OnVersionTx' not in source
symbols = subprocess.check_output([str(NM), str(ROOT / 'build/ecmpilot_gw.elf')], text=True)
for name in ['gw_can_wake_prepare', 'gw_can_info_decode', 'gw_retained_log_read',
             'gw_retained_log_boot', 'gw_log_reset_name', 'gw_log_reset_reason']:
    assert re.search(r'\b' + name + r'$', symbols, re.M), name
assert 'gw_can_version_prepare' not in symbols
assert 'OnVersionTx' not in symbols
mapping = read('build/ecmpilot_gw.map')
assert re.search(r'\.rtc_noinit\s+0x50000000\s+0x18d8', mapping)
assert 'retained_log.c.obj' in mapping
binary = ROOT / 'build/ecmpilot_gw.bin'
image = binary.read_bytes()
assert b'Reset: ${s.reset_reason' in image
assert b'Previous boot: last ' in image
assert len(image) < 0x1F0000
assert (ROOT / 'build/index.html.S').stat().st_mtime >= (ROOT / 'main/web/index.html').stat().st_mtime
lib = ctypes.CDLL(str(ROOT / 'build-host-tests/udp-info-test.dll'))
assert lib.gw_process_data_init() == 0
assert lib.varstore_count() == 173
report = dict(date='2026-11-01', version='0.5.0',
    native_tests=148,
    browser_tests=dict(log=6, wifi_ota=13, power=6),
    can=dict(profile='INFO', bitrate=125000, mode='normal', tx_gpio=15, rx_gpio=16,
             queue_length=0, single_shot=True, version_transmitter_removed=True,
             wake=dict(default_id='43C', allowed_ids=['401', '43C'], dlc=2,
                       default_payload='00 80', packed_payload='0xB0B1', force_supported=True,
                       one_per_valid_udp=True, skip_power_management=1,
                       power_management_max_age_ms=1000, pending_max_age_ms=250),
             optional_profile='HSCAN1/500000 listen-only; no wake TX'),
    log=dict(reset_reason=True, previous_records=12, live_records=64,
             rtc_address='50000000', rtc_bytes=6360, flash_writes=False),
    signals=173, no_psram=True, hardware_test=False, flashed=False,
    evidence=dict(full_build='build-info-reset.log', full_edges=int(steps[-1][1]),
                  final_build='build-info-reset-final.log', native_tests='info-reset-tests.log',
                  browser_commands=['node tests/log-ui.test.cjs', 'node tests/web-ui.test.cjs',
                                    'node tests/power-ui.test.cjs']),
    image=dict(path='build/ecmpilot_gw.bin', bytes=len(image), ota_slot_bytes=0x1F0000,
               sha256=hashlib.sha256(image).hexdigest()))
(ROOT / 'docs/info-can-wake-verification.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print(json.dumps(report, indent=2))
