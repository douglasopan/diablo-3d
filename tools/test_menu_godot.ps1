param(
    [string]$ExportPath = (Join-Path $PSScriptRoot '..\editor\godot\local\tests\menu-layout.ini'),
    [string]$EvidencePath
)

$ErrorActionPreference = 'Stop'
$taskValidator = Join-Path $PSScriptRoot 'Menu-Godot.ps1'
$taskFixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('d3d-menu-layout-test-' + [Guid]::NewGuid().ToString('N'))
$taskForbiddenProfile = Join-Path $taskFixtureRoot 'profile-must-stay-absent'
$taskFiles = [Collections.Generic.List[string]]::new()
$taskResults = [Collections.Generic.List[object]]::new()
$taskUtf8 = [Text.UTF8Encoding]::new($false)
$taskExportHash = $null

if (-not (Test-Path -LiteralPath $taskValidator -PathType Leaf)) { throw 'Validador Menu-Godot.ps1 ausente.' }
New-Item -ItemType Directory -Path $taskFixtureRoot | Out-Null

function Test-LayoutCase {
    param([string]$Name, [string]$Path, [bool]$ExpectedSuccess, [string]$ExpectedError = '')

    $taskAccepted = $false
    $taskError = ''
    $taskHashBefore = if (Test-Path -LiteralPath $Path -PathType Leaf) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash } else { $null }
    try {
        $taskOutput = @(& $taskValidator -Acao Aplicar -LayoutPath $Path -ProfileDirectory $taskForbiddenProfile -ValidateOnly)
        $taskAccepted = $true
    } catch {
        $taskError = $_.Exception.Message
    }
    $taskHashAfter = if (Test-Path -LiteralPath $Path -PathType Leaf) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash } else { $null }
    $taskProfileAbsent = -not (Test-Path -LiteralPath $taskForbiddenProfile)
    $taskPassed = $taskAccepted -eq $ExpectedSuccess -and $taskProfileAbsent -and $taskHashBefore -eq $taskHashAfter
    if (-not $ExpectedSuccess -and $ExpectedError) { $taskPassed = $taskPassed -and $taskError.Contains($ExpectedError) }
    if ($ExpectedSuccess) { $taskPassed = $taskPassed -and ($taskOutput -join ' ').Contains('nenhum arquivo instalado') }
    $taskResults.Add([pscustomobject]@{
        name = $Name
        passed = $taskPassed
        accepted = $taskAccepted
        expected_success = $ExpectedSuccess
        error = $taskError
        input_unchanged = $taskHashBefore -eq $taskHashAfter
        profile_absent = $taskProfileAbsent
    })
    if ($taskPassed) { Write-Output "PASS $Name" } else { Write-Output "FAIL $Name" }
}

$taskBase = @'
[Layout]
format=d3d.ui-layout
schemaVersion=1

[MenuLogo]
anchor=center
offsetX=-290
offsetY=-240
width=580
height=154

[MenuList]
anchor=center
offsetX=-255
offsetY=-48
width=510
height=258
'@

try {
    # The real export is read and validated in place; even the destination path
    # points into this fresh fixture, never at an installed player profile.
    if (Test-Path -LiteralPath $ExportPath -PathType Leaf) {
        $taskExportHash = (Get-FileHash -LiteralPath $ExportPath -Algorithm SHA256).Hash
        Test-LayoutCase -Name 'real-godot-export' -Path $ExportPath -ExpectedSuccess $true
    } else {
        Write-Output 'NOTE: real export unavailable; generated contract fixtures are still checked.'
    }

    $taskCases = @(
        @{ Name = 'valid-contract'; Content = $taskBase; Accept = $true; Error = '' },
        @{ Name = 'utf8-bom'; Content = [string][char]0xFEFF + $taskBase; Accept = $true; Error = '' },
        @{ Name = 'crlf-lines'; Content = ($taskBase -replace '\r?\n', "`r`n"); Accept = $true; Error = '' },
        @{ Name = 'leading-ascii-whitespace'; Content = "`t; comentario`n" + $taskBase.Replace('[MenuList]', " `t[MenuList]").Replace('offsetY=-48', " `toffsetY `t= `t-48"); Accept = $true; Error = '' },
        @{ Name = 'header-section-case'; Content = $taskBase.Replace('[Layout]', '[layout]'); Accept = $false; Error = 'Cabeçalho incompatível' },
        @{ Name = 'header-key-case'; Content = $taskBase.Replace('schemaVersion=1', 'SchemaVersion=1'); Accept = $false; Error = 'Cabeçalho incompatível' },
        @{ Name = 'logo-section-case'; Content = $taskBase.Replace('[MenuLogo]', '[menulogo]'); Accept = $false; Error = 'Elementos do menu ausentes' },
        @{ Name = 'list-section-case'; Content = $taskBase.Replace('[MenuList]', '[menulist]'); Accept = $false; Error = 'Elementos do menu ausentes' },
        @{ Name = 'anchor-key-case'; Content = $taskBase.Replace('anchor=center', 'Anchor=center'); Accept = $false; Error = 'Elemento incompatível' },
        @{ Name = 'dimension-key-case'; Content = $taskBase.Replace('width=510', 'Width=510'); Accept = $false; Error = 'Dimensão inválida' },
        @{ Name = 'positive-sign'; Content = $taskBase.Replace('offsetY=-48', 'offsetY=+1'); Accept = $false; Error = 'Dimensão inválida' },
        @{ Name = 'integer-overflow'; Content = $taskBase.Replace('offsetY=-48', 'offsetY=9999999999999999'); Accept = $false; Error = 'Dimensão inválida' },
        @{ Name = 'trailing-value-space'; Content = $taskBase.Replace('offsetY=-48', 'offsetY=-48 '); Accept = $false; Error = 'Dimensão inválida' },
        @{ Name = 'trailing-header-tab'; Content = $taskBase.Replace('schemaVersion=1', "schemaVersion=1`t"); Accept = $false; Error = 'Cabeçalho incompatível' },
        @{ Name = 'trailing-anchor-space'; Content = $taskBase.Replace('anchor=center', 'anchor=center '); Accept = $false; Error = 'Elemento incompatível' },
        @{ Name = 'trailing-section-space'; Content = $taskBase.Replace('[MenuList]', '[MenuList] '); Accept = $false; Error = 'Linha inválida' },
        @{ Name = 'unicode-leading-space'; Content = $taskBase.Replace('offsetY=-48', ('offsetY=' + [char]0xA0 + '-48')); Accept = $false; Error = 'Dimensão inválida' },
        @{ Name = 'hash-comment'; Content = "# comment`n" + $taskBase; Accept = $false; Error = 'Linha inválida' },
        @{ Name = 'utf16-file'; Bytes = [Text.Encoding]::Unicode.GetBytes($taskBase); Accept = $false; Error = 'byte nulo' },
        @{ Name = 'invalid-utf8'; Bytes = [byte[]]@(0xFF, 0xFE, 0xFF); Accept = $false; Error = 'UTF-8 válido' },
        @{ Name = 'null-byte'; Content = $taskBase + [char]0; Accept = $false; Error = 'byte nulo' },
        @{ Name = 'wrong-format'; Content = $taskBase.Replace('format=d3d.ui-layout', 'format=other.layout'); Accept = $false; Error = 'Cabeçalho incompatível' },
        @{ Name = 'unsupported-schema'; Content = $taskBase.Replace('schemaVersion=1', 'schemaVersion=2'); Accept = $false; Error = 'Cabeçalho incompatível' },
        @{ Name = 'missing-schema'; Content = ($taskBase -replace '\r?\nschemaVersion=1', ''); Accept = $false; Error = 'Cabeçalho incompatível' },
        @{ Name = 'extra-header-key'; Content = $taskBase.Replace('schemaVersion=1', "schemaVersion=1`nextra=1"); Accept = $false; Error = 'Cabeçalho incompatível' },
        @{ Name = 'duplicate-header'; Content = $taskBase + "`n[Layout]`nformat=d3d.ui-layout`nschemaVersion=1"; Accept = $false; Error = 'Seção duplicada' },
        @{ Name = 'duplicate-header-key'; Content = $taskBase.Replace('schemaVersion=1', "schemaVersion=1`nschemaVersion=1"); Accept = $false; Error = 'Campo duplicado' },
        @{ Name = 'duplicate-menu-section'; Content = $taskBase + "`n[MenuList]`nanchor=center"; Accept = $false; Error = 'Seção duplicada' },
        @{ Name = 'duplicate-menu-key'; Content = $taskBase.Replace('width=510', "width=510`nwidth=510"); Accept = $false; Error = 'Campo duplicado' },
        @{ Name = 'missing-logo'; Content = ($taskBase -replace '(?ms)^\[MenuLogo\]\r?\n.*?(?=^\[|\z)', ''); Accept = $false; Error = 'Elementos do menu ausentes' },
        @{ Name = 'missing-list'; Content = ($taskBase -replace '(?ms)^\[MenuList\]\r?\n.*?(?=^\[|\z)', ''); Accept = $false; Error = 'Elementos do menu ausentes' },
        @{ Name = 'invalid-anchor'; Content = $taskBase.Replace('anchor=center', 'anchor=middle'); Accept = $false; Error = 'Elemento incompatível' },
        @{ Name = 'missing-element-key'; Content = $taskBase.Replace('offsetX=-255', ''); Accept = $false; Error = 'Elemento incompatível' },
        @{ Name = 'extra-element-key'; Content = $taskBase.Replace('width=510', "width=510`nextra=1"); Accept = $false; Error = 'Elemento incompatível' },
        @{ Name = 'negative-width'; Content = $taskBase.Replace('width=510', 'width=-1'); Accept = $false; Error = 'Dimensão inválida' },
        @{ Name = 'zero-height'; Content = $taskBase.Replace('height=258', 'height=0'); Accept = $false; Error = 'Dimensão inválida' },
        @{ Name = 'out-of-range-offset'; Content = $taskBase.Replace('offsetY=-48', 'offsetY=4097'); Accept = $false; Error = 'Dimensão inválida' },
        @{ Name = 'non-integer-dimension'; Content = $taskBase.Replace('width=510', 'width=5e2'); Accept = $false; Error = 'Dimensão inválida' },
        @{ Name = 'wrong-logo-size'; Content = $taskBase.Replace('width=580', 'width=579'); Accept = $false; Error = 'Dimensões do menu fora do contrato' },
        @{ Name = 'menu-too-narrow'; Content = $taskBase.Replace('width=510', 'width=299'); Accept = $false; Error = 'Dimensões do menu fora do contrato' },
        @{ Name = 'menu-too-wide'; Content = $taskBase.Replace('width=510', 'width=641'); Accept = $false; Error = 'Dimensões do menu fora do contrato' },
        @{ Name = 'menu-too-short'; Content = $taskBase.Replace('height=258', 'height=257'); Accept = $false; Error = 'Dimensões do menu fora do contrato' },
        @{ Name = 'menu-too-tall'; Content = $taskBase.Replace('height=258', 'height=385'); Accept = $false; Error = 'Dimensões do menu fora do contrato' },
        @{ Name = 'line-before-header'; Content = "invalid`n" + $taskBase; Accept = $false; Error = 'Linha inválida' },
        @{ Name = 'empty-file'; Content = ''; Accept = $false; Error = 'Tamanho inválido' },
        @{ Name = 'oversized-file'; Content = ('a' * 32769); Accept = $false; Error = 'Tamanho inválido' }
    )
    $taskIndex = 0
    foreach ($taskCase in $taskCases) {
        $taskCasePath = Join-Path $taskFixtureRoot ('{0:D2}-{1}.ini' -f $taskIndex, $taskCase.Name)
        $taskFiles.Add($taskCasePath)
        if ($taskCase.ContainsKey('Bytes')) {
            [IO.File]::WriteAllBytes($taskCasePath, $taskCase.Bytes)
        } else {
            [IO.File]::WriteAllText($taskCasePath, $taskCase.Content, $taskUtf8)
        }
        Test-LayoutCase -Name $taskCase.Name -Path $taskCasePath -ExpectedSuccess $taskCase.Accept -ExpectedError $taskCase.Error
        $taskIndex++
    }
    Test-LayoutCase -Name 'missing-file' -Path (Join-Path $taskFixtureRoot 'missing.ini') -ExpectedSuccess $false -ExpectedError 'Exporte o layout'

    $taskReport = [pscustomobject]@{
        validation_only = $true
        cases = $taskResults.Count
        passed = @($taskResults | Where-Object { $_.passed }).Count
        failed = @($taskResults | Where-Object { -not $_.passed }).Count
        real_export_checked = $null -ne $taskExportHash
        real_export_sha256 = $taskExportHash
        profile_absent = -not (Test-Path -LiteralPath $taskForbiddenProfile)
        results = $taskResults.ToArray()
    }
    if ($EvidencePath) {
        $taskEvidenceDirectory = Split-Path -Parent ([IO.Path]::GetFullPath($EvidencePath))
        New-Item -ItemType Directory -Path $taskEvidenceDirectory -Force | Out-Null
        [IO.File]::WriteAllText([IO.Path]::GetFullPath($EvidencePath), ($taskReport | ConvertTo-Json -Depth 5), $taskUtf8)
    }
    if ($taskReport.failed -ne 0) { throw "$($taskReport.failed) verificações de Menu-Godot falharam." }
    Write-Output "PASS $($taskReport.passed) verificações; somente ValidateOnly, nenhum perfil instalado."
} finally {
    foreach ($taskPath in $taskFiles) {
        if (Test-Path -LiteralPath $taskPath -PathType Leaf) { Remove-Item -LiteralPath $taskPath -Force }
    }
    # No recursive deletion: an unexpected installed file is retained for review.
    try { [IO.Directory]::Delete($taskFixtureRoot) } catch { }
}
