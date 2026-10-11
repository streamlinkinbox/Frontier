# Stage 4c — bounded quadratic-radius partial-edge blend

Extend the explicit Stage 4a partial-edge descriptor by one distinct law: a genuinely nonlinear
quadratic radius sampled at the selected interval's start, middle, and end. The parent edge is
still one straight orthogonal planar corner, the blend interval is still strict interior, and
sharp-to-rounded sector caps remain explicit.

## Acceptance boundary

- `QuadraticPartialEdgeFilletSpecification` only; no arbitrary input B-rep edge selection;
- positive quadratic law with a distinct middle value and sampled positivity/width feasibility;
- quadratic lofts through three exact quarter-circle station sections only over the selected
  interval; the outside edge portions remain sharp;
- analytic volume using the integrated quadratic radius square;
- deterministic capped topology, refusal transactions, outward support/transition normals, and
  one comparison proof against the sharp parent;
- no claim of rolling-ball G2, arbitrary variable-radius support handling, variable-radius apexes,
  freeform supports, or general healing.
