#!/usr/bin/env python3
"""Build and run the native force field checks, twice, and insist the two runs agree.

    python3 Exhibits/Workbench/GasFluid/RunNativeForceFields.py

Two runs into separate build directories, because these numbers end up in a replicated simulation and
anything only nearly deterministic desynchronises a peer somewhere else entirely. The header is
dependency-free, so this is a single translation unit and the build is quick.
"""
import subprocess
import sys
from pathlib import Path

Root = Path(__file__).resolve().parents[3]
Flags = ['-std=c++20', '-O2', '-Wall', '-Wextra']
Source = 'Exhibits/Workbench/GasFluid/NativeForceFields.cpp'

Transcripts = []
for Attempt in ('first', 'second'):
    Build = Root / f'.cache/forcefieldbuild-{Attempt}'
    Build.mkdir(parents=True, exist_ok=True)
    Binary = Build / 'ForceFieldProof'
    subprocess.run(['g++', *Flags, Source, '-o', str(Binary)], cwd=Root, check=True)
    Result = subprocess.run([str(Binary)], cwd=Root, capture_output=True, text=True)
    sys.stdout.write(Result.stdout)
    if Result.returncode != 0:
        sys.stderr.write(Result.stderr)
        raise SystemExit('native force field proof failed')
    Transcripts.append(Result.stdout)

if Transcripts[0] != Transcripts[1]:
    raise SystemExit('the two runs disagree; the force field arithmetic is not reproducible')

print('\nBoth runs agree, byte for byte.')
