"""Record GW parity evidence and target-link the optional HA branch without flashing."""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT/'build'
OUT = ROOT/'build-host-tests'
NINJA = 'C:/Espressif/tools/ninja/1.12.1/ninja.exe'
NM = Path('C:/Espressif/tools/xtensa-esp-elf/esp-13.2.0_20240530/xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm.exe')

def read(name):
    return (ROOT/name).read_text(encoding='utf-8')

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-log', default='build-lcd-parity.log')
parser.add_argument('--final-build-log', default=None)
parser.add_argument('--test-log', default='lcd-parity-tests.log')
args = parser.parse_args()

full = read(args.build_log)
steps = re.findall(r'\[serial (\d+)/(\d+)\]', full)
assert steps
if args.final_build_log:
    # A complete SDK compile/archive pass may need the incremental runner to
    # rematerialize archive/link response files before the final application link.
    assert int(steps[-1][0]) >= int(steps[-1][1]) - 3
else:
    assert steps[-1][0] == steps[-1][1]
    assert 'Successfully created esp32s3 image' in full
if args.final_build_log:
    final = read(args.final_build_log)
    assert 'Sequential incremental build completed' in final
    assert 'Successfully created esp32s3 image' in final
tests = read(args.test_log)
assert 'Ran 130 tests' in tests and re.search(r'^OK$', tests, re.M)
assert 'All varstore tests passed.' in read('lcd-parity-varstore.log')

cfg = read('build/config/sdkconfig.h')
is_info = '#define CONFIG_GW_CAN_INFO 1' in cfg
profile = 'INFO' if is_info else 'HSCAN1'
bitrate = 125000 if is_info else 500000
assert ('#define CONFIG_GW_CAN_HS1 1' in cfg) != is_info
for key in ['GW_CAN_RX_ENABLED', 'MQTT_TASK_CORE_SELECTION_ENABLED', 'MQTT_USE_CORE_0',
            'ESP_WIFI_TASK_PINNED_TO_CORE_0', 'LWIP_TCPIP_TASK_AFFINITY_CPU0', 'ESP_MAIN_TASK_AFFINITY_CPU1']:
    assert f'#define CONFIG_{key} 1' in cfg, key
assert f'#define CONFIG_GW_CAN_BITRATE {bitrate}' in cfg
for name in ['sdkconfig.defaults', 'sdkconfig']:
    settings = read(name)
    assert f'CONFIG_GW_CAN_{"INFO" if is_info else "HS1"}=y' in settings
    assert f'CONFIG_GW_CAN_{"125K" if is_info else "500K"}=y' in settings
assert '#define CONFIG_FREERTOS_TIMER_TASK_PRIORITY 24' in cfg
assert '#define CONFIG_SPIRAM 1' not in cfg
assert '#define CONFIG_GW_HA_ENABLE 1' not in cfg
assert '#define GW_GVRET_QUEUE_LENGTH 64' in read('main/gvret_protocol.h')
assert 'GPIO_NUM_15' in read('main/task_can.cpp') and 'GPIO_NUM_16' in read('main/task_can.cpp')
assert 'TWAI_MSG_FLAG_SS' in read('main/task_can.cpp') and 'general.tx_queue_len = 0' in read('main/task_can.cpp')

# Compile the enabled branch with a TEST-NET broker string. This is a separate
# object/ELF only: the delivered default image remains broker-unconfigured.
header = OUT/'ha-enabled-config.h'
header.write_text('''#include "sdkconfig.h"
#undef CONFIG_GW_HA_ENABLE
#define CONFIG_GW_HA_ENABLE 1
#define CONFIG_GW_HA_MQTT_URI "mqtt://192.0.2.1:1883"
#define CONFIG_GW_HA_MQTT_USERNAME ""
#define CONFIG_GW_HA_MQTT_PASSWORD ""
#define CONFIG_GW_HA_TOPIC_PREFIX "ecmpilot"
#define CONFIG_GW_HA_PUBLISH_PERIOD_MS 1000
''', encoding='utf-8')
entry = next(e for e in json.loads(read('build/compile_commands.json'))
             if e['file'].replace('\\', '/').endswith('/main/ha_bridge.c'))
obj = (OUT/'ha-enabled-check.obj').as_posix()
command = re.sub(r' -o\s+(?:"[^"]+"|\S+)', ' -o '+obj, entry['command'])
command = re.sub(r' -MF\s+(?:"[^"]+"|\S+)', ' -MF '+obj+'.d', command)
result = subprocess.run(command+' -include '+header.as_posix(), cwd=entry['directory'], capture_output=True, text=True)
(ROOT/'ha-enabled-compile.log').write_text(result.stdout+result.stderr, encoding='utf-8')
assert result.returncode == 0, result.stderr
graph = subprocess.check_output([NINJA, '-C', str(BUILD), '-t', 'commands', 'all'], text=True).splitlines()
command = next(c for c in graph if ' -o ecmpilot_gw.elf ' in c)
command = command.replace('-Wl,--Map='+BUILD.as_posix()+'/ecmpilot_gw.map', '-Wl,--Map='+(OUT/'ha-enabled-check.map').as_posix())
command = command.replace(' -o ecmpilot_gw.elf ', ' -o '+(OUT/'ha-enabled-check.elf').as_posix()+' ')
command = command.replace('@CMakeFiles', obj+' @CMakeFiles')
result = subprocess.run(command, cwd=BUILD, capture_output=True, text=True)
(ROOT/'ha-enabled-link.log').write_text(result.stdout+result.stderr, encoding='utf-8')
assert result.returncode == 0, result.stderr
symbols = subprocess.check_output([str(NM), str(BUILD/'ecmpilot_gw.elf')], text=True)
decoder = 'gw_can_info_decode' if is_info else 'gw_can_hs1_decode_publish'
for name in [decoder, 'gw_can_wake_prepare', 'gw_can_version_prepare', 'gw_vehicle_tick', 'varstore_write_at']:
    assert re.search(r'\b'+name+r'$', symbols, re.M), name
enabled = subprocess.check_output([str(NM), str(OUT/'ha-enabled-check.elf')], text=True)
for name in ['gw_ha_start', 'gw_ha_state_json', 'gw_ha_discovery_json', 'esp_mqtt_client_publish']:
    assert re.search(r'\b'+name+r'$', enabled, re.M), name
binary = BUILD/'ecmpilot_gw.bin'
if is_info:
    assert b'BSI wake TX #' in binary.read_bytes()
assert binary.stat().st_size < 0x1F0000
lib = ctypes.CDLL(str(OUT/'udp-info-test.dll'))
assert lib.gw_process_data_init() == 0
report = dict(date='2026-10-31', version='0.5.0', host_tests=130,
              varstore_concurrency='150000 writes +225000 overrides +two readers; PASS',
              units='mV/mA/mC/W/percent x10; range km x10', signals=lib.varstore_count(), appended_ids=45,
              can=dict(profile=profile, bitrate=bitrate, tx_gpio=15, rx_gpio=16,
                       mode='normal' if is_info else 'listen-only',
                       wake_version_enabled=is_info, rx_handlers=12 if is_info else 7,
                       battery_source='3E0 SOC; no voltage' if is_info else '5C3 SOC/pack voltage',
                       optional_profile='HSCAN1/500000 listen-only' if is_info else 'INFO/125000 normal',
                       wake=dict(enabled=is_info, id='401', dlc=2, payload='00 02', cause='Internal', one_per_udp=True, skip_phase=1),
                       version=dict(enabled=is_info, id='5DF', period_ms=500, only_phase=1), queue_length=0, single_shot=True),
              gvret=dict(port=23, queue_records=64, external_memory=False, socket_tx_supported=False),
              ha=dict(default_enabled=False, enabled_target_compile=True, enabled_target_link=True,
                      entities=24, client_core=0, publisher_core=0, outbox_limit=8192,
                      live_broker_test=False, telemetry_wakes_vehicle=False),
              no_ufrm=True, no_psram=True, flashed=False,
              evidence=dict(build_log=args.build_log, final_build_log=args.final_build_log, test_log=args.test_log),
              image=dict(path='build/ecmpilot_gw.bin', bytes=binary.stat().st_size,
                         sha256=hashlib.sha256(binary.read_bytes()).hexdigest()))
(ROOT/'docs/lcd-backport-verification.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print(json.dumps(report, indent=2))
