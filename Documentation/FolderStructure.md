# Folder Structure

## Current native UI slice

```text
Frontier/                         # Repository root
├── .gitignore
├── AGENTS.md / CLAUDE.md
├── Automation/                   # Pinned UI dependencies, patches, font tooling
├── Documentation/                # Project tracker and ownership map
├── Engine/                       # Shared UI foundations and content utilities
│   ├── ContentInterchange/
│   ├── Diagnostics/
│   ├── Interface/
│   ├── Lifecycle/
│   └── Render/                   # Image helpers only here; no scene renderer
├── EngineDocs/AgenticInstuctions/ # Four retained authoritative skills
├── EngineContent/                # Ignored generated font archives and shared content
├── Exhibits/
│   ├── LogStream/                # Native panel capture harness and validation
│   └── ProjectEverest/
│       ├── Code/                 # Native CMake, browser-flow capture, lifecycle test
│       ├── Images/               # Native CPU-rasterized ImGui evidence + labelled references
│       └── Results/              # Branch basis and verification result
├── ExternalPackages/             # Ignored pinned ImGui/GLFW/TOML/ThorVG/STB dependencies
├── Frontier/                     # Native application and engine modules
│   ├── Applications/Editor/      # Project browser, native entry point, project hand-off
│   ├── Engine/Diagnostics/
│   ├── Engine/Foundation/
│   ├── Engine/Interface/         # LogStream, Outliner, Inspector, shared panel wiring
│   ├── Engine/Rendering/         # Portable environment models; no GPU renderer
│   ├── Engine/Scene/             # Records and Outliner/Inspector ownership
│   └── Projects/
│       ├── Project-Everest/      # UI-first .frontier descriptor and Content placeholder
│       └── Project-VehicleHmiDemo/Reference/ # HTML source material only
└── UI/Editor/                    # Production Control Centre and editor UI foundations
```

The native UI build and checks are owned by `Exhibits/ProjectEverest/Code/CMakeLists.txt`; `Build/`, `.venv/`, `ExternalPackages/`, and generated font archives are ignored. This is a selective UI integration, not a claim that the inherited engine migration or renderer integration is complete.

Create meaningful folders as their contents are needed; do not add empty placeholder trees. Keep Project Everest evidence with its generating C++ exhibit, and label inherited screenshots as references. No scene renderer is linked or validated by this UI slice.

## Inherited layout — migration review outcome (2026-10-22)

`Internal/`, `Executables/`, `Applications/`, and `memory/` were reclaimed from this branch; the
complete trees remain on `master` and in this branch's history. Their documents, together with
`Documentation/Prototypes/`, `Documentation/Research/`, and the inherited root-level plan and
reference notes, are retained under `Documentation/Plans/References/InheritedEngine/`.
`Automation/` is current tooling (see the target map). Existing diagnostics remain in
`Exhibits/Diagnostics/`. `EngineDocs/` retains existing governance references until separately
reconciled.
The linked source also has `Docs/`, `Editor/`, `Projects/`, `Tools/`, and dependency/content folders;
map their ownership and build dependencies before any import or relocation.

## Selective port boundary

`Exhibits/UIFoundation/` retains verification code, results, and actual SVG raster evidence.
`Automation/Patches/` owns the inherited ImGui patches. Dependency pins live in
`Automation/UIDependencies.json`. No scene renderer is ported or built by the Project Everest target.
No inherited `Internal/` code is linked into this target.

`Exhibits/ControlCentre/` owns current appearance interaction tests, native CPU images, and build evidence.
`Automation/FontSources.json`, `PrepareFonts.py`, and `FontRequirements.txt` define open-font recovery.
Windows packaging and product-binary generation are outside this UI-only slice.

## Shared content and dependencies

```text
EngineContent/
├── Icons/                     # Tracked original/compatibility SVGs
├── MaterialArchives/          # Tracked Texture Studio material library (.material + baked maps)
├── PatternArchives/           # Tracked Texture Studio pattern presets
├── EnvironmentArchives/       # Tracked baked equirect studio environments (+ user imports)
├── Defaults/                  # Tracked blank project space
└── FontArchives/<font name>/  # Ignored font downloads, OFL notices, TOML descriptors
ExternalPackages/
├── glfw/                      # GLFW 3.4 platform windows/input
├── tomlpp/                    # Native TOML formats
├── imgui/                     # Pinned UI + approved patches
└── thorvg/                    # SVG rasterization
```

ContentDirectory resolves bundled content from the executable independently of the launch working directory.
The font catalogue has a native TOML form; JSON remains only an automation/report interchange.
