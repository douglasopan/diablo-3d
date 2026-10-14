param([switch]$VerifyOnly,[switch]$PrepareOnly,[string]$DataDirectory,[string]$ProfileRoot)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
function Get-PackageSha256([string]$Path){
 $stream=[IO.File]::OpenRead($Path)
 $hasher=[Security.Cryptography.SHA256]::Create()
 try{return [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
 finally{$stream.Dispose();$hasher.Dispose()}
}
$manifest=Get-Content -LiteralPath (Join-Path $root 'package-manifest.json') -Raw | ConvertFrom-Json
foreach($file in $manifest.files){
 if($file.path -match '(^/|\\|:|(^|/)\.\.(/|$))'){throw 'Caminho invalido no pacote.'}
 $target=Join-Path $root $file.path
 if(!(Test-Path -LiteralPath $target -PathType Leaf) -or (Get-Item -LiteralPath $target).Length -ne $file.bytes -or (Get-PackageSha256 $target) -cne $file.sha256){throw ('Arquivo diferente do pacote: '+$file.path)}
}
if($VerifyOnly){Write-Output ('Pacote local3-full verificado: '+$manifest.files.Count+' arquivos.');exit 0}
if(!$ProfileRoot){$ProfileRoot=Join-Path $env:LOCALAPPDATA 'Diablo3D-local3-full'}
$ProfileRoot=[IO.Path]::GetFullPath($ProfileRoot)
$choice=Join-Path $ProfileRoot 'data-path.txt'
if(!$DataDirectory -and (Test-Path -LiteralPath $choice)){$DataDirectory=(Get-Content -LiteralPath $choice -Raw).Trim()}
if(!$DataDirectory -or !(Test-Path -LiteralPath (Join-Path $DataDirectory 'DIABDAT.MPQ') -PathType Leaf)){
 if($PrepareOnly){throw 'Forneca -DataDirectory com DIABDAT.MPQ para preparar sem janela.'}
 $DataDirectory=$null
 $candidates=@($root,(Join-Path ${env:ProgramFiles(x86)} 'GOG Galaxy/Games/Diablo'),(Join-Path $env:ProgramFiles 'GOG Galaxy/Games/Diablo'),'C:/GOG Games/Diablo')
 foreach($candidate in $candidates){if(Test-Path -LiteralPath (Join-Path $candidate 'DIABDAT.MPQ') -PathType Leaf){$DataDirectory=$candidate;break}}
}
if(!$DataDirectory){
 Add-Type -AssemblyName System.Windows.Forms
 $dialog=New-Object System.Windows.Forms.OpenFileDialog
 $dialog.Title='Diablo 3D - selecione DIABDAT.MPQ da sua copia do Diablo'
 $dialog.Filter='Dados do Diablo (DIABDAT.MPQ)|DIABDAT.MPQ'
 if($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK){exit 0}
 $DataDirectory=Split-Path -Parent $dialog.FileName
}
if(!(Test-Path -LiteralPath (Join-Path $DataDirectory 'DIABDAT.MPQ') -PathType Leaf)){throw 'DIABDAT.MPQ nao encontrado.'}
$mutex=[Threading.Mutex]::new($false,'Local\Diablo3D-local3-full-profile')
if(!$mutex.WaitOne(0)){throw 'Esta build ja esta aberta. Feche-a antes de iniciar outra.'}
try{
 $profile=Join-Path $ProfileRoot 'profile'
 [IO.Directory]::CreateDirectory($profile)|Out-Null
 $config=Join-Path $profile 'diablo.ini'
 if(!(Test-Path -LiteralPath $config)){Copy-Item -LiteralPath (Join-Path $root 'defaults/diablo.ini') -Destination $config}
 [IO.File]::WriteAllText($choice,[IO.Path]::GetFullPath($DataDirectory),[Text.UTF8Encoding]::new($false))
 if($PrepareOnly){Write-Output ('Perfil preparado: '+$profile);exit 0}
 $diagnostics=Join-Path $ProfileRoot ('diagnostics/'+[Guid]::NewGuid().ToString('N'))
 [IO.Directory]::CreateDirectory($diagnostics)|Out-Null
 Set-Location -LiteralPath $root
 & (Join-Path $root 'Diablo3D-candidate.exe') --diablo --data-dir $DataDirectory --config-dir $profile --save-dir $profile --gpu-required --d3d-local-diagnostics --d3d-diagnostics-dir $diagnostics
 exit $LASTEXITCODE
}finally{$mutex.ReleaseMutex();$mutex.Dispose()}
