param(
    [switch]$ConfigureOnly,
    [switch]$SkipConfigure,
    [switch]$WithSmoke,
    [switch]$WithBranding,
    [switch]$WithAnimatedLogoSmoke,
    [string[]]$Targets = @('devilutionx'),
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$ExecutableName = 'devilutionx-tristram-v4',
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$taskNestedSource = Join-Path $taskRoot 'devilutionx'
if (Test-Path -LiteralPath (Join-Path $taskNestedSource 'CMakeLists.txt') -PathType Leaf) {
    $taskSource = $taskNestedSource
} elseif (Test-Path -LiteralPath (Join-Path $taskRoot 'CMakeLists.txt') -PathType Leaf) {
    $taskSource = $taskRoot
} else {
    throw 'Não encontrei CMakeLists.txt na raiz nem em devilutionx. Execute o build.ps1 incluído no clone completo. Consulte docs/BUILDING-D3D.md.'
}
$taskBuild = Join-Path $taskRoot 'build'

$taskVswhereCommand = Get-Command vswhere.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
$taskVswhere = if ($taskVswhereCommand) { $taskVswhereCommand.Source } else {
    Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
}
if (-not (Test-Path -LiteralPath $taskVswhere -PathType Leaf)) {
    throw 'Não encontrei o vswhere do Visual Studio Installer. Instale Visual Studio 2022 ou posterior, ou Build Tools, com Desenvolvimento para desktop com C++ (MSVC x64 e Windows SDK). Consulte docs/BUILDING-D3D.md e https://visualstudio.microsoft.com/visual-cpp-build-tools/.'
}
$taskVsInstallation = & $taskVswhere -latest -products '*' -version '[17.0,)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $taskVsInstallation) {
    throw 'Não encontrei Visual Studio 2022 ou posterior com MSVC x64. No Visual Studio Installer, habilite Desenvolvimento para desktop com C++ e um Windows SDK. Consulte docs/BUILDING-D3D.md e https://visualstudio.microsoft.com/visual-cpp-build-tools/.'
}
$taskVsInstallation = @($taskVsInstallation)[0].Trim()
$taskVcvars = Join-Path $taskVsInstallation 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $taskVcvars -PathType Leaf)) {
    throw "O MSVC encontrado não contém vcvars64.bat: $taskVcvars. Repare a instalação de C++ pelo Visual Studio Installer. Consulte docs/BUILDING-D3D.md e https://visualstudio.microsoft.com/visual-cpp-build-tools/."
}

# Import Visual Studio's compiler environment only into this process.
$taskSetupCommand = 'call "' + $taskVcvars + '" >nul && set'
$taskEnvironment = & $env:ComSpec /d /s /c $taskSetupCommand
if ($LASTEXITCODE -ne 0) {
    throw 'Não consegui preparar o compilador C++ x64. Verifique MSVC e Windows SDK no Visual Studio Installer. Consulte docs/BUILDING-D3D.md e https://visualstudio.microsoft.com/visual-cpp-build-tools/.'
}
foreach ($taskLine in $taskEnvironment) {
    if ($taskLine -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}

function Find-TaskBuildTool([string]$BundledPath, [string]$CommandName, [string]$VisualStudioPath) {
    $taskBundledTool = Join-Path $taskRoot $BundledPath
    if (Test-Path -LiteralPath $taskBundledTool -PathType Leaf) { return $taskBundledTool }
    $taskPathTool = Get-Command $CommandName -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($taskPathTool) { return $taskPathTool.Source }
    $taskVsTool = Join-Path $taskVsInstallation $VisualStudioPath
    if (Test-Path -LiteralPath $taskVsTool -PathType Leaf) { return $taskVsTool }
    return $null
}

$taskCmake = Find-TaskBuildTool '.tools\python\cmake\data\bin\cmake.exe' 'cmake.exe' 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$taskNinja = Find-TaskBuildTool '.tools\python\bin\ninja.exe' 'ninja.exe' 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not $taskCmake) {
    throw 'Não encontrei CMake 3.22 ou posterior. Habilite Ferramentas do C++ CMake para Windows no Visual Studio Installer ou adicione CMake ao PATH. Consulte docs/BUILDING-D3D.md e https://cmake.org/download/.'
}
if (-not $taskNinja) {
    throw 'Não encontrei Ninja. Habilite Ferramentas do C++ CMake para Windows no Visual Studio Installer ou adicione Ninja ao PATH. Consulte docs/BUILDING-D3D.md e https://github.com/ninja-build/ninja/releases.'
}

Write-Host "Código-fonte: $taskSource"
Write-Host "Compilador: $taskVsInstallation"
Write-Host "CMake: $taskCmake"
Write-Host "Ninja: $taskNinja"
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskConfigureLog = Join-Path $taskBuild 'configure.log'
$taskBuildLog = Join-Path $taskBuild 'build.log'
$taskArguments = @(
    '-S', $taskSource, '-B', $taskBuild, '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$taskNinja",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
    '-DNONET=ON', '-DDISABLE_LTO=ON', '-DBUILD_TESTING=OFF',
    '-DCPACK=OFF', '-DBUILD_ASSETS_MPQ=OFF', '-DNOSOUND=OFF',
    "-DTRISTRAM_EXECUTABLE_NAME=$ExecutableName",
    '-DBUILD_SHARED_LIBS=OFF',
    '-DDEVILUTIONX_SYSTEM_SDL2=OFF', '-DDEVILUTIONX_STATIC_SDL2=ON',
    '-DDEVILUTIONX_SYSTEM_ZLIB=OFF', '-DDEVILUTIONX_STATIC_ZLIB=ON',
    '-DDEVILUTIONX_SYSTEM_BZIP2=OFF', '-DDEVILUTIONX_STATIC_BZIP2=ON',
    '-DDEVILUTIONX_SYSTEM_LUA=OFF', '-DDEVILUTIONX_STATIC_LUA=ON',
    '-DDEVILUTIONX_SYSTEM_SDL_IMAGE=OFF', '-DDEVILUTIONX_STATIC_SDL_IMAGE=ON',
    '-DDEVILUTIONX_SYSTEM_SDL_AUDIOLIB=OFF', '-DDEVILUTIONX_STATIC_SDL_AUDIOLIB=ON',
    '-DDEVILUTIONX_SYSTEM_LIBPNG=OFF', '-DDEVILUTIONX_STATIC_LIBPNG=ON',
    '-DDEVILUTIONX_SYSTEM_SHEENBIDI=OFF', '-DDEVILUTIONX_STATIC_SHEENBIDI=ON'
)
if ($WithSmoke) {
    $taskArguments += '-DBUILD_TOWN_VIEW_SMOKE=ON'
}
if ($WithBranding) {
    $taskArguments += '-DBUILD_DIABLO_LOGO_TOOL=ON'
}
if ($WithAnimatedLogoSmoke) {
    $taskArguments += '-DBUILD_ANIMATED_LOGO_SMOKE=ON'
}

if (-not $SkipConfigure) {
    Write-Host "Configurando a compilação para Windows. Registro: $taskConfigureLog"
    $ErrorActionPreference = 'Continue'
    & $taskCmake @taskArguments *> $taskConfigureLog
    $taskExitCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($taskExitCode -ne 0) {
        Get-Content -LiteralPath $taskConfigureLog -Tail 70
        throw "A configuração falhou. Registro completo: $taskConfigureLog. Consulte docs/BUILDING-D3D.md."
    }
    Get-Content -LiteralPath $taskConfigureLog -Tail 12
}
if ($ConfigureOnly) { exit 0 }

Write-Host "Compilando o jogo. Registro: $taskBuildLog"
$ErrorActionPreference = 'Continue'
& $taskCmake --build $taskBuild --target @Targets --parallel $Jobs *> $taskBuildLog
$taskExitCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($taskExitCode -ne 0) {
    Get-Content -LiteralPath $taskBuildLog -Tail 90
    throw "A compilação falhou. Registro completo: $taskBuildLog. Consulte docs/BUILDING-D3D.md."
}
Get-Content -LiteralPath $taskBuildLog -Tail 12
Write-Host "Compilação concluída para: $($Targets -join ', ')"
