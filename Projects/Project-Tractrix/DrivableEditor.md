# Project-Tractrix through Frontier.exe

Project-Tractrix uses the same project-opening pattern as every new Frontier project:

```text
Frontier.exe Projects/Project-Tractrix/ProjectTractrix.frontier
```

The shared host owns the window, device, renderer, editor, camera, input, and celestial environment. The project code
image declares only Project-Tractrix behaviour through `ConstructProjectInterchange`; no Vulkan or ImGui record crosses
the edge.

## Creating a project without copying another project

```bash
python3 Projects/Project-Tractrix/Build/CreateProjectSpecification.py Project-Example --root /path/to/Frontier
```

The creator writes a project specification, content/scene folder, build ignore rules, and a minimal code-image source.
It does not copy `GameExecution.cpp`, another project's source folder, a renderer, an editor, or a windowed entry.

Vehicle course, simulation, driver input, camera requests, and project panels remain code-image responsibilities. The
shared engine facilities, including GPU and CPU Surfel GI, remain Frontier ownership.
