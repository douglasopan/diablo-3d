param(
    [string]$DiagnosticsDirectory
)

# Windows PowerShell 5.1 fixtures. All files are synthetic and every process
# launch is intercepted. No real profile, save, game archive or binary is used.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$taskRepository = Split-Path -Parent $PSScriptRoot
$taskLauncherSource = Join-Path $taskRepository 'Godot-Editor.ps1'
if ([string]::IsNullOrWhiteSpace($DiagnosticsDirectory)) {
    $DiagnosticsDirectory = Join-Path (Split-Path -Parent $taskRepository) 'diagnostics\godot-launcher-tests'
}
$taskDiagnosticsRoot = [System.IO.Path]::GetFullPath($DiagnosticsDirectory)
$taskRepositoryPrefix = [System.IO.Path]::GetFullPath($taskRepository).TrimEnd('\') + '\'
if ($taskDiagnosticsRoot.TrimEnd('\') -eq $taskRepositoryPrefix.TrimEnd('\') -or
    $taskDiagnosticsRoot.StartsWith($taskRepositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Fixture diagnostics must be outside the repository.'
}
$taskRunDirectory = Join-Path $taskDiagnosticsRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $taskRunDirectory -Force | Out-Null
$taskUtf8 = New-Object System.Text.UTF8Encoding($false)
$script:taskLaunchRequests = New-Object System.Collections.Generic.List[object]
$script:taskResults = New-Object System.Collections.Generic.List[object]

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
    $taskLaunchRequests.Add([pscustomobject]@{
        FilePath = $FilePath
        Arguments = @($ArgumentList)
        WorkingDirectory = $WorkingDirectory
        WindowStyle = [string]$WindowStyle
    })
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Assert-Equal {
    param($Expected, $Actual, [string]$Message)
    if ($Expected -cne $Actual) { throw $Message }
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
    $taskPrefix = $Root.TrimEnd('\') + '\'
    $taskEntries = @(Get-ChildItem -LiteralPath $Root -Force -Recurse | Sort-Object FullName | ForEach-Object {
        $taskRelative = $_.FullName.Substring($taskPrefix.Length)
        if ($_.PSIsContainer) { 'directory:' + $taskRelative }
        else { 'file:' + $taskRelative + ':' + (Get-FixtureHash $_.FullName) + ':' + $_.LastWriteTimeUtc.Ticks }
    })
    return [string]::Join("`n", [string[]]$taskEntries)
}

function New-Fixture {
    param([string]$Name)
    $taskWorkspace = Join-Path $taskRunDirectory ($Name + '\workspace with spaces')
    $taskRepo = Join-Path $taskWorkspace 'devilutionx'
    $taskProfile = Join-Path $taskWorkspace 'perfil-godot-review'
    $taskLauncher = Join-Path $taskRepo 'Godot-Editor.ps1'
    $taskExecutable = Join-Path $taskWorkspace 'build\devilutionx-tristram-godot.exe'
    $taskData = Join-Path $taskWorkspace 'data with spaces'
    $taskManifest = Join-Path $taskProfile 'd3d-maps\tristram.ini'
    $taskModel = Join-Path $taskProfile 'd3d-models\editor\well.d3d'
    $taskReceiptPath = Join-Path $taskProfile 'godot-pack-receipt.json'
    New-Item -ItemType Directory -Path $taskRepo -Force | Out-Null
    Copy-Item -LiteralPath $taskLauncherSource -Destination $taskLauncher
    Write-FixtureText $taskExecutable 'synthetic-candidate-not-an-executable'
    Write-FixtureText (Join-Path $taskData 'DIABDAT.MPQ') 'synthetic-archive-not-game-data'
    Write-FixtureText $taskManifest "[Scene]`nformat=d3d.town-map`nschemaVersion=1`nedition=retail`ninstance=well`n"
    Write-FixtureText $taskModel 'synthetic-model-for-launcher-hash-only'
    Write-FixtureText (Join-Path $taskProfile 'diablo.ini') "[Graphics]`nWidth=1280`nHeight=720`n"
    Write-FixtureText (Join-Path $taskProfile 'single_99.sv') 'synthetic-save-never-a-playable-save'
    $taskReceipt = [ordered]@{
        format = 'd3d.godot-pack-receipt'
        schemaVersion = 1
        gameExecutableSha256 = Get-FixtureHash $taskExecutable
        manifestSha256 = Get-FixtureHash $taskManifest
        models = @([ordered]@{
            instanceId = 'well'
            path = 'd3d-models/editor/well.d3d'
            sha256 = Get-FixtureHash $taskModel
        })
    }
    Write-FixtureText $taskReceiptPath ($taskReceipt | ConvertTo-Json -Depth 6)
    return [pscustomobject]@{
        Workspace = $taskWorkspace
        Launcher = $taskLauncher
        Profile = $taskProfile
        Executable = $taskExecutable
        Data = $taskData
        Manifest = $taskManifest
        Model = $taskModel
        ReceiptPath = $taskReceiptPath
        Receipt = $taskReceipt
    }
}

function Invoke-FixtureLauncher {
    param($Fixture, [switch]$ExpectFailure, [string]$FailurePattern)
    $taskBefore = Get-FixtureFingerprint $Fixture.Workspace
    $taskStartCount = $script:taskLaunchRequests.Count
    $taskCaught = $null
    try {
        & $Fixture.Launcher -Acao Testar -DataDirectory $Fixture.Data *> $null
    } catch {
        $taskCaught = $_
    }
    Assert-Equal $taskBefore (Get-FixtureFingerprint $Fixture.Workspace) 'Launcher changed a synthetic file or its modification time.'
    if ($ExpectFailure) {
        Assert-True ($null -ne $taskCaught) 'Expected the launcher to reject this fixture.'
        Assert-Equal $taskStartCount $script:taskLaunchRequests.Count 'Rejected fixture attempted to launch a process.'
        if (-not [string]::IsNullOrWhiteSpace($FailurePattern)) {
            Assert-True ($taskCaught.Exception.Message -match $FailurePattern) 'Launcher rejected the fixture for an unexpected reason.'
        }
    } else {
        if ($null -ne $taskCaught) { throw $taskCaught }
        Assert-Equal ($taskStartCount + 1) $script:taskLaunchRequests.Count 'Valid fixture did not make exactly one intercepted launch.'
    }
}

function Invoke-TestCase {
    param([string]$Name, [scriptblock]$Body)
    try {
        $taskFixture = New-Fixture $Name
        & $Body $taskFixture
        $script:taskResults.Add([pscustomobject]@{ name = $Name; passed = $true })
        Write-Host ('PASS ' + $Name)
    } catch {
        $script:taskResults.Add([pscustomobject]@{ name = $Name; passed = $false; failure = $_.Exception.Message })
        Write-Host ('FAIL ' + $Name + ': ' + $_.Exception.Message)
    }
}

$taskOriginalLocalAppData = $env:LOCALAPPDATA
try {
    # The Testar branch needs to locate Python but must never execute it.
    $env:LOCALAPPDATA = Join-Path $taskRunDirectory 'synthetic-local-app-data'
    Write-FixtureText (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe') 'synthetic-python-never-executed'

    Invoke-TestCase 'valid-receipt-and-arguments' {
        param($Fixture)
        Invoke-FixtureLauncher $Fixture
        $taskLaunch = $script:taskLaunchRequests[$script:taskLaunchRequests.Count - 1]
        Assert-Equal $Fixture.Executable $taskLaunch.FilePath 'Launcher selected another executable.'
        Assert-Equal $Fixture.Workspace $taskLaunch.WorkingDirectory 'Unexpected game working directory.'
        Assert-Equal 'Normal' $taskLaunch.WindowStyle 'Interactive game window must be visible.'
        $taskArguments = @($taskLaunch.Arguments)
        Assert-True ($taskArguments -contains '--diablo') 'Retail game mode was not requested.'
        Assert-True (-not ($taskArguments -contains '--spawn')) 'Launcher requested shareware mode.'
        foreach ($taskFlag in @('--save-dir', '--config-dir')) {
            $taskIndex = [array]::IndexOf($taskArguments, $taskFlag)
            Assert-True ($taskIndex -ge 0 -and $taskIndex + 1 -lt $taskArguments.Count) 'Profile argument missing.'
            Assert-Equal ('"' + $Fixture.Profile + '"') $taskArguments[$taskIndex + 1] 'Review profile was not quoted or selected.'
        }
        $taskDataIndex = [array]::IndexOf($taskArguments, '--data-dir')
        Assert-True ($taskDataIndex -ge 0 -and $taskDataIndex + 1 -lt $taskArguments.Count) 'Data argument missing.'
        Assert-Equal ('"' + $Fixture.Data + '"') $taskArguments[$taskDataIndex + 1] 'Data path with spaces was not quoted.'
    }
    Invoke-TestCase 'changed-candidate-rejected' {
        param($Fixture)
        Write-FixtureText $Fixture.Executable 'changed-synthetic-candidate'
        Invoke-FixtureLauncher $Fixture -ExpectFailure -FailurePattern 'executavel diverge'
    }
    Invoke-TestCase 'changed-manifest-rejected' {
        param($Fixture)
        Write-FixtureText $Fixture.Manifest 'changed-synthetic-manifest'
        Invoke-FixtureLauncher $Fixture -ExpectFailure -FailurePattern 'manifesto mudou'
    }
    Invoke-TestCase 'changed-model-rejected' {
        param($Fixture)
        Write-FixtureText $Fixture.Model 'changed-synthetic-model'
        Invoke-FixtureLauncher $Fixture -ExpectFailure -FailurePattern 'modelo mudou'
    }
    Invoke-TestCase 'missing-candidate-rejected' {
        param($Fixture)
        Remove-Item -LiteralPath $Fixture.Executable
        Invoke-FixtureLauncher $Fixture -ExpectFailure -FailurePattern 'Compile o candidato'
    }
    Invoke-TestCase 'old-v4-only-rejected' {
        param($Fixture)
        Remove-Item -LiteralPath $Fixture.Executable
        $taskOldExecutable = Join-Path $Fixture.Workspace 'build\devilutionx-tristram-v4.exe'
        Write-FixtureText $taskOldExecutable 'synthetic-old-v4-without-editor-loader'
        # Even a receipt that matches the old binary cannot authorize fallback.
        $Fixture.Receipt.gameExecutableSha256 = Get-FixtureHash $taskOldExecutable
        Write-FixtureText $Fixture.ReceiptPath ($Fixture.Receipt | ConvertTo-Json -Depth 6)
        Invoke-FixtureLauncher $Fixture -ExpectFailure -FailurePattern 'Compile o candidato'
    }
    Invoke-TestCase 'missing-receipt-rejected' {
        param($Fixture)
        Remove-Item -LiteralPath $Fixture.ReceiptPath
        Invoke-FixtureLauncher $Fixture -ExpectFailure -FailurePattern 'Exporte no Godot'
    }
    Invoke-TestCase 'shareware-only-rejected' {
        param($Fixture)
        Remove-Item -LiteralPath (Join-Path $Fixture.Data 'DIABDAT.MPQ')
        Write-FixtureText (Join-Path $Fixture.Data 'spawn.mpq') 'synthetic-shareware-archive'
        Invoke-FixtureLauncher $Fixture -ExpectFailure -FailurePattern 'requer DIABDAT'
    }
} finally {
    $env:LOCALAPPDATA = $taskOriginalLocalAppData
}

$taskFailures = @($script:taskResults | Where-Object { -not $_.passed })
$taskResultPath = Join-Path $taskRunDirectory 'results.json'
Write-FixtureText $taskResultPath ([ordered]@{
    format = 'd3d.godot-launcher-fixtures'
    schemaVersion = 1
    powershellVersion = $PSVersionTable.PSVersion.ToString()
    syntheticOnly = $true
    actualProcessesLaunched = 0
    interceptedLaunches = $script:taskLaunchRequests.Count
    passed = ($taskFailures.Count -eq 0)
    cases = $script:taskResults.ToArray()
} | ConvertTo-Json -Depth 6)
if ($taskFailures.Count -ne 0) { throw ($taskFailures.Count.ToString() + ' Godot launcher fixtures failed. See ' + $taskResultPath) }
Write-Host ('GODOT_LAUNCHER_FIXTURES ' + $script:taskResults.Count + ' passed; ' + $taskResultPath)
