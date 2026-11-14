/*============================================================================================================================================
                                                           WEBGPUVIEWPORT.JS
============================================================================================================================================*/
// Real WebGPU (navigator.gpu + WGSL) 4-Way Quad LOD Comparison, Cluster LOD, Dielectric Fresnel Glass, Guide-Edge Overlay,
// Dual-Contour, SDF Slice, and 128px Transparent Icon renderer, with automatic Canvas2D software Z-buffer fallback.

//------------------------------------------------------------------------------------------------------------------------
//                                                    WGSL SHADER SOURCE
//------------------------------------------------------------------------------------------------------------------------

export const GEOMETRY_COMPILER_WGSL = /* wgsl */ `
struct FrameUniforms {
  viewProj : mat4x4<f32>,
  cameraEyeMode : vec4<f32>,   // xyz = camera eye, w = shadeMode (0=Surface/Glass, 1=ClusterLOD, 2=DualContour)
  tintOptical : vec4<f32>,     // rgb = base tint, w = opticalFlags (1=DielectricGlass, 2=ThinFoliage, 3=SubsurfaceWax)
  overlayFlags : vec4<f32>,    // x = showGuides, y = wireOverlay, z = timeSec, w = breachHighlight
};

@group(0) @binding(0) var<uniform> uFrame : FrameUniforms;

struct VertexIn {
  @location(0) pos     : vec3<f32>,
  @location(1) norm    : vec3<f32>,
  @location(2) attrib  : vec4<f32>, // x = clusterHue01, y = breachWeight, z = thinWallLocked, w = guideVertex
  @location(3) bary    : vec3<f32>, // triangle barycentric coords for anti-aliased wireframe
};

struct VertexOut {
  @builtin(position) clipPos : vec4<f32>,
  @location(0) worldPos : vec3<f32>,
  @location(1) worldN   : vec3<f32>,
  @location(2) attrib   : vec4<f32>,
  @location(3) bary     : vec3<f32>,
};

fn hueToRgb(h : f32) -> vec3<f32> {
  let k = fract(vec3<f32>(h, h + 0.66667, h + 0.33333)) * 6.0;
  let rgb = clamp(abs(k - 3.0) - 1.0, vec3<f32>(0.0), vec3<f32>(1.0));
  return mix(vec3<f32>(0.22), rgb, 0.72);
}

@vertex
fn vsMain(input : VertexIn) -> VertexOut {
  var vsOut : VertexOut;
  vsOut.clipPos = uFrame.viewProj * vec4<f32>(input.pos, 1.0);
  vsOut.worldPos = input.pos;
  let nLen = length(input.norm);
  vsOut.worldN = select(vec3<f32>(0.0, 0.0, 1.0), input.norm / nLen, nLen > 0.00001);
  vsOut.attrib = input.attrib;
  vsOut.bary = input.bary;
  return vsOut;
}

@fragment
fn fsMain(input : VertexOut, @builtin(front_facing) frontFacing : bool) -> @location(0) vec4<f32> {
  let wnLen = length(input.worldN);
  var n = select(vec3<f32>(0.0, 0.0, 1.0), input.worldN / wnLen, wnLen > 0.00001);
  let vVec = uFrame.cameraEyeMode.xyz - input.worldPos;
  let vLen = length(vVec);
  let viewDir = select(vec3<f32>(0.0, 0.0, 1.0), vVec / vLen, vLen > 0.00001);
  if (!frontFacing) {
    n = -n;
  }

  let keyDir = normalize(vec3<f32>(0.55, 0.68, 0.74));
  let fillDir = normalize(vec3<f32>(-0.72, -0.25, 0.35));
  let rimDir = normalize(vec3<f32>(-0.18, 0.75, -0.58));
  let halfKey = normalize(keyDir + viewDir);

  let ndotv = clamp(abs(dot(n, viewDir)), 0.0, 1.0);
  let keyD = max(dot(n, keyDir), 0.0);
  let fillD = max(dot(n, fillDir), 0.0);
  let rimD = max(dot(n, rimDir), 0.0);
  let fresnel = pow(1.0 - ndotv, 3.0);
  let hemi = 0.5 + 0.5 * n.z;

  let mode = i32(round(uFrame.cameraEyeMode.w));
  let isGlass = uFrame.tintOptical.w > 0.5 && uFrame.tintOptical.w < 1.5;

  var albedo = uFrame.tintOptical.rgb;
  if (mode == 1) {
    albedo = hueToRgb(input.attrib.x);
  } else if (mode == 2) {
    albedo = vec3<f32>(0.68, 0.80, 0.72); // Dual-Contour sage ceramic
  }

  var color : vec3<f32>;
  if (isGlass && mode == 0) {
    let spec = pow(max(dot(n, halfKey), 0.0), 56.0);
    let glassBase = albedo * (0.24 + 0.52 * keyD + 0.28 * fillD);
    let refractTint = vec3<f32>(0.36, 0.72, 0.86) * (0.35 * fresnel + 0.14 * rimD);
    color = glassBase + refractTint + vec3<f32>(0.92, 0.97, 1.0) * spec * 0.82;
  } else {
    let spec = pow(max(dot(n, halfKey), 0.0), 26.0);
    let amb = 0.16 + 0.15 * hemi;
    color = albedo * (amb + 0.76 * keyD + 0.30 * fillD) + vec3<f32>(0.22) * spec + vec3<f32>(0.14, 0.16, 0.19) * (fresnel + rimD * 0.6);
  }

  // Highlight rare opposite-shell wall breaches in subtle crimson if detected
  if (uFrame.overlayFlags.w > 0.5 && input.attrib.y > 0.25) {
    let pulse = 0.75 + 0.25 * sin(uFrame.overlayFlags.z * 4.5);
    color = mix(color, vec3<f32>(0.94, 0.24, 0.26), clamp(input.attrib.y * 0.85 * pulse, 0.0, 0.88));
  }

  let minBary = min(input.bary.x, min(input.bary.y, input.bary.z));
  let fw = max(fwidth(minBary), 0.0001);
  let edgeAlpha = 1.0 - smoothstep(0.0, fw * 1.35, minBary);

  // Anti-aliased hardware wireframe overlay
  if (uFrame.overlayFlags.y > 0.5) {
    color = mix(color, vec3<f32>(0.92, 0.96, 1.0), edgeAlpha * 0.38);
  }

  // Luminous amber highlight on locked Hard Edges, UV Seams & Open Boundary Guides
  if (uFrame.overlayFlags.x > 0.5 && input.attrib.w > 0.65) {
    let guideGlow = clamp((input.attrib.w - 0.65) / 0.35, 0.0, 1.0);
    color = mix(color, vec3<f32>(1.0, 0.71, 0.33), clamp(edgeAlpha * 0.85 + guideGlow * 0.28, 0.0, 0.92));
  }

  return vec4<f32>(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                      OPTIMIZED 3D SPARSE 8x8x8 SDF RAYMARCHING SHADER (HIERARCHICAL MIP1 + BRICK POOL)
//------------------------------------------------------------------------------------------------------------------------

export const SDF_RAYMARCH_WGSL = /* wgsl */ `
struct SdfRayUniforms {
  camPosIso    : vec4<f32>, // xyz = eye, w = isoOffsetMeters
  camRightFov  : vec4<f32>, // xyz = right, w = tanHalfFov * aspect
  camUpTan     : vec4<f32>, // xyz = up, w = tanHalfFov
  camFwdTime   : vec4<f32>, // xyz = forward, w = timeSec
  boundsMinDim : vec4<f32>, // xyz = boundsMin, w = dim
  boundsMaxBpa : vec4<f32>, // xyz = boundsMax, w = bricksPerAxis
  voxelParams  : vec4<f32>, // x = voxelStep, y = narrowBandMeters, z = mip1Dim, w = forceMip1
  vizParams    : vec4<f32>, // x = vizMode (0=IsoAo, 1=Cutaway, 2=StepHeat, 3=SparseBricks), y = sliceAxis, z = sliceDepth, w = cutFlip
  tintOptical  : vec4<f32>, // rgb = baseTint, w = opticalFlag
};

@group(0) @binding(0) var<uniform> uSdf : SdfRayUniforms;
@group(0) @binding(1) var<storage, read> mip1Data : array<f32>;
@group(0) @binding(2) var<storage, read> brickIndir : array<i32>;
@group(0) @binding(3) var<storage, read> brickPool : array<u32>;

struct SdfRayOut {
  @builtin(position) clipPos : vec4<f32>,
  @location(0) ndc : vec2<f32>,
};

@vertex
fn vsSdfRay(@builtin(vertex_index) vid : u32) -> SdfRayOut {
  var verts = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>( 3.0, -1.0),
    vec2<f32>(-1.0,  3.0),
  );
  let pt = verts[vid];
  var vOut : SdfRayOut;
  vOut.clipPos = vec4<f32>(pt, 0.0, 1.0);
  vOut.ndc = pt;
  return vOut;
}

fn hitBox(ro : vec3<f32>, invRd : vec3<f32>, bMin : vec3<f32>, bMax : vec3<f32>) -> vec2<f32> {
  let t0 = (bMin - ro) * invRd;
  let t1 = (bMax - ro) * invRd;
  let tSm = min(t0, t1);
  let tLg = max(t0, t1);
  let tNear = max(max(tSm.x, tSm.y), tSm.z);
  let tFar = min(min(tLg.x, tLg.y), tLg.z);
  return vec2<f32>(tNear, tFar);
}

fn mip1Voxel(x : i32, y : i32, z : i32, mDim : i32) -> f32 {
  return mip1Data[u32((z * mDim + y) * mDim + x)];
}

fn sampleMip1(p : vec3<f32>) -> f32 {
  let bMin = uSdf.boundsMinDim.xyz;
  let bMax = uSdf.boundsMaxBpa.xyz;
  let cp = clamp(p, bMin, bMax);
  let ext = length(p - cp);
  let mDim = i32(uSdf.voxelParams.z + 0.5);
  let mMax = f32(mDim - 1);
  let step2 = uSdf.voxelParams.x * 2.0;
  let fc = clamp((cp - bMin) / step2 - vec3<f32>(0.5), vec3<f32>(0.0), vec3<f32>(mMax));
  let i0 = vec3<i32>(fc);
  let i1 = min(i0 + vec3<i32>(1), vec3<i32>(mDim - 1));
  let fr = fc - vec3<f32>(i0);

  let c00 = mix(mip1Voxel(i0.x, i0.y, i0.z, mDim), mip1Voxel(i1.x, i0.y, i0.z, mDim), fr.x);
  let c10 = mix(mip1Voxel(i0.x, i1.y, i0.z, mDim), mip1Voxel(i1.x, i1.y, i0.z, mDim), fr.x);
  let c01 = mix(mip1Voxel(i0.x, i0.y, i1.z, mDim), mip1Voxel(i1.x, i0.y, i1.z, mDim), fr.x);
  let c11 = mix(mip1Voxel(i0.x, i1.y, i1.z, mDim), mip1Voxel(i1.x, i1.y, i1.z, mDim), fr.x);
  let val = mix(mix(c00, c10, fr.y), mix(c01, c11, fr.y), fr.z);
  return select(val, max(0.0, val) + ext, ext > 0.0);
}

fn fetchBrickWord(slot : i32, lx : i32, ly : i32, lz : i32) -> f32 {
  let li = u32((lz * 8 + ly) * 8 + lx);
  let wIdx = u32(slot) * 256u + (li >> 1u);
  let packed = brickPool[wIdx];
  let shift = (li & 1u) * 16u;
  let raw16 = i32(packed << (16u - shift)) >> 16u;
  return (f32(raw16) / 32767.0) * uSdf.voxelParams.y;
}

fn fetchMip0Corner(vx : i32, vy : i32, vz : i32, bpa : i32, mDim : i32) -> f32 {
  let bx = vx >> 3u;
  let by = vy >> 3u;
  let bz = vz >> 3u;
  let slot = brickIndir[u32((bz * bpa + by) * bpa + bx)];
  if (slot >= 0) {
    return fetchBrickWord(slot, vx & 7, vy & 7, vz & 7);
  }
  return mip1Voxel(min(vx >> 1u, mDim - 1), min(vy >> 1u, mDim - 1), min(vz >> 1u, mDim - 1), mDim);
}

fn sampleMip0(p : vec3<f32>) -> f32 {
  if (uSdf.voxelParams.w > 0.5) {
    return sampleMip1(p);
  }
  let bMin = uSdf.boundsMinDim.xyz;
  let bMax = uSdf.boundsMaxBpa.xyz;
  let cp = clamp(p, bMin, bMax);
  let ext = length(p - cp);
  let vDim = i32(uSdf.boundsMinDim.w + 0.5);
  let bpa = i32(uSdf.boundsMaxBpa.w + 0.5);
  let mDim = i32(uSdf.voxelParams.z + 0.5);
  let fc = clamp((cp - bMin) / uSdf.voxelParams.x - vec3<f32>(0.5), vec3<f32>(0.0), vec3<f32>(f32(vDim - 1)));
  let i0 = vec3<i32>(fc);
  let i1 = min(i0 + vec3<i32>(1), vec3<i32>(vDim - 1));
  let fr = fc - vec3<f32>(i0);

  // Fast same-brick path: 67%+ of narrow-band trilinear taps live inside 1 brick (1 indirection lookup instead of 8!)
  let b0 = i0 >> vec3<u32>(3u);
  let b1 = i1 >> vec3<u32>(3u);
  var val = 0.0;
  if (all(b0 == b1)) {
    let slot = brickIndir[u32((b0.z * bpa + b0.y) * bpa + b0.x)];
    if (slot < 0) {
      return sampleMip1(p);
    }
    let l0 = i0 & vec3<i32>(7);
    let l1 = i1 & vec3<i32>(7);
    let c00 = mix(fetchBrickWord(slot, l0.x, l0.y, l0.z), fetchBrickWord(slot, l1.x, l0.y, l0.z), fr.x);
    let c10 = mix(fetchBrickWord(slot, l0.x, l1.y, l0.z), fetchBrickWord(slot, l1.x, l1.y, l0.z), fr.x);
    let c01 = mix(fetchBrickWord(slot, l0.x, l0.y, l1.z), fetchBrickWord(slot, l1.x, l0.y, l1.z), fr.x);
    let c11 = mix(fetchBrickWord(slot, l0.x, l1.y, l1.z), fetchBrickWord(slot, l1.x, l1.y, l1.z), fr.x);
    val = mix(mix(c00, c10, fr.y), mix(c01, c11, fr.y), fr.z);
  } else {
    let c00 = mix(fetchMip0Corner(i0.x, i0.y, i0.z, bpa, mDim), fetchMip0Corner(i1.x, i0.y, i0.z, bpa, mDim), fr.x);
    let c10 = mix(fetchMip0Corner(i0.x, i1.y, i0.z, bpa, mDim), fetchMip0Corner(i1.x, i1.y, i0.z, bpa, mDim), fr.x);
    let c01 = mix(fetchMip0Corner(i0.x, i0.y, i1.z, bpa, mDim), fetchMip0Corner(i1.x, i0.y, i1.z, bpa, mDim), fr.x);
    let c11 = mix(fetchMip0Corner(i0.x, i1.y, i1.z, bpa, mDim), fetchMip0Corner(i1.x, i1.y, i1.z, bpa, mDim), fr.x);
    val = mix(mix(c00, c10, fr.y), mix(c01, c11, fr.y), fr.z);
  }
  return select(val, max(0.0, val) + ext, ext > 0.0);
}

fn sampleField(p : vec3<f32>) -> f32 {
  let isoOff = uSdf.camPosIso.w;
  let dCoarse = sampleMip1(p) - isoOff;
  if (uSdf.voxelParams.w < 0.5 && abs(dCoarse) < uSdf.voxelParams.y * 0.65) {
    return sampleMip0(p) - isoOff;
  }
  return dCoarse;
}

fn calcNormal(p : vec3<f32>) -> vec3<f32> {
  // 4-tap tetrahedral SDF gradient (33% fewer lookups than 6-tap central difference)
  let h = uSdf.voxelParams.x * 0.75;
  let k0 = vec3<f32>( 1.0, -1.0, -1.0);
  let k1 = vec3<f32>(-1.0, -1.0,  1.0);
  let k2 = vec3<f32>(-1.0,  1.0, -1.0);
  let k3 = vec3<f32>( 1.0,  1.0,  1.0);
  let g = k0 * sampleField(p + k0 * h)
        + k1 * sampleField(p + k1 * h)
        + k2 * sampleField(p + k2 * h)
        + k3 * sampleField(p + k3 * h);
  let gl = length(g);
  return select(vec3<f32>(0.0, 0.0, 1.0), g / gl, gl > 1e-5);
}

fn calcAo(p : vec3<f32>, n : vec3<f32>) -> f32 {
  let vStep = uSdf.voxelParams.x;
  var occ = 0.0;
  var w = 1.0;
  for (var i = 1; i <= 4; i = i + 1) {
    let dist = vStep * (f32(i) * f32(i) * 0.75 + 0.5);
    let sd = select(sampleMip1(p + n * dist) - uSdf.camPosIso.w, sampleField(p + n * dist), i <= 2);
    occ = occ + w * max(0.0, dist - sd) / max(dist, 1e-4);
    w = w * 0.55;
  }
  return clamp(1.0 - occ * 0.42, 0.22, 1.0);
}

fn calcSoftShadow(ro : vec3<f32>, rd : vec3<f32>) -> f32 {
  let vStep = uSdf.voxelParams.x;
  let span = length(uSdf.boundsMaxBpa.xyz - uSdf.boundsMinDim.xyz);
  var t = vStep * 2.2;
  var sh = 1.0;
  for (var i = 0; i < 10; i = i + 1) {
    if (t > span * 0.75) { break; }
    let d = sampleMip1(ro + rd * t) - uSdf.camPosIso.w;
    if (d < vStep * 0.15) { return 0.25; }
    sh = min(sh, 6.0 * d / t);
    t = t + max(d, vStep * 0.8);
  }
  return clamp(sh, 0.25, 1.0);
}

fn isolinesColor(sd : f32, band : f32) -> vec3<f32> {
  let normD = clamp(sd / max(band, 1e-4), -1.0, 1.0);
  let rings = 0.5 + 0.5 * cos(sd * 56.0);
  let lineEdge = smoothstep(0.86, 0.98, rings);
  let zeroContour = 1.0 - smoothstep(0.0, band * 0.085, abs(sd));
  var col = vec3<f32>(0.0);
  if (sd <= 0.0) {
    // Interior: deep warm crimson-amber
    col = mix(vec3<f32>(0.96, 0.52, 0.22), vec3<f32>(0.36, 0.09, 0.07), clamp(-normD, 0.0, 1.0));
    col = mix(col, vec3<f32>(1.0, 0.86, 0.58), lineEdge * 0.42);
  } else {
    // Exterior: Project Zero teal-cyan distance rings
    col = mix(vec3<f32>(0.22, 0.78, 0.76), vec3<f32>(0.05, 0.12, 0.18), clamp(normD, 0.0, 1.0));
    col = mix(col, vec3<f32>(0.62, 0.96, 0.94), lineEdge * 0.38);
  }
  return mix(col, vec3<f32>(1.0, 0.98, 0.92), zeroContour * 0.92);
}

fn paletteHeat(x : f32) -> vec3<f32> {
  let t = clamp(x, 0.0, 1.0);
  let c0 = vec3<f32>(0.05, 0.07, 0.18);
  let c1 = vec3<f32>(0.10, 0.58, 0.64);
  let c2 = vec3<f32>(0.96, 0.68, 0.22);
  let c3 = vec3<f32>(0.95, 0.18, 0.34);
  if (t < 0.33) { return mix(c0, c1, t / 0.33); }
  if (t < 0.66) { return mix(c1, c2, (t - 0.33) / 0.33); }
  return mix(c2, c3, (t - 0.66) / 0.34);
}

fn shadeIsoSurface(p : vec3<f32>, n : vec3<f32>, viewDir : vec3<f32>) -> vec3<f32> {
  let keyDir = normalize(vec3<f32>(0.55, 0.68, 0.74));
  let fillDir = normalize(vec3<f32>(-0.72, -0.25, 0.35));
  let rimDir = normalize(vec3<f32>(-0.18, 0.75, -0.58));
  let halfKey = normalize(keyDir + viewDir);

  let ao = calcAo(p, n);
  let sh = calcSoftShadow(p + n * uSdf.voxelParams.x * 1.4, keyDir);
  let ndl = max(dot(n, keyDir), 0.0);
  let ndf = max(dot(n, fillDir), 0.0);
  let ndr = max(dot(n, rimDir), 0.0);
  let spec = pow(max(dot(n, halfKey), 0.0), 48.0);
  let ndv = clamp(dot(n, viewDir), 0.0, 1.0);
  let fresnel = pow(1.0 - ndv, 3.4);

  let baseCol = mix(uSdf.tintOptical.rgb, vec3<f32>(0.42, 0.84, 0.76), 0.22);
  var col = baseCol * (0.20 * ao + 0.72 * ndl * sh * ao + 0.22 * ndf * ao)
          + vec3<f32>(0.85, 0.94, 1.0) * (spec * 0.42 * sh)
          + vec3<f32>(0.34, 0.76, 0.78) * (ndr * 0.20 + fresnel * 0.28);
  if (uSdf.tintOptical.w > 0.5 && uSdf.tintOptical.w < 1.5) {
    col = mix(col, vec3<f32>(0.58, 0.90, 0.98), fresnel * 0.45);
  }
  return col;
}

fn traceBricksOverlay(ro : vec3<f32>, rd : vec3<f32>, tStart : f32, tMax : f32) -> vec4<f32> {
  let bMin = uSdf.boundsMinDim.xyz;
  let bMax = uSdf.boundsMaxBpa.xyz;
  let bpa = i32(uSdf.boundsMaxBpa.w + 0.5);
  let cellSize = (bMax - bMin) / f32(bpa);
  let safeRd = vec3<f32>(
    select(rd.x, 1e-6, abs(rd.x) < 1e-6),
    select(rd.y, 1e-6, abs(rd.y) < 1e-6),
    select(rd.z, 1e-6, abs(rd.z) < 1e-6),
  );
  let p0 = ro + rd * (tStart + 1e-4);
  let gPos = (p0 - bMin) / cellSize;
  var cell = clamp(vec3<i32>(floor(gPos)), vec3<i32>(0), vec3<i32>(bpa - 1));
  let stepDir = vec3<i32>(
    select(-1, 1, safeRd.x >= 0.0),
    select(-1, 1, safeRd.y >= 0.0),
    select(-1, 1, safeRd.z >= 0.0),
  );
  let tDelta = abs(cellSize / safeRd);
  let nextEdge = bMin + vec3<f32>(cell + max(stepDir, vec3<i32>(0))) * cellSize;
  var tNext = (nextEdge - ro) / safeRd;

  var accum = vec3<f32>(0.0);
  var alpha = 0.0;
  var tCur = tStart;
  for (var i = 0; i < 48; i = i + 1) {
    if (cell.x < 0 || cell.x >= bpa || cell.y < 0 || cell.y >= bpa || cell.z < 0 || cell.z >= bpa) { break; }
    if (tCur > tMax || alpha > 0.82) { break; }
    let tExitCell = min(min(tNext.x, tNext.y), tNext.z);
    let slot = brickIndir[u32((cell.z * bpa + cell.y) * bpa + cell.x)];
    if (slot >= 0) {
      let hitP = ro + rd * tCur;
      let loc = fract((hitP - bMin) / cellSize);
      let dx = min(loc.x, 1.0 - loc.x);
      let dy = min(loc.y, 1.0 - loc.y);
      let dz = min(loc.z, 1.0 - loc.z);
      // True 3D wireframe edge: 2 out of 3 coordinates close to brick face boundary!
      let e2 = min(max(dx, dy), min(max(dy, dz), max(dx, dz)));
      let wire = 1.0 - smoothstep(0.025, 0.065, e2);
      let brickCol = mix(vec3<f32>(0.16, 0.72, 0.70), vec3<f32>(1.0, 0.74, 0.32), wire);
      let contribA = 0.022 + wire * 0.30;
      accum = accum + (1.0 - alpha) * contribA * brickCol;
      alpha = alpha + (1.0 - alpha) * contribA;
    }
    tCur = tExitCell;
    if (tNext.x <= tNext.y && tNext.x <= tNext.z) {
      tNext.x = tNext.x + tDelta.x;
      cell.x = cell.x + stepDir.x;
    } else if (tNext.y <= tNext.z) {
      tNext.y = tNext.y + tDelta.y;
      cell.y = cell.y + stepDir.y;
    } else {
      tNext.z = tNext.z + tDelta.z;
      cell.z = cell.z + stepDir.z;
    }
  }
  return vec4<f32>(accum, alpha);
}

@fragment
fn fsSdfRay(vIn : SdfRayOut) -> @location(0) vec4<f32> {
  let ro = uSdf.camPosIso.xyz;
  let rd = normalize(
    uSdf.camFwdTime.xyz
    + uSdf.camRightFov.xyz * (vIn.ndc.x * uSdf.camRightFov.w)
    + uSdf.camUpTan.xyz * (vIn.ndc.y * uSdf.camUpTan.w)
  );
  let invRd = vec3<f32>(
    1.0 / select(rd.x, 1e-6, abs(rd.x) < 1e-6),
    1.0 / select(rd.y, 1e-6, abs(rd.y) < 1e-6),
    1.0 / select(rd.z, 1e-6, abs(rd.z) < 1e-6),
  );

  // Subtle Project Zero studio radial backdrop
  let bgGrad = clamp(1.0 - length(vIn.ndc) * 0.55, 0.0, 1.0);
  var bg = mix(vec3<f32>(0.038, 0.042, 0.050), vec3<f32>(0.068, 0.076, 0.092), bgGrad);

  let bMin = uSdf.boundsMinDim.xyz;
  let bMax = uSdf.boundsMaxBpa.xyz;
  let boxT = hitBox(ro, invRd, bMin, bMax);
  if (boxT.y < max(boxT.x, 0.0)) {
    return vec4<f32>(bg, 1.0);
  }

  let tBoxIn = max(boxT.x, 0.0);
  let tBoxOut = boxT.y;
  let vizMode = i32(uSdf.vizParams.x + 0.5);
  let vStep = uSdf.voxelParams.x;
  let band = uSdf.voxelParams.y;

  // Faint AABB bounds cage edges for spatial volume clarity
  let entryP = ro + rd * tBoxIn;
  let uvwIn = (entryP - bMin) / max(bMax - bMin, vec3<f32>(1e-4));
  let bEdgeX = min(uvwIn.x, 1.0 - uvwIn.x);
  let bEdgeY = min(uvwIn.y, 1.0 - uvwIn.y);
  let bEdgeZ = min(uvwIn.z, 1.0 - uvwIn.z);
  let cageMask = 1.0 - smoothstep(0.004, 0.011, max(min(bEdgeX, bEdgeY), min(max(bEdgeX, bEdgeY), bEdgeZ)));
  bg = mix(bg, vec3<f32>(0.34, 0.44, 0.52), cageMask * 0.35);

  var tStart = tBoxIn;
  var tEnd = tBoxOut;
  var planeHitAlpha = 0.0;
  var planeHitCol = vec3<f32>(0.0);
  var cutCapHit = false;
  var cutCapCol = vec3<f32>(0.0);

  // Viz Mode 1: Interactive 3D Cutaway Plane along X/Y/Z with glowing isoline cross-section!
  if (vizMode == 1) {
    let ax = i32(uSdf.vizParams.y + 0.5); // 0=Z, 1=Y, 2=X
    let depth01 = clamp(uSdf.vizParams.z, 0.02, 0.98);
    let signDir = select(1.0, -1.0, uSdf.vizParams.w > 0.5);
    var pNorm = vec3<f32>(0.0, 0.0, signDir);
    var cutCoord = mix(bMin.z, bMax.z, depth01);
    var roAx = ro.z;
    var rdAx = rd.z;
    if (ax == 1) {
      pNorm = vec3<f32>(0.0, signDir, 0.0);
      cutCoord = mix(bMin.y, bMax.y, depth01);
      roAx = ro.y; rdAx = rd.y;
    } else if (ax == 2) {
      pNorm = vec3<f32>(signDir, 0.0, 0.0);
      cutCoord = mix(bMin.x, bMax.x, depth01);
      roAx = ro.x; rdAx = rd.x;
    }
    let roSigned = (roAx - cutCoord) * signDir;
    let rdSigned = rdAx * signDir;
    if (abs(rdSigned) > 1e-5) {
      let tPlane = -roSigned / rdSigned;
      if (roSigned > 0.0) {
        // Camera is on clipped half-space: ray must cross plane before entering retained solid
        if (rdSigned >= 0.0) {
          return vec4<f32>(bg, 1.0);
        }
        if (tPlane > tBoxOut) {
          return vec4<f32>(bg, 1.0);
        }
        if (tPlane >= tBoxIn) {
          let pPl = ro + rd * tPlane;
          let dPl = sampleField(pPl);
          let isoCol = isolinesColor(dPl, band);
          if (dPl <= 0.0) {
            cutCapHit = true;
            let lit = 0.72 + 0.28 * abs(dot(pNorm, normalize(vec3<f32>(0.55, 0.68, 0.74))));
            cutCapCol = isoCol * lit;
          } else {
            // Translucent holographic exterior distance field on the cut plane!
            let fade = clamp(1.0 - dPl / (band * 1.45), 0.0, 1.0);
            planeHitAlpha = 0.14 + 0.44 * fade * fade;
            planeHitCol = isoCol;
          }
          tStart = tPlane + vStep * 0.15;
        }
      } else {
        // Camera is on retained half-space: clip ray at tPlane if heading toward clipped side
        if (rdSigned > 0.0 && tPlane > tBoxIn && tPlane < tBoxOut) {
          tEnd = tPlane;
          let pPl = ro + rd * tPlane;
          let dPl = sampleField(pPl);
          if (dPl > 0.0) {
            let fade = clamp(1.0 - dPl / (band * 1.45), 0.0, 1.0);
            planeHitAlpha = 0.12 + 0.36 * fade * fade;
            planeHitCol = isolinesColor(dPl, band);
          }
        }
      }
    }
  }

  if (cutCapHit) {
    return vec4<f32>(clamp(cutCapCol, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
  }

  // Two-Level Hierarchical Sphere Tracing: Conservative Mip1 empty-space skipping -> Mip0 8x8x8 bricks!
  var t = tStart;
  var steps = 0;
  var hit = false;
  var prevD = 0.0;
  var stepLen = 0.0;
  let isoOff = uSdf.camPosIso.w;

  for (var i = 0; i < 128; i = i + 1) {
    if (t > tEnd) { break; }
    steps = i + 1;
    let p = ro + rd * t;
    let dCoarse = sampleMip1(p) - isoOff;
    var d = dCoarse;
    var inFine = false;
    if (uSdf.voxelParams.w < 0.5 && dCoarse < band * 0.85) {
      d = sampleMip0(p) - isoOff;
      inFine = true;
    }
    let hitEps = max(vStep * 0.12, t * 0.0014);
    if (d < hitEps) {
      if (d < 0.0 && prevD > 0.0) {
        let frac = prevD / max(prevD - d, 1e-5);
        t = (t - stepLen) + stepLen * clamp(frac, 0.0, 1.0);
      }
      hit = true;
      break;
    }
    // Strictly conservative sphere-tracing relaxation: zero thin-wall tunneling!
    let omega = select(0.96, 0.78, inFine);
    stepLen = max(d * omega, vStep * 0.14);
    prevD = d;
    t = t + stepLen;
  }

  var color = bg;
  if (vizMode == 2) {
    // Raymarch Step Cost Heatmap (proves hierarchical Mip1 + 8x8x8 empty-space skipping!)
    let heat = paletteHeat(f32(steps) / 56.0);
    if (hit) {
      let hitP = ro + rd * t;
      let n = calcNormal(hitP);
      let rim = 0.55 + 0.45 * max(dot(n, normalize(vec3<f32>(0.55, 0.68, 0.74))), 0.0);
      color = heat * rim;
    } else {
      color = mix(bg, heat, 0.55);
    }
    return vec4<f32>(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
  }

  if (hit) {
    let hitP = ro + rd * t;
    let n = calcNormal(hitP);
    color = shadeIsoSurface(hitP, n, -rd);
  }

  if (vizMode == 1 && planeHitAlpha > 0.0) {
    color = mix(color, planeHitCol, planeHitAlpha);
  }

  if (vizMode == 3) {
    // 3D Sparse 8x8x8 Brick Occupancy DDA overlaid around the lit iso-surface!
    let ddaMax = select(tEnd, t, hit);
    let brickVis = traceBricksOverlay(ro, rd, tStart, ddaMax);
    color = mix(color, brickVis.rgb / max(brickVis.a, 1e-4), brickVis.a * 0.85);
  }

  return vec4<f32>(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                              CAMERA & MATRIX UTILITIES
//------------------------------------------------------------------------------------------------------------------------

function computeCameraBasis(eye, focusPt, worldUp = [0, 0, 1]) {
  let fx = focusPt[0] - eye[0], fy = focusPt[1] - eye[1], fz = focusPt[2] - eye[2];
  const fl = Math.hypot(fx, fy, fz) || 1;
  fx /= fl; fy /= fl; fz /= fl;

  let rx = fy * worldUp[2] - fz * worldUp[1];
  let ry = fz * worldUp[0] - fx * worldUp[2];
  let rz = fx * worldUp[1] - fy * worldUp[0];
  const rl = Math.hypot(rx, ry, rz) || 1;
  rx /= rl; ry /= rl; rz /= rl;

  const ux = ry * fz - rz * fy;
  const uy = rz * fx - rx * fz;
  const uz = rx * fy - ry * fx;
  return { fwd: [fx, fy, fz], right: [rx, ry, rz], up: [ux, uy, uz] };
}

function mat4Multiply(a, b) {
  const out = new Float32Array(16);
  for (let c = 0; c < 4; ++c) {
    for (let r = 0; r < 4; ++r) {
      out[c * 4 + r] =
        a[0 * 4 + r] * b[c * 4 + 0] +
        a[1 * 4 + r] * b[c * 4 + 1] +
        a[2 * 4 + r] * b[c * 4 + 2] +
        a[3 * 4 + r] * b[c * 4 + 3];
    }
  }
  return out;
}

function mat4PerspectiveWebGpu(fovYRad, aspect, near, far) {
  const f = 1.0 / Math.tan(fovYRad * 0.5);
  const nf = 1.0 / (near - far);
  const out = new Float32Array(16);
  out[0] = f / Math.max(0.1, aspect);
  out[5] = f;
  out[10] = far * nf;
  out[11] = -1;
  out[14] = far * near * nf;
  return out;
}

function mat4LookAt(eye, target, up = [0, 0, 1]) {
  let zx = eye[0] - target[0], zy = eye[1] - target[1], zz = eye[2] - target[2];
  let zl = Math.hypot(zx, zy, zz) || 1;
  zx /= zl; zy /= zl; zz /= zl;

  let xx = up[1] * zz - up[2] * zy;
  let xy = up[2] * zx - up[0] * zz;
  let xz = up[0] * zy - up[1] * zx;
  let xl = Math.hypot(xx, xy, xz) || 1;
  xx /= xl; xy /= xl; xz /= xl;

  const yx = zy * xz - zz * xy;
  const yy = zz * xx - zx * xz;
  const yz = zx * xy - zy * xx;

  const out = new Float32Array(16);
  out[0] = xx; out[1] = yx; out[2] = zx; out[3] = 0;
  out[4] = xy; out[5] = yy; out[6] = zy; out[7] = 0;
  out[8] = xz; out[9] = yz; out[10] = zz; out[11] = 0;
  out[12] = -(xx * eye[0] + xy * eye[1] + xz * eye[2]);
  out[13] = -(yx * eye[0] + yy * eye[1] + yz * eye[2]);
  out[14] = -(zx * eye[0] + zy * eye[1] + zz * eye[2]);
  out[15] = 1;
  return out;
}

function computeCameraEye(camera) {
  const cp = Math.cos(camera.pitch);
  const sp = Math.sin(camera.pitch);
  const cy = Math.cos(camera.yaw);
  const sy = Math.sin(camera.yaw);
  return [
    camera.target[0] + camera.distance * cp * cy,
    camera.target[1] + camera.distance * cp * sy,
    camera.target[2] + camera.distance * sp,
  ];
}

function hueToRgb255(h) {
  const f = (n) => {
    const k = (n + h * 6) % 6;
    const v = 1 - Math.max(0, Math.min(k, 4 - k, 1));
    return Math.round((0.22 + (1 - v) * 0.74) * 255);
  };
  return [f(5), f(3), f(1)];
}

//------------------------------------------------------------------------------------------------------------------------
//                                           UNIFIED WEBGPU + FALLBACK RENDERER
//------------------------------------------------------------------------------------------------------------------------

export class GeometryCompilerViewport {
  constructor(gpuCanvas, overlayCanvas) {
    this.gpuCanvas = gpuCanvas;
    this.overlayCanvas = overlayCanvas;
    this.overlayCtx = overlayCanvas.getContext("2d");

    this.webGpuAvailable = false;
    this.adapterLabel = "Probing WebGPU...";
    this.device = null;
    this.gpuCtx = null;
    this.format = null;
    this.pipeline = null;
    this.sdfPipeline = null;
    this.sdfBindLayout = null;
    this.sdfUniformBuf = null;
    this.sdfGpu = { mip1Buf: null, indirBuf: null, poolBuf: null, bindGroup: null };
    this.depthTex = null;
    this.depthW = 0;
    this.depthH = 0;
    this.uniformBufs = [];
    this.bindGroups = [];
    this._rayFallbackCanvas = null;

    this.buffers = {
      original: null,
      retopoLods: [null, null, null, null],
      naniteQemLods: [null, null, null, null],
      hybridLods: [null, null, null, null],
      dualContour: null,
    };
  }

  async initialize() {
    if (typeof navigator !== "undefined" && navigator.gpu) {
      try {
        const adapter = await navigator.gpu.requestAdapter();
        if (adapter) {
          const device = await adapter.requestDevice();
          const ctx = this.gpuCanvas.getContext("webgpu");
          if (ctx) {
            const format = navigator.gpu.getPreferredCanvasFormat();
            ctx.configure({ device, format, alphaMode: "opaque" });

            device.pushErrorScope("validation");
            const shaderModule = device.createShaderModule({ code: GEOMETRY_COMPILER_WGSL });
            if (typeof shaderModule.getCompilationInfo === "function") {
              const info = await shaderModule.getCompilationInfo();
              if (info.messages.some((m) => m.type === "error")) {
                await device.popErrorScope();
                throw new Error("WGSL shader compilation error");
              }
            }

            const bindLayout = device.createBindGroupLayout({
              entries: [{
                binding: 0,
                visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
                buffer: { type: "uniform" },
              }],
            });
            const pipeline = await device.createRenderPipelineAsync({
              layout: device.createPipelineLayout({ bindGroupLayouts: [bindLayout] }),
              vertex: {
                module: shaderModule,
                entryPoint: "vsMain",
                buffers: [{
                  arrayStride: 52, // 13 floats: pos(3), norm(3), attrib(4), bary(3)
                  attributes: [
                    { shaderLocation: 0, offset: 0, format: "float32x3" },
                    { shaderLocation: 1, offset: 12, format: "float32x3" },
                    { shaderLocation: 2, offset: 24, format: "float32x4" },
                    { shaderLocation: 3, offset: 40, format: "float32x3" },
                  ],
                }],
              },
              fragment: {
                module: shaderModule,
                entryPoint: "fsMain",
                targets: [{ format }],
              },
              primitive: { topology: "triangle-list", cullMode: "none" },
              depthStencil: { format: "depth24plus", depthWriteEnabled: true, depthCompare: "less" },
            });

            const validationErr = await device.popErrorScope();
            if (validationErr) {
              throw new Error(validationErr.message || "WebGPU pipeline validation failed");
            }

            // Build Hardware Sparse 8x8x8 SDF Raymarching Pipeline (fullscreen triangle, 0 vertex buffers!)
            device.pushErrorScope("validation");
            const sdfShaderModule = device.createShaderModule({ code: SDF_RAYMARCH_WGSL });
            if (typeof sdfShaderModule.getCompilationInfo === "function") {
              const sInfo = await sdfShaderModule.getCompilationInfo();
              if (sInfo.messages.some((m) => m.type === "error")) {
                await device.popErrorScope();
                throw new Error("SDF Raymarch WGSL compilation error");
              }
            }
            const sdfBindLayout = device.createBindGroupLayout({
              entries: [
                { binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
                { binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
                { binding: 2, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
                { binding: 3, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
              ],
            });
            const sdfPipeline = await device.createRenderPipelineAsync({
              layout: device.createPipelineLayout({ bindGroupLayouts: [sdfBindLayout] }),
              vertex: { module: sdfShaderModule, entryPoint: "vsSdfRay" },
              fragment: { module: sdfShaderModule, entryPoint: "fsSdfRay", targets: [{ format }] },
              primitive: { topology: "triangle-list", cullMode: "none" },
            });
            const sdfValidationErr = await device.popErrorScope();
            if (!sdfValidationErr) {
              this.sdfBindLayout = sdfBindLayout;
              this.sdfPipeline = sdfPipeline;
              this.sdfUniformBuf = device.createBuffer({ size: 160, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
            }

            // 4 uniform slots for simultaneous 2x2 Quad Viewport comparison in a single GPU RenderPass!
            this.uniformBufs = [];
            this.bindGroups = [];
            for (let i = 0; i < 4; ++i) {
              const uBuf = device.createBuffer({ size: 128, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
              const bg = device.createBindGroup({ layout: bindLayout, entries: [{ binding: 0, resource: { buffer: uBuf } }] });
              this.uniformBufs.push(uBuf);
              this.bindGroups.push(bg);
            }

            this.device = device;
            this.gpuCtx = ctx;
            this.format = format;
            this.pipeline = pipeline;
            this.webGpuAvailable = true;

            const desc = adapter.info?.description || adapter.info?.architecture || adapter.info?.vendor || "Hardware GPU";
            this.adapterLabel = `WebGPU · ${desc}`;
            return true;
          }
        }
      } catch (_err) {
        // Gracefully fall back to software Z-buffer
      }
    }
    this.webGpuAvailable = false;
    this.adapterLabel = "Canvas2D Z-Buffer Fallback";
    return false;
  }

  _packVertexStream(lodMesh) {
    const posArr = lodMesh.positions;
    const normArr = lodMesh.normals;
    const breachMask = lodMesh.breachMask || null;
    const triGroupId = lodMesh.triClusterId || null;
    const thinWallMask = lodMesh.thinWallLockedVerts || null;
    const guideMask = lodMesh.guideMask || lodMesh.uvSeamMask || null;

    const triCount = lodMesh.triangleCount;
    const floatsPerVert = 13;
    const stream = new Float32Array(triCount * 3 * floatsPerVert);
    const baryTable = [
      [1, 0, 0],
      [0, 1, 0],
      [0, 0, 1],
    ];

    for (let t = 0; t < triCount; ++t) {
      const gid = triGroupId ? triGroupId[t] : 0;
      const hue = ((gid * 0.61803398875) % 1 + 1) % 1;
      for (let k = 0; k < 3; ++k) {
        const vi = lodMesh.indices[t * 3 + k];
        const base = (t * 3 + k) * floatsPerVert;
        stream[base + 0] = posArr[vi * 3];
        stream[base + 1] = posArr[vi * 3 + 1];
        stream[base + 2] = posArr[vi * 3 + 2];
        stream[base + 3] = normArr[vi * 3];
        stream[base + 4] = normArr[vi * 3 + 1];
        stream[base + 5] = normArr[vi * 3 + 2];
        stream[base + 6] = hue;
        stream[base + 7] = breachMask ? breachMask[vi] : 0;
        stream[base + 8] = thinWallMask ? thinWallMask[vi] : 0;
        stream[base + 9] = guideMask ? guideMask[vi] : 0;
        stream[base + 10] = baryTable[k][0];
        stream[base + 11] = baryTable[k][1];
        stream[base + 12] = baryTable[k][2];
      }
    }
    return { stream, vertexCount: triCount * 3 };
  }

  _destroyGpuBuf(entry) {
    if (entry?.gpuBuf) entry.gpuBuf.destroy();
  }

  _makeGpuBuf(packed) {
    if (!this.webGpuAvailable || !this.device || !packed || packed.vertexCount === 0) return null;
    const gpuBuf = this.device.createBuffer({
      size: packed.stream.byteLength,
      usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
    });
    this.device.queue.writeBuffer(gpuBuf, 0, packed.stream);
    return { gpuBuf, vertexCount: packed.vertexCount };
  }

  uploadSdfVolume(sdfVolume) {
    if (!this.webGpuAvailable || !this.device || !this.sdfBindLayout || !this.sdfUniformBuf || !sdfVolume) return;
    if (this.sdfGpu.mip1Buf) this.sdfGpu.mip1Buf.destroy();
    if (this.sdfGpu.indirBuf) this.sdfGpu.indirBuf.destroy();
    if (this.sdfGpu.poolBuf) this.sdfGpu.poolBuf.destroy();

    const mip1Src = sdfVolume.conservativeMip1;
    const mip1Buf = this.device.createBuffer({
      size: Math.max(4, mip1Src.byteLength),
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    this.device.queue.writeBuffer(mip1Buf, 0, mip1Src);

    const indirSrc = sdfVolume.brickIndirection;
    const indirBuf = this.device.createBuffer({
      size: Math.max(4, indirSrc.byteLength),
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    this.device.queue.writeBuffer(indirBuf, 0, indirSrc);

    // Pack 512 signed Int16 voxels per allocated 8x8x8 brick into 256 u32 words (2x less GPU bandwidth!)
    const bricks = sdfVolume.allocatedBricks || [];
    const wordCount = Math.max(1, bricks.length * 256);
    const packedPool = new Uint32Array(wordCount);
    for (let s = 0; s < bricks.length; ++s) {
      const b = bricks[s];
      const base = s * 256;
      for (let i = 0; i < 256; ++i) {
        const lo = b[i * 2] & 0xffff;
        const hi = b[i * 2 + 1] & 0xffff;
        packedPool[base + i] = (hi << 16) | lo;
      }
    }
    const poolBuf = this.device.createBuffer({
      size: packedPool.byteLength,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    this.device.queue.writeBuffer(poolBuf, 0, packedPool);

    const bindGroup = this.device.createBindGroup({
      layout: this.sdfBindLayout,
      entries: [
        { binding: 0, resource: { buffer: this.sdfUniformBuf } },
        { binding: 1, resource: { buffer: mip1Buf } },
        { binding: 2, resource: { buffer: indirBuf } },
        { binding: 3, resource: { buffer: poolBuf } },
      ],
    });
    this.sdfGpu = { mip1Buf, indirBuf, poolBuf, bindGroup };
  }

  uploadCompiledAsset(compiled) {
    if (!this.webGpuAvailable || !compiled?.clusterDag) return;
    const { clusterDag, dualContour, sdfVolume } = compiled;

    this._destroyGpuBuf(this.buffers.original);
    this.buffers.original = this._makeGpuBuf(this._packVertexStream(clusterDag.originalLod));

    const uploadChain = (slotKey, chain) => {
      for (let i = 0; i < 4; ++i) {
        this._destroyGpuBuf(this.buffers[slotKey][i]);
        this.buffers[slotKey][i] = chain?.[i] ? this._makeGpuBuf(this._packVertexStream(chain[i])) : null;
      }
    };
    uploadChain("retopoLods", clusterDag.retopoLods);
    uploadChain("naniteQemLods", clusterDag.naniteQemLods);
    uploadChain("hybridLods", clusterDag.hybridLods);

    this._destroyGpuBuf(this.buffers.dualContour);
    this.buffers.dualContour = dualContour?.surface
      ? this._makeGpuBuf(this._packVertexStream(dualContour.surface))
      : null;

    if (sdfVolume) {
      this.uploadSdfVolume(sdfVolume);
    }
  }

  _ensureDepthTexture(w, h) {
    if (this.depthTex && this.depthW === w && this.depthH === h) return;
    if (this.depthTex) this.depthTex.destroy();
    this.depthTex = this.device.createTexture({
      size: [w, h],
      format: "depth24plus",
      usage: GPUTextureUsage.RENDER_ATTACHMENT,
    });
    this.depthW = w;
    this.depthH = h;
  }

  _writeUniform(slotIdx, viewProj, eye, shadeMode, tintRgb, opticalFlag, showGuides, wireOverlay, timeSec, breachHighlight) {
    const data = new Float32Array(28);
    data.set(viewProj, 0);
    data[16] = eye[0]; data[17] = eye[1]; data[18] = eye[2]; data[19] = shadeMode;
    data[20] = tintRgb[0]; data[21] = tintRgb[1]; data[22] = tintRgb[2]; data[23] = opticalFlag;
    data[24] = showGuides; data[25] = wireOverlay; data[26] = timeSec; data[27] = breachHighlight;
    this.device.queue.writeBuffer(this.uniformBufs[slotIdx], 0, data);
  }

  _resolveActiveMethodBuf(method, lodIdx) {
    if (method === "retopo") return this.buffers.retopoLods[lodIdx] || this.buffers.original;
    if (method === "naniteQem") return this.buffers.naniteQemLods[lodIdx] || this.buffers.original;
    return this.buffers.hybridLods[lodIdx] || this.buffers.original;
  }

  renderFrame(state, compiled) {
    const rect = this.gpuCanvas.parentElement.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.max(240, Math.floor(rect.width * dpr));
    const h = Math.max(200, Math.floor(rect.height * dpr));
    if (this.gpuCanvas.width !== w || this.gpuCanvas.height !== h) {
      this.gpuCanvas.width = w; this.gpuCanvas.height = h;
      this.overlayCanvas.width = w; this.overlayCanvas.height = h;
    }

    const ctx2d = this.overlayCtx;
    ctx2d.clearRect(0, 0, w, h);

    if (!compiled?.geometry) return;
    const mode = state.viewMode;

    if (mode === "sdfSlice" || mode === "iconStudio") {
      this.gpuCanvas.style.opacity = "0";
      if (mode === "sdfSlice") this._renderSdfSlice2D(ctx2d, w, h, compiled);
      else this._renderIconStudio2D(ctx2d, w, h, compiled);
      return;
    }

    this.gpuCanvas.style.opacity = this.webGpuAvailable ? "1" : "0";
    const tintMap = {
      DielectricGlass: [0.62, 0.88, 0.95],
      OpaqueSolid: [0.78, 0.80, 0.84],
      ThinFoliage: [0.46, 0.74, 0.52],
      SubsurfaceWax: [0.89, 0.72, 0.57],
    };
    const optFlagMap = { DielectricGlass: 1, ThinFoliage: 2, SubsurfaceWax: 3, OpaqueSolid: 0 };
    const optical = state.opticalCategory || compiled.geometry.opticalCategory;
    const tint = tintMap[optical] || tintMap.OpaqueSolid;
    const optFlag = optFlagMap[optical] ?? 0;
    const eye = computeCameraEye(state.camera);
    const timeSec = performance.now() * 0.001;
    const lodIdx = Math.max(0, Math.min(3, state.activeLodIndex ?? 2));
    const showGuides = state.showGuides ? 1 : 0;
    const wire = state.wireframe ? 1 : 0;

    if (mode === "sdfRaymarch" && compiled.sdfVolume) {
      if (this.webGpuAvailable && this.device && this.sdfPipeline && this.sdfGpu.bindGroup) {
        this._renderSdfRaymarchWebGpu(state, compiled.sdfVolume, eye, tint, optFlag, timeSec);
      } else {
        this.gpuCanvas.style.opacity = "0";
        this._renderSdfRaymarchFallback(ctx2d, w, h, state, compiled.sdfVolume, eye, tint, optFlag);
      }
      return;
    }

    if (this.webGpuAvailable && this.device && this.pipeline) {
      const colorTex = this.gpuCtx.getCurrentTexture();
      const tw = colorTex.width;
      const th = colorTex.height;
      this._ensureDepthTexture(tw, th);
      const encoder = this.device.createCommandEncoder();
      const pass = encoder.beginRenderPass({
        colorAttachments: [{
          view: colorTex.createView(),
          clearValue: { r: 0.043, g: 0.047, b: 0.055, a: 1.0 },
          loadOp: "clear",
          storeOp: "store",
        }],
        depthStencilAttachment: {
          view: this.depthTex.createView(),
          depthClearValue: 1.0,
          depthLoadOp: "clear",
          depthStoreOp: "store",
        },
      });
      pass.setPipeline(this.pipeline);

      if (mode === "quadCompare") {
        // 4-Way 2x2 Synchronized WebGPU Comparison:
        // [0] Top-Left: Original LOD 0      | [1] Top-Right: Retopo + Shrinkwrap
        // [2] Bot-Left: Nanite-Style QEM    | [3] Bot-Right: Hybrid (Prob-QEM + Retopo + Shrinkwrap)
        const halfW = Math.floor(tw / 2);
        const halfH = Math.floor(th / 2);
        const aspect = halfW / Math.max(1, halfH);
        const vp = mat4Multiply(mat4PerspectiveWebGpu(0.70, aspect, 0.05, 40), mat4LookAt(eye, state.camera.target));

        const panes = [
          { x: 0, y: 0, pw: halfW, ph: halfH, buf: this.buffers.original, breach: 0 },
          { x: halfW, y: 0, pw: tw - halfW, ph: halfH, buf: this.buffers.retopoLods[lodIdx], breach: 1 },
          { x: 0, y: halfH, pw: halfW, ph: th - halfH, buf: this.buffers.naniteQemLods[lodIdx], breach: 1 },
          { x: halfW, y: halfH, pw: tw - halfW, ph: th - halfH, buf: this.buffers.hybridLods[lodIdx], breach: 1 },
        ];
        for (let i = 0; i < 4; ++i) {
          const p = panes[i];
          if (!p.buf) continue;
          this._writeUniform(i, vp, eye, 0, tint, optFlag, showGuides, wire, timeSec, p.breach);
          pass.setViewport(p.x, p.y, p.pw, p.ph, 0, 1);
          pass.setBindGroup(0, this.bindGroups[i]);
          pass.setVertexBuffer(0, p.buf.gpuBuf);
          pass.draw(p.buf.vertexCount);
        }

        // Crisp 2x2 crosshair lines on overlayCanvas
        ctx2d.strokeStyle = "rgba(255,255,255,0.14)";
        ctx2d.lineWidth = Math.max(1, dpr);
        ctx2d.beginPath();
        ctx2d.moveTo(Math.floor(w / 2), 0); ctx2d.lineTo(Math.floor(w / 2), h);
        ctx2d.moveTo(0, Math.floor(h / 2)); ctx2d.lineTo(w, Math.floor(h / 2));
        ctx2d.stroke();
      } else {
        const aspect = tw / Math.max(1, th);
        const vp = mat4Multiply(mat4PerspectiveWebGpu(0.70, aspect, 0.05, 40), mat4LookAt(eye, state.camera.target));
        let buf = this.buffers.original;
        let shadeMode = 0;
        if (mode === "clusterLod") {
          buf = this._resolveActiveMethodBuf(state.activeMethod, lodIdx);
          shadeMode = 1;
        } else if (mode === "dualContour") {
          buf = this.buffers.dualContour || this.buffers.original;
          shadeMode = 2;
        } else if (mode === "surface") {
          buf = this._resolveActiveMethodBuf(state.activeMethod, lodIdx);
          shadeMode = 0;
        }
        if (buf) {
          this._writeUniform(0, vp, eye, shadeMode, tint, optFlag, showGuides, wire, timeSec, 1);
          pass.setViewport(0, 0, tw, th, 0, 1);
          pass.setBindGroup(0, this.bindGroups[0]);
          pass.setVertexBuffer(0, buf.gpuBuf);
          pass.draw(buf.vertexCount);
        }
      }

      pass.end();
      this.device.queue.submit([encoder.finish()]);
      return;
    }

    // Software Z-Buffer Canvas2D Fallback (exact 2x2 quad parity)
    this._renderFallback3D(ctx2d, w, h, state, compiled, eye, tint, optical, lodIdx, dpr);
  }

  _renderFallback3D(ctx, w, h, state, compiled, eye, tint, optical, lodIdx, dpr) {
    ctx.fillStyle = "#0b0c0f";
    ctx.fillRect(0, 0, w, h);

    const drawPane = (ox, oy, pw, ph, geom, opts) => {
      if (!geom) return;
      const aspect = pw / Math.max(1, ph);
      const vp = mat4Multiply(mat4PerspectiveWebGpu(0.70, aspect, 0.05, 40), mat4LookAt(eye, state.camera.target));
      const posArr = geom.positions;
      const normArr = geom.normals;
      const indices = geom.indices;
      const triCount = geom.triangleCount;
      const keyDir = [0.50, 0.62, 0.60];
      const isGlass = optical === "DielectricGlass";

      const tris = [];
      const triStep = Math.max(1, Math.ceil(triCount / 24000));
      for (let t = 0; t < triCount; t += triStep) {
        const gid = geom.triClusterId ? geom.triClusterId[t] : 0;
        const pts = [];
        let zSum = 0;
        let breach = 0;
        let guideCnt = 0;
        let nx = 0, ny = 0, nz = 0;
        let skip = false;
        for (let k = 0; k < 3; ++k) {
          const vi = indices[t * 3 + k];
          const x = posArr[vi * 3], y = posArr[vi * 3 + 1], z = posArr[vi * 3 + 2];
          const cx = vp[0] * x + vp[4] * y + vp[8] * z + vp[12];
          const cy = vp[1] * x + vp[5] * y + vp[9] * z + vp[13];
          const cz = vp[2] * x + vp[6] * y + vp[10] * z + vp[14];
          const cw = vp[3] * x + vp[7] * y + vp[11] * z + vp[15];
          if (cw <= 0.05) { skip = true; break; }
          pts.push([ox + (cx / cw * 0.5 + 0.5) * pw, oy + (0.5 - cy / cw * 0.5) * ph]);
          zSum += cz / cw;
          nx += normArr[vi * 3]; ny += normArr[vi * 3 + 1]; nz += normArr[vi * 3 + 2];
          if (geom.breachMask && geom.breachMask[vi]) breach = 1;
          if (geom.guideMask && geom.guideMask[vi]) guideCnt++;
        }
        if (skip) continue;
        const nl = Math.hypot(nx, ny, nz) || 1;
        tris.push({ pts, depth: zSum / 3, n: [nx / nl, ny / nl, nz / nl], gid, breach, isGuide: guideCnt >= 2 });
      }
      tris.sort((a, b) => b.depth - a.depth);

      ctx.save();
      ctx.beginPath(); ctx.rect(ox, oy, pw, ph); ctx.clip();
      for (const tr of tris) {
        const diff = Math.max(0.14, Math.abs(tr.n[0] * keyDir[0] + tr.n[1] * keyDir[1] + tr.n[2] * keyDir[2]));
        let r, g, b;
        if (opts.breachHighlight && tr.breach) {
          r = 238; g = 58; b = 66;
        } else if (opts.colorByGroup) {
          const rgb = hueToRgb255(((tr.gid * 0.61803398875) % 1 + 1) % 1);
          r = Math.round(rgb[0] * (0.35 + 0.72 * diff));
          g = Math.round(rgb[1] * (0.35 + 0.72 * diff));
          b = Math.round(rgb[2] * (0.35 + 0.72 * diff));
        } else if (opts.dualContour) {
          r = Math.round(166 * (0.30 + 0.78 * diff));
          g = Math.round(204 * (0.30 + 0.78 * diff));
          b = Math.round(182 * (0.30 + 0.78 * diff));
        } else if (isGlass) {
          r = Math.round(tint[0] * 255 * (0.32 + 0.78 * diff) + 32);
          g = Math.round(tint[1] * 255 * (0.36 + 0.78 * diff) + 42);
          b = Math.round(tint[2] * 255 * (0.42 + 0.78 * diff) + 52);
        } else {
          r = Math.round(tint[0] * 255 * (0.24 + 0.80 * diff));
          g = Math.round(tint[1] * 255 * (0.24 + 0.80 * diff));
          b = Math.round(tint[2] * 255 * (0.24 + 0.80 * diff));
        }
        ctx.fillStyle = `rgb(${Math.min(255, r)},${Math.min(255, g)},${Math.min(255, b)})`;
        ctx.beginPath();
        ctx.moveTo(tr.pts[0][0], tr.pts[0][1]);
        ctx.lineTo(tr.pts[1][0], tr.pts[1][1]);
        ctx.lineTo(tr.pts[2][0], tr.pts[2][1]);
        ctx.closePath();
        ctx.fill();
        if (state.wireframe || (state.showGuides && tr.isGuide)) {
          ctx.strokeStyle = state.showGuides && tr.isGuide ? "rgba(255,180,84,0.8)" : "rgba(255,255,255,0.16)";
          ctx.lineWidth = Math.max(0.75, 0.75 * dpr);
          ctx.stroke();
        }
      }
      ctx.restore();
    };

    const dag = compiled.clusterDag;
    const activeLod = state.activeMethod === "retopo"
      ? dag.retopoLods[lodIdx]
      : state.activeMethod === "naniteQem"
        ? dag.naniteQemLods[lodIdx]
        : dag.hybridLods[lodIdx];

    if (state.viewMode === "quadCompare") {
      const halfW = Math.floor(w / 2);
      const halfH = Math.floor(h / 2);
      drawPane(0, 0, halfW, halfH, dag.originalLod, {});
      drawPane(halfW, 0, w - halfW, halfH, dag.retopoLods[lodIdx], { breachHighlight: true });
      drawPane(0, halfH, halfW, h - halfH, dag.naniteQemLods[lodIdx], { breachHighlight: true });
      drawPane(halfW, halfH, w - halfW, h - halfH, dag.hybridLods[lodIdx], { breachHighlight: true });
      ctx.strokeStyle = "rgba(255,255,255,0.16)";
      ctx.lineWidth = Math.max(1, dpr);
      ctx.beginPath();
      ctx.moveTo(halfW, 0); ctx.lineTo(halfW, h);
      ctx.moveTo(0, halfH); ctx.lineTo(w, halfH);
      ctx.stroke();
    } else if (state.viewMode === "clusterLod") {
      drawPane(0, 0, w, h, activeLod, { colorByGroup: true });
    } else if (state.viewMode === "dualContour" && compiled.dualContour?.surface) {
      drawPane(0, 0, w, h, compiled.dualContour.surface, { dualContour: true });
    } else {
      drawPane(0, 0, w, h, activeLod, {});
    }
  }

  _renderSdfRaymarchWebGpu(state, sdfVolume, eye, tint, optFlag, timeSec) {
    const colorTex = this.gpuCtx.getCurrentTexture();
    const tw = colorTex.width;
    const th = colorTex.height;
    const aspect = tw / Math.max(1, th);
    const tanHalfFov = Math.tan(0.70 * 0.5);
    const basis = computeCameraBasis(eye, state.camera.target);
    const forceMip1 = (sdfVolume.mip0Evicted || state.sdfSliceLevel === 1) ? 1.0 : 0.0;
    const vizMode = state.sdfVizMode ?? 0;
    const isoOffsetM = (state.sdfIsoOffsetMm ?? 0) * 0.001;
    const cutFlip = state.sdfCutFlip ? 1.0 : 0.0;

    const uData = new Float32Array(36);
    uData[0] = eye[0]; uData[1] = eye[1]; uData[2] = eye[2]; uData[3] = isoOffsetM;
    uData[4] = basis.right[0]; uData[5] = basis.right[1]; uData[6] = basis.right[2]; uData[7] = tanHalfFov * aspect;
    uData[8] = basis.up[0]; uData[9] = basis.up[1]; uData[10] = basis.up[2]; uData[11] = tanHalfFov;
    uData[12] = basis.fwd[0]; uData[13] = basis.fwd[1]; uData[14] = basis.fwd[2]; uData[15] = timeSec;
    uData[16] = sdfVolume.boundsMin[0]; uData[17] = sdfVolume.boundsMin[1]; uData[18] = sdfVolume.boundsMin[2]; uData[19] = sdfVolume.dim;
    uData[20] = sdfVolume.boundsMax[0]; uData[21] = sdfVolume.boundsMax[1]; uData[22] = sdfVolume.boundsMax[2]; uData[23] = sdfVolume.bricksPerAxis;
    uData[24] = sdfVolume.voxelStep; uData[25] = sdfVolume.narrowBandMeters; uData[26] = sdfVolume.mip1Dim; uData[27] = forceMip1;
    uData[28] = vizMode; uData[29] = state.sdfSliceAxis ?? 0; uData[30] = state.sdfSliceDepth ?? 0.5; uData[31] = cutFlip;
    uData[32] = tint[0]; uData[33] = tint[1]; uData[34] = tint[2]; uData[35] = optFlag;
    this.device.queue.writeBuffer(this.sdfUniformBuf, 0, uData);

    const encoder = this.device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
      colorAttachments: [{
        view: colorTex.createView(),
        clearValue: { r: 0.043, g: 0.047, b: 0.055, a: 1.0 },
        loadOp: "clear",
        storeOp: "store",
      }],
    });
    pass.setPipeline(this.sdfPipeline);
    pass.setViewport(0, 0, tw, th, 0, 1);
    pass.setBindGroup(0, this.sdfGpu.bindGroup);
    pass.draw(3);
    pass.end();
    this.device.queue.submit([encoder.finish()]);
  }

  _renderSdfRaymarchFallback(ctx, w, h, state, sdfVolume, eye, tint, optFlag) {
    // Optimized Adaptive-Resolution CPU Sparse 8x8x8 SDF Raymarcher (exact Mip1 empty-space skipping + brick pool parity!)
    const aspect = w / Math.max(1, h);
    const rw = Math.min(220, Math.max(96, Math.round(w * 0.28)));
    const rh = Math.max(64, Math.round(rw / aspect));
    if (!this._rayFallbackCanvas || this._rayFallbackCanvas.width !== rw || this._rayFallbackCanvas.height !== rh) {
      this._rayFallbackCanvas = document.createElement("canvas");
      this._rayFallbackCanvas.width = rw;
      this._rayFallbackCanvas.height = rh;
    }
    const octx = this._rayFallbackCanvas.getContext("2d");
    const img = octx.createImageData(rw, rh);
    const out = img.data;

    const { dim, mip1Dim, bricksPerAxis, voxelStep, narrowBandMeters, boundsMin, boundsMax, conservativeMip1, brickIndirection, allocatedBricks } = sdfVolume;
    const bMinX = boundsMin[0], bMinY = boundsMin[1], bMinZ = boundsMin[2];
    const bMaxX = boundsMax[0], bMaxY = boundsMax[1], bMaxZ = boundsMax[2];
    const invStep = 1 / voxelStep;
    const invStep2 = 0.5 * invStep;
    const bandScale = narrowBandMeters / 32767;
    const forceMip1 = sdfVolume.mip0Evicted || state.sdfSliceLevel === 1;
    const isoOff = (state.sdfIsoOffsetMm ?? 0) * 0.001;
    const vizMode = state.sdfVizMode ?? 0;
    const sliceAxis = state.sdfSliceAxis ?? 0;
    const sliceDepth = Math.min(0.98, Math.max(0.02, state.sdfSliceDepth ?? 0.5));
    const cutSign = state.sdfCutFlip ? -1 : 1;
    const cutCoord = sliceAxis === 0
      ? bMinZ + (bMaxZ - bMinZ) * sliceDepth
      : sliceAxis === 1
        ? bMinY + (bMaxY - bMinY) * sliceDepth
        : bMinX + (bMaxX - bMinX) * sliceDepth;

    const sampleMip1 = (px, py, pz) => {
      const cx = px < bMinX ? bMinX : px > bMaxX ? bMaxX : px;
      const cy = py < bMinY ? bMinY : py > bMaxY ? bMaxY : py;
      const cz = pz < bMinZ ? bMinZ : pz > bMaxZ ? bMaxZ : pz;
      const fx = Math.max(0, Math.min(mip1Dim - 1, (cx - bMinX) * invStep2 - 0.5));
      const fy = Math.max(0, Math.min(mip1Dim - 1, (cy - bMinY) * invStep2 - 0.5));
      const fz = Math.max(0, Math.min(mip1Dim - 1, (cz - bMinZ) * invStep2 - 0.5));
      const x0 = fx | 0, y0 = fy | 0, z0 = fz | 0;
      const x1 = x0 + 1 < mip1Dim ? x0 + 1 : mip1Dim - 1;
      const y1 = y0 + 1 < mip1Dim ? y0 + 1 : mip1Dim - 1;
      const z1 = z0 + 1 < mip1Dim ? z0 + 1 : mip1Dim - 1;
      const tx = fx - x0, ty = fy - y0, tz = fz - z0;
      const row0 = z0 * mip1Dim, row1 = z1 * mip1Dim;
      const v000 = conservativeMip1[(row0 + y0) * mip1Dim + x0];
      const v100 = conservativeMip1[(row0 + y0) * mip1Dim + x1];
      const v010 = conservativeMip1[(row0 + y1) * mip1Dim + x0];
      const v110 = conservativeMip1[(row0 + y1) * mip1Dim + x1];
      const v001 = conservativeMip1[(row1 + y0) * mip1Dim + x0];
      const v101 = conservativeMip1[(row1 + y0) * mip1Dim + x1];
      const v011 = conservativeMip1[(row1 + y1) * mip1Dim + x0];
      const v111 = conservativeMip1[(row1 + y1) * mip1Dim + x1];
      const c00 = v000 + (v100 - v000) * tx;
      const c10 = v010 + (v110 - v010) * tx;
      const c01 = v001 + (v101 - v001) * tx;
      const c11 = v011 + (v111 - v011) * tx;
      const c0 = c00 + (c10 - c00) * ty;
      const c1 = c01 + (c11 - c01) * ty;
      const val = c0 + (c1 - c0) * tz;
      const dx = px - cx, dy = py - cy, dz = pz - cz;
      return (dx || dy || dz) ? Math.max(0, val) + Math.hypot(dx, dy, dz) : val;
    };

    const atMip0 = (vx, vy, vz) => {
      const slot = brickIndirection[((vz >> 3) * bricksPerAxis + (vy >> 3)) * bricksPerAxis + (vx >> 3)];
      if (slot >= 0) return allocatedBricks[slot][((vz & 7) * 8 + (vy & 7)) * 8 + (vx & 7)] * bandScale;
      const mx = Math.min(vx >> 1, mip1Dim - 1), my = Math.min(vy >> 1, mip1Dim - 1), mz = Math.min(vz >> 1, mip1Dim - 1);
      return conservativeMip1[(mz * mip1Dim + my) * mip1Dim + mx];
    };

    const sampleMip0 = (px, py, pz) => {
      const cx = px < bMinX ? bMinX : px > bMaxX ? bMaxX : px;
      const cy = py < bMinY ? bMinY : py > bMaxY ? bMaxY : py;
      const cz = pz < bMinZ ? bMinZ : pz > bMaxZ ? bMaxZ : pz;
      const fx = Math.max(0, Math.min(dim - 1, (cx - bMinX) * invStep - 0.5));
      const fy = Math.max(0, Math.min(dim - 1, (cy - bMinY) * invStep - 0.5));
      const fz = Math.max(0, Math.min(dim - 1, (cz - bMinZ) * invStep - 0.5));
      const x0 = fx | 0, y0 = fy | 0, z0 = fz | 0;
      const x1 = x0 + 1 < dim ? x0 + 1 : dim - 1;
      const y1 = y0 + 1 < dim ? y0 + 1 : dim - 1;
      const z1 = z0 + 1 < dim ? z0 + 1 : dim - 1;
      const tx = fx - x0, ty = fy - y0, tz = fz - z0;
      let val;
      if ((x0 >> 3) === (x1 >> 3) && (y0 >> 3) === (y1 >> 3) && (z0 >> 3) === (z1 >> 3)) {
        const slot = brickIndirection[((z0 >> 3) * bricksPerAxis + (y0 >> 3)) * bricksPerAxis + (x0 >> 3)];
        if (slot < 0) return sampleMip1(px, py, pz);
        const br = allocatedBricks[slot];
        const lx0 = x0 & 7, ly0 = y0 & 7, lz0 = z0 & 7, lx1 = x1 & 7, ly1 = y1 & 7, lz1 = z1 & 7;
        const c00 = br[(lz0 * 8 + ly0) * 8 + lx0] * (1 - tx) + br[(lz0 * 8 + ly0) * 8 + lx1] * tx;
        const c10 = br[(lz0 * 8 + ly1) * 8 + lx0] * (1 - tx) + br[(lz0 * 8 + ly1) * 8 + lx1] * tx;
        const c01 = br[(lz1 * 8 + ly0) * 8 + lx0] * (1 - tx) + br[(lz1 * 8 + ly0) * 8 + lx1] * tx;
        const c11 = br[(lz1 * 8 + ly1) * 8 + lx0] * (1 - tx) + br[(lz1 * 8 + ly1) * 8 + lx1] * tx;
        val = ((c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz) * bandScale;
      } else {
        const c00 = atMip0(x0, y0, z0) * (1 - tx) + atMip0(x1, y0, z0) * tx;
        const c10 = atMip0(x0, y1, z0) * (1 - tx) + atMip0(x1, y1, z0) * tx;
        const c01 = atMip0(x0, y0, z1) * (1 - tx) + atMip0(x1, y0, z1) * tx;
        const c11 = atMip0(x0, y1, z1) * (1 - tx) + atMip0(x1, y1, z1) * tx;
        val = (c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz;
      }
      const dx = px - cx, dy = py - cy, dz = pz - cz;
      return (dx || dy || dz) ? Math.max(0, val) + Math.hypot(dx, dy, dz) : val;
    };

    const sampleField = (px, py, pz) => {
      const dc = sampleMip1(px, py, pz) - isoOff;
      if (!forceMip1 && Math.abs(dc) < narrowBandMeters * 0.65) {
        return sampleMip0(px, py, pz) - isoOff;
      }
      return dc;
    };

    const isoColor = (sd) => {
      const nd = Math.max(-1, Math.min(1, sd / Math.max(narrowBandMeters, 1e-4)));
      const ring = 0.5 + 0.5 * Math.cos(sd * 56.0);
      const line = ring > 0.9 ? (ring - 0.9) * 10 : 0;
      const zero = Math.max(0, 1 - Math.abs(sd) / (narrowBandMeters * 0.09));
      let r, g, b;
      if (sd <= 0) {
        const k = Math.min(1, -nd);
        r = 0.96 * (1 - k) + 0.36 * k + line * 0.12;
        g = 0.52 * (1 - k) + 0.09 * k + line * 0.18;
        b = 0.22 * (1 - k) + 0.07 * k + line * 0.16;
      } else {
        const k = Math.min(1, nd);
        r = 0.22 * (1 - k) + 0.05 * k + line * 0.18;
        g = 0.78 * (1 - k) + 0.12 * k + line * 0.22;
        b = 0.76 * (1 - k) + 0.18 * k + line * 0.20;
      }
      return [
        r * (1 - zero * 0.9) + zero * 0.9,
        g * (1 - zero * 0.9) + zero * 0.88,
        b * (1 - zero * 0.9) + zero * 0.82,
      ];
    };

    const heatCol = (x) => {
      const t = Math.max(0, Math.min(1, x));
      if (t < 0.33) { const u = t / 0.33; return [0.05 + 0.05 * u, 0.07 + 0.51 * u, 0.18 + 0.46 * u]; }
      if (t < 0.66) { const u = (t - 0.33) / 0.33; return [0.10 + 0.86 * u, 0.58 + 0.10 * u, 0.64 - 0.42 * u]; }
      const u = (t - 0.66) / 0.34;
      return [0.96 - 0.01 * u, 0.68 - 0.50 * u, 0.22 + 0.12 * u];
    };

    const tanHalfFov = Math.tan(0.70 * 0.5);
    const basis = computeCameraBasis(eye, state.camera.target);
    const roX = eye[0], roY = eye[1], roZ = eye[2];
    const kl = Math.hypot(0.55, 0.68, 0.74);
    const keyX = 0.55 / kl, keyY = 0.68 / kl, keyZ = 0.74 / kl;
    const fl = Math.hypot(-0.72, -0.25, 0.35);
    const fillX = -0.72 / fl, fillY = -0.25 / fl, fillZ = 0.35 / fl;
    const baseR = tint[0] * 0.78 + 0.42 * 0.22;
    const baseG = tint[1] * 0.78 + 0.84 * 0.22;
    const baseB = tint[2] * 0.78 + 0.76 * 0.22;
    const cellX = (bMaxX - bMinX) / bricksPerAxis;
    const cellY = (bMaxY - bMinY) / bricksPerAxis;
    const cellZ = (bMaxZ - bMinZ) / bricksPerAxis;

    for (let py = 0; py < rh; ++py) {
      const ndcY = 1 - 2 * ((py + 0.5) / rh);
      for (let px = 0; px < rw; ++px) {
        const ndcX = 2 * ((px + 0.5) / rw) - 1;
        let rdX = basis.fwd[0] + basis.right[0] * (ndcX * tanHalfFov * aspect) + basis.up[0] * (ndcY * tanHalfFov);
        let rdY = basis.fwd[1] + basis.right[1] * (ndcX * tanHalfFov * aspect) + basis.up[1] * (ndcY * tanHalfFov);
        let rdZ = basis.fwd[2] + basis.right[2] * (ndcX * tanHalfFov * aspect) + basis.up[2] * (ndcY * tanHalfFov);
        const rLen = Math.hypot(rdX, rdY, rdZ) || 1;
        rdX /= rLen; rdY /= rLen; rdZ /= rLen;

        const bgG = Math.max(0, 1 - Math.hypot(ndcX, ndcY) * 0.55);
        let rCol = 0.038 + 0.030 * bgG;
        let gCol = 0.042 + 0.034 * bgG;
        let bCol = 0.050 + 0.042 * bgG;

        const invX = 1 / (Math.abs(rdX) < 1e-6 ? 1e-6 : rdX);
        const invY = 1 / (Math.abs(rdY) < 1e-6 ? 1e-6 : rdY);
        const invZ = 1 / (Math.abs(rdZ) < 1e-6 ? 1e-6 : rdZ);
        const tx0 = (bMinX - roX) * invX, tx1 = (bMaxX - roX) * invX;
        const ty0 = (bMinY - roY) * invY, ty1 = (bMaxY - roY) * invY;
        const tz0 = (bMinZ - roZ) * invZ, tz1 = (bMaxZ - roZ) * invZ;
        const tNear = Math.max(Math.min(tx0, tx1), Math.min(ty0, ty1), Math.min(tz0, tz1));
        const tFar = Math.min(Math.max(tx0, tx1), Math.max(ty0, ty1), Math.max(tz0, tz1));

        const idx = (py * rw + px) * 4;
        if (tFar < Math.max(tNear, 0)) {
          out[idx] = rCol * 255; out[idx + 1] = gCol * 255; out[idx + 2] = bCol * 255; out[idx + 3] = 255;
          continue;
        }

        let tStart = Math.max(tNear, 0);
        let tEnd = tFar;
        let planeA = 0, pCol = null, cutCap = false;

        if (vizMode === 1) {
          const roAx = sliceAxis === 0 ? roZ : sliceAxis === 1 ? roY : roX;
          const rdAx = sliceAxis === 0 ? rdZ : sliceAxis === 1 ? rdY : rdX;
          const roS = (roAx - cutCoord) * cutSign;
          const rdS = rdAx * cutSign;
          if (Math.abs(rdS) > 1e-5) {
            const tPl = -roS / rdS;
            if (roS > 0) {
              if (rdS >= 0 || tPl > tFar) {
                out[idx] = rCol * 255; out[idx + 1] = gCol * 255; out[idx + 2] = bCol * 255; out[idx + 3] = 255;
                continue;
              }
              if (tPl >= tStart) {
                const dPl = sampleField(roX + rdX * tPl, roY + rdY * tPl, roZ + rdZ * tPl);
                const ic = isoColor(dPl);
                if (dPl <= 0) {
                  cutCap = true;
                  rCol = ic[0] * 0.92; gCol = ic[1] * 0.92; bCol = ic[2] * 0.92;
                } else {
                  const fade = Math.max(0, Math.min(1, 1 - dPl / (narrowBandMeters * 1.45)));
                  planeA = 0.14 + 0.44 * fade * fade;
                  pCol = ic;
                }
                tStart = tPl + voxelStep * 0.15;
              }
            } else if (rdS > 0 && tPl > tStart && tPl < tEnd) {
              tEnd = tPl;
              const dPl = sampleField(roX + rdX * tPl, roY + rdY * tPl, roZ + rdZ * tPl);
              if (dPl > 0) {
                const fade = Math.max(0, Math.min(1, 1 - dPl / (narrowBandMeters * 1.45)));
                planeA = 0.12 + 0.36 * fade * fade;
                pCol = isoColor(dPl);
              }
            }
          }
        }

        if (!cutCap) {
          let t = tStart, steps = 0, hit = false, prevD = 0, stepLen = 0;
          for (let i = 0; i < 112 && t <= tEnd; ++i) {
            steps = i + 1;
            const pxW = roX + rdX * t, pyW = roY + rdY * t, pzW = roZ + rdZ * t;
            const dCoarse = sampleMip1(pxW, pyW, pzW) - isoOff;
            let d = dCoarse;
            let inFine = false;
            if (!forceMip1 && dCoarse < narrowBandMeters * 0.85) {
              d = sampleMip0(pxW, pyW, pzW) - isoOff;
              inFine = true;
            }
            const eps = Math.max(voxelStep * 0.14, t * 0.0018);
            if (d < eps) {
              if (d < 0 && prevD > 0) {
                const f = Math.max(0, Math.min(1, prevD / Math.max(prevD - d, 1e-5)));
                t = (t - stepLen) + stepLen * f;
              }
              hit = true;
              break;
            }
            // Strictly conservative sphere-tracing relaxation: zero thin-wall tunneling!
            stepLen = Math.max(d * (inFine ? 0.78 : 0.96), voxelStep * 0.14);
            prevD = d;
            t += stepLen;
          }

          if (vizMode === 2) {
            const hc = heatCol(steps / 52);
            const mixK = hit ? 0.95 : 0.55;
            rCol = rCol * (1 - mixK) + hc[0] * mixK;
            gCol = gCol * (1 - mixK) + hc[1] * mixK;
            bCol = bCol * (1 - mixK) + hc[2] * mixK;
          } else {
            if (hit) {
              const hx = roX + rdX * t, hy = roY + rdY * t, hz = roZ + rdZ * t;
              const hEps = voxelStep * 0.78;
              const d0 = sampleField(hx + hEps, hy - hEps, hz - hEps);
              const d1 = sampleField(hx - hEps, hy - hEps, hz + hEps);
              const d2 = sampleField(hx - hEps, hy + hEps, hz - hEps);
              const d3 = sampleField(hx + hEps, hy + hEps, hz + hEps);
              let nx = d0 - d1 - d2 + d3;
              let ny = -d0 - d1 + d2 + d3;
              let nz = -d0 + d1 - d2 + d3;
              const nl = Math.hypot(nx, ny, nz) || 1;
              nx /= nl; ny /= nl; nz /= nl;

              // Fast 3-tap Mip1 SDF AO
              let occ = 0;
              for (let a = 1; a <= 3; ++a) {
                const dist = voxelStep * (a * a * 0.85 + 0.6);
                const sd = sampleMip1(hx + nx * dist, hy + ny * dist, hz + nz * dist) - isoOff;
                occ += Math.max(0, dist - sd) / Math.max(dist, 1e-4) * (a === 1 ? 1 : a === 2 ? 0.55 : 0.3);
              }
              const ao = Math.max(0.25, Math.min(1, 1 - occ * 0.42));
              const ndl = Math.max(0, nx * keyX + ny * keyY + nz * keyZ);
              const ndf = Math.max(0, nx * fillX + ny * fillY + nz * fillZ);
              const ndv = Math.max(0, Math.min(1, -(nx * rdX + ny * rdY + nz * rdZ)));
              const fres = Math.pow(1 - ndv, 3.2);
              const lit = (0.20 + 0.72 * ndl + 0.22 * ndf) * ao;
              rCol = baseR * lit + 0.34 * fres * 0.35;
              gCol = baseG * lit + 0.76 * fres * 0.35;
              bCol = baseB * lit + 0.78 * fres * 0.35;
              if (optFlag === 1) {
                rCol = rCol * (1 - fres * 0.4) + 0.58 * fres * 0.4;
                gCol = gCol * (1 - fres * 0.4) + 0.90 * fres * 0.4;
                bCol = bCol * (1 - fres * 0.4) + 0.98 * fres * 0.4;
              }
            }
            if (vizMode === 1 && planeA > 0 && pCol) {
              rCol = rCol * (1 - planeA) + pCol[0] * planeA;
              gCol = gCol * (1 - planeA) + pCol[1] * planeA;
              bCol = bCol * (1 - planeA) + pCol[2] * planeA;
            }
            if (vizMode === 3) {
              // Fast macro-brick occupancy DDA overlay
              const ddaMax = hit ? t : tEnd;
              const p0x = roX + rdX * (tStart + 1e-4);
              const p0y = roY + rdY * (tStart + 1e-4);
              const p0z = roZ + rdZ * (tStart + 1e-4);
              let cx = Math.max(0, Math.min(bricksPerAxis - 1, Math.floor((p0x - bMinX) / cellX)));
              let cy = Math.max(0, Math.min(bricksPerAxis - 1, Math.floor((p0y - bMinY) / cellY)));
              let cz = Math.max(0, Math.min(bricksPerAxis - 1, Math.floor((p0z - bMinZ) / cellZ)));
              const sx = rdX >= 0 ? 1 : -1, sy = rdY >= 0 ? 1 : -1, sz = rdZ >= 0 ? 1 : -1;
              const tdx = Math.abs(cellX * invX), tdy = Math.abs(cellY * invY), tdz = Math.abs(cellZ * invZ);
              let tnx = ((bMinX + (cx + (sx > 0 ? 1 : 0)) * cellX) - roX) * invX;
              let tny = ((bMinY + (cy + (sy > 0 ? 1 : 0)) * cellY) - roY) * invY;
              let tnz = ((bMinZ + (cz + (sz > 0 ? 1 : 0)) * cellZ) - roZ) * invZ;
              let tCur = tStart, accR = 0, accG = 0, accB = 0, accA = 0;
              for (let k = 0; k < 44 && tCur <= ddaMax && accA < 0.8; ++k) {
                if (cx < 0 || cx >= bricksPerAxis || cy < 0 || cy >= bricksPerAxis || cz < 0 || cz >= bricksPerAxis) break;
                const slot = brickIndirection[(cz * bricksPerAxis + cy) * bricksPerAxis + cx];
                const tNextCell = Math.min(tnx, tny, tnz);
                if (slot >= 0) {
                  const hpx = (roX + rdX * tCur - bMinX) / cellX;
                  const hpy = (roY + rdY * tCur - bMinY) / cellY;
                  const hpz = (roZ + rdZ * tCur - bMinZ) / cellZ;
                  const fx = hpx - Math.floor(hpx), fy = hpy - Math.floor(hpy), fz = hpz - Math.floor(hpz);
                  const dx = Math.min(fx, 1 - fx), dy = Math.min(fy, 1 - fy), dz = Math.min(fz, 1 - fz);
                  // True 3D wireframe edge (2 of 3 coords near brick face boundary)
                  const e2 = Math.min(Math.max(dx, dy), Math.max(dy, dz), Math.max(dx, dz));
                  const wire = e2 < 0.065 ? 1 : 0;
                  const ca = 0.022 + wire * 0.30;
                  const brR = wire ? 1.0 : 0.16, brG = wire ? 0.74 : 0.72, brB = wire ? 0.32 : 0.70;
                  accR += (1 - accA) * ca * brR;
                  accG += (1 - accA) * ca * brG;
                  accB += (1 - accA) * ca * brB;
                  accA += (1 - accA) * ca;
                }
                tCur = tNextCell;
                if (tnx <= tny && tnx <= tnz) { tnx += tdx; cx += sx; }
                else if (tny <= tnz) { tny += tdy; cy += sy; }
                else { tnz += tdz; cz += sz; }
              }
              if (accA > 0) {
                const k = accA * 0.85;
                rCol = rCol * (1 - k) + (accR / accA) * k;
                gCol = gCol * (1 - k) + (accG / accA) * k;
                bCol = bCol * (1 - k) + (accB / accA) * k;
              }
            }
          }
        }

        out[idx + 0] = Math.max(0, Math.min(255, Math.round(rCol * 255)));
        out[idx + 1] = Math.max(0, Math.min(255, Math.round(gCol * 255)));
        out[idx + 2] = Math.max(0, Math.min(255, Math.round(bCol * 255)));
        out[idx + 3] = 255;
      }
    }

    octx.putImageData(img, 0, 0);
    ctx.imageSmoothingEnabled = true;
    ctx.drawImage(this._rayFallbackCanvas, 0, 0, w, h);
  }

  _renderSdfSlice2D(ctx, w, h, compiled) {
    ctx.fillStyle = "#090a0d";
    ctx.fillRect(0, 0, w, h);
    if (!compiled.sdfSliceRgba) return;

    const dim = compiled.sdfSliceSize || 256;
    const offscreen = document.createElement("canvas");
    offscreen.width = dim; offscreen.height = dim;
    const octx = offscreen.getContext("2d");
    const img = octx.createImageData(dim, dim);
    img.data.set(compiled.sdfSliceRgba);
    octx.putImageData(img, 0, 0);

    const side = Math.floor(Math.min(w, h) * 0.72);
    const ox = Math.floor((w - side) * 0.5);
    const oy = Math.floor((h - side) * 0.44);
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(offscreen, ox, oy, side, side);

    const bpa = compiled.sdfVolume.bricksPerAxis;
    ctx.strokeStyle = "rgba(255,255,255,0.12)";
    ctx.lineWidth = 1;
    for (let i = 0; i <= bpa; ++i) {
      const p = ox + (i / bpa) * side;
      ctx.beginPath(); ctx.moveTo(p, oy); ctx.lineTo(p, oy + side); ctx.stroke();
      const q = oy + (i / bpa) * side;
      ctx.beginPath(); ctx.moveTo(ox, q); ctx.lineTo(ox + side, q); ctx.stroke();
    }
    ctx.strokeStyle = "rgba(255, 180, 84, 0.55)";
    ctx.lineWidth = 1.5;
    ctx.strokeRect(ox, oy, side, side);
  }

  _renderIconStudio2D(ctx, w, h, compiled) {
    ctx.fillStyle = "#0b0c0f";
    ctx.fillRect(0, 0, w, h);
    const icon = compiled.icon;
    if (!icon) return;

    const side = Math.floor(Math.min(w, h) * 0.62);
    const ox = Math.floor((w - side) * 0.5);
    const oy = Math.floor((h - side) * 0.5);

    const tile = Math.max(12, Math.floor(side / 16));
    for (let y = 0; y < side; y += tile) {
      for (let x = 0; x < side; x += tile) {
        const even = ((Math.floor(x / tile) + Math.floor(y / tile)) & 1) === 0;
        ctx.fillStyle = even ? "#1b1d22" : "#121418";
        ctx.fillRect(ox + x, oy + y, Math.min(tile, side - x), Math.min(tile, side - y));
      }
    }

    const offscreen = document.createElement("canvas");
    offscreen.width = icon.width; offscreen.height = icon.height;
    const octx = offscreen.getContext("2d");
    const img = octx.createImageData(icon.width, icon.height);
    img.data.set(icon.rgba);
    octx.putImageData(img, 0, 0);

    ctx.imageSmoothingEnabled = true;
    ctx.drawImage(offscreen, ox, oy, side, side);

    ctx.strokeStyle = "rgba(255,255,255,0.16)";
    ctx.lineWidth = 1.5;
    ctx.strokeRect(ox, oy, side, side);
  }
}
