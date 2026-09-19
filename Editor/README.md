# Editor

Everything that is *authoring* rather than *runtime* lives here. The runtime is
`Engine/`; a shipped game never links anything from `Editor/`.

```
Editor/
└── EditorTools/                 one folder per creation tool
    ├── ParametricSketcher/      SolidArc — NURBS / B-rep modelling, sketch constraints, dimensions
    ├── TextureEditor/           3D painting and baking            (planned)
    └── …                        further tools each get their own folder
```

Rules
- Each tool is self-contained: its own `CMakeLists.txt`, `README.md`, `Proofs/`, `Verification/`, `Scripts/`.
- A tool may depend on `Engine/`; it never depends on a sibling tool or on a `Projects/` entry.
- Every tool builds standalone (`Console` host or window) *and* can be embedded in the Editor shell as a panel.
