param([string]$DataDirectory, [switch]$MeshyReview)

$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$taskExecutable = Join-Path $taskRoot 'build\devilutionx-tristram-v4.exe'
if (-not (Test-Path -LiteralPath $taskExecutable -PathType Leaf)) {
    throw 'O executavel ainda nao foi compilado. Execute build.ps1 primeiro.'
}
if ([string]::IsNullOrWhiteSpace($DataDirectory)) {
    $taskGogData = 'C:\Program Files (x86)\GOG Galaxy\Games\Diablo'
    if (Test-Path -LiteralPath (Join-Path $taskGogData 'DIABDAT.MPQ') -PathType Leaf) {
        $DataDirectory = $taskGogData
    } else {
        $DataDirectory = Join-Path $taskRoot 'data'
    }
}
$DataDirectory = (Resolve-Path -LiteralPath $DataDirectory).Path
if (-not ((Test-Path -LiteralPath (Join-Path $DataDirectory 'DIABDAT.MPQ')) -or
          (Test-Path -LiteralPath (Join-Path $DataDirectory 'spawn.mpq')))) {
    throw 'Informe a pasta que contem DIABDAT.MPQ ou spawn.mpq.'
}
$taskProfile = Join-Path $taskRoot $(if ($MeshyReview) { 'perfil-meshy-review' } else { 'perfil-tristram' })
if ($MeshyReview) {
    $taskMeshyModel = Join-Path $taskRoot 'models\meshy\cabin-east-v2-multiview\runtime\cabin-east.d3d'
    if (-not (Test-Path -LiteralPath $taskMeshyModel -PathType Leaf)) {
        throw 'O modelo experimental Meshy ainda não foi gerado e convertido nesta instalação. Consulte docs/MESHY-WORKFLOW.md.'
    }
    if (-not (Test-Path -LiteralPath $taskProfile)) {
        New-Item -ItemType Directory -Path $taskProfile | Out-Null
        $taskOriginalProfile = Join-Path $taskRoot 'perfil-tristram'
        if (Test-Path -LiteralPath (Join-Path $taskOriginalProfile 'diablo.ini')) {
            Copy-Item -LiteralPath (Join-Path $taskOriginalProfile 'diablo.ini') -Destination $taskProfile
        }
        if (Test-Path -LiteralPath $taskOriginalProfile) {
            Get-ChildItem -LiteralPath $taskOriginalProfile -File -Filter 'single_*.sv' | ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $taskProfile
            }
        }
    }
    $taskOverrideDirectory = Join-Path $taskProfile 'd3d-models'
    New-Item -ItemType Directory -Path $taskOverrideDirectory -Force | Out-Null
    Copy-Item -LiteralPath $taskMeshyModel -Destination (Join-Path $taskOverrideDirectory 'cabin-east.d3d') -Force
    Write-Host 'Revisão Meshy: cabana leste experimental. F4 e giro mostram o modelo importado; Home usa a vista original.'
    Write-Host 'Este candidato ainda tem diferenças de janela, telhado e base. O perfil de revisão usa uma cópia inicial do jogador.'
}
New-Item -ItemType Directory -Path $taskProfile -Force | Out-Null
$taskConfig = Join-Path $taskProfile 'diablo.ini'
if (-not (Test-Path -LiteralPath $taskConfig)) {
    @'
[Graphics]
Width=960
Height=540
Fullscreen=0
Fit to Screen=0
Upscale=1
Zoom=0

[GameMode]
Game=2

[Gameplay]
Grab Input=0
'@ | Set-Content -LiteralPath $taskConfig -Encoding utf8
}
Write-Host 'Tristram: F4 alterna original / 3D. Segure o botao do meio para girar e inclinar.'
Write-Host 'Roda: zoom. Shift + botao do meio: deslocar a camera. Home: restaurar.'
$taskGameMode = if (Test-Path -LiteralPath (Join-Path $DataDirectory 'DIABDAT.MPQ')) { '--diablo' } else { '--spawn' }
$taskArguments = @(
    '-n', $taskGameMode,
    '--data-dir', ('"' + ($DataDirectory -replace '(\\+)$', '$1$1') + '"'),
    '--save-dir', ('"' + $taskProfile + '"'),
    '--config-dir', ('"' + $taskProfile + '"'),
    '--log-to-file', ('"' + (Join-Path $taskProfile 'prototipo.log') + '"')
)
$taskGameProcess = Start-Process -FilePath $taskExecutable -ArgumentList $taskArguments -WorkingDirectory $taskRoot -WindowStyle Normal -PassThru -Wait
if ($taskGameProcess.ExitCode -ne 0) { throw "O jogo terminou com codigo $($taskGameProcess.ExitCode)." }
