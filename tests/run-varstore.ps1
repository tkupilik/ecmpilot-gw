param([string]$ClangPath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
if (!$ClangPath) {
    $compiler = Get-ChildItem 'C:/Espressif/tools/esp-clang/*/esp-clang/bin/clang.exe' -ErrorAction SilentlyContinue | Select-Object -Last 1
    if (!$compiler) { throw 'Pass -ClangPath pointing to a Windows Clang executable.' }
    $ClangPath = $compiler.FullName
}
$linker = Join-Path (Split-Path $ClangPath) 'lld-link.exe'
$out = Join-Path $projectRoot 'build-host-tests'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$dlltool = Join-Path (Split-Path $ClangPath) 'llvm-dlltool.exe'
& $dlltool -m i386:x86-64 -d "$PSScriptRoot/host-kernel32.def" -l "$out/kernel32.lib"
if ($LASTEXITCODE) { throw 'Failed to create the Win32 import library.' }
$common = @('--target=x86_64-pc-windows-msvc','-std=c11','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-Wall','-Wextra','-Werror',"-I$projectRoot/components/varstore/include",'-c')
& $ClangPath @common "$projectRoot/components/varstore/varstore.c" -o "$out/varstore.obj"
if ($LASTEXITCODE) { throw 'Component compilation failed.' }
& $ClangPath @common "$PSScriptRoot/varstore_test.c" -o "$out/tests.obj"
if ($LASTEXITCODE) { throw 'Test compilation failed.' }
& $linker /entry:mainCRTStartup /subsystem:console /nodefaultlib /machine:x64 "/out:$out/varstore-test.exe" "$out/varstore.obj" "$out/tests.obj" "$out/kernel32.lib"
if ($LASTEXITCODE) { throw 'Test linking failed.' }
& "$out/varstore-test.exe"
if ($LASTEXITCODE) { throw 'Varstore tests failed.' }
