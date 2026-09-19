#=============================================================================================================================================
# 📦 Project-F20/Build/Construct.ps1 — Direct Toolchain Build Driver for Project-F20 Game Project
#=============================================================================================================================================

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
$OutputRoot     = Join-Path $ProjectRoot "build\$Configuration"

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
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat'
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

    Write-Host "[Project-F20 Build] Importing MSVC environment from: $Selected" -ForegroundColor Cyan

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

# Compile engine translation units first
powershell -NoProfile -ExecutionPolicy Bypass -File "$RepositoryRoot\Build\Construct.ps1" -Configuration $Configuration

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
    New-Item -ItemType Directory -Path $BinRoot -Force | Out-Null
}

$SourceFiles = @(
    Get-ChildItem -Path (Join-Path $ProjectRoot 'Source') -Filter '*.cpp' | ForEach-Object { $_.FullName }
)

$EngineObjFiles = Get-ChildItem -Path (Join-Path $RepositoryRoot "build\$Configuration") -Filter '*.obj' |
    Where-Object { $_.Name -notmatch 'EngineExecution\.obj' } |
    ForEach-Object { $_.FullName }

$TargetExe = Join-Path $BinRoot 'Project-F20.exe'

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
    '/WX',
    '/wd4324',
    '/utf-8',
    '/Zc:__cplusplus',
    '/DWIN32_LEAN_AND_MEAN',
    '/DNOMINMAX',
    '/DFRONTIER_DEVELOPMENT',
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
$LinkArgs     = @('/nologo', '/DEBUG', "/OUT:`"$TargetExe`"", '/SUBSYSTEM:CONSOLE') + $GameObjFiles + $EngineObjFiles

& link.exe $LinkArgs
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "[Project-F20] Executable built successfully: $TargetExe" -ForegroundColor Green

if ($Run)
{
    & $TargetExe
}
