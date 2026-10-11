param(
    [Parameter(Mandatory = $true)][string]$SdkRoot,
    # This repository carries Engine/ProjectInterchange itself, so SlateRoot is only needed when
    #    building against a different checkout's project ABI.
    [Parameter(Mandatory = $false)][string]$SlateRoot,
    [Parameter(Mandatory = $true)][string]$GuiRoot,
    [Parameter(Mandatory = $true)][string]$PhotonRoot
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$Output = Join-Path $PSScriptRoot 'Output'
$SdkRoot = (Resolve-Path $SdkRoot).Path
$RepositoryRoot = Split-Path (Split-Path $ProjectRoot -Parent) -Parent
if ($SlateRoot) { $SlateRoot = (Resolve-Path $SlateRoot).Path }
$GuiRoot = (Resolve-Path $GuiRoot).Path
$PhotonRoot = (Resolve-Path $PhotonRoot).Path
$Include = Join-Path $SdkRoot 'Include'
$Library = Join-Path $SdkRoot 'Lib/EOSSDK-Win64-Shipping.lib'
$Runtime = Join-Path $SdkRoot 'Bin/EOSSDK-Win64-Shipping.dll'
$Interchange = Join-Path $RepositoryRoot 'Engine/ProjectInterchange'
if ($SlateRoot) {
    $Donor = Join-Path $SlateRoot 'Frontier/Engine/ProjectInterchange'
    if (Test-Path (Join-Path $Donor 'ProjectInterchange.h')) { $Interchange = $Donor }
}
$PhotonInclude = Join-Path $PhotonRoot 'realtime/include'
$PhotonLibDir = Join-Path $SdkRoot 'Photon/Lib'
foreach ($Required in @($Library, $Runtime, (Join-Path $Include 'eos_sdk.h'), (Join-Path $Include 'eos_ecom.h'),
    (Join-Path $Interchange 'ProjectInterchange.h'), (Join-Path $PhotonInclude 'LoadBalancing-cpp/inc/Client.h'))) {
    if (-not (Test-Path $Required)) { throw "Required file absent: $Required" }
}
$PhotonLibs = @()
if (Test-Path $PhotonLibDir) {
    $PhotonLibs = @(Get-ChildItem $PhotonLibDir -Filter '*.lib' -File | Sort-Object Name)
}
if ($PhotonLibs.Count -gt 0 -and $PhotonLibs.Count -lt 3) {
    throw "Partial Photon lib set in $PhotonLibDir (need Common, Photon and LoadBalancing /MD x64, or none for stub mode)."
}
$PhotonLinked = $PhotonLibs.Count -ge 3
if ($PhotonLinked) {
    Write-Host "Photon: linking official libs: $($PhotonLibs.Name -join ', ')"
} else {
    Write-Host 'Photon: official /MD x64 libs absent; compiling the real transport as a check and linking the stub.'
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Run from an x64 Visual Studio developer PowerShell with cl.exe available.'
}
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    throw 'Use the x64 Visual Studio developer environment.'
}
New-Item -ItemType Directory -Force $Output | Out-Null
$Flags = @('/nologo', '/std:c++20', '/MD', '/EHsc', '/W4', '/utf-8', '/D_CRT_SECURE_NO_WARNINGS', "/I$Include", "/I$Interchange", "/I$PhotonInclude")
$Exchange = @('EpicExchange.cpp', 'BackendClient.cpp', 'LobbyRuntime.cpp', 'SessionHistory.cpp', 'TransportRouter.cpp', 'EcomOwnership.cpp', 'EosTransport.cpp', 'ReplicationLink.cpp', 'PhotonReplicationLink.cpp', 'ReplicationSequence.cpp') | ForEach-Object { Join-Path $ProjectRoot "Source/$_" }
if ($PhotonLinked) {
    $Exchange += Join-Path $ProjectRoot 'Source/PhotonTransport.cpp'
    # Photon archives need these OS libs named explicitly (no #pragma comment in headers).
    $LinkLibs = @($Library) + @($PhotonLibs | ForEach-Object { $_.FullName }) + @('winhttp.lib', 'Bcrypt.lib', 'ws2_32.lib', 'winmm.lib')
} else {
    $Exchange += Join-Path $ProjectRoot 'Source/PhotonLinkStub.cpp'
    # winhttp stays unconditional: the backend premium client needs it in every build.
    $LinkLibs = @($Library) + @('winhttp.lib')
}
Push-Location $Output
try {
    if (-not $PhotonLinked) {
        # Compile-now guarantee: the real transport must always type-check against the pinned headers.
        & cl.exe @Flags /c (Join-Path $ProjectRoot 'Source/PhotonTransport.cpp') '/Fo:PhotonTransport.check.obj' 2>&1 | Write-Host
        if ($LASTEXITCODE -ne 0) { throw 'Photon transport compile check failed.' }
    }
    $Compile = & cl.exe @Flags @Exchange (Join-Path $ProjectRoot 'Source/LoginHost.cpp') '/Fe:LoginHost.exe' /link $LinkLibs 2>&1
    $Code = $LASTEXITCODE
    $Compile | Write-Host
    if ($Code -ne 0) {
        $Detail = (($Compile | Select-Object -Last 60) -join "`n").Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
        Write-Host "::error::$Detail"
        throw 'EOS console compilation/link failed.'
    }
    & cl.exe @Flags /LD @Exchange (Join-Path $ProjectRoot 'Source/NetworkingInterchange.cpp') '/Fe:ProjectNetworking.dll' /link $LinkLibs
    if ($LASTEXITCODE -ne 0) { throw 'Frontier project DLL compilation/link failed.' }
    Copy-Item $Runtime (Join-Path $Output 'EOSSDK-Win64-Shipping.dll') -Force
    $GuiBuild = Join-Path $Output 'WindowBuild'
    $ConfigureOutput = & cmake -S (Join-Path $PSScriptRoot 'WindowHost') -B $GuiBuild -A x64 "-DEOS_SDK_ROOT=$SdkRoot" "-DGUI_ROOT=$GuiRoot" "-DPHOTON_ROOT=$PhotonRoot" "-DPHOTON_LIB_DIR=$PhotonLibDir" 2>&1
    $ConfigureCode = $LASTEXITCODE
    $ConfigureOutput | Write-Host
    if ($ConfigureCode -ne 0) {
        $Detail = (($ConfigureOutput | Select-Object -Last 40) -join "`n").Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
        Write-Host "::error::$Detail"
        throw 'GLFW/ImGui window configuration failed.'
    }
    $BuildOutput = & cmake --build $GuiBuild --config Release --parallel 4 2>&1
    $BuildCode = $LASTEXITCODE
    $BuildOutput | Write-Host
    if ($BuildCode -ne 0) {
        $Detail = (($BuildOutput | Select-Object -Last 40) -join "`n").Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
        Write-Host "::error::$Detail"
        throw 'GLFW/ImGui window build failed.'
    }
    $Revision = & git -C $SlateRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Unable to record Slate revision.' }
    $PhotonRevision = & git -C $PhotonRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { $PhotonRevision = 'unavailable' }
    @{
        slate_revision = $Revision
        gui_glfw_revision = (& git -C (Join-Path $GuiRoot 'glfw') rev-parse HEAD)
        gui_imgui_revision = (& git -C (Join-Path $GuiRoot 'imgui') rev-parse HEAD)
        photon_header_revision = $PhotonRevision
        photon_linked = $PhotonLinked
        photon_libs = @($PhotonLibs | ForEach-Object {
            @{ name = $_.Name; sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
        })
        window_sha256 = (Get-FileHash (Join-Path $Output 'NetworkingLogin.exe') -Algorithm SHA256).Hash
        built_utc = [DateTime]::UtcNow.ToString('o')
        eos_runtime_sha256 = (Get-FileHash $Runtime -Algorithm SHA256).Hash
        sources = @(Get-ChildItem (Join-Path $ProjectRoot 'Source') -File | ForEach-Object {
            @{ name = $_.Name; sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
        })
    } | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $Output 'BuildEvidence.json')
} finally {
    Pop-Location
}
Write-Host "Built GLFW/ImGui window, console diagnostic and project DLL in $Output. Authentication has NOT been executed."
