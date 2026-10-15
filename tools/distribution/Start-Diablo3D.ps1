param([switch]$VerifyOnly,[switch]$PrepareOnly,[string]$DataDirectory,[string]$ProfileRoot)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
function Get-PackageSha256([string]$Path){
 $stream=[IO.File]::OpenRead($Path);$hasher=[Security.Cryptography.SHA256]::Create()
 try{return [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
 finally{$stream.Dispose();$hasher.Dispose()}
}
function Assert-Files([string]$Directory,$Files){
 foreach($file in $Files){
  if($file.path -match '(^/|\\|:|(^|/)\.\.(/|$))'){throw 'Invalid package path / Caminho invalido.'}
  $target=Join-Path $Directory $file.path
  if(!(Test-Path -LiteralPath $target -PathType Leaf) -or (Get-Item -LiteralPath $target).Length -ne $file.bytes -or (Get-PackageSha256 $target) -cne $file.sha256){throw ('File mismatch / Arquivo divergente: '+$file.path)}
 }
}
$manifest=Get-Content -LiteralPath (Join-Path $root 'package-manifest.json') -Raw | ConvertFrom-Json
Assert-Files $root $manifest.files
if($VerifyOnly){Write-Output 'Package verified / Pacote verificado.';exit 0}
if(!$ProfileRoot){$ProfileRoot=Join-Path $env:LOCALAPPDATA 'Diablo3D-local6-grid162'}
$ProfileRoot=[IO.Path]::GetFullPath($ProfileRoot)
$choice=Join-Path $ProfileRoot 'data-path.txt'
if(!$DataDirectory -and (Test-Path -LiteralPath $choice)){$DataDirectory=(Get-Content -LiteralPath $choice -Raw).Trim()}
if(!$DataDirectory -or !(Test-Path -LiteralPath (Join-Path $DataDirectory 'DIABDAT.MPQ') -PathType Leaf)){
 if($PrepareOnly){throw 'Provide -DataDirectory with DIABDAT.MPQ / Forneca o caminho do MPQ.'}
 $DataDirectory=$null
 foreach($candidate in @($root,(Join-Path ${env:ProgramFiles(x86)} 'GOG Galaxy/Games/Diablo'),(Join-Path $env:ProgramFiles 'GOG Galaxy/Games/Diablo'),'C:/GOG Games/Diablo')){
  if(Test-Path -LiteralPath (Join-Path $candidate 'DIABDAT.MPQ') -PathType Leaf){$DataDirectory=$candidate;break}
 }
}
if(!$DataDirectory){
 Add-Type -AssemblyName System.Windows.Forms
 $dialog=New-Object System.Windows.Forms.OpenFileDialog
 $dialog.Title='Diablo 3D - Select / Selecione DIABDAT.MPQ'
 $dialog.Filter='Original Diablo data (DIABDAT.MPQ)|DIABDAT.MPQ'
 if($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK){exit 0}
 $DataDirectory=Split-Path -Parent $dialog.FileName
}
if(!(Test-Path -LiteralPath (Join-Path $DataDirectory 'DIABDAT.MPQ') -PathType Leaf)){throw 'DIABDAT.MPQ not found / nao encontrado.'}
$mutex=[Threading.Mutex]::new($false,'Local\Diablo3D-local6-grid162-profile')
if(!$mutex.WaitOne(0)){throw 'This build is already open / Esta build ja esta aberta.'}
try{
 $profile=Join-Path $ProfileRoot 'profile'
 $scene=Join-Path $profile 'd3d-scenes'
 $origin=Join-Path $profile 'scene-preparation.json'
 $mpqSha=Get-PackageSha256 (Join-Path $DataDirectory 'DIABDAT.MPQ')
 $recipe=Get-Content -LiteralPath (Join-Path $root 'scene/recipe.json') -Raw | ConvertFrom-Json
 if(Test-Path -LiteralPath $scene){
  Assert-Files $profile $recipe.runtimeFiles
  if(!(Test-Path -LiteralPath $origin)){throw 'Existing scene has no preparation receipt; preserved / Cena existente preservada.'}
  $receipt=Get-Content -LiteralPath $origin -Raw | ConvertFrom-Json
  if($receipt.originalMpqSha256 -cne $mpqSha -or $receipt.targetSha256 -cne $recipe.targetSha256){throw 'Original data changed; existing scene preserved / Dados originais alterados; cena preservada.'}
 }
 else{
  $stage=Join-Path ([IO.Path]::GetTempPath()) ('Diablo3D-Tristram-'+[Guid]::NewGuid().ToString('N'))
  [IO.Directory]::CreateDirectory($stage)|Out-Null
  $materials=Join-Path $stage 'materials'
  Write-Output 'Preparing Tristram from your original data / Preparando Tristram com seus dados originais...'
  & (Join-Path $root 'Prepare-Tristram.exe') $DataDirectory (Join-Path $root 'assets') (Join-Path $root 'scene/preparation') $materials
  if($LASTEXITCODE -ne 0){throw 'Original data preparation failed / Falha ao preparar dados originais.'}
  $baseline=Join-Path $materials 'tristram.d3dvis'
  if((Get-PackageSha256 $baseline) -cne $recipe.baselineSha256){throw 'Baseline mismatch; installation refused / Base divergente.'}
  $ready=Join-Path $stage 'ready';[IO.Directory]::CreateDirectory($ready)|Out-Null
  Copy-Item -LiteralPath (Join-Path $root 'scene/final/d3d-scenes') -Destination $ready -Recurse
  $static=Join-Path $ready 'd3d-scenes/static';[IO.Directory]::CreateDirectory($static)|Out-Null
  $baselineBytes=[IO.File]::ReadAllBytes($baseline)
  foreach($change in $recipe.baselineChanges){
   if($change.offset -lt 0 -or $change.offset -ge $baselineBytes.Length -or $baselineBytes[$change.offset] -ne $change.before){throw 'Baseline edit mismatch / Edicao de base divergente.'}
   $baselineBytes[$change.offset]=[byte]$change.after
  }
  [IO.File]::WriteAllBytes((Join-Path $static 'tristram-baseline.d3dvis'),$baselineBytes)
  Copy-Item -LiteralPath (Join-Path $materials 'palette.rgb') -Destination (Join-Path $static 'palette.rgb')
  $bytes=[IO.File]::ReadAllBytes($baseline)
  foreach($change in $recipe.changes){
   if($change.offset -lt 0 -or $change.offset -ge $bytes.Length -or $bytes[$change.offset] -ne $change.before){throw 'Authored edit mismatch / Edicao divergente.'}
   $bytes[$change.offset]=[byte]$change.after
  }
  [IO.File]::WriteAllBytes((Join-Path $static 'tristram.d3dvis'),$bytes)
  Assert-Files $ready $recipe.runtimeFiles
  & (Join-Path $root 'Prepare-Tristram.exe') $DataDirectory (Join-Path $root 'assets') $ready (Join-Path $stage 'validation-no-output') --validate-scene
  if($LASTEXITCODE -ne 0){throw 'Selected scene validation failed / Falha na validacao da cena selecionada.'}
  [IO.Directory]::CreateDirectory($profile)|Out-Null
  if(Test-Path -LiteralPath $scene){throw 'Scene appeared during preparation; preserved / Cena existente preservada.'}
  $moveSource=[IO.Path]::GetFullPath((Join-Path $ready 'd3d-scenes'))
  $moveTarget=[IO.Path]::GetFullPath($scene)
  if(!$moveSource.StartsWith(([IO.Path]::GetFullPath($stage)+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or !$moveTarget.StartsWith(($ProfileRoot+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid scene destination / Destino invalido.'}
  Move-Item -LiteralPath $moveSource -Destination $moveTarget
  [IO.File]::WriteAllText($origin,(@{originalMpqSha256=$mpqSha;targetSha256=$recipe.targetSha256;baselineSha256=$recipe.baselineSha256;runtimeFileCount=$recipe.runtimeFiles.Count}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
 }
 $config=Join-Path $profile 'diablo.ini'
 if(!(Test-Path -LiteralPath $config)){Copy-Item -LiteralPath (Join-Path $root 'defaults/diablo.ini') -Destination $config}
 [IO.File]::WriteAllText($choice,[IO.Path]::GetFullPath($DataDirectory),[Text.UTF8Encoding]::new($false))
 if($PrepareOnly){Write-Output ('Selected scene ready / Cena selecionada pronta: '+$profile);exit 0}
 $diagnostics=Join-Path $ProfileRoot ('diagnostics/'+[Guid]::NewGuid().ToString('N'))
 [IO.Directory]::CreateDirectory($diagnostics)|Out-Null
 Set-Location -LiteralPath $root
 & (Join-Path $root 'Diablo3D-candidate.exe') --diablo --data-dir $DataDirectory --config-dir $profile --save-dir $profile --gpu-required --d3d-local-diagnostics --d3d-diagnostics-dir $diagnostics
 exit $LASTEXITCODE
}finally{$mutex.ReleaseMutex();$mutex.Dispose()}
