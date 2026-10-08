param(
    [string]$DataDirectory,
    [switch]$MeshyReview,
    [ValidateSet('Atual', 'Nitido', 'Suave', 'Amplo')]
    [string]$Presentation = 'Atual',
    [switch]$PrepareOnly
)

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
$taskSavePattern = if (Test-Path -LiteralPath (Join-Path $DataDirectory 'DIABDAT.MPQ') -PathType Leaf) { 'single_*.sv' } else { 'spawn_*.sv' }
if ($MeshyReview) {
    $taskMeshyModel = Join-Path $taskRoot 'models\meshy\cabin-east-v2-multiview\runtime\cabin-east.d3d'
    if (-not (Test-Path -LiteralPath $taskMeshyModel -PathType Leaf)) {
        throw 'O modelo experimental Meshy ainda não foi gerado e convertido nesta instalação. Consulte docs/MESHY-WORKFLOW.md.'
    }
}
$taskSourceProfile = Join-Path $taskRoot $(if ($MeshyReview) { 'perfil-meshy-review' } else { 'perfil-tristram' })
$taskProfile = $taskSourceProfile
if ($Presentation -ne 'Atual') {
    $taskPresentationName = 'perfil-apresentacao-' + $Presentation.ToLowerInvariant()
    if ($MeshyReview) { $taskPresentationName += '-meshy' }
    $taskProfile = Join-Path $taskRoot $taskPresentationName
    if (-not (Test-Path -LiteralPath $taskProfile)) {
        New-Item -ItemType Directory -Path $taskProfile | Out-Null
        $taskCopyProfile = $taskSourceProfile
        if ($MeshyReview -and -not (Test-Path -LiteralPath $taskCopyProfile)) {
            $taskCopyProfile = Join-Path $taskRoot 'perfil-tristram'
        }
        if (Test-Path -LiteralPath (Join-Path $taskCopyProfile 'diablo.ini')) {
            Copy-Item -LiteralPath (Join-Path $taskCopyProfile 'diablo.ini') -Destination $taskProfile
        }
        if (Test-Path -LiteralPath $taskCopyProfile) {
            Get-ChildItem -LiteralPath $taskCopyProfile -File -Filter $taskSavePattern | ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $taskProfile
            }
        }
        Write-Host 'Apresentação: cópia inicial do perfil. O progresso desta comparação fica separado.'
    }
}
if ($MeshyReview) {
    if (-not (Test-Path -LiteralPath $taskProfile)) {
        New-Item -ItemType Directory -Path $taskProfile | Out-Null
        $taskOriginalProfile = Join-Path $taskRoot 'perfil-tristram'
        if (Test-Path -LiteralPath (Join-Path $taskOriginalProfile 'diablo.ini')) {
            Copy-Item -LiteralPath (Join-Path $taskOriginalProfile 'diablo.ini') -Destination $taskProfile
        }
        if (Test-Path -LiteralPath $taskOriginalProfile) {
            Get-ChildItem -LiteralPath $taskOriginalProfile -File -Filter $taskSavePattern | ForEach-Object {
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
if ($Presentation -ne 'Atual') {
    $taskDisplayOptions = [ordered]@{
        'Width' = $(if ($Presentation -eq 'Amplo') { 1280 } else { 960 })
        'Height' = $(if ($Presentation -eq 'Amplo') { 720 } else { 540 })
        'Fit to Screen' = 0
        'Upscale' = 1
        'Zoom' = 0
        'Scaling Quality' = $(if ($Presentation -eq 'Nitido') { 0 } else { 2 })
        'Integer Scaling' = 0
    }
    $taskIniText = [System.IO.File]::ReadAllText($taskConfig)
    $taskGraphicsMatch = [regex]::Match($taskIniText, '(?ims)^\[Graphics\][ \t]*\r?\n(.*?)(?=^\[|\z)')
    if (-not $taskGraphicsMatch.Success) { throw 'O perfil de apresentação não contém a seção Graphics.' }
    $taskGraphicsBody = $taskGraphicsMatch.Groups[1].Value
    foreach ($taskDisplayEntry in $taskDisplayOptions.GetEnumerator()) {
        $taskEntryPattern = '(?im)^' + [regex]::Escape($taskDisplayEntry.Key) + '[ \t]*=[^\r\n]*'
        $taskEntryLine = $taskDisplayEntry.Key + '=' + $taskDisplayEntry.Value
        if ([regex]::IsMatch($taskGraphicsBody, $taskEntryPattern)) {
            $taskGraphicsBody = [regex]::Replace($taskGraphicsBody, $taskEntryPattern, $taskEntryLine)
        } else {
            $taskGraphicsBody = $taskGraphicsBody.TrimEnd("`r", "`n") + "`r`n" + $taskEntryLine + "`r`n"
        }
    }
    $taskGraphicsBlock = "[Graphics]`r`n" + $taskGraphicsBody
    $taskIniText = $taskIniText.Substring(0, $taskGraphicsMatch.Index) + $taskGraphicsBlock + $taskIniText.Substring($taskGraphicsMatch.Index + $taskGraphicsMatch.Length)
    [System.IO.File]::WriteAllText($taskConfig, $taskIniText, [System.Text.UTF8Encoding]::new($false))
    switch ($Presentation) {
        'Nitido' { Write-Host 'Apresentação nítida: ampliação sem suavização, área de jogo em 960 x 540.' }
        'Suave' { Write-Host 'Apresentação suave: filtro na ampliação, área de jogo em 960 x 540.' }
        'Amplo' { Write-Host 'Campo ampliado: mais cenário em 1280 x 720. Objetos e interface podem parecer menores; a fluidez pode diminuir.' }
    }
}
if ($PrepareOnly) {
    Write-Host ('Perfil preparado: ' + $taskProfile)
    return
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
