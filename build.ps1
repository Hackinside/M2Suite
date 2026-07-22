# M2Suite build script with visible progress.
#
# Usage (from any PowerShell prompt):
#   .\build.ps1                # full GUI build (windows-msvc preset)
#   .\build.ps1 -Preset core-msvc   # fast core libs + tests only
#   .\build.ps1 -Test          # also run ctest afterwards
#
# Progress: ninja prints "[n/total]" per compile step live in this console;
# everything is also logged to build\last-build.log. First-ever configure
# builds Qt via vcpkg (hours, one-time — later runs restore from the binary
# cache in minutes).

param(
    [string]$Preset = "windows-msvc",
    [switch]$Test
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
$log = Join-Path $root "build\last-build.log"
New-Item -ItemType Directory -Force (Join-Path $root "build") | Out-Null

Write-Host "== M2Suite build ($Preset) ==" -ForegroundColor Cyan
Write-Host "log: $log"

if ($Preset -eq "core-msvc") {
    & $cmake --preset $Preset 2>&1 | Tee-Object -FilePath $log
    & $cmake --build --preset $Preset 2>&1 | Tee-Object -FilePath $log -Append
} else {
    # Full preset needs the MSVC environment for Ninja.
    cmd /c "call `"$vsDevCmd`" >nul 2>&1 && set `"VCPKG_ROOT=$env:VCPKG_ROOT`" && `"$cmake`" --preset $Preset && `"$cmake`" --build --preset $Preset" 2>&1 |
        Tee-Object -FilePath $log
}
if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED — see $log" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "BUILD OK" -ForegroundColor Green

if ($Test) {
    & $ctest --preset $Preset 2>&1 | Tee-Object -FilePath $log -Append
    if ($LASTEXITCODE -ne 0) {
        Write-Host "TESTS FAILED" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    Write-Host "TESTS OK" -ForegroundColor Green
}

if ($Preset -ne "core-msvc") {
    Write-Host "app: build\$Preset\apps\m2suite-shell\m2suite-shell.exe"
}
