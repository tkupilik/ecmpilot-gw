"""Incremental IDF verification when Windows Ninja/async pipes are unavailable.
Run inside the ESP-IDF environment. Reuses the configured SDK objects; compiles
all project-owned translation units, then executes current generated archive,
linker and image commands sequentially. It does not flash or access CAN.
"""
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'

def run(command, label, cwd=BUILD):
    print(label, flush=True)
    subprocess.run(command, cwd=cwd, check=True, timeout=120)

run(['cmake', '-S', str(ROOT), '-B', str(BUILD), '-G', 'Ninja'], 'Configure current sources', ROOT)
graph = (BUILD / 'build.ninja').read_text(encoding='utf-8')
archives = re.findall(r'^build ([^:]+\.a): (?:C|CXX)_STATIC_LIBRARY_LINKER', graph, re.M)
def commands(*targets):
    path = BUILD / 'sequential-commands.txt'
    with path.open('w', encoding='utf-8') as output:
        subprocess.run(['ninja', '-C', str(BUILD), '-t', 'commands', '-s', *targets], stdout=output, check=True, timeout=30)
    return path.read_text(encoding='utf-8').splitlines()

for command in commands('index.html.S'):
    run(command, 'Embed web UI')
entries = json.loads((BUILD / 'compile_commands.json').read_text(encoding='utf-8'))
compiled = []
for entry in entries:
    file = Path(entry['file'])
    if file.is_relative_to(ROOT):
        run(entry['command'], 'Compile ' + str(file.relative_to(ROOT)), entry['directory'])
        compiled.append(str(file.relative_to(ROOT)))
# Ninja normally materializes response files while launching a rule. Its
# command-dump mode does not, so supply the archive response files explicitly.
for target in archives:
    a = graph.index('build ' + target + ':')
    block = graph[a:graph.index('\n\n', a)]
    fields = dict(re.findall(r'^  (\w+) = (.*)$', block, re.M))
    assert (BUILD / target).resolve().is_relative_to(BUILD.resolve())
    if 'RSP_FILE' in fields:
        archive_inputs = block.splitlines()[0].split(' ', 3)[3].split(' |')[0]
        rsp = (BUILD / fields['RSP_FILE'].replace('\\', '/')).resolve()
        assert rsp.is_relative_to(BUILD.resolve())
        rsp.parent.mkdir(parents=True, exist_ok=True)
        rsp.write_text(archive_inputs, encoding='utf-8')
for command in commands(*archives):
    run(command, 'Archive')
for command in commands('esp-idf/esp_system/ld/sections.ld'):
    run(command, 'Generate linker sections')
a = graph.index('build ecmpilot_gw.elf:')
block = graph[a:graph.index('\n\n', a)]
inputs = block.splitlines()[0].split(' ', 3)[3].split(' |')[0]
variables = dict(re.findall(r'^  (\w+) = (.*)$', block, re.M))
(BUILD / variables['RSP_FILE'].replace('\\', '/')).write_text(
    inputs + ' ' + variables['LINK_PATH'] + ' ' + variables['LINK_LIBRARIES'], encoding='utf-8')
for command in commands('ecmpilot_gw.elf'):
    run(command, 'Link')
for command in commands('.bin_timestamp', 'esp-idf/esptool_py/CMakeFiles/app_check_size'):
    run(command, 'Generate/check OTA image')
(BUILD / 'sequential-build.json').write_text(json.dumps({'compiled_project_files': compiled, 'sdk_objects_reused': True}, indent=2), encoding='utf-8')
print('Sequential incremental build completed', flush=True)
