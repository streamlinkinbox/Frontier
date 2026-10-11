# Stage 4a — bounded partial-edge constant-radius blend

Implement one deliberately narrow partial-edge route: a standalone orthogonal planar corner
with a square support width, a **constant** positive radius, and a strict interior interval
`0 < Start < End < Length` along one straight parent edge. The blend exists only between those
stations. Two planar sector caps close the newly exposed endpoint transitions; the unblended
portions remain sharp.

## Acceptance boundary

- explicit geometry descriptor only; no arbitrary B-rep edge selection or healing;
- two perpendicular planar supports and a straight axis-aligned frame;
- constant radius only, with `0 < radius < width`;
- strict interior interval, so this is not a renamed complete-edge route;
- split planar support patches plus two three-sided planar transition caps and two full end caps;
- closed, genus-zero, manifold result with deterministic `V34/E65/F33/L33` topology and
  outward-normal/section checks;
- zero/negative radius, invalid interval, non-planar/degenerate frame requests refuse before
  construction and do not mutate any caller-owned body.

This slice does not claim variable-radius partial edges, arbitrary support selection, partial
apex fillets, freeform support handling, or general intersection/trim/sew healing.
