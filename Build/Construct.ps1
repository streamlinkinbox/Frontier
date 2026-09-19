#=============================================================================================================================================
# 📦 Frontier/Build/Construct.ps1 — Direct cl.exe / link.exe Toolchain Build Driver for Windows Command Line
#=============================================================================================================================================

# Direct compiler invocation script for Frontier Engine on Windows.
# Builds with cl.exe and link.exe directly without opening Visual Studio IDE.
#
# Usage:
#     powershell -NoProfile -ExecutionPolicy Bypass -File Build/Construct.ps1
#     powershell -NoProfile -ExecutionPolicy Bypass -File Build/Construct.ps1 -Configuration Release
#     powershell -NoProfile -ExecutionPolicy Bypass -File Build/Construct.ps1 -Configuration Debug -Rebuild
#     powershell -NoProfile -ExecutionPolicy Bypass -File Build/Construct.ps1 -Run

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Release',
    [switch]                                   $Rebuild,
    [int]                                      $Parallel      = 0,
    [switch]                                   $Run
)

$ErrorActionPreference = 'Stop'

$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$BinRoot        = Join-Path $RepositoryRoot 'bin'
$OutputRoot     = Join-Path $RepositoryRoot "build\$Configuration"

#-------------------------------------------------------------------------------------------------------------------------
#                                                   CONSOLE REPORTING
#-------------------------------------------------------------------------------------------------------------------------

function Write-Report([string] $Tag, [System.ConsoleColor] $Colour, [string] $Message)
{
    Write-Host ("[$Tag]".PadRight(12)) -ForegroundColor $Colour -NoNewline
    Write-Host " $Message"
}

function Write-Building([string] $Message) { Write-Report 'Build'    DarkGray $Message }
function Write-Skipped([string]  $Message) { Write-Report 'Skip'     Cyan     $Message }
function Write-Rejected([string] $Message) { Write-Report 'Failed'   Red      $Message }
function Write-Produced([string] $Message) { Write-Report 'Compiled' Green    $Message }
function Write-Linked([string]   $Message) { Write-Report 'Linked'   Yellow   $Message }

#-------------------------------------------------------------------------------------------------------------------------
#                                                 TOOLCHAIN IMPORT
#-------------------------------------------------------------------------------------------------------------------------

function Import-ToolchainEnvironment
{
    if (Get-Command cl.exe -ErrorAction SilentlyContinue)
    {
        Write-Skipped 'MSVC Toolchain already configured on PATH.'
        return
    }

    $Candidates = @(
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat'
    )

    $Selected = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1

    if ($null -eq $Selected)
    {
        # Fallback to vswhere
        $VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $VsWhere)
        {
            $InstallPath = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($InstallPath)
            {
                $Candidate = Join-Path $InstallPath 'VC\Auxiliary\Build\vcvarsall.bat'
                if (Test-Path $Candidate)
                {
                    $Selected = $Candidate
                }
            }
        }
    }

    if ($null -eq $Selected)
    {
        throw 'No Visual Studio vcvarsall.bat toolchain was located. Ensure MSVC C++ tools are installed.'
    }

    Write-Building "Importing MSVC environment from: $Selected"

    $Captured = cmd.exe /c "`"$Selected`" x64 > nul & set"
    foreach ($Line in $Captured)
    {
        if ($Line -match '^([^=]+)=(.*)$')
        {
            Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
        }
    }

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue))
    {
        throw 'vcvarsall.bat executed but cl.exe remains absent from current PATH.'
    }
}

#-------------------------------------------------------------------------------------------------------------------------
#                                                 COMPILATION PIPELINE
#-------------------------------------------------------------------------------------------------------------------------

Import-ToolchainEnvironment

if ($Rebuild -and (Test-Path $OutputRoot))
{
    Write-Building "Cleaning build tree at: $OutputRoot"
    Remove-Item -Path $OutputRoot -Recurse -Force
}

if (-not (Test-Path $OutputRoot))
{
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
}

if (-not (Test-Path $BinRoot))
{
    New-Item -ItemType Directory -Path $BinRoot -Force | Out-Null
}

# Source discovery
$SourceFiles = @(
    Get-ChildItem -Path (Join-Path $RepositoryRoot 'DeviceExchange')           -Filter '*.cpp' | ForEach-Object { $_.FullName }
    Get-ChildItem -Path (Join-Path $RepositoryRoot 'PhysicalDynamics')         -Filter '*.cpp' | ForEach-Object { $_.FullName }
    Get-ChildItem -Path (Join-Path $RepositoryRoot 'VolumetricDynamics')       -Filter '*.cpp' | ForEach-Object { $_.FullName }
    Get-ChildItem -Path (Join-Path $RepositoryRoot 'GeometricRaster')          -Filter '*.cpp' | ForEach-Object { $_.FullName }
    Get-ChildItem -Path (Join-Path $RepositoryRoot 'PhotometricIllumination')  -Filter '*.cpp' | ForEach-Object { $_.FullName }
    Get-ChildItem -Path (Join-Path $RepositoryRoot 'PlatformInterchange')      -Filter '*.cpp' | ForEach-Object { $_.FullName }
    Get-ChildItem -Path (Join-Path $RepositoryRoot 'DisplayPresentation')      -Filter '*.cpp' | ForEach-Object { $_.FullName }
)

Write-Building "Compiling $($SourceFiles.Count) Frontier translation units ($Configuration mode)..."

$MpFlag = if ($Parallel -gt 0) { "/MP$Parallel" } else { '/MP' }

$CompilerFlags = @(
    '/nologo',
    '/c',
    '/EHsc',
    $MpFlag,
    '/MD',
    '/std:c++20',
    '/permissive-',
    '/fp:precise',
    '/W4',
    '/WX',
    '/wd4324',
    '/utf-8',
    '/Zc:__cplusplus',
    '/DWIN32_LEAN_AND_MEAN',
    '/DNOMINMAX',
    '/DFRONTIER_DEVELOPMENT',
    "/I`"$RepositoryRoot`"",
    "/Fo`"$OutputRoot\\`"",
    "/Fd`"$OutputRoot\FrontierEngine.pdb`""
)

if ($Configuration -eq 'Debug')
{
    $CompilerFlags += @('/Od', '/Zi', '/Zf', '/DFRONTIER_DEBUG=1')
}
else
{
    $CompilerFlags += @('/O2', '/Zi', '/Zf', '/DNDEBUG')
}

$CompileArgs = $CompilerFlags + $SourceFiles
& cl.exe $CompileArgs

if ($LASTEXITCODE -ne 0)
{
    Write-Rejected 'Compilation failed with errors.'
    exit 1
}

Write-Produced "All translation units compiled successfully."

#-------------------------------------------------------------------------------------------------------------------------
#                                                   LINKING PIPELINE
#-------------------------------------------------------------------------------------------------------------------------

$ObjectFiles = Get-ChildItem -Path $OutputRoot -Filter '*.obj' | ForEach-Object { $_.FullName }
$TargetExe   = Join-Path $BinRoot 'FrontierEngine.exe'

Write-Building "Linking target executable: $TargetExe"

$LinkerFlags = @(
    '/nologo',
    '/DEBUG',
    "/OUT:`"$TargetExe`"",
    "/PDB:`"$BinRoot\FrontierEngine.pdb`"",
    '/SUBSYSTEM:CONSOLE',
    'user32.lib'
)

$LinkArgs = $LinkerFlags + $ObjectFiles
& link.exe $LinkArgs

if ($LASTEXITCODE -ne 0)
{
    Write-Rejected 'Linking failed with errors.'
    exit 1
}

Write-Linked "FrontierEngine.exe built successfully in $BinRoot"

#-------------------------------------------------------------------------------------------------------------------------
#                                                  OPTIONAL EXECUTION
#-------------------------------------------------------------------------------------------------------------------------

if ($Run)
{
    Write-Building "Executing: $TargetExe"
    & $TargetExe
}
