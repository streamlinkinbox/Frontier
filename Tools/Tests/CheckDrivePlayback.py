#!/usr/bin/env python3
"""Compile and exercise real project simulation callbacks and the imported scene/editor feed without a window."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tomllib

Root = Path(__file__).resolve().parents[2]
Scratch = Root.parent / "_AgentScratch/build/DrivePlayback"
Scratch.mkdir(parents=True, exist_ok=True)
Windows = sys.platform == "win32"
Compiler = "cl.exe" if Windows else os.environ.get("CXX", "g++")
Includes = [Root, Root / "Engine", Root / "ExternalPackages/vulkan-headers/include",
            Root / "ExternalPackages/cgltf", Root / "ExternalPackages/stb", Root / "ExternalPackages/ufbx"]
Flags = (["/nologo", "/c", "/O2", "/Gy", "/MD", "/EHsc", "/std:c++20", "/utf-8", "/UNDEBUG",
          "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", "/D_CRT_SECURE_NO_WARNINGS"] if Windows else
         ["-c", "-O1", "-std=c++20", "-UNDEBUG", "-ffunction-sections", "-fdata-sections", "-pthread"])
Flags += [("/I" if Windows else "-I") + str(Folder) for Folder in Includes]
Compiled = {}


def Compile(Relative):
    if Relative not in Compiled:
        Output = Scratch / (Relative.replace("/", "_") + (".obj" if Windows else ".o"))
        Destination = [f"/Fo{Output}"] if Windows else ["-o", str(Output)]
        subprocess.run([Compiler, *Flags, str(Root / Relative), *Destination], cwd=Scratch, check=True)
        Compiled[Relative] = Output
    return str(Compiled[Relative])


def Link(Name, Sources, Arguments=()):
    Output = Scratch / (Name + (".exe" if Windows else ""))
    Objects = [Compile(Source) for Source in Sources]
    Command = (["link.exe", "/nologo", "/OPT:REF", f"/OUT:{Output}", *Objects] if Windows else
               [Compiler, *Objects, "-pthread", "-Wl,--gc-sections", "-o", str(Output)])
    subprocess.run(Command, cwd=Scratch, check=True)
    subprocess.run([str(Output), *map(str, Arguments)], cwd=Scratch, check=True)
    return Output


Link("DeploymentPointChecks", ["../VisualProof/DeploymentPoint/DeploymentPointChecks.cpp"])

Simulation = json.loads((Root / "Projects/Project-Drive/Build/DriveSimulationSources.json").read_text())["sources"]
Link("DriveInterchangeChecks", [*Simulation, "Projects/Project-Drive/Source/DriveInterchangeChecks.cpp"])
Link("DriveSpawnChecks", [*Simulation, "Projects/Project-Drive/Source/DriveSpawnChecks.cpp"])
Content = json.loads((Root / "Projects/Project-Drive/Build/DriveContentSources.json").read_text())["sources"]
Scene = Scratch / "DriveCourse.gltf"
# The file is generated within this check's own scratch directory, never over the user's authored scene.
if Scene.exists():
    Scene.unlink()
Author = Link("DriveContentHost", Content, ["--ensure", Scene])
Original = Scene.read_bytes()
Specification = tomllib.loads((Root / "Projects/Project-Drive/ProjectDrive.frontier").read_text(encoding="utf-8"))
assert Path(Specification["Project"]["OpeningScene"]).stem == json.loads(Original)["scenes"][0]["name"]
subprocess.run([str(Author), "--ensure", str(Scene)], cwd=Scratch, check=True)
assert Scene.read_bytes() == Original
Older = Original.replace(b'"DriveCourse.r4"', b'"DriveCourse.r3"')
assert Older != Original
Scene.write_bytes(Older)
subprocess.run([str(Author), "--ensure", str(Scene)], cwd=Scratch, check=True)
assert Scene.read_bytes() == Older, "Existing scenes, even older generated copies, must not be overwritten"
Scene.write_bytes(Original)
print("PASS opening filename matches authored revision; ensure preserves both current and existing older scenes")
Roster = [Source for Source in Content if not Source.endswith("DriveContentHost.cpp")]
Link("DrivePlacementChecks", [*Roster, "Engine/GeometricRaster/CameraProjection.cpp",
                              "Engine/Host/EditorFeedSequence.cpp",
                              "Projects/Project-Drive/Source/DrivePlacementChecks.cpp"], [Scene])
print("PASS project playback and imported scene/roster checks; no window or GPU was used.")
