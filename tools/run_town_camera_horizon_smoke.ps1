param([string]$OutputDirectory)

# Private, dependency-free diagnostic. Never uses CMake, game assets or profiles.
$ErrorActionPreference = 'Stop'
$taskSource = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $taskSource ('diagnostics\horizon-camera\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskVswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $taskVswhere -PathType Leaf)) { throw 'MSVC with Windows SDK is required for this private diagnostic.' }
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $taskVs) { throw 'No MSVC x64 installation found.' }
$taskVcvars = Join-Path (@($taskVs)[0].Trim()) 'VC\Auxiliary\Build\vcvars64.bat'
$taskSetup = 'call "' + $taskVcvars + '" >nul && set'
$taskEnvironment = & $env:ComSpec /d /s /c $taskSetup
if ($LASTEXITCODE -ne 0) { throw 'Could not prepare private compiler environment.' }
foreach ($taskLine in $taskEnvironment) {
    if ($taskLine -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$taskSources = @(
    (Join-Path $taskSource 'tools\town_camera_horizon_smoke.cpp'),
    (Join-Path $taskSource 'Source\engine\render\town_camera.cpp'),
    (Join-Path $taskSource 'Source\engine\render\town_horizon.cpp')
)
$taskExecutable = Join-Path $taskOutput 'town_camera_horizon_smoke.exe'
$taskCompileLog = Join-Path $taskOutput 'compile.log'
$taskRunLog = Join-Path $taskOutput 'run.log'
Push-Location -LiteralPath $taskOutput
try {
    $taskCompilerArguments = @('/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', '/O2', '/fp:precise', ('/I' + (Join-Path $taskSource 'Source')), ('/Fe:' + $taskExecutable)) + $taskSources
    & cl.exe @taskCompilerArguments *> $taskCompileLog
    if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $taskCompileLog; throw 'Private prototype compilation failed.' }
    & $taskExecutable $taskOutput *> $taskRunLog
    if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $taskRunLog; throw 'Private prototype checks failed.' }
    $taskFiles = @($taskSources) + @(
        (Join-Path $taskSource 'Source\engine\render\town_camera.hpp'),
        (Join-Path $taskSource 'Source\engine\render\town_horizon.hpp'),
        (Join-Path $taskSource 'tools\town_camera_checks.hpp'),
        (Join-Path $taskSource 'tools\town_horizon_checks.hpp'),
        $PSCommandPath
    )
    $taskHashes = @($taskFiles | ForEach-Object { $taskHash = Get-FileHash -LiteralPath $_ -Algorithm SHA256; [ordered]@{ path = $taskHash.Path; sha256 = $taskHash.Hash } })
    [ordered]@{
        scope = 'Private standalone synthetic CPU prototype; no runtime integration or game profile'
        compiler = (Get-Command cl.exe).Source
        executableSha256 = (Get-FileHash -LiteralPath $taskExecutable -Algorithm SHA256).Hash
        inputs = $taskHashes
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskOutput 'receipt.json') -Encoding UTF8
    Get-Content -LiteralPath $taskRunLog
    Write-Output ('Evidence: ' + $taskOutput)
} finally { Pop-Location }
