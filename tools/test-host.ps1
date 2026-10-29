# 主机单元测试（纯逻辑模块，不依赖 ESP-IDF 目标）
# 用法: powershell -ExecutionPolicy Bypass -File tools\test-host.ps1
#
# 依赖：
#   - MinGW-w64 gcc  -> C:\tools\mingw64\bin\gcc.exe（WinLibs 16.2.0，手动解压）
#   - CMake / Ninja  -> ESP-IDF 自带工具目录

$ErrorActionPreference = 'Stop'

$repo = Split-Path $PSScriptRoot -Parent
$gccBin = 'C:\tools\mingw64\bin'
$cmake = 'C:\Espressif\tools\tools\cmake\4.0.3\bin\cmake.exe'
$ctest = 'C:\Espressif\tools\tools\cmake\4.0.3\bin\ctest.exe'
$ninja = 'C:\Espressif\tools\tools\ninja\1.12.1\ninja.exe'
$build = Join-Path $repo 'tests\host\build-host'

$env:PATH = "$gccBin;$env:PATH"

& $cmake -G Ninja -S (Join-Path $repo 'tests\host') -B $build `
    "-DCMAKE_C_COMPILER=$gccBin\gcc.exe" "-DCMAKE_MAKE_PROGRAM=$ninja"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build $build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $ctest --test-dir $build --output-on-failure
exit $LASTEXITCODE
