#!/usr/bin/env python3
#=============================================================================================================================================
# 📦 Frontier/Tools/ProjectConstruct.py — Standalone Game Project Scaffolding and Automated Build Generator
#=============================================================================================================================================

import os
import sys

def ScaffoldProject(projectName, destinationDir="Projects"):
    projectRoot = os.path.join(destinationDir, projectName)
    
    subfolders = [
        "Source",
        "Shaders",
        "Content/AudioArchives",
        "Content/FontArchives",
        "Content/GeometryArchives",
        "Content/GraphicArchives",
        "Content/ShaderArchives",
        "Content/WorldArchives",
        "Build"
    ]
    
    print(f"[Frontier Project Generator] Scaffolding Project: {projectName} -> {projectRoot}")
    
    for folder in subfolders:
        path = os.path.join(projectRoot, folder)
        os.makedirs(path, exist_ok=True)
        print(f"  + Created: {folder}")

    # Generate Project Build/Construct.ps1
    ps1Content = f"""# Direct Toolchain Build Driver for {projectName}
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Release',
    [switch] $Rebuild,
    [switch] $Run
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$RepositoryRoot = Split-Path -Parent (Split-Path -Parent $ProjectRoot)

function Import-ToolchainEnvironment
{{
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {{ return }}
    $Candidates = @(
        'C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat'
        'C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Auxiliary\\Build\\vcvarsall.bat'
        'C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Auxiliary\\Build\\vcvarsall.bat'
        'C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvarsall.bat'
        'C:\\Program Files\\Microsoft Visual Studio\\2019\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat'
        'C:\\Program Files\\Microsoft Visual Studio\\2019\\Professional\\VC\\Auxiliary\\Build\\vcvarsall.bat'
        'C:\\Program Files\\Microsoft Visual Studio\\2019\\Enterprise\\VC\\Auxiliary\\Build\\vcvarsall.bat'
    )
    $Selected = $Candidates | Where-Object {{ Test-Path $_ }} | Select-Object -First 1
    if ($null -eq $Selected)
    {{
        $VsWhere = "$($env:ProgramFiles(x86))\\Microsoft Visual Studio\\Installer\\vswhere.exe"
        if (Test-Path $VsWhere)
        {{
            $InstallPath = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($InstallPath) {{ $Candidate = Join-Path $InstallPath 'VC\\Auxiliary\\Build\\vcvarsall.bat'; if (Test-Path $Candidate) {{ $Selected = $Candidate }} }}
        }}
    }}
    if ($null -eq $Selected) {{ throw 'No Visual Studio vcvarsall.bat toolchain was located.' }}
    $Captured = cmd.exe /c "`"$Selected`" x64 > nul & set"
    foreach ($Line in $Captured) {{ if ($Line -match '^([^=]+)=(.*)$') {{ Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue }} }}
}}

Import-ToolchainEnvironment

powershell -NoProfile -ExecutionPolicy Bypass -File "$RepositoryRoot\\Build\\Construct.ps1" -Configuration $Configuration
$OutputRoot = Join-Path $ProjectRoot "build\\$Configuration"
$BinRoot = Join-Path $ProjectRoot 'bin'
if ($Rebuild -and (Test-Path $OutputRoot)) {{ Remove-Item -Path $OutputRoot -Recurse -Force }}
if (-not (Test-Path $OutputRoot)) {{ New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null }}
if (-not (Test-Path $BinRoot)) {{ New-Item -ItemType Directory -Path $BinRoot -Force | Out-Null }}
$SourceFiles = @(Get-ChildItem -Path (Join-Path $ProjectRoot 'Source') -Filter '*.cpp' | ForEach-Object {{ $_.FullName }})
$EngineObjFiles = Get-ChildItem -Path (Join-Path $RepositoryRoot "build\\$Configuration") -Filter '*.obj' | Where-Object {{ $_.Name -notmatch 'EngineExecution\\.obj' }} | ForEach-Object {{ $_.FullName }}
$TargetExe = Join-Path $BinRoot '{projectName}.exe'
$CompilerFlags = @('/nologo', '/c', '/EHsc', '/MP', '/MD', '/std:c++20', '/permissive-', '/fp:precise', '/W4', '/WX', '/wd4324', '/utf-8', '/Zc:__cplusplus', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/DFRONTIER_DEVELOPMENT', "/I`"$RepositoryRoot`"", "/I`"$ProjectRoot`"", "/Fo`"$OutputRoot\\\\`"")
if ($Configuration -eq 'Debug') {{ $CompilerFlags += @('/Od', '/Zi', '/Zf', '/DFRONTIER_DEBUG=1') }} else {{ $CompilerFlags += @('/O2', '/Zi', '/Zf', '/DNDEBUG') }}
& cl.exe $CompilerFlags $SourceFiles
if ($LASTEXITCODE -ne 0) {{ exit 1 }}
$GameObjFiles = Get-ChildItem -Path $OutputRoot -Filter '*.obj' | ForEach-Object {{ $_.FullName }}
& link.exe @('/nologo', '/DEBUG', "/OUT:`"$TargetExe`"", '/SUBSYSTEM:CONSOLE') $GameObjFiles $EngineObjFiles
if ($LASTEXITCODE -ne 0) {{ exit 1 }}
Write-Host "[{projectName}] Executable built successfully: $TargetExe" -ForegroundColor Green
if ($Run) {{ & $TargetExe }}
"""
    with open(os.path.join(projectRoot, "Build", "Construct.ps1"), "w") as f:
        f.write(ps1Content)
    print("  + Generated: Build/Construct.ps1")

    # Generate Project Makefile
    makeContent = f"""CXX := g++
CXXFLAGS := -std=c++20 -O3 -Wall -Wextra -Werror -pedantic -DFRONTIER_DEVELOPMENT -pthread
INCLUDES := -I../.. -I.

GAME_SRCS := $(wildcard Source/*.cpp)
GAME_OBJS := $(GAME_SRCS:.cpp=.o)

ENGINE_DIR := ../..
ENGINE_OBJS := \\
	$(ENGINE_DIR)/DeviceExchange/VulkanExchange.o \\
	$(ENGINE_DIR)/DeviceExchange/ByteSpace.o \\
	$(ENGINE_DIR)/DeviceExchange/TaskScheduler.o \\
	$(ENGINE_DIR)/DeviceExchange/ExecutionQueue.o \\
	$(ENGINE_DIR)/DeviceExchange/VendorClassifier.o \\
	$(ENGINE_DIR)/DeviceExchange/OrientationClassifier.o \\
	$(ENGINE_DIR)/DeviceExchange/WindowExchange.o \\
	$(ENGINE_DIR)/DeviceExchange/InputExchange.o \\
	$(ENGINE_DIR)/DeviceExchange/DiagnosticMetrics.o \\
	$(ENGINE_DIR)/PhysicalDynamics/RigidBodySolver.o \\
	$(ENGINE_DIR)/PhysicalDynamics/DeformableSolver.o \\
	$(ENGINE_DIR)/PhysicalDynamics/LocomotionSolver.o \\
	$(ENGINE_DIR)/PhysicalDynamics/WorldSpace.o \\
	$(ENGINE_DIR)/VolumetricDynamics/LevelSetSpace.o \\
	$(ENGINE_DIR)/VolumetricDynamics/FluidSolver.o \\
	$(ENGINE_DIR)/VolumetricDynamics/ParticleIntegrator.o \\
	$(ENGINE_DIR)/GeometricRaster/GeometryStructure.o \\
	$(ENGINE_DIR)/GeometricRaster/CameraProjection.o \\
	$(ENGINE_DIR)/GeometricRaster/VisibilityProjection.o \\
	$(ENGINE_DIR)/GeometricRaster/RasterSequence.o \\
	$(ENGINE_DIR)/GeometricRaster/MaterialCodec.o \\
	$(ENGINE_DIR)/PhotometricIllumination/ClusteredSpace.o \\
	$(ENGINE_DIR)/PhotometricIllumination/DirectIlluminationIntegrator.o \\
	$(ENGINE_DIR)/PhotometricIllumination/GlobalIlluminationIntegrator.o \\
	$(ENGINE_DIR)/PhotometricIllumination/AtmosphereIntegrator.o \\
	$(ENGINE_DIR)/PlatformInterchange/AcousticStructure.o \\
	$(ENGINE_DIR)/PlatformInterchange/AcousticIntegrator.o \\
	$(ENGINE_DIR)/PlatformInterchange/VoiceExchange.o \\
	$(ENGINE_DIR)/PlatformInterchange/OnlineInterchange.o \\
	$(ENGINE_DIR)/DisplayPresentation/WorkspacePanel.o \\
	$(ENGINE_DIR)/DisplayPresentation/CycleScheduler.o \\
	$(ENGINE_DIR)/DisplayPresentation/FidelityClassifier.o \\
	$(ENGINE_DIR)/DisplayPresentation/FrontierHost.o

TARGET := bin/{projectName}

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(ENGINE_OBJS) $(GAME_OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(ENGINE_DIR)/%.o: $(ENGINE_DIR)/%.cpp
	$(MAKE) -C $(ENGINE_DIR)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(GAME_OBJS) bin/{projectName}
"""
    with open(os.path.join(projectRoot, "Makefile"), "w") as f:
        f.write(makeContent)
    print("  + Generated: Makefile")
        
    print(f"[Frontier Project Generator] Project {projectName} automated build configuration generated successfully.")

if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else "Project-F20"
    dest = sys.argv[2] if len(sys.argv) > 2 else "Projects"
    ScaffoldProject(name, dest)
