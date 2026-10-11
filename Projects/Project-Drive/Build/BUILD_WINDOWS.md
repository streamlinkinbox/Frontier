# Project-Drive through Frontier.exe

Project-Drive is opened by `Frontier.exe`; it is not a windowed executable. The shared host owns the window, Vulkan
estate, renderer, editor, input, camera facilities, celestial environment, and shared GPU/CPU Surfel GI.

## Windows build route

The authoritative MSVC route builds `Frontier.exe`, `ProjectZero.dll`, and `ProjectDrive.dll` without compiling the
shared host source into either project image:

```powershell
powershell -File Tools\Build\ToolchainSequence.ps1
```

`Projects\Project-Drive\Build\ToolchainSequence.ps1` forwards to that route for compatibility. It does not define a
second target or invoke a second windowed process.

The project code image is staged at `Projects\Project-Drive\Build\ProjectDrive.dll`. Its declarative opening is
`Projects\Project-Drive\ProjectDrive.frontier`.

## Opening the project

```text
Frontier.exe Projects/Project-Drive/ProjectDrive.frontier
```

The opening specification owns the content root and opening scene. It must not resolve either through Project-Zero.
Project-specific vehicle behaviour belongs behind `ProjectDriveInterchange.cpp`; it requests shared camera, render,
scene, panel, and diagnostic facilities through the versioned C ABI. No Vulkan, ImGui, C++ standard-library record,
exception, or allocator ownership crosses that edge.

## CMake route

`ProjectDrive.cmake` defines only `ProjectDrive.dll`. It does not use a Project-Zero source batch, `DriveExecution.cpp`,
or a project `main`. The top-level CMake registration defines the same `ProjectDrive` image directly.
