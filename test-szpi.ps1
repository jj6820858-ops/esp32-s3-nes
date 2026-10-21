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
    & $Compiler -std=c11 -Wall -Wextra -Werror -I components/retro-go/targets/szpi-esp32s3 `
        tests/test_szpi_partition.c components/retro-go/targets/szpi-esp32s3/szpi_partition.c `
        -o tests/test_szpi_partition.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host partition test compilation failed.' }
    & tests/test_szpi_partition.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host partition tests failed.' }
    $coreSources = Get-ChildItem retro-core/components/nofrendo/nes,retro-core/components/nofrendo/mappers `
        -Filter '*.c' | Select-Object -ExpandProperty FullName
    & $Compiler -std=gnu11 -O2 -I retro-core/components/nofrendo -I retro-core/components/nofrendo/nes `
        tests/test_nes_demo.c @coreSources -lm -o tests/test_nes_demo.exe
    if ($LASTEXITCODE -ne 0) { throw 'NES host core compilation failed.' }
    & tests/test_nes_demo.exe
    if ($LASTEXITCODE -ne 0) { throw 'NES demo execution failed.' }
} finally { Pop-Location }
