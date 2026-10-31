# GeSe build: compiles GeSe.exe (MinGW-w64 GCC + Ninja), optionally builds the installer.
#   tools\fetch_deps.ps1 first (FFmpeg, ONNX Runtime, the AI model), then:  tools\build.ps1 [-Installer] [-Clean]
# The toolchain is taken from -Toolchain, %GESE_MINGW%, D:\bolupy\toolchains\mingw64 or PATH (in that order).
param([switch]$Installer, [switch]$Clean, [string]$Toolchain = '')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot\.."
if (-not $Toolchain) { $Toolchain = $env:GESE_MINGW }
if (-not $Toolchain -and (Test-Path 'D:\bolupy\toolchains\mingw64\bin\g++.exe')) { $Toolchain = 'D:\bolupy\toolchains\mingw64' }
if ($Toolchain) { $env:PATH = "$Toolchain\bin;$env:PATH" }
if (-not (Get-Command g++.exe -ErrorAction SilentlyContinue)) { throw "MinGW-w64 g++ not found: pass -Toolchain <folder> or set GESE_MINGW" }
$build = Join-Path $root 'build'
if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }
& "$PSScriptRoot\make_delaylibs.ps1" -Out (Join-Path $build 'delay') -Toolchain $Toolchain
cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_RC_COMPILER=windres | Out-Host
if ($LASTEXITCODE) { throw "cmake configure failed" }
cmake --build $build --target GeSe -j 24 | Out-Host
if ($LASTEXITCODE) { throw "build failed" }
"GeSe.exe built: $build\bin\GeSe.exe"
# assets change without code changes (splash, effects, presets): always sync them next to the exe
Copy-Item (Join-Path $root 'Main\*') (Join-Path $build 'bin\resources') -Recurse -Force
Copy-Item (Join-Path $root 'Plugins\*') (Join-Path $build 'bin\plugins') -Recurse -Force
Copy-Item (Join-Path $root 'licenses\*') (Join-Path $build 'bin\licenses') -Recurse -Force
# the old flat layout (DLLs next to the exe, Main\) must not shadow the new one
Get-ChildItem (Join-Path $build 'bin') -Filter *.dll -File | Remove-Item -Force
if (Test-Path (Join-Path $build 'bin\Main')) { Remove-Item -Recurse -Force (Join-Path $build 'bin\Main') }
if ($Installer) {
    & "$PSScriptRoot\make_payload.ps1" -Bin "$build\bin" -Out "$build\payload"
    cmake -S $root -B $build | Out-Host
    cmake --build $build --target GeSe_Setup -j 24 | Out-Host
    if ($LASTEXITCODE) { throw "installer build failed" }
    "Installer built: $build\bin\GeSe_Setup.exe"
}
