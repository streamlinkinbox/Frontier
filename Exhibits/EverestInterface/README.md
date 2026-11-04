# Everest Interface & Runtime Validation Exhibit

Validates Phase 1 of the Frontier general-purpose engine editor, C++ project generator, composable simulation properties, and `Projects/Everest/`:

1. **`ProjectGenerator` C++ Auto-Scaffolding**: Proves `ProjectBrowserPanel::CreateProject` auto-generates `Source/<Identifier>Application.{h,cpp}`, `Source/Main.cpp`, `CMakeLists.txt`, and `Content/Levels/Default.levelspace`.
2. **`.levelspace` Containers**: Proves `SceneWorld` loads and saves `.levelspace` files (`Default.levelspace`) and rejects non-`.levelspace` extensions.
3. **Composable Property Configurations & Multi-Inspector `InspectorRuntime`**: Verifies modular `SpatialConfiguration`, `PhysicsConfiguration` (`MassConfiguration` + helpers), `ThermalConfiguration`, `ChemicalConfiguration` (Arrhenius kinetics), and `SurfaceOpticsConfiguration` alongside environment, render pipeline, and continuum solver inspectors.
4. **Generic `DiagramControls` & Standalone `<Domain>Editor`s**: Exercises `DiagramControls` across inspectors and standalone `FractureEditor`, `FluidPlumeEditor`, `ParticleEditor`, and `PolygonClusterEditor`.
5. **Shipping Strip Proof (`EverestShippingValidation`)**: Verifies `FRONTIER_EDITOR == 0` compiles out `SceneDirectory`, `InspectorRuntime`, and Editor UI while stepping physics, thermal, and chemical simulations.
