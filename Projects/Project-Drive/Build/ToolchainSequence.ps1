#============================================================================================================================================
#                                                TOOLCHAINSEQUENCE.PS1
#============================================================================================================================================
# 📦 Compatibility forwarding route; the authoritative Frontier.exe and project-image build is Tools/Build/ToolchainSequence.ps1.

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Release',
    [switch] $SetupDependencies,
    [switch] $Rebuild,
    [switch] $Run,
    [int]    $Parallel = 0,
    [ValidateSet('SSE2', 'AVX', 'AVX2')] [string] $Isa = 'SSE2',
    [switch] $FluidOpenMP,
    [switch] $Development = $true
)

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
& (Join-Path $RepositoryRoot 'Tools\Build\ToolchainSequence.ps1') @PSBoundParameters
exit $LASTEXITCODE
