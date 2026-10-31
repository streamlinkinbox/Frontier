param(
    [switch]$AllowCreateUser,
    [ValidateSet('developer', 'accountportal')][string]$LoginMethod = 'developer',
    [string]$DeveloperCredential
)
$ErrorActionPreference = 'Stop'
$Output = Join-Path $PSScriptRoot 'Output'
$Executable = Join-Path $Output 'LoginHost.exe'
if (-not (Test-Path $Executable)) { throw 'Build with ToolchainSequence.ps1 first.' }
if ($env:EOS_CLIENT_SECRET) {
    throw 'Remove EOS_CLIENT_SECRET from this shell first; this runner prompts for the rotated secret.'
}
if ($LoginMethod -eq 'developer' -and -not $DeveloperCredential) {
    $DeveloperCredential = Read-Host 'Saved credential NAME in Developer Authentication Tool (port 6547)'
    if (-not $DeveloperCredential) { throw 'A saved developer credential name is required.' }
}
if ($LoginMethod -eq 'accountportal') {
    Write-Warning 'Account Portal requires EOS redistributable and launch through EOS Bootstrapper. Direct launch may be refused.'
}
$PreviousConsent = $env:EOS_ALLOW_CREATE_USER
$PreviousMethod = $env:EOS_LOGIN_METHOD
$PreviousCredential = $env:EOS_DEVELOPER_CREDENTIAL
$Secret = Read-Host 'Rotated EOS client secret (hidden; never paste it in chat)' -AsSecureString
$Address = [IntPtr]::Zero
$ExitCode = 2
$Evidence = Join-Path $Output ('Login-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '.log')
try {
    $Address = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Secret)
    $env:EOS_CLIENT_SECRET = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($Address)
    $env:EOS_ALLOW_CREATE_USER = if ($AllowCreateUser) { '1' } else { '0' }
    $env:EOS_LOGIN_METHOD = $LoginMethod
    $env:EOS_DEVELOPER_CREDENTIAL = $DeveloperCredential
    "started_utc=$([DateTime]::UtcNow.ToString('o'))" | Set-Content $Evidence
    "executable_sha256=$((Get-FileHash $Executable -Algorithm SHA256).Hash)" | Add-Content $Evidence
    & $Executable | Tee-Object -FilePath $Evidence -Append
    $ExitCode = $LASTEXITCODE
    "exit_code=$ExitCode" | Add-Content $Evidence
    "finished_utc=$([DateTime]::UtcNow.ToString('o'))" | Add-Content $Evidence
} finally {
    Remove-Item Env:EOS_CLIENT_SECRET -ErrorAction SilentlyContinue
    $env:EOS_ALLOW_CREATE_USER = $PreviousConsent
    $env:EOS_LOGIN_METHOD = $PreviousMethod
    $env:EOS_DEVELOPER_CREDENTIAL = $PreviousCredential
    if ($Address -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($Address) }
    $Secret.Dispose()
}
Write-Host "Redacted login evidence: $Evidence"
exit $ExitCode
