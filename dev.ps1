param(
    [ValidateSet('build','reconfigure','menuconfig','size','flash','monitor')]
    [string]$Action = 'build',
    [string]$Port,
    [string]$IdfPath = $env:IDF_PATH,
    [string]$ToolsPath = $env:IDF_TOOLS_PATH,
    [switch]$EnvironmentOnly
)
$ErrorActionPreference = 'Stop'
if (!$IdfPath) { $IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.3.2' }
if (!$ToolsPath) { $ToolsPath = 'C:\Espressif' }
if (!(Test-Path -LiteralPath "$IdfPath\tools\idf.py")) { throw 'Set IDF_PATH or pass -IdfPath to an ESP-IDF 5.3.x installation.' }
$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $ToolsPath
$taskPython = Join-Path $ToolsPath 'python_env\idf5.3_py3.11_env\Scripts\python.exe'
if ($env:IDF_PYTHON_ENV_PATH) { $taskPython = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe' }
if (!(Test-Path -LiteralPath $taskPython)) { throw 'Activate the ESP-IDF Python environment first (IDF_PYTHON_ENV_PATH).' }
$env:IDF_PYTHON_ENV_PATH = Split-Path (Split-Path $taskPython)
$env:PYTHONDONTWRITEBYTECODE = '1'
$taskExports = & $taskPython "$IdfPath\tools\idf_tools.py" export --format key-value
if ($LASTEXITCODE) { throw 'ESP-IDF tool discovery failed.' }
foreach ($taskLine in $taskExports) {
    $taskPair = $taskLine -split '=', 2
    if ($taskPair.Count -ne 2) { continue }
    if ($taskPair[0] -eq 'PATH') {
        $taskToolPath = $taskPair[1].Replace('%PATH%', '').Replace('$PATH', '').Trim(';')
        $env:PATH = "$taskToolPath;$env:PATH"
    } else { [Environment]::SetEnvironmentVariable($taskPair[0], $taskPair[1], 'Process') }
}
if ($Action -in @('flash','monitor') -and !$Port) { throw 'Select a device explicitly with -Port COMx.' }
if ($EnvironmentOnly) { return }
$taskArgs = @()
if ($Port) { $taskArgs += @('-p', $Port) }
$taskArgs += $Action
Push-Location $PSScriptRoot
try {
    & $taskPython "$IdfPath\tools\idf.py" @taskArgs
    if ($LASTEXITCODE) { throw "idf.py failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }
