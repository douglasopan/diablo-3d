param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$AssetProfile,
    [string]$GameData = 'C:\Program Files (x86)\GOG Galaxy\Games\Diablo',
    [switch]$Quick,
    [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
$taskSource = Split-Path -Parent $PSScriptRoot
$taskRoot = Split-Path -Parent $taskSource
$taskVswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $taskVs) { throw 'MSVC x64 with Windows SDK required.' }
$taskVcvars = Join-Path (@($taskVs)[0].Trim()) 'VC\Auxiliary\Build\vcvars64.bat'
$taskEnvironment = & $env:ComSpec /d /s /c ('call "' + $taskVcvars + '" >nul && set')
if ($LASTEXITCODE -ne 0) { throw 'Could not prepare compiler environment.' }
foreach ($taskLine in $taskEnvironment) {
    if ($taskLine -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$taskArguments = @((Join-Path $PSScriptRoot 'first_person_stage_profile.py'), '--build', (Join-Path $taskRoot 'build'), '--output', $OutputDirectory, '--asset-profile', $AssetProfile, '--game-data', $GameData)
if ($Quick) { $taskArguments += '--quick' }
if ($PrepareOnly) { $taskArguments += '--prepare-only' }
& python.exe @taskArguments
if ($LASTEXITCODE -ne 0) { throw 'Private first-person audit failed; inspect output logs.' }
if (-not $PrepareOnly) {
    & python.exe (Join-Path $PSScriptRoot 'first_person_summarize.py') (Join-Path $OutputDirectory 'first-person-stages.jsonl')
    if ($LASTEXITCODE -ne 0) { throw 'Could not summarize private first-person stage data.' }
}
