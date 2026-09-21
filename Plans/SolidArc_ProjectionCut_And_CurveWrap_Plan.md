# SolidArc feature plan — projected cutting curves and curve-wrapped sketches/solids

## User-requested features

### 1. Screen-projected line/curve cutting

A line or curve drawn in the viewport should be usable as a cutting tool. The intended interaction is:

- draw or select a 2D line/curve in a view;
- project it through the selected object along the view direction;
- intersect the resulting ruled/projected tool with the target B-rep;
- split the target wherever the projected curve crosses its boundary;
- publish two or more valid bodies when the cut actually partitions the target.

A projected line through a cylinder should therefore produce two cylindrical pieces; a projected curve may produce more
than two pieces when its intersections form multiple closed regions or disconnected cut paths.

#### Proposed bounded implementation stages

1. **View-space tool definition**
   - Store the source sketch/curve, camera/view frame, projection direction, depth policy, and target selection.
   - Support orthographic projection first; make perspective projection explicit rather than silently treating it as parallel.
   - Refuse a missing target, a curve with no finite projection, back-facing-only selection, or an ambiguous depth policy.
2. **Projection surface and intersection**
   - Build the projected ruled sheet or cutter volume from exact NURBS curves, not screen pixels.
   - Intersect against analytic planes/cylinders/cones and then the general SSI path.
   - Preserve intersection curves with parameter-space traces on every affected face.
3. **Split and sew**
   - Split all crossed edges/faces, classify the resulting regions, and sew each closed component independently.
   - Require closed, manifold, consistently oriented positive-volume outputs before publishing any result.
   - If the projected curve only touches, is tangent to, lies on a seam, or fails to partition the body, refuse transactionally.
4. **Multiple-result transaction**
   - Return a result set with stable names and source immutability until every component validates.
   - Preserve the original object on refusal; never return a partial split.

#### Planned verification

- orthographic line through a cylinder → exactly two valid bodies;
- a polyline with two crossings → expected multiple components;
- curved projected cutter through a box and cylinder;
- tangent, coincident, seam, open, self-intersecting, back-facing, and non-partitioning refusals;
- source immutability, volume conservation within declared SSI tolerance, and rendered before/after proofs.

### 2. Curve-wrapped sketches and 3D objects

A planar sketch should be able to follow a guide curve, analogous to wrapping a decal/profile around a path. The
operation needs to distinguish two useful results:

- **surface wrap:** map a 2D sketch onto a target surface using a surface parameterization or geodesic/normal rule;
- **solid flow/sweep:** carry a closed sketch or a complete 3D object along a guide curve, using a moving frame, optional
  scale/twist, and an explicit solid/void policy.

For a 3D object, the feasible bounded interpretation is a curve-driven deformation or sweep in which a chosen object
frame is transported along the guide. It is possible, but it is not the same operation as projecting a flat sketch: the
frame law, section correspondence, self-intersection behavior, and end constraints must be specified.

#### Proposed bounded implementation stages

1. **Planar sketch along a path**
   - Accept a closed planar sketch and a non-self-intersecting guide curve.
   - Use rotation-minimizing frames by default, with explicit Frenet/fixed-frame alternatives.
   - Support translation, optional uniform scale, and optional twist laws; preserve exact NURBS sections.
2. **Surface wrap**
   - Add an explicit target surface and mapping mode: closest-normal, UV, or geodesic where validated.
   - Refuse poles, seams, folds, orientation flips, and mappings that produce self-intersecting loops.
3. **3D object flow**
   - Require a source frame and guide parameter correspondence.
   - Carry topology and trimmed supports only when every transformed face/edge remains valid; otherwise fall back to a
     section-based sweep or refuse rather than deforming topology approximately.
   - Detect collisions, inverted sections, zero scale, curvature-frame singularities, and non-manifold results.
4. **History and editing**
   - Store the source sketch/object, guide, frame law, scale/twist laws, and mapping mode as editable feature data.
   - Keep the source immutable and make updates transactional.

#### Planned verification

- rectangle/hexagon sketch swept along straight, circular, and spatial guide curves;
- rotation-minimizing versus Frenet frame behavior at a 3D bend;
- exact section correspondence, optional twist/scale, and volume checks for solids;
- surface wrap around a cylinder with seam and pole refusals;
- self-intersection, frame singularity, collision, inverted-section, and non-manifold refusals;
- visible source/guide/result proofs and console/document round trips.

## Relationship to Phase 5 / Phase 33

Variable-radius and G2 blends are a supporting capability rather than a substitute for these two tools. The current bounded
Phase 5 increment should first validate explicit radius laws and curvature/continuity acceptance on exact ruled surfaces.
Later work can reuse those laws for variable-width projected cutters, tapered wraps, and G2 path transitions, but no
projected split or arbitrary 3D flow should be claimed complete until its intersection and topology gates exist.

## Safety policy

Every stage must remain transactional: validate the source, classify the support, construct on private data, validate all
outputs, and publish only a complete result. Ambiguous projection depth, tangent/coincident contacts, frame singularities,
self-intersections, non-manifold trims, or unsupported mixed geometry must refuse with the source unchanged.
