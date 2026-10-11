#=============================================================================================================================================
# 📦 Project-Zero/Build/Construct.ps1 — MSVC Build Driver for the Headless CPU Reference (sandbox stand-in)
#=============================================================================================================================================
# Builds bin/Project-Zero-CpuReference.exe from an EXPLICIT file list (no globs, no engine-object reuse):
#   powershell -NoProfile -ExecutionPolicy Bypass -File Build/Construct.ps1 [-Run] [-Rebuild] [-Configuration Release]
# The product renderer is the Vulkan + Slang stack built by Build/ToolchainSequence.ps1 (see ../Construct.bat).

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Release',
    [switch]                                   $Rebuild,
    [switch]                                   $Run
)

$ErrorActionPreference = 'Stop'

$ProjectRoot    = Split-Path -Parent $PSScriptRoot
$RepositoryRoot = Split-Path -Parent (Split-Path -Parent $ProjectRoot)
$BinRoot        = Join-Path $ProjectRoot 'bin'
$OutputRoot     = Join-Path $ProjectRoot "build\$Configuration-CpuReference"

#-------------------------------------------------------------------------------------------------------------------------
#                                                 TOOLCHAIN IMPORT
#-------------------------------------------------------------------------------------------------------------------------

function Import-ToolchainEnvironment
{
    if (Get-Command cl.exe -ErrorAction SilentlyContinue)
    {
        return
    }

    $Candidates = @(
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat'
    )

    $Selected = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1

    if ($null -eq $Selected)
    {
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

    Write-Host "[Project-Zero Build] Importing MSVC environment from: $Selected" -ForegroundColor Cyan

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

Import-ToolchainEnvironment

#-------------------------------------------------------------------------------------------------------------------------
#                                                 CPU REFERENCE SOURCES
#-------------------------------------------------------------------------------------------------------------------------
# Explicit list: the headless reference compiles ONLY these translation units. Never glob Source/ — GameExecution.cpp
#    (the Vulkan product entry point) lives there and requires the Vulkan SDK + Slang.

$SourceFiles = @(
    (Join-Path $ProjectRoot 'Source\CpuReferenceMain.cpp'),
    (Join-Path $ProjectRoot 'Source\RendererHost.cpp'),
    (Join-Path $ProjectRoot 'Source\RayTracingSolver.cpp'),
    (Join-Path $ProjectRoot 'Source\SkyFogIntegrator.cpp'),
    (Join-Path $RepositoryRoot 'Engine\GeometricRaster\CameraProjection.cpp'),
    (Join-Path $RepositoryRoot 'Engine\DeviceExchange\OrientationClassifier.cpp'),
    (Join-Path $RepositoryRoot 'Engine\DeviceExchange\DiagnosticMetrics.cpp')
)

foreach ($Required in $SourceFiles)
{
    if (-not (Test-Path $Required)) { throw "CPU reference source missing: $Required" }
}

if ($Rebuild -and (Test-Path $OutputRoot))
{
    Remove-Item -Path $OutputRoot -Recurse -Force
}

if (-not (Test-Path $OutputRoot))
{
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
}

if (-not (Test-Path $BinRoot))
{
    New-Item -ItemType Directory -Path $BinRoot | Out-Null
}

$TargetExe = Join-Path $BinRoot 'Project-Zero-CpuReference.exe'

$CompilerFlags = @(
    '/nologo',
    '/c',
    '/EHsc',
    '/MP',
    '/MD',
    '/std:c++20',
    '/permissive-',
    '/fp:precise',
    '/W4',
    '/wd4324',
    '/utf-8',
    '/Zc:__cplusplus',
    '/DWIN32_LEAN_AND_MEAN',
    '/DNOMINMAX',
    "/I`"$RepositoryRoot`"",
    "/I`"$ProjectRoot`"",
    "/Fo`"$OutputRoot\\`""
)

if ($Configuration -eq 'Debug')
{
    $CompilerFlags += @('/Od', '/Zi', '/Zf', '/DFRONTIER_DEBUG=1')
}
else
{
    $CompilerFlags += @('/O2', '/Zi', '/Zf', '/DNDEBUG')
}

& cl.exe $CompilerFlags $SourceFiles
if ($LASTEXITCODE -ne 0) { exit 1 }

$GameObjFiles = Get-ChildItem -Path $OutputRoot -Filter '*.obj' | ForEach-Object { $_.FullName }
$LinkArgs     = @('/nologo', '/DEBUG', "/OUT:`"$TargetExe`"", '/SUBSYSTEM:CONSOLE') + $GameObjFiles

& link.exe $LinkArgs
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "[Project-Zero] CPU reference built successfully: $TargetExe" -ForegroundColor Green

if ($Run)
{
    if (-not (Test-Path (Join-Path $ProjectRoot 'Diagnostics')))
    {
        New-Item -ItemType Directory -Path (Join-Path $ProjectRoot 'Diagnostics') | Out-Null
    }
    Push-Location $ProjectRoot
    try
    {
        & $TargetExe
    }
    finally
    {
        Pop-Location
    }
}
