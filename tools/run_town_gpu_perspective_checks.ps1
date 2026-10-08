param([string]$OutputDirectory)

# Offscreen-only compiler and GPU diagnostic, isolated from CMake/game builds.
$ErrorActionPreference = 'Stop'
$taskSource = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $taskSource ('diagnostics\horizon-camera\gpu-perspective-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskVswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $taskVswhere -PathType Leaf)) { throw 'MSVC with Windows SDK is required.' }
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $taskVs) { throw 'No MSVC x64 installation found.' }
$taskVcvars = Join-Path (@($taskVs)[0].Trim()) 'VC\Auxiliary\Build\vcvars64.bat'
$taskSetup = 'call "' + $taskVcvars + '" >nul && set'
$taskEnvironment = & $env:ComSpec /d /s /c $taskSetup
if ($LASTEXITCODE -ne 0) { throw 'Could not prepare compiler environment.' }
foreach ($taskLine in $taskEnvironment) {
    if ($taskLine -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$taskSources = @(
    (Join-Path $taskSource 'tools\town_gpu_perspective_checks.cpp'),
    (Join-Path $taskSource 'Source\engine\render\town_gpu.cpp'),
    (Join-Path $taskSource 'Source\engine\render\town_lighting.cpp')
)
$taskExecutable = Join-Path $taskOutput 'town_gpu_perspective_checks.exe'
Push-Location -LiteralPath $taskOutput
try {
    $taskCompilerArguments = @('/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', '/O2', '/fp:precise', ('/I' + (Join-Path $taskSource 'Source')), ('/Fe:' + $taskExecutable)) + $taskSources + @('d3d11.lib', 'd3dcompiler.lib', 'dxgi.lib')
    & cl.exe @taskCompilerArguments *> (Join-Path $taskOutput 'compile.log')
    if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath (Join-Path $taskOutput 'compile.log'); throw 'Private GPU check compilation failed.' }
    & $taskExecutable *> (Join-Path $taskOutput 'run.log')
    if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath (Join-Path $taskOutput 'run.log'); throw 'Private GPU checks failed.' }
    $taskFiles = @($taskSources) + @(
        (Join-Path $taskSource 'Source\engine\render\town_gpu.hpp'),
        (Join-Path $taskSource 'Source\engine\render\town_lighting.hpp'),
        (Join-Path $taskSource 'tools\town_cabin_light_gpu_checks.hpp'),
        $PSCommandPath
    )
    [ordered]@{
        scope = 'Private offscreen D3D11 perspective/orthographic checks; no game assets/profile or CMake'
        compiler = (Get-Command cl.exe).Source
        executableSha256 = (Get-FileHash -LiteralPath $taskExecutable -Algorithm SHA256).Hash
        inputs = @($taskFiles | ForEach-Object { $taskHash = Get-FileHash -LiteralPath $_ -Algorithm SHA256; [ordered]@{ path = $taskHash.Path; sha256 = $taskHash.Hash } })
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskOutput 'receipt.json') -Encoding UTF8
    Get-Content -LiteralPath (Join-Path $taskOutput 'run.log')
    Write-Output ('Evidence: ' + $taskOutput)
} finally { Pop-Location }
