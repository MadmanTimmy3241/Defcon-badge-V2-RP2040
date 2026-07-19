$cli = "C:\Program Files\Arduino CLI\arduino-cli.exe"
$sketch = $PSScriptRoot
$outDir = Join-Path $sketch "build"

& $cli compile --fqbn rp2040:rp2040:waveshare_rp2040_zero $sketch --output-dir $outDir
