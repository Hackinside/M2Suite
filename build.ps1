# M2Suite build script with visible progress.
#
# Usage (from any PowerShell prompt):
#   .\build.ps1                # full GUI build (windows-msvc preset)
#   .\build.ps1 -Preset core-msvc   # fast core libs + tests only
#   .\build.ps1 -Test          # also run ctest afterwards
#   .\build.ps1 -Package       # also stage dist\M2Suite-portable + .zip
#
# Progress: ninja prints "[n/total]" per compile step live in this console;
# everything is also logged to build\last-build.log. First-ever configure
# builds Qt via vcpkg (hours, one-time — later runs restore from the binary
# cache in minutes).

param(
    [string]$Preset = "windows-msvc",
    [switch]$Test,
    [switch]$Package
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

if ($Package) {
    if ($Preset -eq "core-msvc") {
        Write-Host "-Package needs the GUI preset (core-msvc builds no app)" -ForegroundColor Red
        exit 1
    }
    Write-Host "== staging portable package ==" -ForegroundColor Cyan
    $appDir = Join-Path $root "build\$Preset\apps\m2suite-shell"
    $dist   = Join-Path $root "dist\M2Suite-portable"
    $zip    = Join-Path $root "dist\M2Suite-portable.zip"

    # Rebuild the staging folder from scratch: copying over a previous
    # release would silently keep DLLs that are no longer needed, and a
    # stale Qt plugin is the kind of thing that only fails on someone
    # else's machine.
    if (Test-Path -LiteralPath $dist) { Remove-Item -LiteralPath $dist -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dist | Out-Null

    # The build tree already has the Qt DLLs and plugin folders deployed
    # beside the exe (see apps/m2suite-shell/CMakeLists.txt), so the
    # package is that directory minus build litter.
    # Allow-list rather than deny-list: a deny-list silently ships whatever
    # the build system starts emitting next. The 25 MB .pdb and CMake's
    # install scripts got in that way the first time.
    $skipDirs = @('CMakeFiles', 'm2suite-shell_autogen', 'generated')
    $keepExt  = @('.dll', '.exe', '.conf', '.txt')
    Get-ChildItem -LiteralPath $appDir |
        Where-Object {
            if ($_.PSIsContainer) { $_.Name -notin $skipDirs }
            else { $_.Extension -in $keepExt }
        } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $dist -Recurse -Force }

    # A log from the developer's own runs must not ship.
    Remove-Item -LiteralPath (Join-Path $dist "m2suite.log") -Force -ErrorAction SilentlyContinue

    foreach ($doc in @("README.txt", "CHANGELOG.txt")) {
        $srcDoc = Join-Path $root "dist\$doc"
        if (Test-Path -LiteralPath $srcDoc) { Copy-Item -LiteralPath $srcDoc -Destination $dist -Force }
    }

    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $dist '*') -DestinationPath $zip -CompressionLevel Optimal

    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    $mb = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)
    Write-Host "PACKAGE OK  dist\M2Suite-portable.zip  ($mb MB)" -ForegroundColor Green
    Write-Host "SHA256: $hash"
}
