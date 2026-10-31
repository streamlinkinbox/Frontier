# Project Everest

Native Vulkan C++ port of the VehicleHMI proving-grounds application on Frontier's reusable editor foundation.

## Entry flow

| Step | Component | Behaviour |
|---|---|---|
| 1. Browse | `ProjectBrowserPanel` | Opens the Frontier Project Browser, enumerates `Frontier/Projects/*/*.frontier`, and lists **Project Everest**. |
| 2. Select | Reader & Hand-off | Selecting **Project Everest** previews its descriptor/README and activates native hand-off via **Open in Frontier**. |
| 3. Workspace | `ControlCentrePanel` + `PanelDockspace` + `LogStreamPanel` | Opens the docked Project Everest workspace with the top trapezoidal Control Centre theme controller (`OLED` / `Dark` / `Light`) and live `LogStreamPanel`. |

## Dual-tier GPU rendering contract (Phase 3+)

- **RTX tier** (`VK_KHR_acceleration_structure` + `VK_KHR_ray_query`): hardware BVH traversal, ReSTIR DI/GI reservoir
  reuse, solid/thin-pane dielectric glass transport, and motion SVGF.
- **GTX / lower-end GPU fallback tier**: Vulkan `.slang` 4-cascade toroidal 3D SDF (`SceneClip0..3`) + `SurfaceCards`
  multi-bounce GI + screen-space depth pyramid paired with 2-level SAH triangle **BVH reflections**
  (`OrderedSoftwareBvh`).
