# Project-Tractrix

Project-Tractrix is an independent Frontier project. It is opened by `Frontier.exe` through
`ProjectTractrix.frontier`; it is not copied from Project-Zero and has no windowed executable.

## Files that establish independence

- `ProjectTractrix.frontier` declares its content root, opening scene, optional code image, interchange number, and
  interface fingerprint.
- `Source/ProjectTractrixInterchange.cpp` supplies the project C ABI construction entry.
- `Build/ProjectTractrix.cmake` defines only `ProjectTractrix.dll`.
- `Build/CreateProjectSpecification.py` creates the same standalone starting shape for a new project without copying
  an existing source tree.

Run the project through:

```text
Frontier.exe Projects/Project-Tractrix/ProjectTractrix.frontier
```

Shared window, Vulkan, editor, camera, celestial, renderer, and Surfel-GI facilities remain Frontier ownership.
Project-specific vehicle and course semantics remain Project-Tractrix ownership behind the versioned C ABI.
