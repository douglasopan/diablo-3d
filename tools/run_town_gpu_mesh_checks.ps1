param([string]$OutputDirectory, [string]$GpuSource)

# Private compiler and offscreen GPU fixture. No CMake, launcher or profile writes.
# Run after the backend owner has enabled the matching town_gpu_mesh.hpp contract.
$ErrorActionPreference = 'Stop'
$taskSource = Split-Path -Parent $PSScriptRoot
if (-not $GpuSource) { $GpuSource = Join-Path $taskSource 'Source\engine\render\town_gpu.cpp' }
$taskWorkspace = Split-Path -Parent $taskSource
$taskDiagnosticRoot = [IO.Path]::GetFullPath((Join-Path $taskWorkspace 'diagnostics\gpu-static-mesh'))
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $taskDiagnosticRoot (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
}
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
$taskDiagnosticPrefix = $taskDiagnosticRoot.TrimEnd('\') + '\'
if (-not $taskOutput.StartsWith($taskDiagnosticPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw ('The isolated compiler output must be inside ' + $taskDiagnosticRoot)
}
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
    (Join-Path $taskSource 'tools\town_gpu_mesh_checks.cpp'),
    ([IO.Path]::GetFullPath($GpuSource)),
    (Join-Path $taskSource 'Source\engine\render\town_lighting.cpp'),
    (Join-Path $taskSource 'Source\engine\render\town_camera.cpp')
)
$taskExecutable = Join-Path $taskOutput 'town_gpu_mesh_checks.exe'
Push-Location -LiteralPath $taskOutput
try {
    $taskCompilerArguments = @('/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', '/O2', '/fp:precise', ('/I' + (Join-Path $taskSource 'Source')), ('/Fe:' + $taskExecutable)) + $taskSources + @('d3d11.lib', 'd3dcompiler.lib', 'dxgi.lib')
    & cl.exe @taskCompilerArguments *> (Join-Path $taskOutput 'compile.log')
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath (Join-Path $taskOutput 'compile.log')
        throw 'Private static-mesh check compilation failed.'
    }
    & $taskExecutable *> (Join-Path $taskOutput 'run.log')
    $taskRunExitCode = $LASTEXITCODE
    $taskFiles = @($taskSources) + @(
        (Join-Path $taskSource 'Source\engine\render\town_gpu.hpp'),
        (Join-Path $taskSource 'Source\engine\render\town_gpu_mesh.hpp'),
        (Join-Path $taskSource 'Source\engine\render\town_gpu_mesh_backend.inc'),
        (Join-Path $taskSource 'Source\engine\render\town_lighting.hpp'),
        (Join-Path $taskSource 'Source\engine\render\town_camera.hpp'),
        $PSCommandPath
    )
    [ordered]@{
        scope = 'Synthetic offscreen D3D11 static indexed meshes; explicit diagnostic WARP; no game assets/profile, CMake or game window'
        limitation = 'CPU command/upload and synchronous readback are separate diagnostic times; no GPU timestamps or sustained gameplay FPS'
        compiler = (Get-Command cl.exe).Source
        runExitCode = $taskRunExitCode
        executableSha256 = (Get-FileHash -LiteralPath $taskExecutable -Algorithm SHA256).Hash
        inputs = @($taskFiles | ForEach-Object { $taskHash = Get-FileHash -LiteralPath $_ -Algorithm SHA256; [ordered]@{ path = $taskHash.Path; sha256 = $taskHash.Hash } })
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskOutput 'receipt.json') -Encoding UTF8
    Get-Content -LiteralPath (Join-Path $taskOutput 'run.log')
    if ($taskRunExitCode -ne 0) { throw 'Private static-mesh checks failed; inspect run.log and receipt.json.' }
    Write-Output ('Evidence: ' + $taskOutput)
} finally { Pop-Location }
