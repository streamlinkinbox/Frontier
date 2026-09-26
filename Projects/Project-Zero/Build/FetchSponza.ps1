# Frontier/Projects/Project-Zero/Build/FetchSponza.ps1
# Downloads the Khronos glTF-Sample-Models Crytek Sponza (geometry only: .gltf + .bin, ~10 MB) into
# Projects\Project-Zero\Content\Scenes\Sponza\ so the R2 culling proofs can be reproduced:
#     Project-Zero.exe --scene Projects/Project-Zero/Content/Scenes/Sponza/Sponza.gltf
# Textures are not fetched (R2 has no bindless textures yet; the loader ignores image references).
# PS 5.1 compatible - no unicode.

[CmdletBinding()]
param([switch] $Force)

$ErrorActionPreference = 'Stop'
$RepositoryRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$TargetRoot     = Join-Path $RepositoryRoot 'Projects\Project-Zero\Content\Scenes\Sponza'
$BaseUrl        = 'https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Models/main/2.0/Sponza/glTF/'

New-Item -ItemType Directory -Force -Path $TargetRoot | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

foreach ($Name in @('Sponza.gltf', 'Sponza.bin'))
{
    $Target = Join-Path $TargetRoot $Name
    if ((Test-Path $Target) -and -not $Force) { Write-Host "  [skipped]  $Name already present"; continue }
    Write-Host "  [fetching] $Name"
    Invoke-WebRequest -Uri ($BaseUrl + $Name) -OutFile $Target -UseBasicParsing
}
Write-Host "  [ready]    $TargetRoot"
