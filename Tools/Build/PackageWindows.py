#!/usr/bin/env python3
"""Package the linked Windows desktop build, its projects and its compiled shaders in one zip."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import tomllib
import zipfile

Root = Path(__file__).resolve().parents[2]
Build = Root / 'Build'
Stage = Build / 'WindowsDesktop'
Output = Build / 'Frontier-Windows-x64.zip'


def Copy(Source, Destination):
    if not Source.is_file():
        raise FileNotFoundError(f'Required Windows build output is missing: {Source}')
    Destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(Source, Destination)


def Main():
    if Stage.exists():
        shutil.rmtree(Stage)
    Stage.mkdir(parents=True)
    for Name in ('Frontier.exe', 'glfw3.dll'):
        Copy(Build / Name, Stage / Name)
    for Name in ('Project-Zero', 'Project-Drive'):
        Project = Root / 'Projects' / Name
        for Spec in Project.glob('*.frontier'):
            Copy(Spec, Stage / 'Projects' / Name / Spec.name)
        Image = 'ProjectZero' if Name == 'Project-Zero' else 'ProjectDrive'
        Copy(Project / 'Build' / (Image + '.dll'), Stage / 'Projects' / Name / 'Build' / (Image + '.dll'))
    shutil.copytree(Root / 'Projects/Project-Zero/Content', Stage / 'Projects/Project-Zero/Content')
    for Name in ('Project-Zero', 'Project-Drive'):
        Preview = Root / 'Projects' / Name / 'Preview'
        if Preview.is_dir():
            shutil.copytree(Preview, Stage / 'Projects' / Name / 'Preview')
    Dyno = Root / 'Projects/Project-Dyno/Build/Output/Windows/Release/Binary/Project-Dyno.exe'
    Copy(Dyno, Stage / 'Tools/Project-Dyno.exe')
    DriveContent = Root / 'Projects/Project-Drive/Content'
    if DriveContent.exists():
        shutil.copytree(DriveContent, Stage / 'Projects/Project-Drive/Content')
    Shaders = sorted((Root / 'Engine/Shaders').glob('*.spv'))
    if not Shaders:
        raise RuntimeError('No compiled SPIR-V shaders found')
    for Shader in Shaders:
        Copy(Shader, Stage / 'Engine/Shaders' / Shader.name)
    Manifest = Root / 'Engine/Shaders/ShaderManifest.json'
    if Manifest.exists():
        Copy(Manifest, Stage / 'Engine/Shaders/ShaderManifest.json')
    shutil.copytree(Root / 'EngineContent', Stage / 'EngineContent',
                    ignore=shutil.ignore_patterns('GeometryArchives'))
    SolidArc = Build / 'SolidArc'
    Copy(SolidArc / 'SolidArc.exe', Stage / 'SolidArc/SolidArc.exe')
    shutil.copytree(SolidArc / 'EngineContent', Stage / 'SolidArc/EngineContent')
    Copy(SolidArc / 'BuildManifest.json', Stage / 'SolidArc/BuildManifest.json')
    for Dll in SolidArc.glob('*.dll'):
        Copy(Dll, Stage / 'SolidArc' / Dll.name)
        Copy(Dll, Stage / Dll.name)

    # Project-Zero authors Materials.gltf on first launch. The project browser refuses
    # missing scenes before the author can run; export and import it on the CI runner,
    # without opening Vulkan, so the extracted bundle is usable on its first launch.
    Spec = Stage / 'Projects/Project-Zero/ProjectZero.frontier'
    Opening = tomllib.loads(Spec.read_text(encoding='utf-8'))['Project']['OpeningScene']
    Scene = Stage / 'Projects/Project-Zero' / Opening
    if Scene.resolve().parent != (Stage / 'Projects/Project-Zero/Content/Scenes').resolve():
        raise RuntimeError(f'Unexpected Project-Zero opening scene: {Opening}')
    Result = subprocess.run([str(Stage / 'Frontier.exe'), str(Spec), '--verify-opening-scene'],
                            cwd=Stage, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=180)
    print(Result.stdout, flush=True)
    if Result.returncode or 'PASS opening scene import: ProjectZero' not in Result.stdout:
        raise RuntimeError(f'Packaged Project-Zero scene import failed (exit {Result.returncode})')
    if not Scene.is_file() or Scene.stat().st_size == 0:
        raise RuntimeError(f'Packaged Project-Zero opening scene is missing/empty: {Scene}')
    # The verification process writes CI-only telemetry; don't ship it as user diagnostics.
    for Logs in (Stage / 'Diagnostics', Stage / 'Build/Diagnostics'):
        if Logs.is_dir():
            shutil.rmtree(Logs)

    # Keep a console and exit status even if the child exits before it can report ready
    # to the browser. The last GPU heartbeat alone cannot identify a crash cause.
    (Stage / 'Diagnose-ProjectZero.cmd').write_text(
        '@echo off\r\nsetlocal\r\ncd /d "%~dp0"\r\n'
        'if not exist "Diagnostics" mkdir "Diagnostics"\r\n'
        'echo Running Project-Zero; please wait. Output: Diagnostics\\ProjectZero-launch.txt\r\n'
        '"%~dp0Frontier.exe" "%~dp0Projects\\Project-Zero\\ProjectZero.frontier" %* '
        '> "Diagnostics\\ProjectZero-launch.txt" 2>&1\r\n'
        'set "FrontierExitCode=%ERRORLEVEL%"\r\n'
        'echo Frontier exit code: %FrontierExitCode%\r\n'
        'echo Frontier exit code: %FrontierExitCode% >> "Diagnostics\\ProjectZero-launch.txt"\r\n'
        'echo Send Diagnostics\\ProjectZero-launch.txt and Build\\Diagnostics\\startup-*.csv if available.\r\n'
        'pause\r\nexit /b %FrontierExitCode%\r\n', encoding='ascii', newline='')
    Commit = __import__('os').environ.get('GITHUB_SHA', 'local')
    (Stage / 'BuildManifest.json').write_text(json.dumps({
        'sourceCommit': Commit, 'platform': 'Windows x64', 'configuration': 'Release',
        'applications': ['Frontier.exe', 'SolidArc/SolidArc.exe'],
        'projectImages': ['Projects/Project-Zero/Build/ProjectZero.dll',
                          'Projects/Project-Drive/Build/ProjectDrive.dll'],
        'verifiedOpeningScenes': ['Projects/Project-Zero/' + Opening],
        'additionalTools': ['Tools/Project-Dyno.exe'], 'shaderCount': len(Shaders),
        'note': 'Project-Zero scene exported/imported on CI; interactive GPU startup requires a Windows desktop.'
    }, indent=2), encoding='utf-8')
    (Stage / 'START-HERE.txt').write_text(
        'Extract the whole zip before running. On Windows x64, double-click Frontier.exe '
        'or SolidArc\\SolidArc.exe. In Frontier use Add (+) > Authoring Tools > Open in SolidArc. '
        'Keep the folders alongside the executables. Frontier.exe accepts a path to '
        'Projects\\Project-Zero\\ProjectZero.frontier or Projects\\Project-Drive\\ProjectDrive.frontier. '
        'A Vulkan-capable driver is needed for Frontier; SolidArc uses Direct3D 11.\n'
        'Project-Zero opens Projects\\Project-Zero\\Content\\Scenes\\Materials.gltf. '
        'This scene is generated and imported during packaging; do not run from inside the zip.\n'
        'If Frontier closes unexpectedly, run Diagnose-ProjectZero.cmd from the extracted folder. '
        'It keeps the console open and saves the exit code and output in Diagnostics\\ProjectZero-launch.txt. '
        'Also send Build\\Diagnostics\\startup-*.csv if present and the Windows Reliability Monitor crash details. '
        'The last GPU wait line alone cannot identify the cause of a process exit.\n'
        'See BuildManifest.json for the source commit and shader count.\n', encoding='utf-8')
    if Output.exists():
        Output.unlink()
    with zipfile.ZipFile(Output, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as Zip:
        for File in Stage.rglob('*'):
            if File.is_file():
                Zip.write(File, File.relative_to(Stage))
    with zipfile.ZipFile(Output) as Zip:
        Bad = Zip.testzip()
        if Bad:
            raise RuntimeError(f'Bundle zip is corrupt at {Bad}')
        Required = ('Frontier.exe', 'SolidArc/SolidArc.exe',
                    'Projects/Project-Zero/ProjectZero.frontier',
                    'Projects/Project-Zero/Build/ProjectZero.dll',
                    'Projects/Project-Zero/' + Opening,
                    'Projects/Project-Zero/Preview/Default/Project.png',
                    'Diagnose-ProjectZero.cmd')
        for Name in Required:
            if Name not in Zip.namelist() or Zip.getinfo(Name).file_size == 0:
                raise RuntimeError(f'Windows bundle missing required nonempty entry: {Name}')
    print(f'PASS {Output} ({Output.stat().st_size} bytes), Project-Zero scene and preview, {len(Shaders)} shaders, SHA256 {hashlib.sha256(Output.read_bytes()).hexdigest()}')


if __name__ == '__main__':
    Main()
