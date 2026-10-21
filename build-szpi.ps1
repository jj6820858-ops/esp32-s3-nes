param(
    [string]$IdfPath = $env:IDF_PATH,
    [string]$IdfToolsPath = $env:IDF_TOOLS_PATH,
    [ValidateSet('build-img','build','monitor','flash')][string]$Action = 'build-img',
    [string]$Port
)
$ErrorActionPreference = 'Stop'
# This checkout is independent; the sibling runtime is only a local fallback.
if (!$IdfPath) { $IdfPath = Join-Path $PSScriptRoot '..\bootload\tools\runtime\esp-idf' }
if (!$IdfToolsPath) { $IdfToolsPath = Join-Path $PSScriptRoot '..\bootload\tools\runtime\idf-tools' }
$IdfPath = (Resolve-Path -LiteralPath $IdfPath).Path
$IdfToolsPath = (Resolve-Path -LiteralPath $IdfToolsPath).Path
$pythonEnv = Get-ChildItem (Join-Path $IdfToolsPath 'python_env') -Directory |
    Where-Object Name -Like 'idf5.2_*' | Select-Object -First 1
if (!$pythonEnv) { throw 'ESP-IDF 5.2 Python environment not found.' }
$pythonExe = Join-Path $pythonEnv.FullName 'Scripts\python.exe'
$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $IdfToolsPath
$env:IDF_PYTHON_ENV_PATH = $pythonEnv.FullName
$env:PYTHONIOENCODING = 'utf-8'
$toolBins = Get-ChildItem (Join-Path $IdfToolsPath 'tools') -Recurse -Directory |
    Where-Object { $_.Name -eq 'bin' -or $_.Parent.Name -eq 'ninja' } |
    Select-Object -ExpandProperty FullName
$env:PATH = ((@((Join-Path $pythonEnv.FullName 'Scripts')) + $toolBins) -join ';') + ';' + $env:PATH
$toolArgs = @('rg_tool.py','--target=szpi-esp32s3','--no-networking')
if ($Port) { $toolArgs += "--port=$Port" }
if ($Action -in @('monitor','flash') -and !$Port) { throw 'Specify -Port COMx.' }
$toolArgs += @($Action,'launcher','retro-core')
Push-Location $PSScriptRoot
try {
    if ($Action -eq 'flash') {
        # 完整镜像写入 ESP32 的 Flash，与 TF 卡上的分区及文件无关。
        & $pythonExe -m esptool --chip esp32s3 --port $Port --baud 460800 `
            write_flash 0x0 szpi-nes_0.1.0_szpi-esp32s3.img
    } else {
        & $pythonExe @toolArgs
    }
    if ($LASTEXITCODE -ne 0) { throw "Retro-Go command failed ($LASTEXITCODE)." }
} finally { Pop-Location }
