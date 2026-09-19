# Phase 18: 2D constraint solver

## Goal

A user can build a sketch (a set of curves on the workplane) and attach
geometric constraints to it: distance, angle, coincident, horizontal,
vertical, parallel, perpendicular, equal-length, equal-radius. The solver
keeps the sketch fully determined. Editing a dim that's now a constraint
re-solves the whole sketch — the user sees their rectangle/circle/triangle
snap into the new shape.

This is the missing piece for "real CAD": the user can dimension and
constrain, and the model is always fully determined.

## Scope (confirmed with user)

- 9 constraint types: distance, angle, coincident, horizontal, vertical,
  parallel, perpendicular, equal-length, equal-radius.
- Dim-driven re-solve: when the user `dim edit`s a dimension that's
  marked as a constraint, the solver re-positions the affected figures
  to satisfy the constraint graph.
- 3 demo scripts: rectangle (4 sides + 4 corners), triangle (3 corners),
  two circles (distance between centres).

## Math: dof analysis + Newton solve

Each *unknown* is a 2D point coordinate (x, y) on the workplane. A sketch
with N points has 2N unknowns.

Each *constraint* is a scalar equation f(unknowns) = 0 (or = prescribed
value for distance/angle). The system is over-determined when the number
of constraints > 2N, well-determined when = 2N, under-determined when < 2N.

Dof = 2N - rank(J), where J is the constraint Jacobian (M × 2N for M
constraints). For each constraint type we add ∂f/∂x_i, ∂f/∂y_i to the
Jacobian. The actual unknowns are the (x, y) of every point mentioned by
any constraint, and the rank deficiency gives the remaining dof.

The Newton step: J^T J dx = -J^T r, where r is the residual vector of
constraint values. Iterate until |r| < tolerance or max iterations.

## Unknowns

Every figure mentioned by a constraint contributes its parametric source
to the unknown list. For Phase 18, we constrain the **2D parametric
source** of the figure — what gets stored in `Blueprint.PolylinePoints`,
`Blueprint.A`, `Blueprint.B`, `Blueprint.Centre` (lifted to 2D on the
workplane), `Blueprint.MajorDirection`, etc. The current values are
written back into the Blueprint, the figure is rebuilt, and the scene
re-emits dims.

The constraint graph sits *on top of* the Blueprint. A `dim edit` on a
constrained dim is treated as a "prescribed value" — the constraint is
re-evaluated with the new value and the Newton step re-solves the other
constraints around it.

## Constraint type → equation + Jacobian

| Type | Equation | Jacobian w.r.t. unknowns |
|---|---|---|
| Distance (p1, p2) = d | sqrt((x2-x1)^2 + (y2-y1)^2) - d = 0 | d/dx1 = -(x2-x1)/d, d/dy1 = -(y2-y1)/d, d/dx2 = +(x2-x1)/d, d/dy2 = +(y2-y1)/d |
| Angle (l1, l2) = θ° | atan2(cross, dot) - θ = 0 | d/dx/y of each line's two endpoints (4 unknowns) — full derivation in the code |
| Coincident (p1, p2) | x1-x2 = 0, y1-y2 = 0 | two scalar rows: ±1 on x, ±1 on y |
| Horizontal (p1, p2) | y2 - y1 = 0 | -1 on y1, +1 on y2 |
| Vertical (p1, p2) | x2 - x1 = 0 | -1 on x1, +1 on x2 |
| Parallel (l1, l2) | cross(dir1, dir2) = 0 | d/d endpoints, similar to angle |
| Perpendicular (l1, l2) | dot(dir1, dir2) = 0 | similar |
| EqualLength (l1, l2) | len(l1) - len(l2) = 0 | similar to distance |
| EqualRadius (c1, c2) | r1 - r2 = 0 | trivial, just r1 - r2 |

We treat all 9 constraints uniformly — each is a row in the residual
vector, with a known analytic gradient. No automatic differentiation.

## File layout

- `Editor/EditorTools/ParametricSketcher/Kernel/ConstraintSolver.h` — the solver API.
- `Editor/EditorTools/ParametricSketcher/Kernel/ConstraintSolver.cpp` — Newton + dof.
- `Editor/EditorTools/ParametricSketcher/Console/ConstraintGrammar.cpp` (or in
  ConsoleHost.cpp) — verb dispatch for `constraint ...`.
- `Editor/EditorTools/ParametricSketcher/Verification/ConstraintVerification.cpp` —
  the verification binary.
- `Editor/EditorTools/ParametricSketcher/Scripts/Phase18_Constraints.scr` — demo script
  with the 3 demos.
- `Editor/EditorTools/ParametricSketcher/CMakeLists.txt` — register the new verification.
- `Editor/EditorTools/ParametricSketcher/Verification/SuiteVerification.cpp` — register
  the new script.

## Verb surface

```
constraint distance P1 P2 d          # prescribe the distance between two named points
constraint angle L1 L2 deg           # prescribe the angle between two lines
constraint coincident P1 P2          # snap two points together
constraint horizontal P1 P2          # lock y1 = y2
constraint vertical P1 P2            # lock x1 = x2
constraint parallel L1 L2            # lock direction(L1) = direction(L2)
constraint perpendicular L1 L2       # lock dot(dir1, dir2) = 0
constraint equal L1 L2               # lock length(L1) = length(L2)
constraint equal-radius C1 C2        # lock radius(C1) = radius(C2)
constraint list                      # show the active constraints
constraint delete <id>               # remove a constraint
constraint dof                       # report dof + over/under-determined status
constraint solve                     # run Newton to satisfy all constraints
```

For 2D points, named as `FigureName:pK` (vertex K of polyline) or
`FigureName:endA` / `FigureName:endB` (line endpoints) or `FigureName:centre`
(circle/arc centre). Lines: `FigureName:start` / `FigureName:end`.

## Demo scripts (3 in 1)

1. **Rectangle**: 4 lines forming a 2×3 rectangle. 4 distance constraints
   (one per side, the prescribed lengths), 4 angle-90 constraints at
   the corners, 1 coincident at the closing corner. 8 unknown points ×
   2 = 16 dof. 4 distance + 4 angle-90 = 8 constraints + 2 coincident = 10.
   Actually we need to count: 4 distance + 4 angle + 1 coincident = 9 constraints,
   8 unknown points × 2 = 16, 1 dof (a free rotation). Add an angle-90
   constraint between side 1 and the X-axis → fully determined (dof = 0).

2. **Triangle**: 3 lines forming a 3-4-5 triangle. 3 distance constraints
   on the 3 sides, 1 coincident at one end, 1 angle-90 at one corner.
   6 unknown points × 2 = 12, 5 constraints → 7 dof. To get a closed
   triangle, we need 2 more coincidences → 3 dof. Add 3 more angle
   constraints that the angles sum to 180° (or just trust the user to
   drag a side).

3. **Two circles**: 2 circles, distance between centres = 5. Edit that
   distance to 3.0 and the circles snap together.

For the demos to demonstrate, we'll add a `constraint solve` + `dim edit`
+ verify the dim value is reached.

## What I will NOT do in Phase 18

- **No 3D constraints** (constraints between faces, edges of bodies). All
  constraints are 2D, on the active workplane. Moving a 2D constraint
  to a 3D face would be a Phase 19+ feature.
- **No driver expressions** (Phase 18c: `length = width * 2`). Scalar
  expressions are nice but require a separate expression parser.
- **No re-emit of auto-dims on every solve** — we re-emit the dim
  that was edited, not all 5+ dims on the figure.
- **No incremental Newton / sparse matrix** — for the demo-scale
  problems (< 50 unknowns) the dense 2N×2N matrix is fine.

## Risks

- **Newton convergence**: if the initial sketch is degenerate (e.g. all
  points coincident), the Jacobian is singular. We detect that and refuse
  with a clear message.
- **Convergence rate**: with bad initial guesses Newton can diverge. We
  add line search (backtrack on residual).
- **Over-constrained**: if the user adds 5 distance constraints to a
  rectangle side, the system is over-determined. We report this on
  `constraint dof` (dof = -2 etc.) and refuse to solve.

## File list (what we touch)

- `Editor/EditorTools/ParametricSketcher/Kernel/ConstraintSolver.h` — NEW
- `Editor/EditorTools/ParametricSketcher/Kernel/ConstraintSolver.cpp` — NEW
- `Editor/EditorTools/ParametricSketcher/Console/ConsoleHost.cpp` — add `constraint` verb
  + integrate dim-edit re-solve path
- `Editor/EditorTools/ParametricSketcher/Verification/ConstraintVerification.cpp` — NEW
- `Editor/EditorTools/ParametricSketcher/CMakeLists.txt` — register
- `Editor/EditorTools/ParametricSketcher/Scripts/Phase18_Constraints.scr` — NEW
- `Editor/EditorTools/ParametricSketcher/Verification/SuiteVerification.cpp` — register script
- `Editor/EditorTools/ParametricSketcher/README.md` — add Phase 18 row
- `Editor/EditorTools/ParametricSketcher/Proofs/Phase18_*.png` — 3 demo PNGs

## Acceptance criteria

- 14 → 15 verification suites, 797 → ~830 checks, all 0 failed.
- 3 new proof PNGs (rectangle after dim edit, triangle, two-circles).
- README row describing the 9 constraint types + 3 demo scripts.
