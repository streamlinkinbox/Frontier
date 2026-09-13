// ============================================================================
// Frontier SDF Terrain — WebGL2 volumetric viewport.
//
// Renders the SDF volume DIRECTLY (sphere-traced raymarching of the 3D field —
// no mesh, no heightmap) with procedural satellite-style surfacing driven by
// SDF attributes: sediment drape, flow/wetness trails, talus, hardness,
// strata id, cavity/curvature, AO — all computed from the volume in-shader.
// ============================================================================

export class Camera {
  constructor() {
    this.target = { x: 0, y: -0.05, z: 0 };
    this.yaw = 0.7; this.pitch = 0.42; this.dist = 2.6;
    this.fov = 42; this.near = 0.05; this.far = 12;
  }
  eye(out) {
    out = out || {};
    const cp = Math.cos(this.pitch), sp = Math.sin(this.pitch);
    out.x = this.target.x + this.dist * cp * Math.sin(this.yaw);
    out.y = this.target.y + this.dist * sp;
    out.z = this.target.z + this.dist * cp * Math.cos(this.yaw);
    return out;
  }
  basis(out) {
    out = out || {};
    const e = this.eye({});
    let fx = this.target.x - e.x, fy = this.target.y - e.y, fz = this.target.z - e.z;
    const fl = Math.hypot(fx, fy, fz) || 1; fx /= fl; fy /= fl; fz /= fl;
    // right = f × up(0,1,0); up = right × f
    let rx = -fz, ry = 0, rz = fx;
    const rl = Math.hypot(rx, ry, rz) || 1; rx /= rl; ry /= rl; rz /= rl;
    const ux = ry * fz - rz * fy, uy = rz * fx - rx * fz, uz = rx * fy - ry * fx;
    out.eye = e;
    out.fwd = { x: fx, y: fy, z: fz };
    out.right = { x: rx, y: ry, z: rz };
    out.up = { x: ux, y: uy, z: uz };
    return out;
  }
  ray(ndcX, ndcY, aspect, out) {
    const b = this.basis({});
    const t = Math.tan(this.fov * Math.PI / 360);
    let dx = b.fwd.x + (b.right.x * ndcX * t * aspect + b.up.x * ndcY * t);
    let dy = b.fwd.y + (b.right.y * ndcX * t * aspect + b.up.y * ndcY * t);
    let dz = b.fwd.z + (b.right.z * ndcX * t * aspect + b.up.z * ndcY * t);
    const l = Math.hypot(dx, dy, dz) || 1;
    out.ox = b.eye.x; out.oy = b.eye.y; out.oz = b.eye.z;
    out.dx = dx / l; out.dy = dy / l; out.dz = dz / l;
    return out;
  }
}

// ---------------------------------------------------------------------------
const VS_QUAD = `#version 300 es
layout(location=0) in vec2 aPos;
out vec2 vUv;
void main(){ vUv = aPos*0.5+0.5; gl_Position = vec4(aPos,0.0,1.0); }
`;

const FS_MAIN = `#version 300 es
precision highp float;
precision highp sampler3D;
in vec2 vUv;
out vec4 oColor;

uniform sampler3D uSdf;    // R32F
uniform sampler3D uAttrA;  // RGBA8 dynamic
uniform sampler3D uAttrB;  // RGBA8 material
uniform int uN;
uniform float uBoxH;       // half extent of volume
uniform float uVoxel;
uniform vec3 uEye, uFwd, uRight, uUp;
uniform float uTanFov, uAspect;
uniform vec3 uSunDir;
uniform float uTime;
// material (SAT Shade node)
uniform int uPalette;
uniform float uSnow, uVeg, uStrataAmt, uAoAmt, uSeaLevel, uShowSea;
// paint feedback
uniform vec4 uBrush;   // xyz, radius (radius<0 disables)
uniform float uShowEmit;

// --- volume sampling ---------------------------------------------------------
float sdfFetch(ivec3 c){
  c = clamp(c, ivec3(0), ivec3(uN-1));
  return texelFetch(uSdf, c, 0).r;
}
float sampleSdf(vec3 p){
  vec3 g = (p + uBoxH) / (2.0*uBoxH) * float(uN) - 0.5;
  vec3 g0 = floor(g); vec3 f = g - g0;
  ivec3 c0 = ivec3(g0);
  float c000=sdfFetch(c0), c100=sdfFetch(c0+ivec3(1,0,0));
  float c010=sdfFetch(c0+ivec3(0,1,0)), c110=sdfFetch(c0+ivec3(1,1,0));
  float c001=sdfFetch(c0+ivec3(0,0,1)), c101=sdfFetch(c0+ivec3(1,0,1));
  float c011=sdfFetch(c0+ivec3(0,1,1)), c111=sdfFetch(c0+ivec3(1,1,1));
  return mix(mix(mix(c000,c100,f.x), mix(c010,c110,f.x), f.y),
             mix(mix(c001,c101,f.x), mix(c011,c111,f.x), f.y), f.z);
}
vec4 sampleA(vec3 p){
  vec3 uvw = (p + uBoxH) / (2.0*uBoxH);
  if(any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) return vec4(0.0);
  return texture(uAttrA, uvw);
}
vec4 sampleB(vec3 p){
  vec3 uvw = (p + uBoxH) / (2.0*uBoxH);
  if(any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) return vec4(0.5,0.5,0.0,0.0);
  return texture(uAttrB, uvw);
}

// --- tiny shader noise (detail only; structure comes from the volume) ---------
float hash13(vec3 p){ p = fract(p*0.1031); p += dot(p, p.zyx+31.32); return fract((p.x+p.y)*p.z); }
float vnoise(vec3 p){
  vec3 i = floor(p), f = fract(p);
  vec3 u = f*f*(3.0-2.0*f);
  return mix(mix(mix(hash13(i),hash13(i+vec3(1,0,0)),u.x),
                 mix(hash13(i+vec3(0,1,0)),hash13(i+vec3(1,1,0)),u.x), u.y),
             mix(mix(hash13(i+vec3(0,0,1)),hash13(i+vec3(1,0,1)),u.x),
                 mix(hash13(i+vec3(0,1,1)),hash13(i+vec3(1,1,1)),u.x), u.y), u.z);
}
float fbm2(vec3 p){ return vnoise(p)*0.65 + vnoise(p*2.13+7.7)*0.35; }

// --- satellite-style albedo ----------------------------------------------------
// Inputs: position, normal, attributes. No image textures — all procedural.
vec3 satAlbedo(vec3 p, vec3 n, vec4 A, vec4 B, float cavity, float curv, out float rough){
  float slope = 1.0 - n.y;                 // 0 flat .. ~1 vertical/overhang
  float sed = A.r, wet = A.g, tal = A.b;
  float hard = B.r, strata = B.g, precip = B.a;
  float det = fbm2(p*34.0);
  float detBig = fbm2(p*7.0 + 3.1);
  float band = sin(strata*6.28318 + detBig*2.2)*0.5+0.5;

  vec3 rockA, rockB, cliffC, soilC, vegC, sandC, snowC;
  if(uPalette == 1){ // canyon
    rockA=vec3(0.72,0.42,0.26); rockB=vec3(0.55,0.28,0.18);
    cliffC=vec3(0.48,0.26,0.17); soilC=vec3(0.62,0.38,0.24);
    vegC=vec3(0.25,0.32,0.14); sandC=vec3(0.80,0.60,0.38); snowC=vec3(0.9);
  } else if(uPalette == 2){ // karst jungle
    rockA=vec3(0.52,0.52,0.47); rockB=vec3(0.36,0.36,0.33);
    cliffC=vec3(0.42,0.42,0.39); soilC=vec3(0.30,0.22,0.14);
    vegC=vec3(0.10,0.38,0.12); sandC=vec3(0.62,0.55,0.38); snowC=vec3(0.9);
  } else if(uPalette == 3){ // volcanic
    rockA=vec3(0.16,0.14,0.15); rockB=vec3(0.30,0.16,0.12);
    cliffC=vec3(0.20,0.17,0.16); soilC=vec3(0.24,0.18,0.15);
    vegC=vec3(0.16,0.30,0.10); sandC=vec3(0.35,0.28,0.24); snowC=vec3(0.85);
  } else if(uPalette == 4){ // coastal
    rockA=vec3(0.45,0.44,0.38); rockB=vec3(0.60,0.55,0.44);
    cliffC=vec3(0.50,0.46,0.36); soilC=vec3(0.34,0.30,0.20);
    vegC=vec3(0.20,0.36,0.12); sandC=vec3(0.82,0.74,0.55); snowC=vec3(0.9);
  } else { // alpine
    rockA=vec3(0.42,0.40,0.38); rockB=vec3(0.55,0.52,0.48);
    cliffC=vec3(0.36,0.34,0.32); soilC=vec3(0.30,0.26,0.18);
    vegC=vec3(0.16,0.34,0.10); sandC=vec3(0.66,0.58,0.44); snowC=vec3(0.93,0.94,0.97);
  }

  // strata-bedded bedrock
  vec3 rock = mix(rockA, rockB, smoothstep(0.25,0.75,band)*uStrataAmt + (1.0-uStrataAmt)*0.5);
  rock *= 0.85 + 0.3*det;
  // hard pale outcrops on exposed ribs
  rock = mix(rock, rock*1.18+vec3(0.06), smoothstep(0.55,0.9,hard)*0.55);
  // cliff faces
  vec3 alb = mix(rock, cliffC*(0.8+0.4*det), smoothstep(0.25,0.62,slope));
  // soil + vegetation on gentle moist ground
  float moist = clamp(wet*1.4 + (1.0-slope)*0.55 + (detBig-0.5)*0.7, 0.0, 1.0);
  float vegM = smoothstep(0.55,0.2,slope) * smoothstep(0.35,0.75,moist) * uVeg;
  vegM *= smoothstep(0.25,0.55,detBig+0.25);
  alb = mix(alb, soilC*(0.8+0.4*det), smoothstep(0.5,0.15,slope)*0.8);
  alb = mix(alb, vegC*(0.75+0.5*det), clamp(vegM,0.0,1.0));
  // sediment drape + talus aprons
  alb = mix(alb, sandC*(0.85+0.3*det), clamp(sed*1.5,0.0,0.9));
  alb = mix(alb, mix(cliffC,sandC,0.5), clamp(tal*1.2,0.0,0.75)*(1.0-smoothstep(0.6,0.2,slope)*0.4));
  // beach
  float beach = smoothstep(0.035,0.0,abs(p.y-uSeaLevel-0.012)) * smoothstep(0.45,0.12,slope);
  alb = mix(alb, sandC*1.05, beach*0.85);
  // snow
  float snowM = smoothstep(uSnow, uSnow+0.06, p.y + (det-0.5)*0.09) * smoothstep(0.55,0.28,slope);
  alb = mix(alb, snowC, snowM);
  // evaporite precipitate flecks
  alb = mix(alb, vec3(0.88,0.86,0.78), clamp(precip*1.4,0.0,0.8)*smoothstep(0.6,0.3,det));
  // curvature: pale worn ribs, dark incised rills
  alb *= 1.0 + clamp(curv,-1.0,1.0)*0.10;
  // cavity dirt + wet darkening
  alb *= 1.0 - cavity*0.42*uAoAmt;
  alb *= 1.0 - wet*0.30;
  rough = clamp(0.93 - wet*0.55 - snowM*0.25 - beach*0.2, 0.25, 1.0);
  return alb;
}

vec2 boxHit(vec3 ro, vec3 rd){
  vec3 b = vec3(uBoxH);
  vec3 t1 = (-b - ro)/rd, t2 = (b - ro)/rd;
  vec3 tmin = min(t1,t2), tmax = max(t1,t2);
  return vec2(max(max(tmin.x,tmin.y),tmin.z), min(min(tmax.x,tmax.y),tmax.z));
}

float softShadow(vec3 p, vec3 l){
  float t = uVoxel*1.5, res = 1.0;
  for(int i=0;i<26;i++){
    float d = sampleSdf(p + l*t);
    if(d < uVoxel*0.3) return 0.0;
    res = min(res, 7.0*d/t);
    t += clamp(d*0.9, uVoxel*0.8, 0.12);
    if(t > 2.2) break;
  }
  return clamp(res, 0.0, 1.0);
}

void main(){
  vec2 ndc = vUv*2.0 - 1.0;
  vec3 rd = normalize(uFwd + uRight*ndc.x*uTanFov*uAspect + uUp*ndc.y*uTanFov);
  vec3 ro = uEye;

  vec3 skyTop = vec3(0.30,0.46,0.72), skyHor = vec3(0.74,0.80,0.86);
  vec3 sunCol = vec3(1.25,1.12,0.95);
  float sunD = max(dot(rd, uSunDir), 0.0);

  vec2 bh = boxHit(ro, rd);
  float t = max(bh.x, 0.0);
  float tEnd = bh.y;
  bool hit = false;
  vec3 hp = ro;
  float eps = uVoxel*0.45;
  if(tEnd > t){
    for(int i=0;i<240;i++){
      hp = ro + rd*t;
      float d = sampleSdf(hp);
      if(d < eps){ hit = true; break; }
      t += max(d*0.82, uVoxel*0.35);
      if(t > tEnd) break;
    }
  }
  // sea plane
  float tSea = (uSeaLevel - ro.y)/rd.y;
  bool seaHit = uShowSea > 0.5 && rd.y < -1e-4 && tSea > 0.0 &&
                abs(ro.x+rd.x*tSea) < uBoxH && abs(ro.z+rd.z*tSea) < uBoxH;

  vec3 col;
  if(hit && (!seaHit || t < tSea)){
    float e = uVoxel*0.85;
    vec3 n = normalize(vec3(
      sampleSdf(hp+vec3(e,0,0))-sampleSdf(hp-vec3(e,0,0)),
      sampleSdf(hp+vec3(0,e,0))-sampleSdf(hp-vec3(0,e,0)),
      sampleSdf(hp+vec3(0,0,e))-sampleSdf(hp-vec3(0,0,e))));
    if(dot(n,rd) > 0.0) n = -n;
    // SDF-cone AO
    float ao = 0.0, w = 1.0;
    for(int i=1;i<=5;i++){
      float h = uVoxel*(0.8 + float(i)*1.7);
      float d = sampleSdf(hp + n*h);
      ao += (h - min(d,h))*w;
      w *= 0.72;
    }
    ao = clamp(1.0 - ao/(uVoxel*7.0)*1.6, 0.0, 1.0);
    ao = mix(1.0, ao, uAoAmt);
    // curvature (laplacian sign: + pit, - rib)
    float ec = uVoxel*1.6;
    float lap = (sampleSdf(hp+vec3(ec,0,0))+sampleSdf(hp-vec3(ec,0,0))+
                 sampleSdf(hp+vec3(0,ec,0))+sampleSdf(hp-vec3(0,ec,0))+
                 sampleSdf(hp+vec3(0,0,ec))+sampleSdf(hp-vec3(0,0,ec))-6.0*sampleSdf(hp))/(ec*ec);
    float cavity = clamp(lap*uVoxel*2.2, 0.0, 1.0);
    float curv = clamp(-lap*uVoxel*3.0, -1.0, 1.0);
    vec4 A = sampleA(hp + n*uVoxel*0.6);
    vec4 B = sampleB(hp - n*uVoxel*0.6);
    float rough;
    vec3 alb = satAlbedo(hp, n, A, B, cavity, curv, rough);
    float sh = softShadow(hp + n*uVoxel*1.2, uSunDir);
    float ndl = max(dot(n, uSunDir), 0.0);
    float wrap = max(dot(n, uSunDir)*0.5+0.5, 0.0);
    vec3 amb = mix(vec3(0.32,0.34,0.38), vec3(0.55,0.62,0.72), n.y*0.5+0.5);
    vec3 lit = alb*(amb*ao*0.9 + sunCol*ndl*sh) + alb*vec3(0.35,0.38,0.42)*pow(wrap,2.0)*ao*0.4;
    // wet specular
    vec3 hv = normalize(uSunDir - rd);
    lit += sunCol*pow(max(dot(n,hv),0.0), mix(90.0,14.0,rough))*(1.0-rough)*0.6*sh;
    // emitter overlay (paint feedback)
    if(uShowEmit > 0.5 && A.a > 0.02){
      lit = mix(lit, vec3(0.2,0.6,1.0), clamp(A.a*0.85,0.0,0.85));
    }
    // brush ring
    if(uBrush.w > 0.0){
      float bd = distance(hp, uBrush.xyz);
      float ring = smoothstep(uBrush.w, uBrush.w*0.94, bd) - smoothstep(uBrush.w*0.90, uBrush.w*0.82, bd);
      float fill = smoothstep(uBrush.w, uBrush.w*0.9, bd)*0.18;
      lit = mix(lit, vec3(0.3,0.8,1.0), clamp(ring+fill,0.0,0.8));
    }
    // distance haze
    float fog = 1.0 - exp(-t*0.55);
    lit = mix(lit, skyHor, fog*0.55);
    col = lit;
  } else if(seaHit){
    vec3 sp = ro + rd*tSea;
    float w1 = fbm2(sp*22.0 + vec3(uTime*0.35, 0.0, uTime*0.22));
    float w2 = fbm2(sp*47.0 - vec3(0.0, 0.0, uTime*0.5));
    vec3 n = normalize(vec3((w1-0.5)*0.35, 1.0, (w2-0.5)*0.35));
    float fres = pow(1.0 - max(dot(n,-rd),0.0), 3.0)*0.85 + 0.06;
    // shallow tint: march down a little to sense the bed
    float bed = sampleSdf(sp - vec3(0.0, uVoxel*2.0, 0.0));
    float depthM = clamp(-bed*2.2, 0.0, 1.0);
    vec3 shallow = vec3(0.25,0.55,0.60), deep = vec3(0.03,0.18,0.32);
    vec3 base = mix(shallow, deep, depthM);
    vec3 hv = normalize(uSunDir - rd);
    float spec = pow(max(dot(n,hv),0.0), 220.0)*1.4 + pow(max(dot(n,hv),0.0), 36.0)*0.25;
    vec3 skyRef = mix(skyHor, skyTop, clamp(reflect(rd,n).y,0.0,1.0));
    col = mix(base, skyRef, fres) + sunCol*spec;
    float foam = smoothstep(0.012,0.0,abs(bed+uVoxel*1.5))*(0.5+0.5*sin(uTime*2.0+w1*9.0));
    col = mix(col, vec3(0.9,0.93,0.94), clamp(foam,0.0,1.0)*0.7);
    float fog = 1.0 - exp(-tSea*0.5);
    col = mix(col, skyHor, fog*0.5);
  } else {
    float h = clamp(rd.y, -0.1, 1.0);
    col = mix(skyHor, skyTop, pow(max(h,0.0), 0.6));
    col += sunCol * (pow(sunD, 900.0)*1.2 + pow(sunD, 18.0)*0.18);
    // ground haze below horizon
    col = mix(col, skyHor*0.92, smoothstep(0.0,-0.4,rd.y));
  }
  // vignette + tonemap
  col = col/(1.0+col*0.12);
  vec2 q = vUv - 0.5;
  col *= 1.0 - dot(q,q)*0.55;
  // dither
  col += (hash13(vec3(vUv*913.0, uTime))-0.5)*0.008;
  oColor = vec4(col, 1.0);
}
`;

const VS_LINE = `#version 300 es
layout(location=0) in vec3 aP;
uniform vec3 uEye, uFwd, uRight, uUp;
uniform float uTanFov, uAspect, uNear, uFar;
void main(){
  vec3 d = aP - uEye;
  float depth = max(dot(d, uFwd), 1e-3); // uFwd points into the scene
  float x = dot(d, uRight)/(uTanFov*uAspect*depth);
  float y = dot(d, uUp)/(uTanFov*depth);
  float A = (uFar+uNear)/(uFar-uNear), B = 2.0*uFar*uNear/(uFar-uNear);
  float zndc = A - B/depth;
  gl_Position = vec4(x*depth, y*depth, zndc*depth, depth);
}
`;
const FS_LINE = `#version 300 es
precision mediump float;
out vec4 o;
void main(){ o = vec4(0.55,0.75,1.0,0.42); }
`;

// ---------------------------------------------------------------------------
function compile(gl, type, src) {
  const sh = gl.createShader(type);
  gl.shaderSource(sh, src);
  gl.compileShader(sh);
  if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
    throw new Error('Shader: ' + gl.getShaderInfoLog(sh));
  }
  return sh;
}
function program(gl, vs, fs) {
  const p = gl.createProgram();
  gl.attachShader(p, compile(gl, gl.VERTEX_SHADER, vs));
  gl.attachShader(p, compile(gl, gl.FRAGMENT_SHADER, fs));
  gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
    throw new Error('Link: ' + gl.getProgramInfoLog(p));
  }
  return p;
}

export class Renderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.gl = null;
    this.vol = null;
    this.ready = false;
    this.renderScale = 0.8;
    this.showRain = true;
    this.showEmit = true;
    this.brush = { x: 0, y: 0, z: 0, r: -1 };
    this.mat = null;
    this.time = 0;
  }

  init() {
    const gl = this.canvas.getContext('webgl2', { antialias: false, alpha: false });
    if (!gl) return 'WebGL2 is required (no context).';
    this.gl = gl;
    try {
      this.prog = program(gl, VS_QUAD, FS_MAIN);
      this.lineProg = program(gl, VS_LINE, FS_LINE);
    } catch (e) {
      return String(e.message || e);
    }
    // quad
    this.quad = gl.createVertexArray();
    gl.bindVertexArray(this.quad);
    const vb = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vb);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
    // line overlay buffers
    this.lineVAO = gl.createVertexArray();
    gl.bindVertexArray(this.lineVAO);
    this.lineBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.lineBuf);
    gl.bufferData(gl.ARRAY_BUFFER, 24000 * 2 * 3 * 4, gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 24, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 24, 12);
    gl.bindVertexArray(null);
    this.lineData = new Float32Array(24000 * 2 * 3);
    // textures
    this.texSdf = gl.createTexture();
    this.texA = gl.createTexture();
    this.texB = gl.createTexture();
    this.u = {};
    for (const n of ['uSdf', 'uAttrA', 'uAttrB', 'uN', 'uBoxH', 'uVoxel', 'uEye', 'uFwd',
      'uRight', 'uUp', 'uTanFov', 'uAspect', 'uSunDir', 'uTime', 'uPalette', 'uSnow',
      'uVeg', 'uStrataAmt', 'uAoAmt', 'uSeaLevel', 'uShowSea', 'uBrush', 'uShowEmit']) {
      this.u[n] = gl.getUniformLocation(this.prog, n);
    }
    this.lu = {};
    for (const n of ['uEye', 'uFwd', 'uRight', 'uUp', 'uTanFov', 'uAspect', 'uNear', 'uFar']) {
      this.lu[n] = gl.getUniformLocation(this.lineProg, n);
    }
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.CULL_FACE);
    this.ready = true;
    return null;
  }

  bindVolume(vol) {
    this.vol = vol;
    const gl = this.gl, N = vol.N;
    gl.bindTexture(gl.TEXTURE_3D, this.texSdf);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);
    gl.texImage3D(gl.TEXTURE_3D, 0, gl.R32F, N, N, N, 0, gl.RED, gl.FLOAT, vol.sdf);
    for (const [tex, arr] of [[this.texA, vol.attrA], [this.texB, vol.attrB]]) {
      gl.bindTexture(gl.TEXTURE_3D, tex);
      gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);
      gl.texImage3D(gl.TEXTURE_3D, 0, gl.RGBA8, N, N, N, 0, gl.RGBA, gl.UNSIGNED_BYTE, arr);
    }
    vol.dirtyBricks.clear();
    this._tmpS = new Float32Array(8 * 8 * 8);
    this._tmpA = new Uint8Array(8 * 8 * 8 * 4);
    this._tmpB = new Uint8Array(8 * 8 * 8 * 4);
  }

  syncDirty(maxBricks = 700) {
    const vol = this.vol;
    if (!vol || !vol.dirtyBricks.size) return 0;
    const gl = this.gl, N = vol.N, B = vol.bricksPerSide;
    const bricks = vol.takeDirtyBricks(maxBricks);
    for (const b of bricks) {
      const bi = b % B, bj = ((b / B) | 0) % B, bk = (b / (B * B)) | 0;
      const x0 = bi * 8, y0 = bj * 8, z0 = bk * 8;
      const sx = Math.min(8, N - x0), sy = Math.min(8, N - y0), sz = Math.min(8, N - z0);
      // gather brick voxels (bricks are non-contiguous in the flat arrays)
      let p = 0, q = 0;
      for (let k = 0; k < 8; k++) for (let j = 0; j < 8; j++) for (let i = 0; i < 8; i++) {
        const ii = Math.min(N - 1, x0 + i), jj = Math.min(N - 1, y0 + j), kk = Math.min(N - 1, z0 + k);
        const id = (kk * N + jj) * N + ii;
        this._tmpS[p++] = vol.sdf[id];
        const a4 = id * 4;
        this._tmpA[q] = vol.attrA[a4]; this._tmpA[q + 1] = vol.attrA[a4 + 1];
        this._tmpA[q + 2] = vol.attrA[a4 + 2]; this._tmpA[q + 3] = vol.attrA[a4 + 3];
        this._tmpB[q] = vol.attrB[a4]; this._tmpB[q + 1] = vol.attrB[a4 + 1];
        this._tmpB[q + 2] = vol.attrB[a4 + 2]; this._tmpB[q + 3] = vol.attrB[a4 + 3];
        q += 4;
      }
      gl.bindTexture(gl.TEXTURE_3D, this.texSdf);
      gl.texSubImage3D(gl.TEXTURE_3D, 0, x0, y0, z0, 8, 8, 8, gl.RED, gl.FLOAT, this._tmpS);
      gl.bindTexture(gl.TEXTURE_3D, this.texA);
      gl.texSubImage3D(gl.TEXTURE_3D, 0, x0, y0, z0, 8, 8, 8, gl.RGBA, gl.UNSIGNED_BYTE, this._tmpA);
      gl.bindTexture(gl.TEXTURE_3D, this.texB);
      gl.texSubImage3D(gl.TEXTURE_3D, 0, x0, y0, z0, 8, 8, 8, gl.RGBA, gl.UNSIGNED_BYTE, this._tmpB);
      void sx; void sy; void sz;
    }
    return bricks.length;
  }

  setMaterial(m) { this.mat = m; }

  resize() {
    const c = this.canvas;
    const r = c.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 1.5);
    const w = Math.max(2, Math.floor(r.width * dpr * this.renderScale));
    const h = Math.max(2, Math.floor(r.height * dpr * this.renderScale));
    if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
    return { w, h };
  }

  render(cam, hydro) {
    const gl = this.gl;
    if (!gl || !this.ready || !this.vol) return;
    const { w, h } = this.resize();
    gl.viewport(0, 0, w, h);
    const m = this.mat || {};
    const az = (m.sunAz ?? 135) * Math.PI / 180, el = (m.sunEl ?? 42) * Math.PI / 180;
    const sun = { x: Math.sin(az) * Math.cos(el), y: Math.sin(el), z: Math.cos(az) * Math.cos(el) };
    const b = cam.basis({});
    this.time += 1 / 60;
    gl.useProgram(this.prog);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_3D, this.texSdf);
    gl.uniform1i(this.u.uSdf, 0);
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_3D, this.texA);
    gl.uniform1i(this.u.uAttrA, 1);
    gl.activeTexture(gl.TEXTURE2);
    gl.bindTexture(gl.TEXTURE_3D, this.texB);
    gl.uniform1i(this.u.uAttrB, 2);
    gl.uniform1i(this.u.uN, this.vol.N);
    gl.uniform1f(this.u.uBoxH, this.vol.worldSize / 2);
    gl.uniform1f(this.u.uVoxel, this.vol.voxel);
    gl.uniform3f(this.u.uEye, b.eye.x, b.eye.y, b.eye.z);
    gl.uniform3f(this.u.uFwd, b.fwd.x, b.fwd.y, b.fwd.z);
    gl.uniform3f(this.u.uRight, b.right.x, b.right.y, b.right.z);
    gl.uniform3f(this.u.uUp, b.up.x, b.up.y, b.up.z);
    gl.uniform1f(this.u.uTanFov, Math.tan(cam.fov * Math.PI / 360));
    gl.uniform1f(this.u.uAspect, w / h);
    gl.uniform3f(this.u.uSunDir, sun.x, sun.y, sun.z);
    gl.uniform1f(this.u.uTime, this.time);
    gl.uniform1i(this.u.uPalette, m.palette ?? 0);
    gl.uniform1f(this.u.uSnow, m.snow ?? 0.34);
    gl.uniform1f(this.u.uVeg, m.veg ?? 0.8);
    gl.uniform1f(this.u.uStrataAmt, m.strataAmt ?? 0.7);
    gl.uniform1f(this.u.uAoAmt, m.aoAmt ?? 0.85);
    gl.uniform1f(this.u.uSeaLevel, m.seaLevel ?? -0.28);
    gl.uniform1f(this.u.uShowSea, (m.showSea ?? true) ? 1 : 0);
    gl.uniform4f(this.u.uBrush, this.brush.x, this.brush.y, this.brush.z, this.brush.r);
    gl.uniform1f(this.u.uShowEmit, this.showEmit ? 1 : 0);
    gl.bindVertexArray(this.quad);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    // rain streak overlay
    if (this.showRain && hydro && hydro.overlayCount > 2) {
      const n = Math.min(hydro.overlayCount, 24000);
      const O = hydro.overlay, OV = hydro.overlayVel;
      for (let k = 0; k < n; k++) {
        const x = O[k * 3], y = O[k * 3 + 1], z = O[k * 3 + 2];
        const vx = OV[k * 3], vy = OV[k * 3 + 1], vz = OV[k * 3 + 2];
        const sp = Math.hypot(vx, vy, vz) || 1;
        const L = 0.028;
        this.lineData[k * 6] = x; this.lineData[k * 6 + 1] = y; this.lineData[k * 6 + 2] = z;
        this.lineData[k * 6 + 3] = x - vx / sp * L;
        this.lineData[k * 6 + 4] = y - vy / sp * L;
        this.lineData[k * 6 + 5] = z - vz / sp * L;
      }
      gl.bindBuffer(gl.ARRAY_BUFFER, this.lineBuf);
      gl.bufferSubData(gl.ARRAY_BUFFER, 0, this.lineData, 0, n * 6);
      gl.useProgram(this.lineProg);
      gl.uniform3f(this.lu.uEye, b.eye.x, b.eye.y, b.eye.z);
      gl.uniform3f(this.lu.uFwd, b.fwd.x, b.fwd.y, b.fwd.z);
      gl.uniform3f(this.lu.uRight, b.right.x, b.right.y, b.right.z);
      gl.uniform3f(this.lu.uUp, b.up.x, b.up.y, b.up.z);
      gl.uniform1f(this.lu.uTanFov, Math.tan(cam.fov * Math.PI / 360));
      gl.uniform1f(this.lu.uAspect, w / h);
      gl.uniform1f(this.lu.uNear, cam.near);
      gl.uniform1f(this.lu.uFar, cam.far);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
      gl.bindVertexArray(this.lineVAO);
      gl.drawArrays(gl.LINES, 0, n * 2);
      gl.disable(gl.BLEND);
    }
    gl.bindVertexArray(null);
  }
}
;
      gl.drawArrays(gl.LINES, 0, n * 2);
      gl.disable(gl.BLEND);
    }
    gl.bindVertexArray(null);
  }
}
