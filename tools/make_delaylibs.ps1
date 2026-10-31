# Builds MinGW delay-load import libraries for the FFmpeg DLLs, so GeSe.exe can keep every DLL in
# runtime\ (it calls SetDllDirectory before the first FFmpeg call) instead of next to the exe.
param([string]$Ffmpeg = '', [string]$Out = "$PSScriptRoot\..\build\delay", [string]$Toolchain = '')
$ErrorActionPreference = 'Stop'
if (-not $Ffmpeg) {
    $local = Join-Path (Resolve-Path "$PSScriptRoot\..") 'third_party\ffmpeg'
    $Ffmpeg = if (Test-Path "$local\bin") { $local } else { 'C:\ffmpeg' }
}
$gendef = if ($Toolchain) { Join-Path $Toolchain 'bin\gendef.exe' } else { (Get-Command gendef.exe).Source }
$dlltool = if ($Toolchain) { Join-Path $Toolchain 'bin\dlltool.exe' } else { (Get-Command dlltool.exe).Source }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
foreach ($base in 'avformat', 'avcodec', 'avutil', 'swscale', 'swresample', 'avfilter') {
    $dllFile = Get-ChildItem "$Ffmpeg\bin\$base-*.dll" | Select-Object -First 1
    if (-not $dllFile) { throw "$base-*.dll not found in $Ffmpeg\bin (run tools\fetch_deps.ps1)" }
    $dll = $dllFile.BaseName
    $lib = Join-Path $Out "lib$($base)_delay.a"
    if ((Test-Path $lib) -and (Get-Item $lib).LastWriteTime -gt $dllFile.LastWriteTime) { continue }
    Push-Location $Out
    & $gendef $dllFile.FullName | Out-Null
    Pop-Location
    & $dlltool --input-def (Join-Path $Out "$dll.def") --dllname "$dll.dll" --output-delaylib $lib
    if ($LASTEXITCODE) { throw "dlltool failed for $dll" }
    "delay lib: $lib"
}
