param([string]$Compiler = 'D:\workspace\w64devkit\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Compiler)) {
    $Compiler = (Get-Command gcc -ErrorAction Stop).Source
}
$env:PATH = (Split-Path $Compiler -Parent) + ';' + $env:PATH
Push-Location $PSScriptRoot
try {
    & $Compiler -std=c11 -Wall -Wextra -Werror -I components/retro-go `
        -I components/retro-go/targets/szpi-esp32s3 tests/test_szpi_touch.c `
        components/retro-go/targets/szpi-esp32s3/szpi_touch.c -o tests/test_szpi_touch.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed.' }
    & tests/test_szpi_touch.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host touch tests failed.' }
} finally { Pop-Location }
