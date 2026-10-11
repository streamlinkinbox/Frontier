#!/usr/bin/env python3
"""Build the project-owned scene author with MSVC or GCC/Clang, then generate/verify its opening scene."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tomllib

Root = Path(__file__).resolve().parents[2]
Parser = argparse.ArgumentParser(description=__doc__)
Specification = tomllib.loads((Root / "Projects/Project-Drive/ProjectDrive.frontier").read_text(encoding="utf-8"))
Parser.add_argument("--scene", type=Path, default=Root / "Projects/Project-Drive" / Specification["Project"]["OpeningScene"])
Parser.add_argument("--output-dir", type=Path, default=Root / "Projects/Project-Drive/Build")
Arguments = Parser.parse_args()
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
Manifest = json.loads((Root / "Projects/Project-Drive/Build/DriveContentSources.json").read_text())
Scratch = Root.parent / "_AgentScratch/build/DriveContent"
Scratch.mkdir(parents=True, exist_ok=True)
Arguments.output_dir.mkdir(parents=True, exist_ok=True)
Windows = sys.platform == "win32"
Compiler = "cl.exe" if Windows else os.environ.get("CXX", "g++")
Flags = (["/nologo", "/c", "/O2", "/MD", "/EHsc", "/std:c++20", "/utf-8", "/W3",
          "/DNDEBUG", "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", "/D_CRT_SECURE_NO_WARNINGS"] if Windows
         else ["-c", "-O2", "-std=c++20", "-DNDEBUG", "-pthread", "-Wall", "-Wextra"])
Includes = [("/I" if Windows else "-I") + str(Root / Folder) for Folder in Manifest["includes"]]
Objects = []
for Relative in Manifest["sources"]:
    Source = Root / Relative
    Object = Scratch / (Relative.replace("/", "_") + (".obj" if Windows else ".o"))
    Output = [f"/Fo{Object}"] if Windows else ["-o", str(Object)]
    subprocess.run([Compiler, *Flags, *Includes, str(Source), *Output], check=True, cwd=Root)
    Objects.append(str(Object))
Executable = Arguments.output_dir.resolve() / ("DriveContentHost.exe" if Windows else "DriveContentHost")
Link = (["link.exe", "/nologo", f"/OUT:{Executable}", *Objects] if Windows
        else [Compiler, *Objects, "-pthread", "-o", str(Executable)])
subprocess.run(Link, check=True, cwd=Root)
subprocess.run([str(Executable), "--ensure", str(Arguments.scene.resolve())], check=True, cwd=Root)
