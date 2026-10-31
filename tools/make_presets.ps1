# Splits tools\presets.json into Main\Presets\*.gspreset files
$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot\.."
$dst = Join-Path $root 'Main\Presets'
New-Item -ItemType Directory -Force $dst | Out-Null
$all = Get-Content (Join-Path $PSScriptRoot 'presets.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$i = 0
foreach ($p in $all) {
    $i++
    $obj = [ordered]@{ name = $p.name; app = 'GeSe v2.4.0'; effects = @() }
    foreach ($e in $p.effects) {
        $fx = [ordered]@{ id = 900000 + $i * 10; def = $e.def; name = $e.name; on = $true; open = $true }
        if ($e.intensity) { $fx.intensity = $e.intensity } else { $fx.intensity = @{ v = @(100) } }
        $fx.speed = @{ v = @(1) }
        $params = [ordered]@{}
        if ($e.params) { foreach ($prop in $e.params.PSObject.Properties) { $params[$prop.Name] = $prop.Value } }
        $fx.params = $params
        $obj.effects += $fx
    }
    $json = $obj | ConvertTo-Json -Depth 12
    $file = Join-Path $dst ($p.name -replace '[\\/:*?"<>|]', '_')
    [System.IO.File]::WriteAllText("$file.gspreset", $json, (New-Object System.Text.UTF8Encoding $false))
}
"$i presets written to $dst"
