param(
    [string]$InputDirectory = (Get-Location).Path,
    [string]$OutputPath = (Join-Path (Get-Location).Path 'FunASRSubtitle-Setup.exe'),
    [string]$BaseName = 'FunASRSubtitle-Setup.exe'
)

$ErrorActionPreference = 'Stop'
$parts = @(Get-ChildItem -LiteralPath $InputDirectory -Filter "$BaseName.part*" -File |
    Sort-Object { [int]($_.Name -replace '^.*\.part', '') })
if ($parts.Count -eq 0) {
    throw "No installer parts found in $InputDirectory"
}

$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $outputFullPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$buffer = New-Object byte[] (1024 * 1024)
$output = [IO.File]::Open($outputFullPath, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
    foreach ($part in $parts) {
        Write-Host "Appending $($part.Name)"
        $input = [IO.File]::OpenRead($part.FullName)
        try {
            while (($read = $input.Read($buffer, 0, $buffer.Length)) -gt 0) {
                $output.Write($buffer, 0, $read)
            }
        }
        finally {
            $input.Dispose()
        }
    }
}
finally {
    $output.Dispose()
}

$hash = (Get-FileHash -LiteralPath $outputFullPath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "Created: $outputFullPath"
Write-Host "SHA256:  $hash"

$checksumPath = Join-Path $InputDirectory "$BaseName.sha256"
if (Test-Path -LiteralPath $checksumPath) {
    $expected = ((Get-Content -LiteralPath $checksumPath -Raw) -split '\s+')[0].ToLowerInvariant()
    if ($hash -ne $expected) {
        throw "SHA256 mismatch. Expected $expected, got $hash"
    }
    Write-Host 'SHA256:  verified'
}
