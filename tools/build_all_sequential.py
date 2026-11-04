"""Optional Windows fallback: execute Ninja's own command graph without its pipe runner.

Not incremental; sequential by default (GW_BUILD_JOBS may batch independent
compiler edges). Real Ninja still expands the generated graph and response files. Intended for restricted shells where Ninja cannot create pipes.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import json
import os
import re
import subprocess
import sys

real = os.environ.get('LCD_REAL_NINJA', r'C:\Espressif\tools\ninja\1.12.1\ninja.exe')
args = sys.argv[1:]
if '-t' in args or '--version' in args or '--help' in args:
    sys.exit(subprocess.call([real, *args]))
options, targets = [], []
i = 0
while i < len(args):
    arg = args[i]
    if arg in ('-C', '-f'):
        options.extend(args[i:i+2]); i += 2; continue
    if arg in ('-j', '-l', '-d', '-w'):
        i += 2; continue
    if arg.startswith(('-j', '-l')) or arg in ('-v', '--quiet'):
        i += 1; continue
    if arg.startswith('-'):
        raise SystemExit('Unsupported serial Ninja option: ' + arg)
    targets.append(arg); i += 1

cwd = Path.cwd()
for i, arg in enumerate(options):
    if arg == '-C': cwd = (cwd / options[i+1]).resolve()

def query(tool):
    return subprocess.check_output([real, *options, '-t', *tool], text=True, encoding='utf-8')

commands = query(['commands', *targets]).splitlines()
ordinary = json.loads(query(['compdb']))
expanded = {e['output']: e['command'] for e in json.loads(query(['compdb', '-x']))}
expansions = {e['command']: expanded[e['output']] for e in ordinary if e['output'] in expanded}
def execute(number, command):
    # ccache may itself need named pipes in the restricted host. Bypass only
    # its launcher, keeping every compiler argument from the generated graph.
    if command.startswith('ccache '):
        command = command[len('ccache '):]
    # Normally Ninja writes these before launching the command.
    match = re.search(r'@("[^"]+"|\S+)', command)
    if match:
        full = expansions.get(command)
        if full is None: raise SystemExit('No response-file expansion for: ' + command)
        before, after = command[:match.start()], command[match.end():]
        if not full.startswith(before) or (after and not full.endswith(after)):
            raise SystemExit('Unexpected response-file layout: ' + command)
        content = full[len(before):len(full)-len(after) if after else None]
        response = cwd / match[1].strip('"')
        response.parent.mkdir(parents=True, exist_ok=True)
        response.write_text(content, encoding='utf-8')
    if '/bootloader/subproject' in command and '-GNinja' in command:
        shim = Path(__file__).with_name('ninja_serial.cmd').resolve().as_posix()
        command = command.replace('-GNinja', '-DCMAKE_MAKE_PROGRAM=' + shim + ' -GNinja')
    print(f'[serial {number}/{len(commands)}] {command[:190]}', flush=True)
    # CMake can emit a bare generated .bat command (e.g. linker script). The
    # Windows CreateProcess API cannot execute it directly; use cmd explicitly.
    if re.match(r'(?:"[^"]+\.bat"|\S+\.bat)(?:\s|$)', command, re.IGNORECASE):
        command = os.environ.get('COMSPEC', r'C:\Windows\System32\cmd.exe') + ' /d /c ' + command
    result = subprocess.run(command, cwd=cwd, timeout=180)
    if result.returncode: raise subprocess.CalledProcessError(result.returncode, command)

# Only independent, contiguous compiler edges are parallelized. Every generator,
# archive and link edge is a fence, preserving Ninja's topological ordering.
jobs = max(1, min(8, int(os.environ.get('GW_BUILD_JOBS', '1'))))
with ThreadPoolExecutor(max_workers=jobs) as workers:
    pending = []
    for number, command in enumerate(commands, 1):
        if ' -c ' in command and not command.startswith(('cmd', 'C:\\WINDOWS')):
            pending.append(workers.submit(execute, number, command))
        else:
            for task in pending: task.result()
            pending.clear()
            execute(number, command)
    for task in pending: task.result()
