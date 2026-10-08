<#
Store the current Windows user's Tripo API key using DPAPI.
Run manually in a trusted PowerShell console; the key is never an argument or output.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$tripoSecret = $null
$tripoTemp = $null
try {
    $tripoDirectory = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.codex\secrets'
    $tripoTarget = Join-Path $tripoDirectory 'diablo-tripo.dpapi'
    [void][System.IO.Directory]::CreateDirectory($tripoDirectory)
    $tripoIdentity = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
    $tripoSecret = Read-Host 'Tripo API key (input hidden)' -AsSecureString
    if ($tripoSecret.Length -eq 0) {
        throw 'The credential is empty.'
    }
    # With no -Key/-SecureKey, ConvertFrom-SecureString uses current-user Windows DPAPI.
    $tripoProtected = ConvertFrom-SecureString -SecureString $tripoSecret
    $tripoTemp = Join-Path $tripoDirectory ('.tripo-' + [Guid]::NewGuid().ToString('N') + '.tmp')
    [System.IO.File]::WriteAllText($tripoTemp, $tripoProtected, [System.Text.UTF8Encoding]::new($false))
    $tripoAcl = [System.Security.AccessControl.FileSecurity]::new()
    $tripoAcl.SetAccessRuleProtection($true, $false)
    $tripoAcl.SetOwner($tripoIdentity)
    $tripoRule = [System.Security.AccessControl.FileSystemAccessRule]::new(
        $tripoIdentity, [System.Security.AccessControl.FileSystemRights]::FullControl,
        [System.Security.AccessControl.AccessControlType]::Allow)
    [void]$tripoAcl.AddAccessRule($tripoRule)
    Set-Acl -LiteralPath $tripoTemp -AclObject $tripoAcl
    if ([System.IO.File]::Exists($tripoTarget)) {
        [System.IO.File]::Replace($tripoTemp, $tripoTarget, $null)
    } else {
        [System.IO.File]::Move($tripoTemp, $tripoTarget)
    }
    Set-Acl -LiteralPath $tripoTarget -AclObject $tripoAcl
    Write-Output 'Tripo credential saved for the current Windows user. No API request was made.'
} catch {
    # Exceptions from secret APIs are deliberately not printed.
    Write-Error 'Could not save the current-user Tripo credential securely.'
    exit 1
} finally {
    if ($null -ne $tripoSecret) { $tripoSecret.Dispose() }
    $tripoProtected = $null
    if ($null -ne $tripoTemp -and [System.IO.File]::Exists($tripoTemp)) {
        Remove-Item -LiteralPath $tripoTemp -Force
    }
}
