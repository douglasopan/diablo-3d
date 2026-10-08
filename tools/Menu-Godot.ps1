param(
    [ValidateSet('Abrir', 'Aplicar')][string]$Acao = 'Abrir',
    [ValidateSet('Menu', 'HUD')][string]$Interface = 'Menu',
    [string]$ProfileDirectory,
    [string]$LayoutPath,
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent $PSScriptRoot
$taskRoot = if ((Split-Path -Leaf $taskRepository) -eq 'devilutionx') { Split-Path -Parent $taskRepository } else { $taskRepository }
$taskProject = Join-Path $taskRepository 'editor\godot'
if ($Acao -eq 'Abrir') {
    $taskGodot = Join-Path $taskRoot '.tools\godot\Godot_v4.7.2-stable_win64.exe'
    if (-not (Test-Path -LiteralPath $taskGodot -PathType Leaf)) { throw 'Godot ausente. Abra editor/godot/project.godot com o Godot 4 instalado.' }
    $taskScene = if ($Interface -eq 'HUD') { 'res://ui/hud.tscn' } else { 'res://ui/main_menu.tscn' }
    Start-Process -FilePath $taskGodot -ArgumentList @('--editor', '--path', ('"' + $taskProject + '"'), $taskScene) -WorkingDirectory $taskProject -WindowStyle Normal
    return
}
if (-not $ProfileDirectory) { $ProfileDirectory = Join-Path $taskRoot 'perfil-tristram' }
if (-not $LayoutPath) { $LayoutPath = Join-Path $taskProject 'local\ui\layout.ini' }
if (-not (Test-Path -LiteralPath $LayoutPath -PathType Leaf)) { throw 'Exporte o layout pela raiz do menu ou do HUD no Godot antes de aplicar.' }
$taskSource = Get-Item -LiteralPath $LayoutPath
if ($taskSource.Length -eq 0 -or $taskSource.Length -gt 32768) { throw 'Tamanho inválido de layout.' }
$taskSections = [Collections.Hashtable]::new([StringComparer]::Ordinal)
$taskSection = ''
try {
    $taskContent = [Text.UTF8Encoding]::new($false, $true).GetString([IO.File]::ReadAllBytes($taskSource.FullName))
} catch { throw 'Layout precisa estar em UTF-8 válido.' }
if ($taskContent.IndexOf([char]0) -ge 0) { throw 'Layout contém byte nulo.' }
if ($taskContent.StartsWith([string][char]0xFEFF, [StringComparison]::Ordinal)) { $taskContent = $taskContent.Substring(1) }
$taskLines = $taskContent.Split([char]10)
for ($taskLineIndex = 0; $taskLineIndex -lt $taskLines.Length; $taskLineIndex++) {
    $taskLine = $taskLines[$taskLineIndex]
    if ($taskLineIndex -lt $taskLines.Length - 1 -and $taskLine.EndsWith("`r")) { $taskLine = $taskLine.Substring(0, $taskLine.Length - 1) }
    # Match the game's INI loader: leading spaces/tabs are insignificant, but
    # trailing value whitespace and non-ASCII whitespace are part of the value.
    $taskText = $taskLine.TrimStart([char[]]@(' ', "`t"))
    if (-not $taskText -or $taskText.StartsWith(';')) { continue }
    if ($taskText -match '^\[([A-Za-z][A-Za-z0-9]{0,63})\]$') {
        $taskSection = $Matches[1]
        if ($taskSections.ContainsKey($taskSection)) { throw 'Seção duplicada.' }
        $taskSections[$taskSection] = [Collections.Hashtable]::new([StringComparer]::Ordinal)
        continue
    }
    if (-not $taskSection -or $taskText -notmatch '^([A-Za-z][A-Za-z0-9]*)[ \t]*=[ \t]*([^=]+)$') { throw 'Linha inválida no layout.' }
    $taskKey = $Matches[1]; $taskValue = $Matches[2]
    if ($taskSections[$taskSection].ContainsKey($taskKey)) { throw 'Campo duplicado.' }
    $taskSections[$taskSection][$taskKey] = $taskValue
}
if ($taskSections.Count -gt 32 -or -not $taskSections.ContainsKey('Layout') -or
    $taskSections['Layout'].Count -ne 2 -or $taskSections['Layout']['format'] -cne 'd3d.ui-layout' -or
    $taskSections['Layout']['schemaVersion'] -cne '1') { throw 'Cabeçalho incompatível.' }
foreach ($taskName in $taskSections.Keys) {
    if ($taskName -ceq 'Layout') { continue }
    $taskElement = $taskSections[$taskName]
    if ($taskElement.Count -ne 5 -or $taskElement['anchor'] -cnotin @('center', 'bottom-center', 'bottom-right', 'top-right', 'top-left')) { throw 'Elemento incompatível.' }
    foreach ($taskKey in @('offsetX', 'offsetY', 'width', 'height')) {
        $taskNumber = 0
        if ($taskElement[$taskKey] -notmatch '^-?[0-9]+$' -or -not [int]::TryParse($taskElement[$taskKey], [ref]$taskNumber) -or $taskNumber -lt -4096 -or $taskNumber -gt 4096 -or
            ($taskKey -in @('width', 'height') -and $taskNumber -lt 1)) { throw 'Dimensão inválida.' }
    }
}
if (-not $taskSections.ContainsKey('MenuLogo') -or -not $taskSections.ContainsKey('MenuList')) { throw 'Elementos do menu ausentes.' }
if ($taskSections.MenuLogo.width -ne '580' -or $taskSections.MenuLogo.height -ne '154' -or
    [int]$taskSections.MenuList.width -lt 300 -or [int]$taskSections.MenuList.width -gt 640 -or
    [int]$taskSections.MenuList.height -lt 258 -or [int]$taskSections.MenuList.height -gt 384) { throw 'Dimensões do menu fora do contrato v1.' }
if ($ValidateOnly) { Write-Output 'Layout válido; nenhum arquivo instalado.'; return }
if (Get-Process -Name 'devilutionx*' -ErrorAction SilentlyContinue) { throw 'Feche o jogo antes de aplicar. Nenhum processo foi interrompido.' }
$taskDestination = Join-Path $ProfileDirectory 'd3d-ui\layout.ini'
New-Item -ItemType Directory -Path (Split-Path -Parent $taskDestination) -Force | Out-Null
if (Test-Path -LiteralPath $taskDestination) {
    Copy-Item -LiteralPath $taskDestination -Destination ($taskDestination + '.backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
Copy-Item -LiteralPath $taskSource.FullName -Destination ($taskDestination + '.pending')
Move-Item -LiteralPath ($taskDestination + '.pending') -Destination $taskDestination -Force
Write-Output 'Layout do menu e HUD aplicado. Abra novamente o jogo pelo atalho habitual.'
