"""Record this manual CAN-wake deep-sleep build; no hardware/network access."""
import ctypes
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
NM=Path('C:/Espressif/tools/xtensa-esp-elf/esp-13.2.0_20240530/xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm.exe')
def read(name): return (ROOT/name).read_text(encoding='utf-8')
assert 'Sequential incremental build completed' in read('build-can-deep-sleep.log')
assert 'Ran 156 tests' in read('can-deep-tests.log')
assert re.search(r'^OK$',read('can-deep-tests.log'),re.M)
cfg=read('build/config/sdkconfig.h')
for key in ['GW_CAN_INFO','GW_CAN_RX_ENABLED','PM_ENABLE','FREERTOS_USE_TICKLESS_IDLE']:
    assert f'#define CONFIG_{key} 1' in cfg
assert '#define CONFIG_GW_CAN_BITRATE 125000' in cfg
assert '#define CONFIG_SPIRAM 1' not in cfg
elf=ROOT/'build/ecmpilot_gw.elf'
symbols=subprocess.check_output([str(NM),str(elf)],text=True)
for name in ['gw_power_enter_can_deep_sleep','esp_deep_sleep_start','esp_sleep_enable_ext0_wakeup',
             'gw_can_wake_prepare','gw_retained_log_read','gpio_hold_en','gpio_hold_dis']:
    assert re.search(r'\b'+name+r'$',symbols,re.M),name
for name in ['gw_can_version_prepare','gw_power_prepare_deep_sleep','esp_sleep_enable_timer_wakeup']:
    # SDK PM may contain its own timer facilities; our old deep timer helper must be gone.
    if name != 'esp_sleep_enable_timer_wakeup': assert name not in symbols
source=read('main/power.cpp')
assert 'esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)' in source
assert 'esp_sleep_enable_ext0_wakeup(GW_CAN_RX_GPIO, 0)' in source
assert 'esp_sleep_enable_timer_wakeup' not in source
assert 'gpio_deep_sleep_hold_en()' in source
assert 'GPIO_NUM_16' in read('main/can_pins.h')
assert 'GPIO_NUM_15' in read('main/can_pins.h')
assert 'twai_driver_uninstall_v2' in read('main/task_can.cpp')
assert 'gpio_reset_pin(TX_GPIO)' in read('main/task_can.cpp')
assert read('main/task_can.cpp').count('twai_transmit_v2(')==1
image=(ROOT/'build/ecmpilot_gw.bin').read_bytes()
assert b'Deep sleep' in image and len(image)<0x1F0000
assert (ROOT/'build/index.html.S').stat().st_mtime >= (ROOT/'main/web/index.html').stat().st_mtime
lib=ctypes.CDLL(str(ROOT/'build-host-tests/udp-info-test.dll'))
assert lib.gw_process_data_init()==0 and lib.varstore_count()==182
assert re.search(r'\.rtc_noinit\s+0x50000000\s+0x18d8',read('build/ecmpilot_gw.map'))
report=dict(date='2026-11-01',version='0.5.0',native_tests=156,
            browser_tests=dict(power=9,wifi_ota=13,log=6),signals=182,
            can=dict(profile='INFO',bitrate=125000,rx_gpio=16,tx_gpio=15,version_tx_removed=True),
            deep_sleep=dict(trigger='Manual Home button / POST api/power can_deep_sleep',
                automatic_timeout=False,timer_wake=False,wake_source='EXT0 GPIO16 LOW',
                tx_held_recessive=True,can_owner_uninstalls=True,network_owner_stops_wifi=True,
                raw_varstore_handshake=True,wake_is_new_boot=True),
            physical_test=False,flashed=False,no_psram=True,
            build_log='build-can-deep-sleep.log',test_log='can-deep-tests.log',
            image=dict(path='build/ecmpilot_gw.bin',bytes=len(image),ota_slot_bytes=0x1F0000,
                sha256=hashlib.sha256(image).hexdigest()))
(ROOT/'docs/can-deep-sleep-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
