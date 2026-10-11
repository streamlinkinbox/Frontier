#!/usr/bin/env python3
"""Build the SolidArc Windows GUI beside Frontier Release and package its runtime content."""
from pathlib import Path
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys

Root = Path(__file__).resolve().parents[2]
Source = Root / "Editor/AuthoringTools/Modelling/SolidArc"
Work = Root.parent / "_AgentScratch/build/solidarc-application"
Package = Root / "Build/SolidArc"

def Execute(Command, Name, Cwd=Root, Timeout=600):
    Completed = subprocess.run(list(map(str, Command)), cwd=Cwd, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, encoding="utf-8", errors="replace", timeout=Timeout)
    (Work / (Name + ".log")).write_text(Completed.stdout, encoding="utf-8")
    if Completed.returncode:
        print(Completed.stdout, flush=True)
        for Line in Completed.stdout.splitlines():
            if "error" in Line.lower() or "refused" in Line.lower():
                print("::error::" + Line.replace("%", "%25"))
        raise RuntimeError(f"{Name} failed ({Completed.returncode})")
    return Completed.stdout

def Main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    if sys.platform != "win32" or not shutil.which("cl.exe"):
        raise RuntimeError("Use an x64 MSVC developer shell after ToolchainSequence.ps1 -Configuration Release")
    Work.mkdir(parents=True, exist_ok=True)
    if Package.exists():
        shutil.rmtree(Package)
    Package.mkdir(parents=True)
    EngineObjects = Root / "Build/Output/Windows/Release/Object"
    if not (EngineObjects / "ViewportPanel.obj").exists():
        raise RuntimeError("Build Frontier Release first; the SolidArc GUI reuses its compiled editor facilities")
    # Archive rather than directly linking: the application pulls shared UI objects, not the game main/runtime.
    # The two historical CameraProjection classes must not be linked into the same application.
    Excluded = {"CameraProjection.obj", "RayTracingSolver.obj", "FlyThroughSolver.obj", "EditorHost.obj", "FrontierRuntime.obj", "FrontierHost.obj"}
    Objects = [Path for Path in EngineObjects.glob("*.obj") if Path.name not in Excluded]
    Response = Work / "Shared.rsp"
    Response.write_text("\n".join(['"'+str(Path)+'"' for Path in Objects]), encoding="utf-8")
    Execute(["lib.exe", "/nologo", f"/OUT:{Work / 'Shared.lib'}", "@"+str(Response)], "Shared")
    Vulkan = os.environ.get("VULKAN_SDK")
    if not Vulkan:
        Installed = sorted(Path("C:/VulkanSDK").glob("*/Include"), reverse=True)
        if not Installed:
            raise RuntimeError("Vulkan SDK is required for SolidArc GUI compilation")
        Vulkan = str(Installed[0].parent)
    Includes = [Root, Root / "Engine", Source, Source / "Presentation", Root / "ExternalPackages/imgui",
                Root / "ExternalPackages/imgui/backends", Root / "Engine/DisplayPresentation",
                Root / "ExternalPackages/stb", Root / "ExternalPackages/tomlpp/include",
                Path(Vulkan) / "Include"]
    Flags = ["/nologo", "/c", "/std:c++20", "/EHsc", "/MD", "/O2", "/W4", "/utf-8", "/DNOMINMAX",
             "/DUNICODE", "/D_UNICODE", "/D_CRT_SECURE_NO_WARNINGS", "/DFRONTIER_DEVELOPMENT", "/DTVG_STATIC",
             '/DSOLIDARC_PROOF_FOLDER="Documents"', *["/I"+str(Path) for Path in Includes]]
    Sources = [Path for Folder in ("Kernel", "Presentation", "Interaction", "Document", "Console", "Editor")
               for Path in sorted((Source / Folder).glob("*.cpp")) if Path.name != "SolidArcConsole.cpp"]
    Sources += [Root / "ExternalPackages/imgui/backends" / Name for Name in ("imgui_impl_win32.cpp", "imgui_impl_dx11.cpp")]
    def Compile(Path):
        Destination = Work / (Path.parent.name + "_" + Path.stem + ".obj")
        Execute(["cl.exe", *Flags, Path, "/Fo"+str(Destination)], Destination.stem)
        return Destination
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as Workers:
        Compiled = list(Workers.map(Compile, Sources))
    Executable = Package / "SolidArc.exe"
    Libraries = [Work / "Shared.lib", Root / "ExternalPackages/thorvg/lib/Release/thorvg.lib",
                 Root / "ExternalPackages/jolt/lib/Release/Jolt.lib",
                 Root / "ExternalPackages/glfw/lib-vc2022/glfw3dll.lib", Path(Vulkan) / "Lib/vulkan-1.lib",
                 "d3d11.lib", "dxgi.lib", "d3dcompiler.lib", "user32.lib", "gdi32.lib", "shell32.lib", "comdlg32.lib", "dwmapi.lib", "imm32.lib", "ole32.lib", "uuid.lib", "windowscodecs.lib"]
    Execute(["link.exe", "/nologo", "/SUBSYSTEM:WINDOWS", "/MANIFEST:EMBED", "/MANIFESTINPUT:"+str(Source / "Editor/SolidArcApplication.manifest"), "/OPT:REF", "/OUT:"+str(Executable), *Compiled, *Libraries], "Link")
    shutil.copytree(Root / "EngineContent", Package / "EngineContent",
                    ignore=shutil.ignore_patterns("GeometryArchives"))
    # Ship the runtime beside the GUI so the portable package does not require a separate VC++ installer.
    Redist = Path(os.environ.get("VCToolsRedistDir", "")) / "x64"
    Runtimes = sorted(Redist.glob("Microsoft.VC*.CRT"))
    if not Runtimes:
        raise RuntimeError("VC++ redistributable directory is missing")
    for Dll in Runtimes[-1].glob("*.dll"):
        shutil.copy2(Dll, Package / Dll.name)
    (Package / "BuildManifest.json").write_text(json.dumps({"sourceCommit": os.environ.get("GITHUB_SHA", "local"),
        "application": "SolidArc.exe", "presentation": "native D3D11 (hardware or WARP)",
        "interface": "shared SolidArcEditorHost with outliner, inspector and document dialogs",
        "note": "Windows GUI linked; open on a Windows desktop to test interaction"}, indent=2))
    print(f"PASS runnable SolidArc application: {Package}")

if __name__ == "__main__":
    Main()
