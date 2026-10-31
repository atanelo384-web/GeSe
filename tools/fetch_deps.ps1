# Downloads everything GeSe needs that is too big (or not ours) to keep in git:
#   third_party\ffmpeg       FFmpeg shared build (GPL, with Rubber Band / x264 / libvpx / LAME)
#   third_party\onnxruntime  ONNX Runtime + DirectML DLLs and the BiRefNet background-removal model
# Run once after cloning:  powershell -ExecutionPolicy Bypass -File tools\fetch_deps.ps1
param([string]$OrtVersion = '1.24.4', [string]$DmlVersion = '1.15.4',
      [string]$FfmpegUrl = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip')
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Resolve-Path "$PSScriptRoot\.."
$tp = Join-Path $root 'third_party'
$tmp = Join-Path $root 'build\downloads'
New-Item -ItemType Directory -Force $tmp | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Get-File($url, $out) {
    if (Test-Path $out) { return }
    "downloading $url"
    curl.exe -sSL -o $out $url
    if ($LASTEXITCODE) { throw "download failed: $url" }
}
function Expand-Fresh($zip, $dir) {
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $dir)
}

# ---- FFmpeg
$ff = Join-Path $tp 'ffmpeg'
if (-not (Test-Path "$ff\bin")) {
    Get-File $FfmpegUrl "$tmp\ffmpeg.zip"
    Expand-Fresh "$tmp\ffmpeg.zip" "$tmp\ffmpeg"
    $inner = Get-ChildItem "$tmp\ffmpeg" -Directory | Select-Object -First 1
    if (Test-Path $ff) { Remove-Item -Recurse -Force $ff }
    Move-Item $inner.FullName $ff
}
"FFmpeg: $ff"

# ---- ONNX Runtime (DirectML) + DirectML
$ort = Join-Path $tp 'onnxruntime'
New-Item -ItemType Directory -Force "$ort\bin", "$ort\include", "$ort\models" | Out-Null
Get-File "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/$OrtVersion/microsoft.ml.onnxruntime.directml.$OrtVersion.nupkg" "$tmp\ort.nupkg"
Get-File "https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/$DmlVersion/microsoft.ai.directml.$DmlVersion.nupkg" "$tmp\dml.nupkg"
Expand-Fresh "$tmp\ort.nupkg" "$tmp\ort"
Expand-Fresh "$tmp\dml.nupkg" "$tmp\dml"
Copy-Item "$tmp\ort\runtimes\win-x64\native\onnxruntime.dll" "$ort\bin" -Force
Copy-Item "$tmp\ort\build\native\include\onnxruntime_c_api.h", "$tmp\ort\build\native\include\onnxruntime_ep_c_api.h" "$ort\include" -Force
Copy-Item "$tmp\ort\LICENSE" "$ort\LICENSE" -Force
Copy-Item "$tmp\dml\bin\x64-win\DirectML.dll" "$ort\bin" -Force
# ONNX Runtime is built with MSVC: ship the VC++ runtime next to it (from the installed VC++ Redistributable)
foreach ($f in 'msvcp140.dll', 'msvcp140_1.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    $src = Join-Path $env:WINDIR "System32\$f"
    if (-not (Test-Path $src)) { throw "$f not found: install the Microsoft Visual C++ Redistributable (x64)" }
    Copy-Item $src "$ort\bin" -Force
}

# ---- BiRefNet (MIT) background-removal model, ONNX export from the rembg project
Get-File 'https://github.com/danielgatis/rembg/releases/download/v0.0.0/BiRefNet-general-bb_swin_v1_tiny-epoch_232.onnx' "$ort\models\birefnet_lite.onnx"
"ONNX Runtime + model: $ort"
"Done. Build with: powershell -ExecutionPolicy Bypass -File tools\build.ps1"
