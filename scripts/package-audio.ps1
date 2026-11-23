$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$package = Join-Path $build 'audio-package'
$archive = Join-Path $build 'VBCABLE_Driver_Pack46.zip'
$url = 'https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack46.zip'
$sha256 = 'BC44E8974191A5A2427D6C979E2DAB08DBB09925F3F435DFA2F0BA3E45B6EB90'

$exe = Join-Path $build 'Release/HdrBridge.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw 'Compile o HDR Bridge em Release antes de empacotar o audio.'
}

Invoke-WebRequest -Uri $url -OutFile $archive -TimeoutSec 45
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $sha256) {
    throw 'O ZIP oficial de VB-CABLE nao corresponde ao hash da versao revisada.'
}

New-Item -Path $package -ItemType Directory -Force | Out-Null
$driver = Join-Path $package 'VB-CABLE'
Expand-Archive -LiteralPath $archive -DestinationPath $driver -Force
if (-not (Test-Path -LiteralPath (Join-Path $driver 'VBCABLE_Setup_x64.exe') -PathType Leaf)) {
    throw 'O instalador x64 de VB-CABLE nao foi encontrado no pacote.'
}

Copy-Item -LiteralPath $exe -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $package
Copy-Item -LiteralPath (Join-Path $build 'WebView2-LICENSE.txt') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'docs/AUDIO-SETUP.txt') -Destination $package
