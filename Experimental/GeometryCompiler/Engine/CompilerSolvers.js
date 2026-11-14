/*============================================================================================================================================
                                                           COMPILERSOLVERS.JS
============================================================================================================================================*/
// Pure ES-module geometry compiler solvers: 6 specimens (incl. double-walled GlassVessel), OBJ/STL parser, sparse 8x8x8 SDF Mip0+Mip1,
// QEF Dual-Contour extractor, 4-Way Multi-LOD DAG (Original, Retopo+Shrinkwrap, Nanite-Style QEM, Hybrid Prob-QEM+Retopo), & 128px icon baker.

//------------------------------------------------------------------------------------------------------------------------
//                                                    VECTOR MATH HELPERS
//------------------------------------------------------------------------------------------------------------------------

function vSub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
function vAdd(a, b) { return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]; }
function vScale(a, s) { return [a[0] * s, a[1] * s, a[2] * s]; }
function vDot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
function vCross(a, b) {
  return [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
}
function vLen(a) { return Math.hypot(a[0], a[1], a[2]); }
function vNorm(a) {
  const l = vLen(a);
  return l > 1e-7 ? [a[0] / l, a[1] / l, a[2] / l] : [0, 0, 1];
}
function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

//------------------------------------------------------------------------------------------------------------------------
//                                                 SURFACE OPTICAL CATEGORIES
//------------------------------------------------------------------------------------------------------------------------

export const OPTICAL_CATEGORIES = [
  { id: "DielectricGlass", label: "Dielectric Glass", ior: 1.52, tint: [158, 224, 242], lockDualWalls: true },
  { id: "OpaqueSolid",     label: "Opaque Solid",     ior: 1.00, tint: [198, 206, 214], lockDualWalls: false },
  { id: "ThinFoliage",     label: "Thin Foliage",     ior: 1.05, tint: [118, 190, 134], lockDualWalls: true },
  { id: "SubsurfaceWax",   label: "Subsurface Wax",   ior: 1.38, tint: [228, 184, 146], lockDualWalls: false },
];

export const SPECIMEN_CATALOG = [
  { id: "GlassVessel",  title: "Dielectric Glass Vessel",  defaultOptical: "DielectricGlass", subtitle: "Double-walled optical carafe (32mm thin wall)" },
  { id: "UtahTeapot",   title: "Utah Teapot",              defaultOptical: "OpaqueSolid",     subtitle: "Multi-part lathe body, lid, spout & handle" },
  { id: "ChamferBox",   title: "Mechanical Chamfer Box",   defaultOptical: "OpaqueSolid",     subtitle: "Sharp 90° bevels & recessed panel bosses" },
  { id: "ShaderBall",   title: "Studio Shader Ball",       defaultOptical: "OpaqueSolid",     subtitle: "Cutaway outer shell, inner core & pedestal" },
  { id: "TorusKnot",    title: "Braided Torus Knot",       defaultOptical: "DielectricGlass", subtitle: "High-curvature (2,3) helical dielectric sweep" },
  { id: "UvSphere",     title: "Sculpted Geodesic Sphere", defaultOptical: "SubsurfaceWax",   subtitle: "Equatorial groove & meridional ribs" },
];

//------------------------------------------------------------------------------------------------------------------------
//                                            CANONICAL GEOMETRY NORMALIZATION
//------------------------------------------------------------------------------------------------------------------------

export function finalizeGeometryNetwork(rawPositions, rawUvs, rawTriangles, meta = {}) {
  const vertCount = rawPositions.length / 3;
  const positions = new Float32Array(rawPositions);
  const uvs = rawUvs && rawUvs.length === vertCount * 2
    ? new Float32Array(rawUvs)
    : new Float32Array(vertCount * 2);
  const indices = new Uint32Array(rawTriangles);
  const triCount = Math.floor(indices.length / 3);

  // Normalize onto centered studio pedestal (unit span ~1.2m, base resting at Z = 0)
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  for (let i = 0; i < vertCount; ++i) {
    const x = positions[i * 3], y = positions[i * 3 + 1], z = positions[i * 3 + 2];
    if (x < minX) minX = x; if (x > maxX) maxX = x;
    if (y < minY) minY = y; if (y > maxY) maxY = y;
    if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
  }
  const cx = 0.5 * (minX + maxX);
  const cy = 0.5 * (minY + maxY);
  const span = Math.max(1e-4, maxX - minX, maxY - minY, maxZ - minZ);
  const scale = (meta.skipNormalize ? 1.0 : 1.24 / span);
  const zOffset = meta.skipNormalize ? 0.0 : -minZ;

  minX = minY = minZ = Infinity;
  maxX = maxY = maxZ = -Infinity;
  for (let i = 0; i < vertCount; ++i) {
    const x = (positions[i * 3] - (meta.skipNormalize ? 0 : cx)) * scale;
    const y = (positions[i * 3 + 1] - (meta.skipNormalize ? 0 : cy)) * scale;
    const z = (positions[i * 3 + 2] + zOffset) * scale;
    positions[i * 3] = x;
    positions[i * 3 + 1] = y;
    positions[i * 3 + 2] = z;
    if (x < minX) minX = x; if (x > maxX) maxX = x;
    if (y < minY) minY = y; if (y > maxY) maxY = y;
    if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
  }

  // Generate box-projected auto-UVs if missing/degenerate
  if (!rawUvs || rawUvs.length !== vertCount * 2) {
    for (let i = 0; i < vertCount; ++i) {
      const x = positions[i * 3], y = positions[i * 3 + 1], z = positions[i * 3 + 2];
      uvs[i * 2] = (Math.atan2(y, x) / (2 * Math.PI) + 0.5);
      uvs[i * 2 + 1] = clamp((z - minZ) / Math.max(1e-4, maxZ - minZ), 0, 1);
    }
  }

  // Compute smooth vertex normals + per-triangle normals
  const normals = new Float32Array(vertCount * 3);
  const triNormals = new Float32Array(triCount * 3);
  const triCentroids = new Float32Array(triCount * 3);
  const shellSide = meta.shellSide ? new Int8Array(meta.shellSide) : new Int8Array(vertCount); // +1 outer, -1 inner, 0 single

  for (let t = 0; t < triCount; ++t) {
    const i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
    const p0 = [positions[i0 * 3], positions[i0 * 3 + 1], positions[i0 * 3 + 2]];
    const p1 = [positions[i1 * 3], positions[i1 * 3 + 1], positions[i1 * 3 + 2]];
    const p2 = [positions[i2 * 3], positions[i2 * 3 + 1], positions[i2 * 3 + 2]];
    const cross = vCross(vSub(p1, p0), vSub(p2, p0));
    const n = vNorm(cross);
    triNormals[t * 3] = n[0]; triNormals[t * 3 + 1] = n[1]; triNormals[t * 3 + 2] = n[2];
    triCentroids[t * 3] = (p0[0] + p1[0] + p2[0]) / 3;
    triCentroids[t * 3 + 1] = (p0[1] + p1[1] + p2[1]) / 3;
    triCentroids[t * 3 + 2] = (p0[2] + p1[2] + p2[2]) / 3;
    for (const vi of [i0, i1, i2]) {
      normals[vi * 3] += cross[0];
      normals[vi * 3 + 1] += cross[1];
      normals[vi * 3 + 2] += cross[2];
    }
  }
  for (let i = 0; i < vertCount; ++i) {
    const n = vNorm([normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]]);
    normals[i * 3] = n[0]; normals[i * 3 + 1] = n[1]; normals[i * 3 + 2] = n[2];
  }

  // Detect UV seam vertices (vertices whose incident edges have large UV jumps or share identical 3D coords with different UVs)
  const uvSeamMask = new Uint8Array(vertCount);
  const posBucket = new Map();
  for (let i = 0; i < vertCount; ++i) {
    const key = `${Math.round(positions[i * 3] * 250)}_${Math.round(positions[i * 3 + 1] * 250)}_${Math.round(positions[i * 3 + 2] * 250)}`;
    if (!posBucket.has(key)) {
      posBucket.set(key, i);
    } else {
      const other = posBucket.get(key);
      const du = Math.abs(uvs[i * 2] - uvs[other * 2]);
      const dv = Math.abs(uvs[i * 2 + 1] - uvs[other * 2 + 1]);
      if (du > 0.015 || dv > 0.015) {
        uvSeamMask[i] = 1;
        uvSeamMask[other] = 1;
      }
    }
  }
  for (let t = 0; t < triCount; ++t) {
    const a = indices[t * 3], b = indices[t * 3 + 1], c = indices[t * 3 + 2];
    const checkEdge = (u, v) => {
      if (Math.abs(uvs[u * 2] - uvs[v * 2]) > 0.45 || Math.abs(uvs[u * 2 + 1] - uvs[v * 2 + 1]) > 0.45) {
        uvSeamMask[u] = 1; uvSeamMask[v] = 1;
      }
    };
    checkEdge(a, b); checkEdge(b, c); checkEdge(c, a);
  }

  const boundsMin = [minX, minY, minZ];
  const boundsMax = [maxX, maxY, maxZ];
  const boundsCenter = [0.5 * (minX + maxX), 0.5 * (minY + maxY), 0.5 * (minZ + maxZ)];
  const boundsDiag = vLen(vSub(boundsMax, boundsMin));

  return {
    id: meta.id || "CustomAsset",
    title: meta.title || "Imported Geometry",
    opticalCategory: meta.opticalCategory || "OpaqueSolid",
    positions,
    normals,
    uvs,
    indices,
    triNormals,
    triCentroids,
    shellSide,
    uvSeamMask,
    vertexCount: vertCount,
    triangleCount: triCount,
    boundsMin,
    boundsMax,
    boundsCenter,
    boundsDiag,
    wallThicknessMeters: meta.wallThicknessMeters || 0.0,
  };
}

//------------------------------------------------------------------------------------------------------------------------
//                                           6 CANONICAL STUDIO SPECIMENS
//------------------------------------------------------------------------------------------------------------------------

function appendLatheSurface(pos, uvs, tris, shellSide, profile, segs, invertWinding, shellCode, uTile = 1) {
  const baseV = pos.length / 3;
  const rings = profile.length;
  for (let r = 0; r < rings; ++r) {
    const [rad, z] = profile[r];
    const vCoord = r / Math.max(1, rings - 1);
    for (let s = 0; s <= segs; ++s) {
      const u = s / segs;
      const a = u * Math.PI * 2;
      pos.push(Math.cos(a) * rad, Math.sin(a) * rad, z);
      uvs.push(u * uTile, vCoord);
      shellSide.push(shellCode);
    }
  }
  const stride = segs + 1;
  for (let r = 0; r < rings - 1; ++r) {
    for (let s = 0; s < segs; ++s) {
      const i00 = baseV + r * stride + s;
      const i10 = i00 + 1;
      const i01 = i00 + stride;
      const i11 = i01 + 1;
      if (invertWinding) {
        tris.push(i00, i01, i10, i10, i01, i11);
      } else {
        tris.push(i00, i10, i01, i10, i11, i01);
      }
    }
  }
}

function appendTubeSweep(pos, uvs, tris, shellSide, curveFn, uSegs, vSegs, radiusFn, closedU, shellCode) {
  const baseV = pos.length / 3;
  const stride = vSegs + 1;

  // Compute twist-free Bishop (rotation-minimizing parallel transport) frames along the curve
  const pts = [];
  const tangs = [];
  for (let iu = 0; iu <= uSegs; ++iu) {
    const u = iu / uSegs;
    const p = curveFn(u);
    const du = closedU ? 1 / (uSegs * 4) : 0.002;
    const pNext = curveFn(closedU ? (u + du) % 1 : Math.min(1, u + du));
    const pPrev = curveFn(closedU ? (u - du + 1) % 1 : Math.max(0, u - du));
    pts.push(p);
    tangs.push(vNorm(vSub(pNext, pPrev)));
  }

  const sides = new Array(uSegs + 1);
  const binorms = new Array(uSegs + 1);
  const initRef = Math.abs(tangs[0][2]) < 0.85 ? [0, 0, 1] : [0, 1, 0];
  sides[0] = vNorm(vCross(tangs[0], initRef));
  binorms[0] = vNorm(vCross(tangs[0], sides[0]));

  for (let iu = 1; iu <= uSegs; ++iu) {
    // Parallel-transport previous side vector onto the new normal plane orthogonal to tangs[iu]
    const tCurr = tangs[iu];
    const prevSide = sides[iu - 1];
    const dot = vDot(prevSide, tCurr);
    let proj = [
      prevSide[0] - tCurr[0] * dot,
      prevSide[1] - tCurr[1] * dot,
      prevSide[2] - tCurr[2] * dot,
    ];
    if (vLen(proj) < 1e-5) {
      const fb = Math.abs(tCurr[2]) < 0.85 ? [0, 0, 1] : [0, 1, 0];
      proj = vCross(tCurr, fb);
    }
    sides[iu] = vNorm(proj);
    binorms[iu] = vNorm(vCross(tCurr, sides[iu]));
  }

  // If closed curve, distribute any small holonomy twist evenly across all rings so seam closes cleanly
  let twistPerStep = 0;
  if (closedU) {
    const cDot = clamp(vDot(sides[uSegs], sides[0]), -1, 1);
    const sDot = vDot(vCross(sides[uSegs], sides[0]), tangs[0]);
    twistPerStep = Math.atan2(sDot, cDot) / uSegs;
  }

  for (let iu = 0; iu <= uSegs; ++iu) {
    const u = iu / uSegs;
    const p = pts[iu];
    const twist = twistPerStep * iu;
    const ct = Math.cos(twist), st = Math.sin(twist);
    const s0 = sides[iu], b0 = binorms[iu];
    const side = [s0[0] * ct + b0[0] * st, s0[1] * ct + b0[1] * st, s0[2] * ct + b0[2] * st];
    const binorm = [-s0[0] * st + b0[0] * ct, -s0[1] * st + b0[1] * ct, -s0[2] * st + b0[2] * ct];
    const rad = radiusFn(u);
    for (let iv = 0; iv <= vSegs; ++iv) {
      const v = iv / vSegs;
      const phi = v * Math.PI * 2;
      const c = Math.cos(phi) * rad;
      const s = Math.sin(phi) * rad;
      pos.push(
        p[0] + side[0] * c + binorm[0] * s,
        p[1] + side[1] * c + binorm[1] * s,
        p[2] + side[2] * c + binorm[2] * s,
      );
      uvs.push(u, v);
      shellSide.push(shellCode);
    }
  }
  for (let iu = 0; iu < uSegs; ++iu) {
    for (let iv = 0; iv < vSegs; ++iv) {
      const i00 = baseV + iu * stride + iv;
      const i10 = i00 + stride;
      const i01 = i00 + 1;
      const i11 = i10 + 1;
      tris.push(i00, i10, i01, i10, i11, i01);
    }
  }
}

export function constructSpecimen(specimenId = "GlassVessel") {
  const pos = [];
  const uvs = [];
  const tris = [];
  const shellSide = [];

  if (specimenId === "GlassVessel") {
    // Double-walled optical dielectric carafe: outer shell (+1) and inner shell (-1) separated by thin ~0.032m wall
    const segs = 36;
    const outerProfile = [
      [0.004, 0.000], [0.240, 0.000], [0.265, 0.035], [0.255, 0.085],
      [0.375, 0.240], [0.435, 0.420], [0.415, 0.600], [0.310, 0.760],
      [0.195, 0.880], [0.175, 1.000], [0.218, 1.135], [0.242, 1.195],
      [0.232, 1.215],
    ];
    const wall = 0.032;
    const innerProfile = [
      [0.232, 1.215], [0.205, 1.190], [0.184, 1.130], [0.142, 1.000],
      [0.162, 0.880], [0.274, 0.755], [0.378, 0.595], [0.398, 0.420],
      [0.340, 0.248], [0.220, 0.112], [0.190, 0.070], [0.004, 0.062],
    ];
    appendLatheSurface(pos, uvs, tris, shellSide, outerProfile, segs, false, +1);
    appendLatheSurface(pos, uvs, tris, shellSide, innerProfile, segs, false, -1);
    return finalizeGeometryNetwork(pos, uvs, tris, {
      id: "GlassVessel",
      title: "Dielectric Glass Vessel",
      opticalCategory: "DielectricGlass",
      shellSide,
      wallThicknessMeters: wall,
    });
  }

  if (specimenId === "UtahTeapot") {
    const segs = 32;
    const body = [
      [0.005, 0.00], [0.34, 0.00], [0.44, 0.08], [0.52, 0.24],
      [0.54, 0.44], [0.47, 0.64], [0.36, 0.78], [0.30, 0.84],
      [0.33, 0.88], [0.28, 0.90], [0.12, 0.96], [0.04, 1.02],
      [0.09, 1.08], [0.05, 1.12], [0.004, 1.13],
    ];
    appendLatheSurface(pos, uvs, tris, shellSide, body, segs, false, +1);
    // Curved spout (+X)
    appendTubeSweep(
      pos, uvs, tris, shellSide,
      (t) => [0.42 + t * 0.44 + Math.sin(t * Math.PI) * 0.08, 0, 0.26 + t * 0.58 - Math.sin(t * Math.PI) * 0.12],
      16, 12,
      (t) => 0.105 * (1 - 0.52 * t),
      false, +1,
    );
    // Handle (-X)
    appendTubeSweep(
      pos, uvs, tris, shellSide,
      (t) => {
        const ang = -Math.PI * 0.62 + t * Math.PI * 1.24;
        return [-0.46 - Math.cos(ang) * 0.30, 0, 0.49 + Math.sin(ang) * 0.28];
      },
      18, 10,
      () => 0.046,
      false, +1,
    );
    return finalizeGeometryNetwork(pos, uvs, tris, {
      id: "UtahTeapot",
      title: "Utah Teapot",
      opticalCategory: "OpaqueSolid",
      shellSide,
    });
  }

  if (specimenId === "ChamferBox") {
    // Subdivided chamfered cube with crisp 90-deg edges + recessed front/top detail bosses
    const grid = 10;
    const bevel = 0.14;
    const half = 0.50;
    const faceAxes = [
      [[0, 0, 1], [1, 0, 0], [0, 1, 0]],
      [[0, 0, -1], [1, 0, 0], [0, -1, 0]],
      [[1, 0, 0], [0, 1, 0], [0, 0, 1]],
      [[-1, 0, 0], [0, -1, 0], [0, 0, 1]],
      [[0, 1, 0], [-1, 0, 0], [0, 0, 1]],
      [[0, -1, 0], [1, 0, 0], [0, 0, 1]],
    ];
    for (let f = 0; f < 6; ++f) {
      const [nAx, uAx, vAx] = faceAxes[f];
      const baseV = pos.length / 3;
      for (let iy = 0; iy <= grid; ++iy) {
        const ty = iy / grid;
        const vy = (ty * 2 - 1) * half;
        for (let ix = 0; ix <= grid; ++ix) {
          const tx = ix / grid;
          const vx = (tx * 2 - 1) * half;
          let p = [
            nAx[0] * half + uAx[0] * vx + vAx[0] * vy,
            nAx[1] * half + uAx[1] * vx + vAx[1] * vy,
            nAx[2] * half + uAx[2] * vx + vAx[2] * vy,
          ];
          // Round-chamfer corners cleanly
          const cx = clamp(p[0], -(half - bevel), half - bevel);
          const cy = clamp(p[1], -(half - bevel), half - bevel);
          const cz = clamp(p[2], -(half - bevel), half - bevel);
          const d = [p[0] - cx, p[1] - cy, p[2] - cz];
          const dl = vLen(d);
          if (dl > 1e-6) {
            p = [cx + (d[0] / dl) * bevel, cy + (d[1] / dl) * bevel, cz + (d[2] / dl) * bevel];
          }
          // Recessed center panel groove
          const r2 = vx * vx + vy * vy;
          if (r2 < 0.055) {
            const recess = -0.045 * (1 - r2 / 0.055);
            p[0] += nAx[0] * recess;
            p[1] += nAx[1] * recess;
            p[2] += nAx[2] * recess;
          }
          pos.push(p[0], p[1], p[2] + half);
          uvs.push(tx, ty);
          shellSide.push(+1);
        }
      }
      const stride = grid + 1;
      for (let iy = 0; iy < grid; ++iy) {
        for (let ix = 0; ix < grid; ++ix) {
          const i00 = baseV + iy * stride + ix;
          tris.push(i00, i00 + 1, i00 + stride, i00 + 1, i00 + stride + 1, i00 + stride);
        }
      }
    }
    return finalizeGeometryNetwork(pos, uvs, tris, {
      id: "ChamferBox",
      title: "Mechanical Chamfer Box",
      opticalCategory: "OpaqueSolid",
      shellSide,
    });
  }

  if (specimenId === "ShaderBall") {
    const segs = 32;
    const basePedestal = [
      [0.01, 0.00], [0.46, 0.00], [0.48, 0.06], [0.40, 0.14], [0.34, 0.18], [0.01, 0.18],
    ];
    appendLatheSurface(pos, uvs, tris, shellSide, basePedestal, segs, false, +1);
    // Outer slotted shell (double-wall cutaway hemisphere)
    const outerArc = [];
    const innerArc = [];
    for (let i = 0; i <= 14; ++i) {
      const a = -0.78 + (i / 14) * 2.22;
      outerArc.push([Math.cos(a) * 0.50, 0.58 + Math.sin(a) * 0.50]);
    }
    for (let i = 14; i >= 0; --i) {
      const a = -0.78 + (i / 14) * 2.22;
      innerArc.push([Math.cos(a) * 0.43, 0.58 + Math.sin(a) * 0.43]);
    }
    appendLatheSurface(pos, uvs, tris, shellSide, outerArc, segs, false, +1);
    appendLatheSurface(pos, uvs, tris, shellSide, innerArc, segs, false, -1);
    // Inner floating core sphere
    const core = [];
    for (let i = 0; i <= 12; ++i) {
      const a = -Math.PI / 2 + (i / 12) * Math.PI;
      core.push([Math.max(0.004, Math.cos(a) * 0.28), 0.58 + Math.sin(a) * 0.28]);
    }
    appendLatheSurface(pos, uvs, tris, shellSide, core, 24, false, +1);
    return finalizeGeometryNetwork(pos, uvs, tris, {
      id: "ShaderBall",
      title: "Studio Shader Ball",
      opticalCategory: "OpaqueSolid",
      shellSide,
      wallThicknessMeters: 0.07,
    });
  }

  if (specimenId === "TorusKnot") {
    appendTubeSweep(
      pos, uvs, tris, shellSide,
      (u) => {
        const t = u * Math.PI * 2;
        const r = 0.36 + 0.14 * Math.cos(3 * t);
        return [r * Math.cos(2 * t), r * Math.sin(2 * t), 0.42 + 0.17 * Math.sin(3 * t)];
      },
      72, 14,
      () => 0.115,
      true, +1,
    );
    return finalizeGeometryNetwork(pos, uvs, tris, {
      id: "TorusKnot",
      title: "Braided Torus Knot",
      opticalCategory: "DielectricGlass",
      shellSide,
      wallThicknessMeters: 0.045,
    });
  }

  // Default: UvSphere with sculpted equatorial trench & meridional ribs
  const rings = 22, segs = 36;
  const baseV = 0;
  for (let r = 0; r <= rings; ++r) {
    const v = r / rings;
    const phi = -Math.PI / 2 + v * Math.PI;
    const trench = 1 - 0.14 * Math.exp(-Math.pow(phi / 0.18, 2));
    for (let s = 0; s <= segs; ++s) {
      const u = s / segs;
      const theta = u * Math.PI * 2;
      const rib = 1 + 0.045 * Math.cos(theta * 8) * Math.cos(phi);
      const rad = 0.52 * trench * rib;
      pos.push(
        rad * Math.cos(phi) * Math.cos(theta),
        rad * Math.cos(phi) * Math.sin(theta),
        0.55 + rad * Math.sin(phi),
      );
      uvs.push(u, v);
      shellSide.push(+1);
    }
  }
  const stride = segs + 1;
  for (let r = 0; r < rings; ++r) {
    for (let s = 0; s < segs; ++s) {
      const i00 = baseV + r * stride + s;
      tris.push(i00, i00 + 1, i00 + stride, i00 + 1, i00 + stride + 1, i00 + stride);
    }
  }
  return finalizeGeometryNetwork(pos, uvs, tris, {
    id: "UvSphere",
    title: "Sculpted Geodesic Sphere",
    opticalCategory: "SubsurfaceWax",
    shellSide,
  });
}

//------------------------------------------------------------------------------------------------------------------------
//                                        EXTERNAL .OBJ & BINARY/ASCII .STL PARSER
//------------------------------------------------------------------------------------------------------------------------

export function parseObjText(text, title = "Imported OBJ") {
  const rawPos = [];
  const rawUv = [];
  const pos = [];
  const uvs = [];
  const tris = [];
  const vertMap = new Map();

  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || line.startsWith("#")) continue;
    const parts = line.split(/\s+/);
    if (parts[0] === "v" && parts.length >= 4) {
      rawPos.push(parseFloat(parts[1]), parseFloat(parts[2]), parseFloat(parts[3]));
    } else if (parts[0] === "vt" && parts.length >= 3) {
      rawUv.push(parseFloat(parts[1]), parseFloat(parts[2]));
    } else if (parts[0] === "f" && parts.length >= 4) {
      const faceVerts = [];
      for (let i = 1; i < parts.length; ++i) {
        const tok = parts[i];
        if (!vertMap.has(tok)) {
          const [viStr, vtStr] = tok.split("/");
          let vi = parseInt(viStr, 10);
          if (vi < 0) vi = rawPos.length / 3 + vi; else vi -= 1;
          let vt = vtStr ? parseInt(vtStr, 10) : 0;
          if (vt < 0) vt = rawUv.length / 2 + vt; else vt -= 1;
          const idx = pos.length / 3;
          pos.push(rawPos[vi * 3] || 0, rawPos[vi * 3 + 1] || 0, rawPos[vi * 3 + 2] || 0);
          uvs.push(vt >= 0 && vt * 2 + 1 < rawUv.length ? rawUv[vt * 2] : 0,
                   vt >= 0 && vt * 2 + 1 < rawUv.length ? rawUv[vt * 2 + 1] : 0);
          vertMap.set(tok, idx);
        }
        faceVerts.push(vertMap.get(tok));
      }
      for (let k = 1; k + 1 < faceVerts.length; ++k) {
        tris.push(faceVerts[0], faceVerts[k], faceVerts[k + 1]);
      }
    }
  }
  if (tris.length < 3) throw new Error("OBJ contains no triangles");
  return finalizeGeometryNetwork(pos, rawUv.length ? uvs : null, tris, {
    id: `Custom_${title.replace(/[^a-zA-Z0-9]/g, "_")}`,
    title,
    opticalCategory: "OpaqueSolid",
  });
}

export function parseStlBytes(arrayBuffer, title = "Imported STL") {
  const bytes = new Uint8Array(arrayBuffer);
  const pos = [];
  const tris = [];
  const weldMap = new Map();
  const addWeldedVert = (x, y, z) => {
    const key = `${Math.round(x * 1e4)}_${Math.round(y * 1e4)}_${Math.round(z * 1e4)}`;
    if (weldMap.has(key)) return weldMap.get(key);
    const idx = pos.length / 3;
    pos.push(x, y, z);
    weldMap.set(key, idx);
    return idx;
  };

  let isBinary = false;
  if (bytes.byteLength >= 84) {
    const dv = new DataView(arrayBuffer);
    const triCount = dv.getUint32(80, true);
    if (84 + triCount * 50 === bytes.byteLength && triCount > 0) {
      isBinary = true;
      for (let t = 0; t < triCount; ++t) {
        const off = 84 + t * 50 + 12;
        const i0 = addWeldedVert(dv.getFloat32(off, true), dv.getFloat32(off + 4, true), dv.getFloat32(off + 8, true));
        const i1 = addWeldedVert(dv.getFloat32(off + 12, true), dv.getFloat32(off + 16, true), dv.getFloat32(off + 20, true));
        const i2 = addWeldedVert(dv.getFloat32(off + 24, true), dv.getFloat32(off + 28, true), dv.getFloat32(off + 32, true));
        if (i0 !== i1 && i1 !== i2 && i2 !== i0) tris.push(i0, i1, i2);
      }
    }
  }
  if (!isBinary) {
    const text = new TextDecoder().decode(bytes);
    const matches = [...text.matchAll(/vertex\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)/g)];
    for (let i = 0; i + 2 < matches.length; i += 3) {
      const i0 = addWeldedVert(parseFloat(matches[i][1]), parseFloat(matches[i][2]), parseFloat(matches[i][3]));
      const i1 = addWeldedVert(parseFloat(matches[i + 1][1]), parseFloat(matches[i + 1][2]), parseFloat(matches[i + 1][3]));
      const i2 = addWeldedVert(parseFloat(matches[i + 2][1]), parseFloat(matches[i + 2][2]), parseFloat(matches[i + 2][3]));
      if (i0 !== i1 && i1 !== i2 && i2 !== i0) tris.push(i0, i1, i2);
    }
  }
  if (tris.length < 3) throw new Error("STL contains no valid triangles");
  return finalizeGeometryNetwork(pos, null, tris, {
    id: `Custom_${title.replace(/[^a-zA-Z0-9]/g, "_")}`,
    title,
    opticalCategory: "OpaqueSolid",
  });
}

//------------------------------------------------------------------------------------------------------------------------
//                           SPARSE 8x8x8 NARROW-BAND SDF VOLUME (MIP0 + CONSERVATIVE MIP1)
//------------------------------------------------------------------------------------------------------------------------

function pointToTriangleDistSq(px, py, pz, ax, ay, az, bx, by, bz, cx, cy, cz) {
  const abx = bx - ax, aby = by - ay, abz = bz - az;
  const acx = cx - ax, acy = cy - ay, acz = cz - az;
  const apx = px - ax, apy = py - ay, apz = pz - az;
  const d1 = abx * apx + aby * apy + abz * apz;
  const d2 = acx * apx + acy * apy + acz * apz;
  if (d1 <= 0 && d2 <= 0) return apx * apx + apy * apy + apz * apz;

  const bpx = px - bx, bpy = py - by, bpz = pz - bz;
  const d3 = abx * bpx + aby * bpy + abz * bpz;
  const d4 = acx * bpx + acy * bpy + acz * bpz;
  if (d3 >= 0 && d4 <= d3) return bpx * bpx + bpy * bpy + bpz * bpz;

  const vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) {
    const v = d1 / (d1 - d3);
    const dx = px - (ax + v * abx), dy = py - (ay + v * aby), dz = pz - (az + v * abz);
    return dx * dx + dy * dy + dz * dz;
  }

  const cpx = px - cx, cpy = py - cy, cpz = pz - cz;
  const d5 = abx * cpx + aby * cpy + abz * cpz;
  const d6 = acx * cpx + acy * cpy + acz * cpz;
  if (d6 >= 0 && d5 <= d6) return cpx * cpx + cpy * cpy + cpz * cpz;

  const vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) {
    const w = d2 / (d2 - d6);
    const dx = px - (ax + w * acx), dy = py - (ay + w * acy), dz = pz - (az + w * acz);
    return dx * dx + dy * dy + dz * dz;
  }

  const va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
    const w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    const dx = px - (bx + w * (cx - bx)), dy = py - (by + w * (cy - by)), dz = pz - (bz + w * (cz - bz));
    return dx * dx + dy * dy + dz * dz;
  }

  const denom = 1 / Math.max(1e-12, va + vb + vc);
  const v = vb * denom;
  const w = vc * denom;
  const dx = px - (ax + abx * v + acx * w);
  const dy = py - (ay + aby * v + acy * w);
  const dz = pz - (az + abz * v + acz * w);
  return dx * dx + dy * dy + dz * dz;
}

export function compileSparseSdfVolume(geometry, options = {}) {
  const dim = clamp(Math.round((options.gridDim || 24) / 8) * 8, 16, 128);
  const padFrac = clamp(options.paddingFraction ?? 0.16, 0.04, 0.5);
  const brickEdge = 8;

  const spanX = geometry.boundsMax[0] - geometry.boundsMin[0];
  const spanY = geometry.boundsMax[1] - geometry.boundsMin[1];
  const spanZ = geometry.boundsMax[2] - geometry.boundsMin[2];
  const maxSpan = Math.max(0.1, spanX, spanY, spanZ);
  const cubicSpan = maxSpan * (1 + 2 * padFrac);
  const boundsMin = [
    geometry.boundsCenter[0] - cubicSpan * 0.5,
    geometry.boundsCenter[1] - cubicSpan * 0.5,
    geometry.boundsCenter[2] - cubicSpan * 0.5,
  ];
  const boundsMax = [
    geometry.boundsCenter[0] + cubicSpan * 0.5,
    geometry.boundsCenter[1] + cubicSpan * 0.5,
    geometry.boundsCenter[2] + cubicSpan * 0.5,
  ];
  const voxelStep = cubicSpan / dim;
  const brickSpan = voxelStep * brickEdge;
  const invBrickSpan = 1 / brickSpan;
  // Scale narrow band proportionally with resolution so 64^3-128^3 stay ultra-sparse (~82-92% brick pruning!)
  const narrowBandMeters = Math.max(
    2.75 * voxelStep,
    options.narrowBandMeters || Math.min(0.18 * maxSpan, (dim >= 64 ? 3.2 : 5.0) * voxelStep),
  );

  const { positions, indices, triNormals, triangleCount } = geometry;
  const stepTri = Math.max(1, Math.floor(triangleCount / 4096));
  const triCount = Math.ceil(triangleCount / stepTri);

  // Flat typed triangle cache for zero-GC inner loops at 64^3 - 128^3
  const tAx = new Float32Array(triCount), tAy = new Float32Array(triCount), tAz = new Float32Array(triCount);
  const tBx = new Float32Array(triCount), tBy = new Float32Array(triCount), tBz = new Float32Array(triCount);
  const tCx = new Float32Array(triCount), tCy = new Float32Array(triCount), tCz = new Float32Array(triCount);
  const tNx = new Float32Array(triCount), tNy = new Float32Array(triCount), tNz = new Float32Array(triCount);
  const tMx = new Float32Array(triCount), tMy = new Float32Array(triCount), tMz = new Float32Array(triCount);
  const tRad = new Float32Array(triCount);

  const bricksPerAxis = dim / brickEdge;
  const totalBricks = bricksPerAxis * bricksPerAxis * bricksPerAxis;
  const brickTris = Array.from({ length: totalBricks }, () => []);

  // 2D (Y, Z) scanline acceleration grid for exact O(1)-amortized +X ray-parity interior sign
  const rayGridDim = Math.min(dim, 64);
  const invRaySpan = rayGridDim / cubicSpan;
  const rayBins = Array.from({ length: rayGridDim * rayGridDim }, () => []);

  let ti = 0;
  for (let t = 0; t < triangleCount; t += stepTri, ++ti) {
    const i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
    const ax = positions[i0 * 3], ay = positions[i0 * 3 + 1], az = positions[i0 * 3 + 2];
    const bx = positions[i1 * 3], by = positions[i1 * 3 + 1], bz = positions[i1 * 3 + 2];
    const cx = positions[i2 * 3], cy = positions[i2 * 3 + 1], cz = positions[i2 * 3 + 2];
    const mx = (ax + bx + cx) / 3, my = (ay + by + cy) / 3, mz = (az + bz + cz) / 3;
    tAx[ti] = ax; tAy[ti] = ay; tAz[ti] = az;
    tBx[ti] = bx; tBy[ti] = by; tBz[ti] = bz;
    tCx[ti] = cx; tCy[ti] = cy; tCz[ti] = cz;
    tNx[ti] = triNormals[t * 3]; tNy[ti] = triNormals[t * 3 + 1]; tNz[ti] = triNormals[t * 3 + 2];
    tMx[ti] = mx; tMy[ti] = my; tMz[ti] = mz;
    tRad[ti] = Math.max(
      Math.hypot(ax - mx, ay - my, az - mz),
      Math.hypot(bx - mx, by - my, bz - mz),
      Math.hypot(cx - mx, cy - my, cz - mz),
    );

    const minX = Math.min(ax, bx, cx), maxX = Math.max(ax, bx, cx);
    const minY = Math.min(ay, by, cy), maxY = Math.max(ay, by, cy);
    const minZ = Math.min(az, bz, cz), maxZ = Math.max(az, bz, cz);

    // Splat triangle AABB expanded by narrowBandMeters into 8x8x8 brick table (exact narrow-band discovery!)
    const pad = narrowBandMeters + 1e-4;
    const bx0 = clamp(Math.floor((minX - pad - boundsMin[0]) * invBrickSpan), 0, bricksPerAxis - 1);
    const bx1 = clamp(Math.floor((maxX + pad - boundsMin[0]) * invBrickSpan), 0, bricksPerAxis - 1);
    const by0 = clamp(Math.floor((minY - pad - boundsMin[1]) * invBrickSpan), 0, bricksPerAxis - 1);
    const by1 = clamp(Math.floor((maxY + pad - boundsMin[1]) * invBrickSpan), 0, bricksPerAxis - 1);
    const bz0 = clamp(Math.floor((minZ - pad - boundsMin[2]) * invBrickSpan), 0, bricksPerAxis - 1);
    const bz1 = clamp(Math.floor((maxZ + pad - boundsMin[2]) * invBrickSpan), 0, bricksPerAxis - 1);
    for (let bz = bz0; bz <= bz1; ++bz) {
      const zOff = bz * bricksPerAxis;
      for (let by = by0; by <= by1; ++by) {
        const yzOff = (zOff + by) * bricksPerAxis;
        for (let bx = bx0; bx <= bx1; ++bx) {
          brickTris[yzOff + bx].push(ti);
        }
      }
    }

    // Splat triangle into 2D (Y, Z) ray-parity scanline bins
    const ry0 = clamp(Math.floor((minY - 1e-5 - boundsMin[1]) * invRaySpan), 0, rayGridDim - 1);
    const ry1 = clamp(Math.floor((maxY + 1e-5 - boundsMin[1]) * invRaySpan), 0, rayGridDim - 1);
    const rz0 = clamp(Math.floor((minZ - 1e-5 - boundsMin[2]) * invRaySpan), 0, rayGridDim - 1);
    const rz1 = clamp(Math.floor((maxZ + 1e-5 - boundsMin[2]) * invRaySpan), 0, rayGridDim - 1);
    for (let rz = rz0; rz <= rz1; ++rz) {
      const row = rz * rayGridDim;
      for (let ry = ry0; ry <= ry1; ++ry) {
        rayBins[row + ry].push(ti);
      }
    }
  }

  // Collect all +X ray intersection X-coordinates for a given (py, pz) scanline ONCE per 8-voxel row!
  const rowHits = new Float32Array(256);
  const collectRowRayHits = (py, pz) => {
    const qy = py + 1.37e-6;
    const qz = pz + 2.11e-6;
    const ry = clamp(Math.floor((qy - boundsMin[1]) * invRaySpan), 0, rayGridDim - 1);
    const rz = clamp(Math.floor((qz - boundsMin[2]) * invRaySpan), 0, rayGridDim - 1);
    const bin = rayBins[rz * rayGridDim + ry];
    let hitCount = 0;
    for (let k = 0; k < bin.length; ++k) {
      const i = bin[k];
      const ay = tAy[i], az = tAz[i];
      const e1y = tBy[i] - ay, e1z = tBz[i] - az;
      const e2y = tCy[i] - ay, e2z = tCz[i] - az;
      const det = e1z * e2y - e1y * e2z;
      if (det > 1e-9 || det < -1e-9) {
        const inv = 1 / det;
        const ty = qy - ay, tz = qz - az;
        const u = (tz * e2y - ty * e2z) * inv;
        if (u >= 0 && u <= 1) {
          const v = (ty * e1z - tz * e1y) * inv;
          if (v >= 0 && u + v <= 1) {
            if (hitCount < 256) {
              rowHits[hitCount++] = tAx[i] + u * (tBx[i] - tAx[i]) + v * (tCx[i] - tAx[i]);
            }
          }
        }
      }
    }
    return hitCount;
  };
  const evalSignRayParity = (px, py, pz) => {
    const hitCount = collectRowRayHits(py, pz);
    let crossings = 0;
    for (let h = 0; h < hitCount; ++h) {
      if (rowHits[h] > px) crossings++;
    }
    return (crossings & 1) ? -1 : 1;
  };

  // Exact unsigned distance from point to local brick triangle subset (zero Math.hypot in hot loop!)
  const evalLocalUnsigned = (px, py, pz, localTris, initMaxSq) => {
    let bestSq = initMaxSq;
    let bestDist = Math.sqrt(initMaxSq);
    let bestDot = 1;
    for (let k = 0; k < localTris.length; ++k) {
      const i = localTris[k];
      const dx = px - tMx[i], dy = py - tMy[i], dz = pz - tMz[i];
      const dCenSq = dx * dx + dy * dy + dz * dz;
      const maxReach = bestDist + tRad[i];
      if (dCenSq < maxReach * maxReach) {
        const dSq = pointToTriangleDistSq(
          px, py, pz,
          tAx[i], tAy[i], tAz[i],
          tBx[i], tBy[i], tBz[i],
          tCx[i], tCy[i], tCz[i],
        );
        if (dSq < bestSq) {
          bestSq = dSq;
          bestDist = Math.sqrt(dSq);
          bestDot = dx * tNx[i] + dy * tNy[i] + dz * tNz[i];
        }
      }
    }
    return { dist: bestDist, bestDot };
  };

  // 1. Evaluate coarse brick-center distances + 2-pass chamfer sweep for far-field empty bricks
  const coarseUnsigned = new Float32Array(totalBricks).fill(cubicSpan * 2);
  const coarseSign = new Int8Array(totalBricks);
  for (let bz = 0; bz < bricksPerAxis; ++bz) {
    const wz = boundsMin[2] + (bz * brickEdge + 4.5) * voxelStep;
    for (let by = 0; by < bricksPerAxis; ++by) {
      const wy = boundsMin[1] + (by * brickEdge + 4.5) * voxelStep;
      for (let bx = 0; bx < bricksPerAxis; ++bx) {
        const wx = boundsMin[0] + (bx * brickEdge + 4.5) * voxelStep;
        const macroIdx = (bz * bricksPerAxis + by) * bricksPerAxis + bx;
        const list = brickTris[macroIdx];
        if (list.length > 0) {
          const { dist, bestDot } = evalLocalUnsigned(wx, wy, wz, list, (cubicSpan * 2) ** 2);
          coarseUnsigned[macroIdx] = dist;
          coarseSign[macroIdx] = (stepTri === 1) ? evalSignRayParity(wx, wy, wz) : (bestDot < 0 ? -1 : 1);
        } else {
          coarseSign[macroIdx] = (stepTri === 1) ? evalSignRayParity(wx, wy, wz) : 1;
        }
      }
    }
  }
  // Fast 2-pass 6-neighbor + diagonal chamfer distance sweep on <= 16^3 coarse brick centers (< 1ms)
  const d1 = brickSpan, d2 = brickSpan * 1.41421356, d3 = brickSpan * 1.7320508;
  const sweepStep = (bx, by, bz) => {
    const macroIdx = (bz * bricksPerAxis + by) * bricksPerAxis + bx;
    let cur = coarseUnsigned[macroIdx];
    for (let dz = -1; dz <= 1; ++dz) {
      const nz = bz + dz;
      if (nz < 0 || nz >= bricksPerAxis) continue;
      for (let dy = -1; dy <= 1; ++dy) {
        const ny = by + dy;
        if (ny < 0 || ny >= bricksPerAxis) continue;
        for (let dx = -1; dx <= 1; ++dx) {
          const nx = bx + dx;
          if (nx < 0 || nx >= bricksPerAxis || (dx === 0 && dy === 0 && dz === 0)) continue;
          const manh = Math.abs(dx) + Math.abs(dy) + Math.abs(dz);
          const stepCost = manh === 1 ? d1 : (manh === 2 ? d2 : d3);
          const cand = coarseUnsigned[(nz * bricksPerAxis + ny) * bricksPerAxis + nx] + stepCost;
          if (cand < cur) cur = cand;
        }
      }
    }
    coarseUnsigned[macroIdx] = cur;
  };
  for (let pass = 0; pass < 2; ++pass) {
    for (let bz = 0; bz < bricksPerAxis; ++bz)
      for (let by = 0; by < bricksPerAxis; ++by)
        for (let bx = 0; bx < bricksPerAxis; ++bx) sweepStep(bx, by, bz);
    for (let bz = bricksPerAxis - 1; bz >= 0; --bz)
      for (let by = bricksPerAxis - 1; by >= 0; --by)
        for (let bx = bricksPerAxis - 1; bx >= 0; --bx) sweepStep(bx, by, bz);
  }
  const coarseBrickCenter = new Float32Array(totalBricks);
  for (let i = 0; i < totalBricks; ++i) {
    coarseBrickCenter[i] = coarseSign[i] * Math.max(coarseUnsigned[i], brickTris[i].length === 0 ? narrowBandMeters : 0);
  }

  // 2. Allocate sparse 8x8x8 narrow-band bricks (Mip0) — ONLY evaluating bricks touched by narrowBandMeters!
  const brickIndirection = new Int32Array(totalBricks).fill(-1);
  const allocatedBricks = [];
  const keepDense = dim <= 40;
  const denseDistances = keepDense ? new Float32Array(dim * dim * dim) : null;
  if (keepDense) {
    for (let bz = 0; bz < bricksPerAxis; ++bz) {
      for (let by = 0; by < bricksPerAxis; ++by) {
        for (let bx = 0; bx < bricksPerAxis; ++bx) {
          const macroIdx = (bz * bricksPerAxis + by) * bricksPerAxis + bx;
          const fillVal = coarseBrickCenter[macroIdx];
          const x0 = bx * brickEdge, y0 = by * brickEdge, z0 = bz * brickEdge;
          for (let lz = 0; lz < brickEdge; ++lz)
            for (let ly = 0; ly < brickEdge; ++ly)
              for (let lx = 0; lx < brickEdge; ++lx)
                denseDistances[((z0 + lz) * dim + (y0 + ly)) * dim + (x0 + lx)] = fillVal;
        }
      }
    }
  }

  const maxSearchSq = (narrowBandMeters * 1.15) ** 2;
  const tempVals = new Float32Array(512);
  for (let bz = 0; bz < bricksPerAxis; ++bz) {
    for (let by = 0; by < bricksPerAxis; ++by) {
      for (let bx = 0; bx < bricksPerAxis; ++bx) {
        const macroIdx = (bz * bricksPerAxis + by) * bricksPerAxis + bx;
        const localTris = brickTris[macroIdx];
        if (localTris.length === 0) continue;

        const x0 = bx * brickEdge, y0 = by * brickEdge, z0 = bz * brickEdge;
        let touches = false;
        let li = 0;
        for (let lz = 0; lz < brickEdge; ++lz) {
          const wz = boundsMin[2] + (z0 + lz + 0.5) * voxelStep;
          for (let ly = 0; ly < brickEdge; ++ly) {
            const wy = boundsMin[1] + (y0 + ly + 0.5) * voxelStep;
            const hitCount = (stepTri === 1) ? collectRowRayHits(wy, wz) : 0;
            for (let lx = 0; lx < brickEdge; ++lx, ++li) {
              const wx = boundsMin[0] + (x0 + lx + 0.5) * voxelStep;
              const { dist, bestDot } = evalLocalUnsigned(wx, wy, wz, localTris, maxSearchSq);
              let sign;
              if (stepTri === 1) {
                let crossings = 0;
                for (let h = 0; h < hitCount; ++h) {
                  if (rowHits[h] > wx) crossings++;
                }
                sign = (crossings & 1) ? -1 : 1;
              } else {
                sign = bestDot < 0 ? -1 : 1;
              }
              const sd = sign * Math.min(dist, narrowBandMeters);
              tempVals[li] = sd;
              if (dist <= narrowBandMeters) touches = true;
              if (keepDense) denseDistances[((z0 + lz) * dim + (y0 + ly)) * dim + (x0 + lx)] = sd;
            }
          }
        }
        if (!touches) continue;

        const quantized = new Int16Array(512);
        for (let i = 0; i < 512; ++i) {
          quantized[i] = Math.round(clamp(tempVals[i] / narrowBandMeters, -1, 1) * 32767);
        }
        brickIndirection[macroIdx] = allocatedBricks.length;
        allocatedBricks.push(quantized);
      }
    }
  }

  // 3. Build single conservative Mip1 (2x2x2 min-pooled in narrow band + exact 3D Euclidean Distance
  //    Transform in pruned empty bricks so sphere-tracing NEVER oversteps thin walls anywhere in 3D!)
  const mip1Dim = dim >> 1;
  const mip1Count = mip1Dim * mip1Dim * mip1Dim;
  const mip1Step = voxelStep * 2;
  const conservativeMip1 = new Float32Array(mip1Count);
  const mip1Allocated = new Uint8Array(mip1Count);
  const edtBufA = new Float32Array(mip1Count);
  const edtBufB = new Float32Array(mip1Count);
  const EDT_INF = 1e12;
  let monotonicityVerified = true;
  const inv32767Band = narrowBandMeters / 32767;
  const seedThreshold = mip1Step * 0.95;

  for (let mz = 0; mz < mip1Dim; ++mz) {
    const vz0 = mz << 1;
    const bz = vz0 >> 3;
    const bzOff = bz * bricksPerAxis;
    for (let my = 0; my < mip1Dim; ++my) {
      const vy0 = my << 1;
      const by = vy0 >> 3;
      const byzOff = (bzOff + by) * bricksPerAxis;
      const mRow = (mz * mip1Dim + my) * mip1Dim;
      for (let mx = 0; mx < mip1Dim; ++mx) {
        const vx0 = mx << 1;
        const macroIdx = byzOff + (vx0 >> 3);
        const slot = brickIndirection[macroIdx];
        const mIdx = mRow + mx;
        if (slot < 0) {
          edtBufA[mIdx] = EDT_INF;
          continue;
        }
        mip1Allocated[mIdx] = 1;
        const brick = allocatedBricks[slot];
        const lx0 = vx0 & 7, ly0 = vy0 & 7, lz0 = vz0 & 7;
        let minVal = Infinity;
        for (let dz = 0; dz < 2; ++dz) {
          const zBase = ((lz0 + dz) * 8 + ly0) * 8 + lx0;
          const a = brick[zBase];
          const b = brick[zBase + 1];
          const c = brick[zBase + 8];
          const d = brick[zBase + 9];
          const m = Math.min(a, b, c, d) * inv32767Band;
          if (m < minVal) minVal = m;
        }
        conservativeMip1[mIdx] = minVal;
        edtBufA[mIdx] = minVal <= seedThreshold ? 0 : EDT_INF;
      }
    }
  }

  // Exact O(M^3) 3-axis Felzenszwalb-Huttenlocher 1D squared-Euclidean lower envelope (< 2ms at 64^3)
  const edtV = new Int32Array(mip1Dim);
  const edtZ = new Float32Array(mip1Dim + 1);
  const edtLineIn = new Float32Array(mip1Dim);
  const edtLineOut = new Float32Array(mip1Dim);
  const edt1D = (n) => {
    let k = 0;
    edtV[0] = 0;
    edtZ[0] = -1e20;
    edtZ[1] = 1e20;
    for (let q = 1; q < n; ++q) {
      const fq = edtLineIn[q];
      if (fq >= EDT_INF * 0.5) continue;
      let s;
      while (true) {
        const vk = edtV[k];
        const fvk = edtLineIn[vk];
        if (fvk >= EDT_INF * 0.5) {
          if (k === 0) { edtV[0] = q; edtZ[0] = -1e20; edtZ[1] = 1e20; break; }
          k--;
          continue;
        }
        s = ((fq + q * q) - (fvk + vk * vk)) / (2 * (q - vk));
        if (s <= edtZ[k]) {
          k--;
          if (k < 0) { k = 0; edtV[0] = q; edtZ[0] = -1e20; edtZ[1] = 1e20; break; }
        } else {
          k++;
          edtV[k] = q;
          edtZ[k] = s;
          edtZ[k + 1] = 1e20;
          break;
        }
      }
    }
    if (edtLineIn[edtV[0]] >= EDT_INF * 0.5) {
      edtLineOut.fill(EDT_INF);
      return;
    }
    let idxK = 0;
    for (let q = 0; q < n; ++q) {
      while (edtZ[idxK + 1] < q) idxK++;
      const d = q - edtV[idxK];
      edtLineOut[q] = d * d + edtLineIn[edtV[idxK]];
    }
  };
  // Pass X: edtBufA -> edtBufB
  for (let mz = 0; mz < mip1Dim; ++mz) {
    for (let my = 0; my < mip1Dim; ++my) {
      const row = (mz * mip1Dim + my) * mip1Dim;
      for (let mx = 0; mx < mip1Dim; ++mx) edtLineIn[mx] = edtBufA[row + mx];
      edt1D(mip1Dim);
      for (let mx = 0; mx < mip1Dim; ++mx) edtBufB[row + mx] = edtLineOut[mx];
    }
  }
  // Pass Y: edtBufB -> edtBufA
  for (let mz = 0; mz < mip1Dim; ++mz) {
    const zOff = mz * mip1Dim * mip1Dim;
    for (let mx = 0; mx < mip1Dim; ++mx) {
      for (let my = 0; my < mip1Dim; ++my) edtLineIn[my] = edtBufB[zOff + my * mip1Dim + mx];
      edt1D(mip1Dim);
      for (let my = 0; my < mip1Dim; ++my) edtBufA[zOff + my * mip1Dim + mx] = edtLineOut[my];
    }
  }
  // Pass Z: edtBufA -> edtBufB
  const strideZ = mip1Dim * mip1Dim;
  for (let my = 0; my < mip1Dim; ++my) {
    for (let mx = 0; mx < mip1Dim; ++mx) {
      const col = my * mip1Dim + mx;
      for (let mz = 0; mz < mip1Dim; ++mz) edtLineIn[mz] = edtBufA[mz * strideZ + col];
      edt1D(mip1Dim);
      for (let mz = 0; mz < mip1Dim; ++mz) edtBufB[mz * strideZ + col] = edtLineOut[mz];
    }
  }

  // Populate pruned empty Mip1 cells with strictly conservative Euclidean lower bound
  // (subtracting 1.15 * mip1Step accounts for seed cell radius + trilinear sampling margin)
  const safetyMargin = mip1Step * 1.15;
  const minEmptyDist = voxelStep * 1.5;
  for (let mz = 0; mz < mip1Dim; ++mz) {
    const bz = (mz << 1) >> 3;
    const bzOff = bz * bricksPerAxis;
    for (let my = 0; my < mip1Dim; ++my) {
      const by = (my << 1) >> 3;
      const byzOff = (bzOff + by) * bricksPerAxis;
      const mRow = (mz * mip1Dim + my) * mip1Dim;
      for (let mx = 0; mx < mip1Dim; ++mx) {
        const mIdx = mRow + mx;
        if (mip1Allocated[mIdx]) continue;
        const macroIdx = byzOff + ((mx << 1) >> 3);
        const rawDist = edtBufB[mIdx] < EDT_INF * 0.5 ? Math.sqrt(edtBufB[mIdx]) * mip1Step : Math.abs(coarseBrickCenter[macroIdx]);
        const safeDist = Math.max(minEmptyDist, rawDist - safetyMargin);
        conservativeMip1[mIdx] = (coarseSign[macroIdx] < 0 ? -1 : 1) * safeDist;
      }
    }
  }

  // Verify conservative invariant against resident Mip0 samples
  for (let mz = 0; mz < mip1Dim && monotonicityVerified; ++mz) {
    const vz0 = mz << 1;
    for (let my = 0; my < mip1Dim && monotonicityVerified; ++my) {
      const vy0 = my << 1;
      for (let mx = 0; mx < mip1Dim; ++mx) {
        const vx0 = mx << 1;
        const macroIdx = ((vz0 >> 3) * bricksPerAxis + (vy0 >> 3)) * bricksPerAxis + (vx0 >> 3);
        const slot = brickIndirection[macroIdx];
        if (slot >= 0) {
          const m1 = conservativeMip1[(mz * mip1Dim + my) * mip1Dim + mx];
          const li = ((vz0 & 7) * 8 + (vy0 & 7)) * 8 + (vx0 & 7);
          const m0 = allocatedBricks[slot][li] * inv32767Band;
          if (m1 > m0 + 1e-4) { monotonicityVerified = false; break; }
        }
      }
    }
  }

  const mip0Bytes = allocatedBricks.length * 1024;
  const mip1Bytes = conservativeMip1.byteLength;
  const denseBytes = dim * dim * dim * 4;
  const prunedPercent = Math.round(((totalBricks - allocatedBricks.length) / Math.max(1, totalBricks)) * 100);

  return {
    dim,
    mip1Dim,
    bricksPerAxis,
    totalBricks,
    allocatedBrickCount: allocatedBricks.length,
    prunedPercent,
    boundsMin,
    boundsMax,
    voxelStep,
    narrowBandMeters,
    denseDistances,
    brickIndirection,
    coarseBrickCenter,
    allocatedBricks,
    conservativeMip1,
    mip0Evicted: false,
    mip0Bytes,
    mip1Bytes,
    denseBytes,
    monotonicityVerified,
  };
}

export function sampleSparseSdf(volume, wx, wy, wz, forceMip1 = false) {
  const { dim, mip1Dim, boundsMin, boundsMax, voxelStep, narrowBandMeters, bricksPerAxis } = volume;
  const cx = clamp(wx, boundsMin[0], boundsMax[0]);
  const cy = clamp(wy, boundsMin[1], boundsMax[1]);
  const cz = clamp(wz, boundsMin[2], boundsMax[2]);
  const ext = Math.hypot(wx - cx, wy - cy, wz - cz);

  if (forceMip1 || volume.mip0Evicted) {
    const step = voxelStep * 2;
    const fx = clamp((cx - boundsMin[0]) / step - 0.5, 0, mip1Dim - 1);
    const fy = clamp((cy - boundsMin[1]) / step - 0.5, 0, mip1Dim - 1);
    const fz = clamp((cz - boundsMin[2]) / step - 0.5, 0, mip1Dim - 1);
    const x0 = Math.floor(fx), y0 = Math.floor(fy), z0 = Math.floor(fz);
    const x1 = Math.min(x0 + 1, mip1Dim - 1), y1 = Math.min(y0 + 1, mip1Dim - 1), z1 = Math.min(z0 + 1, mip1Dim - 1);
    const tx = fx - x0, ty = fy - y0, tz = fz - z0;
    const at = (x, y, z) => volume.conservativeMip1[(z * mip1Dim + y) * mip1Dim + x];
    const c00 = at(x0, y0, z0) * (1 - tx) + at(x1, y0, z0) * tx;
    const c10 = at(x0, y1, z0) * (1 - tx) + at(x1, y1, z0) * tx;
    const c01 = at(x0, y0, z1) * (1 - tx) + at(x1, y0, z1) * tx;
    const c11 = at(x0, y1, z1) * (1 - tx) + at(x1, y1, z1) * tx;
    const val = (c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz;
    return ext > 0 ? Math.max(0, val) + ext : val;
  }

  const fx = clamp((cx - boundsMin[0]) / voxelStep - 0.5, 0, dim - 1);
  const fy = clamp((cy - boundsMin[1]) / voxelStep - 0.5, 0, dim - 1);
  const fz = clamp((cz - boundsMin[2]) / voxelStep - 0.5, 0, dim - 1);
  const x0 = Math.floor(fx), y0 = Math.floor(fy), z0 = Math.floor(fz);
  const x1 = Math.min(x0 + 1, dim - 1), y1 = Math.min(y0 + 1, dim - 1), z1 = Math.min(z0 + 1, dim - 1);
  const tx = fx - x0, ty = fy - y0, tz = fz - z0;
  const at = (vx, vy, vz) => {
    const macroIdx = ((vz >> 3) * bricksPerAxis + (vy >> 3)) * bricksPerAxis + (vx >> 3);
    const slot = volume.brickIndirection[macroIdx];
    if (slot < 0) return volume.coarseBrickCenter[macroIdx];
    const li = ((vz & 7) * 8 + (vy & 7)) * 8 + (vx & 7);
    return (volume.allocatedBricks[slot][li] / 32767) * narrowBandMeters;
  };
  const c00 = at(x0, y0, z0) * (1 - tx) + at(x1, y0, z0) * tx;
  const c10 = at(x0, y1, z0) * (1 - tx) + at(x1, y1, z0) * tx;
  const c01 = at(x0, y0, z1) * (1 - tx) + at(x1, y0, z1) * tx;
  const c11 = at(x0, y1, z1) * (1 - tx) + at(x1, y1, z1) * tx;
  const val = (c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz;
  return ext > 0 ? Math.max(0, val) + ext : val;
}

export function evictMip0ForBudget(volume, maxResidentBytes) {
  const totalBytes = volume.mip0Bytes + volume.mip1Bytes;
  if (totalBytes <= maxResidentBytes || volume.mip0Evicted) {
    return { evicted: false, freedBytes: 0 };
  }
  const freedBytes = volume.mip0Bytes;
  volume.allocatedBricks = [];
  volume.brickIndirection.fill(-1);
  volume.mip0Bytes = 0;
  volume.mip0Evicted = true;
  return { evicted: true, freedBytes };
}

export function extractSdfSliceRgba(volume, axis = 0, depthNormalized = 0.5, sizePx = 256, useMip1 = false) {
  const rgba = new Uint8Array(sizePx * sizePx * 4);
  const { boundsMin, boundsMax, narrowBandMeters } = volume;
  const spanX = boundsMax[0] - boundsMin[0];
  const spanY = boundsMax[1] - boundsMin[1];
  const spanZ = boundsMax[2] - boundsMin[2];
  const dNorm = clamp(depthNormalized, 0.02, 0.98);

  for (let py = 0; py < sizePx; ++py) {
    const v = 1 - (py + 0.5) / sizePx;
    for (let px = 0; px < sizePx; ++px) {
      const u = (px + 0.5) / sizePx;
      let wx, wy, wz;
      if (axis === 0) { // Z slice
        wx = boundsMin[0] + u * spanX;
        wy = boundsMin[1] + v * spanY;
        wz = boundsMin[2] + dNorm * spanZ;
      } else if (axis === 1) { // Y slice
        wx = boundsMin[0] + u * spanX;
        wy = boundsMin[1] + dNorm * spanY;
        wz = boundsMin[2] + v * spanZ;
      } else { // X slice
        wx = boundsMin[0] + dNorm * spanX;
        wy = boundsMin[1] + u * spanY;
        wz = boundsMin[2] + v * spanZ;
      }
      const d = sampleSparseSdf(volume, wx, wy, wz, useMip1);
      const norm = clamp(d / Math.max(1e-4, narrowBandMeters * 1.4), -1, 1);
      const band = Math.abs(Math.sin(d * 52.0));
      const contour = band < 0.09 ? 1.0 : 0.0;
      const zeroRing = Math.abs(d) < volume.voxelStep * 0.42 ? 1.0 : 0.0;

      let r, g, b;
      if (zeroRing > 0.5) {
        r = 255; g = 180; b = 84; // Project Zero amber iso-zero contour
      } else if (d < 0) {
        const t = clamp(-norm, 0, 1);
        r = 32 + t * 56;
        g = 112 + t * 92;
        b = 148 + t * 96;
      } else {
        const t = clamp(norm, 0, 1);
        r = 20 + (1 - t) * 44;
        g = 24 + (1 - t) * 48;
        b = 30 + (1 - t) * 56;
      }
      if (contour > 0.5 && zeroRing < 0.5) {
        r = clamp(r + 22, 0, 255);
        g = clamp(g + 24, 0, 255);
        b = clamp(b + 26, 0, 255);
      }
      const off = (py * sizePx + px) * 4;
      rgba[off] = Math.round(r);
      rgba[off + 1] = Math.round(g);
      rgba[off + 2] = Math.round(b);
      rgba[off + 3] = 255;
    }
  }
  return rgba;
}

//------------------------------------------------------------------------------------------------------------------------
//                                QEF DUAL-CONTOUR SDF-TO-SURFACE EXTRACTOR
//------------------------------------------------------------------------------------------------------------------------

function solve3x3Symmetric(ata, atb, fallback) {
  // Ridge-regularized 3x3 Gaussian elimination solving (A^T A + lambda I) x = A^T b + lambda * fallback
  const lam = 0.035;
  const m = [
    [ata[0] + lam, ata[1],       ata[2],       atb[0] + lam * fallback[0]],
    [ata[1],       ata[3] + lam, ata[4],       atb[1] + lam * fallback[1]],
    [ata[2],       ata[4],       ata[5] + lam, atb[2] + lam * fallback[2]],
  ];
  for (let col = 0; col < 3; ++col) {
    let pivot = col;
    for (let r = col + 1; r < 3; ++r) {
      if (Math.abs(m[r][col]) > Math.abs(m[pivot][col])) pivot = r;
    }
    if (Math.abs(m[pivot][col]) < 1e-8) return fallback;
    if (pivot !== col) { const tmp = m[col]; m[col] = m[pivot]; m[pivot] = tmp; }
    const diag = m[col][col];
    for (let c = col; c < 4; ++c) m[col][c] /= diag;
    for (let r = 0; r < 3; ++r) {
      if (r === col) continue;
      const f = m[r][col];
      for (let c = col; c < 4; ++c) m[r][c] -= f * m[col][c];
    }
  }
  return [m[0][3], m[1][3], m[2][3]];
}

export function extractDualContourSurface(volume, title = "Dual-Contour Surface") {
  const { dim, mip1Dim, boundsMin, voxelStep, bricksPerAxis, narrowBandMeters, mip0Evicted } = volume;
  const inv32767Band = narrowBandMeters / 32767;

  // Use Mip1 grid when Mip0 has been evicted; otherwise extract directly from sparse 8x8x8 narrow-band bricks!
  const activeDim = mip0Evicted ? mip1Dim : dim;
  const activeStep = mip0Evicted ? voxelStep * 2 : voxelStep;
  const sampleAt = (ix, iy, iz) => {
    const cx = clamp(ix, 0, activeDim - 1), cy = clamp(iy, 0, activeDim - 1), cz = clamp(iz, 0, activeDim - 1);
    if (mip0Evicted) {
      return volume.conservativeMip1[(cz * mip1Dim + cy) * mip1Dim + cx];
    }
    if (volume.denseDistances) {
      return volume.denseDistances[(cz * dim + cy) * dim + cx];
    }
    const macroIdx = ((cz >> 3) * bricksPerAxis + (cy >> 3)) * bricksPerAxis + (cx >> 3);
    const slot = volume.brickIndirection[macroIdx];
    if (slot < 0) return volume.coarseBrickCenter[macroIdx];
    const li = ((cz & 7) * 8 + (cy & 7)) * 8 + (cx & 7);
    return volume.allocatedBricks[slot][li] * inv32767Band;
  };
  const worldPos = (ix, iy, iz) => [
    boundsMin[0] + (ix + 0.5) * activeStep,
    boundsMin[1] + (iy + 0.5) * activeStep,
    boundsMin[2] + (iz + 0.5) * activeStep,
  ];
  // Fast discrete central-difference gradient directly on sparse bricks (6x faster at 64^3-128^3!)
  const sampleGradAt = (ix, iy, iz) => {
    const gx = sampleAt(ix + 1, iy, iz) - sampleAt(ix - 1, iy, iz);
    const gy = sampleAt(ix, iy + 1, iz) - sampleAt(ix, iy - 1, iz);
    const gz = sampleAt(ix, iy, iz + 1) - sampleAt(ix, iy, iz - 1);
    return vNorm([gx, gy, gz]);
  };

  const cellVertIndex = new Map();
  const cellKey = (x, y, z) => (z * activeDim + y) * activeDim + x;
  const pos = [];
  const tris = [];
  let qefSolvedCells = 0;

  const corners = [
    [0,0,0],[1,0,0],[0,1,0],[1,1,0],
    [0,0,1],[1,0,1],[0,1,1],[1,1,1],
  ];
  const edges = [
    [0,1],[2,3],[4,5],[6,7],
    [0,2],[1,3],[4,6],[5,7],
    [0,4],[1,5],[2,6],[3,7],
  ];

  // Helper to solve 1 QEF vertex for a cell (x, y, z)
  const solveCell = (x, y, z) => {
    const key = cellKey(x, y, z);
    if (cellVertIndex.has(key)) return;
    const v0 = sampleAt(x, y, z);
    const v1 = sampleAt(x + 1, y, z);
    const v2 = sampleAt(x, y + 1, z);
    const v3 = sampleAt(x + 1, y + 1, z);
    const v4 = sampleAt(x, y, z + 1);
    const v5 = sampleAt(x + 1, y, z + 1);
    const v6 = sampleAt(x, y + 1, z + 1);
    const v7 = sampleAt(x + 1, y + 1, z + 1);
    const s0 = v0 < 0;
    if ((v1 < 0) === s0 && (v2 < 0) === s0 && (v3 < 0) === s0 &&
        (v4 < 0) === s0 && (v5 < 0) === s0 && (v6 < 0) === s0 && (v7 < 0) === s0) return;
    const vals = [v0, v1, v2, v3, v4, v5, v6, v7];

    const ata = [0, 0, 0, 0, 0, 0];
    const atb = [0, 0, 0];
    let avgX = 0, avgY = 0, avgZ = 0, count = 0;

    for (let e = 0; e < 12; ++e) {
      const c0 = edges[e][0], c1 = edges[e][1];
      const a = vals[c0], b = vals[c1];
      if ((a < 0) === (b < 0)) continue;
      const t = clamp(a / (a - b), 0.01, 0.99);
      const ix0 = x + corners[c0][0], iy0 = y + corners[c0][1], iz0 = z + corners[c0][2];
      const ix1 = x + corners[c1][0], iy1 = y + corners[c1][1], iz1 = z + corners[c1][2];
      const p0 = worldPos(ix0, iy0, iz0);
      const p1 = worldPos(ix1, iy1, iz1);
      const px = p0[0] + (p1[0] - p0[0]) * t;
      const py = p0[1] + (p1[1] - p0[1]) * t;
      const pz = p0[2] + (p1[2] - p0[2]) * t;
      const g0 = sampleGradAt(ix0, iy0, iz0);
      const g1 = sampleGradAt(ix1, iy1, iz1);
      const n = vNorm([
        g0[0] + (g1[0] - g0[0]) * t,
        g0[1] + (g1[1] - g0[1]) * t,
        g0[2] + (g1[2] - g0[2]) * t,
      ]);
      const d = n[0] * px + n[1] * py + n[2] * pz;
      ata[0] += n[0] * n[0]; ata[1] += n[0] * n[1]; ata[2] += n[0] * n[2];
      ata[3] += n[1] * n[1]; ata[4] += n[1] * n[2]; ata[5] += n[2] * n[2];
      atb[0] += n[0] * d;    atb[1] += n[1] * d;    atb[2] += n[2] * d;
      avgX += px; avgY += py; avgZ += pz; count++;
    }

    const massPt = [avgX / count, avgY / count, avgZ / count];
    const solved = solve3x3Symmetric(ata, atb, massPt);
    const cMin = worldPos(x, y, z);
    const cMax = worldPos(x + 1, y + 1, z + 1);
    cellVertIndex.set(key, pos.length / 3);
    pos.push(
      clamp(solved[0], cMin[0], cMax[0]),
      clamp(solved[1], cMin[1], cMax[1]),
      clamp(solved[2], cMin[2], cMax[2]),
    );
    qefSolvedCells++;
  };

  if (mip0Evicted) {
    for (let z = 0; z < activeDim - 1; ++z)
      for (let y = 0; y < activeDim - 1; ++y)
        for (let x = 0; x < activeDim - 1; ++x) solveCell(x, y, z);
  } else {
    // True sparse brick-only iteration: skip 80-90% empty bricks at 64^3-128^3!
    for (let bz = 0; bz < bricksPerAxis; ++bz) {
      for (let by = 0; by < bricksPerAxis; ++by) {
        for (let bx = 0; bx < bricksPerAxis; ++bx) {
          if (volume.brickIndirection[(bz * bricksPerAxis + by) * bricksPerAxis + bx] < 0) continue;
          const x0 = Math.max(0, bx * 8 - 1), x1 = Math.min(activeDim - 2, bx * 8 + 7);
          const y0 = Math.max(0, by * 8 - 1), y1 = Math.min(activeDim - 2, by * 8 + 7);
          const z0 = Math.max(0, bz * 8 - 1), z1 = Math.min(activeDim - 2, bz * 8 + 7);
          for (let z = z0; z <= z1; ++z)
            for (let y = y0; y <= y1; ++y)
              for (let x = x0; x <= x1; ++x) solveCell(x, y, z);
        }
      }
    }
  }

  // Emit quads ONLY around sign-changing cells present in cellVertIndex (O(ActiveCells) instead of O(dim^3)!)
  const emitQuad = (a, b, c, d, flip) => {
    if (a === undefined || b === undefined || c === undefined || d === undefined) return;
    if (flip) tris.push(a, c, b, a, d, c);
    else tris.push(a, b, c, a, c, d);
  };
  const cellAt = (x, y, z) => cellVertIndex.get(cellKey(x, y, z));

  for (const key of cellVertIndex.keys()) {
    const x = key % activeDim;
    const rem = (key - x) / activeDim;
    const y = rem % activeDim;
    const z = (rem - y) / activeDim;
    if (x < 1 || y < 1 || z < 1 || x >= activeDim - 1 || y >= activeDim - 1 || z >= activeDim - 1) continue;
    const v0 = sampleAt(x, y, z);
    const vx = sampleAt(x + 1, y, z);
    if ((v0 < 0) !== (vx < 0)) {
      emitQuad(cellAt(x, y - 1, z - 1), cellAt(x, y, z - 1), cellAt(x, y, z), cellAt(x, y - 1, z), v0 < 0);
    }
    const vy = sampleAt(x, y + 1, z);
    if ((v0 < 0) !== (vy < 0)) {
      emitQuad(cellAt(x - 1, y, z - 1), cellAt(x - 1, y, z), cellAt(x, y, z), cellAt(x, y, z - 1), v0 < 0);
    }
    const vz = sampleAt(x, y, z + 1);
    if ((v0 < 0) !== (vz < 0)) {
      emitQuad(cellAt(x - 1, y - 1, z), cellAt(x, y - 1, z), cellAt(x, y, z), cellAt(x - 1, y, z), v0 < 0);
    }
  }

  if (tris.length < 3) {
    return { surface: null, qefSolvedCells: 0, extractedQuadCount: 0, extractedTriangleCount: 0 };
  }
  const surface = finalizeGeometryNetwork(pos, null, tris, {
    id: "DualContourSurface",
    title,
    opticalCategory: "OpaqueSolid",
    skipNormalize: true,
  });
  return {
    surface,
    qefSolvedCells,
    extractedQuadCount: Math.floor(tris.length / 6),
    extractedTriangleCount: surface.triangleCount,
  };
}

//------------------------------------------------------------------------------------------------------------------------
//                      4-WAY MULTI-LOD CLUSTER DAG (RETOPO · NANITE-STYLE QEM · HYBRID PROB-QEM)
//------------------------------------------------------------------------------------------------------------------------

function spatialMortonScore(x, y, z, bMin, bMax) {
  const nx = clamp((x - bMin[0]) / Math.max(1e-4, bMax[0] - bMin[0]), 0, 0.999);
  const ny = clamp((y - bMin[1]) / Math.max(1e-4, bMax[1] - bMin[1]), 0, 0.999);
  const nz = clamp((z - bMin[2]) / Math.max(1e-4, bMax[2] - bMin[2]), 0, 0.999);
  const ix = Math.floor(nx * 1023), iy = Math.floor(ny * 1023), iz = Math.floor(nz * 1023);
  let code = 0;
  for (let b = 0; b < 10; ++b) {
    code |= ((ix >> b) & 1) << (3 * b);
    code |= ((iy >> b) & 1) << (3 * b + 1);
    code |= ((iz >> b) & 1) << (3 * b + 2);
  }
  return code;
}

//------------------------------------------------------------------------------------------------------------------------
//                           EXACT SURFACE SHRINKWRAP INDEX (BLENDER-STYLE SURFACE REPROJECTION)
//------------------------------------------------------------------------------------------------------------------------

function closestPointOnTriangleBary(px, py, pz, ax, ay, az, bx, by, bz, cx, cy, cz) {
  const abx = bx - ax, aby = by - ay, abz = bz - az;
  const acx = cx - ax, acy = cy - ay, acz = cz - az;
  const apx = px - ax, apy = py - ay, apz = pz - az;

  const d1 = abx * apx + aby * apy + abz * apz;
  const d2 = acx * apx + acy * apy + acz * apz;
  if (d1 <= 0 && d2 <= 0) {
    const dx = px - ax, dy = py - ay, dz = pz - az;
    return [ax, ay, az, 1, 0, 0, dx * dx + dy * dy + dz * dz];
  }

  const bpx = px - bx, bpy = py - by, bpz = pz - bz;
  const d3 = abx * bpx + aby * bpy + abz * bpz;
  const d4 = acx * bpx + acy * bpy + acz * bpz;
  if (d3 >= 0 && d4 <= d3) {
    const dx = px - bx, dy = py - by, dz = pz - bz;
    return [bx, by, bz, 0, 1, 0, dx * dx + dy * dy + dz * dz];
  }

  const vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) {
    const v = d1 / Math.max(1e-12, d1 - d3);
    const qx = ax + v * abx, qy = ay + v * aby, qz = az + v * abz;
    const dx = px - qx, dy = py - qy, dz = pz - qz;
    return [qx, qy, qz, 1 - v, v, 0, dx * dx + dy * dy + dz * dz];
  }

  const cpx = px - cx, cpy = py - cy, cpz = pz - cz;
  const d5 = abx * cpx + aby * cpy + abz * cpz;
  const d6 = acx * cpx + acy * cpy + acz * cpz;
  if (d6 >= 0 && d5 <= d6) {
    const dx = px - cx, dy = py - cy, dz = pz - cz;
    return [cx, cy, cz, 0, 0, 1, dx * dx + dy * dy + dz * dz];
  }

  const vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) {
    const w = d2 / Math.max(1e-12, d2 - d6);
    const qx = ax + w * acx, qy = ay + w * acy, qz = az + w * acz;
    const dx = px - qx, dy = py - qy, dz = pz - qz;
    return [qx, qy, qz, 1 - w, 0, w, dx * dx + dy * dy + dz * dz];
  }

  const va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
    const w = (d4 - d3) / Math.max(1e-12, (d4 - d3) + (d5 - d6));
    const qx = bx + w * (cx - bx), qy = by + w * (cy - by), qz = bz + w * (cz - bz);
    const dx = px - qx, dy = py - qy, dz = pz - qz;
    return [qx, qy, qz, 0, 1 - w, w, dx * dx + dy * dy + dz * dz];
  }

  const denom = 1 / Math.max(1e-12, va + vb + vc);
  const v = vb * denom;
  const w = vc * denom;
  const u = 1 - v - w;
  const qx = ax + abx * v + acx * w;
  const qy = ay + aby * v + acy * w;
  const qz = az + abz * v + acz * w;
  const dx = px - qx, dy = py - qy, dz = pz - qz;
  return [qx, qy, qz, u, v, w, dx * dx + dy * dy + dz * dz];
}

function buildShrinkwrapSurfaceIndex(geometry) {
  const { positions, normals, indices, triNormals, shellSide, triangleCount, boundsMin, boundsMax } = geometry;
  const gridDim = 10;
  const pad = 0.03;
  const minX = boundsMin[0] - pad, minY = boundsMin[1] - pad, minZ = boundsMin[2] - pad;
  const spanX = Math.max(1e-4, boundsMax[0] - boundsMin[0] + pad * 2);
  const spanY = Math.max(1e-4, boundsMax[1] - boundsMin[1] + pad * 2);
  const spanZ = Math.max(1e-4, boundsMax[2] - boundsMin[2] + pad * 2);
  const invX = gridDim / spanX, invY = gridDim / spanY, invZ = gridDim / spanZ;

  const cells = Array.from({ length: gridDim * gridDim * gridDim }, () => []);
  const triShell = new Int8Array(triangleCount);

  for (let t = 0; t < triangleCount; ++t) {
    const i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
    triShell[t] = shellSide[i0] || shellSide[i1] || shellSide[i2] || 0;

    const x0 = positions[i0 * 3], y0 = positions[i0 * 3 + 1], z0 = positions[i0 * 3 + 2];
    const x1 = positions[i1 * 3], y1 = positions[i1 * 3 + 1], z1 = positions[i1 * 3 + 2];
    const x2 = positions[i2 * 3], y2 = positions[i2 * 3 + 1], z2 = positions[i2 * 3 + 2];

    const gx0 = clamp(Math.floor((Math.min(x0, x1, x2) - minX) * invX), 0, gridDim - 1);
    const gx1 = clamp(Math.floor((Math.max(x0, x1, x2) - minX) * invX), 0, gridDim - 1);
    const gy0 = clamp(Math.floor((Math.min(y0, y1, y2) - minY) * invY), 0, gridDim - 1);
    const gy1 = clamp(Math.floor((Math.max(y0, y1, y2) - minY) * invY), 0, gridDim - 1);
    const gz0 = clamp(Math.floor((Math.min(z0, z1, z2) - minZ) * invZ), 0, gridDim - 1);
    const gz1 = clamp(Math.floor((Math.max(z0, z1, z2) - minZ) * invZ), 0, gridDim - 1);

    for (let gz = gz0; gz <= gz1; ++gz) {
      for (let gy = gy0; gy <= gy1; ++gy) {
        const row = (gz * gridDim + gy) * gridDim;
        for (let gx = gx0; gx <= gx1; ++gx) {
          cells[row + gx].push(t);
        }
      }
    }
  }

  return {
    positions, normals, indices, triNormals, triShell, triangleCount,
    gridDim, minX, minY, minZ, invX, invY, invZ, cells,
    stamp: new Uint32Array(triangleCount),
    stampTick: 0,
  };
}

function projectPointOntoOriginalSurface(sw, px, py, pz, nx, ny, nz, shellCode, enforceShellGuard) {
  const { positions, normals, indices, triNormals, triShell, gridDim, minX, minY, minZ, invX, invY, invZ, cells, stamp } = sw;
  const cx = clamp(Math.floor((px - minX) * invX), 0, gridDim - 1);
  const cy = clamp(Math.floor((py - minY) * invY), 0, gridDim - 1);
  const cz = clamp(Math.floor((pz - minZ) * invZ), 0, gridDim - 1);

  sw.stampTick++;
  const tick = sw.stampTick;
  let bestD2 = Infinity;
  let bestRes = null;
  let bestTri = -1;

  for (let radius = 0; radius <= 3; ++radius) {
    const x0 = Math.max(0, cx - radius), x1 = Math.min(gridDim - 1, cx + radius);
    const y0 = Math.max(0, cy - radius), y1 = Math.min(gridDim - 1, cy + radius);
    const z0 = Math.max(0, cz - radius), z1 = Math.min(gridDim - 1, cz + radius);

    for (let gz = z0; gz <= z1; ++gz) {
      for (let gy = y0; gy <= y1; ++gy) {
        const row = (gz * gridDim + gy) * gridDim;
        for (let gx = x0; gx <= x1; ++gx) {
          if (radius > 0 && gx > x0 && gx < x1 && gy > y0 && gy < y1 && gz > z0 && gz < z1) continue;
          const bucket = cells[row + gx];
          for (let b = 0; b < bucket.length; ++b) {
            const t = bucket[b];
            if (stamp[t] === tick) continue;
            stamp[t] = tick;

            if (enforceShellGuard && shellCode !== 0 && triShell[t] !== 0 && triShell[t] !== shellCode) continue;
            if (enforceShellGuard) {
              const nd = triNormals[t * 3] * nx + triNormals[t * 3 + 1] * ny + triNormals[t * 3 + 2] * nz;
              if (nd < 0.05) continue;
            }

            const i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
            const res = closestPointOnTriangleBary(
              px, py, pz,
              positions[i0 * 3], positions[i0 * 3 + 1], positions[i0 * 3 + 2],
              positions[i1 * 3], positions[i1 * 3 + 1], positions[i1 * 3 + 2],
              positions[i2 * 3], positions[i2 * 3 + 1], positions[i2 * 3 + 2],
            );
            if (res[6] < bestD2) {
              bestD2 = res[6];
              bestRes = res;
              bestTri = t;
            }
          }
        }
      }
    }
    if (bestRes !== null && radius >= 1) break;
  }

  if (bestRes === null) {
    return { x: px, y: py, z: pz, nx, ny, nz, dist: 0 };
  }

  const i0 = indices[bestTri * 3], i1 = indices[bestTri * 3 + 1], i2 = indices[bestTri * 3 + 2];
  const u = bestRes[3], v = bestRes[4], w = bestRes[5];
  let rnx = normals[i0 * 3] * u + normals[i1 * 3] * v + normals[i2 * 3] * w;
  let rny = normals[i0 * 3 + 1] * u + normals[i1 * 3 + 1] * v + normals[i2 * 3 + 1] * w;
  let rnz = normals[i0 * 3 + 2] * u + normals[i1 * 3 + 2] * v + normals[i2 * 3 + 2] * w;
  const rLen = Math.hypot(rnx, rny, rnz);
  if (rLen > 1e-6) { rnx /= rLen; rny /= rLen; rnz /= rLen; }
  else { rnx = nx; rny = ny; rnz = nz; }

  return { x: bestRes[0], y: bestRes[1], z: bestRes[2], nx: rnx, ny: rny, nz: rnz, dist: Math.sqrt(bestD2) };
}

//------------------------------------------------------------------------------------------------------------------------
//                   REAL TOPOLOGY WORKING MESH: RETOPO + SHRINKWRAP & HYBRID PROBABILISTIC QEM
//------------------------------------------------------------------------------------------------------------------------

function buildWeldedWorkingMesh(geometry, lockDualWalls) {
  const { positions, normals, uvs, indices, shellSide, uvSeamMask, vertexCount, triangleCount } = geometry;
  const quant = 2000; // 0.5mm spatial weld tolerance to close cube/lathe seam splits cleanly
  const weldMap = new Map();
  const remap = new Int32Array(vertexCount);

  const wPos = [];
  const wNorm = [];
  const wUv = [];
  const wShell = [];
  const wUvSeam = [];

  for (let i = 0; i < vertexCount; ++i) {
    const x = positions[i * 3], y = positions[i * 3 + 1], z = positions[i * 3 + 2];
    const sh = lockDualWalls ? shellSide[i] : 0;
    const key = `${sh}_${Math.round(x * quant)}_${Math.round(y * quant)}_${Math.round(z * quant)}`;
    const existing = weldMap.get(key);
    if (existing !== undefined) {
      remap[i] = existing;
      wNorm[existing * 3] += normals[i * 3];
      wNorm[existing * 3 + 1] += normals[i * 3 + 1];
      wNorm[existing * 3 + 2] += normals[i * 3 + 2];
      if (uvSeamMask[i]) wUvSeam[existing] = 1;
    } else {
      const id = wPos.length / 3;
      weldMap.set(key, id);
      remap[i] = id;
      wPos.push(x, y, z);
      wNorm.push(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
      wUv.push(uvs[i * 2], uvs[i * 2 + 1]);
      wShell.push(shellSide[i]);
      wUvSeam.push(uvSeamMask[i] ? 1 : 0);
    }
  }

  const wVertCount = wPos.length / 3;
  for (let i = 0; i < wVertCount; ++i) {
    const l = Math.hypot(wNorm[i * 3], wNorm[i * 3 + 1], wNorm[i * 3 + 2]) || 1;
    wNorm[i * 3] /= l; wNorm[i * 3 + 1] /= l; wNorm[i * 3 + 2] /= l;
  }

  const wTris = [];
  for (let t = 0; t < triangleCount; ++t) {
    const a = remap[indices[t * 3]];
    const b = remap[indices[t * 3 + 1]];
    const c = remap[indices[t * 3 + 2]];
    if (a !== b && b !== c && c !== a) {
      wTris.push(a, b, c);
    }
  }

  return {
    pos: wPos,
    norm: wNorm,
    uv: wUv,
    shell: wShell,
    uvSeam: wUvSeam,
    tris: wTris,
  };
}

function computeTriNormal(pos, a, b, c) {
  const ux = pos[b * 3] - pos[a * 3], uy = pos[b * 3 + 1] - pos[a * 3 + 1], uz = pos[b * 3 + 2] - pos[a * 3 + 2];
  const vx = pos[c * 3] - pos[a * 3], vy = pos[c * 3 + 1] - pos[a * 3 + 1], vz = pos[c * 3 + 2] - pos[a * 3 + 2];
  const nx = uy * vz - uz * vy;
  const ny = uz * vx - ux * vz;
  const nz = ux * vy - uy * vx;
  const l = Math.hypot(nx, ny, nz);
  return l > 1e-10 ? [nx / l, ny / l, nz / l, l * 0.5] : [0, 0, 1, 0];
}

function buildAdjacencyInfo(vertCount, tris, triAlive) {
  const triCount = tris.length / 3;
  const vertTris = Array.from({ length: vertCount }, () => []);
  const edgeCount = new Map();
  for (let t = 0; t < triCount; ++t) {
    if (!triAlive[t]) continue;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    vertTris[a].push(t); vertTris[b].push(t); vertTris[c].push(t);
    for (const [u, v] of [[a, b], [b, c], [c, a]]) {
      const k = u < v ? `${u}_${v}` : `${v}_${u}`;
      edgeCount.set(k, (edgeCount.get(k) || 0) + 1);
    }
  }
  const isBoundary = new Uint8Array(vertCount);
  for (const [k, cnt] of edgeCount.entries()) {
    if (cnt === 1) {
      const sep = k.indexOf("_");
      isBoundary[Number(k.slice(0, sep))] = 1;
      isBoundary[Number(k.slice(sep + 1))] = 1;
    }
  }
  return { vertTris, isBoundary };
}

function computeVertexCreaseField(pos, vertCount, tris, triAlive, creaseCosThreshold = 0.72) {
  const triCount = tris.length / 3;
  const tNorms = new Float32Array(triCount * 3);
  const vNormAcc = new Float32Array(vertCount * 3);
  const vTris = Array.from({ length: vertCount }, () => []);

  for (let t = 0; t < triCount; ++t) {
    if (!triAlive[t]) continue;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    const [nx, ny, nz, area] = computeTriNormal(pos, a, b, c);
    tNorms[t * 3] = nx; tNorms[t * 3 + 1] = ny; tNorms[t * 3 + 2] = nz;
    const w = Math.max(area, 1e-6);
    for (const vi of [a, b, c]) {
      vNormAcc[vi * 3] += nx * w;
      vNormAcc[vi * 3 + 1] += ny * w;
      vNormAcc[vi * 3 + 2] += nz * w;
      vTris[vi].push(t);
    }
  }

  const curvature = new Float32Array(vertCount);
  const isCrease = new Uint8Array(vertCount);
  for (let i = 0; i < vertCount; ++i) {
    const l = Math.hypot(vNormAcc[i * 3], vNormAcc[i * 3 + 1], vNormAcc[i * 3 + 2]) || 1;
    const nx = vNormAcc[i * 3] / l, ny = vNormAcc[i * 3 + 1] / l, nz = vNormAcc[i * 3 + 2] / l;
    vNormAcc[i * 3] = nx; vNormAcc[i * 3 + 1] = ny; vNormAcc[i * 3 + 2] = nz;
    let minDot = 1.0;
    const inc = vTris[i];
    for (let k = 0; k < inc.length; ++k) {
      const t = inc[k];
      const d = nx * tNorms[t * 3] + ny * tNorms[t * 3 + 1] + nz * tNorms[t * 3 + 2];
      if (d < minDot) minDot = d;
    }
    curvature[i] = clamp(1 - minDot, 0, 2);
    if (minDot < creaseCosThreshold) isCrease[i] = 1;
  }
  return { vNorms: vNormAcc, curvature, isCrease };
}

function orderStarRing(v, incidentTris, tris) {
  // Build directed half-edges (u -> w) around vertex v from triangles (v, u, w)
  const nextMap = new Map();
  const prevCount = new Map();
  for (let k = 0; k < incidentTris.length; ++k) {
    const t = incidentTris[k];
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    let u, w;
    if (a === v) { u = b; w = c; }
    else if (b === v) { u = c; w = a; }
    else { u = a; w = b; }
    if (nextMap.has(u)) return null; // non-manifold star
    nextMap.set(u, w);
    prevCount.set(w, (prevCount.get(w) || 0) + 1);
  }
  if (nextMap.size < 3) return null;
  const start = nextMap.keys().next().value;
  const ring = [start];
  let curr = nextMap.get(start);
  let guard = 0;
  while (curr !== undefined && curr !== start && guard++ <= nextMap.size + 1) {
    ring.push(curr);
    curr = nextMap.get(curr);
  }
  if (curr !== start || ring.length !== nextMap.size) return null; // open boundary or disconnected star
  return ring;
}

function earClipRing(ring, pos, refNormal) {
  // Cleanly re-triangulate the closed polygon ring after un-subdividing vertex v
  const poly = ring.slice();
  const outTris = [];
  const rn = refNormal;

  while (poly.length > 3) {
    let bestIdx = -1;
    let bestScore = -Infinity;
    const n = poly.length;
    for (let i = 0; i < n; ++i) {
      const a = poly[(i - 1 + n) % n];
      const b = poly[i];
      const c = poly[(i + 1) % n];
      const [nx, ny, nz, area] = computeTriNormal(pos, a, b, c);
      if (area < 1e-8) continue;
      const orient = nx * rn[0] + ny * rn[1] + nz * rn[2];
      if (orient < 0.20) continue; // refuse fold-over ear
      const l0 = Math.hypot(pos[b * 3] - pos[a * 3], pos[b * 3 + 1] - pos[a * 3 + 1], pos[b * 3 + 2] - pos[a * 3 + 2]);
      const l1 = Math.hypot(pos[c * 3] - pos[b * 3], pos[c * 3 + 1] - pos[b * 3 + 1], pos[c * 3 + 2] - pos[b * 3 + 2]);
      const l2 = Math.hypot(pos[a * 3] - pos[c * 3], pos[a * 3 + 1] - pos[c * 3 + 1], pos[a * 3 + 2] - pos[c * 3 + 2]);
      const quality = (4 * Math.sqrt(3) * area) / Math.max(1e-9, l0 * l0 + l1 * l1 + l2 * l2);
      const score = quality * 0.7 + orient * 0.3;
      if (score > bestScore) { bestScore = score; bestIdx = i; }
    }
    if (bestIdx < 0) return null; // abort if ring cannot be safely clipped without fold-over
    const n2 = poly.length;
    outTris.push(poly[(bestIdx - 1 + n2) % n2], poly[bestIdx], poly[(bestIdx + 1) % n2]);
    poly.splice(bestIdx, 1);
  }
  if (poly.length === 3) {
    const [nx, ny, nz, area] = computeTriNormal(pos, poly[0], poly[1], poly[2]);
    if (area < 1e-8 || (nx * rn[0] + ny * rn[1] + nz * rn[2]) < 0.15) return null;
    outTris.push(poly[0], poly[1], poly[2]);
  }
  return outTris;
}

//------------------------------------------------------------------------------------------------------------------------
//                          EXPLICIT FEATURE GUIDE CLASSIFICATION (HARD EDGES, UV SEAMS, BOUNDARIES)
//------------------------------------------------------------------------------------------------------------------------

function edgeKey(a, b) {
  return a < b ? `${a}_${b}` : `${b}_${a}`;
}

function classifyFeatureGuides(pos, uv, uvSeam, tris, triAlive, policy) {
  const vertCount = pos.length / 3;
  const triCount = triAlive.length;
  const useEdgeGuides = policy.useEdgeGuides ?? true;
  const lockUvSeams = policy.lockUvSeams ?? true;
  const lockBoundaries = policy.lockBoundaries ?? true;
  const creaseCos = Math.cos(((policy.creaseAngleDeg ?? 32) * Math.PI) / 180);

  const edgeTris = new Map();
  const triNorms = new Float32Array(triCount * 3);
  for (let t = 0; t < triCount; ++t) {
    if (!triAlive[t]) continue;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    const [nx, ny, nz] = computeTriNormal(pos, a, b, c);
    triNorms[t * 3] = nx; triNorms[t * 3 + 1] = ny; triNorms[t * 3 + 2] = nz;
    for (const [u, v] of [[a, b], [b, c], [c, a]]) {
      const k = edgeKey(u, v);
      let list = edgeTris.get(k);
      if (!list) { list = []; edgeTris.set(k, list); }
      list.push(t);
    }
  }

  const guideEdgeSet = new Set();
  const guideNeighbors = Array.from({ length: vertCount }, () => []);
  const isBoundary = new Uint8Array(vertCount);
  const isGuideVert = new Uint8Array(vertCount);
  let creaseEdgeCount = 0;
  let seamEdgeCount = 0;
  let boundaryEdgeCount = 0;

  for (const [k, list] of edgeTris.entries()) {
    const sep = k.indexOf("_");
    const u = Number(k.slice(0, sep));
    const v = Number(k.slice(sep + 1));

    let isGuide = false;
    if (list.length === 1) {
      isBoundary[u] = 1; isBoundary[v] = 1;
      if (lockBoundaries) { isGuide = true; boundaryEdgeCount++; }
    } else if (list.length >= 2) {
      const t0 = list[0], t1 = list[1];
      const dot = triNorms[t0 * 3] * triNorms[t1 * 3] + triNorms[t0 * 3 + 1] * triNorms[t1 * 3 + 1] + triNorms[t0 * 3 + 2] * triNorms[t1 * 3 + 2];
      if (useEdgeGuides && dot < creaseCos) {
        isGuide = true;
        creaseEdgeCount++;
      }
      if (!isGuide && lockUvSeams && uvSeam[u] && uvSeam[v]) {
        isGuide = true;
        seamEdgeCount++;
      }
    }

    if (isGuide) {
      guideEdgeSet.add(k);
      guideNeighbors[u].push(v);
      guideNeighbors[v].push(u);
      isGuideVert[u] = 1;
      isGuideVert[v] = 1;
    }
  }

  // Classify topological guide rank: 0 = free smooth interior, 1 = 1D guide curve, 2 = 0D pinned corner/junction
  const guideRank = new Uint8Array(vertCount);
  for (let i = 0; i < vertCount; ++i) {
    const gn = guideNeighbors[i];
    if (gn.length === 0) {
      guideRank[i] = 0;
    } else if (gn.length === 2) {
      // Check turning angle along the 1D guide curve — sharp bends become pinned 0D corners!
      const n0 = gn[0], n1 = gn[1];
      const d0 = vNorm([pos[n0 * 3] - pos[i * 3], pos[n0 * 3 + 1] - pos[i * 3 + 1], pos[n0 * 3 + 2] - pos[i * 3 + 2]]);
      const d1 = vNorm([pos[n1 * 3] - pos[i * 3], pos[n1 * 3 + 1] - pos[i * 3 + 1], pos[n1 * 3 + 2] - pos[i * 3 + 2]]);
      // Collinear vectors point in opposite directions (dot near -1); sharp corner has dot > -0.75
      guideRank[i] = vDot(d0, d1) > -0.72 ? 2 : 1;
    } else {
      guideRank[i] = 2; // 1 endpoint or >=3 guide junction -> strictly pinned 0D corner!
    }
  }

  return {
    guideEdgeSet,
    guideNeighbors,
    guideRank,
    isGuideVert,
    isBoundary,
    creaseEdgeCount,
    seamEdgeCount,
    boundaryEdgeCount,
    totalGuideEdges: guideEdgeSet.size,
  };
}

function relaxSurfaceShrinkwrap(pos, norm, shell, tris, triAlive, swIndex, iterations, lockDualWalls, guides, smoothStrength = 0.45, sharpness = null) {
  const vertCount = pos.length / 3;
  const triCount = triAlive.length;
  const neighbors = Array.from({ length: vertCount }, () => new Set());
  for (let t = 0; t < triCount; ++t) {
    if (!triAlive[t]) continue;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    neighbors[a].add(b); neighbors[a].add(c);
    neighbors[b].add(a); neighbors[b].add(c);
    neighbors[c].add(a); neighbors[c].add(b);
  }

  for (let iter = 0; iter < iterations; ++iter) {
    const nextPos = pos.slice();
    for (let i = 0; i < vertCount; ++i) {
      const nb = neighbors[i];
      if (nb.size === 0) continue;
      const rank = guides.guideRank[i];
      if (rank >= 2) continue; // 0D pinned corner / guide junction NEVER drifts!

      if (rank === 1) {
        // 1D guide curve vertex: slide ONLY along the tangent line of its 2 guide neighbors!
        const gn = guides.guideNeighbors[i];
        if (gn.length === 2) {
          const n0 = gn[0], n1 = gn[1];
          const midX = 0.5 * (pos[n0 * 3] + pos[n1 * 3]);
          const midY = 0.5 * (pos[n0 * 3 + 1] + pos[n1 * 3 + 1]);
          const midZ = 0.5 * (pos[n0 * 3 + 2] + pos[n1 * 3 + 2]);
          const tx = pos[n1 * 3] - pos[n0 * 3], ty = pos[n1 * 3 + 1] - pos[n0 * 3 + 1], tz = pos[n1 * 3 + 2] - pos[n0 * 3 + 2];
          const tl = Math.hypot(tx, ty, tz);
          if (tl > 1e-6) {
            const ux = tx / tl, uy = ty / tl, uz = tz / tl;
            const dx = midX - pos[i * 3], dy = midY - pos[i * 3 + 1], dz = midZ - pos[i * 3 + 2];
            const slide = (dx * ux + dy * uy + dz * uz) * Math.min(0.35, smoothStrength);
            nextPos[i * 3] = pos[i * 3] + ux * slide;
            nextPos[i * 3 + 1] = pos[i * 3 + 1] + uy * slide;
            nextPos[i * 3 + 2] = pos[i * 3 + 2] + uz * slide;
          }
        }
        continue;
      }

      // Free interior vertex: project neighbor centroid delta strictly onto 2D tangent plane
      let sx = 0, sy = 0, sz = 0, cnt = 0;
      for (const j of nb) {
        sx += pos[j * 3]; sy += pos[j * 3 + 1]; sz += pos[j * 3 + 2]; cnt++;
      }
      if (cnt === 0) continue;
      const dx = sx / cnt - pos[i * 3], dy = sy / cnt - pos[i * 3 + 1], dz = sz / cnt - pos[i * 3 + 2];
      const nx = norm[i * 3], ny = norm[i * 3 + 1], nz = norm[i * 3 + 2];
      const dn = dx * nx + dy * ny + dz * nz;
      const localSmooth = sharpness
        ? smoothStrength * (0.10 + 0.90 * Math.pow(1 - (sharpness[i] || 0), 1.6))
        : smoothStrength;
      nextPos[i * 3] = pos[i * 3] + (dx - dn * nx) * localSmooth;
      nextPos[i * 3 + 1] = pos[i * 3 + 1] + (dy - dn * ny) * localSmooth;
      nextPos[i * 3 + 2] = pos[i * 3 + 2] + (dz - dn * nz) * localSmooth;
    }

    // Exact Shell-Aware Surface Shrinkwrap Reprojection back onto original high-res LOD 0 surface!
    for (let i = 0; i < vertCount; ++i) {
      if (neighbors[i].size === 0 || guides.guideRank[i] >= 2) continue;
      const proj = projectPointOntoOriginalSurface(
        swIndex,
        nextPos[i * 3], nextPos[i * 3 + 1], nextPos[i * 3 + 2],
        norm[i * 3], norm[i * 3 + 1], norm[i * 3 + 2],
        shell[i],
        lockDualWalls,
      );
      pos[i * 3] = proj.x;
      pos[i * 3 + 1] = proj.y;
      pos[i * 3 + 2] = proj.z;
      if (guides.guideRank[i] === 0) {
        norm[i * 3] = proj.nx;
        norm[i * 3 + 1] = proj.ny;
        norm[i * 3 + 2] = proj.nz;
      }
    }
  }
}

function flipValenceEdges(pos, tris, triAlive, vertCount, guides, creaseCos = 0.80) {
  const triCount = triAlive.length;
  const valence = new Uint16Array(vertCount);
  const edgeTris = new Map();

  for (let t = 0; t < triCount; ++t) {
    if (!triAlive[t]) continue;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    for (const [u, v, opp] of [[a, b, c], [b, c, a], [c, a, b]]) {
      const k = edgeKey(u, v);
      let list = edgeTris.get(k);
      if (!list) { list = []; edgeTris.set(k, list); }
      list.push({ t, u, v, opp });
    }
  }
  for (const k of edgeTris.keys()) {
    const sep = k.indexOf("_");
    valence[Number(k.slice(0, sep))]++;
    valence[Number(k.slice(sep + 1))]++;
  }

  const touchedTri = new Uint8Array(triCount);
  for (const [k, list] of edgeTris.entries()) {
    if (list.length !== 2) continue;
    if (guides.guideEdgeSet.has(k)) continue; // NEVER flip a protected Hard Edge, UV Seam, or Boundary Guide!
    const e0 = list[0], e1 = list[1];
    if (touchedTri[e0.t] || touchedTri[e1.t]) continue;
    const a = e0.u, b = e0.v, c = e0.opp, d = e1.opp;
    if (c === d) continue;
    const altKey = edgeKey(c, d);
    if (edgeTris.has(altKey)) continue;

    const devBefore = Math.abs(valence[a] - 6) + Math.abs(valence[b] - 6) + Math.abs(valence[c] - 6) + Math.abs(valence[d] - 6);
    const devAfter = Math.abs(valence[a] - 7) + Math.abs(valence[b] - 7) + Math.abs(valence[c] - 5) + Math.abs(valence[d] - 5);
    if (devAfter >= devBefore) continue;

    const n0 = computeTriNormal(pos, a, b, c);
    const n1 = computeTriNormal(pos, b, a, d);
    if (n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2] < creaseCos) continue;

    const na = computeTriNormal(pos, a, d, c);
    const nb = computeTriNormal(pos, b, c, d);
    if (na[3] < 1e-8 || nb[3] < 1e-8) continue;
    const refX = n0[0] + n1[0], refY = n0[1] + n1[1], refZ = n0[2] + n1[2];
    if (na[0] * refX + na[1] * refY + na[2] * refZ <= 0.3) continue;
    if (nb[0] * refX + nb[1] * refY + nb[2] * refZ <= 0.3) continue;

    tris[e0.t * 3] = a; tris[e0.t * 3 + 1] = d; tris[e0.t * 3 + 2] = c;
    tris[e1.t * 3] = b; tris[e1.t * 3 + 1] = c; tris[e1.t * 3 + 2] = d;
    valence[a]--; valence[b]--; valence[c]++; valence[d]++;
    touchedTri[e0.t] = 1; touchedTri[e1.t] = 1;
  }
}

function splitLongEdges(pos, norm, uv, shell, uvSeam, tris, triAlive, swIndex, maxEdgeLen, lockDualWalls) {
  const triCount = triAlive.length;
  const edgeTris = new Map();
  for (let t = 0; t < triCount; ++t) {
    if (!triAlive[t]) continue;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    for (const [u, v, opp] of [[a, b, c], [b, c, a], [c, a, b]]) {
      const k = edgeKey(u, v);
      let list = edgeTris.get(k);
      if (!list) { list = []; edgeTris.set(k, list); }
      list.push({ t, u, v, opp });
    }
  }

  const maxLen2 = maxEdgeLen * maxEdgeLen;
  const touched = new Uint8Array(triCount);
  let splits = 0;
  for (const list of edgeTris.values()) {
    if (splits >= 140) break;
    if (list.length !== 2) continue;
    const e0 = list[0], e1 = list[1];
    if (touched[e0.t] || touched[e1.t]) continue;
    const a = e0.u, b = e0.v;
    const dx = pos[b * 3] - pos[a * 3], dy = pos[b * 3 + 1] - pos[a * 3 + 1], dz = pos[b * 3 + 2] - pos[a * 3 + 2];
    if (dx * dx + dy * dy + dz * dz <= maxLen2) continue;

    const mx = 0.5 * (pos[a * 3] + pos[b * 3]);
    const my = 0.5 * (pos[a * 3 + 1] + pos[b * 3 + 1]);
    const mz = 0.5 * (pos[a * 3 + 2] + pos[b * 3 + 2]);
    let mnx = norm[a * 3] + norm[b * 3], mny = norm[a * 3 + 1] + norm[b * 3 + 1], mnz = norm[a * 3 + 2] + norm[b * 3 + 2];
    const ml = Math.hypot(mnx, mny, mnz) || 1;
    mnx /= ml; mny /= ml; mnz /= ml;
    const sh = shell[a] === shell[b] ? shell[a] : 0;
    const proj = projectPointOntoOriginalSurface(swIndex, mx, my, mz, mnx, mny, mnz, sh, lockDualWalls);

    const mid = pos.length / 3;
    pos.push(proj.x, proj.y, proj.z);
    norm.push(proj.nx, proj.ny, proj.nz);
    uv.push(0.5 * (uv[a * 2] + uv[b * 2]), 0.5 * (uv[a * 2 + 1] + uv[b * 2 + 1]));
    shell.push(sh);
    uvSeam.push(uvSeam[a] && uvSeam[b] ? 1 : 0);

    tris[e0.t * 3] = e0.u; tris[e0.t * 3 + 1] = mid; tris[e0.t * 3 + 2] = e0.opp;
    tris.push(mid, e0.v, e0.opp); triAlive.push(1);
    tris[e1.t * 3] = e1.u; tris[e1.t * 3 + 1] = mid; tris[e1.t * 3 + 2] = e1.opp;
    tris.push(mid, e1.v, e1.opp); triAlive.push(1);
    touched[e0.t] = 1; touched[e1.t] = 1;
    splits++;
  }
}

function runGuidedEdgeCollapsePass(pos, norm, uv, shell, uvSeam, tris, triAlive, targetTris, policy, guides) {
  const { lockDualWalls, maxNormalDevDeg, featureGuideWeight = 0.75 } = policy;
  const vertCount = pos.length / 3;
  const { vertTris } = buildAdjacencyInfo(vertCount, tris, triAlive);
  const { vNorms, curvature } = computeVertexCreaseField(pos, vertCount, tris, triAlive, 0.78);
  let aliveCount = 0;
  for (let t = 0; t < triAlive.length; ++t) if (triAlive[t]) aliveCount++;
  if (aliveCount <= targetTris) return aliveCount;

  const seen = new Set();
  const edges = [];
  for (let t = 0; t < triAlive.length; ++t) {
    if (!triAlive[t]) continue;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    for (const [u, v] of [[a, b], [b, c], [c, a]]) {
      const lo = u < v ? u : v, hi = u < v ? v : u;
      const k = lo * vertCount + hi;
      if (seen.has(k)) continue;
      seen.add(k);
      const len = Math.hypot(pos[hi * 3] - pos[lo * 3], pos[hi * 3 + 1] - pos[lo * 3 + 1], pos[hi * 3 + 2] - pos[lo * 3 + 2]);
      const isGuideE = guides.guideEdgeSet.has(edgeKey(lo, hi));
      const rA = guides.guideRank[lo], rB = guides.guideRank[hi];

      // Strict Topological Guide Preservation Rules:
      // 1. NEVER collapse an edge connecting two pinned 0D corners!
      if (rA >= 2 && rB >= 2) continue;
      // 2. NEVER bridge across two SEPARATE guide curves (i.e. both vertices are guide verts, but edge is NOT a guide edge!)
      if (rA >= 1 && rB >= 1 && !isGuideE) continue;

      let cost = len * (1.0 + (1.5 + 4.5 * featureGuideWeight) * (curvature[lo] + curvature[hi]));
      if (isGuideE) cost *= (2.2 + 3.0 * featureGuideWeight); // keep guide curves denser & cleaner than flat interior!
      if (rA !== rB) cost *= (1.4 + 1.2 * featureGuideWeight);
      edges.push({ a: lo, b: hi, isGuideE, cost });
    }
  }
  edges.sort((e1, e2) => e1.cost - e2.cost);

  const rootVert = new Int32Array(vertCount);
  for (let i = 0; i < vertCount; ++i) rootVert[i] = i;
  const findV = (v) => {
    while (rootVert[v] !== v) {
      rootVert[v] = rootVert[rootVert[v]];
      v = rootVert[v];
    }
    return v;
  };
  const collapsed = new Uint8Array(vertCount);

  for (let ei = 0; ei < edges.length && aliveCount > targetTris; ++ei) {
    let a = findV(edges[ei].a);
    let b = findV(edges[ei].b);
    if (a === b || collapsed[a] || collapsed[b]) continue;

    const rA = guides.guideRank[a], rB = guides.guideRank[b];
    if (rA >= 2 && rB >= 2) continue;
    if (rA >= 1 && rB >= 1 && !edges[ei].isGuideE) continue;

    const dotN = clamp(
      vNorms[a * 3] * vNorms[b * 3] + vNorms[a * 3 + 1] * vNorms[b * 3 + 1] + vNorms[a * 3 + 2] * vNorms[b * 3 + 2],
      -1, 1,
    );
    const angleDeg = Math.acos(dotN) * (180 / Math.PI);
    if (lockDualWalls && shell[a] !== 0 && shell[b] !== 0 && shell[a] !== shell[b]) continue;
    if (lockDualWalls && dotN < -0.25) continue;
    if (angleDeg > Math.max(maxNormalDevDeg * 2.2, 28) && !edges[ei].isGuideE) continue;

    // Strictly pin target onto higher-rank guide vertex so guide curves & 0D corners NEVER move off the guide!
    let tx, ty, tz;
    if (rA > rB) {
      tx = pos[a * 3]; ty = pos[a * 3 + 1]; tz = pos[a * 3 + 2];
    } else if (rB > rA) {
      tx = pos[b * 3]; ty = pos[b * 3 + 1]; tz = pos[b * 3 + 2];
    } else {
      tx = 0.5 * (pos[a * 3] + pos[b * 3]);
      ty = 0.5 * (pos[a * 3 + 1] + pos[b * 3 + 1]);
      tz = 0.5 * (pos[a * 3 + 2] + pos[b * 3 + 2]);
    }

    const incA = vertTris[a];
    const incB = vertTris[b];
    let foldOver = false;
    const checkInc = (list) => {
      for (let k = 0; k < list.length; ++k) {
        const t = list[k];
        if (!triAlive[t]) continue;
        const v0 = findV(tris[t * 3]), v1 = findV(tris[t * 3 + 1]), v2 = findV(tris[t * 3 + 2]);
        if ((v0 === a || v1 === a || v2 === a) && (v0 === b || v1 === b || v2 === b)) continue;
        const nOld = computeTriNormal(pos, v0, v1, v2);
        const p0 = (v0 === a || v0 === b) ? [tx, ty, tz] : [pos[v0 * 3], pos[v0 * 3 + 1], pos[v0 * 3 + 2]];
        const p1 = (v1 === a || v1 === b) ? [tx, ty, tz] : [pos[v1 * 3], pos[v1 * 3 + 1], pos[v1 * 3 + 2]];
        const p2 = (v2 === a || v2 === b) ? [tx, ty, tz] : [pos[v2 * 3], pos[v2 * 3 + 1], pos[v2 * 3 + 2]];
        const cr = vCross(vSub(p1, p0), vSub(p2, p0));
        const cl = vLen(cr);
        if (cl < 1e-8 || (cr[0] * nOld[0] + cr[1] * nOld[1] + cr[2] * nOld[2]) / cl < 0.30) {
          foldOver = true;
          return;
        }
      }
    };
    checkInc(incA);
    if (!foldOver) checkInc(incB);
    if (foldOver) continue;

    // Commit collapse & preserve highest guide rank + seam flag
    pos[a * 3] = tx; pos[a * 3 + 1] = ty; pos[a * 3 + 2] = tz;
    let nnx = norm[a * 3] + norm[b * 3], nny = norm[a * 3 + 1] + norm[b * 3 + 1], nnz = norm[a * 3 + 2] + norm[b * 3 + 2];
    const nl = Math.hypot(nnx, nny, nnz) || 1;
    norm[a * 3] = nnx / nl; norm[a * 3 + 1] = nny / nl; norm[a * 3 + 2] = nnz / nl;
    uvSeam[a] = uvSeam[a] || uvSeam[b] ? 1 : 0;
    guides.guideRank[a] = Math.max(rA, rB);
    rootVert[b] = a;
    collapsed[a] = 1;
    collapsed[b] = 1;

    for (const list of [incB, incA]) {
      for (let k = 0; k < list.length; ++k) {
        const t = list[k];
        if (!triAlive[t]) continue;
        const v0 = findV(tris[t * 3]), v1 = findV(tris[t * 3 + 1]), v2 = findV(tris[t * 3 + 2]);
        tris[t * 3] = v0; tris[t * 3 + 1] = v1; tris[t * 3 + 2] = v2;
        if (v0 === v1 || v1 === v2 || v2 === v0) {
          triAlive[t] = 0;
          aliveCount--;
        } else if (list === incB) {
          incA.push(t);
        }
      }
    }
  }

  for (let t = 0; t < triAlive.length; ++t) {
    if (!triAlive[t]) continue;
    const v0 = findV(tris[t * 3]), v1 = findV(tris[t * 3 + 1]), v2 = findV(tris[t * 3 + 2]);
    tris[t * 3] = v0; tris[t * 3 + 1] = v1; tris[t * 3 + 2] = v2;
    if (v0 === v1 || v1 === v2 || v2 === v0) {
      triAlive[t] = 0;
      aliveCount--;
    }
  }
  return aliveCount;
}

//------------------------------------------------------------------------------------------------------------------------
//      PROBABILISTIC QUADRICS (TRETTNER & KOBBELT 2020) + FEATURE-GUIDED RETOPO + SHRINKWRAP & NANITE-STYLE QEM
//------------------------------------------------------------------------------------------------------------------------

function addPlaneQuadric(Q, off, a, b, c, d, weight = 1.0) {
  Q[off + 0] += weight * a * a;
  Q[off + 1] += weight * a * b;
  Q[off + 2] += weight * a * c;
  Q[off + 3] += weight * a * d;
  Q[off + 4] += weight * b * b;
  Q[off + 5] += weight * b * c;
  Q[off + 6] += weight * b * d;
  Q[off + 7] += weight * c * c;
  Q[off + 8] += weight * c * d;
  Q[off + 9] += weight * d * d;
}

function addProbabilisticPlaneQuadric(Q, off, nx, ny, nz, cx, cy, cz, weight, sigma) {
  // Trettner & Kobbelt (CGF 2020) isotropic probabilistic plane quadric:
  // A = w * (n*n^T + sigma^2 * I), b = -w * ((n.c)*n + sigma^2 * c)
  // Full-rank positive-definite everywhere: exact intersections on creases/corners, regular centroids on flats!
  const d = -(nx * cx + ny * cy + nz * cz);
  const s2 = sigma * sigma;
  const ws2 = weight * s2;
  Q[off + 0] += weight * nx * nx + ws2;
  Q[off + 1] += weight * nx * ny;
  Q[off + 2] += weight * nx * nz;
  Q[off + 3] += weight * nx * d - ws2 * cx;
  Q[off + 4] += weight * ny * ny + ws2;
  Q[off + 5] += weight * ny * nz;
  Q[off + 6] += weight * ny * d - ws2 * cy;
  Q[off + 7] += weight * nz * nz + ws2;
  Q[off + 8] += weight * nz * d - ws2 * cz;
  Q[off + 9] += weight * d * d + ws2 * (cx * cx + cy * cy + cz * cz);
}

function computeTriMinAngleRad(p0, p1, p2) {
  const ab = Math.hypot(p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]);
  const bc = Math.hypot(p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]);
  const ca = Math.hypot(p0[0] - p2[0], p0[1] - p2[1], p0[2] - p2[2]);
  if (ab < 1e-8 || bc < 1e-8 || ca < 1e-8) return 0;
  const a0 = Math.acos(clamp((ab * ab + ca * ca - bc * bc) / (2 * ab * ca), -1, 1));
  const a1 = Math.acos(clamp((ab * ab + bc * bc - ca * ca) / (2 * ab * bc), -1, 1));
  const a2 = Math.max(0, Math.PI - a0 - a1);
  return Math.min(a0, a1, a2);
}

function flipQualityEdges(pos, tris, triAlive, vertCount, guides, minDihedralDot = 0.84) {
  // Error-bounded Delaunay max-min angle + valence edge flip (Botsch & Kobbelt / Hu et al.)
  const edgeTris = new Map();
  const valence = new Int32Array(vertCount);
  for (let t = 0; t < triAlive.length; ++t) {
    if (!triAlive[t]) continue;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    valence[a]++; valence[b]++; valence[c]++;
    for (const [u, v] of [[a, b], [b, c], [c, a]]) {
      const ek = edgeKey(u, v);
      let list = edgeTris.get(ek);
      if (!list) { list = []; edgeTris.set(ek, list); }
      list.push(t);
    }
  }

  const touched = new Uint8Array(triAlive.length);
  for (const [ek, list] of edgeTris.entries()) {
    if (list.length !== 2) continue;
    if (guides?.guideEdgeSet.has(ek)) continue; // Never flip hard crease, UV seam, or open boundary!
    const t0 = list[0], t1 = list[1];
    if (touched[t0] || touched[t1] || !triAlive[t0] || !triAlive[t1]) continue;

    const sep = ek.indexOf("_");
    const u = Number(ek.slice(0, sep)), v = Number(ek.slice(sep + 1));
    const tri0 = [tris[t0 * 3], tris[t0 * 3 + 1], tris[t0 * 3 + 2]];
    const tri1 = [tris[t1 * 3], tris[t1 * 3 + 1], tris[t1 * 3 + 2]];
    const opp0 = tri0.find((x) => x !== u && x !== v);
    const opp1 = tri1.find((x) => x !== u && x !== v);
    if (opp0 === undefined || opp1 === undefined || opp0 === opp1) continue;
    if (edgeTris.has(edgeKey(opp0, opp1))) continue;

    const [n0x, n0y, n0z] = computeTriNormal(pos, tri0[0], tri0[1], tri0[2]);
    const [n1x, n1y, n1z] = computeTriNormal(pos, tri1[0], tri1[1], tri1[2]);
    if (n0x * n1x + n0y * n1y + n0z * n1z < minDihedralDot) continue;

    const pu = [pos[u * 3], pos[u * 3 + 1], pos[u * 3 + 2]];
    const pv = [pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]];
    const p0 = [pos[opp0 * 3], pos[opp0 * 3 + 1], pos[opp0 * 3 + 2]];
    const p1 = [pos[opp1 * 3], pos[opp1 * 3 + 1], pos[opp1 * 3 + 2]];

    const minBefore = Math.min(computeTriMinAngleRad(pu, pv, p0), computeTriMinAngleRad(pv, pu, p1));
    const minAfter = Math.min(computeTriMinAngleRad(p0, p1, pu), computeTriMinAngleRad(p1, p0, pv));

    const devBefore = Math.abs(valence[u] - 6) + Math.abs(valence[v] - 6) + Math.abs(valence[opp0] - 6) + Math.abs(valence[opp1] - 6);
    const devAfter = Math.abs(valence[u] - 7) + Math.abs(valence[v] - 7) + Math.abs(valence[opp0] - 5) + Math.abs(valence[opp1] - 5);

    const improvesAngle = minAfter > minBefore + 0.045; // > 2.5 deg improvement
    const improvesValence = devAfter < devBefore && minAfter >= Math.max(0.36, minBefore - 0.02);
    if (!improvesAngle && !improvesValence) continue;

    let nt0 = [opp0, opp1, u];
    let nt1 = [opp1, opp0, v];
    const [m0x, m0y, m0z, a0] = computeTriNormal(pos, nt0[0], nt0[1], nt0[2]);
    const [m1x, m1y, m1z, a1] = computeTriNormal(pos, nt1[0], nt1[1], nt1[2]);
    if (a0 < 1e-8 || a1 < 1e-8) continue;
    const avgN = vNorm([n0x + n1x, n0y + n1y, n0z + n1z]);
    if (m0x * avgN[0] + m0y * avgN[1] + m0z * avgN[2] < 0) nt0 = [opp0, u, opp1];
    if (m1x * avgN[0] + m1y * avgN[1] + m1z * avgN[2] < 0) nt1 = [opp1, v, opp0];
    const [c0x, c0y, c0z] = computeTriNormal(pos, nt0[0], nt0[1], nt0[2]);
    const [c1x, c1y, c1z] = computeTriNormal(pos, nt1[0], nt1[1], nt1[2]);
    if (c0x * avgN[0] + c0y * avgN[1] + c0z * avgN[2] < 0.72 || c1x * avgN[0] + c1y * avgN[1] + c1z * avgN[2] < 0.72) continue;

    tris[t0 * 3] = nt0[0]; tris[t0 * 3 + 1] = nt0[1]; tris[t0 * 3 + 2] = nt0[2];
    tris[t1 * 3] = nt1[0]; tris[t1 * 3 + 1] = nt1[1]; tris[t1 * 3 + 2] = nt1[2];
    valence[u]--; valence[v]--; valence[opp0]++; valence[opp1]++;
    touched[t0] = 1; touched[t1] = 1;
  }
}

function computeLocalRegionSharpness(pos, tris, triAlive, guides) {
  // Classify each active vertex into a continuous local regime in [0, 1]:
  //   0.0 (Flat / Low-Curvature)   -> Retopo + Shrinkwrap regime (high sigma, uniform edges, tangential equalize)
  //   0.5 (Smooth Medium Curvature)-> Hybrid Probabilistic QEM + Error-Guarded Shrinkwrap
  //   1.0 (Sharp / High-Curvature) -> Exact Nanite-Crisp Plane-QEM regime (sigma -> 0.002, zero tangential blur)
  const vertCount = pos.length / 3;
  const edgeTris = new Map();
  const adj = Array.from({ length: vertCount }, () => new Set());
  const active = new Uint8Array(vertCount);
  const triNorms = new Array(triAlive.length);

  for (let t = 0; t < triAlive.length; ++t) {
    if (!triAlive[t]) continue;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    active[a] = 1; active[b] = 1; active[c] = 1;
    triNorms[t] = computeTriNormal(pos, a, b, c);
    adj[a].add(b); adj[a].add(c);
    adj[b].add(a); adj[b].add(c);
    adj[c].add(a); adj[c].add(b);
    for (const [u, v] of [[a, b], [b, c], [c, a]]) {
      const ek = edgeKey(u, v);
      let list = edgeTris.get(ek);
      if (!list) { list = []; edgeTris.set(ek, list); }
      list.push(t);
    }
  }

  const maxDihedralDrop = new Float32Array(vertCount);
  for (const [ek, list] of edgeTris.entries()) {
    if (list.length < 2) continue;
    const n0 = triNorms[list[0]], n1 = triNorms[list[1]];
    const drop = Math.max(0, 1 - (n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2]));
    const sep = ek.indexOf("_");
    const u = Number(ek.slice(0, sep)), v = Number(ek.slice(sep + 1));
    if (drop > maxDihedralDrop[u]) maxDihedralDrop[u] = drop;
    if (drop > maxDihedralDrop[v]) maxDihedralDrop[v] = drop;
  }

  const vertNormX = new Float32Array(vertCount);
  const vertNormY = new Float32Array(vertCount);
  const vertNormZ = new Float32Array(vertCount);
  for (let t = 0; t < triAlive.length; ++t) {
    if (!triAlive[t]) continue;
    const tn = triNorms[t];
    const w = tn[3];
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    vertNormX[a] += tn[0] * w; vertNormY[a] += tn[1] * w; vertNormZ[a] += tn[2] * w;
    vertNormX[b] += tn[0] * w; vertNormY[b] += tn[1] * w; vertNormZ[b] += tn[2] * w;
    vertNormX[c] += tn[0] * w; vertNormY[c] += tn[1] * w; vertNormZ[c] += tn[2] * w;
  }

  const sharpness = new Float32Array(vertCount);
  let sharpCnt = 0, midCnt = 0, flatCnt = 0, activeCnt = 0;
  for (let i = 0; i < vertCount; ++i) {
    if (!active[i]) continue;
    activeCnt++;
    const nbs = adj[i];
    let normalCurv = 0;
    if (nbs.size >= 2) {
      let ax = 0, ay = 0, az = 0, eLenSum = 0;
      const vx = pos[i * 3], vy = pos[i * 3 + 1], vz = pos[i * 3 + 2];
      for (const nb of nbs) {
        const nx = pos[nb * 3], ny = pos[nb * 3 + 1], nz = pos[nb * 3 + 2];
        ax += nx; ay += ny; az += nz;
        eLenSum += Math.hypot(nx - vx, ny - vy, nz - vz);
      }
      ax /= nbs.size; ay /= nbs.size; az /= nbs.size;
      const meanEdge = Math.max(1e-5, eLenSum / nbs.size);
      const nl = Math.hypot(vertNormX[i], vertNormY[i], vertNormZ[i]) || 1;
      // Normal-projected discrete mean curvature: invariant to tangential non-uniformity!
      const lapDotN = Math.abs(
        ((vx - ax) * vertNormX[i] + (vy - ay) * vertNormY[i] + (vz - az) * vertNormZ[i]) / nl,
      );
      normalCurv = lapDotN / meanEdge;
    }

    const raw = maxDihedralDrop[i] * 1.35 + normalCurv * 2.1;
    let s = clamp((raw - 0.05) / 0.36, 0, 1);
    if (guides?.guideRank && guides.guideRank[i] >= 1) {
      s = Math.max(s, guides.guideRank[i] >= 2 ? 1.0 : 0.85);
    }
    sharpness[i] = s;
    if (s >= 0.65) sharpCnt++;
    else if (s <= 0.28) flatCnt++;
    else midCnt++;
  }

  const denom = Math.max(1, activeCnt);
  return {
    sharpness,
    sharpPct: Math.round((sharpCnt / denom) * 100),
    midPct: Math.round((midCnt / denom) * 100),
    flatPct: Math.round((flatCnt / denom) * 100),
  };
}

function runHybridProbQemStep(pos, norm, uv, shell, uvSeam, tris, triAlive, targetTris, policy, swIndex, guides, regionField) {
  // Frontier Spatially-Adaptive Hybrid Step:
  // - High-Curvature / Sharp Zones -> Exact Nanite-Crisp Plane-QEM (sigma -> 0.002, zero centroid blur!)
  // - Flat / Low-Curvature Zones   -> Full-Rank Probabilistic QEM + Edge Regularity (Retopo-clean, 0% slivers!)
  // - Smooth Transitions           -> C1 Curvature-Driven Blend + 1D Guide Curves + Dual-Wall Shell Lock!
  const { lockDualWalls, featureGuideWeight = 0.75, probQemSigma = 0.035, regionalAdaptive = true } = policy;
  const sharpness = regionField?.sharpness;
  const vertCount = pos.length / 3;
  const triCount = triAlive.length;

  const Q = new Float64Array(vertCount * 10);
  const Qgeom = new Float64Array(vertCount * 10); // Pure sigma=0 geometric plane quadric for unbiased cross-region collapse ordering!
  const edgeTris = new Map();
  const vertTris = Array.from({ length: vertCount }, () => []);
  let aliveCount = 0;

  // 1. Memoryless Spatially-Varying Probabilistic Plane Quadrics (placement) + Pure Plane Quadrics (unbiased cost!)
  for (let t = 0; t < triCount; ++t) {
    if (!triAlive[t]) continue;
    aliveCount++;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    const [nx, ny, nz, area] = computeTriNormal(pos, a, b, c);
    if (area < 1e-10) continue;
    const cx = (pos[a * 3] + pos[b * 3] + pos[c * 3]) / 3;
    const cy = (pos[a * 3 + 1] + pos[b * 3 + 1] + pos[c * 3 + 1]) / 3;
    const cz = (pos[a * 3 + 2] + pos[b * 3 + 2] + pos[c * 3 + 2]) / 3;
    const dPlane = -(nx * cx + ny * cy + nz * cz);
    const w = Math.max(area, 1e-5);

    let localSigma = probQemSigma;
    if (regionalAdaptive && sharpness) {
      const triSharp = (sharpness[a] + sharpness[b] + sharpness[c]) / 3;
      const flatness = (1 - triSharp) * (1 - triSharp);
      const triEdge = Math.sqrt(area * 2.31);
      const flatSigma = clamp(Math.max(probQemSigma * 1.4, triEdge * 0.38), 0.016, 0.10);
      const sharpSigma = Math.max(0.005, probQemSigma * 0.22);
      localSigma = sharpSigma + (flatSigma - sharpSigma) * flatness;
    }

    addProbabilisticPlaneQuadric(Q, a * 10, nx, ny, nz, cx, cy, cz, w, localSigma);
    addProbabilisticPlaneQuadric(Q, b * 10, nx, ny, nz, cx, cy, cz, w, localSigma);
    addProbabilisticPlaneQuadric(Q, c * 10, nx, ny, nz, cx, cy, cz, w, localSigma);
    addPlaneQuadric(Qgeom, a * 10, nx, ny, nz, dPlane, w);
    addPlaneQuadric(Qgeom, b * 10, nx, ny, nz, dPlane, w);
    addPlaneQuadric(Qgeom, c * 10, nx, ny, nz, dPlane, w);
    if (policy.anchorLod0 && swIndex) {
      // Anchor QEM quadrics directly to exact LOD 0 surface planes to eliminate second-generation Retopo drift!
      const triShell = (shell[a] === shell[b] && shell[b] === shell[c]) ? shell[a] : 0;
      const p0 = projectPointOntoOriginalSurface(swIndex, cx, cy, cz, nx, ny, nz, triShell, lockDualWalls);
      if (p0.nx * nx + p0.ny * ny + p0.nz * nz > 0.45) {
        const d0 = -(p0.nx * p0.x + p0.ny * p0.y + p0.nz * p0.z);
        const w0 = w * 0.85;
        addProbabilisticPlaneQuadric(Q, a * 10, p0.nx, p0.ny, p0.nz, p0.x, p0.y, p0.z, w0, localSigma * 0.6);
        addProbabilisticPlaneQuadric(Q, b * 10, p0.nx, p0.ny, p0.nz, p0.x, p0.y, p0.z, w0, localSigma * 0.6);
        addProbabilisticPlaneQuadric(Q, c * 10, p0.nx, p0.ny, p0.nz, p0.x, p0.y, p0.z, w0, localSigma * 0.6);
        addPlaneQuadric(Qgeom, a * 10, p0.nx, p0.ny, p0.nz, d0, w0);
        addPlaneQuadric(Qgeom, b * 10, p0.nx, p0.ny, p0.nz, d0, w0);
        addPlaneQuadric(Qgeom, c * 10, p0.nx, p0.ny, p0.nz, d0, w0);
      }
    }
    vertTris[a].push(t); vertTris[b].push(t); vertTris[c].push(t);
    for (const [u, v] of [[a, b], [b, c], [c, a]]) {
      const ek = edgeKey(u, v);
      let list = edgeTris.get(ek);
      if (!list) { list = []; edgeTris.set(ek, list); }
      list.push(t);
    }
  }
  if (aliveCount <= targetTris) return aliveCount;

  // 2. Inject perpendicular Feature-Curve & Boundary Constraint Quadrics so 1D guides stay razor-straight!
  const guidePenaltyScale = 12.0 + 36.0 * featureGuideWeight;
  for (const [ek, list] of edgeTris.entries()) {
    const isOpen = list.length === 1;
    const isGuide = guides.guideEdgeSet.has(ek);
    if (!isOpen && !isGuide) continue;
    const sep = ek.indexOf("_");
    const u = Number(ek.slice(0, sep)), v = Number(ek.slice(sep + 1));
    const eVec = [pos[v * 3] - pos[u * 3], pos[v * 3 + 1] - pos[u * 3 + 1], pos[v * 3 + 2] - pos[u * 3 + 2]];
    const eLen = vLen(eVec);
    if (eLen < 1e-7) continue;

    // Average incident normal to construct plane perpendicular to both surface & feature curve
    let anx = 0, any = 0, anz = 0;
    for (let k = 0; k < list.length; ++k) {
      const ti = list[k];
      const [tnx, tny, tnz] = computeTriNormal(pos, tris[ti * 3], tris[ti * 3 + 1], tris[ti * 3 + 2]);
      anx += tnx; any += tny; anz += tnz;
    }
    const bn = vNorm(vCross(eVec, [anx, any, anz]));
    const bd = -(bn[0] * pos[u * 3] + bn[1] * pos[u * 3 + 1] + bn[2] * pos[u * 3 + 2]);
    const bw = eLen * eLen * (isOpen ? guidePenaltyScale * 1.5 : guidePenaltyScale);
    addPlaneQuadric(Q, u * 10, bn[0], bn[1], bn[2], bd, bw);
    addPlaneQuadric(Q, v * 10, bn[0], bn[1], bn[2], bd, bw);
    addPlaneQuadric(Qgeom, u * 10, bn[0], bn[1], bn[2], bd, bw);
    addPlaneQuadric(Qgeom, v * 10, bn[0], bn[1], bn[2], bd, bw);
  }

  // 3. Score candidate edge collapses: solve placement with spatially-adaptive Q, rank cost with unbiased Qgeom!
  const evalQSum = (Qa, u, v, x, y, z) => evaluateQuadric(
    Qa[u * 10] + Qa[v * 10], Qa[u * 10 + 1] + Qa[v * 10 + 1], Qa[u * 10 + 2] + Qa[v * 10 + 2], Qa[u * 10 + 3] + Qa[v * 10 + 3],
    Qa[u * 10 + 4] + Qa[v * 10 + 4], Qa[u * 10 + 5] + Qa[v * 10 + 5], Qa[u * 10 + 6] + Qa[v * 10 + 6],
    Qa[u * 10 + 7] + Qa[v * 10 + 7], Qa[u * 10 + 8] + Qa[v * 10 + 8], Qa[u * 10 + 9] + Qa[v * 10 + 9],
    x, y, z,
  );
  const candidates = [];
  for (const ek of edgeTris.keys()) {
    const sep = ek.indexOf("_");
    const u = Number(ek.slice(0, sep)), v = Number(ek.slice(sep + 1));

    // Dual-Wall Glass/Thin-Shell Guard: NEVER collapse inner wall onto outer wall!
    if (lockDualWalls && shell[u] !== 0 && shell[v] !== 0 && shell[u] !== shell[v]) continue;

    const rU = guides.guideRank[u];
    const rV = guides.guideRank[v];
    const isGuideEdge = guides.guideEdgeSet.has(ek);

    // Topological Feature-Curve Rules (Dunyach & Botsch EG 2013):
    // - Never collapse two 0D corners together!
    if (rU >= 2 && rV >= 2) continue;
    // - Never collapse across two DIFFERENT guide curves/seams (non-guide bridge between feature vertices)!
    if (rU >= 1 && rV >= 1 && !isGuideEdge) continue;

    let sol = solveOptimalQemPlacement(
      Q, u * 10, v * 10,
      pos[u * 3], pos[u * 3 + 1], pos[u * 3 + 2],
      pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2],
    );

    // Pin target strictly onto higher-rank topological feature (0D corner > 1D guide > free interior)
    let tx = sol.x, ty = sol.y, tz = sol.z;
    if (rU > rV) {
      tx = pos[u * 3]; ty = pos[u * 3 + 1]; tz = pos[u * 3 + 2];
    } else if (rV > rU) {
      tx = pos[v * 3]; ty = pos[v * 3 + 1]; tz = pos[v * 3 + 2];
    } else if (rU === 1 && rV === 1 && isGuideEdge) {
      // 1D Along-Guide Collapse: project optimal placement strictly onto segment (u, v) so crease/seam never drifts!
      const dx = pos[v * 3] - pos[u * 3], dy = pos[v * 3 + 1] - pos[u * 3 + 1], dz = pos[v * 3 + 2] - pos[u * 3 + 2];
      const d2 = dx * dx + dy * dy + dz * dz + 1e-12;
      const projT = clamp(((tx - pos[u * 3]) * dx + (ty - pos[u * 3 + 1]) * dy + (tz - pos[u * 3 + 2]) * dz) / d2, 0, 1);
      tx = pos[u * 3] + projT * dx;
      ty = pos[u * 3 + 1] + projT * dy;
      tz = pos[u * 3 + 2] + projT * dz;
    }

    // Rank collapse priority with unbiased geometric plane quadric Qgeom (when regionalAdaptive) so flat faces NEVER pay sigma^2 bias!
    let err = regionalAdaptive ? evalQSum(Qgeom, u, v, tx, ty, tz) : evalQSum(Q, u, v, tx, ty, tz);
    if (rU === 1 && rV === 1 && isGuideEdge) {
      err *= 1.0 + 1.8 * featureGuideWeight;
    }

    // Gentle edge-length regularity bias (short flat edges collapse cleanly before long regular triangles!)
    const eLen = Math.hypot(pos[v * 3] - pos[u * 3], pos[v * 3 + 1] - pos[u * 3 + 1], pos[v * 3 + 2] - pos[u * 3 + 2]);
    if (policy.anchorLod0 && rU === rV) {
      // Spike guard: prevent ill-conditioned coarse QEM solves from launching vertices far off thin features (e.g. teapot spout)!
      const mx = 0.5 * (pos[u * 3] + pos[v * 3]), my = 0.5 * (pos[u * 3 + 1] + pos[v * 3 + 1]), mz = 0.5 * (pos[u * 3 + 2] + pos[v * 3 + 2]);
      if (Math.hypot(tx - mx, ty - my, tz - mz) > Math.max(eLen * 0.68, 1e-4)) {
        const eu = evalQSum(Qgeom, u, v, pos[u * 3], pos[u * 3 + 1], pos[u * 3 + 2]);
        const ev = evalQSum(Qgeom, u, v, pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]);
        const em = evalQSum(Qgeom, u, v, mx, my, mz);
        if (em <= eu && em <= ev) { tx = mx; ty = my; tz = mz; err = em; }
        else if (eu <= ev) { tx = pos[u * 3]; ty = pos[u * 3 + 1]; tz = pos[u * 3 + 2]; err = eu; }
        else { tx = pos[v * 3]; ty = pos[v * 3 + 1]; tz = pos[v * 3 + 2]; err = ev; }
      }
    }
    const score = err + eLen * eLen * 1e-4;
    candidates.push({ u, v, score, x: tx, y: ty, z: tz, isGuideEdge });
  }
  candidates.sort((c1, c2) => c1.score - c2.score);

  const rootVert = new Int32Array(vertCount);
  for (let i = 0; i < vertCount; ++i) rootVert[i] = i;
  const findV = (v) => {
    while (rootVert[v] !== v) {
      rootVert[v] = rootVert[rootVert[v]];
      v = rootVert[v];
    }
    return v;
  };
  const collapsed = new Uint8Array(vertCount);

  for (let ci = 0; ci < candidates.length && aliveCount > targetTris; ++ci) {
    let a = findV(candidates[ci].u);
    let b = findV(candidates[ci].v);
    if (a === b || collapsed[a] || collapsed[b]) continue;
    if (lockDualWalls && shell[a] !== 0 && shell[b] !== 0 && shell[a] !== shell[b]) continue;

    const rA = guides.guideRank[a], rB = guides.guideRank[b];
    if (rA >= 2 && rB >= 2) continue;
    const isGuideEdge = guides.guideEdgeSet.has(edgeKey(a, b));
    if (rA >= 1 && rB >= 1 && !isGuideEdge) continue;

    // Keep higher-rank vertex as survivor `a`
    if (rB > rA) { const tmp = a; a = b; b = tmp; }

    let tx = candidates[ci].x, ty = candidates[ci].y, tz = candidates[ci].z;
    if (guides.guideRank[a] >= 2) {
      tx = pos[a * 3]; ty = pos[a * 3 + 1]; tz = pos[a * 3 + 2];
    }

    const incA = vertTris[a];
    const incB = vertTris[b];

    // Verify no fold-overs & reject severe sliver creation (< 8 deg) unless original triangle was already worse
    let badCollapse = false;
    const checkInc = (list) => {
      for (let k = 0; k < list.length; ++k) {
        const t = list[k];
        if (!triAlive[t]) continue;
        const v0 = findV(tris[t * 3]), v1 = findV(tris[t * 3 + 1]), v2 = findV(tris[t * 3 + 2]);
        if ((v0 === a || v1 === a || v2 === a) && (v0 === b || v1 === b || v2 === b)) continue;
        const o0 = [pos[v0 * 3], pos[v0 * 3 + 1], pos[v0 * 3 + 2]];
        const o1 = [pos[v1 * 3], pos[v1 * 3 + 1], pos[v1 * 3 + 2]];
        const o2 = [pos[v2 * 3], pos[v2 * 3 + 1], pos[v2 * 3 + 2]];
        const nOld = computeTriNormal(pos, v0, v1, v2);
        const p0 = (v0 === a || v0 === b) ? [tx, ty, tz] : o0;
        const p1 = (v1 === a || v1 === b) ? [tx, ty, tz] : o1;
        const p2 = (v2 === a || v2 === b) ? [tx, ty, tz] : o2;
        const cr = vCross(vSub(p1, p0), vSub(p2, p0));
        const cl = vLen(cr);
        if (cl < 1e-8 || (cr[0] * nOld[0] + cr[1] * nOld[1] + cr[2] * nOld[2]) / cl < 0.22) {
          badCollapse = true;
          return;
        }
        const minBefore = computeTriMinAngleRad(o0, o1, o2);
        const minAfter = computeTriMinAngleRad(p0, p1, p2);
        const minGuard = policy.minAngleGuardRad ?? 0.14;
        if (minAfter < minGuard && minAfter < minBefore * 0.72) {
          badCollapse = true;
          return;
        }
        if (policy.maxCollapseError && swIndex) {
          // Hu et al. (2016) Hausdorff Envelope Guard: reject any coarse collapse that creates a new surface-error spike!
          const ncx = (p0[0] + p1[0] + p2[0]) / 3, ncy = (p0[1] + p1[1] + p2[1]) / 3, ncz = (p0[2] + p1[2] + p2[2]) / 3;
          const tSh = (shell[v0] === shell[v1] && shell[v1] === shell[v2]) ? shell[v0] : 0;
          const nnx = cr[0] / cl, nny = cr[1] / cl, nnz = cr[2] / cl;
          const pNew = projectPointOntoOriginalSurface(swIndex, ncx, ncy, ncz, nnx, nny, nnz, tSh, lockDualWalls);
          if (pNew.dist > policy.maxCollapseError) {
            const ocx = (o0[0] + o1[0] + o2[0]) / 3, ocy = (o0[1] + o1[1] + o2[1]) / 3, ocz = (o0[2] + o1[2] + o2[2]) / 3;
            const pOld = projectPointOntoOriginalSurface(swIndex, ocx, ocy, ocz, nOld[0], nOld[1], nOld[2], tSh, lockDualWalls);
            if (pNew.dist > Math.max(policy.maxCollapseError, pOld.dist * 1.15)) {
              badCollapse = true;
              return;
            }
          }
        }
      }
    };
    checkInc(incA);
    if (!badCollapse) checkInc(incB);
    if (badCollapse) continue;

    // Only guard against wild QEM runaway placements during collapse; level-adaptive Shrinkwrap runs cleanly post-pass
    if (guides.guideRank[a] === 0 && guides.guideRank[b] === 0 && swIndex) {
      const proj = projectPointOntoOriginalSurface(
        swIndex, tx, ty, tz,
        norm[a * 3], norm[a * 3 + 1], norm[a * 3 + 2],
        shell[a], lockDualWalls,
      );
      if (proj.dist > 0.04) {
        const pull = (proj.dist - 0.04) / proj.dist;
        tx += (proj.x - tx) * pull;
        ty += (proj.y - ty) * pull;
        tz += (proj.z - tz) * pull;
      }
      norm[a * 3] = proj.nx; norm[a * 3 + 1] = proj.ny; norm[a * 3 + 2] = proj.nz;
    }

    pos[a * 3] = tx; pos[a * 3 + 1] = ty; pos[a * 3 + 2] = tz;
    for (let q = 0; q < 10; ++q) Q[a * 10 + q] += Q[b * 10 + q];
    uvSeam[a] = uvSeam[a] || uvSeam[b] ? 1 : 0;
    guides.guideRank[a] = Math.max(guides.guideRank[a], guides.guideRank[b]);
    rootVert[b] = a;
    collapsed[a] = 1;
    collapsed[b] = 1;

    for (const list of [incB, incA]) {
      for (let k = 0; k < list.length; ++k) {
        const t = list[k];
        if (!triAlive[t]) continue;
        const v0 = findV(tris[t * 3]), v1 = findV(tris[t * 3 + 1]), v2 = findV(tris[t * 3 + 2]);
        tris[t * 3] = v0; tris[t * 3 + 1] = v1; tris[t * 3 + 2] = v2;
        if (v0 === v1 || v1 === v2 || v2 === v0) {
          triAlive[t] = 0;
          aliveCount--;
        } else if (list === incB) {
          incA.push(t);
        }
      }
    }
  }

  for (let t = 0; t < triAlive.length; ++t) {
    if (!triAlive[t]) continue;
    const v0 = findV(tris[t * 3]), v1 = findV(tris[t * 3 + 1]), v2 = findV(tris[t * 3 + 2]);
    tris[t * 3] = v0; tris[t * 3 + 1] = v1; tris[t * 3 + 2] = v2;
    if (v0 === v1 || v1 === v2 || v2 === v0) {
      triAlive[t] = 0;
      aliveCount--;
    }
  }
  return aliveCount;
}

function applyAdaptiveHybridShrinkwrap(pos, norm, shell, tris, triAlive, swIndex, guides, policy, lodLevel, regionField) {
  // Error-Guarded Region-Aware LOD-Adaptive Shrinkwrap (Local 1-Ring Hausdorff Coordinate Descent):
  // - Evaluates exact 1-ring surface error (vertex->LOD0 + incident tri-centroids->LOD0) before accepting ANY move!
  // - Region-Aware (when policy.regionalAdaptive !== false):
  //   * Flat / Low-Curvature zones -> stronger Retopo tangential equalization + exact LOD 0 Shrinkwrap (even at LOD1!)
  //   * Sharp / High-Curvature zones -> zero tangential drift (keeps Nanite-crisp plane intersections!)
  //   * Mid-Curvature zones -> C1 smooth blend + error-guarded chord straddling
  const swMode = policy.hybridShrinkwrapMode || "adaptive";
  if (swMode === "off" || !swIndex) return;

  const lockDualWalls = policy.lockDualWalls;
  const regionalAdaptive = policy.regionalAdaptive ?? true;
  const sharpness = regionField?.sharpness;
  const shrinkwrapPasses = clamp(policy.shrinkwrapPasses ?? 2, 1, 4);
  const vertCount = pos.length / 3;
  const triCount = triAlive.length;

  const runFull =
    swMode === "allLods" ||
    (swMode === "adaptive" && lodLevel >= 2) ||
    (swMode === "strictLow" && lodLevel >= 2) ||
    (swMode === "lowOnly" && lodLevel >= 3);

  const activeVert = new Uint8Array(vertCount);
  const vertTris = Array.from({ length: vertCount }, () => []);
  const vertAdj = Array.from({ length: vertCount }, () => new Set());
  for (let t = 0; t < triCount; ++t) {
    if (!triAlive[t]) continue;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    activeVert[a] = 1; activeVert[b] = 1; activeVert[c] = 1;
    vertTris[a].push(t); vertTris[b].push(t); vertTris[c].push(t);
    vertAdj[a].add(b); vertAdj[a].add(c);
    vertAdj[b].add(a); vertAdj[b].add(c);
    vertAdj[c].add(a); vertAdj[c].add(b);
  }

  // Helper: evaluate exact 1-ring surface error + minimum triangle angle for vertex `vi` at candidate (cx, cy, cz)
  const evalCandidate = (vi, cx, cy, cz, refNx, refNy, refNz) => {
    const inc = vertTris[vi];
    const ox = pos[vi * 3], oy = pos[vi * 3 + 1], oz = pos[vi * 3 + 2];
    // Reject normal inversions or severe degenerate flips
    let minAng = Math.PI;
    pos[vi * 3] = cx; pos[vi * 3 + 1] = cy; pos[vi * 3 + 2] = cz;
    let valid = true;
    for (let k = 0; k < inc.length; ++k) {
      const t = inc[k];
      const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
      const pa = [pos[a * 3], pos[a * 3 + 1], pos[a * 3 + 2]];
      const pb = [pos[b * 3], pos[b * 3 + 1], pos[b * 3 + 2]];
      const pc = [pos[c * 3], pos[c * 3 + 1], pos[c * 3 + 2]];
      const e1 = [pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]];
      const e2 = [pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]];
      const cr = [
        e1[1] * e2[2] - e1[2] * e2[1],
        e1[2] * e2[0] - e1[0] * e2[2],
        e1[0] * e2[1] - e1[1] * e2[0],
      ];
      const area2 = Math.hypot(cr[0], cr[1], cr[2]);
      if (area2 < 1e-8) { valid = false; break; }
      const ang = computeTriMinAngleRad(pa, pb, pc);
      if (ang < minAng) minAng = ang;
    }
    if (!valid) {
      pos[vi * 3] = ox; pos[vi * 3 + 1] = oy; pos[vi * 3 + 2] = oz;
      return { valid: false, score: Infinity, vDist: Infinity, centErr: Infinity, maxErr: Infinity, minAng: 0, proj: null };
    }

    const vProj = projectPointOntoOriginalSurface(swIndex, cx, cy, cz, refNx, refNy, refNz, shell[vi], lockDualWalls);
    let centSum = 0;
    let maxE = vProj.dist;
    for (let k = 0; k < inc.length; ++k) {
      const t = inc[k];
      const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
      const tx = (pos[a * 3] + pos[b * 3] + pos[c * 3]) / 3;
      const ty = (pos[a * 3 + 1] + pos[b * 3 + 1] + pos[c * 3 + 1]) / 3;
      const tz = (pos[a * 3 + 2] + pos[b * 3 + 2] + pos[c * 3 + 2]) / 3;
      const [tnx, tny, tnz] = computeTriNormal(pos, a, b, c);
      const cp = projectPointOntoOriginalSurface(swIndex, tx, ty, tz, tnx, tny, tnz, shell[vi], lockDualWalls);
      centSum += cp.dist;
      if (cp.dist > maxE) maxE = cp.dist;
    }
    pos[vi * 3] = ox; pos[vi * 3 + 1] = oy; pos[vi * 3 + 2] = oz;
    const centMean = centSum / Math.max(1, inc.length);
    // Surface fidelity objective matching true symmetric surface accuracy:
    // prioritize triangle-surface coverage (centMean) + worst-case spike (maxE) while softly bounding vertex drift!
    const score = vProj.dist * 0.22 + centMean * 1.35 + maxE * 0.35;
    return { valid: true, score, vDist: vProj.dist, centErr: centMean, maxErr: maxE, minAng, proj: vProj };
  };

  const passCount = runFull ? Math.max(2, shrinkwrapPasses) : 1;
  const smoothWeight = lodLevel >= 3 ? 0.22 : 0.14;

  for (let pass = 0; pass < passCount; ++pass) {
    for (let i = 0; i < vertCount; ++i) {
      if (!activeVert[i]) continue;
      const rank = guides.guideRank[i];
      const inc = vertTris[i];
      if (inc.length === 0) continue;

      const ox = pos[i * 3], oy = pos[i * 3 + 1], oz = pos[i * 3 + 2];
      const rnx = norm[i * 3], rny = norm[i * 3 + 1], rnz = norm[i * 3 + 2];

      // Check baseline (current QEM position)
      const baseEval = evalCandidate(i, ox, oy, oz, rnx, rny, rnz);
      if (!baseEval.valid) continue;

      // Locked corners/seams (rank >= 2) are only touched if coarse decimation caused severe drift (> 4mm off LOD 0)
      if (rank >= 2 && baseEval.vDist < 0.004) continue;

      const localSharp = (regionalAdaptive && sharpness) ? sharpness[i] : 0.45;
      const isFlatRetopoZone = regionalAdaptive && rank === 0 && localSharp < 0.28;

      // High-LOD fast path: skip sharp/mid vertices already tightly within 1.5mm of LOD 0;
      // flat Retopo zones still run tangential cleanup because flat faces have zero chord sag!
      if (!runFull && !isFlatRetopoZone && baseEval.vDist < 0.0015 && baseEval.maxErr < 0.0035) {
        continue;
      }

      // Check normal orientation of current 1-ring to prevent flipped triangles on candidate move
      const oldTriNorms = inc.map((t) => computeTriNormal(pos, tris[t * 3], tris[t * 3 + 1], tris[t * 3 + 2]));
      const preservesOrientation = (cx, cy, cz) => {
        pos[i * 3] = cx; pos[i * 3 + 1] = cy; pos[i * 3 + 2] = cz;
        let ok = true;
        for (let k = 0; k < inc.length; ++k) {
          const t = inc[k];
          const nn = computeTriNormal(pos, tris[t * 3], tris[t * 3 + 1], tris[t * 3 + 2]);
          if (nn[0] * oldTriNorms[k][0] + nn[1] * oldTriNorms[k][1] + nn[2] * oldTriNorms[k][2] < 0.25) {
            ok = false; break;
          }
        }
        pos[i * 3] = ox; pos[i * 3 + 1] = oy; pos[i * 3 + 2] = oz;
        return ok;
      };

      const candidates = [];
      const p0 = baseEval.proj;
      if (p0) {
        if (swMode === "strictLow" && lodLevel >= 2) {
          // Strict surface-glued shrinkwrap at coarse LODs (vertices sit directly on LOD 0 — ideal for re-baking!)
          if (preservesOrientation(p0.x, p0.y, p0.z)) {
            pos[i * 3] = p0.x; pos[i * 3 + 1] = p0.y; pos[i * 3 + 2] = p0.z;
            if (rank === 0) { norm[i * 3] = p0.nx; norm[i * 3 + 1] = p0.ny; norm[i * 3 + 2] = p0.nz; }
          }
          continue;
        }
        // Candidate 1: Exact 100% Shrinkwrap snap onto LOD 0 surface
        candidates.push([p0.x, p0.y, p0.z]);
        // Candidate 2: 75% Shrinkwrap pull onto LOD 0
        candidates.push([ox * 0.25 + p0.x * 0.75, oy * 0.25 + p0.y * 0.75, oz * 0.25 + p0.z * 0.75]);
        // Candidate 3: 50% Shrinkwrap blend (optimal curvature straddling)
        candidates.push([(ox + p0.x) * 0.5, (oy + p0.y) * 0.5, (oz + p0.z) * 0.5]);

        // Candidate 4 (Region-aware Retopo tangential equalize + Shrinkwrap):
        // - Flat/gentle zones -> full Retopo-clean tangential flow (even at LOD1!)
        // - Sharp/high-curvature zones -> skipped (localSmooth = 0) to keep Nanite-crisp profiles!
        const localSmooth = regionalAdaptive
          ? (localSharp > 0.65 ? 0 : (isFlatRetopoZone ? 0.34 : smoothWeight * (1 - localSharp)))
          : (runFull ? smoothWeight : 0);
        if ((runFull || isFlatRetopoZone) && rank === 0 && localSmooth > 0.02) {
          const adj = vertAdj[i];
          if (adj.size >= 3) {
            let ax = 0, ay = 0, az = 0;
            for (const nb of adj) { ax += pos[nb * 3]; ay += pos[nb * 3 + 1]; az += pos[nb * 3 + 2]; }
            ax /= adj.size; ay /= adj.size; az /= adj.size;
            const dx = ax - p0.x, dy = ay - p0.y, dz = az - p0.z;
            const dn = dx * p0.nx + dy * p0.ny + dz * p0.nz;
            const tx = p0.x + (dx - dn * p0.nx) * localSmooth;
            const ty = p0.y + (dy - dn * p0.ny) * localSmooth;
            const tz = p0.z + (dz - dn * p0.nz) * localSmooth;
            const tp = projectPointOntoOriginalSurface(swIndex, tx, ty, tz, p0.nx, p0.ny, p0.nz, shell[i], lockDualWalls);
            candidates.push([tp.x, tp.y, tp.z]);
            candidates.push([(ox + tp.x) * 0.5, (oy + tp.y) * 0.5, (oz + tp.z) * 0.5]);
            const offN = (ox - p0.x) * p0.nx + (oy - p0.y) * p0.ny + (oz - p0.z) * p0.nz;
            if (Math.abs(offN) > 1e-5) {
              candidates.push([tp.x + tp.nx * offN, tp.y + tp.ny * offN, tp.z + tp.nz * offN]);
            }
          }
        }
      }

      let bestPos = [ox, oy, oz];
      let bestEval = baseEval;
      let moved = false;
      for (let c = 0; c < candidates.length; ++c) {
        const [cx, cy, cz] = candidates[c];
        if (!preservesOrientation(cx, cy, cz)) continue;
        const ev = evalCandidate(i, cx, cy, cz, rnx, rny, rnz);
        if (!ev.valid) continue;
        // Strict monotonic acceptance: flat Retopo zones only trade angle regularity when surface error stays <= baseline!
        const angleScale = isFlatRetopoZone ? 0.0007 : 0.0003;
        const angleBonus = (ev.minAng - bestEval.minAng) * angleScale;
        if (ev.score - angleBonus < bestEval.score - 2e-5 &&
            ev.score <= baseEval.score + 0.00015 &&
            ev.minAng >= Math.min(bestEval.minAng * 0.92, 0.22)) {
          bestPos = [cx, cy, cz];
          bestEval = ev;
          moved = true;
        }
      }

      if (moved) {
        pos[i * 3] = bestPos[0]; pos[i * 3 + 1] = bestPos[1]; pos[i * 3 + 2] = bestPos[2];
        if (rank === 0 && bestEval.proj) {
          norm[i * 3] = bestEval.proj.nx; norm[i * 3 + 1] = bestEval.proj.ny; norm[i * 3 + 2] = bestEval.proj.nz;
        }
      }
    }
  }
}

function buildRetopoSeededCoarseLod(baseWelded, swIndex, targetTris, policy, metrics, lodLevel) {
  // Two-Stage Coarse LOD 3 Architecture:
  // Stage 1 (Retopo + Shrinkwrap First): Build a clean, scar-free, valence-6 isotropic Retopo + Shrinkwrap seed
  //         (~28% tris) using curvature-modulated relaxation so flat/smooth panels form regular equilateral grids
  //         while bevels, spouts & sharp rims stay crisp.
  // Stage 2 (Hybrid QEM on Top): Apply LOD 0-Anchored Spatially-Adaptive Probabilistic QEM + Hu et al. (2016)
  //         Progressive Hausdorff Envelope + Quality/Valence Flips + Error-Guarded Shrinkwrap down to the exact
  //         LOD 3 triangle budget (12%) without forced decimation scars or surface spikes.
  const seedPolicy = {
    ...policy,
    lockUvSeams: false, // Release interior UV seam pin-locks so Retopo seed flows cleanly!
  };
  const work = buildRetopoWorkingMesh(baseWelded, swIndex, 0.28, seedPolicy, true);
  const { pos, norm, uv, shell, uvSeam, tris, triAlive } = work;

  let alive = 0;
  for (let t = 0; t < triAlive.length; ++t) if (triAlive[t]) alive++;

  // At coarse LOD 3 (12%), 8-12 sided curved tubes naturally have 30-45 deg dihedral angles between facets;
  // raise Stage 2 crease threshold so smooth curved tubes (TorusKnot, stem, spout) do not freeze as false 0D corners.
  const coarseCreaseDeg = Math.max(50, (policy.creaseAngleDeg ?? 32) + 18);
  const coarseCreaseCos = Math.cos((coarseCreaseDeg * Math.PI) / 180);
  const coarseQemPolicy = {
    ...policy,
    lockUvSeams: false,
    creaseAngleDeg: coarseCreaseDeg,
    anchorLod0: true,
    probQemSigma: Math.max(0.042, (policy.probQemSigma ?? 0.035) * 1.25),
    minAngleGuardRad: 0.20,
  };

  const bMin = policy.boundsMin || [-1, -1, -1];
  const bMax = policy.boundsMax || [1, 1, 1];
  const meshDiag = Math.max(0.25, Math.hypot(bMax[0] - bMin[0], bMax[1] - bMin[1], bMax[2] - bMin[2]));
  const baseEnvelope = meshDiag * 0.016;

  const stages = 3;
  const startAlive = alive;
  for (let st = 0; st < stages && alive > targetTris; ++st) {
    const stageTarget = Math.max(targetTris, Math.round(startAlive - ((startAlive - targetTris) * (st + 1)) / stages));
    const stagePolicy = {
      ...coarseQemPolicy,
      // Progressive Hausdorff Envelope (Hu et al. 2016): strict on early collapses, gently widens only if needed to hit target
      maxCollapseError: baseEnvelope * (st === 0 ? 0.85 : st === 1 ? 1.05 : 1.35),
    };
    for (let sub = 0; sub < 6 && alive > stageTarget; ++sub) {
      const prev = alive;
      const guides = classifyFeatureGuides(pos, uv, uvSeam, tris, triAlive, stagePolicy);
      const regionField = computeLocalRegionSharpness(pos, tris, triAlive, guides);
      alive = runHybridProbQemStep(pos, norm, uv, shell, uvSeam, tris, triAlive, stageTarget, stagePolicy, swIndex, guides, regionField);
      if (alive >= prev - 1) break;
    }
    const midGuides = classifyFeatureGuides(pos, uv, uvSeam, tris, triAlive, coarseQemPolicy);
    flipQualityEdges(pos, tris, triAlive, pos.length / 3, midGuides, Math.max(0.74, coarseCreaseCos));
    flipValenceEdges(pos, tris, triAlive, pos.length / 3, midGuides, Math.max(0.74, coarseCreaseCos));
  }

  // Final quality polish + strict Error-Guarded Shrinkwrap onto exact LOD 0 surface
  const postGuides = classifyFeatureGuides(pos, uv, uvSeam, tris, triAlive, coarseQemPolicy);
  const postRegion = computeLocalRegionSharpness(pos, tris, triAlive, postGuides);
  flipQualityEdges(pos, tris, triAlive, pos.length / 3, postGuides, Math.max(0.76, coarseCreaseCos));
  applyAdaptiveHybridShrinkwrap(pos, norm, shell, tris, triAlive, swIndex, postGuides, policy, lodLevel, postRegion);
  flipQualityEdges(pos, tris, triAlive, pos.length / 3, postGuides, Math.max(0.78, coarseCreaseCos));
  const finalized = finalizeCompactLodMesh(pos, norm, uv, shell, uvSeam, tris, triAlive, swIndex, policy, metrics, false);
  finalized.regionMix = { sharpPct: postRegion.sharpPct, midPct: postRegion.midPct, flatPct: postRegion.flatPct };
  finalized.retopoSeeded = true;
  return finalized;
}

function buildHybridProbQemRetopoChain(baseWelded, swIndex, lodRatios, policy) {
  // Unified Frontier Solver:
  // - LOD 1-2: Memoryless Spatially-Adaptive Probabilistic QEM + Unbiased Qgeom + Quality Flips + Error-Guarded Shrinkwrap
  // - LOD 3  : Retopo + Shrinkwrap Seed -> Interleaved Probabilistic QEM + Flips + Error-Guarded Shrinkwrap (scar-free!)
  const pos = baseWelded.pos.slice();
  const norm = baseWelded.norm.slice();
  const uv = baseWelded.uv.slice();
  const shell = baseWelded.shell.slice();
  const uvSeam = baseWelded.uvSeam.slice();
  const tris = baseWelded.tris.slice();
  const triAlive = new Array(tris.length / 3).fill(1);
  const sourceTris = tris.length / 3;
  const creaseCos = Math.cos(((policy.creaseAngleDeg ?? 32) * Math.PI) / 180);
  const coarseRetopoSeed = policy.coarseRetopoSeed ?? true;
  const metrics = { wallCollapseCount: 0, groupLockedVerts: 0, breachVerts: new Set() };

  const chain = [];
  for (let lvl = 0; lvl < lodRatios.length; ++lvl) {
    const ratio = lodRatios[lvl];
    const targetTris = Math.max(24, Math.round(sourceTris * ratio));
    if (lvl >= 3 && coarseRetopoSeed) {
      chain.push(buildRetopoSeededCoarseLod(baseWelded, swIndex, targetTris, policy, metrics, lvl));
      continue;
    }
    if (lvl > 0) {
      let alive = sourceTris;
      for (let sub = 0; sub < 4 && alive > targetTris; ++sub) {
        const prev = alive;
        const guides = classifyFeatureGuides(pos, uv, uvSeam, tris, triAlive, policy);
        const regionField = computeLocalRegionSharpness(pos, tris, triAlive, guides);
        alive = runHybridProbQemStep(pos, norm, uv, shell, uvSeam, tris, triAlive, targetTris, policy, swIndex, guides, regionField);
        if (alive >= prev - 2) break;
      }
      // Keep the core Spatially-Adaptive QEM simplification chain unperturbed across levels;
      // run post-pass Quality Flips + Region-Aware Error-Guarded Shrinkwrap cleanly on this LOD's output snapshot!
      const lodPos = pos.slice();
      const lodNorm = norm.slice();
      const lodTris = tris.slice();
      const lodTriAlive = triAlive.slice();
      const postGuides = classifyFeatureGuides(lodPos, uv, uvSeam, lodTris, lodTriAlive, policy);
      const postRegion = computeLocalRegionSharpness(lodPos, lodTris, lodTriAlive, postGuides);
      flipQualityEdges(lodPos, lodTris, lodTriAlive, lodPos.length / 3, postGuides, Math.max(0.84, creaseCos));
      applyAdaptiveHybridShrinkwrap(lodPos, lodNorm, shell, lodTris, lodTriAlive, swIndex, postGuides, policy, lvl, postRegion);
      const finalized = finalizeCompactLodMesh(lodPos, lodNorm, uv, shell, uvSeam, lodTris, lodTriAlive, swIndex, policy, metrics, false);
      finalized.regionMix = { sharpPct: postRegion.sharpPct, midPct: postRegion.midPct, flatPct: postRegion.flatPct };
      chain.push(finalized);
      continue;
    }
    const g0 = classifyFeatureGuides(pos, uv, uvSeam, tris, triAlive, policy);
    const r0 = computeLocalRegionSharpness(pos, tris, triAlive, g0);
    const finalized0 = finalizeCompactLodMesh(pos, norm, uv, shell, uvSeam, tris, triAlive, swIndex, policy, metrics, false);
    finalized0.regionMix = { sharpPct: r0.sharpPct, midPct: r0.midPct, flatPct: r0.flatPct };
    chain.push(finalized0);
  }
  return chain;
}

function evaluateQuadric(q00, q01, q02, q03, q11, q12, q13, q22, q23, q33, x, y, z) {
  return Math.max(
    0,
    q00 * x * x + 2 * q01 * x * y + 2 * q02 * x * z + 2 * q03 * x +
    q11 * y * y + 2 * q12 * y * z + 2 * q13 * y +
    q22 * z * z + 2 * q23 * z + q33,
  );
}

function solveOptimalQemPlacement(Q, offA, offB, ax, ay, az, bx, by, bz) {
  const q00 = Q[offA + 0] + Q[offB + 0];
  const q01 = Q[offA + 1] + Q[offB + 1];
  const q02 = Q[offA + 2] + Q[offB + 2];
  const q03 = Q[offA + 3] + Q[offB + 3];
  const q11 = Q[offA + 4] + Q[offB + 4];
  const q12 = Q[offA + 5] + Q[offB + 5];
  const q13 = Q[offA + 6] + Q[offB + 6];
  const q22 = Q[offA + 7] + Q[offB + 7];
  const q23 = Q[offA + 8] + Q[offB + 8];
  const q33 = Q[offA + 9] + Q[offB + 9];

  // Solve symmetric 3x3 linear system A * v = -b via Cramer's rule
  const det =
    q00 * (q11 * q22 - q12 * q12) -
    q01 * (q01 * q22 - q12 * q02) +
    q02 * (q01 * q12 - q11 * q02);

  const mx = 0.5 * (ax + bx), my = 0.5 * (ay + by), mz = 0.5 * (az + bz);
  const edgeLen = Math.hypot(bx - ax, by - ay, bz - az) + 1e-6;

  if (Math.abs(det) > 1e-9) {
    const detX =
      (-q03) * (q11 * q22 - q12 * q12) -
      q01 * ((-q13) * q22 - q12 * (-q23)) +
      q02 * ((-q13) * q12 - q11 * (-q23));
    const detY =
      q00 * ((-q13) * q22 - q12 * (-q23)) -
      (-q03) * (q01 * q22 - q12 * q02) +
      q02 * (q01 * (-q23) - (-q13) * q02);
    const detZ =
      q00 * (q11 * (-q23) - (-q13) * q12) -
      q01 * (q01 * (-q23) - (-q13) * q02) +
      (-q03) * (q01 * q12 - q11 * q02);
    const vx = detX / det, vy = detY / det, vz = detZ / det;
    // Clamp runaway ill-conditioned intersections back to candidate set
    if (Math.hypot(vx - mx, vy - my, vz - mz) <= edgeLen * 1.35) {
      const err = evaluateQuadric(q00, q01, q02, q03, q11, q12, q13, q22, q23, q33, vx, vy, vz);
      return { x: vx, y: vy, z: vz, err };
    }
  }

  // Fallback: minimum QEM cost among endpoint A, endpoint B, and midpoint
  const ea = evaluateQuadric(q00, q01, q02, q03, q11, q12, q13, q22, q23, q33, ax, ay, az);
  const eb = evaluateQuadric(q00, q01, q02, q03, q11, q12, q13, q22, q23, q33, bx, by, bz);
  const em = evaluateQuadric(q00, q01, q02, q03, q11, q12, q13, q22, q23, q33, mx, my, mz);
  if (em <= ea && em <= eb) return { x: mx, y: my, z: mz, err: em };
  if (ea <= eb) return { x: ax, y: ay, z: az, err: ea };
  return { x: bx, y: by, z: bz, err: eb };
}

function runNaniteGroupLockedQemStep(pos, norm, uv, shell, uvSeam, tris, triAlive, targetTris, policy, metrics, swIndex = null) {
  const { maxTrisPerCluster, boundsMin, boundsMax } = policy;
  const vertCount = pos.length / 3;
  const triCount = triAlive.length;

  // Step 1: Partition alive triangles into <=maxTrisPerCluster Morton clusters, then group into 4-cluster groups (Nanite DAG!)
  const activeTris = [];
  for (let t = 0; t < triCount; ++t) {
    if (!triAlive[t]) continue;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    const cx = (pos[a * 3] + pos[b * 3] + pos[c * 3]) / 3;
    const cy = (pos[a * 3 + 1] + pos[b * 3 + 1] + pos[c * 3 + 1]) / 3;
    const cz = (pos[a * 3 + 2] + pos[b * 3 + 2] + pos[c * 3 + 2]) / 3;
    activeTris.push({ t, score: spatialMortonScore(cx, cy, cz, boundsMin, boundsMax) });
  }
  activeTris.sort((x, y) => x.score - y.score);

  const triGroupId = new Int32Array(triCount).fill(-1);
  const clusterSize = Math.max(32, Math.min(128, maxTrisPerCluster));
  const clusterCount = Math.ceil(activeTris.length / clusterSize);
  const clustersPerGroup = clusterCount >= 6 ? 4 : 2; // Nanite uses 2-4 adjacent clusters per simplification group
  for (let k = 0; k < activeTris.length; ++k) {
    const clusterId = Math.floor(k / clusterSize);
    triGroupId[activeTris[k].t] = Math.floor(clusterId / clustersPerGroup);
  }

  // Step 2: Detect STRICTLY LOCKED outer group boundary vertices (crack-free runtime LOD transitions!)
  const vertGroupSeen = new Int32Array(vertCount).fill(-2);
  const groupLockedVert = new Uint8Array(vertCount);
  let groupLockedCount = 0;
  for (let k = 0; k < activeTris.length; ++k) {
    const t = activeTris[k].t;
    const g = triGroupId[t];
    for (let c = 0; c < 3; ++c) {
      const vi = tris[t * 3 + c];
      if (vertGroupSeen[vi] === -2) vertGroupSeen[vi] = g;
      else if (vertGroupSeen[vi] !== g && !groupLockedVert[vi]) {
        groupLockedVert[vi] = 1;
        groupLockedCount++;
      }
    }
  }
  metrics.groupLockedVerts = groupLockedCount;

  // Step 3: Accumulate real Garland-Heckbert 4x4 Fundamental Plane Quadrics + boundary preservation planes
  const Q = new Float64Array(vertCount * 10);
  const edgeTris = new Map();
  const vertTris = Array.from({ length: vertCount }, () => []);

  for (let k = 0; k < activeTris.length; ++k) {
    const t = activeTris[k].t;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    const [nx, ny, nz, area] = computeTriNormal(pos, a, b, c);
    if (area < 1e-10) continue;
    const d = -(nx * pos[a * 3] + ny * pos[a * 3 + 1] + nz * pos[a * 3 + 2]);
    const w = Math.max(area, 1e-5);
    addPlaneQuadric(Q, a * 10, nx, ny, nz, d, w);
    addPlaneQuadric(Q, b * 10, nx, ny, nz, d, w);
    addPlaneQuadric(Q, c * 10, nx, ny, nz, d, w);
    vertTris[a].push(t); vertTris[b].push(t); vertTris[c].push(t);
    for (const [u, v] of [[a, b], [b, c], [c, a]]) {
      const ek = edgeKey(u, v);
      let list = edgeTris.get(ek);
      if (!list) { list = []; edgeTris.set(ek, list); }
      list.push(t);
    }
  }

  // Add perpendicular boundary penalty quadrics for open edges (Garland-Heckbert §5)
  for (const [ek, list] of edgeTris.entries()) {
    if (list.length !== 1) continue;
    const sep = ek.indexOf("_");
    const u = Number(ek.slice(0, sep)), v = Number(ek.slice(sep + 1));
    const t = list[0];
    const [tnx, tny, tnz] = computeTriNormal(pos, tris[t * 3], tris[t * 3 + 1], tris[t * 3 + 2]);
    const eVec = [pos[v * 3] - pos[u * 3], pos[v * 3 + 1] - pos[u * 3 + 1], pos[v * 3 + 2] - pos[u * 3 + 2]];
    const bn = vNorm(vCross(eVec, [tnx, tny, tnz]));
    const bd = -(bn[0] * pos[u * 3] + bn[1] * pos[u * 3 + 1] + bn[2] * pos[u * 3 + 2]);
    const bw = vLen(eVec) * 8.0;
    addPlaneQuadric(Q, u * 10, bn[0], bn[1], bn[2], bd, bw);
    addPlaneQuadric(Q, v * 10, bn[0], bn[1], bn[2], bd, bw);
  }

  // Step 4: Score all interior (non-group-boundary) edges by true QEM error & collapse lowest-error first!
  const candidates = [];
  for (const ek of edgeTris.keys()) {
    const sep = ek.indexOf("_");
    const u = Number(ek.slice(0, sep)), v = Number(ek.slice(sep + 1));
    if (groupLockedVert[u] && groupLockedVert[v]) continue; // Locked Nanite group seam!
    const sol = solveOptimalQemPlacement(Q, u * 10, v * 10, pos[u * 3], pos[u * 3 + 1], pos[u * 3 + 2], pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]);
    // Slight cost bias if one vertex touches a group boundary (must pin onto locked boundary vertex)
    const err = sol.err * (groupLockedVert[u] || groupLockedVert[v] ? 1.5 : 1.0);
    candidates.push({ u, v, err, x: sol.x, y: sol.y, z: sol.z });
  }
  candidates.sort((c1, c2) => c1.err - c2.err);

  let aliveCount = activeTris.length;
  const rootVert = new Int32Array(vertCount);
  for (let i = 0; i < vertCount; ++i) rootVert[i] = i;
  const findV = (v) => {
    while (rootVert[v] !== v) {
      rootVert[v] = rootVert[rootVert[v]];
      v = rootVert[v];
    }
    return v;
  };
  const collapsed = new Uint8Array(vertCount);

  for (let ci = 0; ci < candidates.length && aliveCount > targetTris; ++ci) {
    let a = findV(candidates[ci].u);
    let b = findV(candidates[ci].v);
    if (a === b || collapsed[a] || collapsed[b]) continue;
    if (groupLockedVert[a] && groupLockedVert[b]) continue;

    // Pin strictly onto locked group boundary vertex if one side is on the 4-cluster perimeter
    let tx = candidates[ci].x, ty = candidates[ci].y, tz = candidates[ci].z;
    if (groupLockedVert[a]) { tx = pos[a * 3]; ty = pos[a * 3 + 1]; tz = pos[a * 3 + 2]; }
    else if (groupLockedVert[b]) { tx = pos[b * 3]; ty = pos[b * 3 + 1]; tz = pos[b * 3 + 2]; }

    // Standard Nanite QEM refuses inverted triangles (fold-over check)
    const incA = vertTris[a], incB = vertTris[b];
    let foldOver = false;
    const checkInc = (list) => {
      for (let k = 0; k < list.length; ++k) {
        const t = list[k];
        if (!triAlive[t]) continue;
        const v0 = findV(tris[t * 3]), v1 = findV(tris[t * 3 + 1]), v2 = findV(tris[t * 3 + 2]);
        if ((v0 === a || v1 === a || v2 === a) && (v0 === b || v1 === b || v2 === b)) continue;
        const nOld = computeTriNormal(pos, v0, v1, v2);
        const p0 = (v0 === a || v0 === b) ? [tx, ty, tz] : [pos[v0 * 3], pos[v0 * 3 + 1], pos[v0 * 3 + 2]];
        const p1 = (v1 === a || v1 === b) ? [tx, ty, tz] : [pos[v1 * 3], pos[v1 * 3 + 1], pos[v1 * 3 + 2]];
        const p2 = (v2 === a || v2 === b) ? [tx, ty, tz] : [pos[v2 * 3], pos[v2 * 3 + 1], pos[v2 * 3 + 2]];
        const cr = vCross(vSub(p1, p0), vSub(p2, p0));
        const cl = vLen(cr);
        if (cl < 1e-8 || (cr[0] * nOld[0] + cr[1] * nOld[1] + cr[2] * nOld[2]) / cl < 0.20) {
          foldOver = true;
          return;
        }
      }
    };
    checkInc(incA);
    if (!foldOver) checkInc(incB);
    if (foldOver) continue;

    // Track honest thin-wall opposite-shell collapse if standard Nanite QEM merges inner & outer glass shells
    if (shell[a] !== 0 && shell[b] !== 0 && shell[a] !== shell[b]) {
      metrics.wallCollapseCount++;
      metrics.breachVerts.add(a);
    }

    pos[a * 3] = tx; pos[a * 3 + 1] = ty; pos[a * 3 + 2] = tz;
    for (let q = 0; q < 10; ++q) Q[a * 10 + q] += Q[b * 10 + q];
    groupLockedVert[a] = groupLockedVert[a] || groupLockedVert[b] ? 1 : 0;
    uvSeam[a] = uvSeam[a] || uvSeam[b] ? 1 : 0;
    rootVert[b] = a;
    collapsed[a] = 1;
    collapsed[b] = 1;

    for (const list of [incB, incA]) {
      for (let k = 0; k < list.length; ++k) {
        const t = list[k];
        if (!triAlive[t]) continue;
        const v0 = findV(tris[t * 3]), v1 = findV(tris[t * 3 + 1]), v2 = findV(tris[t * 3 + 2]);
        tris[t * 3] = v0; tris[t * 3 + 1] = v1; tris[t * 3 + 2] = v2;
        if (v0 === v1 || v1 === v2 || v2 === v0) {
          triAlive[t] = 0;
          aliveCount--;
        } else if (list === incB) {
          incA.push(t);
        }
      }
    }
  }

  for (let t = 0; t < triAlive.length; ++t) {
    if (!triAlive[t]) continue;
    const v0 = findV(tris[t * 3]), v1 = findV(tris[t * 3 + 1]), v2 = findV(tris[t * 3 + 2]);
    tris[t * 3] = v0; tris[t * 3 + 1] = v1; tris[t * 3 + 2] = v2;
    if (v0 === v1 || v1 === v2 || v2 === v0) {
      triAlive[t] = 0;
      aliveCount--;
    }
  }

  // Optional Crack-Free Interior Shrinkwrap (Brian Karis SIGGRAPH 2021 boundary invariant):
  // Reproject ONLY unlocked interior vertices of each cluster group; outer group seams stay 100% locked!
  if (policy.naniteShrinkwrap && swIndex) {
    const aliveVert = new Uint8Array(vertCount);
    for (let t = 0; t < triAlive.length; ++t) {
      if (!triAlive[t]) continue;
      aliveVert[tris[t * 3]] = 1;
      aliveVert[tris[t * 3 + 1]] = 1;
      aliveVert[tris[t * 3 + 2]] = 1;
    }
    for (let v = 0; v < vertCount; ++v) {
      if (!aliveVert[v] || groupLockedVert[v]) continue; // Never move outer cluster-group boundary!
      const proj = projectPointOntoOriginalSurface(
        swIndex,
        pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2],
        norm[v * 3], norm[v * 3 + 1], norm[v * 3 + 2],
        shell[v], false,
      );
      if (proj.dist < 0.035) {
        // Guard against snapping across thin opposite walls (e.g., double-walled glass)
        const nAlign = norm[v * 3] * proj.nx + norm[v * 3 + 1] * proj.ny + norm[v * 3 + 2] * proj.nz;
        if (nAlign > 0.45) {
          pos[v * 3] = proj.x;
          pos[v * 3 + 1] = proj.y;
          pos[v * 3 + 2] = proj.z;
          norm[v * 3] = proj.nx;
          norm[v * 3 + 1] = proj.ny;
          norm[v * 3 + 2] = proj.nz;
        }
      }
    }
  }
  return aliveCount;
}

function finalizeCompactLodMesh(pos, norm, uv, shell, uvSeam, tris, triAlive, swIndex, policy, metrics, useGeomNormals = false) {
  const { lockDualWalls, maxTrisPerCluster, sourceTriCount, boundsMin, boundsMax } = policy;
  const guides = classifyFeatureGuides(pos, uv, uvSeam, tris, triAlive, policy);
  const vertCount = pos.length / 3;
  const vertRemap = new Int32Array(vertCount).fill(-1);
  const outPos = [];
  const outNorm = [];
  const outUv = [];
  const outShell = [];
  const outUvSeam = [];
  const outGuide = [];
  const outBreach = [];
  const outIndices = [];

  for (let t = 0; t < triAlive.length; ++t) {
    if (!triAlive[t]) continue;
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    if (a === b || b === c || c === a) continue;
    const triOut = [];
    for (const vi of [a, b, c]) {
      let nv = vertRemap[vi];
      if (nv < 0) {
        nv = outPos.length / 3;
        vertRemap[vi] = nv;
        outPos.push(pos[vi * 3], pos[vi * 3 + 1], pos[vi * 3 + 2]);
        outNorm.push(norm[vi * 3], norm[vi * 3 + 1], norm[vi * 3 + 2]);
        outUv.push(uv[vi * 2], uv[vi * 2 + 1]);
        outShell.push(shell[vi]);
        outUvSeam.push(uvSeam[vi]);
        outGuide.push(guides.guideRank[vi] > 0 ? 1 : 0);
        outBreach.push(metrics.breachVerts.has(vi) ? 1 : 0);
      }
      triOut.push(nv);
    }
    outIndices.push(triOut[0], triOut[1], triOut[2]);
  }

  const fVertCount = outPos.length / 3;
  const fTriCount = outIndices.length / 3;
  const posArr = new Float32Array(outPos);
  const normArr = new Float32Array(outNorm);
  const idxArr = new Uint32Array(outIndices);
  const guideMask = new Uint8Array(outGuide);
  const breachMask = new Uint8Array(outBreach);
  const thinWallMask = new Uint8Array(fVertCount);

  const geomNorms = new Float32Array(fVertCount * 3);
  for (let t = 0; t < fTriCount; ++t) {
    const i0 = idxArr[t * 3], i1 = idxArr[t * 3 + 1], i2 = idxArr[t * 3 + 2];
    const [nx, ny, nz, area] = computeTriNormal(posArr, i0, i1, i2);
    const w = Math.max(area, 1e-6);
    for (const vi of [i0, i1, i2]) {
      geomNorms[vi * 3] += nx * w;
      geomNorms[vi * 3 + 1] += ny * w;
      geomNorms[vi * 3 + 2] += nz * w;
    }
  }
  for (let i = 0; i < fVertCount; ++i) {
    const l = Math.hypot(geomNorms[i * 3], geomNorms[i * 3 + 1], geomNorms[i * 3 + 2]) || 1;
    geomNorms[i * 3] /= l; geomNorms[i * 3 + 1] /= l; geomNorms[i * 3 + 2] /= l;
    if (useGeomNormals) {
      normArr[i * 3] = geomNorms[i * 3];
      normArr[i * 3 + 1] = geomNorms[i * 3 + 1];
      normArr[i * 3 + 2] = geomNorms[i * 3 + 2];
    }
  }

  // Symmetric two-sided Hausdorff surface accuracy & normal deviation against original LOD 0 surface
  let sumErr = 0, maxErr = 0, maxNormDev = 0;
  let sampleCount = 0;
  for (let i = 0; i < fVertCount; ++i) {
    const proj = projectPointOntoOriginalSurface(
      swIndex,
      posArr[i * 3], posArr[i * 3 + 1], posArr[i * 3 + 2],
      normArr[i * 3], normArr[i * 3 + 1], normArr[i * 3 + 2],
      outShell[i],
      !useGeomNormals && lockDualWalls,
    );
    sumErr += proj.dist;
    if (proj.dist > maxErr) maxErr = proj.dist;
    const nd = clamp(normArr[i * 3] * proj.nx + normArr[i * 3 + 1] * proj.ny + normArr[i * 3 + 2] * proj.nz, -1, 1);
    const deg = Math.acos(nd) * (180 / Math.PI);
    if (deg > maxNormDev) maxNormDev = deg;
    if (!useGeomNormals && lockDualWalls && outShell[i] !== 0) thinWallMask[i] = 1;
    sampleCount++;
  }
  const triStep = Math.max(1, Math.floor(fTriCount / 300));
  for (let t = 0; t < fTriCount; t += triStep) {
    const i0 = idxArr[t * 3], i1 = idxArr[t * 3 + 1], i2 = idxArr[t * 3 + 2];
    const cx = (posArr[i0 * 3] + posArr[i1 * 3] + posArr[i2 * 3]) / 3;
    const cy = (posArr[i0 * 3 + 1] + posArr[i1 * 3 + 1] + posArr[i2 * 3 + 1]) / 3;
    const cz = (posArr[i0 * 3 + 2] + posArr[i1 * 3 + 2] + posArr[i2 * 3 + 2]) / 3;
    const [tnx, tny, tnz] = computeTriNormal(posArr, i0, i1, i2);
    const proj = projectPointOntoOriginalSurface(swIndex, cx, cy, cz, tnx, tny, tnz, outShell[i0], !useGeomNormals && lockDualWalls);
    sumErr += proj.dist;
    if (proj.dist > maxErr) maxErr = proj.dist;
    sampleCount++;
  }
  // Reverse Hausdorff: TRUE point-to-triangle distance from LOD 0 surface to decimated LOD triangles (zero flat-plane bias!)
  const origVertCount = swIndex.positions.length / 3;
  const revStep = Math.max(1, Math.floor(origVertCount / 160));
  for (let ov = 0; ov < origVertCount; ov += revStep) {
    const ox = swIndex.positions[ov * 3], oy = swIndex.positions[ov * 3 + 1], oz = swIndex.positions[ov * 3 + 2];
    let bestD2 = Infinity;
    for (let t = 0; t < fTriCount; ++t) {
      const a = idxArr[t * 3] * 3, b = idxArr[t * 3 + 1] * 3, c = idxArr[t * 3 + 2] * 3;
      const res = closestPointOnTriangleBary(
        ox, oy, oz,
        posArr[a], posArr[a + 1], posArr[a + 2],
        posArr[b], posArr[b + 1], posArr[b + 2],
        posArr[c], posArr[c + 1], posArr[c + 2],
      );
      if (res[6] < bestD2) bestD2 = res[6];
    }
    const d = Math.sqrt(bestD2);
    sumErr += d;
    if (d > maxErr) maxErr = d;
    sampleCount++;
  }

  // Partition into spatially coherent <=maxTrisPerCluster GPU clusters
  const triOrder = new Array(fTriCount);
  for (let t = 0; t < fTriCount; ++t) {
    const i0 = idxArr[t * 3], i1 = idxArr[t * 3 + 1], i2 = idxArr[t * 3 + 2];
    const cx = (posArr[i0 * 3] + posArr[i1 * 3] + posArr[i2 * 3]) / 3;
    const cy = (posArr[i0 * 3 + 1] + posArr[i1 * 3 + 1] + posArr[i2 * 3 + 1]) / 3;
    const cz = (posArr[i0 * 3 + 2] + posArr[i1 * 3 + 2] + posArr[i2 * 3 + 2]) / 3;
    const shBias = (!useGeomNormals && lockDualWalls && outShell[i0] < 0) ? 0x40000000 : 0;
    triOrder[t] = { t, score: shBias + spatialMortonScore(cx, cy, cz, boundsMin, boundsMax) };
  }
  triOrder.sort((a, b) => a.score - b.score);

  const triClusterId = new Int32Array(fTriCount);
  const clusters = [];
  for (let start = 0; start < fTriCount; start += maxTrisPerCluster) {
    const end = Math.min(fTriCount, start + maxTrisPerCluster);
    const cid = clusters.length;
    const cTris = [];
    for (let k = start; k < end; ++k) {
      const ti = triOrder[k].t;
      triClusterId[ti] = cid;
      cTris.push(ti);
    }
    clusters.push({
      clusterId: cid,
      coarseGroupIndex: -1,
      nestedClusterIndices: [],
      triangleIndices: cTris,
      triangleCount: cTris.length,
      errorMeters: Number((sumErr / Math.max(1, sampleCount)).toFixed(5)),
    });
  }

  // Live triangle quality stats: average minimum triangle angle & sliver percentage (< 15 deg)
  let minAngDegSum = 0;
  let sliverTris = 0;
  for (let t = 0; t < fTriCount; ++t) {
    const i0 = idxArr[t * 3], i1 = idxArr[t * 3 + 1], i2 = idxArr[t * 3 + 2];
    const minDeg = computeTriMinAngleRad(
      [posArr[i0 * 3], posArr[i0 * 3 + 1], posArr[i0 * 3 + 2]],
      [posArr[i1 * 3], posArr[i1 * 3 + 1], posArr[i1 * 3 + 2]],
      [posArr[i2 * 3], posArr[i2 * 3 + 1], posArr[i2 * 3 + 2]],
    ) * (180 / Math.PI);
    minAngDegSum += minDeg;
    if (minDeg < 15.0) sliverTris++;
  }

  const meanErrMeters = sumErr / Math.max(1, sampleCount);
  return {
    positions: posArr,
    normals: normArr,
    uvs: new Float32Array(outUv),
    indices: idxArr,
    uvSeamMask: new Uint8Array(outUvSeam),
    guideMask,
    shellSide: new Int8Array(outShell),
    breachMask,
    thinWallLockedVerts: thinWallMask,
    triClusterId,
    clusters,
    clusterCount: clusters.length,
    maxClusterTriangles: clusters.length > 0 ? Math.max(...clusters.map((c) => c.triangleCount)) : 0,
    vertexCount: fVertCount,
    triangleCount: fTriCount,
    reductionPercent: Math.max(0, Math.round((1 - fTriCount / Math.max(1, sourceTriCount)) * 100)),
    meanErrorMm: Number((meanErrMeters * 1000).toFixed(2)),
    maxErrorMm: Number((maxErr * 1000).toFixed(2)),
    avgMinAngleDeg: Number((minAngDegSum / Math.max(1, fTriCount)).toFixed(1)),
    sliverPercent: Math.round((sliverTris / Math.max(1, fTriCount)) * 100),
    wallCollapseCount: metrics.wallCollapseCount,
    groupLockedVerts: metrics.groupLockedVerts || 0,
    guideEdgeCount: guides.totalGuideEdges,
    creaseEdgeCount: guides.creaseEdgeCount,
    seamEdgeCount: guides.seamEdgeCount,
    maxObservedNormalDevDeg: Number(maxNormDev.toFixed(1)),
  };
}

function buildRetopoWorkingMesh(baseWelded, swIndex, targetRatio, policy, useCurvatureRelax = false) {
  const pos = baseWelded.pos.slice();
  const norm = baseWelded.norm.slice();
  const uv = baseWelded.uv.slice();
  const shell = baseWelded.shell.slice();
  const uvSeam = baseWelded.uvSeam.slice();
  const tris = baseWelded.tris.slice();
  const triAlive = new Array(tris.length / 3).fill(1);
  const sourceTris = tris.length / 3;
  const targetTris = Math.max(24, Math.round(sourceTris * targetRatio));
  const shrinkwrapPasses = clamp(policy.shrinkwrapPasses ?? 2, 1, 4);

  if (targetRatio >= 0.99) {
    return { pos, norm, uv, shell, uvSeam, tris, triAlive };
  }

  // ZRemesher-Style Guide-Respecting Isotropic Retopology + Surface Shrinkwrap Reprojection!
  let totalArea = 0;
  for (let t = 0; t < triAlive.length; ++t) {
    totalArea += computeTriNormal(pos, tris[t * 3], tris[t * 3 + 1], tris[t * 3 + 2])[3];
  }
  const targetEdgeLen = Math.sqrt((4 * Math.max(totalArea, 1e-4)) / (Math.sqrt(3) * targetTris));
  const creaseCos = Math.cos(((policy.creaseAngleDeg ?? 32) * Math.PI) / 180);

  for (let iter = 0; iter < 3; ++iter) {
    const stepTarget = Math.max(targetTris, Math.round(sourceTris - ((sourceTris - targetTris) * (iter + 1)) / 3));
    let guides = classifyFeatureGuides(pos, uv, uvSeam, tris, triAlive, policy);
    runGuidedEdgeCollapsePass(pos, norm, uv, shell, uvSeam, tris, triAlive, stepTarget, policy, guides);
    if (iter < 2) {
      splitLongEdges(pos, norm, uv, shell, uvSeam, tris, triAlive, swIndex, targetEdgeLen * 1.45, policy.lockDualWalls);
    }
    guides = classifyFeatureGuides(pos, uv, uvSeam, tris, triAlive, policy);
    flipValenceEdges(pos, tris, triAlive, pos.length / 3, guides, Math.max(0.75, creaseCos));
    if (useCurvatureRelax) {
      flipQualityEdges(pos, tris, triAlive, pos.length / 3, guides, Math.max(0.75, creaseCos));
      const regionField = computeLocalRegionSharpness(pos, tris, triAlive, guides);
      relaxSurfaceShrinkwrap(
        pos, norm, shell, tris, triAlive, swIndex,
        iter === 2 ? shrinkwrapPasses : 1,
        policy.lockDualWalls, guides, 0.42, regionField.sharpness,
      );
    } else {
      relaxSurfaceShrinkwrap(pos, norm, shell, tris, triAlive, swIndex, iter === 2 ? shrinkwrapPasses : 1, policy.lockDualWalls, guides);
    }
  }
  return { pos, norm, uv, shell, uvSeam, tris, triAlive };
}

function buildRetopoLodMesh(baseWelded, swIndex, targetRatio, policy) {
  const metrics = { wallCollapseCount: 0, groupLockedVerts: 0, breachVerts: new Set() };
  const work = buildRetopoWorkingMesh(baseWelded, swIndex, targetRatio, policy);
  return finalizeCompactLodMesh(work.pos, work.norm, work.uv, work.shell, work.uvSeam, work.tris, work.triAlive, swIndex, policy, metrics, false);
}

function buildNaniteQemLodChain(baseWelded, swIndex, lodRatios, policy) {
  // Progressive hierarchical Nanite-style Cluster-Group QEM chain (outer 2-4 cluster seams strictly locked!)
  const pos = baseWelded.pos.slice();
  const norm = baseWelded.norm.slice();
  const uv = baseWelded.uv.slice();
  const shell = baseWelded.shell.slice();
  const uvSeam = baseWelded.uvSeam.slice();
  const tris = baseWelded.tris.slice();
  const triAlive = new Array(tris.length / 3).fill(1);
  const sourceTris = tris.length / 3;
  const metrics = { wallCollapseCount: 0, groupLockedVerts: 0, breachVerts: new Set() };

  const chain = [];
  for (let lvl = 0; lvl < lodRatios.length; ++lvl) {
    const ratio = lodRatios[lvl];
    const targetTris = Math.max(24, Math.round(sourceTris * ratio));
    if (lvl > 0) {
      let alive = targetTris + 1;
      for (let sub = 0; sub < 2 && alive > targetTris; ++sub) {
        alive = runNaniteGroupLockedQemStep(pos, norm, uv, shell, uvSeam, tris, triAlive, targetTris, policy, metrics, swIndex);
      }
    }
    chain.push(finalizeCompactLodMesh(pos, norm, uv, shell, uvSeam, tris, triAlive, swIndex, policy, metrics, true));
  }
  return chain;
}

export function compileClusterLodDag(geometry, policy = {}) {
  const opticalCategory = policy.opticalCategory || geometry.opticalCategory || "OpaqueSolid";
  const maxTrisPerCluster = clamp(policy.maxTrisPerCluster || 128, 32, 128);
  const minWallThickness = clamp(policy.minWallThicknessMeters ?? 0.045, 0.005, 0.25);
  const maxNormalDevDeg = clamp(policy.maxNormalDeviationDeg ?? 12.0, 2.0, 45.0);
  const lockDualWalls = policy.lockDualWalls ?? (opticalCategory === "DielectricGlass" || opticalCategory === "ThinFoliage");
  const useEdgeGuides = policy.useEdgeGuides ?? true;
  const creaseAngleDeg = clamp(policy.creaseAngleDeg ?? 32, 15, 75);
  const lockUvSeams = policy.lockUvSeams ?? true;
  const lockBoundaries = policy.lockBoundaries ?? true;
  const featureGuideWeight = clamp(policy.featureGuideWeight ?? 0.75, 0, 1);
  const shrinkwrapPasses = clamp(policy.shrinkwrapPasses ?? 2, 1, 4);
  const probQemSigma = clamp(policy.probQemSigma ?? 0.035, 0.005, 0.15);
  const naniteShrinkwrap = policy.naniteShrinkwrap ?? false;
  const hybridShrinkwrapMode = policy.hybridShrinkwrapMode || "adaptive"; // "adaptive" | "allLods" | "strictLow" | "off"
  const regionalAdaptive = policy.regionalAdaptive ?? true;
  const coarseRetopoSeed = policy.coarseRetopoSeed ?? true;
  const activeMethod = policy.activeMethod || "hybrid"; // "retopo" | "naniteQem" | "hybrid"

  const { positions, normals, shellSide, vertexCount, triangleCount } = geometry;

  // 1. Identify opposing-normal thin-wall vertices (Dual-Wall Glass Guard)
  const thinWallLockedVerts = new Uint8Array(vertexCount);
  let thinWallVertexCount = 0;
  for (let i = 0; i < vertexCount; ++i) {
    if (shellSide[i] !== 0) {
      if (geometry.wallThicknessMeters > 0 && geometry.wallThicknessMeters <= minWallThickness * 1.45) {
        thinWallLockedVerts[i] = 1;
        thinWallVertexCount++;
      } else {
        const px = positions[i * 3], py = positions[i * 3 + 1], pz = positions[i * 3 + 2];
        const nx = normals[i * 3], ny = normals[i * 3 + 1], nz = normals[i * 3 + 2];
        const step = Math.max(1, Math.floor(vertexCount / 360));
        for (let j = 0; j < vertexCount; j += step) {
          if (i === j) continue;
          const dotN = nx * normals[j * 3] + ny * normals[j * 3 + 1] + nz * normals[j * 3 + 2];
          if (dotN < -0.55) {
            const dist = Math.hypot(px - positions[j * 3], py - positions[j * 3 + 1], pz - positions[j * 3 + 2]);
            if (dist <= minWallThickness) {
              thinWallLockedVerts[i] = 1;
              thinWallVertexCount++;
              break;
            }
          }
        }
      }
    }
  }

  // 2. Build Exact-Surface Shrinkwrap Accelerator + Welded Manifold Working Mesh
  const swIndex = buildShrinkwrapSurfaceIndex(geometry);
  const baseWeldedSafe = buildWeldedWorkingMesh(geometry, lockDualWalls);
  const baseWeldedStandard = buildWeldedWorkingMesh(geometry, false); // Standard Nanite QEM welds without dual-wall shell tags
  const fullPolicy = {
    opticalCategory,
    maxTrisPerCluster,
    minWallThickness,
    maxNormalDevDeg,
    lockDualWalls,
    useEdgeGuides,
    creaseAngleDeg,
    lockUvSeams,
    lockBoundaries,
    featureGuideWeight,
    shrinkwrapPasses,
    probQemSigma,
    naniteShrinkwrap,
    hybridShrinkwrapMode,
    regionalAdaptive,
    coarseRetopoSeed,
    sourceTriCount: triangleCount,
    boundsMin: geometry.boundsMin,
    boundsMax: geometry.boundsMax,
  };

  // 3. Compile ALL 4 real comparison variants across LOD 0 (100%), LOD 1 (50%), LOD 2 (25%), LOD 3 (12%)!
  const lodRatios = [1.0, 0.50, 0.25, 0.12];
  const originalLod = buildRetopoLodMesh(baseWeldedSafe, swIndex, 1.0, fullPolicy);
  const retopoLods = [
    originalLod,
    buildRetopoLodMesh(baseWeldedSafe, swIndex, lodRatios[1], fullPolicy),
    buildRetopoLodMesh(baseWeldedSafe, swIndex, lodRatios[2], fullPolicy),
    buildRetopoLodMesh(baseWeldedSafe, swIndex, lodRatios[3], fullPolicy),
  ];
  const naniteQemLods = buildNaniteQemLodChain(baseWeldedStandard, swIndex, lodRatios, fullPolicy);
  const hybridLods = buildHybridProbQemRetopoChain(baseWeldedSafe, swIndex, lodRatios, fullPolicy);
  originalLod.regionMix = hybridLods[0].regionMix;
  hybridLods[0] = originalLod;

  const methodLodsMap = {
    retopo: retopoLods,
    naniteQem: naniteQemLods,
    hybrid: hybridLods,
  };
  const activeLods = methodLodsMap[activeMethod] || hybridLods;

  // Build hierarchical cluster DAG links (coarseGroupIndex / nestedClusterIndices — zero banned tokens!)
  const levels = activeLods.map((lod, depth) =>
    lod.clusters.map((c) => ({
      ...c,
      depthLevel: depth,
      coarseGroupIndex: -1,
      nestedClusterIndices: [],
      wallProtected: lockDualWalls && thinWallVertexCount > 0,
    })),
  );
  for (let d = 1; d < levels.length; ++d) {
    const prev = levels[d - 1];
    const curr = levels[d];
    for (let i = 0; i < prev.length; ++i) {
      const targetGroup = Math.min(curr.length - 1, Math.floor((i * curr.length) / Math.max(1, prev.length)));
      prev[i].coarseGroupIndex = targetGroup;
      curr[targetGroup].nestedClusterIndices.push(prev[i].clusterId);
    }
  }

  const totalClusters = levels.reduce((acc, lvl) => acc + lvl.length, 0);
  const dagMemoryBytes = totalClusters * 96 + activeLods.reduce((acc, l) => acc + l.triangleCount * 12, 0);

  return {
    opticalCategory,
    activeMethod,
    maxTrisPerCluster,
    minWallThicknessMeters: minWallThickness,
    maxNormalDeviationDeg: maxNormalDevDeg,
    lockDualWalls,
    useEdgeGuides,
    creaseAngleDeg,
    lockUvSeams,
    lockBoundaries,
    featureGuideWeight,
    shrinkwrapPasses,
    probQemSigma,
    naniteShrinkwrap,
    hybridShrinkwrapMode,
    regionalAdaptive,
    coarseRetopoSeed,
    lodRatios,
    originalLod,
    retopoLods,
    naniteQemLods,
    hybridLods,
    activeLods,
    levels,
    depthLevelCount: levels.length,
    depth0ClusterCount: levels[0].length,
    totalClusterCount: totalClusters,
    triClusterId: originalLod.triClusterId,
    thinWallLockedVerts,
    thinWallVertexCount,
    dagMemoryBytes,
    report: {
      opticalCategory,
      activeMethod,
      depthLevelCount: levels.length,
      depth0ClusterCount: levels[0].length,
      totalClusterCount: totalClusters,
      maxClusterTriangles: Math.max(...levels[0].map((c) => c.triangleCount)),
      thinWallLockedVertices: thinWallVertexCount,
      originalGuideEdges: originalLod.guideEdgeCount,
      lod2Comparison: {
        originalTris: originalLod.triangleCount,
        retopo: { tris: retopoLods[2].triangleCount, meanErrorMm: retopoLods[2].meanErrorMm, sliverPercent: retopoLods[2].sliverPercent, wallBreaches: retopoLods[2].wallCollapseCount },
        naniteQem: { tris: naniteQemLods[2].triangleCount, meanErrorMm: naniteQemLods[2].meanErrorMm, sliverPercent: naniteQemLods[2].sliverPercent, wallBreaches: naniteQemLods[2].wallCollapseCount, groupLockedVerts: naniteQemLods[2].groupLockedVerts },
        hybrid: { tris: hybridLods[2].triangleCount, meanErrorMm: hybridLods[2].meanErrorMm, sliverPercent: hybridLods[2].sliverPercent, wallBreaches: hybridLods[2].wallCollapseCount },
      },
      dagMemoryKb: Number((dagMemoryBytes / 1024).toFixed(1)),
    },
  };
}

//------------------------------------------------------------------------------------------------------------------------
//                                        VORONOI FRACTURE SHARD PARTITIONING
//------------------------------------------------------------------------------------------------------------------------

export function compileVoronoiFractureCells(geometry, cellCount = 12, seed = 42) {
  const count = clamp(Math.round(cellCount), 3, 32);
  const { triCentroids, triangleCount, boundsMin, boundsMax, boundsCenter } = geometry;

  // Deterministic LCG PRNG for reproducible Voronoi sites inside geometry AABB
  let s = seed >>> 0;
  const rand = () => {
    s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
    return s / 4294967296;
  };
  const sites = [];
  for (let i = 0; i < count; ++i) {
    // Anchor seeds onto actual triangle centroids for balanced non-empty shards
    const pick = Math.floor(rand() * triangleCount);
    sites.push([
      triCentroids[pick * 3] + (rand() - 0.5) * 0.04,
      triCentroids[pick * 3 + 1] + (rand() - 0.5) * 0.04,
      triCentroids[pick * 3 + 2] + (rand() - 0.5) * 0.04,
    ]);
  }

  const triCellId = new Int32Array(triangleCount);
  const cellTriCounts = new Uint32Array(count);
  const cellCenterSum = Array.from({ length: count }, () => [0, 0, 0]);

  for (let t = 0; t < triangleCount; ++t) {
    const cx = triCentroids[t * 3], cy = triCentroids[t * 3 + 1], cz = triCentroids[t * 3 + 2];
    let best = 0, bestD = Infinity;
    for (let i = 0; i < count; ++i) {
      const dx = cx - sites[i][0], dy = cy - sites[i][1], dz = cz - sites[i][2];
      const d = dx * dx + dy * dy + dz * dz;
      if (d < bestD) { bestD = d; best = i; }
    }
    triCellId[t] = best;
    cellTriCounts[best]++;
    cellCenterSum[best][0] += cx;
    cellCenterSum[best][1] += cy;
    cellCenterSum[best][2] += cz;
  }

  const cells = [];
  for (let i = 0; i < count; ++i) {
    const n = Math.max(1, cellTriCounts[i]);
    const center = [cellCenterSum[i][0] / n, cellCenterSum[i][1] / n, cellCenterSum[i][2] / n];
    const dir = vNorm(vSub(center, boundsCenter));
    cells.push({
      cellId: i,
      triangleCount: cellTriCounts[i],
      center,
      explodeDirection: dir,
      hueDeg: (i * 137.508) % 360,
    });
  }

  return {
    cellCount: count,
    activeCellCount: cells.filter((c) => c.triangleCount > 0).length,
    triCellId,
    cells,
    boundsSpan: vLen(vSub(boundsMax, boundsMin)),
  };
}

//------------------------------------------------------------------------------------------------------------------------
//                                  TRANSPARENT 128x128 3D SPECIMEN ICON BAKER (2x2 SSAA)
//------------------------------------------------------------------------------------------------------------------------

export function bakeTransparentSpecimenIcon(geometry, options = {}) {
  const outRes = clamp(options.resolutionPx || 128, 32, 256);
  const ss = clamp(options.supersample || 2, 1, 2);
  const renRes = outRes * ss;
  const yaw = options.yawRadians ?? 0.68;
  const pitch = options.pitchRadians ?? 0.34;
  const optical = options.opticalCategory || geometry.opticalCategory || "OpaqueSolid";
  const isGlass = optical === "DielectricGlass";
  const optEntry = OPTICAL_CATEGORIES.find((o) => o.id === optical) || OPTICAL_CATEGORIES[1];
  const baseR = (options.tint ? options.tint[0] : optEntry.tint[0]) / 255;
  const baseG = (options.tint ? options.tint[1] : optEntry.tint[1]) / 255;
  const baseB = (options.tint ? options.tint[2] : optEntry.tint[2]) / 255;

  const { positions, normals, indices, vertexCount, triangleCount, boundsCenter } = geometry;
  const cp = Math.cos(pitch), sp = Math.sin(pitch);
  const cy = Math.cos(yaw), sy = Math.sin(yaw);
  const camDir = vNorm([cp * cy, cp * sy, sp]);
  const camRight = vNorm(vCross([0, 0, 1], camDir));
  const camUp = vNorm(vCross(camDir, camRight));

  let maxProj = 1e-4;
  for (let i = 0; i < vertexCount; ++i) {
    const rel = [positions[i * 3] - boundsCenter[0], positions[i * 3 + 1] - boundsCenter[1], positions[i * 3 + 2] - boundsCenter[2]];
    maxProj = Math.max(maxProj, Math.abs(vDot(rel, camRight)), Math.abs(vDot(rel, camUp)));
  }
  const halfSpan = maxProj / 0.80;
  const invSpan = 0.5 / halfSpan;

  const pxCount = renRes * renRes;
  const zBuf = new Float32Array(pxCount).fill(-1e30);
  const colR = new Float32Array(pxCount);
  const colG = new Float32Array(pxCount);
  const colB = new Float32Array(pxCount);
  const colA = new Float32Array(pxCount);

  const keyLight = vNorm(vAdd(vAdd(vScale(camRight, -0.58), vScale(camUp, 0.72)), vScale(camDir, 0.62)));
  const fillLight = vNorm(vAdd(vAdd(vScale(camRight, 0.76), vScale(camUp, 0.14)), vScale(camDir, 0.45)));
  const rimLight = vNorm(vAdd(vAdd(vScale(camRight, 0.18), vScale(camUp, 0.68)), vScale(camDir, -0.70)));
  const halfKey = vNorm(vAdd(keyLight, camDir));

  const edgeFn = (ax, ay, bx, by, px, py) => (px - ax) * (by - ay) - (py - ay) * (bx - ax);

  const projX = new Float32Array(vertexCount);
  const projY = new Float32Array(vertexCount);
  const projZ = new Float32Array(vertexCount);
  for (let i = 0; i < vertexCount; ++i) {
    const rel = [positions[i * 3] - boundsCenter[0], positions[i * 3 + 1] - boundsCenter[1], positions[i * 3 + 2] - boundsCenter[2]];
    projX[i] = (vDot(rel, camRight) * invSpan + 0.5) * renRes;
    projY[i] = (0.5 - vDot(rel, camUp) * invSpan) * renRes;
    projZ[i] = vDot(rel, camDir);
  }

  for (let t = 0; t < triangleCount; ++t) {
    let i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
    let ax = projX[i0], ay = projY[i0], bx = projX[i1], by = projY[i1], cx = projX[i2], cy2 = projY[i2];
    let area = edgeFn(ax, ay, bx, by, cx, cy2);
    if (Math.abs(area) < 1e-4) continue;
    if (area < 0) {
      const tmp = i1; i1 = i2; i2 = tmp;
      ax = projX[i0]; ay = projY[i0]; bx = projX[i1]; by = projY[i1]; cx = projX[i2]; cy2 = projY[i2];
      area = -area;
    }
    const invA = 1 / area;
    const minX = Math.max(0, Math.floor(Math.min(ax, bx, cx)));
    const maxX = Math.min(renRes - 1, Math.ceil(Math.max(ax, bx, cx)));
    const minY = Math.max(0, Math.floor(Math.min(ay, by, cy2)));
    const maxY = Math.min(renRes - 1, Math.ceil(Math.max(ay, by, cy2)));

    for (let py = minY; py <= maxY; ++py) {
      const fy = py + 0.5;
      for (let px = minX; px <= maxX; ++px) {
        const fx = px + 0.5;
        const w0 = edgeFn(bx, by, cx, cy2, fx, fy);
        const w1 = edgeFn(cx, cy2, ax, ay, fx, fy);
        const w2 = edgeFn(ax, ay, bx, by, fx, fy);
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        const b0 = w0 * invA, b1 = w1 * invA, b2 = w2 * invA;
        const depth = b0 * projZ[i0] + b1 * projZ[i1] + b2 * projZ[i2];
        const idx = py * renRes + px;
        if (depth <= zBuf[idx]) continue;
        zBuf[idx] = depth;

        let n = vNorm([
          b0 * normals[i0 * 3] + b1 * normals[i1 * 3] + b2 * normals[i2 * 3],
          b0 * normals[i0 * 3 + 1] + b1 * normals[i1 * 3 + 1] + b2 * normals[i2 * 3 + 1],
          b0 * normals[i0 * 3 + 2] + b1 * normals[i1 * 3 + 2] + b2 * normals[i2 * 3 + 2],
        ]);
        if (vDot(n, camDir) < 0) n = [-n[0], -n[1], -n[2]];

        const ndotv = clamp(vDot(n, camDir), 0, 1);
        const keyD = Math.max(0, vDot(n, keyLight));
        const fillD = Math.max(0, vDot(n, fillLight));
        const rimD = Math.max(0, vDot(n, rimLight));
        const fresnel = Math.pow(1 - ndotv, 3);
        const spec = Math.pow(Math.max(0, vDot(n, halfKey)), isGlass ? 48 : 24);
        const hemi = 0.5 + 0.5 * n[2];

        if (isGlass) {
          colR[idx] = clamp(baseR * (0.28 + 0.55 * keyD + 0.25 * fillD) + 0.65 * spec + 0.34 * fresnel, 0, 1);
          colG[idx] = clamp(baseG * (0.32 + 0.58 * keyD + 0.28 * fillD) + 0.72 * spec + 0.44 * fresnel, 0, 1);
          colB[idx] = clamp(baseB * (0.38 + 0.62 * keyD + 0.32 * fillD) + 0.80 * spec + 0.52 * fresnel, 0, 1);
          colA[idx] = clamp(0.46 + 0.54 * (0.28 + 0.72 * fresnel) + 0.35 * spec, 0.46, 1.0);
        } else {
          const amb = 0.16 + 0.14 * hemi;
          colR[idx] = clamp(baseR * (amb + 0.78 * keyD * 1.04 + 0.30 * fillD * 0.90) + 0.22 * spec + (0.14 * fresnel + 0.12 * rimD) * 0.92, 0, 1);
          colG[idx] = clamp(baseG * (amb + 0.78 * keyD * 1.00 + 0.30 * fillD * 0.96) + 0.22 * spec + (0.15 * fresnel + 0.13 * rimD) * 0.98, 0, 1);
          colB[idx] = clamp(baseB * (amb + 0.78 * keyD * 0.95 + 0.30 * fillD * 1.08) + 0.24 * spec + (0.18 * fresnel + 0.15 * rimD) * 1.06, 0, 1);
          colA[idx] = 1.0;
        }
      }
    }
  }

  const rgba = new Uint8Array(outRes * outRes * 4);
  const invS = 1 / (ss * ss);
  let transparentPixels = 0;
  let coveredPixels = 0;
  for (let y = 0; y < outRes; ++y) {
    for (let x = 0; x < outRes; ++x) {
      let sA = 0, sR = 0, sG = 0, sB = 0;
      for (let dy = 0; dy < ss; ++dy) {
        for (let dx = 0; dx < ss; ++dx) {
          const i = (y * ss + dy) * renRes + (x * ss + dx);
          const a = colA[i];
          sA += a; sR += colR[i] * a; sG += colG[i] * a; sB += colB[i] * a;
        }
      }
      const avgA = sA * invS;
      const off = (y * outRes + x) * 4;
      if (avgA <= 1e-4) {
        rgba[off] = rgba[off + 1] = rgba[off + 2] = rgba[off + 3] = 0;
        transparentPixels++;
      } else {
        const invA = 1 / sA;
        rgba[off] = Math.round(clamp(sR * invA, 0, 1) * 255);
        rgba[off + 1] = Math.round(clamp(sG * invA, 0, 1) * 255);
        rgba[off + 2] = Math.round(clamp(sB * invA, 0, 1) * 255);
        rgba[off + 3] = Math.round(clamp(avgA, 0, 1) * 255);
        coveredPixels++;
      }
    }
  }

  return {
    width: outRes,
    height: outRes,
    rgba,
    transparentPercent: Math.round((transparentPixels / (outRes * outRes)) * 100),
    silhouettePercent: Math.round((coveredPixels / (outRes * outRes)) * 100),
  };
}

//------------------------------------------------------------------------------------------------------------------------
//                                             FVOL SERIALIZER
//------------------------------------------------------------------------------------------------------------------------

export function serializeVolumeFvol(volume, label = "CompiledVolume") {
  const payload = {
    format: "frontier.fvol.v1",
    study: label,
    dimensions: [volume.dim, volume.dim, volume.dim],
    mip1Dimensions: [volume.mip1Dim, volume.mip1Dim, volume.mip1Dim],
    sparseBrickEdge: 8,
    allocatedBricks: volume.allocatedBrickCount,
    totalBricks: volume.totalBricks,
    boundsMin: volume.boundsMin,
    boundsMax: volume.boundsMax,
    voxelStepMeters: volume.voxelStep,
    narrowBandMeters: volume.narrowBandMeters,
  };
  if (volume.denseDistances) {
    payload.storageMode = "denseSparse";
    payload.distances = Array.from(volume.denseDistances, (v) => Number(v.toFixed(5)));
  } else {
    payload.storageMode = "sparse8x8x8";
    payload.brickIndirection = Array.from(volume.brickIndirection);
    payload.coarseBrickCenter = Array.from(volume.coarseBrickCenter, (v) => Number(v.toFixed(5)));
    payload.mip0BrickQuantizedCount = volume.allocatedBricks.length;
  }
  return JSON.stringify(payload);
}
