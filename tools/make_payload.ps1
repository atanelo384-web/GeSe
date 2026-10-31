# Packs the built GeSe (exe, DLLs, Main\, Plugins\) into one LZMS-compressed blob
# that GeSe_Setup.exe embeds as an RCDATA resource.
param([string]$Bin = "$PSScriptRoot\..\build\bin", [string]$Out = "$PSScriptRoot\..\build\payload")
$ErrorActionPreference = 'Stop'
$Bin = (Resolve-Path $Bin).Path
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Lzms {
    [DllImport("cabinet.dll", SetLastError = true)] static extern bool CreateCompressor(uint alg, IntPtr alloc, out IntPtr h);
    [DllImport("cabinet.dll", SetLastError = true)] static extern bool Compress(IntPtr h, byte[] data, UIntPtr size, byte[] outBuf, UIntPtr outSize, out UIntPtr compSize);
    [DllImport("cabinet.dll")] static extern bool CloseCompressor(IntPtr h);
    public static byte[] Pack(byte[] data) {
        IntPtr h;
        if (!CreateCompressor(5, IntPtr.Zero, out h)) throw new Exception("CreateCompressor failed");
        try {
            UIntPtr need;
            Compress(h, data, (UIntPtr)data.Length, null, UIntPtr.Zero, out need);
            byte[] buf = new byte[(long)need.ToUInt64() + 1024];
            UIntPtr got;
            if (!Compress(h, data, (UIntPtr)data.Length, buf, (UIntPtr)buf.Length, out got)) throw new Exception("Compress failed " + Marshal.GetLastWin32Error());
            byte[] r = new byte[(long)got.ToUInt64()];
            Array.Copy(buf, r, r.Length);
            return r;
        } finally { CloseCompressor(h); }
    }
}
"@

$files = @()
$files += Get-ChildItem $Bin -File | Where-Object { $_.Extension -in '.exe', '.dll' -and $_.Name -ne 'GeSe_Setup.exe' }
foreach ($d in 'runtime', 'resources', 'plugins', 'licenses') { $files += Get-ChildItem (Join-Path $Bin $d) -Recurse -File }

$blob = Join-Path $Out 'payload.bin'
$fs = [System.IO.File]::Create($blob)
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([System.Text.Encoding]::ASCII.GetBytes('GESEPAK1'))
$bw.Write([UInt32]$files.Count)
$rawTotal = 0; $packTotal = 0
foreach ($f in $files) {
    $rel = $f.FullName.Substring($Bin.Length).TrimStart('\').Replace('\', '/')
    $data = [System.IO.File]::ReadAllBytes($f.FullName)
    $packed = [Lzms]::Pack($data)
    $method = 1
    if ($packed.Length -ge $data.Length) { $packed = $data; $method = 0 }
    $pb = [System.Text.Encoding]::UTF8.GetBytes($rel)
    $bw.Write([UInt16]$pb.Length); $bw.Write($pb)
    $bw.Write([UInt64]$data.Length); $bw.Write([UInt64]$packed.Length); $bw.Write([Byte]$method)
    $bw.Write($packed)
    $rawTotal += $data.Length; $packTotal += $packed.Length
}
$bw.Close()
$rc = "101 RCDATA `"$($blob.Replace('\','/'))`"`n1 ICON `"$((Resolve-Path "$PSScriptRoot\..\res\gese.ico").Path.Replace('\','/'))`"`n"
Set-Content -Path (Join-Path $Out 'payload.rc') -Value $rc -Encoding ascii
"{0} files, {1:N1} MB -> {2:N1} MB" -f $files.Count, ($rawTotal / 1MB), ($packTotal / 1MB)
