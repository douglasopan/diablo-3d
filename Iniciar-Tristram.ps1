param(
    [string]$DataDirectory,
    [switch]$MeshyReview,
    [switch]$QualityReview,
    [ValidateSet('Atual', 'Nitido', 'Suave', 'Amplo')]
    [string]$Presentation = 'Atual',
    [switch]$PrepareOnly
)

$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot

function Resolve-TaskRelativePath([string]$Root, [string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [System.IO.Path]::IsPathRooted($Relative)) {
        throw 'O manifesto deve usar caminhos relativos ao workspace.'
    }
    $taskPrefix = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $taskResolved = [System.IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if (-not $taskResolved.StartsWith($taskPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'O caminho do manifesto sai do workspace permitido.'
    }
    return $taskResolved
}

function Get-TaskHash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TaskSource([string]$Relative, [string]$Expected, [string]$Label) {
    if ($Expected -notmatch '^[0-9a-fA-F]{64}$') { throw ('Hash invalido no manifesto: ' + $Label) }
    $taskPath = Resolve-TaskRelativePath $taskRoot $Relative
    $taskExists = Test-Path -LiteralPath $taskPath -PathType Leaf
    if ((Test-Path -LiteralPath $taskPath) -and -not $taskExists) { throw ('A fonte nao e um arquivo: ' + $taskPath) }
    if ($taskExists -and (Get-TaskHash $taskPath) -ne $Expected.ToLowerInvariant()) {
        throw ('A fonte de ' + $Label + ' diverge do hash selecionado. Nenhum perfil foi alterado: ' + $taskPath)
    }
    return [pscustomobject]@{ path = $taskPath; available = $taskExists; expected = $Expected.ToLowerInvariant() }
}

function Get-TaskAssetPlan($Source, [string]$Destination, [string]$Label, [string]$FallbackPath = '') {
    if (Test-Path -LiteralPath $Destination) {
        if (-not (Test-Path -LiteralPath $Destination -PathType Leaf) -or (Get-TaskHash $Destination) -ne $Source.expected) {
            throw ('O perfil contem ' + $Label + ' divergente; o arquivo foi preservado. Separe esse override antes de selecionar a baseline: ' + $Destination)
        }
        return [pscustomobject]@{ source = $Source; destination = $Destination; copyFrom = $null; status = 'verified-existing' }
    }
    if ($Source.available) {
        return [pscustomobject]@{ source = $Source; destination = $Destination; copyFrom = $Source.path; status = 'installed' }
    }
    if (-not [string]::IsNullOrEmpty($FallbackPath) -and (Test-Path -LiteralPath $FallbackPath)) {
        if (-not (Test-Path -LiteralPath $FallbackPath -PathType Leaf) -or (Get-TaskHash $FallbackPath) -ne $Source.expected) {
            throw ('O asset compartilhado de ' + $Label + ' diverge da baseline e poderia ocultar o fallback: ' + $FallbackPath)
        }
        return [pscustomobject]@{ source = $Source; destination = $Destination; copyFrom = $FallbackPath; status = 'installed' }
    }
    return [pscustomobject]@{ source = $Source; destination = $Destination; copyFrom = $null; status = 'missing' }
}

function Install-TaskAsset($Plan, [string]$Label) {
    if ($null -ne $Plan.copyFrom) {
        if ((Get-TaskHash $Plan.copyFrom) -ne $Plan.source.expected) { throw ('A fonte mudou durante a preparacao: ' + $Label) }
        $taskAssetDirectory = Split-Path -Parent $Plan.destination
        New-Item -ItemType Directory -Path $taskAssetDirectory -Force | Out-Null
        $taskTemporary = Join-Path $taskAssetDirectory ('.baseline-' + [guid]::NewGuid().ToString('N') + '.tmp')
        try {
            Copy-Item -LiteralPath $Plan.copyFrom -Destination $taskTemporary
            if ((Get-TaskHash $taskTemporary) -ne $Plan.source.expected) { throw ('Falha de verificacao da copia: ' + $Label) }
            if (Test-Path -LiteralPath $Plan.destination) { throw ('O destino surgiu durante a copia; preservado: ' + $Plan.destination) }
            Move-Item -LiteralPath $taskTemporary -Destination $Plan.destination
        } finally {
            if (Test-Path -LiteralPath $taskTemporary -PathType Leaf) { Remove-Item -LiteralPath $taskTemporary }
        }
    }
    $taskInstalledHash = $null
    if (Test-Path -LiteralPath $Plan.destination -PathType Leaf) {
        $taskInstalledHash = Get-TaskHash $Plan.destination
        if ($taskInstalledHash -ne $Plan.source.expected) { throw ('O asset instalado mudou durante a preparacao: ' + $Label) }
    }
    return [ordered]@{
        status = $Plan.status
        sha256 = $taskInstalledHash
        expectedSha256 = $Plan.source.expected
        sourceAvailable = $Plan.source.available
    }
}

$taskExecutable = Join-Path $taskRoot 'build\devilutionx-tristram-v4.exe'
if ($QualityReview) {
    $taskQualityExecutable = Join-Path $taskRoot 'build\devilutionx-tristram-quality.exe'
    if (Test-Path -LiteralPath $taskQualityExecutable -PathType Leaf) {
        $taskExecutable = $taskQualityExecutable
    }
    if ($Presentation -eq 'Atual') { $Presentation = 'Suave' }
}
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
$taskManifestPath = Join-Path $taskRoot 'assets\runtime-baseline.json'
if (-not (Test-Path -LiteralPath $taskManifestPath -PathType Leaf)) { throw 'Manifesto assets/runtime-baseline.json ausente; a selecao de modelo nao pode ser verificada.' }
$taskBaseline = [System.IO.File]::ReadAllText($taskManifestPath) | ConvertFrom-Json
if ($taskBaseline.schemaVersion -ne 1 -or $taskBaseline.format -ne 'd3d.runtime-baseline' -or
    $taskBaseline.assetId -ne 'tristram.arch.cabin' -or $taskBaseline.runtimePath -ne 'd3d-models/cabin-east.d3d' -or
    $taskBaseline.lightingRuntimePath -ne 'd3d-lighting.ini' -or [string]::IsNullOrWhiteSpace($taskBaseline.pipeline.id)) {
    throw 'O manifesto de baseline nao corresponde ao contrato da cabana leste.'
}
$taskModelSource = Get-TaskSource $taskBaseline.sourcePath $taskBaseline.sourceSha256 'modelo'
$taskLightingSource = Get-TaskSource $taskBaseline.lightingSourcePath $taskBaseline.lightingSha256 'iluminacao'
if ($MeshyReview -and (-not $taskModelSource.available -or -not $taskLightingSource.available)) {
    throw 'A revisao Meshy exige as fontes selecionadas de modelo e iluminacao. Nenhum perfil foi alterado.'
}
$taskSourceProfile = Join-Path $taskRoot $(if ($MeshyReview) { 'perfil-meshy-review' } else { 'perfil-tristram' })
$taskProfile = $taskSourceProfile
if ($Presentation -ne 'Atual') {
    $taskPresentationName = 'perfil-apresentacao-' + $Presentation.ToLowerInvariant()
    if ($QualityReview) { $taskPresentationName += '-qualidade' }
    if ($MeshyReview) { $taskPresentationName += '-meshy' }
    $taskProfile = Join-Path $taskRoot $taskPresentationName
}
$taskModelDestination = Resolve-TaskRelativePath $taskProfile $taskBaseline.runtimePath
$taskLightingDestination = Resolve-TaskRelativePath $taskProfile $taskBaseline.lightingRuntimePath
$taskSharedModel = Resolve-TaskRelativePath (Join-Path $taskRoot 'build\assets') $taskBaseline.runtimePath
$taskModelPlan = Get-TaskAssetPlan $taskModelSource $taskModelDestination 'modelo' $taskSharedModel
$taskLightingPlan = Get-TaskAssetPlan $taskLightingSource $taskLightingDestination 'iluminacao'
if ((Test-Path -LiteralPath $taskProfile) -and -not (Test-Path -LiteralPath $taskProfile -PathType Container)) { throw 'O caminho do perfil nao e uma pasta.' }
if (-not (Test-Path -LiteralPath $taskProfile -PathType Container)) {
    New-Item -ItemType Directory -Path $taskProfile | Out-Null
    $taskCopyProfile = $null
    if ($Presentation -ne 'Atual') { $taskCopyProfile = $taskSourceProfile }
    elseif ($MeshyReview) { $taskCopyProfile = Join-Path $taskRoot 'perfil-tristram' }
    if ($MeshyReview -and $null -ne $taskCopyProfile -and -not (Test-Path -LiteralPath $taskCopyProfile)) {
        $taskCopyProfile = Join-Path $taskRoot 'perfil-tristram'
    }
    if ($null -ne $taskCopyProfile -and $taskCopyProfile -ne $taskProfile -and (Test-Path -LiteralPath $taskCopyProfile -PathType Container)) {
        if (Test-Path -LiteralPath (Join-Path $taskCopyProfile 'diablo.ini') -PathType Leaf) {
            Copy-Item -LiteralPath (Join-Path $taskCopyProfile 'diablo.ini') -Destination $taskProfile
        }
        Get-ChildItem -LiteralPath $taskCopyProfile -File -Filter $taskSavePattern | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $taskProfile
        }
        Write-Host 'Apresentacao: copia inicial do perfil. O progresso desta comparacao fica separado.'
    }
}
$taskModelReceipt = Install-TaskAsset $taskModelPlan 'modelo'
$taskLightingReceipt = Install-TaskAsset $taskLightingPlan 'iluminacao'
if ($taskModelReceipt.status -eq 'missing') {
    $taskModelReceipt.status = 'fallback-procedural'
    Write-Warning 'Modelo da baseline ausente: este perfil usara a cabana procedural, sem o interior Meshy. Resolucao e suavizacao nao recuperam esse asset.'
} else {
    Write-Host ('Baseline de revisao: ' + $taskBaseline.assetId + ' / ' + $taskBaseline.revision + '. Exterior selecionado para continuar a revisao; nao e aceite final.')
}
if ($taskLightingReceipt.status -eq 'missing') {
    $taskLightingReceipt.status = 'renderer-defaults'
    Write-Warning 'Iluminacao calibrada ausente: serao usados os valores internos do renderer.'
}
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
    if ($QualityReview) { $taskDisplayOptions['3D Edge Smoothing'] = 1 }
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
$taskReceipt = [ordered]@{
    schemaVersion = 1
    recordedUtc = [DateTime]::UtcNow.ToString('o')
    assetId = $taskBaseline.assetId
    variantId = $taskBaseline.variantId
    baselineRevision = $taskBaseline.revision
    status = $taskBaseline.status
    accepted = [bool]$taskBaseline.accepted
    profile = Split-Path -Leaf $taskProfile
    presentation = $Presentation
    qualityReview = [bool]$QualityReview
    model = $taskModelReceipt
    lighting = $taskLightingReceipt
    executable = [ordered]@{ name = Split-Path -Leaf $taskExecutable; sha256 = Get-TaskHash $taskExecutable }
    manifestSha256 = Get-TaskHash $taskManifestPath
    pipeline = $taskBaseline.pipeline
}
$taskReceiptPath = Join-Path $taskProfile 'runtime-baseline-receipt.json'
[System.IO.File]::WriteAllText($taskReceiptPath, ($taskReceipt | ConvertTo-Json -Depth 8), [System.Text.UTF8Encoding]::new($false))
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
