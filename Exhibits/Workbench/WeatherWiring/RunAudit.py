#!/usr/bin/env python3
"""Linux/GCC diagnostic: compile actual sequence packers; no GPU required."""
import os
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / 'build/weather-wiring-audit'
BUILD.mkdir(parents=True, exist_ok=True)
includes = [ROOT/'Engine', ROOT/'Projects/Project-Zero/Source',
            ROOT/'ExternalPackages/imgui', ROOT/'ExternalPackages/tomlpp/include',
            ROOT/'ExternalPackages/stb', ROOT/'ExternalPackages/vulkan-headers/include']
includes += sorted(p for p in (ROOT/'Engine').iterdir() if p.is_dir())
sources = ['Exhibits/Workbench/WeatherWiring/WeatherWiringAudit.cpp',
           'Projects/Project-Zero/Source/CelestialSequence.cpp',
           'Engine/DisplayPresentation/CelestialSolver.cpp',
           'Engine/GeometricRaster/StarCatalogueIndex.cpp',
           'Engine/ContentInterchange/AssetResolution.cpp']
command = [os.environ.get('CXX', 'g++'), '-std=c++20', '-O2', '-ffunction-sections', '-fdata-sections',
           *['-I'+str(p) for p in includes], *[str(ROOT/p) for p in sources],
           '-Wl,--gc-sections', '-o', str(BUILD/'WeatherWiringAudit')]
subprocess.run(command, check=True, cwd=ROOT)
result = subprocess.run([str(BUILD/'WeatherWiringAudit')], check=True, cwd=ROOT, capture_output=True, text=True)
(BUILD/'results.txt').write_text(result.stdout)
print(result.stdout, end='')
