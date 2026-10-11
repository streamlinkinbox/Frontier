# Project-Drive

Project-Drive is opened by `Frontier.exe`, never by a project-owned windowed executable. Its project specification is
`ProjectDrive.frontier` and its optional code image is `Source/ProjectDriveInterchange.cpp`.

## Ownership

| Capability | Owner |
|---|---|
| Window, Vulkan device, renderer, editor, global camera, input, celestial environment | `Frontier.exe` |
| Distance Field GI (per-object fields, built once) | shared `Engine/` facilities |
| Drive course, vehicle simulation, driver interpretation, chase-camera requests, vehicle panels | `ProjectDrive.dll` |

The code image reaches host facilities through the versioned `CodeInterchange` C ABI. C++ standard-library records,
exceptions, allocator ownership, Vulkan references, and ImGui records do not cross it.

## Project content

`Source/DriveCourse.h` describes the proving ground shared by the vehicle and its content authoring. The project vehicle
sources keep course and ControlVehicle semantics project-owned. (Surfel reference fixture removed)


## Build and opening

Use the authoritative host route:

```powershell
powershell -File Tools\Build\ToolchainSequence.ps1
```

This compiles shared source once into `Frontier.exe`, then links `ProjectDrive.dll` independently. Open the project with:

```text
Frontier.exe Projects/Project-Drive/ProjectDrive.frontier
```

Do not add `DriveExecution.cpp`, a second project `main`, a window owner, or an include into Project-Zero source.
