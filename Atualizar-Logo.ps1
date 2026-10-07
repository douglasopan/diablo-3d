param([string]$DataDirectory)

$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$taskTool = Join-Path $taskRoot 'build\diablo_logo_build.exe'
if (-not (Test-Path -LiteralPath $taskTool -PathType Leaf)) {
    throw 'Compile a ferramenta com build.ps1 -WithBranding -Targets diablo_logo_build.'
}
if ([string]::IsNullOrWhiteSpace($DataDirectory)) {
    $taskGogData = 'C:\Program Files (x86)\GOG Galaxy\Games\Diablo'
    $DataDirectory = if (Test-Path -LiteralPath (Join-Path $taskGogData 'DIABDAT.MPQ')) { $taskGogData } else { Join-Path $taskRoot 'data' }
}
$DataDirectory = (Resolve-Path -LiteralPath $DataDirectory).Path
$taskMode = if (Test-Path -LiteralPath (Join-Path $DataDirectory 'DIABDAT.MPQ')) { 'gog' } else { 'shareware' }
$taskOutput = Join-Path $taskRoot "branding\diablo-3d-$taskMode"
$taskAssets = Join-Path $taskRoot 'build\assets'
$taskNumeral = Join-Path $taskRoot 'branding\numeral-3.png'

& $taskTool $DataDirectory $taskAssets $taskNumeral $taskOutput
if ($LASTEXITCODE -ne 0) { throw 'A montagem ou verificacao do logo falhou; o perfil nao foi atualizado.' }

$taskProfileArt = Join-Path $taskRoot 'perfil-tristram\ui_art'
New-Item -ItemType Directory -Path $taskProfileArt -Force | Out-Null
foreach ($taskName in @('smlogo.pcx', 'logo.pcx')) {
    $taskSource = Join-Path $taskOutput "ui_art\$taskName"
    if (-not (Test-Path -LiteralPath $taskSource -PathType Leaf)) { throw "Logo ausente: $taskName" }
    Copy-Item -LiteralPath $taskSource -Destination (Join-Path $taskProfileArt $taskName) -Force
}
Write-Host 'Diablo 3D instalado no perfil do prototipo. Feche e reabra o jogo para ver o logo.'
