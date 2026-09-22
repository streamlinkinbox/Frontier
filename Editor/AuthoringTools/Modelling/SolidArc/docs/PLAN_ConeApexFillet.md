# Stage 4b — bounded coaxial conical apex fillet

Implement one explicit apex route: a closed right circular cone receives a spherical cap at its
apex. The user supplies the sphere radius; the cone frustum is trimmed at the analytic tangent
circle, and the spherical cap continues to the axis pole. A planar base disk closes the result.

For base radius `R`, height `H`, slant `L = sqrt(H² + R²)`, and requested spherical radius `q`,
this route requires `0 < q < H R / L`. The sphere centre is at
`z_c = H − q L/R`, its tangent circle has radius `q H/L`, and the cap is the exact spherical
arc tangent to the cone generator.

## Acceptance boundary

- explicit `ConeApexFilletSpecification`; no arbitrary B-rep apex selection or trim/heal dispatch;
- one coaxial right circular cone with a positive base radius and height;
- one positive spherical apex radius satisfying the analytic fit bound;
- exact revolution surfaces for the cone frustum, spherical cap, and planar base disk;
- analytic frustum-plus-spherical-cap volume, G1 seam alignment, closed genus-zero topology,
  outward normals, and refusal transactions;
- one exterior sharp-cone/rounded-apex proof image.

This does not claim general vertex filleting, multi-face apex fillets, partial apex fillets,
cone–cone or mixed-support apex routes, arbitrary support selection, rolling-ball G2 continuity,
or general healing.
