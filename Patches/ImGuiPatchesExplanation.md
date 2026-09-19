<!--========================================================================================================================================-->
<!-- 📦 Frontier/Patches/ImGuiPatchesExplanation.md — Comprehensive Explanation of ImGui Docking & Rendering Patches                       -->
<!--========================================================================================================================================-->

# Dear ImGui Slate Engine Docking & Viewport Patches

This document explains the technical rationale, architectural necessity, and internal mechanics of the unified patch applied to Dear ImGui (`docking` branch) for the Frontier / Slate engine workspace architecture.

---

## Patch 1: Trapezoidal / Angled Tab Geometry & Slant Hit-Testing

### 1. Problem Statement
In standard Dear ImGui (docking branch), `ImGuiTabBar` and `TabItemEx` render tabs strictly as axis-aligned bounding boxes (AABBs) with rounded upper corners via `ImDrawList::AddRectFilled()`. 

In professional CAD, DCC, and simulation tooling (such as Slate), top-level master workspaces (e.g. *Modeling*, *Shading*, *Simulation*, *Diagnostics*) require distinct angled trapezoidal tabs with positive and negative slope offsets. Without this geometry:
1. Tab hierarchies blur together visually, making it difficult to distinguish top-level active workspaces from docked sub-panel tabs.
2. Rectangular hitboxes overlap visually when slanted graphics are attempted via custom shaders without modifying ImGui's internal item layout and mouse hit-testing.

### 2. Patch Mechanics
* **Slant Geometry Calculation**:
  For a tab with height $H$ and slope angle $\theta$ (default $\approx 18^\circ$, slant factor $\sigma = H \cdot \tan\theta$), the four vertices of the trapezoid polygon are computed as:
  $$P_0 = (X_{\text{min}} - \sigma, Y_{\text{max}})$$
  $$P_1 = (X_{\text{min}} + \sigma, Y_{\text{min}})$$
  $$P_2 = (X_{\text{max}} - \sigma, Y_{\text{min}})$$
  $$P_3 = (X_{\text{max}} + \sigma, Y_{\text{max}})$$
* **Convex Polygon Rendering**:
  Replaces `AddRectFilled` with `AddConvexPolyFilled(4 points)` and `AddPolyline` for outer border bevels.
* **Active Tab Fusion**:
  When a trapezoidal tab is active, the bottom border line is clipped to seamlessly fuse the tab into the docked child window body below without seam artifacts.
* **Trapezoidal Point-in-Polygon Hit Testing**:
  Modifies `ItemHoverable()` inside `TabItemEx` with a 2D cross-product check so mouse hovering accurately follows the angled left and right edges.

---

## Patch 2: Focus Gating & Input Isolation (Preventing Viewport Input Leakage)

### 1. Problem Statement
In high-performance 3D engines with interactive camera controllers (such as Unreal Engine-style WASD + RMB flight locomotion in `FlyThroughSolver`), user interactions with docked UI panels (e.g. dragging sliders, typing numerical values, scrolling lists) frequently leak input events:
1. Pressing `W`, `A`, `S`, `D` or `Shift` while editing text or dragging sliders moves the 3D scene camera.
2. Clicking and dragging inside a docked panel propagates mouse deltas to the underlying 3D viewport if the mouse cursor momentarily crosses window borders.
3. Hovering over a floating or docked panel fails to reliably claim exclusive mouse/keyboard ownership.

### 2. Patch Mechanics
* **Strict Focus Gating in `UpdateHoveredWindowAndCapture`**:
  When any docked window or tool panel is active/hovered, `io.WantCaptureMouse` and `io.WantCaptureKeyboard` are strongly asserted, and input propagation to the underlying central dockspace viewport is explicitly inhibited.
* **`ImGuiWindowFlags_ExclusiveInput`**:
  Adds an exclusive input window flag allowing modal inspectors, color pickers, and transform property drawers to trap keyboard navigation completely until focus is released.
* **Explicit Viewport Focus Masking**:
  Integrates with engine-side viewport handlers so that 3D camera navigation only executes when the specific 3D viewport image item is explicitly hovered and right-clicked (`ImGuiMouseButton_Right`).

---

## Patch 3: Alpha Compositing & Non-Premultiplied Swapchain Blend Fix

### 1. Problem Statement
When rendering 3D scenes (such as an infinite GPU ground grid, Cornell Box geometry, or particle systems) alongside an ImGui docking hierarchy, transparency artifacts occur:
1. **GPU Grid Bleed-Through**: If docked panels use default semi-transparent background colors or if the Vulkan pipeline blend state incorrectly handles alpha, the 3D grid and scene geometry show through solid editor panels, outliners, and text inputs.
2. **Double Alpha Blending**: ImGui calculates vertex colors with non-premultiplied alpha. If the Vulkan render pass blend equation expects premultiplied alpha or has `srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE`, transparent elements become overly bright or washed out against dark swapchain clear colors.

### 2. Patch Mechanics
* **Vulkan Render Pass Attachment Blend State**:
  Enforces standard non-premultiplied alpha blending in `imgui_impl_vulkan.cpp`:
  ```c
  VkPipelineColorBlendAttachmentState blend_state = {};
  blend_state.blendEnable         = VK_TRUE;
  blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_state.colorBlendOp        = VK_BLEND_OP_ADD;
  blend_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blend_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_state.alphaBlendOp        = VK_BLEND_OP_ADD;
  ```
* **Opaque Docked Window Clear**:
  Sets `ImGuiCol_WindowBg` to full opacity `(0.12f, 0.12f, 0.14f, 1.0f)` across all docked tool panels while leaving the central dockspace node with `ImGuiDockNodeFlags_PassthruCentralNode` and clear alpha `0.0f` for pristine offscreen 3D viewport presentation.
