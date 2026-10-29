# Render infrastructure validation

Headless proof for the scheduling/threading core the renderer will stand on
(plan §2/§7, pulled forward by owner direction after the Log Stream):

- `Engine/Rendering/SubmissionOrder` — declared unit reads/writes (+ explicit
  `After`) → RAW/WAW/WAR adjacency → deterministic topological order.
  Declaration order is program order (RDG convention); reads of not-yet-written
  targets are reported as imported history. Cycles are refused *by name*.
- `Engine/Rendering/TransientTargetAtlas` — submission-position lifetimes →
  aliased memory bins (strictly disjoint spans only; persistent history
  targets such as ReSTIR reservoirs are never aliased).
- `Engine/Threading/SnapshotExchange` — lock-free triple-buffer publish/take
  (game division → render division snapshots; torn reads impossible by
  construction, verified by checksum under 200k publishes).
- `Engine/Threading/WorkerPool` — fixed thread set, exact completion
  accounting, drain-on-destroy.

Every resolution and refusal is also written to the Log Stream's Render
channel; the proof reads them back through a `LogStreamPanel`, so the Phase 1
diagnostics pipe is exercised end-to-end by the infrastructure it will serve.

## Build & run

```sh
cmake -S . -B Build && cmake --build Build && ctest --test-dir Build --output-on-failure
```

Or directly: `g++ -std=c++17 -Wall -Wextra -Werror -I ../..` over the engine
.cpp files listed in `CMakeLists.txt` plus the validation main, `-pthread`.

## Phase 3 additions (2026-10-22)

- `Engine/Rendering/RenderTarget.h` — usage/format vocabulary; units now declare
  ordered `TargetTouch`es (usage class drives ordering edges, lifetimes, barriers).
- `Engine/Rendering/BarrierDerivation` — per-target usage walks → layout
  transitions vs execution dependencies; resting-usage contract (transient born
  Undefined, persistent declare their rest state). The dump is written to the
  Render log channel; `BarrierDump-Viewport1920x1080.log` is a retained run.
- `Engine/Rendering/ViewportUnits` — the first frame as data (SurfaceRaster →
  DisplayPresent) with `Shaders/{SurfaceRaster,DisplayPresent}.slang` as the
  unit bodies for the SDK build (not compiled here — no slangc in the sandbox).
- `Engine/Foundation/Math.h` + `Projects/Project-ProvingGrounds` — Vulkan-convention
  matrices and the spinning-cube test scene (scenes live in projects, not the
  engine), streamed game→render through `SnapshotExchange` in the proof.

Still blocked on the SDK build (MSVC + Vulkan SDK + upstream `Engine/Interface`):
recording into the host's `VkCommandBuffer` (`VulkanPresentation::
ConfigureRendering` + `VulkanRasterAccess`, contracts read at `9ec758d` on the
moved `Frontier` branch), i.e. the spinning cube *on screen*. Nothing at this
layer fakes that step.

## Capture fix (2026-10-22)

`ProvingGroundsCapture` dropped any triangle with a corner behind the camera,
which deleted the ground plane's front triangle (its corners pass behind the
close camera) and left the cube's tip hanging over the void. Triangles are now
clipped in homogeneous clip space (Sutherland–Hodgman against the six Vulkan
clip planes) before the divide; `Math.h` gained the matching `Vec4` arithmetic.
Frames + GIF in `Capture/` regenerated; validation re-proved 63/63.
