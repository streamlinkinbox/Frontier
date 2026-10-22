# FacetClusterValidation — K1 geometry-build gate

Judges `Engine/Geometry/FacetClusterBuild` with no device, no surface and no shaders: the build under test is pure C++20
over local-space facet soup. This is the Division 1 gate that also runs on the Linux sandbox, where the Windows
delivery build cannot.

## What it checks

| Check | Meaning |
|---|---|
| Reference weld | sphere 5120 facets → 2562 welded vertices; heightfield 18432 → 9409, both exactly as the HTML builder welds them |
| Reference partition | sphere → 48 clusters, heightfield → 159, the reference's level-0 counts |
| Seam and crease ingestion | the seam cylinder exposes UV seams and marked sharp edges, so the classification path is exercised, not bypassed |
| Caps | no cluster exceeds the 128-facet or 256-welded-vertex cap |
| Invariants | coverage exactly once, ascending members, connectivity, class purity, sphere/box containment, usable cone |

Cluster-by-cluster parity against the reference (membership, sphere, cone, enclosure) is a separate step — see
`Exhibits/FacetCluster/README.md`.

## Run

```bash
# Linux / any host with g++ or clang++
bash Executables/Validation/FacetClusterValidation/Build.sh
bash Executables/Validation/FacetClusterValidation/Build.sh --dump /tmp/native-level0.json
```

```powershell
# Windows, Developer PowerShell for VS
Executables\Validation\FacetClusterValidation\Build.bat
```

Exit code 0 = every check passed, 1 = a check failed, 2 = compilation failed.

## CMake wiring (add on merge into the engine line)

The engine line builds validations through its existing static-library targets. Add beside them:

```cmake
add_library(FrontierGeometryBuild STATIC Engine/Geometry/FacetClusterBuild.cpp)
target_include_directories(FrontierGeometryBuild PUBLIC "${CMAKE_SOURCE_DIR}")

add_executable(FacetClusterValidation Executables/Validation/FacetClusterValidation/FacetClusterValidation.cpp)
target_link_libraries(FacetClusterValidation PRIVATE FrontierGeometryBuild)
if(MSVC)
    target_compile_options(FacetClusterValidation PRIVATE /utf-8 /W4)
else()
    target_compile_options(FacetClusterValidation PRIVATE -Wall -Wextra)
endif()
add_test(NAME FacetClusterValidation COMMAND FacetClusterValidation)
```

`FrontierGeometryBuild` carries no Vulkan, GLFW, ImGui or toml++ dependency, which is deliberate: the geometry division
must stay buildable on a machine with no GPU SDK, exactly like the standalone fluid and gas hosts that
`CMakeLists.txt` already isolates behind their own options.

## Corpora

| Corpus | Source | Why |
|---|---|---|
| `sphere` | icosphere, 4 subdivisions, unit radius | Closed manifold; the reference's canonical smooth solid |
| `heightfield` | 96 × 96 quads, extent 40, one 7-unit cliff | Open border, dense welding, the reference's terrain case |
| `seam-cylinder` | 48 segments × 20 rings, meridian UV seam, marked sharp rims | UV seam and crease ingestion, and the sharp-mask propagation path |

The first two are byte-identical ports of `Tools/GeometryHierarchyCorpus.mjs`. The cylinder is this validation's own —
the reference vessel is larger and its cap layout buys nothing K1 has not already proven with it.
