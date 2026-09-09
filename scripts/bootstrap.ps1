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
$files = @{
    'sensevoice-small-q8.gguf' = 'SenseVoiceSmall-GGUF'
    'fsmn-vad.gguf' = 'fsmn-vad-GGUF'
}
$hashes = @{
    'sensevoice-small-q8.gguf' = '4ae45c94422de949b387e2e0fb10d7e14e4c42c69db30c3444ecc7d4b844b7c5'
    'fsmn-vad.gguf' = '1270f2559c495f4e7b6e739541151027d360761a3fda43fc147034f5719f5479'
}
foreach ($name in $files.Keys) {
    $dst = "$root\models\$name"
    if (-not (Test-Path $dst)) {
        if ($ModelSource) { Copy-Item (Join-Path $ModelSource $name) "$dst.part" }
        else { Invoke-WebRequest "https://huggingface.co/FunAudioLLM/$($files[$name])/resolve/main/$name" -OutFile "$dst.part" }
        if ((Get-FileHash "$dst.part").Hash.ToLower() -ne $hashes[$name]) { throw "Model hash mismatch: $name" }
        Move-Item -LiteralPath "$dst.part" -Destination $dst
    }
    if ((Get-FileHash $dst).Hash.ToLower() -ne $hashes[$name]) { throw "Existing model differs from validated version: $name" }
}
Write-Host 'Dependencies ready. Run scripts/build.ps1.'
