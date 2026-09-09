param([string]$ModelSource)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$commit = '803b7fcae893e9caaee3921779628fef83ac0965'
$deps = Join-Path $root 'dependencies'
New-Item -ItemType Directory -Force $deps, "$root\models" | Out-Null
if (-not (Test-Path "$deps\llama.cpp-$commit\ggml\CMakeLists.txt")) {
    $zip = "$deps\llama.zip"
    Invoke-WebRequest "https://codeload.github.com/ggml-org/llama.cpp/zip/$commit" -OutFile $zip
    $expected = '8431e10c4df5877dfc9a0fb6ffe1e88bf4f1f96d93b5d112e90556281584452a'
    if ((Get-FileHash $zip).Hash.ToLower() -ne $expected) { throw 'Dependency archive hash mismatch' }
    Expand-Archive $zip $deps -Force
}
$download = Join-Path $PSScriptRoot 'download-models.ps1'
if ($ModelSource) { & $download -ModelSource $ModelSource -ModelDir (Join-Path $root 'models') }
else { & $download -ModelDir (Join-Path $root 'models') }
Write-Host 'Dependencies ready. Run scripts/build.ps1.'
