param(
    [string]$IdfPath = $env:IDF_PATH,
    [string]$IdfToolsPath = $env:IDF_TOOLS_PATH,
    [ValidateSet('build-img','build','monitor')][string]$Action = 'build-img',
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
if ($Action -eq 'monitor' -and !$Port) { throw 'Specify -Port COMx for the monitor.' }
$toolArgs += @($Action,'launcher','retro-core')
Push-Location $PSScriptRoot
try {
    & $pythonExe @toolArgs
    if ($LASTEXITCODE -ne 0) { throw "Retro-Go command failed ($LASTEXITCODE)." }
} finally { Pop-Location }
