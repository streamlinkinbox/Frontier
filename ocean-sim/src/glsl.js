// ---------------------------------------------------------------------------
// All GLSL lives here. Three.js ShaderMaterial style (GLSL1 syntax, auto
// upgraded to ESSL3 on WebGL2). Rules observed: fixed loop bounds, no
// reversed smoothstep, no pow() of negatives, no undeclared identifiers,
// no shadowing of three-injected attributes (uv, position, ...).
//
// NOTE: the spectrum travels as plain vec4 uniform arrays (uSpecA/uSpecB),
// not float textures — uniform arrays are the most driver-proof path for
// vertex-stage data in WebGL (this is how skinning ships everywhere).
// ---------------------------------------------------------------------------

export const NOISE = /* glsl */`
float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  float a = hash12(i);
  float b = hash12(i + vec2(1.0, 0.0));
  float c = hash12(i + vec2(0.0, 1.0));
  float d = hash12(i + vec2(1.0, 1.0));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm3(vec2 p) {
  float v = 0.0;
  float a = 0.5;
  for (int i = 0; i < 3; i++) { v += a * vnoise(p); p = p * 2.03 + vec2(17.3, 9.1); a *= 0.5; }
  return v;
}
float fbm4(vec2 p) {
  float v = 0.0;
  float a = 0.5;
  for (int i = 0; i < 4; i++) { v += a * vnoise(p); p = p * 2.03 + vec2(17.3, 9.1); a *= 0.5; }
  return v;
}
`;

// Analytic bathymetry. MUST match bathyJS() in bathy.js.
export const BATHY = /* glsl */`
uniform float uShoreX;
uniform float uShoreAngle;
uniform float uBeachSlope;
uniform float uReefX;
uniform float uReefAngle;
uniform float uReefDepth;
uniform float uReefWidth;
uniform float uBarEnable;
float bathymetry(vec2 p) {
  float shoreX = uShoreX + tan(uShoreAngle) * p.y;
  float d = (shoreX - p.x) * uBeachSlope;
  d += 1.2 * sin(p.x * 0.011 + 1.7) * sin(p.y * 0.013 + 0.4);
  d += 0.5 * sin(p.x * 0.043 + 0.3) * sin(p.y * 0.037 + 2.1);
  float reefX = uReefX + tan(uReefAngle) * p.y;
  float q = (p.x - reefX) / uReefWidth;
  float g = exp(-q * q);
  float barD = min(d, uReefDepth + 0.4 * sin(p.y * 0.05));
  d = mix(d, barD, clamp(g * uBarEnable, 0.0, 1.0));
  return min(max(d, -7.0), 42.0);
}
`;

// Shared sky model: gradient + sun + procedural clouds.
// Requires NOISE above it. Used by the dome, water reflections and particles.
export const SKYFN = /* glsl */`
uniform vec3 uSunDir;
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uGroundCol;
uniform vec3 uSunColor;
uniform float uCloudCover;
uniform float uTime;
vec3 skyGradient(vec3 dir) {
  float h = clamp(dir.y, -1.0, 1.0);
  vec3 col = (h >= 0.0)
    ? mix(uHorizon, uZenith, pow(h, 0.55))
    : mix(uHorizon, uGroundCol, clamp(-h * 3.0, 0.0, 1.0));
  float s = clamp(dot(dir, uSunDir), -1.0, 1.0);
  float disk = smoothstep(0.99935, 0.99975, s);
  float sc = clamp(s, 0.0, 1.0);
  float glow = pow(sc, 24.0) * 0.30 + pow(sc, 350.0) * 0.9;
  col += uSunColor * (disk * 3.0 + glow);
  col += uSunColor * pow(sc, 6.0) * (1.0 - abs(h)) * 0.22;
  return col;
}
vec3 skyColor(vec3 dir) {
  vec3 col = skyGradient(dir);
  if (dir.y > 0.015) {
    vec2 cuv = dir.xz / (dir.y + 0.18);
    float cl = fbm3(cuv * 1.35 + vec2(uTime * 0.004, uTime * 0.0016) + vec2(3.7, 1.3));
    float cover = smoothstep(1.0 - uCloudCover - 0.28, 1.0 - uCloudCover + 0.32, cl);
    float shade = vnoise(cuv * 3.1 - vec2(uTime * 0.006, 0.0));
    vec3 cloudCol = mix(uHorizon * 1.04 + vec3(0.05), vec3(1.0, 0.99, 0.97), 0.45);
    cloudCol *= 0.80 + 0.38 * shade;
    float s = clamp(dot(dir, uSunDir), 0.0, 1.0);
    cloudCol += uSunColor * pow(s, 10.0) * 0.30 * cover;
    col = mix(col, cloudCol, cover * smoothstep(0.015, 0.16, dir.y));
  }
  return col;
}
`;

// ---------------------------------------------------------------- ocean ---
export const OCEAN_VS = /* glsl */`
uniform float uTime;
uniform vec4 uSpecA[80];
uniform vec4 uSpecB[80];
uniform vec3 uCascadeAmp;
uniform vec4 uLabA0;
uniform vec4 uLabA1;
uniform vec4 uLabB0;
uniform vec4 uLabB1;
uniform vec2 uLabCenter;
uniform float uLabRadius;
uniform float uSurfOn;
uniform float uShoalGain;
uniform float uBreakAmp;
uniform float uBarrel;
uniform float uPeelSpeed;
uniform float uPeelWidth;
uniform float uPeelOffset;
uniform float uFoldGain;
uniform float uRelief;
attribute float aFlat;
varying vec3 vWorld;
varying vec3 vNormal;
varying vec4 vMisc;
${BATHY}
void labWave(vec4 P0, vec4 P1, vec2 xz, inout vec3 disp, inout float dYdx, inout float dYdz, inout float Jxx, inout float Jxz, inout float Jzz) {
  if (P1.w < 0.01 || P1.x <= 0.0) return;
  vec2 dd = (xz - uLabCenter) / uLabRadius;
  float env = exp(-dot(dd, dd) * 1.5);
  float amp = P1.x * env * uRelief;
  float ph = P0.z * dot(P0.xy, xz) - P0.w * uTime + P1.z;
  float s = sin(ph);
  float c = cos(ph);
  disp.y += amp * s;
  float QA = P1.y * amp;
  disp.x += P0.x * QA * c;
  disp.z += P0.y * QA * c;
  float wa = P0.z * amp;
  dYdx += P0.x * wa * c;
  dYdz += P0.y * wa * c;
  float qwa = P1.y * wa;
  Jxx += P0.x * P0.x * qwa * s;
  Jxz += P0.x * P0.y * qwa * s;
  Jzz += P0.y * P0.y * qwa * s;
}
void main() {
  vec3 pos = position;
  float depth = bathymetry(pos.xz);
  if (aFlat > 0.5) {
    vWorld = pos;
    vNormal = vec3(0.0, 1.0, 0.0);
    vMisc = vec4(0.0, 0.0, 0.5, 42.0);
    gl_Position = projectionMatrix * viewMatrix * vec4(vWorld, 1.0);
    return;
  }
  vec3 disp = vec3(0.0);
  float dYdx = 0.0;
  float dYdz = 0.0;
  float Jxx = 0.0;
  float Jxz = 0.0;
  float Jzz = 0.0;
  float hSwell = 0.0;
  float swellAmp = 0.0;
  float rEdge = max(abs(pos.x), abs(pos.z));
  for (int i = 0; i < 80; i++) {
    vec4 A = uSpecA[i];
    vec4 B = uSpecB[i];
    float casc = B.w;
    float wSwell = 1.0 - step(0.5, casc);
    float wSea = step(0.5, casc) * (1.0 - step(1.5, casc));
    float wChop = step(1.5, casc);
    // wavelength-relative fade: each component dies out only where the graded
    // grid can no longer sample it — long rollers survive to the horizon.
    float compFade = exp(-rEdge * A.z * 0.004);
    float cascAmp = (wSwell * uCascadeAmp.x + wSea * uCascadeAmp.y + wChop * uCascadeAmp.z) * compFade;
    float swellness = wSwell + 0.45 * wSea;
    float shoal = uSurfOn * (1.0 - smoothstep(2.0, 18.0, depth)) * swellness;
    float kEff = A.z / mix(1.0, 0.55, shoal);
    float green = pow(clamp(20.0 / max(depth, 0.8), 1.0, 6.0), 0.25);
    float ampE = B.x * cascAmp * (1.0 + shoal * (green - 1.0) * uShoalGain) * uRelief;
    float Qe = B.y * (1.0 + shoal * 1.5);
    float omE = A.w * mix(1.0, 0.8, shoal);
    float ph = kEff * dot(A.xy, pos.xz) - omE * uTime + B.z;
    float s = sin(ph);
    float c = cos(ph);
    float QA = Qe * ampE;
    disp.x += A.x * QA * c;
    disp.z += A.y * QA * c;
    disp.y += ampE * s;
    float wa = kEff * ampE;
    dYdx += A.x * wa * c;
    dYdz += A.y * wa * c;
    float qwa = Qe * wa;
    Jxx += A.x * A.x * qwa * s;
    Jxz += A.x * A.y * qwa * s;
    Jzz += A.y * A.y * qwa * s;
    hSwell += wSwell * ampE * s;
    swellAmp += wSwell * ampE;
  }
  labWave(uLabA0, uLabA1, pos.xz, disp, dYdx, dYdz, Jxx, Jxz, Jzz);
  labWave(uLabB0, uLabB1, pos.xz, disp, dYdx, dYdz, Jxx, Jxz, Jzz);
  float peelZ = mod(uTime * uPeelSpeed + uPeelOffset, 320.0) - 160.0;
  float pq = (pos.z - peelZ) / uPeelWidth;
  float pulse = exp(-pq * pq);
  float breakZone = uSurfOn * (1.0 - smoothstep(1.2, 6.0, depth));
  float crest = clamp(hSwell / max(swellAmp, 0.001) * 0.5 + 0.5, 0.0, 1.0);
  float Bm = breakZone * (0.30 + 0.70 * pulse) * smoothstep(0.45, 0.9, crest) * uBreakAmp;
  float sharp = crest * crest * crest;
  vec2 shoreDir = normalize(vec2(1.0, -tan(uReefAngle) * 0.5));
  disp.y += Bm * swellAmp * 0.9 * sharp;
  disp.xz += shoreDir * (Bm * uBarrel * swellAmp * sharp);
  float J = (1.0 - Jxx) * (1.0 - Jzz) - Jxz * Jxz;
  float fold = clamp((1.0 - J) * uFoldGain + Bm * 1.6, 0.0, 3.0);
  float edge = 1.0 - smoothstep(450.0, 640.0, rEdge);
  disp *= edge;
  dYdx *= edge;
  dYdz *= edge;
  float waterDepth = depth + disp.y;
  vWorld = vec3(pos.x + disp.x, disp.y, pos.z + disp.z);
  vNormal = normalize(vec3(-dYdx, 1.0, -dYdz));
  vMisc = vec4(fold * (0.35 + 0.65 * edge), Bm * edge, crest, waterDepth);
  gl_Position = projectionMatrix * viewMatrix * vec4(vWorld, 1.0);
}
`;

export const OCEAN_FS = /* glsl */`
uniform vec3 uDeepCol;
uniform vec3 uShallowCol;
uniform vec3 uSSSColor;
uniform vec3 uSkyAmb;
uniform vec2 uWindVec;
uniform float uFoamAmt;
uniform float uWhitecap;
uniform float uSSS;
uniform float uMicroAmp;
uniform float uFogDensity;
uniform vec3 uFogColor;
uniform sampler2D uFoamTex;
varying vec3 vWorld;
varying vec3 vNormal;
varying vec4 vMisc;
${NOISE}
${SKYFN}
vec2 microGrad(vec2 p, vec2 dir, float freq, float speed, float t) {
  float ph = dot(dir, p) * freq + t * speed;
  return dir * (cos(ph) * freq * 0.075);
}
void main() {
  vec3 toCam = cameraPosition - vWorld;
  float dist = length(toCam);
  vec3 V = toCam / max(dist, 0.001);
  vec2 flow = uWindVec * uTime;
  float nBig = fbm4(vWorld.xz * 0.33 + flow * 0.06);
  float nMid = fbm3(vWorld.xz * 1.35 - flow * 0.11 + 7.0);
  float shore = (1.0 - smoothstep(0.2, 2.4, vMisc.w)) * (0.55 + 0.45 * sin(uTime * 0.8 - vMisc.w * 2.2 + nBig * 4.0));
  float waterline = 1.0 - smoothstep(0.05, 0.5, abs(vMisc.w - 0.12));
  float clump = fbm3(vWorld.xz * 2.6 + flow * 0.23);
  // persistent simulated foam: coverage + fresh churn from the advection buffer
  vec2 fuv = vWorld.xz / 1300.0 + 0.5;
  float inR = step(abs(fuv.x - 0.5), 0.5) * step(abs(fuv.y - 0.5), 0.5);
  vec2 pfoam = texture2D(uFoamTex, clamp(fuv, 0.0, 1.0)).rg * inR;
  float foamDrive = vMisc.x * uWhitecap + vMisc.y * 1.35 + shore * 0.9 + waterline * 0.8
    + pfoam.x * 1.15 + pfoam.y * 0.75;
  float foamMask = smoothstep(0.42, 0.72, foamDrive - (nBig * 0.55 + nMid * 0.25) * 0.8 + (clump - 0.5) * 0.35);
  foamMask *= uFoamAmt;
  float mfade = exp(-dist * 0.0022);
  vec2 p = vWorld.xz;
  float t = uTime;
  vec2 grad = vec2(0.0);
  grad += microGrad(p, vec2(0.80, 0.60), 0.9, 1.4, t);
  grad += microGrad(p, vec2(-0.55, 0.84), 1.4, 1.7, t);
  grad += microGrad(p, vec2(0.30, -0.95), 2.0, 2.1, t);
  grad += microGrad(p, vec2(-0.90, -0.44), 2.8, 2.6, t);
  grad += microGrad(p, vec2(0.62, -0.78), 3.6, 3.0, t);
  grad += microGrad(p, vec2(-0.34, 0.94), 5.4, 3.6, t);
  grad += microGrad(p, vec2(0.94, 0.34), 7.8, 4.2, t);
  grad += (vec2(fbm3(p * 1.1 + flow * 0.15), fbm3(p * 1.1 + 31.7 - flow * 0.13)) - 0.5) * 1.1;
  vec3 N = normalize(vNormal + vec3(-grad.x, 0.0, -grad.y) * uMicroAmp * mfade);
  N = normalize(mix(N, vec3(0.0, 1.0, 0.0), clamp(foamMask, 0.0, 1.0) * 0.55));
  float absorbT = exp(-max(vMisc.w, 0.0) * 0.16);
  vec3 waterCol = mix(uDeepCol, uShallowCol, absorbT);
  waterCol += uSkyAmb * 0.12;
  // sun/shade modeling on the wave faces — this is what makes swell read
  float ndl = dot(N, uSunDir) * 0.5 + 0.5;
  waterCol *= 0.72 + 0.56 * ndl;
  // trough/crest depth cue: dark hollows, bright crowns
  waterCol *= 0.85 + 0.30 * vMisc.z;
  vec3 R = reflect(-V, N);
  R.y = abs(R.y);
  vec3 skyRef = skyColor(R);
  float ndv = max(dot(N, V), 0.0);
  float F = 0.02 + 0.94 * pow(1.0 - ndv, 4.0);
  vec3 col = mix(waterCol, skyRef, F);
  vec3 H = normalize(uSunDir + V);
  float ndh = max(dot(N, H), 0.0);
  float rough = clamp(0.08 + (1.0 - vMisc.z) * 0.06 + (1.0 - mfade) * 0.18, 0.05, 0.45);
  float spec = pow(ndh, mix(700.0, 60.0, rough)) * (0.6 + 0.4 * nMid);
  col += uSunColor * spec * 3.0 * (1.0 - clamp(foamMask, 0.0, 1.0));
  col += uSunColor * pow(ndh, 24.0) * 0.12 * (1.0 - clamp(foamMask, 0.0, 1.0));
  float toward = clamp(dot(V, uSunDir), 0.0, 1.0);
  float sss = pow(toward, 3.0) * (0.25 + 0.75 * vMisc.z) * (0.35 + 0.65 * (1.0 - absorbT));
  col += uSSSColor * sss * uSSS * (1.0 - clamp(foamMask, 0.0, 1.0));
  vec3 foamCol = vec3(0.93, 0.95, 0.96) * (0.62 + 0.38 * clump) + uSunColor * pow(toward, 6.0) * 0.22;
  foamCol += uSkyAmb * 0.25;
  col = mix(col, foamCol, clamp(foamMask, 0.0, 1.0));
  float f2 = 1.0 - exp(-dist * uFogDensity - dist * dist * uFogDensity * uFogDensity * 0.35);
  col = mix(col, uFogColor, clamp(f2, 0.0, 1.0));
  float alphaW = mix(0.55, 1.0, 1.0 - absorbT);
  float alpha = mix(alphaW, 1.0, clamp(foamMask, 0.0, 1.0));
  gl_FragColor = vec4(col, alpha);
  #include <tonemapping_fragment>
  #include <colorspace_fragment>
}
`;

// ---------------------------------------------------------------- foam ---
export const FOAM_VS = /* glsl */`
varying vec2 vUv;
void main() {
  vUv = uv;
  gl_Position = vec4(position.xy, 0.0, 1.0);
}
`;

// Foam advection buffer: inject from swell/sea fold + breaker + waterline
// physics, advect with surface flow, diffuse, decay. World mapping:
// wxz = (vUv - 0.5) * 1300 — the ocean shader uses the identical mapping.
export const FOAM_FS = /* glsl */`
uniform sampler2D uPrev;
uniform vec4 uSpecA[80];
uniform vec4 uSpecB[80];
uniform vec3 uCascadeAmp;
uniform float uSurfOn;
uniform float uShoalGain;
uniform float uBreakAmp;
uniform float uPeelSpeed;
uniform float uPeelWidth;
uniform float uPeelOffset;
uniform float uFoldGain;
uniform float uRelief;
uniform float uDt;
uniform vec2 uFlow;
uniform float uTime;
varying vec2 vUv;
${BATHY}
void main() {
  vec2 wxz = (vUv - 0.5) * 1300.0;
  float depth = bathymetry(wxz);
  float Jxx = 0.0;
  float Jxz = 0.0;
  float Jzz = 0.0;
  float hSwell = 0.0;
  float swellAmp = 0.0;
  for (int i = 0; i < 28; i++) {
    vec4 A = uSpecA[i];
    vec4 B = uSpecB[i];
    float wSwell = 1.0 - step(0.5, B.w);
    float cascAmp = wSwell * uCascadeAmp.x + (1.0 - wSwell) * uCascadeAmp.y;
    float swellness = wSwell + 0.45 * (1.0 - wSwell);
    float shoal = uSurfOn * (1.0 - smoothstep(2.0, 18.0, depth)) * swellness;
    float kEff = A.z / mix(1.0, 0.55, shoal);
    float green = pow(clamp(20.0 / max(depth, 0.8), 1.0, 6.0), 0.25);
    float ampE = B.x * cascAmp * (1.0 + shoal * (green - 1.0) * uShoalGain) * uRelief;
    float Qe = B.y * (1.0 + shoal * 1.5);
    float omE = A.w * mix(1.0, 0.8, shoal);
    float ph = kEff * dot(A.xy, wxz) - omE * uTime + B.z;
    float s = sin(ph);
    float qwa = Qe * kEff * ampE;
    Jxx += A.x * A.x * qwa * s;
    Jxz += A.x * A.y * qwa * s;
    Jzz += A.y * A.y * qwa * s;
    hSwell += wSwell * ampE * s;
    swellAmp += wSwell * ampE;
  }
  float J = (1.0 - Jxx) * (1.0 - Jzz) - Jxz * Jxz;
  float fold = (1.0 - J) * uFoldGain;
  float peelZ = mod(uTime * uPeelSpeed + uPeelOffset, 320.0) - 160.0;
  float pq = (wxz.y - peelZ) / uPeelWidth;
  float pulse = exp(-pq * pq);
  float breakZone = uSurfOn * (1.0 - smoothstep(1.2, 6.0, depth));
  float crest = clamp(hSwell / max(swellAmp, 0.001) * 0.5 + 0.5, 0.0, 1.0);
  float Bm = breakZone * (0.30 + 0.70 * pulse) * smoothstep(0.45, 0.9, crest) * uBreakAmp;
  float wlq = (depth - 0.3) / 0.9;
  float wl = exp(-wlq * wlq) * (0.45 + 0.55 * (0.5 + 0.5 * sin(uTime * 0.8 - depth * 2.2)));
  float inject = smoothstep(0.5, 1.1, fold) * 0.9 + Bm * 2.2 + wl * 0.7;
  float freshIn = clamp(Bm * 1.6 + smoothstep(0.9, 1.6, fold), 0.0, 1.5);
  vec2 puv = vUv - uFlow * uDt / 1300.0;
  vec2 cx = vec2(1.5 / 256.0, 0.0);
  vec2 cy = vec2(0.0, 1.5 / 256.0);
  vec2 pr = texture2D(uPrev, puv).rg;
  pr += texture2D(uPrev, puv + cx).rg;
  pr += texture2D(uPrev, puv - cx).rg;
  pr += texture2D(uPrev, puv + cy).rg;
  pr += texture2D(uPrev, puv - cy).rg;
  pr /= 5.0;
  float cov = max(pr.x * exp(-uDt / 18.0), clamp(inject, 0.0, 1.5));
  float fresh = max(pr.y * exp(-uDt / 1.6), freshIn);
  float edge = smoothstep(0.0, 0.03, vUv.x) * (1.0 - smoothstep(0.97, 1.0, vUv.x))
    * smoothstep(0.0, 0.03, vUv.y) * (1.0 - smoothstep(0.97, 1.0, vUv.y));
  cov *= edge;
  fresh *= edge;
  cov *= smoothstep(-1.6, -0.6, depth);
  gl_FragColor = vec4(cov, fresh, 0.0, 1.0);
}
`;

// ------------------------------------------------------------------- sky ---
export const SKY_VS = /* glsl */`
varying vec3 vWorld;
void main() {
  vec4 wp = modelMatrix * vec4(position, 1.0);
  vWorld = wp.xyz;
  vec4 mv = viewMatrix * wp;
  gl_Position = projectionMatrix * mv;
  gl_Position.z = gl_Position.w * 0.99999;
}
`;

export const SKY_FS = /* glsl */`
varying vec3 vWorld;
${NOISE}
${SKYFN}
void main() {
  vec3 dir = normalize(vWorld - cameraPosition);
  vec3 col = skyColor(dir);
  col = mix(col, uGroundCol * 0.6, 1.0 - smoothstep(-0.2, 0.0, dir.y));
  gl_FragColor = vec4(col, 1.0);
  #include <tonemapping_fragment>
  #include <colorspace_fragment>
}
`;

// ---------------------------------------------------------------- seabed ---
export const SEABED_VS = /* glsl */`
varying vec3 vWorld;
void main() {
  vec4 wp = modelMatrix * vec4(position, 1.0);
  vWorld = wp.xyz;
  gl_Position = projectionMatrix * viewMatrix * wp;
}
`;

export const SEABED_FS = /* glsl */`
uniform vec3 uSandDry;
uniform vec3 uSandWet;
uniform vec3 uSandDeep;
uniform vec3 uCausticCol;
uniform float uCaustic;
uniform float uFogDensity;
uniform vec3 uFogColor;
uniform float uTime;
varying vec3 vWorld;
${NOISE}
float caustic(vec2 p, float t) {
  float c1 = sin(p.x * 1.4 + t * 0.9) + sin(p.y * 1.6 - t * 1.15) + sin((p.x + p.y) * 0.8 + t * 0.65);
  float w = 1.0 - abs(c1) * 0.30;
  float c2 = sin(p.x * 2.3 - t * 1.3 + 1.7) + sin(p.y * 2.9 + t * 1.1) + sin((p.x - p.y) * 1.7 - t * 0.8);
  float w2 = 1.0 - abs(c2) * 0.30;
  return pow(clamp(w * w2 * 0.5, 0.0, 2.0), 3.0);
}
void main() {
  float groundY = vWorld.y;
  float wd = max(-groundY, 0.0);
  vec3 sand = mix(uSandWet, uSandDry, smoothstep(0.0, 0.9, groundY));
  sand = mix(uSandDeep, sand, exp(-wd * 0.22));
  float grain = fbm3(vWorld.xz * 0.8);
  sand *= 0.85 + 0.3 * grain;
  float ca = caustic(vWorld.xz * 0.35, uTime);
  float caFade = exp(-wd * 0.30) * uCaustic * (1.0 - smoothstep(0.0, 0.6, groundY));
  sand += uCausticCol * ca * caFade;
  float dist = length(cameraPosition - vWorld);
  float f2 = 1.0 - exp(-dist * uFogDensity - dist * dist * uFogDensity * uFogDensity * 0.35);
  sand = mix(sand, uFogColor, clamp(f2, 0.0, 1.0));
  gl_FragColor = vec4(sand, 1.0);
  #include <tonemapping_fragment>
  #include <colorspace_fragment>
}
`;

// ----------------------------------------------------------------- spray ---
// Breaker spray only: small, soft, short-lived, peel-gated. Persistent foam
// is handled by the FoamSim advection buffer, not sprites.
export const POINTS_VS = /* glsl */`
uniform float uTime;
uniform vec4 uSpecA[80];
uniform vec4 uSpecB[80];
uniform vec2 uDrift;
uniform float uPointScale;
uniform float uSprayAmt;
uniform float uSwellK;
uniform float uSurfOn;
uniform float uPeelSpeed;
uniform float uPeelWidth;
uniform float uPeelOffset;
attribute vec4 aSeed;
varying float vAlpha;
varying float vShade;
varying float vDist;
${BATHY}
void main() {
  float r1 = aSeed.x;
  float r2 = aSeed.y;
  float r3 = aSeed.z;
  float cls = aSeed.w;
  vec3 anchor = position;
  float life = 0.7 + 0.45 * r2;
  float tau = fract(uTime / life + r1 * 7.31 + r3 * 3.7);
  float age = tau * life;
  float h = 0.0;
  for (int i = 0; i < 8; i++) {
    vec4 A = uSpecA[i];
    vec4 B = uSpecB[i];
    h += B.x * sin(A.z * dot(A.xy, anchor.xz) - A.w * uTime + B.z);
  }
  h *= uSwellK;
  float depth = bathymetry(anchor.xz);
  float shoalG = uSurfOn * (1.0 - smoothstep(2.0, 18.0, depth));
  float green = pow(clamp(20.0 / max(depth, 0.8), 1.0, 6.0), 0.25);
  h *= (1.0 + shoalG * (green - 1.0));
  float peelZ = mod(uTime * uPeelSpeed + uPeelOffset, 320.0) - 160.0;
  float pq = (anchor.z - peelZ) / uPeelWidth;
  float pulse = exp(-pq * pq);
  float vis = uSprayAmt;
  if (cls < 0.5) { vis = step(r3, (0.15 + 0.85 * pulse) * uSprayAmt); }
  vec3 v0 = vec3(1.2 + 1.8 * r2, 3.0 + 2.6 * r1 * (0.4 + 0.6 * pulse), (r3 - 0.5) * 2.4);
  vec3 p = anchor + vec3(0.0, h + 0.3, 0.0) + v0 * age + vec3(0.0, -6.0, 0.0) * age * age;
  p.xz += uDrift * age * 0.45;
  float size = mix(0.30, 0.07, tau);
  float alpha = (1.0 - tau) * smoothstep(0.0, 0.10, tau) * 0.55;
  alpha *= clamp(vis, 0.0, 1.0);
  vec4 mv = viewMatrix * vec4(p, 1.0);
  float dist = max(-mv.z, 0.5);
  alpha *= smoothstep(2.0, 9.0, dist) * (1.0 - smoothstep(180.0, 320.0, dist));
  gl_PointSize = clamp(size * uPointScale / dist, 0.0, 64.0);
  vAlpha = alpha;
  vShade = r2;
  vDist = dist;
  gl_Position = projectionMatrix * mv;
}
`;

export const POINTS_FS = /* glsl */`
uniform vec3 uSunColor;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uTime;
varying float vAlpha;
varying float vShade;
varying float vDist;
${NOISE}
void main() {
  if (vAlpha < 0.004) discard;
  vec2 pc = gl_PointCoord - 0.5;
  float d = length(pc) * 2.0;
  float n = vnoise(pc * 5.0 + vShade * 37.0 + uTime * 0.4);
  float n2 = vnoise(pc * 9.0 - vShade * 21.0 - uTime * 0.5);
  float body = 1.0 - smoothstep(0.15, 0.95, d + (n - 0.5) * 0.5);
  float a = body * (0.35 + 0.40 * n2) * vAlpha;
  if (a < 0.01) discard;
  vec3 col = mix(vec3(0.84, 0.89, 0.91), vec3(1.0), n2);
  col *= 0.78 + 0.38 * vShade;
  col += uSunColor * 0.10;
  float f = 1.0 - exp(-vDist * uFogDensity * 1.2);
  col = mix(col, uFogColor, clamp(f, 0.0, 1.0));
  gl_FragColor = vec4(col, a);
  #include <tonemapping_fragment>
  #include <colorspace_fragment>
}
`;

// Debug contour view (?contour=1): paints the ocean by TRUE VERTEX HEIGHT.
// Blue = troughs, white = mean level, red = crests, black lines at every
// odd metre. If displacement is dead the sea renders one flat grey — if it
// is alive you see wandering contour stripes. Pure diagnosis, no physics.
export const OCEAN_CONTOUR_FS = /* glsl */`
varying vec3 vWorld;
varying vec3 vNormal;
varying vec4 vMisc;
void main() {
  float y = vWorld.y;
  vec3 col = mix(vec3(0.15, 0.35, 1.0), vec3(1.0, 0.2, 0.15), clamp(y * 0.25 + 0.5, 0.0, 1.0));
  float band = abs(fract(y * 0.5 + 0.5) - 0.5) * 2.0;
  col = mix(col, vec3(0.02), smoothstep(0.93, 1.0, band) * 0.75);
  gl_FragColor = vec4(col, 1.0);
  #include <tonemapping_fragment>
  #include <colorspace_fragment>
}
`;
