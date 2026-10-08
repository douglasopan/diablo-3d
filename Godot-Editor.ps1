param(
    [ValidateSet('Abrir', 'Preparar', 'Aplicar', 'Testar')]
    [string]$Acao = 'Abrir',
    [string]$DataDirectory,
    [string]$GodotPath,
    [switch]$InstallGodot,
    [switch]$AllowPrototypes
)

$ErrorActionPreference = 'Stop'
$taskRepository = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'devilutionx\tools\godot_bridge.py')) {
    Join-Path $PSScriptRoot 'devilutionx'
} else { $PSScriptRoot }
$taskRoot = if ((Split-Path -Leaf $taskRepository) -eq 'devilutionx') { Split-Path -Parent $taskRepository } else { $taskRepository }
$taskProject = Join-Path $taskRepository 'editor\godot'
$taskProfile = Join-Path $taskRoot 'perfil-godot-review'
$taskExe = Join-Path $taskRoot 'build\devilutionx-tristram-godot.exe'
$taskSmoke = Join-Path $taskRoot 'build\town_view_smoke.exe'
$taskAssets = Join-Path $taskRoot 'build\assets'

if ([string]::IsNullOrWhiteSpace($DataDirectory)) {
    $taskGog = 'C:\Program Files (x86)\GOG Galaxy\Games\Diablo'
    $DataDirectory = if (Test-Path -LiteralPath (Join-Path $taskGog 'DIABDAT.MPQ')) { $taskGog } else { Join-Path $taskRoot 'data' }
}

$taskPython = Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'
if (-not (Test-Path -LiteralPath $taskPython)) {
    $taskPythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $taskPythonCommand) { throw 'Instale Python 3.11 ou posterior com Pillow. Veja docs/GODOT-EDITOR.md.' }
    $taskPython = $taskPythonCommand.Source
}

$taskBridgeArgs = @((Join-Path $taskRepository 'tools\godot_bridge.py'), '--workspace', $taskRoot,
    '--data', $DataDirectory, '--assets', $taskAssets, '--smoke', $taskSmoke, '--executable', $taskExe)
if ($AllowPrototypes) { $taskBridgeArgs += '--allow-prototypes' }
if ($Acao -eq 'Preparar' -or ($Acao -eq 'Abrir' -and -not (Test-Path -LiteralPath (Join-Path $taskProject 'local\town-snapshot.json')))) {
    & $taskPython @taskBridgeArgs prepare
    if ($LASTEXITCODE -ne 0) { throw 'A preparacao do cenario falhou; confira a mensagem acima.' }
    if ($Acao -eq 'Preparar') { return }
}
if ($Acao -eq 'Aplicar') {
    & $taskPython @taskBridgeArgs apply
    if ($LASTEXITCODE -ne 0) { throw 'O pacote nao foi aplicado; confira o erro de validacao acima.' }
    return
}
if ($Acao -eq 'Testar') {
    if (-not (Test-Path -LiteralPath $taskExe)) { throw 'Compile o candidato com build.ps1 -ExecutableName devilutionx-tristram-godot -WithSmoke antes de testar.' }
    if (-not (Test-Path -LiteralPath (Join-Path $taskProfile 'godot-pack-receipt.json'))) { throw 'Exporte no Godot e execute Aplicar-Mapa-Godot.cmd antes do primeiro teste.' }
    $taskReceipt = Get-Content -LiteralPath (Join-Path $taskProfile 'godot-pack-receipt.json') -Raw | ConvertFrom-Json
    if ($taskReceipt.format -ne 'd3d.godot-pack-receipt' -or $taskReceipt.schemaVersion -ne 1 -or
        $taskReceipt.gameExecutableSha256 -ne (Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash.ToLowerInvariant()) {
        throw 'O executavel diverge do pacote validado. Aplique novamente o pacote antes de testar.'
    }
    $taskManifest = Join-Path $taskProfile 'd3d-maps\tristram.ini'
    if (-not (Test-Path -LiteralPath $taskManifest -PathType Leaf) -or
        (Get-FileHash -LiteralPath $taskManifest -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskReceipt.manifestSha256) {
        throw 'O manifesto mudou depois da validacao. Aplique novamente o pacote antes de testar.'
    }
    foreach ($taskModel in $taskReceipt.models) {
        if ($taskModel.path -notmatch '^d3d-models/editor/[a-z0-9-]+\.d3d$') { throw 'Caminho de modelo invalido no recibo.' }
        $taskModelFile = Join-Path $taskProfile $taskModel.path
        if (-not (Test-Path -LiteralPath $taskModelFile -PathType Leaf) -or
            (Get-FileHash -LiteralPath $taskModelFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskModel.sha256) {
            throw 'Um modelo mudou depois da validacao. Exporte e aplique novamente antes de testar.'
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $DataDirectory 'DIABDAT.MPQ') -PathType Leaf)) {
        throw 'A ponte Godot v1 requer DIABDAT.MPQ do Diablo completo.'
    }
    Start-Process -FilePath $taskExe -WorkingDirectory $taskRoot -ArgumentList @(
        '-n', '--diablo', '--data-dir', ('"' + ($DataDirectory -replace '(\\+)$', '$1$1') + '"'),
        '--save-dir', ('"' + $taskProfile + '"'), '--config-dir', ('"' + $taskProfile + '"'),
        '--log-to-file', ('"' + (Join-Path $taskProfile 'prototipo.log') + '"')) -WindowStyle Normal
    return
}

if ([string]::IsNullOrWhiteSpace($GodotPath)) {
    $GodotPath = Join-Path $taskRoot '.tools\godot\Godot_v4.7.2-stable_win64.exe'
    if (-not (Test-Path -LiteralPath $GodotPath)) {
        $taskGodotCommand = Get-Command godot.exe -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($taskGodotCommand) { $GodotPath = $taskGodotCommand.Source }
    }
}
if (-not (Test-Path -LiteralPath $GodotPath) -and $InstallGodot) {
    $taskTools = Join-Path $taskRoot '.tools\godot'
    New-Item -ItemType Directory -Path $taskTools -Force | Out-Null
    $taskZip = Join-Path $taskTools 'Godot_v4.7.2-stable_win64.exe.zip'
    Invoke-WebRequest 'https://github.com/godotengine/godot/releases/download/4.7.2-stable/Godot_v4.7.2-stable_win64.exe.zip' -OutFile $taskZip
    if ((Get-FileHash -LiteralPath $taskZip -Algorithm SHA256).Hash.ToLowerInvariant() -ne '731980f9608d61333e5baf54a2ef17210acc7a538446c0cb9969f002aca1e953') {
        throw 'O download do Godot diverge do checksum oficial; nao sera executado.'
    }
    Expand-Archive -LiteralPath $taskZip -DestinationPath $taskTools -Force
    $GodotPath = Join-Path $taskTools 'Godot_v4.7.2-stable_win64.exe'
}
if (-not (Test-Path -LiteralPath $GodotPath)) {
    throw 'Godot nao encontrado. Use Godot-Editor.ps1 -InstallGodot ou -GodotPath com o executavel Godot 4.'
}
$taskGodotArgs = @('--editor', '--path', ('"' + $taskProject + '"'))
if (Test-Path -LiteralPath (Join-Path $taskProject 'local\tristram.tscn')) { $taskGodotArgs += 'res://local/tristram.tscn' }
Start-Process -FilePath $GodotPath -ArgumentList $taskGodotArgs -WorkingDirectory $taskProject -WindowStyle Normal
