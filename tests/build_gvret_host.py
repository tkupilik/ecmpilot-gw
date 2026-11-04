"""Build actual GVRET worker/codec/GW varstore; socket calls delegate to native-test callbacks."""
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build-host-gvret';OUT.mkdir(exist_ok=True)
strip_includes=lambda text: re.sub(r'^#include .*\n','',text,flags=re.M)
fixture=(ROOT/'tests/gvret_fixture.c').read_text()
fixture=fixture.replace('/* ACTUAL_GVRET */',strip_includes((ROOT/'main/gvret.c').read_text()))
(OUT/'fixture.c').write_text(fixture)
clang=sorted(Path('C:/Espressif/tools/esp-clang').glob('*/esp-clang/bin/clang.exe'))[-1]
def run(args):
    result=subprocess.run([str(a) for a in args],cwd=ROOT,capture_output=True,text=True)
    if result.returncode: raise RuntimeError(result.stdout+result.stderr)
flags=['--target=x86_64-pc-windows-msvc','-std=c11','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2',
       '-Itests/host/include','-Imain','-Icomponents/varstore/include']
objects=[]
for source in [OUT/'fixture.c',ROOT/'main/gvret_protocol.c',ROOT/'main/process_data.c',ROOT/'components/varstore/varstore.c']:
    obj=OUT/(source.name+'.obj');run([clang,*flags,'-c',source,'-o',obj]);objects.append(obj)
run([clang.parent/'llvm-dlltool.exe','-m','i386:x86-64','-d','tests/host/msvcrt.def','-l',OUT/'msvcrt.lib'])
exports=['gw_gvret_init','gw_gvret_start','gw_gvret_capture','gw_gvret_protocol_reset','gw_gvret_protocol_feed','gw_gvret_encode',
         'gvret_test_hook','gvret_test_reset','gvret_test_poll','gvret_test_stat','gvret_test_token','gvret_test_stale','gvret_test_config']
run([clang.parent/'lld-link.exe','/dll','/noentry','/nodefaultlib','/machine:x64',
     *(f'/export:{n}' for n in exports),f'/out:{OUT}/gvret-test.dll',*objects,OUT/'msvcrt.lib'])
print('Built actual GVRET server, protocol, GW varstore and variable definitions')
