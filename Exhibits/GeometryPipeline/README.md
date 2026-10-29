# Geometry pipeline validation

Headless proof for the Phase 4 portable geometry core (`Frontier/Engine/Rendering/ClusterNesting.*`
and `VisibilityCull.*` — plan §7 row 3, §8.1 visibility group, decision D5).

No demo corpus is read: the suite generates an icosphere (1,280 triangles) and a heightfield
(1,152 triangles) procedurally and proves, exactly:

- builds refuse empty meshes and dangling indices; two builds of one mesh produce identical
  structure hashes (determinism);
- every leaf sphere encloses its vertices, every parent encloses its children, and the
  sphere-tree geometric error is monotone non-decreasing towards the root;
- a screen-space-error cut partitions the mesh **exactly once** at five budgets and refines
  monotonically as the budget shrinks or the camera approaches (root-only at huge budget,
  all-leaves at zero);
- frustum sphere tests never reject a cluster that holds an inside vertex (no false negatives);
- the HiZ pyramid propagates maximum depth exactly, and occlusion fires only for geometry
  wholly behind the recorded occluder (front and straddling spheres stay visible).

Latest result: **57/57 checks passed** (GCC, `-std=c++17 -Wall -Wextra -Werror`).

The error metric is the classic sphere-tree form (child error + radial slack per level) — a
conservative stand-in the GPU/DAG phase replaces with measured simplification deviation, over
this same node/cut shape. Nothing here claims GPU execution.
