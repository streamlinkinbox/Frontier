#!/usr/bin/env python3
"""Stage the MSVC Release host and both projects, verifying DLL ABI and actual opening-scene import before upload."""
from __future__ import annotations

import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import sys
import tomllib
import zlib

Root = Path(__file__).resolve().parents[2]
Binary = Root / "Build/Output/Windows/Release/Binary"
Destination = Root.parent / "_AgentScratch/build/Frontier-Windows-Release"
Projects = (("Project-Zero", "ProjectZero"), ("Project-Drive", "ProjectDrive"))


class ProjectInterchange(ctypes.Structure):
    # Exact revision-3 C layout from Engine/ProjectInterchange/ProjectInterchange.h.
    _fields_ = [("StructureSize", ctypes.c_uint32), ("CodeInterchangeNumber", ctypes.c_uint32),
                ("InterfaceFingerprint", ctypes.c_uint64), ("ConstructProject", ctypes.c_void_p),
                ("AdvanceProject", ctypes.c_void_p), ("RetireProject", ctypes.c_void_p)]


class ProjectRefusal(ctypes.Structure):
    _fields_ = [("Number", ctypes.c_uint32), ("Explanation", ctypes.c_char * 1024)]


def VerifyImage(Location: Path, Dll: bool) -> None:
    Content = Location.read_bytes()
    if Content[:2] != b"MZ":
        raise RuntimeError(f"Not a Windows binary: {Location}")
    Offset = struct.unpack_from("<I", Content, 0x3C)[0]
    if Content[Offset:Offset + 4] != b"PE\0\0":
        raise RuntimeError(f"Missing PE signature: {Location}")
    Machine = struct.unpack_from("<H", Content, Offset + 4)[0]
    Characteristics = struct.unpack_from("<H", Content, Offset + 22)[0]
    if Machine != 0x8664 or bool(Characteristics & 0x2000) != Dll:
        raise RuntimeError(f"Expected an x64 {'DLL' if Dll else 'executable'}: {Location}")


def VerifyInterchange(Location: Path, Specification: dict) -> None:
    Image = ctypes.CDLL(str(Location))
    Entry = Image.ConstructProjectInterchange
    Entry.argtypes = [ctypes.c_uint32, ctypes.c_uint64,
                      ctypes.POINTER(ProjectInterchange), ctypes.POINTER(ProjectRefusal)]
    Entry.restype = ctypes.c_uint32
    Delivered = ProjectInterchange()
    Refusal = ProjectRefusal()
    Revision = Specification["CodeInterchangeNumber"]
    Fingerprint = Specification["InterfaceFingerprint"]
    if Revision != 3:
        raise RuntimeError("The package verifier requires the revision-3 C layout")
    if Entry(Revision, Fingerprint, ctypes.byref(Delivered), ctypes.byref(Refusal)) != 1:
        raise RuntimeError(f"{Location.name} rejected its specification: {Refusal.Explanation!r}")
    if (Delivered.StructureSize != ctypes.sizeof(ProjectInterchange)
            or Delivered.CodeInterchangeNumber != Revision
            or Delivered.InterfaceFingerprint != Fingerprint
            or not all((Delivered.ConstructProject, Delivered.AdvanceProject, Delivered.RetireProject))):
        raise RuntimeError(f"Invalid code interchange from {Location.name}")
    for RequestedRevision, RequestedFingerprint in ((Revision - 1, Fingerprint), (Revision, Fingerprint ^ 1)):
        Refusal = ProjectRefusal()
        if (Entry(RequestedRevision, RequestedFingerprint, ctypes.byref(Delivered), ctypes.byref(Refusal)) != 0
                or Refusal.Number == 0):
            raise RuntimeError(f"{Location.name} accepted an incompatible code interchange")
    print(f"PASS {Location.name}: x64 DLL loaded; revision and fingerprint accepted; incompatible requests refused")


def VerifyScene(SpecificationPath: Path, SceneOverride: Path | None = None, ExpectedCode: int = 0) -> None:
    Command = [str(Destination / "Frontier.exe"), str(SpecificationPath), "--verify-opening-scene"]
    if SceneOverride is not None:
        Command += ["--scene", str(SceneOverride)]
    Completed = subprocess.run(Command, cwd=Destination, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               encoding="utf-8", errors="replace", timeout=180)
    print(Completed.stdout)
    if Completed.returncode != ExpectedCode:
        raise RuntimeError(f"Scene verification returned {Completed.returncode}, expected {ExpectedCode}: {Command}")
    if ExpectedCode == 0 and "PASS opening scene import:" not in Completed.stdout:
        raise RuntimeError("Host did not confirm the opening-scene import")



def CopyVisualCppRuntime() -> None:
    Locator = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    Installation = subprocess.check_output([str(Locator), "-latest", "-products", "*", "-requires",
                                            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                                           text=True).strip()
    Candidates = sorted((Path(Installation) / "VC/Redist/MSVC").glob("*/x64/Microsoft.VC*.CRT"))
    if not Candidates:
        raise RuntimeError("No redistributable x64 Visual C++ runtime found")
    for Library in Candidates[-1].glob("*.dll"):
        VerifyImage(Library, True)
        shutil.copy2(Library, Destination)
    if not all((Destination / Name).is_file() for Name in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")):
        raise RuntimeError("Incomplete app-local Visual C++ runtime")


def VerifyBrowser() -> None:
    PixelsPath = Destination.parent / "PackagedProjectBrowser.ppm"
    subprocess.run([str(Destination / "Frontier.exe"), "--verify-project-browser", str(PixelsPath)],
                   cwd=Destination, check=True, timeout=90)
    Magic, Extent, Maximum, Pixels = PixelsPath.read_bytes().split(b"\n", 3)
    Width, Height = map(int, Extent.split())
    if Magic != b"P6" or Maximum != b"255" or (Width, Height) != (840, 640) or len(Pixels) != Width * Height * 3:
        raise RuntimeError("Packaged browser did not return the expected DX11 pixels")
    ColourPixels = sum(max(Pixels[Offset:Offset+3]) - min(Pixels[Offset:Offset+3]) > 10
                       for Row in range(145, 270) for Column in range(36, 296)
                       for Offset in [(Row * Width + Column) * 3])
    if ColourPixels <= 500 or Pixels[(540*Width+600)*3:(540*Width+600)*3+3] != b"\0\0\0":
        raise RuntimeError("Packaged browser must render its actual project image on the OLED-black theme")
    def Chunk(Signature, Content):
        return struct.pack(">I", len(Content)) + Signature + Content + struct.pack(">I", zlib.crc32(Signature + Content) & 0xFFFFFFFF)
    Raster = b"".join(b"\0" + Pixels[Row*Width*3:(Row+1)*Width*3] for Row in range(Height))
    Png = b"\x89PNG\r\n\x1a\n" + Chunk(b"IHDR", struct.pack(">IIBBBBB", Width, Height, 8, 2, 0, 0, 0))
    (Destination / "Docs/ProjectBrowser.png").write_bytes(Png + Chunk(b"IDAT", zlib.compress(Raster)) + Chunk(b"IEND", b""))
    print("PASS packaged Frontier.exe: actual Win32/DX11 browser, OLED-black pixels and project preview texture")


def Main() -> None:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if sys.platform != "win32" or ctypes.sizeof(ctypes.c_void_p) != 8:
        raise RuntimeError("Run after the Release build using x64 Python on Windows")
    VerifyImage(Binary / "Frontier.exe", False)
    VerifyImage(Binary / "glfw3.dll", True)
    Shaders = sorted((Binary / "Engine/Shaders").glob("*.spv"))
    if not Shaders:
        raise RuntimeError("No compiled Vulkan shaders found")
    for Name in ("DistanceFieldConstruct", "DistanceFieldCapture", "DistanceFieldCaptureFixed", "DistanceFieldRadiance", "DistanceFieldGather", "DistanceFieldGatherFixed", "DistanceFieldGIResolve", "DistanceFieldGIResolveFixed"):
        if not (Binary / f"Engine/Shaders/{Name}.spv").is_file():
            raise RuntimeError(f"Missing compiled {Name} shader")
    for Shader in Shaders:
        if Shader.read_bytes()[:4] != b"\x03\x02\x23\x07":
            raise RuntimeError(f"Invalid SPIR-V signature: {Shader}")

    if Destination.exists():
        shutil.rmtree(Destination)
    shutil.copytree(Binary, Destination, ignore=shutil.ignore_patterns("*.pdb", "*.ilk", "*.lib", "*.exp"))
    CopyVisualCppRuntime()
    shutil.copytree(Root / "EngineContent", Destination / "EngineContent", dirs_exist_ok=True)
    PackagedProjects = []
    for Folder, Name in Projects:
        Source = Root / "Projects" / Folder
        SpecificationPath = Source / f"{Name}.frontier"
        Specification = tomllib.loads(SpecificationPath.read_text(encoding="utf-8"))["Project"]
        if Specification["CodeImage"] != f"Build/{Name}.dll" or Specification["ContentLocation"] != "Content":
            raise RuntimeError(f"Unexpected project layout: {SpecificationPath}")
        Target = Destination / "Projects" / Folder
        (Target / "Build").mkdir(parents=True)
        shutil.copy2(SpecificationPath, Target)
        shutil.copytree(Source / "Content", Target / "Content")
        if (Source / "Preview").is_dir():
            shutil.copytree(Source / "Preview", Target / "Preview")
        Library = Target / Specification["CodeImage"]
        shutil.copy2(Source / Specification["CodeImage"], Library)
        VerifyImage(Library, True)
        VerifyInterchange(Library, Specification)

        OpeningScene = Target / Specification["OpeningScene"]
        Preparation = ""
        if Name == "ProjectDrive":
            Author = Target / "Build/DriveContentHost.exe"
            shutil.copy2(Source / "Build/DriveContentHost.exe", Author)
            VerifyImage(Author, False)
            # Export only missing content; malformed existing content must fail, not be silently replaced.
            subprocess.run([str(Author), "--ensure", str(OpeningScene)], check=True, timeout=180)
            SceneRelative = str(Path(Specification["OpeningScene"])).replace("/", "\\")
            Preparation = (
                f'if exist "%~dp0Projects\\{Folder}\\{SceneRelative}" goto LaunchFrontier\n'
                f'"%~dp0Projects\\{Folder}\\Build\\DriveContentHost.exe" --ensure '
                f'"%~dp0Projects\\{Folder}\\{SceneRelative}"\n'
                'if errorlevel 1 goto Finished\n:LaunchFrontier\n')
        VerifyScene(Target / SpecificationPath.name)
        if not OpeningScene.is_file() or OpeningScene.stat().st_size == 0:
            raise RuntimeError(f"Package has no opening scene: {OpeningScene}")
        # Exercise the exact failure this gate missed previously, plus a present but corrupt glTF.
        with tempfile.TemporaryDirectory(prefix="Scene verification ", dir=Destination.parent) as Scratch:
            Missing = Path(Scratch) / "missing.gltf"
            Corrupt = Path(Scratch) / "corrupt.gltf"
            Corrupt.write_text("this is not glTF", encoding="ascii")
            VerifyScene(Target / SpecificationPath.name, Missing, ExpectedCode=1)
            VerifyScene(Target / SpecificationPath.name, Corrupt, ExpectedCode=1)
        print(f"PASS {Name}: packaged scene imports; missing and corrupt scenes are refused without a GPU")
        Launcher = Destination / f"Start-{Name}.cmd"
        Launcher.write_text(
            '@echo off\nsetlocal\ncd /d "%~dp0"\n' + Preparation +
            f'"%~dp0Frontier.exe" "%~dp0Projects\\{Folder}\\{Name}.frontier" %*\n'
            ':Finished\nset "FrontierExitCode=%ERRORLEVEL%"\nif not "%FrontierExitCode%"=="0" pause\n'
            'exit /b %FrontierExitCode%\n', encoding="ascii", newline="\r\n")
        PackagedProjects.append({"name": Name, "launcher": Launcher.name,
                                 "specification": SpecificationPath.relative_to(Root).as_posix(),
                                 "dllAbiVerified": True, "openingSceneImported": True,
                                 "openingScene": OpeningScene.relative_to(Destination).as_posix(),
                                 "missingAndCorruptSceneRefusalVerified": True})

    (Destination / "Docs").mkdir(exist_ok=True)
    (Destination / "Docs/ProjectBrowser.txt").write_text(
        "FRONTIER WINDOWS x64 / RELEASE\n\n"
        "Extract the entire ZIP, then double-click Frontier.exe to open the project browser.\n"
        "Select a project and scene, choose session options, then Open project.\n"
        "The Start-Project*.cmd files launch a project directly, bypassing the browser.\n\n"
        "CUSTOM PREVIEWS\n"
        "Place an image in the Preview folder beside your project's .frontier file.\n"
        "Example: Projects/Project-Drive/Preview/My car.jpg\n"
        "Any image placed directly in Preview overrides the bundled picture. Then click Scan.\n"
        "Any filename: PNG, JPEG, BMP, GIF or TIFF. With several images, the alphabetically\n"
        "first supported filename wins. Bundled fallback: Preview/Default/Project.png.\n"
        "GIF/TIFF use the first frame/page.\n"
        "Missing or corrupt images show a folder icon; hover the large preview for the reason.\n"
        "Images retain their aspect ratio and transparency. Limit: 32 MiB, 64 million pixels,\n"
        "32768 pixels per axis. Decoded thumbnails are bounded to 512 x 288.\n\n"
        "REQUIREMENTS\n"
        "Windows x64 and a compatible Vulkan GPU with its installed driver.\n"
        "The Visual C++ runtime DLLs are included. This is an unsigned test build.\n"
        "BuildManifest.json identifies the source and file hashes. The browser was rendered\n"
        "and read back on Windows in CI; physical-GPU gameplay is not verified by CI.\n",
        encoding="utf-8")
    VerifyBrowser()
    Manifest = {"sourceCommit": os.environ.get("GITHUB_SHA", "local"),
                "configuration": "Release", "architecture": "x64", "compiler": "MSVC",
                "projects": PackagedProjects, "compiledShaderCount": len(Shaders),
                "projectBrowserDx11Verified": True, "visualCppRuntimeBundled": True,
                "previewFolder": "Preview", "previewSelection": "alphabetically first supported filename",
                "gpuRuntimeVerified": False,
                "gpuRuntimeVerificationScope": "Interactive Windows host on physical GPU hardware is not exercised by CI",
                "sdfGi": {"importedCommit": "46476f3e5632256297de795ef1a6ab247f92f5f0",
                          "shaderCompiled": True, "runtimeEnabled": True,
                          "softwareVulkanExecutionVerified": os.environ.get("SDF_VULKAN_EXECUTION_VERIFIED") == "1",
                          "executionValidation": "Production SPIR-V with synchronization and GPU-assisted shader-access validation",
                          "transport": "Three camera-snapped distance volumes, GPU-captured textured surface-card atlas with Jacobi radiance, exact mesh secondary rays",
                          "activation": "Only after all scene resources and shader pipelines are ready",
                          "fallback": "Existing Surfel GI, or existing compute fallback if Surfel is unavailable"},
                "requirements": ["Windows x64", "Microsoft Visual C++ x64 runtime (bundled)",
                                 "Compatible Vulkan GPU and its installed driver"],
                "sha256": {Location.relative_to(Destination).as_posix(): hashlib.sha256(Location.read_bytes()).hexdigest()
                           for Location in sorted(Destination.rglob("*")) if Location.is_file()}}
    (Destination / "BuildManifest.json").write_text(json.dumps(Manifest, indent=2) + "\n", encoding="utf-8")
    print(f"PASS package: Frontier.exe, both project DLLs, specifications, content and {len(Shaders)} shaders")
    Revision = os.environ.get("GITHUB_SHA", "local")[:7]
    Archive = Path(shutil.make_archive(str(Destination.parent / f"Frontier-Windows-x64-{Revision}"), "zip", Destination))
    Checksum = hashlib.sha256(Archive.read_bytes()).hexdigest()
    Archive.with_suffix(".sha256").write_text(f"{Checksum}  {Archive.name}\n", encoding="ascii")
    print(f"Package: {Destination}")
    print(f"ZIP: {Archive} / SHA256 {Checksum}")


if __name__ == "__main__":
    try:
        Main()
    except (OSError, RuntimeError, ValueError, AttributeError, struct.error, subprocess.SubprocessError) as Error:
        print(f"Package failed: {Error}", file=sys.stderr)
        raise SystemExit(1)
