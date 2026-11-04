"""Build production log/retention sources with reset and corruption injection."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build-host-log'
OUT.mkdir(exist_ok=True)
clang = sorted(Path('C:/Espressif/tools/esp-clang').glob('*/esp-clang/bin/clang.exe'))[-1]
flags = ['--target=x86_64-pc-windows-msvc', '-std=c11', '-ffreestanding', '-fno-builtin',
         '-fno-stack-protector', '-O2', '-Wall', '-Wextra', '-Werror', '-Itests/host/include', '-Imain']
objects = []
for source in ['tests/retained_log_fixture.c', 'main/log_buffer.c']:
    obj = OUT / (Path(source).name + '.obj')
    subprocess.run([str(clang), *flags, '-c', source, '-o', str(obj)], cwd=ROOT, check=True)
    objects.append(str(obj))
subprocess.run([str(clang.parent / 'llvm-dlltool.exe'), '-m', 'i386:x86-64', '-d',
                'tests/host/msvcrt.def', '-l', str(OUT / 'msvcrt.lib')], cwd=ROOT, check=True)
exports = ['test_cold_boot', 'test_warm_boot', 'test_invalid_header', 'test_corrupt_latest',
           'test_interrupted_append', 'test_storage_size', 'gw_retained_log_boot',
           'gw_retained_log_count', 'gw_retained_log_boot_number', 'gw_retained_log_read',
           'gw_log_buffer_read', 'gw_log_buffer_append']
subprocess.run([str(clang.parent / 'lld-link.exe'), '/dll', '/noentry', '/nodefaultlib', '/machine:x64',
                *(f'/export:{name}' for name in exports), f'/out:{OUT}/retained-log.dll',
                *objects, str(OUT / 'msvcrt.lib')], cwd=ROOT, check=True)
