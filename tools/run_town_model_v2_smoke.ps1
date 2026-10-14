param([Parameter(Mandatory=$true)][string]$OutputDirectory)

# Standalone parser check; no shared build cache, game data or installed profile.
$ErrorActionPreference = 'Stop'
$taskSource = Split-Path -Parent $PSScriptRoot
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskVswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $taskVs) { throw 'MSVC x64 is required.' }
$taskVcvars = Join-Path (@($taskVs)[0].Trim()) 'VC\Auxiliary\Build\vcvars64.bat'
$taskSetup = 'call "' + $taskVcvars + '" >nul && set'
$taskEnvironment = & $env:ComSpec /d /s /c $taskSetup
if ($LASTEXITCODE -ne 0) { throw 'Could not prepare private compiler environment.' }
foreach ($taskLine in $taskEnvironment) {
    if ($taskLine -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$taskExecutable = Join-Path $taskOutput 'town_model_v2_smoke.exe'
$taskInputs = @((Join-Path $taskSource 'Source\engine\render\town_model_v2.cpp'), (Join-Path $taskSource 'tools\town_model_v2_smoke.cpp'))
Push-Location -LiteralPath $taskOutput
try {
    $taskArguments = @('/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', '/O2', '/fp:precise', ('/I' + (Join-Path $taskSource 'Source')), ('/external:I' + (Join-Path $taskSource '3rdParty\PicoSHA2')), '/external:W0', ('/Fe:' + $taskExecutable)) + $taskInputs
    & cl.exe @taskArguments *> (Join-Path $taskOutput 'compile.log')
    if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath (Join-Path $taskOutput 'compile.log'); throw 'Private v2 compilation failed.' }
    & $taskExecutable *> (Join-Path $taskOutput 'synthetic.log')
    if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath (Join-Path $taskOutput 'synthetic.log'); throw 'Private v2 checks failed.' }
    Get-Content -LiteralPath (Join-Path $taskOutput 'synthetic.log')
    [ordered]@{ scope = 'Standalone synthetic v2 parser; no runtime integration'; compiler = (Get-Command cl.exe).Source; executableSha256 = (Get-FileHash -LiteralPath $taskExecutable).Hash; sources = @($taskInputs | ForEach-Object { [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $_).Hash } }) } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskOutput 'receipt.json') -Encoding UTF8
} finally { Pop-Location }
