param(
    [string]$DiagnosticsDirectory
)

# Standalone fixture tests for Windows PowerShell 5.1. No game is launched and
# no installed model, game archive, profile, configuration, or save is used.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$taskRepository = Split-Path -Parent $PSScriptRoot
$taskLauncherSource = Join-Path $taskRepository 'Iniciar-Tristram.ps1'
$taskManifestSource = Join-Path $taskRepository 'assets\runtime-baseline.json'
if ([string]::IsNullOrWhiteSpace($DiagnosticsDirectory)) {
    $DiagnosticsDirectory = Join-Path (Split-Path -Parent $taskRepository) 'diagnostics\runtime-baseline-tests'
}
$taskDiagnosticRoot = [System.IO.Path]::GetFullPath($DiagnosticsDirectory)
$taskRepositoryPrefix = [System.IO.Path]::GetFullPath($taskRepository).TrimEnd('\') + '\'
if ($taskDiagnosticRoot.TrimEnd('\') -eq $taskRepositoryPrefix.TrimEnd('\') -or
    $taskDiagnosticRoot.StartsWith($taskRepositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The fixture diagnostics directory must be outside the repository.'
}
$taskRunDirectory = Join-Path $taskDiagnosticRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $taskRunDirectory -Force | Out-Null
$script:taskUnexpectedStarts = 0
$script:taskCaseResults = New-Object System.Collections.Generic.List[object]
$taskUtf8 = New-Object System.Text.UTF8Encoding($false)

# A command shim catches regressions even if a future PrepareOnly path tries
# to launch the dummy executable. It is inherited by the invoked launcher.
function Start-Process {
    [CmdletBinding()]
    param(
        [string]$FilePath,
        [object]$ArgumentList,
        [string]$WorkingDirectory,
        [object]$WindowStyle,
        [switch]$PassThru,
        [switch]$Wait
    )
    $script:taskUnexpectedStarts++
    throw 'Fixture test prevented Start-Process.'
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Assert-Equal {
    param($Expected, $Actual, [string]$Message)
    if ($Expected -cne $Actual) { throw ($Message + ': expected [' + $Expected + '], got [' + $Actual + ']') }
}

function Write-FixtureText {
    param([string]$Path, [string]$Text)
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [System.IO.File]::WriteAllText($Path, $Text, $taskUtf8)
}

function Get-FixtureHash {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-FixtureFingerprint {
    param([string]$Root)
    $taskRootPrefix = $Root.TrimEnd('\') + '\'
    $taskEntries = @(Get-ChildItem -LiteralPath $Root -Force -Recurse | Sort-Object FullName | ForEach-Object {
        $taskRelative = $_.FullName.Substring($taskRootPrefix.Length)
        if ($_.PSIsContainer) { 'directory:' + $taskRelative }
        else { 'file:' + $taskRelative + ':' + (Get-FixtureHash $_.FullName) }
    })
    return [string]::Join("`n", [string[]]$taskEntries)
}

function Write-FixtureModel {
    param([string]$Path)
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    $taskStream = New-Object System.IO.MemoryStream
    $taskWriter = New-Object System.IO.BinaryWriter($taskStream)
    try {
        $taskWriter.Write([System.Text.Encoding]::ASCII.GetBytes('D3DMESH1'))
        $taskWriter.Write([uint32]1) # One triangle.
        $taskWriter.Write([uint32]2) # Two RGB pixels.
        $taskWriter.Write([uint32]1)
        foreach ($taskCoordinate in @(0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1)) {
            $taskWriter.Write([single]$taskCoordinate)
        }
        $taskWriter.Write([byte[]]@(10, 20, 30, 40, 50, 60))
        $taskWriter.Flush()
        [System.IO.File]::WriteAllBytes($Path, $taskStream.ToArray())
    } finally {
        $taskWriter.Dispose()
        $taskStream.Dispose()
    }
}

function Invoke-FixturePrepare {
    param($Fixture, [hashtable]$Options = @{}, [switch]$ExpectFailure)
    $taskStartCount = $script:taskUnexpectedStarts
    $taskCaught = $null
    try {
        & $Fixture.Launcher -DataDirectory $Fixture.DataDirectory -PrepareOnly @Options *> $null
    } catch {
        $taskCaught = $_
    }
    Assert-Equal $taskStartCount $script:taskUnexpectedStarts 'PrepareOnly attempted to start a process'
    if ($ExpectFailure) {
        Assert-True ($null -ne $taskCaught) 'Expected launcher rejection'
        return $taskCaught.Exception.Message
    }
    if ($null -ne $taskCaught) { throw $taskCaught }
}

function Invoke-FixtureCase {
    param([string]$Name, [scriptblock]$Test)
    try {
        & $Test
        $script:taskCaseResults.Add([pscustomobject]@{ name = $Name; passed = $true; detail = 'ok' })
        Write-Host ('PASS ' + $Name)
    } catch {
        $script:taskCaseResults.Add([pscustomobject]@{ name = $Name; passed = $false; detail = $_.Exception.Message })
        Write-Host ('FAIL ' + $Name + ': ' + $_.Exception.Message)
    }
}

function Write-FixtureManifest {
    param($Fixture)
    Write-FixtureText $Fixture.ManifestPath ($Fixture.Manifest | ConvertTo-Json -Depth 12)
}

function New-TestFixture {
    param([string]$Name, [switch]$Shareware)
    $taskFixtureRoot = Join-Path $taskRunDirectory $Name
    New-Item -ItemType Directory -Path $taskFixtureRoot | Out-Null
    $taskLauncher = Join-Path $taskFixtureRoot 'Iniciar-Tristram.ps1'
    Copy-Item -LiteralPath $taskLauncherSource -Destination $taskLauncher
    $taskFixtureManifest = Get-Content -LiteralPath $taskManifestSource -Raw | ConvertFrom-Json
    $taskFixtureModel = Join-Path $taskFixtureRoot $taskFixtureManifest.sourcePath
    $taskFixtureLighting = Join-Path $taskFixtureRoot $taskFixtureManifest.lightingSourcePath
    Write-FixtureModel $taskFixtureModel
    Write-FixtureText $taskFixtureLighting "[Lighting]`r`nExposure=1.0`r`nFixture=synthetic-only`r`n"
    $taskFixtureManifest.sourceSha256 = Get-FixtureHash $taskFixtureModel
    $taskFixtureManifest.lightingSha256 = Get-FixtureHash $taskFixtureLighting
    $taskFixtureManifestPath = Join-Path $taskFixtureRoot 'assets\runtime-baseline.json'
    $taskFixtureData = Join-Path $taskFixtureRoot 'data'
    $taskArchiveName = if ($Shareware) { 'spawn.mpq' } else { 'DIABDAT.MPQ' }
    Write-FixtureText (Join-Path $taskFixtureData $taskArchiveName) 'Synthetic archive existence marker; never opened by a game.'
    Write-FixtureText (Join-Path $taskFixtureRoot 'build\devilutionx-tristram-v4.exe') 'Synthetic v4 executable existence marker; never executed.'
    Write-FixtureText (Join-Path $taskFixtureRoot 'build\devilutionx-tristram-quality.exe') 'Synthetic quality executable existence marker; never executed.'
    $taskOriginalProfile = Join-Path $taskFixtureRoot 'perfil-tristram'
    $taskOriginalIni = "[Graphics]`r`nWidth=777`r`nHeight=555`r`nFullscreen=0`r`nCustom Fixture=preserve-this`r`n`r`n[Audio]`r`nMusic Volume=34`r`n`r`n[Network]`r`nPlayerName=synthetic-fixture`r`n"
    Write-FixtureText (Join-Path $taskOriginalProfile 'diablo.ini') $taskOriginalIni
    Write-FixtureText (Join-Path $taskOriginalProfile 'single_0.sv') 'Synthetic full-game save sentinel.'
    Write-FixtureText (Join-Path $taskOriginalProfile 'spawn_0.sv') 'Synthetic shareware save sentinel.'
    $taskFixture = [pscustomobject]@{
        Root = $taskFixtureRoot
        Launcher = $taskLauncher
        Manifest = $taskFixtureManifest
        ManifestPath = $taskFixtureManifestPath
        DataDirectory = $taskFixtureData
        ModelSource = $taskFixtureModel
        LightingSource = $taskFixtureLighting
        SourceProfile = $taskOriginalProfile
        OriginalIni = $taskOriginalIni
    }
    Write-FixtureManifest $taskFixture
    return $taskFixture
}

function Get-ProfilePath {
    param($Fixture, [string]$Presentation = 'Atual', [switch]$MeshyReview, [switch]$QualityReview)
    if ($QualityReview -and $Presentation -eq 'Atual') { $Presentation = 'Suave' }
    if ($Presentation -eq 'Atual') {
        $taskProfileName = if ($MeshyReview) { 'perfil-meshy-review' } else { 'perfil-tristram' }
    } else {
        $taskProfileName = 'perfil-apresentacao-' + $Presentation.ToLowerInvariant()
        if ($QualityReview) { $taskProfileName += '-qualidade' }
        if ($MeshyReview) { $taskProfileName += '-meshy' }
    }
    return Join-Path $Fixture.Root $taskProfileName
}

function Assert-PinnedReceipt {
    param($Fixture, [string]$Profile, [string]$ExecutableName = 'devilutionx-tristram-v4.exe')
    $taskModel = Join-Path $Profile $Fixture.Manifest.runtimePath
    $taskLighting = Join-Path $Profile $Fixture.Manifest.lightingRuntimePath
    $taskReceiptPath = Join-Path $Profile 'runtime-baseline-receipt.json'
    Assert-True (Test-Path -LiteralPath $taskReceiptPath -PathType Leaf) 'Missing baseline receipt'
    $taskReceipt = Get-Content -LiteralPath $taskReceiptPath -Raw | ConvertFrom-Json
    Assert-Equal 1 $taskReceipt.schemaVersion 'Receipt schema'
    Assert-Equal $Fixture.Manifest.assetId $taskReceipt.assetId 'Receipt asset identity'
    Assert-Equal $Fixture.Manifest.revision $taskReceipt.baselineRevision 'Receipt baseline revision'
    Assert-Equal 'review' $taskReceipt.status 'Receipt review status'
    Assert-Equal $false $taskReceipt.accepted 'Fixture must not imply approval'
    Assert-Equal $Fixture.Manifest.pipeline.id $taskReceipt.pipeline.id 'Receipt pipeline identity'
    Assert-Equal $Fixture.Manifest.sourceSha256 (Get-FixtureHash $taskModel) 'Installed model bytes'
    Assert-Equal $Fixture.Manifest.sourceSha256 $taskReceipt.model.expectedSha256 'Receipt expected model hash'
    Assert-Equal (Get-FixtureHash $taskModel) $taskReceipt.model.sha256 'Receipt destination model hash'
    Assert-Equal $Fixture.Manifest.lightingSha256 (Get-FixtureHash $taskLighting) 'Installed lighting bytes'
    Assert-Equal $Fixture.Manifest.lightingSha256 $taskReceipt.lighting.expectedSha256 'Receipt expected lighting hash'
    Assert-Equal (Get-FixtureHash $taskLighting) $taskReceipt.lighting.sha256 'Receipt destination lighting hash'
    Assert-Equal $ExecutableName $taskReceipt.executable.name 'Receipt executable choice'
    Assert-Equal (Get-FixtureHash (Join-Path $Fixture.Root ('build\' + $ExecutableName))) $taskReceipt.executable.sha256 'Receipt executable hash'
    Assert-Equal 10 (@($taskReceipt.graphicsRequested.PSObject.Properties).Count) 'Receipt Graphics key count'
    return $taskReceipt
}

function Assert-RequestedGraphics {
    param($Receipt, [System.Collections.IDictionary]$Expected, [string]$ConfigPath)
    Assert-Equal 'requested-from-final-ini' $Receipt.graphicsScope 'Receipt Graphics scope'
    $taskFinalGraphicsBody = $null
    if (-not [string]::IsNullOrWhiteSpace($ConfigPath)) {
        $taskFinalIni = [System.IO.File]::ReadAllText($ConfigPath)
        $taskFinalGraphicsMatch = [regex]::Match($taskFinalIni, '(?ims)^\[Graphics\][ \t]*\r?\n(.*?)(?=^\[|\z)')
        Assert-True $taskFinalGraphicsMatch.Success 'Final fixture INI has no Graphics section'
        $taskFinalGraphicsBody = $taskFinalGraphicsMatch.Groups[1].Value
    }
    foreach ($taskExpectedGraphics in $Expected.GetEnumerator()) {
        $taskRecordedProperty = $Receipt.graphicsRequested.PSObject.Properties[$taskExpectedGraphics.Key]
        Assert-True ($null -ne $taskRecordedProperty) ('Missing requested Graphics key: ' + $taskExpectedGraphics.Key)
        $taskRecordedValue = $taskRecordedProperty.Value
        Assert-True (($taskRecordedValue -is [int]) -or ($taskRecordedValue -is [long])) ('Requested Graphics value is not an integer: ' + $taskExpectedGraphics.Key)
        Assert-Equal $taskExpectedGraphics.Value $taskRecordedValue ('Requested Graphics value: ' + $taskExpectedGraphics.Key)
        if ($null -ne $taskFinalGraphicsBody) {
            $taskFinalValueMatch = [regex]::Match($taskFinalGraphicsBody, '(?im)^' + [regex]::Escape($taskExpectedGraphics.Key) + '[ \t]*=[ \t]*(-?\d+)[ \t]*\r?$')
            Assert-True $taskFinalValueMatch.Success ('Final INI is missing an explicit Graphics value: ' + $taskExpectedGraphics.Key)
            Assert-Equal ([int]$taskFinalValueMatch.Groups[1].Value) $taskRecordedValue ('Receipt differs from final INI: ' + $taskExpectedGraphics.Key)
        }
    }
}

foreach ($taskPresentation in @('Atual', 'Nitido', 'Suave', 'Amplo')) {
    Invoke-FixtureCase ('baseline-' + $taskPresentation.ToLowerInvariant()) {
        $taskFixture = New-TestFixture ('baseline-' + $taskPresentation.ToLowerInvariant())
        $taskBeforeIniHash = Get-FixtureHash (Join-Path $taskFixture.SourceProfile 'diablo.ini')
        $taskBeforeSaveHash = Get-FixtureHash (Join-Path $taskFixture.SourceProfile 'single_0.sv')
        Invoke-FixturePrepare $taskFixture @{ Presentation = $taskPresentation }
        $taskProfile = Get-ProfilePath $taskFixture -Presentation $taskPresentation
        $taskReceipt = Assert-PinnedReceipt $taskFixture $taskProfile
        Assert-True $taskReceipt.model.sourceAvailable 'Private model source availability'
        Assert-True $taskReceipt.lighting.sourceAvailable 'Pinned lighting source availability'
        Assert-Equal $taskBeforeSaveHash (Get-FixtureHash (Join-Path $taskProfile 'single_0.sv')) 'Save clone changed bytes'
        if ($taskPresentation -eq 'Atual') {
            Assert-Equal $taskBeforeIniHash (Get-FixtureHash (Join-Path $taskProfile 'diablo.ini')) 'Atual changed existing INI'
        } else {
            Assert-True (-not (Test-Path -LiteralPath (Join-Path $taskProfile 'spawn_0.sv'))) 'Full-game profile copied a shareware save'
        }
    }
}

Invoke-FixtureCase 'quality-review-uses-same-baseline' {
    $taskFixture = New-TestFixture 'quality-review'
    Invoke-FixturePrepare $taskFixture @{ QualityReview = $true }
    $taskProfile = Get-ProfilePath $taskFixture -QualityReview
    $null = Assert-PinnedReceipt $taskFixture $taskProfile 'devilutionx-tristram-quality.exe'
    $taskIni = [System.IO.File]::ReadAllText((Join-Path $taskProfile 'diablo.ini'))
    Assert-True ($taskIni -match '(?m)^3D Edge Smoothing=1\r?$') 'Quality review did not prepare edge smoothing'
    $taskReceipt = Get-Content -LiteralPath (Join-Path $taskProfile 'runtime-baseline-receipt.json') -Raw | ConvertFrom-Json
    Assert-Equal 0 $taskReceipt.graphicsRequested.'3D GPU Rendering' 'Quality review must not implicitly enable GPU rendering'
}

foreach ($taskGpu in @(0, 1)) {
    foreach ($taskSmoothing in @(0, 1)) {
        Invoke-FixtureCase ('normal-launch-persisted-gpu-' + $taskGpu + '-smoothing-' + $taskSmoothing) {
            $taskFixture = New-TestFixture ('normal-persisted-gpu-' + $taskGpu + '-smoothing-' + $taskSmoothing)
            $taskExpectedGraphics = [ordered]@{
                'Width' = 1024
                'Height' = 768
                'Fullscreen' = 1
                'Fit to Screen' = 0
                'Upscale' = 1
                'Scaling Quality' = 2
                'Integer Scaling' = 0
                'Zoom' = 0
                '3D GPU Rendering' = $taskGpu
                '3D Edge Smoothing' = $taskSmoothing
            }
            $taskIniLines = @('[Graphics]')
            foreach ($taskEntry in $taskExpectedGraphics.GetEnumerator()) { $taskIniLines += $taskEntry.Key + '=' + $taskEntry.Value }
            $taskIniLines += @('', '[Audio]', 'Music Volume=34')
            $taskIniPath = Join-Path $taskFixture.SourceProfile 'diablo.ini'
            Write-FixtureText $taskIniPath ([string]::Join("`r`n", $taskIniLines) + "`r`n")
            $taskBeforeIniHash = Get-FixtureHash $taskIniPath
            Invoke-FixturePrepare $taskFixture @{}
            $taskReceipt = Assert-PinnedReceipt $taskFixture (Get-ProfilePath $taskFixture)
            Assert-Equal $taskBeforeIniHash (Get-FixtureHash $taskIniPath) 'Normal preparation changed a persisted Graphics INI'
            Assert-Equal $false $taskReceipt.qualityReview 'Persisted edge smoothing must not imply the QualityReview flag'
            Assert-RequestedGraphics $taskReceipt $taskExpectedGraphics $taskIniPath
        }
    }
}

Invoke-FixtureCase 'quality-review-records-final-copy-graphics-and-preserves-source' {
    $taskFixture = New-TestFixture 'quality-final-copy-graphics'
    $taskSourceGraphics = [ordered]@{
        'Width' = 1111
        'Height' = 777
        'Fullscreen' = 1
        'Fit to Screen' = 1
        'Upscale' = 0
        'Scaling Quality' = 0
        'Integer Scaling' = 1
        'Zoom' = 1
        '3D GPU Rendering' = 1
        '3D Edge Smoothing' = 0
    }
    $taskIniLines = @('[Graphics]')
    foreach ($taskEntry in $taskSourceGraphics.GetEnumerator()) { $taskIniLines += $taskEntry.Key + '=' + $taskEntry.Value }
    $taskIniLines += @('', '[Audio]', 'Music Volume=34')
    Write-FixtureText (Join-Path $taskFixture.SourceProfile 'diablo.ini') ([string]::Join("`r`n", $taskIniLines) + "`r`n")
    $taskSourceFingerprint = Get-FixtureFingerprint $taskFixture.SourceProfile
    Invoke-FixturePrepare $taskFixture @{ QualityReview = $true }
    $taskProfile = Get-ProfilePath $taskFixture -QualityReview
    Assert-True ($taskProfile -ne $taskFixture.SourceProfile) 'Quality review must prepare a separate profile'
    $taskReceipt = Assert-PinnedReceipt $taskFixture $taskProfile 'devilutionx-tristram-quality.exe'
    Assert-Equal $taskSourceFingerprint (Get-FixtureFingerprint $taskFixture.SourceProfile) 'Quality review changed source INI or saves'
    Assert-Equal (Get-FixtureHash (Join-Path $taskFixture.SourceProfile 'single_0.sv')) (Get-FixtureHash (Join-Path $taskProfile 'single_0.sv')) 'Quality review initial save copy changed bytes'
    Assert-Equal $true $taskReceipt.qualityReview 'QualityReview flag missing from the copy receipt'
    Assert-RequestedGraphics $taskReceipt ([ordered]@{
        'Width' = 960
        'Height' = 540
        'Fullscreen' = 1
        'Fit to Screen' = 0
        'Upscale' = 1
        'Scaling Quality' = 2
        'Integer Scaling' = 0
        'Zoom' = 0
        '3D GPU Rendering' = 1
        '3D Edge Smoothing' = 1
    }) (Join-Path $taskProfile 'diablo.ini')
}

Invoke-FixtureCase 'meshy-review-is-an-isolated-pinned-profile' {
    $taskFixture = New-TestFixture 'meshy-review'
    Invoke-FixturePrepare $taskFixture @{ MeshyReview = $true }
    $taskProfile = Get-ProfilePath $taskFixture -MeshyReview
    $null = Assert-PinnedReceipt $taskFixture $taskProfile
    Assert-Equal (Get-FixtureHash (Join-Path $taskFixture.SourceProfile 'single_0.sv')) (Get-FixtureHash (Join-Path $taskProfile 'single_0.sv')) 'Meshy review save clone'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $taskProfile 'spawn_0.sv'))) 'Meshy profile copied an unrelated save prefix'
}

Invoke-FixtureCase 'shareware-clones-only-spawn-saves' {
    $taskFixture = New-TestFixture 'shareware' -Shareware
    Invoke-FixturePrepare $taskFixture @{ Presentation = 'Suave' }
    $taskProfile = Get-ProfilePath $taskFixture -Presentation 'Suave'
    $null = Assert-PinnedReceipt $taskFixture $taskProfile
    Assert-Equal (Get-FixtureHash (Join-Path $taskFixture.SourceProfile 'spawn_0.sv')) (Get-FixtureHash (Join-Path $taskProfile 'spawn_0.sv')) 'Shareware save clone changed bytes'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $taskProfile 'single_0.sv'))) 'Shareware profile copied a full-game save'
}

Invoke-FixtureCase 'existing-save-and-unrelated-ini-values-survive' {
    $taskFixture = New-TestFixture 'preservation'
    $taskProfile = Get-ProfilePath $taskFixture -Presentation 'Suave'
    Write-FixtureText (Join-Path $taskProfile 'diablo.ini') $taskFixture.OriginalIni
    Write-FixtureText (Join-Path $taskProfile 'single_0.sv') 'Independent destination progress must survive.'
    $taskSaveHash = Get-FixtureHash (Join-Path $taskProfile 'single_0.sv')
    $taskOriginalSourceFingerprint = Get-FixtureFingerprint $taskFixture.SourceProfile
    Invoke-FixturePrepare $taskFixture @{ Presentation = 'Suave' }
    Invoke-FixturePrepare $taskFixture @{ Presentation = 'Suave' }
    $taskReceipt = Assert-PinnedReceipt $taskFixture $taskProfile
    Assert-Equal 'verified-existing' $taskReceipt.model.status 'Second preparation should verify the existing model'
    Assert-Equal $taskSaveHash (Get-FixtureHash (Join-Path $taskProfile 'single_0.sv')) 'Existing destination save was overwritten'
    Assert-Equal $taskOriginalSourceFingerprint (Get-FixtureFingerprint $taskFixture.SourceProfile) 'Source profile was changed'
    $taskIni = [System.IO.File]::ReadAllText((Join-Path $taskProfile 'diablo.ini'))
    Assert-True ($taskIni.Contains("[Audio]`r`nMusic Volume=34`r`n")) 'Audio settings were changed'
    Assert-True ($taskIni.Contains("[Network]`r`nPlayerName=synthetic-fixture`r`n")) 'Network settings were changed'
    Assert-True ($taskIni -match '(?m)^Custom Fixture=preserve-this\r?$') 'Unrelated Graphics setting was changed'
    Assert-True ($taskIni -match '(?m)^Width=960\r?$') 'Requested presentation width was not prepared'
}

Invoke-FixtureCase 'missing-private-source-has-explicit-procedural-fallback' {
    $taskFixture = New-TestFixture 'missing-source-fallback'
    Write-FixtureText (Join-Path $taskFixture.SourceProfile 'diablo.ini') "[Graphics]`r`nCustom Fixture=preserve-this`r`n"
    Remove-Item -LiteralPath $taskFixture.ModelSource
    Invoke-FixturePrepare $taskFixture @{}
    $taskProfile = Get-ProfilePath $taskFixture
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $taskProfile $taskFixture.Manifest.runtimePath))) 'Missing model source unexpectedly installed a model'
    $taskReceipt = Get-Content -LiteralPath (Join-Path $taskProfile 'runtime-baseline-receipt.json') -Raw | ConvertFrom-Json
    Assert-Equal 'fallback-procedural' $taskReceipt.model.status 'Fallback must be explicit in the receipt'
    Assert-Equal $false $taskReceipt.model.sourceAvailable 'Missing private source recorded as available'
    Assert-RequestedGraphics $taskReceipt ([ordered]@{
        'Width' = 640
        'Height' = 480
        'Fullscreen' = 1
        'Fit to Screen' = 1
        'Upscale' = 1
        'Scaling Quality' = 2
        'Integer Scaling' = 0
        'Zoom' = 0
        '3D GPU Rendering' = 0
        '3D Edge Smoothing' = 0
    })
}

Invoke-FixtureCase 'missing-private-source-retains-matching-profile-model' {
    $taskFixture = New-TestFixture 'missing-source-retain'
    $taskProfile = Get-ProfilePath $taskFixture
    $taskDestination = Join-Path $taskProfile $taskFixture.Manifest.runtimePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskDestination) | Out-Null
    Copy-Item -LiteralPath $taskFixture.ModelSource -Destination $taskDestination
    Remove-Item -LiteralPath $taskFixture.ModelSource
    Invoke-FixturePrepare $taskFixture @{}
    $taskReceipt = Assert-PinnedReceipt $taskFixture $taskProfile
    Assert-Equal 'verified-existing' $taskReceipt.model.status 'Matching existing model was not retained'
    Assert-Equal $false $taskReceipt.model.sourceAvailable 'Private source availability is inaccurate'
}

Invoke-FixtureCase 'missing-private-source-can-use-matching-shared-model' {
    $taskFixture = New-TestFixture 'missing-source-shared'
    $taskSharedModel = Join-Path $taskFixture.Root ('build\assets\' + $taskFixture.Manifest.runtimePath)
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskSharedModel) -Force | Out-Null
    Copy-Item -LiteralPath $taskFixture.ModelSource -Destination $taskSharedModel
    Remove-Item -LiteralPath $taskFixture.ModelSource
    Invoke-FixturePrepare $taskFixture @{}
    $taskReceipt = Assert-PinnedReceipt $taskFixture (Get-ProfilePath $taskFixture)
    Assert-Equal $false $taskReceipt.model.sourceAvailable 'Shared fallback must not imply that the private source exists'
}

Invoke-FixtureCase 'meshy-review-requires-private-source-before-mutation' {
    $taskFixture = New-TestFixture 'strict-source'
    Remove-Item -LiteralPath $taskFixture.ModelSource
    $taskBefore = Get-FixtureFingerprint $taskFixture.Root
    $null = Invoke-FixturePrepare $taskFixture @{ MeshyReview = $true } -ExpectFailure
    Assert-Equal $taskBefore (Get-FixtureFingerprint $taskFixture.Root) 'Strict source rejection mutated the fixture'
}

Invoke-FixtureCase 'meshy-review-still-requires-source-when-profile-model-matches' {
    $taskFixture = New-TestFixture 'strict-source-existing-model'
    $taskProfile = Get-ProfilePath $taskFixture -MeshyReview
    $taskDestination = Join-Path $taskProfile $taskFixture.Manifest.runtimePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskDestination) -Force | Out-Null
    Copy-Item -LiteralPath $taskFixture.ModelSource -Destination $taskDestination
    Remove-Item -LiteralPath $taskFixture.ModelSource
    $taskBefore = Get-FixtureFingerprint $taskFixture.Root
    $null = Invoke-FixturePrepare $taskFixture @{ MeshyReview = $true } -ExpectFailure
    Assert-Equal $taskBefore (Get-FixtureFingerprint $taskFixture.Root) 'Strict source rejection changed an existing review profile'
}

Invoke-FixtureCase 'changed-source-hash-rejected-before-profile-mutation' {
    $taskFixture = New-TestFixture 'source-hash-rejection'
    [System.IO.File]::AppendAllText($taskFixture.ModelSource, 'changed synthetic model')
    $taskBefore = Get-FixtureFingerprint $taskFixture.Root
    $null = Invoke-FixturePrepare $taskFixture @{ Presentation = 'Amplo' } -ExpectFailure
    Assert-Equal $taskBefore (Get-FixtureFingerprint $taskFixture.Root) 'Source hash rejection mutated the fixture'
}

Invoke-FixtureCase 'divergent-profile-model-rejected-without-overwrite' {
    $taskFixture = New-TestFixture 'destination-hash-rejection'
    $taskProfile = Get-ProfilePath $taskFixture -Presentation 'Amplo'
    Write-FixtureText (Join-Path $taskProfile $taskFixture.Manifest.runtimePath) 'Independent destination model sentinel.'
    Write-FixtureText (Join-Path $taskProfile 'single_0.sv') 'Destination save sentinel.'
    $taskBefore = Get-FixtureFingerprint $taskFixture.Root
    $null = Invoke-FixturePrepare $taskFixture @{ Presentation = 'Amplo' } -ExpectFailure
    Assert-Equal $taskBefore (Get-FixtureFingerprint $taskFixture.Root) 'Divergent model rejection overwrote a file'
}

Invoke-FixtureCase 'divergent-shared-model-rejected-before-mutation' {
    $taskFixture = New-TestFixture 'shared-hash-rejection'
    Write-FixtureText (Join-Path $taskFixture.Root ('build\assets\' + $taskFixture.Manifest.runtimePath)) 'Divergent shared model sentinel.'
    Remove-Item -LiteralPath $taskFixture.ModelSource
    $taskBefore = Get-FixtureFingerprint $taskFixture.Root
    $null = Invoke-FixturePrepare $taskFixture @{} -ExpectFailure
    Assert-Equal $taskBefore (Get-FixtureFingerprint $taskFixture.Root) 'Shared model rejection mutated the fixture'
}

Invoke-FixtureCase 'changed-lighting-source-rejected-before-profile-mutation' {
    $taskFixture = New-TestFixture 'lighting-hash-rejection'
    [System.IO.File]::AppendAllText($taskFixture.LightingSource, 'changed synthetic lighting')
    $taskBefore = Get-FixtureFingerprint $taskFixture.Root
    $null = Invoke-FixturePrepare $taskFixture @{ Presentation = 'Nitido' } -ExpectFailure
    Assert-Equal $taskBefore (Get-FixtureFingerprint $taskFixture.Root) 'Lighting hash rejection mutated the fixture'
}

Invoke-FixtureCase 'divergent-profile-lighting-rejected-without-overwrite' {
    $taskFixture = New-TestFixture 'lighting-destination-rejection'
    $taskProfile = Get-ProfilePath $taskFixture -Presentation 'Suave'
    Write-FixtureText (Join-Path $taskProfile $taskFixture.Manifest.lightingRuntimePath) 'Independent destination lighting sentinel.'
    $taskBefore = Get-FixtureFingerprint $taskFixture.Root
    $null = Invoke-FixturePrepare $taskFixture @{ Presentation = 'Suave' } -ExpectFailure
    Assert-Equal $taskBefore (Get-FixtureFingerprint $taskFixture.Root) 'Divergent lighting rejection overwrote a file'
}

Invoke-FixtureCase 'missing-lighting-source-is-explicit-in-normal-mode' {
    $taskFixture = New-TestFixture 'missing-lighting-fallback'
    Remove-Item -LiteralPath $taskFixture.LightingSource
    Invoke-FixturePrepare $taskFixture @{}
    $taskProfile = Get-ProfilePath $taskFixture
    $taskReceipt = Get-Content -LiteralPath (Join-Path $taskProfile 'runtime-baseline-receipt.json') -Raw | ConvertFrom-Json
    Assert-Equal 'renderer-defaults' $taskReceipt.lighting.status 'Lighting fallback is not explicit'
    Assert-Equal $false $taskReceipt.lighting.sourceAvailable 'Missing lighting source recorded as available'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $taskProfile $taskFixture.Manifest.lightingRuntimePath))) 'Missing lighting unexpectedly installed an override'
}

Invoke-FixtureCase 'meshy-review-requires-lighting-before-mutation' {
    $taskFixture = New-TestFixture 'strict-lighting'
    Remove-Item -LiteralPath $taskFixture.LightingSource
    $taskBefore = Get-FixtureFingerprint $taskFixture.Root
    $null = Invoke-FixturePrepare $taskFixture @{ MeshyReview = $true } -ExpectFailure
    Assert-Equal $taskBefore (Get-FixtureFingerprint $taskFixture.Root) 'Strict lighting rejection mutated the fixture'
}

$taskFailed = @($script:taskCaseResults | Where-Object { -not $_.passed })
$taskReport = [ordered]@{
    schemaVersion = 1
    fixtureOnly = $true
    gameLaunches = $script:taskUnexpectedStarts
    powerShellVersion = $PSVersionTable.PSVersion.ToString()
    launcherSha256 = Get-FixtureHash $taskLauncherSource
    publicManifestSha256 = Get-FixtureHash $taskManifestSource
    passed = ($taskFailed.Count -eq 0)
    total = $script:taskCaseResults.Count
    failed = $taskFailed.Count
    cases = $script:taskCaseResults.ToArray()
}
$taskReportPath = Join-Path $taskRunDirectory 'results.json'
Write-FixtureText $taskReportPath ($taskReport | ConvertTo-Json -Depth 12)
Write-Host ('Fixture report: ' + $taskReportPath)
if ($taskFailed.Count -ne 0) { throw ('Runtime baseline fixture tests failed: ' + $taskFailed.Count + '/' + $script:taskCaseResults.Count) }
Write-Host ('Runtime baseline fixture tests passed: ' + $script:taskCaseResults.Count + '; no game launches.')
