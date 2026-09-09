param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
# MSBuild cannot start CL when a host supplies both PATH and Path entries.
# Normalize only the child environment; keep machine/user settings unchanged.
function Invoke-BuildTool([string]$File, [string[]]$ToolArgs) {
    $info = [Diagnostics.ProcessStartInfo]::new($File)
    $info.UseShellExecute = $false
    $clean = @{}
    foreach ($entry in $info.Environment.GetEnumerator()) { $clean[$entry.Key.ToUpperInvariant()] = $entry.Value }
    $info.Environment.Clear()
    foreach ($key in $clean.Keys) { $info.Environment[$key] = $clean[$key] }
    $info.Environment['MSBUILDDISABLENODEREUSE'] = '1'
    foreach ($arg in $ToolArgs) { $info.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($info)
    $process.WaitForExit()
    if ($process.ExitCode) { throw "$File failed with exit code $($process.ExitCode)" }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools are required.' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
Invoke-BuildTool $cmake @('-S', $root, '-B', "$root\build", '-G', 'Visual Studio 17 2022', '-A', 'x64')
Invoke-BuildTool $cmake @('--build', "$root\build", '--config', $Configuration, '--parallel', '8')
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
Invoke-BuildTool $ctest @('--test-dir', "$root\build", '-C', $Configuration, '--output-on-failure')
