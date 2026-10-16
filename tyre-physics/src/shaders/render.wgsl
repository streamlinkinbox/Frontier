// =====================================================================================
//  Rendering for the tyre demo. Everything is read straight from the simulation buffers:
//  the tyre mesh uses the particle positions, the rim uses the hub pose. No CPU copy.
//  Colour encodes physics (membrane area strain), it does not deform anything.
// =====================================================================================

struct Camera {
  vp: mat4x4f,
  eye: vec4f,
  light: vec4f,   // xyz direction towards light
  opts: vec4f,    // x: strain scale, y: show strain (0/1), z: camera x, w: unused
};

@group(0) @binding(0) var<uniform> CAM: Camera;
@group(0) @binding(1) var<storage, read> POS: array<vec4f>;
@group(0) @binding(2) var<storage, read> VNRM: array<vec4f>;
@group(0) @binding(3) var<storage, read> HUB: array<vec4f>;

struct TyreOut {
  @builtin(position) clip: vec4f,
  @location(0) wpos: vec3f,
  @location(1) nrm: vec3f,
  @location(2) strain: f32,
};

@vertex
fn vs_tyre(@builtin(vertex_index) vi: u32) -> TyreOut {
  let p = POS[vi].xyz;
  let n = VNRM[vi];
  var o: TyreOut;
  o.clip = CAM.vp * vec4f(p, 1.0);
  o.wpos = p;
  o.nrm = n.xyz;
  o.strain = n.w;
  return o;
}

@fragment
fn fs_tyre(in: TyreOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
  var n = normalize(in.nrm);
  if (!front) {
    n = -n;
  }
  let rubber = vec3f(0.035, 0.036, 0.04);
  var base = rubber;
  if (CAM.opts.y > 0.5) {
    let s = clamp(in.strain * CAM.opts.x, -1.0, 1.0);
    let stretch = vec3f(1.0, 0.42, 0.08);
    let squeeze = vec3f(0.08, 0.5, 1.0);
    base = mix(rubber, select(squeeze, stretch, s > 0.0), abs(s));
  }
  // contact pmark: a faint highlight on the lowest membrane points
  let pmark = smoothstep(0.012, 0.004, in.wpos.y);
  base = mix(base, vec3f(0.95, 0.85, 0.3), pmark * 0.55);

  let L = normalize(CAM.light.xyz);
  let V = normalize(CAM.eye.xyz - in.wpos);
  let diff = max(dot(n, L), 0.0);
  let H = normalize(L + V);
  let spec = pow(max(dot(n, H), 0.0), 40.0) * 0.35;
  let col = base * (0.18 + 0.85 * diff) + vec3f(spec);
  return vec4f(col, 1.0);
}

// ----------------------------------------------------------------- rim (rotates with the hub)

struct RimIn {
  @location(0) pos: vec3f,
  @location(1) nrm: vec3f,
};

struct RimOut {
  @builtin(position) clip: vec4f,
  @location(0) wpos: vec3f,
  @location(1) nrm: vec3f,
};

@vertex
fn vs_rim(i: RimIn) -> RimOut {
  let h = HUB[0];
  let c = cos(h.w);
  let s = sin(h.w);
  // rotate about the axle (+Z)
  let lp = vec3f(c * i.pos.x - s * i.pos.y, s * i.pos.x + c * i.pos.y, i.pos.z);
  let ln = vec3f(c * i.nrm.x - s * i.nrm.y, s * i.nrm.x + c * i.nrm.y, i.nrm.z);
  let w = h.xyz + lp;
  var o: RimOut;
  o.clip = CAM.vp * vec4f(w, 1.0);
  o.wpos = w;
  o.nrm = ln;
  return o;
}

@fragment
fn fs_rim(in: RimOut) -> @location(0) vec4f {
  let n = normalize(in.nrm);
  let L = normalize(CAM.light.xyz);
  let V = normalize(CAM.eye.xyz - in.wpos);
  let H = normalize(L + V);
  let diff = max(dot(n, L), 0.0);
  let spec = pow(max(dot(n, H), 0.0), 60.0);
  let metal = vec3f(0.62, 0.64, 0.68);
  return vec4f(metal * (0.2 + 0.8 * diff) + vec3f(spec * 0.6), 1.0);
}

// ----------------------------------------------------------------- ground grid

struct GroundOut {
  @builtin(position) clip: vec4f,
  @location(0) wpos: vec3f,
};

@vertex
fn vs_ground(@builtin(vertex_index) vi: u32) -> GroundOut {
  // two triangles covering a 400 m x 400 m area that follows the camera in x
  var corners = array<vec2f, 6>(
    vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0),
    vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let c = corners[vi];
  let w = vec3f(CAM.opts.z + c.x * 200.0, 0.0, c.y * 200.0);
  var o: GroundOut;
  o.clip = CAM.vp * vec4f(w, 1.0);
  o.wpos = w;
  return o;
}

@fragment
fn fs_ground(in: GroundOut) -> @location(0) vec4f {
  let g = in.wpos.xz / 0.5;
  let d = abs(fract(g - 0.5) - 0.5);
  let fw = fwidth(g);
  let line = 1.0 - smoothstep(0.0, 2.0 * max(fw.x, fw.y), min(d.x, d.y));
  let fade = clamp(1.0 - length(in.wpos.xz - vec2f(CAM.opts.z, 0.0)) / 90.0, 0.0, 1.0);
  let base = vec3f(0.055, 0.058, 0.064);
  let lineCol = vec3f(0.30, 0.32, 0.36);
  let col = mix(base, lineCol, line * 0.7) * (0.35 + 0.65 * fade);
  return vec4f(col, 1.0);
}
