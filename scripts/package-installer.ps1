param(
    [string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$install = Join-Path $root 'install'
$exe = Join-Path $root "build\$Configuration\FunASRSubtitle.exe"
$models = @{
    'sensevoice-small-q8.gguf' = '4ae45c94422de949b387e2e0fb10d7e14e4c42c69db30c3444ecc7d4b844b7c5'
    'fsmn-vad.gguf' = '1270f2559c495f4e7b6e739541151027d360761a3fda43fc147034f5719f5479'
}

if (-not (Test-Path -LiteralPath $exe)) {
    throw "Missing build output: $exe. Run scripts/build.ps1 first."
}
foreach ($name in $models.Keys) {
    $path = Join-Path $root "models\$name"
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing model: $path. Run scripts/download-models.ps1 first." }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $models[$name]) { throw "Model hash mismatch: $name" }
}

$makensis = @(
    (Get-Command makensis.exe -ErrorAction SilentlyContinue).Source,
    "${env:ProgramFiles(x86)}\NSIS\makensis.exe",
    "${env:ProgramFiles}\NSIS\makensis.exe"
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $makensis) { throw 'NSIS makensis.exe is required to build the installer.' }

New-Item -ItemType Directory -Force -Path $install | Out-Null
$output = Join-Path $install 'FunASRSubtitle-Setup.exe'
Remove-Item -LiteralPath $output -Force -ErrorAction SilentlyContinue
$args = @('/DPROJECT_ROOT=' + $root, '/DOUT_DIR=' + $install, (Join-Path $PSScriptRoot 'installer.nsi'))
$process = Start-Process -FilePath $makensis -ArgumentList $args -NoNewWindow -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "NSIS failed with exit code $($process.ExitCode)" }
if (-not (Test-Path -LiteralPath $output)) { throw "Installer was not created: $output" }
$hash = Get-FileHash -LiteralPath $output -Algorithm SHA256
Set-Content -LiteralPath "$output.sha256" -Value "$($hash.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($output))" -Encoding ascii
Write-Host "Created $output ($((Get-Item $output).Length) bytes)"
Write-Host "SHA256 $($hash.Hash.ToLowerInvariant())"
