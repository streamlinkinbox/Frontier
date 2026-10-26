// Procedural rock surface injected into MeshStandardMaterial (keeps Three's PBR lighting,
// shadows, environment and fog). Everything is analytic and world-space — no textures, no UVs.
//
// Layers (each contributes height + analytic gradient → one normal perturbation):
//   1. strata colour bands with geological dip, bed seams and oxide pockets
//   2. aggregate grain  — 3D value noise, analytic gradient
//   3. mineral flakes   — three stacked plate layers, each with its own colour, size, height,
//                          shape and crystal share (crystals glint); revealed by the hardness
//                          channel and oxidised by the flow channel
//   4. spalling         — irregular patches where a thin sheet flaked off: a shallow bevelled
//                          step (height + normal), paler fresh rock, weathering pits
//   5. sediment         — the fill laid in beds: facies sorted by stream power, beds that onlap the
//                          valley sides with a parting and cross-lamination in each, current ripples,
//                          mud cracks, and its own grain
//   6. cover            — runoff staining, pebble gravel, vegetation, moss, snow
//
// Vertex aux = (deposit, flow, hardness, cavity) from the erosion pipeline (zeros on rocks).

import * as THREE from 'three';
import { makeBedTable } from './strata-model.js';

const vertexHead = /* glsl */`
attribute vec4 aux;
attribute vec4 aux2;
varying vec4 vAux;
varying vec4 vAux2;
varying vec3 vWorldPos;
`;

const vertexBody = /* glsl */`
{
  vec4 wp4 = vec4( transformed, 1.0 );
  #ifdef USE_INSTANCING
    wp4 = instanceMatrix * wp4;
  #endif
  vWorldPos = ( modelMatrix * wp4 ).xyz;
  vAux = aux;
  vAux2 = aux2;
}
`;

const fragmentHead = /* glsl */`
varying vec4 vAux;
varying vec4 vAux2;
varying vec3 vWorldPos;
uniform vec3 uRockA, uRockB, uRockC, uFresh, uOxide, uGrassA, uGrassB, uDry, uMoss, uSnow, uGravel, uRoad, uSilt;
uniform float uShoreWet, uRoadOn, uBedOn;
uniform float uStrataBand, uStrataContrast, uDipX, uDipZ, uSeamStrength, uSeamWidth, uLaminae, uBedGradient, uHardnessTint;
uniform sampler2D uBedTex;   // stratigraphic column: (top, hardness, tint, thickness) per bed
uniform int uBedCount;
uniform float uBedBase, uWorldSize, uBedLateral;
uniform float uGrainSize, uGrainStrength, uGrainContrast, uGrainFineness;
uniform float uOxideAmount, uOxideScale;
uniform float uCavityStrength;
uniform float uFlakeStrength, uFlakeSheen, uFlakeEdge, uFlakeOxide, uFlakeSparkle;
uniform vec3 uFlakeCol[3];
uniform float uFlakeOn[3], uFlakeSize[3], uFlakeDensity[3], uFlakeHeight[3], uFlakeCrystal[3], uFlakeVar[3], uFlakeShape[3], uFlakeReveal[3];
uniform float uPeelStrength, uPeelScale, uPeelCoverage, uPeelThickness, uPeelBedding, uPeelFresh, uPeelPits, uPeelShadow, uPeelSecond;
uniform float uWetness, uStreakScale, uStreakAmount;
uniform float uGravelAmount, uGravelScale, uGravelRelief, uGravelVariation;
uniform vec3 uSunDir;
uniform float uVegetation, uVegScale, uVegSlope, uVegPatchiness, uDryness;
uniform float uSoilAmount, uSoilSlopeCos, uSoilClods, uSoilMoisture, uSoilAlluvium;
uniform float uSedAmount, uSedBands, uSedBedScale, uSedLaminae, uSedFacies, uSedGrainBias, uSedRipple, uSedRippleScale, uSedCracks, uSedGrain, uSedSlopeCos, uSedDirRad;
uniform vec3 uSedGravel, uSedSand, uSedSilt;
uniform vec3 uSoilColor, uSoilLight;
uniform float uMossiness, uMossScale;
uniform float uSnowLine, uSnowSlopeCos, uSnowSoftness, uSnowRoughness;
uniform float uBumpScale, uBaseRoughness;
uniform float uJointStrength, uJointSpacing, uJointWidth, uJointDepth, uJointStagger, uJointBlocks, uJointDropout;
uniform float uTexWarp, uJointBend, uJointVary, uJointDomain, uFacePatch;
uniform float uSeaLevel, uSeed, uIsRock, uDebugView;
uniform float uPaintWater, uWaterClear;
uniform vec3 uWaterDeep, uWaterShallow;

// lateral bed-thickness jitter — must match bedJitter() in strata-model.js
float bedJitter( float x, float z ) {
  float a = sin( x * 0.0091 + 0.7 * sin( z * 0.0063 + 1.3 ) );
  float b = cos( z * 0.0077 + 0.5 * sin( x * 0.0052 + 0.4 ) );
  return 1.0 + uBedLateral * ( 0.6 * a + 0.4 * b );
}
// bed containing tilted elevation t (binary search in the column texture)
vec4 bedLookup( float t, out float base ) {
  int lo = 0, hi = uBedCount - 1;
  for ( int k = 0; k < 13; k++ ) {
    if ( lo >= hi ) break;
    int mid = ( lo + hi ) / 2;
    if ( t < texelFetch( uBedTex, ivec2( mid, 0 ), 0 ).x ) hi = mid; else lo = mid + 1;
  }
  vec4 b = texelFetch( uBedTex, ivec2( lo, 0 ), 0 );
  base = b.x - b.w;
  return b;
}

float cgHash( uvec3 q ) {
  uint h = q.x * 1597334677u ^ q.y * 3812015801u ^ q.z * 2798796415u;
  h = ( h ^ ( h >> 16u ) ) * 2246822519u;
  h = ( h ^ ( h >> 13u ) ) * 3266489917u;
  return float( h ^ ( h >> 16u ) ) / 4294967295.0;
}
float cgHash2( ivec2 c, float seed ) { return cgHash( uvec3( ivec3( c, int( seed * 7.0 ) + 1000 ) ) ); }
float cgHash3( ivec3 c ) { return cgHash( uvec3( c + ivec3( 4096 ) ) ); }

// 3D value noise with analytic gradient: (value, dx, dy, dz)
vec4 cgNoise3( vec3 p ) {
  ivec3 cell = ivec3( floor( p ) );
  vec3 f = fract( p );
  vec3 u = f * f * f * ( f * ( f * 6.0 - 15.0 ) + 10.0 );
  vec3 du = 30.0 * f * f * ( f * ( f - 2.0 ) + 1.0 );
  float a = cgHash3( cell ), b = cgHash3( cell + ivec3( 1, 0, 0 ) );
  float c = cgHash3( cell + ivec3( 0, 1, 0 ) ), d = cgHash3( cell + ivec3( 1, 1, 0 ) );
  float e = cgHash3( cell + ivec3( 0, 0, 1 ) ), f1 = cgHash3( cell + ivec3( 1, 0, 1 ) );
  float g = cgHash3( cell + ivec3( 0, 1, 1 ) ), h = cgHash3( cell + ivec3( 1, 1, 1 ) );
  float ab = mix( a, b, u.x ), cd = mix( c, d, u.x ), ef = mix( e, f1, u.x ), gh = mix( g, h, u.x );
  float lower = mix( ab, cd, u.y ), upper = mix( ef, gh, u.y );
  vec3 grad = vec3(
    mix( mix( b - a, d - c, u.y ), mix( f1 - e, h - g, u.y ), u.z ),
    mix( cd - ab, gh - ef, u.z ),
    upper - lower ) * du;
  return vec4( mix( lower, upper, u.z ), grad );
}
vec4 cgFbm3( vec3 p, int octaves ) {
  vec4 sum = vec4( 0.0 );
  float amp = 0.5, freq = 1.0, norm = 0.0;
  for ( int o = 0; o < 5; o++ ) {
    if ( o >= octaves ) break;
    vec4 n = cgNoise3( p * freq + float( o ) * 17.3 );
    sum += vec4( n.x, n.yzw * freq ) * amp;
    norm += amp;
    amp *= 0.5; freq *= 2.1;
  }
  return sum / norm;
}

// ---- spalling ("peel") ---------------------------------------------------------------------
// Thin sheets flaked off the face: irregular patches recessed by the sheet thickness with a soft
// bevelled rim (height + analytic normal), a second generation of smaller spalls stepping down
// inside and around them, and sparse weathering pits. No cells, no Voronoi.
struct SpallOut { float h; vec2 g; float fresh; float rim; float pit; };
vec3 spallNoise( vec2 p, float seed ) {   // (value, d/dx, d/dy) of a 3-octave blobby field
  vec4 a = cgNoise3( vec3( p, seed ) );
  vec4 b = cgNoise3( vec3( p * 2.3 + 7.1, seed + 3.0 ) );
  vec4 c = cgNoise3( vec3( p * 5.1 + 2.9, seed + 6.0 ) );
  return vec3( a.x * 0.6 + b.x * 0.28 + c.x * 0.12, a.yz * 0.6 + b.yz * ( 2.3 * 0.28 ) + c.yz * ( 5.1 * 0.12 ) );
}
SpallOut spallLayer( vec2 p, float scale, float coverage, float second, float pits, float aa, float seed ) {
  SpallOut o; o.h = 0.0; o.g = vec2( 0.0 ); o.fresh = 0.0; o.rim = 0.0; o.pit = 0.0;
  vec2 q = p / scale;
  vec3 n1 = spallNoise( q, seed );
  float th = mix( 0.72, 0.44, coverage );
  float w = max( 0.03, aa * 1.2 );
  float t = clamp( ( n1.x - th ) / w + 0.5, 0.0, 1.0 );
  float s1 = t * t * ( 3.0 - 2.0 * t );
  float ds1 = 6.0 * t * ( 1.0 - t ) / w;
  vec2 g1 = ds1 * n1.yz / scale;
  // second generation: smaller spalls, stepping down inside the first and dotted around them
  vec3 n2 = spallNoise( q * 2.9 + 11.0, seed + 29.0 );
  float th2 = mix( 0.78, 0.5, coverage );
  float w2 = max( 0.035, aa * 2.9 * 1.2 );
  float t2 = clamp( ( n2.x - th2 ) / w2 + 0.5, 0.0, 1.0 );
  float s2 = ( t2 * t2 * ( 3.0 - 2.0 * t2 ) ) * second;
  float ds2 = 6.0 * t2 * ( 1.0 - t2 ) / w2 * second;
  vec2 g2 = ds2 * n2.yz * 2.9 / scale;
  o.h = -( s1 + 0.6 * s2 );
  o.g = -( g1 + 0.6 * g2 );
  o.fresh = clamp( s1 + 0.7 * s2, 0.0, 1.0 );
  o.rim = clamp( ( 6.0 * t * ( 1.0 - t ) + 6.0 * t2 * ( 1.0 - t2 ) * second ) / 1.5, 0.0, 1.0 );
  // weathering pits: sparse small craters
  if ( pits > 0.001 ) {
    float ps = scale * 0.14;
    vec2 pq = p / ps;
    ivec2 pc = ivec2( floor( pq ) );
    vec2 pf = fract( pq );
    float paa = aa * scale / ps;
    for ( int y = -1; y <= 1; y++ ) for ( int x = -1; x <= 1; x++ ) {
      ivec2 c = pc + ivec2( x, y );
      float pr = cgHash2( c, seed + 51.0 );
      if ( pr > pits * 0.35 ) continue;
      vec2 centre = vec2( x, y ) + vec2( cgHash2( c, seed + 52.0 ), cgHash2( c, seed + 53.0 ) );
      vec2 dv = pf - centre;
      float d = length( dv );
      float r = 0.12 + 0.16 * cgHash2( c, seed + 54.0 );
      if ( d > r + paa ) continue;
      float x1 = clamp( d / r, 0.0, 1.0 );
      float crater = 1.0 - x1 * x1 * ( 3.0 - 2.0 * x1 );
      float dcr = -6.0 * x1 * ( 1.0 - x1 ) / r;
      vec2 gc = dcr * ( d > 1e-4 ? dv / d : vec2( 0.0 ) ) / ps;
      o.h -= 0.5 * crater;
      o.g += 0.5 * gc;
      o.pit = max( o.pit, crater );
    }
  }
  return o;
}

// ---- mineral flakes ------------------------------------------------------------------------
// Three stacked layers of plates. Every layer has its own colour, size, density, height, shape and
// crystal share; every plate gets a small tint jitter around the layer colour, its own tilt and a
// bevelled rim. The front-most plate wins so plates occlude rather than add.
struct FlakeOut { float cover; vec2 g; vec3 jitter; float crystal; float edge; vec2 facet; };
FlakeOut flakeLayer( vec2 p, float scale, float density, float shape, float aa, float seed ) {
  FlakeOut o; o.cover = 0.0; o.g = vec2( 0.0 ); o.jitter = vec3( 0.5 ); o.crystal = 1.0; o.edge = 0.0; o.facet = vec2( 0.0 );
  vec2 q = p / scale;
  ivec2 cell = ivec2( floor( q ) );
  vec2 f = fract( q );
  float front = -1.0;
  for ( int y = -1; y <= 1; y++ ) for ( int x = -1; x <= 1; x++ ) {
    ivec2 c = cell + ivec2( x, y );
    float pr = cgHash2( c, seed );
    if ( pr < 1.0 - density ) continue;
    vec2 centre = vec2( x, y ) + vec2( cgHash2( c, seed + 1.0 ), cgHash2( c, seed + 2.0 ) );
    float ang = cgHash2( c, seed + 3.0 ) * 6.2831853;
    float ca = cos( ang ), sa = sin( ang );
    mat2 R = mat2( ca, sa, -sa, ca );
    vec2 local = R * ( f - centre );
    float h4 = cgHash2( c, seed + 4.0 ), h5 = cgHash2( c, seed + 5.0 );
    vec2 axes = vec2( 0.8 + h4 * 0.65, 0.75 + pr * 0.8 );
    vec2 d = abs( local ) * axes;
    // angular: box ∩ bevel ∩ oblique cut; rounded: ellipse; shape blends them.
    float bevel = dot( d, vec2( 0.6 + pr * 0.25, 0.55 + h5 * 0.35 ) );
    float cut = dot( local, normalize( vec2( pr - 0.35, 0.65 ) ) ) * 1.18;
    float dPoly = max( max( d.x, d.y ), max( bevel, cut ) );
    float dRound = length( local * axes ) * 0.92;
    float dist = mix( dPoly, dRound, shape );
    float radius = 0.27 + h4 * 0.38;
    float cover = 1.0 - smoothstep( radius - aa, radius + aa, dist );
    if ( cover > 0.001 && pr > front ) {
      front = pr;
      o.cover = cover;
      vec2 tilt = ( vec2( h5, cgHash2( c, seed + 6.0 ) ) - 0.5 ) * 2.0;
      vec2 edgeDir = transpose( R ) * ( sign( local ) * axes );
      float edge = 1.0 - smoothstep( aa, aa + 0.06, abs( dist - radius ) );
      o.edge = edge * cover;
      // plate tilt plus a bevel at the rim [1/cell]
      o.g = transpose( R ) * tilt * cover * 4.0 + edgeDir * o.edge * 3.0;
      o.jitter = vec3( cgHash2( c, seed + 8.0 ), cgHash2( c, seed + 9.0 ), cgHash2( c, seed + 10.0 ) );
      o.crystal = cgHash2( c, seed + 11.0 );
      o.facet = ( vec2( cgHash2( c, seed + 12.0 ), cgHash2( c, seed + 13.0 ) ) - 0.5 ) * 2.0;
    }
  }
  return o;
}

// ---- gravel pebbles ------------------------------------------------------------------------
// Rounded stones packed in a sandy matrix: two generations of jittered-cell ellipsoid domes with a
// proper dome normal and per-stone tone. Front stone wins. Anti-aliased by the pixel footprint.
struct PebbleOut { float cover; vec2 g; float tone; float hue; };
PebbleOut pebbleLayer( vec2 p, float size, float aa, float seed ) {
  PebbleOut o; o.cover = 0.0; o.g = vec2( 0.0 ); o.tone = 0.5; o.hue = 0.5;
  vec2 q = p / size;
  ivec2 cell = ivec2( floor( q ) );
  vec2 f = fract( q );
  float front = -1.0;
  for ( int y = -1; y <= 1; y++ ) for ( int x = -1; x <= 1; x++ ) {
    ivec2 c = cell + ivec2( x, y );
    float pr = cgHash2( c, seed );
    if ( pr < 0.22 ) continue;
    vec2 centre = vec2( x, y ) + 0.5 + ( vec2( cgHash2( c, seed + 1.0 ), cgHash2( c, seed + 2.0 ) ) - 0.5 ) * 0.55;
    float ang = cgHash2( c, seed + 3.0 ) * 3.1415926;
    float ca = cos( ang ), sa = sin( ang );
    mat2 R = mat2( ca, sa, -sa, ca );
    vec2 local = R * ( f - centre );
    vec2 axes = vec2( 1.0, 0.65 + 0.5 * cgHash2( c, seed + 4.0 ) );
    vec2 le = local * axes;
    float d = length( le );
    float r = 0.3 + 0.2 * cgHash2( c, seed + 5.0 );
    float cover = 1.0 - smoothstep( r - aa, r + aa, d );
    if ( cover > 0.001 && pr > front ) {
      front = pr;
      o.cover = cover;
      float x1 = clamp( d / r, 0.0, 0.97 );
      float slope = x1 / sqrt( 1.0 - x1 * x1 );                 // dome: h = r·sqrt(1−x²)
      vec2 dir = d > 1e-4 ? transpose( R ) * ( le * axes / d ) : vec2( 0.0 );
      o.g = -dir * slope * cover * 0.6;                         // [1/cell], scaled by height later
      o.tone = cgHash2( c, seed + 6.0 );
      o.hue = cgHash2( c, seed + 7.0 );
    }
  }
  return o;
}

// ---- breaking the lattice -------------------------------------------------------------------
// One spacing, aligned to the world axes, is the tell of a generated cliff: the eye reads wallpaper
// however good the noise is. Three things undo it. The sample coordinates of every pattern that is
// not bedding are warped and locally stretched, so cells bend and change size from patch to patch.
// The joint frame swings slowly across the massif, the way joint sets curve around a dome. And a
// coarse domain hash — the same grid the true-3D blocks are varied by — sets the block size and how
// much weathering detail that panel carries, so one face is massive slabs and the next is closely
// jointed and busy.
vec2 cgWarp2( vec3 wp, float amp, float seed ) {
  return vec2( cgNoise3( wp * 0.011 + seed ).x, cgNoise3( wp * 0.011 + seed + 31.7 ).x ) * amp;
}
float cgPatch( vec3 wp, float scale, float seed ) { return clamp( cgNoise3( wp / scale + seed ).x * 0.5 + 0.5, 0.0, 1.0 ); }

struct Surface { vec3 albedo; vec3 normalW; float roughness; float ao; float metal; vec3 emissive; };

Surface evaluateCliffSurface( vec3 wp, vec3 n, vec4 aux, vec4 aux2 ) {
  float road = aux2.x * ( 1.0 - uIsRock ) * uRoadOn;
  float riverBed = aux2.y * ( 1.0 - uIsRock ) * uBedOn;
  float lakeBed = aux2.z * ( 1.0 - uIsRock ) * uBedOn; // lake beds and fluvial alluvium: silt / sand
  float localWater = mix( -1.0e6, aux2.w, 1.0 - uIsRock );
  float waterLine = max( uSeaLevel, localWater );
  float bedCover = clamp( max( road, max( riverBed, lakeBed ) ), 0.0, 1.0 );
  float deposit = aux.x * ( 1.0 - uIsRock );
  float flow = aux.y * ( 1.0 - uIsRock );
  float hardness = aux.z;
  float cavity = aux.w;
  float footprint = max( length( dFdx( wp ) ), length( dFdy( wp ) ) ) + 1e-5;
  float wall = 1.0 - abs( n.y );
  // the shared deformation of every pattern that is not bedding, and the mask that lets some
  // panels be massive and others busy
  vec2 cgFreq = vec2( 1.0 ) + ( vec2( cgPatch( wp, 170.0, 3.7 ), cgPatch( wp, 215.0, 11.3 ) ) - 0.5 ) * 0.8 * uTexWarp;
  vec2 cgOff = cgWarp2( wp, 4.5 * uTexWarp, 0.0 );
  float faceVar = mix( 1.0, 0.3 + 1.35 * cgPatch( wp, 145.0, 21.0 ), uFacePatch );

  // ---- cover masks first, so rock micro-detail only grows where rock is exposed ----------------
  float speckle = cgNoise3( wp * 2.3 ).x;
  float vegSlopeCos = uVegSlope;
  float flatness = smoothstep( vegSlopeCos - 0.14, vegSlopeCos + 0.1, n.y );
  float vegNoise = cgNoise3( wp / uVegScale ).x * 0.5 + cgNoise3( wp / uVegScale * 5.5 + 2.0 ).x * 0.5;
  float belowSnow = 1.0 - smoothstep( uSnowLine - 160.0, uSnowLine - 20.0, wp.y );
  float aboveWater = smoothstep( uSeaLevel + 1.0, uSeaLevel + 7.0, wp.y );
  float gravelMix = smoothstep( 0.1, 0.6, deposit ) * uGravelAmount;
  float vegThreshold = mix( 0.25, 0.6, uVegPatchiness );
  float veg = uVegetation * flatness * smoothstep( vegThreshold - 0.15, vegThreshold + 0.15, vegNoise + uVegetation * 0.35 - 0.15 ) * ( 1.0 - gravelMix * 0.8 ) * ( 1.0 - uIsRock * 0.9 ) * belowSnow * aboveWater;
  veg = clamp( veg, 0.0, 1.0 ) * ( 1.0 - smoothstep( 0.15, 0.6, bedCover ) );
  float snowNoise = cgNoise3( wp * 0.02 ).x * 70.0 * uSnowSoftness;
  float snowAlt = smoothstep( uSnowLine - 45.0 * uSnowSoftness + snowNoise, uSnowLine + 45.0 * uSnowSoftness + snowNoise, wp.y );
  float rockMask = ( 1.0 - veg ) * ( 1.0 - gravelMix ) * ( 1.0 - snowAlt * smoothstep( uSnowSlopeCos - 0.2, uSnowSlopeCos + 0.1, n.y ) ) * ( 1.0 - smoothstep( 0.2, 0.7, bedCover ) );

  // Triplanar weights
  vec3 w = pow( abs( n ), vec3( 5.0 ) );
  w /= dot( w, vec3( 1.0 ) );

  vec3 G = vec3( 0.0 );
  float fresh = 0.0, rim = 0.0, pit = 0.0;
  float flakeCover = 0.0, flakeCrystal = 0.0, flakeEdge = 0.0;
  vec3 flakeCol = vec3( 0.0 ), flakeFacet = vec3( 0.0 );
  float pebbleCover = 0.0, pebbleTone = 0.0, pebbleHue = 0.0;

  // spalling happens in patches (uPeelCoverage = share of the rock), on exposed rock only
  float peelAmount = uPeelStrength * rockMask * faceVar;
  float peelThick = uPeelThickness * peelAmount;                 // [m] sheet thickness
  float flakeAmount = uFlakeStrength * rockMask * ( 0.45 + 0.8 * faceVar );
  float gravelHere = max( gravelMix, smoothstep( 0.3, 0.9, riverBed ) );
  float hsel = smoothstep( 0.3, 0.7, hardness );

  for ( int axis = 0; axis < 3; axis++ ) {
    float wa = axis == 0 ? w.x : ( axis == 1 ? w.y : w.z );
    if ( wa < 0.02 ) continue;
    vec2 pp = axis == 0 ? wp.yz : ( axis == 1 ? wp.xz : wp.xy );
    pp += vec2( float( axis ) * 37.0, uSeed );
    pp = pp * cgFreq + cgOff;                    // cells stretch and shift instead of marching
    vec2 g2 = vec2( 0.0 );
    // vertical projections: compress the up axis so spalls follow bedding
    vec2 bedStretch = axis == 1 ? vec2( 1.0 ) : ( axis == 0 ? vec2( 1.0 - 0.4 * uPeelBedding, 1.0 + 0.4 * uPeelBedding ) : vec2( 1.0 + 0.4 * uPeelBedding, 1.0 - 0.4 * uPeelBedding ) );

    if ( peelAmount > 0.003 ) {
      float sc = uPeelScale;
      float fade = 1.0 - smoothstep( 0.1, 0.4, footprint / sc );
      if ( fade > 0.0 ) {
        SpallOut sp = spallLayer( pp * bedStretch, sc, uPeelCoverage, uPeelSecond, uPeelPits, max( 0.01, 1.2 * footprint / sc ), uSeed + float( axis ) * 13.0 );
        g2 += sp.g * bedStretch * peelThick * fade;
        fresh += sp.fresh * fade * wa;
        rim += sp.rim * fade * wa;
        pit += sp.pit * fade * wa;
      }
    }

    if ( flakeAmount > 0.003 ) {
      // bottom → top; the top layer occludes the ones below
      float cov = 0.0, cry = 0.0, edg = 0.0; vec3 col = vec3( 0.0 ), fac = vec3( 0.0 ); vec2 gf = vec2( 0.0 );
      for ( int L = 0; L < 3; L++ ) {
        if ( uFlakeOn[L] < 0.5 ) continue;
        float ls = uFlakeSize[L];
        float vis = 1.0 - smoothstep( 0.12, 0.5, footprint / ls );
        if ( vis < 0.001 ) continue;
        FlakeOut f = flakeLayer( pp + vec2( float( L ) * 19.0, 7.0 ), ls, uFlakeDensity[L], uFlakeShape[L], max( 0.02, 1.3 * footprint / ls ), uSeed + 211.0 + float( L ) * 71.0 + float( axis ) * 7.0 );
        // reveal by the erosion hardness channel: +1 only on hard beds, −1 only on soft beds
        float rv = uFlakeReveal[L];
        float reveal = rv >= 0.0 ? mix( 1.0, hsel, rv ) : mix( 1.0, 1.0 - hsel, -rv );
        float alpha = f.cover * vis * reveal;
        vec3 jit = ( f.jitter - 0.5 ) * uFlakeVar[L];
        vec3 lc = uFlakeCol[L] * ( 1.0 + jit.x * 0.7 ) * ( vec3( 1.0 ) + vec3( jit.y, 0.0, -jit.y ) * 0.25 );
        float isCry = step( f.crystal, uFlakeCrystal[L] );
        lc = mix( lc, lc * 1.25 + vec3( 0.03, 0.04, 0.06 ), isCry );
        cov = mix( cov, 1.0, alpha );
        col = mix( col, lc, alpha );
        cry = mix( cry, isCry, alpha );
        edg = mix( edg, f.edge, alpha );
        fac = mix( fac, vec3( f.facet, 0.0 ), alpha );
        gf = mix( gf, f.g / ls * uFlakeHeight[L] * 0.001, alpha );
      }
      g2 += gf * flakeAmount;
      flakeCover += cov * wa;
      flakeCol += col * wa;
      flakeCrystal += cry * wa;
      flakeEdge += edg * wa;
      flakeFacet += fac * wa;
    }

    if ( gravelHere > 0.003 && uGravelAmount > 0.001 ) {
      float ps = uGravelScale;
      float v1 = 1.0 - smoothstep( 0.12, 0.5, footprint / ps );
      if ( v1 > 0.001 ) {
        PebbleOut pa = pebbleLayer( pp + vec2( 5.0, 23.0 ), ps, max( 0.02, 1.3 * footprint / ps ), uSeed + 307.0 + float( axis ) * 7.0 );
        float ps2 = ps * 0.45;
        float v2 = 1.0 - smoothstep( 0.12, 0.5, footprint / ps2 );
        PebbleOut pb = pebbleLayer( pp + vec2( 41.0, 3.0 ), ps2, max( 0.02, 1.3 * footprint / ps2 ), uSeed + 331.0 + float( axis ) * 7.0 );
        // small stones fill the gaps between the big ones
        float ca = pa.cover * v1, cb = pb.cover * v2 * ( 1.0 - ca );
        float cov = ca + cb;
        vec2 gp = ( pa.g / ps * ps * 0.5 * ca + pb.g / ps2 * ps2 * 0.5 * cb );   // dome height ≈ half the stone size
        g2 += gp * uGravelRelief * gravelHere;
        pebbleCover += cov * wa;
        pebbleTone += ( pa.tone * ca + pb.tone * cb ) * wa;
        pebbleHue += ( pa.hue * ca + pb.hue * cb ) * wa;
      }
    }

    vec3 g3 = axis == 0 ? vec3( 0.0, g2.x, g2.y ) : ( axis == 1 ? vec3( g2.x, 0.0, g2.y ) : vec3( g2.x, g2.y, 0.0 ) );
    G += g3 * wa;
  }
  flakeCol = flakeCover > 1e-4 ? flakeCol / flakeCover : vec3( 0.5 );
  flakeCrystal = flakeCover > 1e-4 ? flakeCrystal / flakeCover : 0.0;
  flakeFacet = flakeCover > 1e-4 ? flakeFacet / flakeCover : vec3( 0.0 );
  flakeCover = clamp( flakeCover * flakeAmount, 0.0, 1.0 );
  pebbleTone = pebbleCover > 1e-4 ? pebbleTone / pebbleCover : 0.5;
  pebbleHue = pebbleCover > 1e-4 ? pebbleHue / pebbleCover : 0.5;
  pebbleCover = clamp( pebbleCover, 0.0, 1.0 );
  fresh = clamp( fresh, 0.0, 1.0 ); rim = clamp( rim, 0.0, 1.0 ); pit = clamp( pit, 0.0, 1.0 );

  // Aggregate grain (3D, no projection needed)
  float grainFade = 1.0 - smoothstep( 0.15, 0.6, footprint / uGrainSize );
  vec4 grain = cgFbm3( wp / uGrainSize, 4 );
  float fineScale = uGrainSize * mix( 0.5, 0.12, uGrainFineness );
  vec4 grainFine = cgFbm3( wp / fineScale + 11.0, 3 );
  float grainAmp = 0.035 * uGrainStrength * grainFade * ( 0.3 + 0.7 * rockMask );
  G += ( grain.yzw / uGrainSize * grainAmp + grainFine.yzw / fineScale * grainAmp * 0.25 * ( 1.0 - smoothstep( 0.15, 0.6, footprint / fineScale ) ) );

  // Normal perturbation from the composite height gradient
  vec3 Gt = ( G - n * dot( n, G ) ) * uBumpScale;
  vec3 nW = normalize( n - Gt );

  // ---- colour --------------------------------------------------------------------------------
  // strata: beds of varying thickness in the dipped frame, wavy, with soft broken seams
  // same frame as the heightfield / 3-D chunks: x,z measured from the tile corner, tilted by the
  // dip, thinned / thickened by the lateral jitter, then looked up in the stratigraphic column
  float sxj = wp.x + uWorldSize * 0.5, szj = wp.z + uWorldSize * 0.5;
  float bandY = wp.y + uDipX * sxj + uDipZ * szj;
  float warp = cgNoise3( wp * 0.012 ).x * 1.5 + cgNoise3( wp * 0.07 + 3.0 ).x * 0.5;
  float bedBase;
  vec4 bedRec = bedLookup( ( bandY + warp ) / ( bedJitter( sxj, szj ) * uStrataBand ), bedBase );
  float bf = clamp( ( ( bandY + warp ) / ( bedJitter( sxj, szj ) * uStrataBand ) - bedBase ) / max( 0.01, bedRec.w ), 0.0, 1.0 );
  float bedHard = bedRec.y;
  float bh = bedRec.z;
  float bh2 = fract( bh * 7.31 + 0.17 );
  float bh3 = fract( bh * 13.7 + 0.43 );
  float lam = floor( bh3 * 3.0 * uLaminae ) + 1.0;
  float sf = fract( bf * lam );
  // colour follows hardness (hard beds pale and clean, soft beds darker / warmer) with the bed's
  // own tint on top, so what was carved is what is painted
  vec3 bandCol = mix( uRockB, uRockA, smoothstep( 0.25, 0.7, bedHard ) );
  bandCol = mix( bandCol, mix( uRockA, uRockB, smoothstep( 0.3, 0.7, bh ) ), 0.45 );
  bandCol = mix( bandCol, uRockC, smoothstep( 0.72, 0.95, bh2 ) * 0.85 );
  bandCol *= 1.0 - uBedGradient * ( 0.25 * ( 1.0 - bf ) + 0.12 * ( 1.0 - sf ) );
  float seamNoise = smoothstep( 0.35, 0.7, cgNoise3( wp * vec3( 0.09, 0.4, 0.09 ) ).x );
  float seam = ( 1.0 - smoothstep( 0.0, uSeamWidth, min( bf, 1.0 - bf ) ) ) * seamNoise;
  bandCol *= 1.0 - uSeamStrength * seam;
  // bedding shows on exposed faces; on gentle ground it is under soil and scree and only hinted
  float bandVis = mix( 0.3, 1.0, smoothstep( 0.12, 0.45, wall ) );

  // ---- jointing: the face is a wall of blocks ---------------------------------------------------
  // Two vertical joint sets (families of planes x ≈ const and z ≈ const, warped, spaced like the
  // bed thickness) and the bedding planes break the rock into blocks. Every bed has its own
  // stagger so the vertical joints are offset bed to bed (brickwork), individual joints drop out
  // or vary in width, and each block has its own slight tilt and tone — a jointed cliff face,
  // not a grid. A joint family parallel to the face is invisible (weighted by the face normal).
  float blockTone = 0.0, crackAO = 0.0;
  if ( uJointStrength > 0.001 && rockMask > 0.01 ) {
    float bedThick = max( 0.3, bedRec.w * bedJitter( sxj, szj ) * uStrataBand ); // [m]
    float spacing = uJointSpacing * mix( 0.6, 1.4, fract( bh * 3.7 ) ) * clamp( bedThick / uStrataBand * 1.5, 0.5, 1.6 );
    // block size varies panel by panel, on the same coarse grid the true-3D blocks use
    if ( uJointDomain > 1.0 ) spacing *= mix( 1.0, 0.55 + 1.25 * cgHash3( ivec3( floor( wp.xz / uJointDomain ), 7 ) ), uJointVary );
    // the joint sets swing slowly across the massif, so the lattice is never square to the view
    float jang = uJointBend * 0.75 * ( cgNoise3( wp * 0.0045 + 5.0 ).x + cgNoise3( wp * 0.0045 + 27.0 ).x * 0.4 );
    mat2 Rw = mat2( cos( jang ), sin( jang ), -sin( jang ), cos( jang ) );
    spacing = clamp( spacing, 0.5, 60.0 );
    float bedIdx = floor( bedBase * 97.0 + 0.5 );
    vec2 stagger = vec2( fract( bh * 5.13 ), fract( bh * 9.71 ) ) * spacing * uJointStagger;
    vec2 jw = vec2( cgNoise3( wp * 0.11 + 3.0 ).x, cgNoise3( wp * 0.11 + 17.0 ).x ) * spacing * 0.35; // warp
    vec2 q = ( Rw * ( wp.xz * cgFreq + cgOff ) + stagger + jw ) / spacing;
    vec2 cellId = floor( q ), fq = fract( q );
    float jointFade = 1.0 - smoothstep( 0.25, 1.0, footprint / uJointWidth );
    float jointAmt = uJointStrength * rockMask * jointFade * ( 0.45 + 0.55 * smoothstep( 0.15, 0.5, wall ) ) * mix( 1.0, 0.3 + 1.15 * faceVar, 0.85 );
    // per-joint presence and width (hash of the joint line and the bed)
    float hx = cgHash3( ivec3( int( cellId.x ), int( bedIdx ), 11 ) );
    float hz = cgHash3( ivec3( 23, int( bedIdx ), int( cellId.y ) ) );
    float onX = step( uJointDropout, hx ), onZ = step( uJointDropout, hz );
    float wX = uJointWidth * mix( 0.5, 1.6, fract( hx * 7.0 ) ), wZ = uJointWidth * mix( 0.5, 1.6, fract( hz * 7.0 ) );
    // distance (m) to the nearest joint plane of each family, groove profile and its slope
    float dX = min( fq.x, 1.0 - fq.x ) * spacing, dZ = min( fq.y, 1.0 - fq.y ) * spacing;
    vec2 nr = Rw * n.xz;
    float gX = ( 1.0 - smoothstep( 0.0, wX, dX ) ) * onX * ( 1.0 - abs( nr.x ) );
    float gZ = ( 1.0 - smoothstep( 0.0, wZ, dZ ) ) * onZ * ( 1.0 - abs( nr.y ) );
    float sX = ( fq.x < 0.5 ? 1.0 : -1.0 ) * onX * ( 1.0 - abs( nr.x ) ) * ( dX < wX ? ( 1.0 - dX / wX ) : 0.0 );
    float sZ = ( fq.y < 0.5 ? 1.0 : -1.0 ) * onZ * ( 1.0 - abs( nr.y ) ) * ( dZ < wZ ? ( 1.0 - dZ / wZ ) : 0.0 );
    // bedding joints: a groove at every bed boundary (strongest on the walls)
    float dB = min( bf, 1.0 - bf ) * bedThick;
    float wB = uJointWidth * 1.2;
    float gB = ( 1.0 - smoothstep( 0.0, wB, dB ) ) * smoothstep( 0.15, 0.5, wall );
    float sB = ( bf < 0.5 ? 1.0 : -1.0 ) * ( dB < wB ? ( 1.0 - dB / wB ) : 0.0 ) * smoothstep( 0.15, 0.5, wall );
    float depth = uJointDepth * jointAmt;
    // gradient of the groove height field: the surface drops into each groove
    vec2 cg2 = transpose( Rw ) * vec2( sX / max( wX, 0.01 ), sZ / max( wZ, 0.01 ) );
    vec3 crackG = vec3( cg2.x, sB / max( wB, 0.01 ), cg2.y ) * depth * 1.5;
    // each block: its own tilt (a slab that is not quite flush) and tone
    float hb = cgHash3( ivec3( int( cellId.x ), int( bedIdx ), int( cellId.y ) ) );
    vec3 tilt = ( vec3( hb, fract( hb * 13.1 ), fract( hb * 29.7 ) ) - 0.5 ) * 0.22 * uJointBlocks * jointAmt;
    G += crackG + tilt;
    crackAO = max( gX, max( gZ, gB ) ) * jointAmt;
    blockTone = ( hb - 0.5 ) * 0.2 * uJointBlocks * jointAmt;
    Gt = ( G - n * dot( n, G ) ) * uBumpScale;
    nW = normalize( n - Gt );
  }

  vec3 rock = mix( uRockA, bandCol, uStrataContrast * bandVis );
  rock *= 1.0 + blockTone;
  rock *= 1.0 - crackAO * 0.55;
  vec3 strataOnly = rock;
  // terrain beds carry the erosion hardness: caprock paler and cleaner, soft beds darker and warmer
  rock = mix( rock, rock * 1.12 + uRockC * 0.06, hardness * uHardnessTint * ( 1.0 - uIsRock ) );
  rock = mix( rock, rock * vec3( 0.86, 0.78, 0.7 ), ( 1.0 - hardness ) * 0.6 * uHardnessTint * ( 1.0 - uIsRock ) );

  // mottle & grain tint
  rock *= 1.0 - uGrainContrast * 0.45 + uGrainContrast * 0.9 * ( grain.x * 0.5 + 0.5 );
  rock *= 1.0 - uGrainContrast * 0.2 + uGrainContrast * 0.4 * ( grainFine.x * 0.5 + 0.5 );

  float streakPre = smoothstep( 0.56, 0.78, cgNoise3( vec3( wp.x, wp.y * 0.05, wp.z ) / uStreakScale ).x * 0.6 + cgNoise3( vec3( wp.x * 3.6, wp.y * 0.1, wp.z * 3.6 ) / uStreakScale + 5.0 ).x * 0.4 ) * wall;

  // oxide pockets
  float ox = smoothstep( 0.52, 0.8, cgNoise3( wp / uOxideScale + 7.0 ).x * 0.6 + cgNoise3( wp / uOxideScale * 3.4 ).x * 0.4 );
  rock = mix( rock, uOxide, ox * uOxideAmount );

  // spalls: paler fresh rock where the sheet is gone, a soft shadow in the bevel, dark pits
  rock = mix( rock, mix( rock, uFresh, uPeelFresh ), fresh );
  rock *= 1.0 - rim * 0.22 * uPeelShadow;
  rock *= 1.0 - pit * 0.45;

  // flakes: layer colours (already composited top-over-bottom), oxidised by the erosion channels,
  // bright bevel at the rims
  float oxidise = clamp( ox * 0.7 + flow * 0.6 + streakPre * 0.4, 0.0, 1.0 ) * uFlakeOxide;
  vec3 plateCol = mix( flakeCol, uOxide * ( 0.7 + 0.5 * flakeCol.g ), oxidise );
  rock = mix( rock, plateCol, flakeCover );
  rock += vec3( 0.08 ) * clamp( flakeEdge, 0.0, 1.0 ) * flakeAmount * uFlakeEdge;

  // cavity / convexity
  rock *= 1.0 + cavity * 0.35 * uCavityStrength;

  // runoff staining
  float streak = cgNoise3( vec3( wp.x, wp.y * 0.05, wp.z ) / uStreakScale ).x * 0.6 + cgNoise3( vec3( wp.x * 3.6, wp.y * 0.1, wp.z * 3.6 ) / uStreakScale + 5.0 ).x * 0.4;
  streak = smoothstep( 0.56, 0.78, streak ) * uStreakAmount;
  float wet = clamp( uWetness * ( flow * 0.9 + streak * wall * 0.7 ), 0.0, 1.0 );
  rock *= 1.0 - wet * 0.5;

  // scree gravel on deposits: pebbles in a sandy matrix
  vec3 matrix = mix( uGravel, uSilt, 0.5 ) * 0.8 * ( 0.9 + 0.2 * speckle );
  vec3 stone = uGravel * ( 1.0 - uGravelVariation * 0.5 + uGravelVariation * pebbleTone ) * ( vec3( 1.0 ) + vec3( pebbleHue - 0.5, 0.0, 0.5 - pebbleHue ) * 0.25 * uGravelVariation );
  vec3 gravel = mix( matrix, stone, pebbleCover );
  rock = mix( rock, gravel, gravelMix );

  // drawn features: river beds (cobbles → silt towards the banks), lake beds (silt), roads
  float cobble = cgNoise3( wp * 2.6 ).x * 0.5 + cgNoise3( wp * 9.0 + 3.0 ).x * 0.5;
  vec3 bedCol = mix( uSilt, mix( uGravel * ( 0.65 + 0.7 * cobble ), gravel, step( 0.001, uGravelAmount ) ), smoothstep( 0.45, 0.95, riverBed ) );
  bedCol = mix( bedCol, uSilt * ( 0.9 + 0.2 * speckle ), lakeBed * ( 1.0 - riverBed ) );
  float bedMix = smoothstep( 0.05, 0.5, max( riverBed, lakeBed ) );
  rock = mix( rock, bedCol, bedMix );
  float roadSurf = smoothstep( 0.5, 0.75, road );
  float shoulder = smoothstep( 0.1, 0.4, road ) * ( 1.0 - roadSurf );
  vec3 roadCol = uRoad * ( 0.9 + 0.2 * cgNoise3( wp * 1.7 ).x ) * ( 0.94 + 0.12 * cgNoise3( wp * 14.0 ).x );
  rock = mix( rock, mix( uGravel, uDry, 0.4 ) * ( 0.85 + 0.3 * speckle ), shoulder );
  rock = mix( rock, roadCol, roadSurf );

  // vegetation: grass with dry patches and bare soil
  float dry = smoothstep( 0.45, 0.7, cgNoise3( wp / uVegScale * 2.5 + 23.0 ).x ) * uDryness;
  vec3 grass = mix( uGrassA, uGrassB, cgNoise3( wp * 0.12 ).x * 0.7 + 0.15 );
  grass = mix( grass, uDry, dry );
  grass *= 0.85 + 0.3 * speckle;

  // moss in sheltered concavities
  float moss = uMossiness * ( 0.35 + 0.65 * smoothstep( 0.0, 0.6, -cavity ) ) * smoothstep( 0.05, 0.7, n.y + 0.2 )
    * smoothstep( 0.5, 0.78, cgNoise3( wp / uMossScale ).x * 0.6 + cgNoise3( wp / uMossScale * 0.15 + 9.0 ).x * 0.4 ) * belowSnow * aboveWater * ( 1.0 - fresh * 0.5 );
  moss = clamp( moss, 0.0, 1.0 );

  // ---- soil: the regolith that covers gentle ground between the rock outcrops -----------------
  // Rock shows where the ground is steep, hard and convex (knolls, ribs); soil gathers on gentle,
  // soft and concave ground and on every deposit. Clods and fine grit give it its own grain; it
  // darkens where it is damp (flow lines, the ground just above the water) and in hollows where
  // humus collects; fluvial alluvium is paler silt / sand on top of it.
  float soilSlope = smoothstep( uSoilSlopeCos - 0.14, uSoilSlopeCos + 0.1, n.y );
  float soilNoise = cgNoise3( wp * 0.035 ).x * 0.55 + cgNoise3( wp * 0.19 + 11.0 ).x * 0.45;
  float outcrop = smoothstep( 0.5, 0.9, hardness ) * smoothstep( -0.1, 0.5, cavity ) * ( 1.0 - deposit );
  float soil = uSoilAmount * soilSlope * ( 1.0 - 0.85 * outcrop )
    * smoothstep( 0.25, 0.6, soilNoise + 0.25 + deposit * 0.4 - cavity * 0.25 + ( 1.0 - hardness ) * 0.2 )
    * belowSnow * ( 1.0 - uIsRock ) * ( 1.0 - smoothstep( 0.15, 0.6, bedCover ) ) * ( 1.0 - gravelMix * 0.5 );
  soil = clamp( soil, 0.0, 1.0 );
  vec4 clodN = cgNoise3( wp * 2.4 );
  float clods = clodN.x * 0.5 + 0.5;
  float grit = cgNoise3( wp * 11.0 + 5.0 ).x * 0.5 + 0.5;
  vec3 soilCol = mix( uSoilColor, uSoilLight, clamp( clods * 0.7 + grit * 0.3, 0.0, 1.0 ) * uSoilClods + 0.15 );
  soilCol *= 0.92 + 0.16 * grit;
  float damp = clamp( flow * 1.1 + ( 1.0 - smoothstep( waterLine + 0.2, waterLine + 2.5, wp.y ) ) + streak * 0.3, 0.0, 1.0 ) * uSoilMoisture;
  soilCol *= 1.0 - 0.4 * damp;
  soilCol *= 1.0 - 0.18 * max( 0.0, -cavity );
  // alluvium / silt on top of the soil (sediment the rivers dropped)
  vec3 siltCol = uSilt * ( 0.92 + 0.16 * clods ) * ( 1.0 - 0.3 * damp );
  soilCol = mix( soilCol, siltCol, lakeBed * uSoilAlluvium );
  rock = mix( rock, soilCol, soil );

  // ---- sediment: the fill that water and weathering laid down, in beds ---------------------------
  // A deposit is not a flat colour. It is sorted by the energy that carried it (gravel where the
  // flow was strong, sand on the bars, silt and mud on the quiet flats), it is laid in beds that
  // thicken, thin and climb the valley sides, each bed with its own parting, its own tone and a set
  // of cross-laminae inside it, and it carries current ripples and mud cracks on top. Where the fill
  // is thick the rock beneath is simply gone: no joints, no flakes, no bedding of its own.
  float sedCover = 0.0;
  vec3 sedCol = vec3( 0.5 );
  vec2 sedGrad = vec2( 0.0 );
  float sedSeam = 0.0, sedRip = 0.0, sedTone = 0.0, sedCoarse = 0.0;
  {
    float sedDepth = smoothstep( 0.05, 0.42, deposit );                       // how much fill is here
    float sedGround = smoothstep( uSedSlopeCos - 0.16, uSedSlopeCos + 0.06, n.y );
    // the fill itself where the rivers laid one, and the weathered gentle ground everywhere else:
    // the pale flats are sediment too, they are just thinner — so the bedding shows on both
    float sd = uSedAmount * clamp( max( sedDepth, soil * 0.8 ), 0.0, 1.0 ) * mix( 0.35, 1.0, sedGround ) * belowSnow * ( 1.0 - uIsRock );
    // the energy that laid it: stream power, i.e. how much water times how steeply it ran. Both are
    // needed — a steep hillslope with no catchment above it sheds mud and colluvium, a big lowland
    // river carries sand, and only a fast concentrated flow can drag gravel.
    float sdPower = flow * ( 0.25 + wall * 1.5 );
    float sdEnergy = clamp( ( sdPower - 0.28 + uSedGrainBias * 0.35 ) * 2.4 + sedDepth * 0.28, 0.0, 1.0 );
    float sdFac = clamp( mix( 0.45, sdEnergy, uSedFacies ) + cgNoise3( wp * 0.05 + 8.0 ).x * 0.3 * ( 1.0 - uSedFacies ), 0.0, 1.0 );
    // beds: surfaces that ripple sideways at landscape scale, so they onlap the valley sides instead
    // of being sawn through them at one constant height
    float sdScale = max( 0.06, uSedBedScale * mix( 0.55, 2.1, sdFac ) );       // coarse beds are thicker
    float sdWarp = cgNoise3( vec3( wp.x * 0.0055, wp.y * 0.02, wp.z * 0.0055 ) + 2.0 ).x;
    float sdIdx = wp.y / sdScale + sdWarp * 3.2 + deposit * 1.8;
    float sdT = fract( sdIdx ), sdId = floor( sdIdx );
    float sdH = cgHash3( ivec3( int( sdId ), 5, 13 ) );                        // this bed's character
    float sdH2 = cgHash3( ivec3( int( sdId ), 29, 7 ) );
    float sdPart = 1.0 - smoothstep( 0.0, 0.09 + 0.13 * sdH, min( sdT, 1.0 - sdT ) );
    // cross-laminae inside the bed: a fine set of lines that wanders sideways
    float sdLam = uSedLaminae > 0.001 ? ( cgNoise3( vec3( wp.x * 0.45, sdIdx * 6.2 + sdH2 * 4.0, wp.z * 0.45 ) + 9.0 ).x * 0.5 + 0.5 ) : 0.5;
    // anti-aliasing by the pixel footprint: below a few pixels per feature each of these patterns
    // fades out, so the flats do not crawl when you orbit
    float sdBedFade = 1.0 - smoothstep( 0.3, 1.3, footprint / sdScale );
    float sdLamFade = 1.0 - smoothstep( 0.2, 0.7, footprint / max( 0.02, sdScale * 0.16 ) );
    float sdWL = max( 0.12, uSedRippleScale * mix( 1.0, 2.6, sdFac ) );   // coarse facies: wider trains
    float sdRipFade = 1.0 - smoothstep( 0.15, 0.55, footprint / sdWL );
    float sdGrainFade = 1.0 - smoothstep( 0.6, 2.0, footprint * ( 9.0 + 26.0 * sdFac ) );
    // the grain of the fill itself, and the facies colours: gravel / sand / silt-and-mud
    float sdGrain = uSedGrain > 0.001 ? mix( 0.5, cgNoise3( wp * ( 9.0 + 26.0 * sdFac ) + 3.0 ).x * 0.5 + 0.5, sdGrainFade ) : 0.5;
    vec3 sdC = mix( uSedSilt, uSedSand, smoothstep( 0.16, 0.56, sdFac ) );
    sdC = mix( sdC, uSedGravel, smoothstep( 0.6, 0.95, sdFac ) );
    sdC *= 1.0 + ( sdH - 0.5 ) * 0.42 * uSedBands * sdBedFade + ( sdLam - 0.5 ) * 0.22 * uSedLaminae * sdLamFade;
    sdC *= 0.9 + 0.2 * sdGrain;
    sdPart *= sdBedFade;
    sdC *= 1.0 - 0.55 * sdPart * uSedBands;                                     // the parting between beds
    sdC *= 1.0 - 0.3 * damp;                                                    // wet sediment is darker
    // current ripples: wave trains along a palaeocurrent direction that wanders patch to patch
    float sdA = uSedDirRad + cgNoise3( wp * 0.018 + 4.0 ).x * 1.15;
    vec2 sdDir = vec2( cos( sdA ), sin( sdA ) );
    float sdPh = dot( wp.xz, sdDir ) / sdWL + sdWarp * 0.9;
    sedRip = sin( sdPh * 6.2831853 );
    float sdAmp = uSedRipple * 0.055 * ( 1.0 - 0.65 * sdFac ) * ( 0.4 + 0.6 * sedDepth ) * sdRipFade;
    sedGrad = sdDir * ( cos( sdPh * 6.2831853 ) * 6.2831853 / sdWL ) * sdAmp;
    sedRip = sedRip * sdRipFade;
    // mud cracks on the open silt flats: the edges of jittered polygons
    if ( uSedCracks > 0.001 ) {
      float sdCs = 1.5 + 2.0 * ( 1.0 - sdFac );
      vec2 sdq = wp.xz / sdCs;
      ivec2 sdc = ivec2( floor( sdq ) );
      vec2 sdf = fract( sdq );
      float sdd1 = 1e9, sdd2 = 1e9;
      for ( int sj = -1; sj <= 1; sj++ ) for ( int si = -1; si <= 1; si++ ) {
        ivec2 scc = sdc + ivec2( si, sj );
        vec2 sdctr = vec2( float( si ), float( sj ) ) + vec2( cgHash2( scc, 21.0 ), cgHash2( scc, 33.0 ) );
        float sdd = length( sdf - sdctr );
        if ( sdd < sdd1 ) { sdd2 = sdd1; sdd1 = sdd; } else if ( sdd < sdd2 ) { sdd2 = sdd; }
      }
      float sdCrk = ( 1.0 - smoothstep( 0.05, 0.16, sdd2 - sdd1 ) ) * uSedCracks * ( 1.0 - smoothstep( 0.4, 0.8, sdFac ) ) * ( 1.0 - smoothstep( 0.25, 0.8, footprint / sdCs ) );
      sdC *= 1.0 - 0.34 * sdCrk;
      sedSeam = max( sedSeam, sdCrk );
    }
    // the bedding tone, kept on its own so it can still show through grass and moss: an alluvial
    // plain reads as alternating pale and dark strips from any height, and that is all that is left
    // of the fill once anything grows on it
    sedTone = ( sdH - 0.5 ) * 0.5 * uSedBands * sdBedFade + ( sdLam - 0.5 ) * 0.24 * uSedLaminae * sdLamFade - 0.45 * sdPart * uSedBands;
    sedCoarse = smoothstep( 0.55, 0.92, sdFac );
    sedCol = sdC * ( 1.0 - 0.09 * sedRip * uSedRipple );
    sedCover = clamp( sd, 0.0, 1.0 ) * ( 1.0 - veg * 0.45 ) * ( 1.0 - smoothstep( 0.1, 0.6, bedCover ) );
    rock = mix( rock, sedCol, sedCover );
    sedSeam = max( sedSeam, sdPart * uSedBands );
  }

  // a gravel bar does not hold grass the way a floodplain does: the coarse facies wins through the
  // vegetation, and the bedding of the fill keeps showing under it
  float vegHere = clamp( veg * ( 1.0 - 0.75 * sedCover * sedCoarse ), 0.0, 1.0 );
  vec3 color = mix( rock, grass, vegHere );
  color *= 1.0 + sedTone * sedCover * ( 0.35 + 0.65 * vegHere );
  color = mix( color, uMoss * ( 0.8 + 0.4 * speckle ), moss * 0.8 );

  // snow
  // couloirs and hollows hold snow on steeper faces, arêtes and convex ribs are blown clear
  float snowCos = uSnowSlopeCos + cavity * 0.12;
  float snow = snowAlt * smoothstep( snowCos - 0.12, snowCos + 0.1, nW.y );
  // rock ribs show through the thin snow on the steeper faces (fluted high-mountain faces)
  float rib = smoothstep( 0.55, 0.85, cgNoise3( wp * vec3( 0.35, 0.07, 0.35 ) + 3.0 ).x ) * ( 1.0 - smoothstep( snowCos + 0.04, snowCos + 0.3, nW.y ) );
  snow *= 1.0 - rib * 0.85;
  snow = clamp( snow * ( 1.0 + deposit * 0.6 + max( 0.0, -cavity ) * 0.4 ), 0.0, 1.0 );
  // wind-packed surface: faint drifts, slightly dirtier in old hollows
  vec3 snowCol = uSnow * ( 0.94 + 0.06 * cgNoise3( wp * 0.6 ).x ) * ( 1.0 - 0.06 * max( 0.0, -cavity ) );
  color = mix( color, snowCol, snow );

  // wet band at the shoreline and darker, cooler ground under water
  float shore = 1.0 - smoothstep( waterLine - 0.3, waterLine + 1.2 * uShoreWet + 0.3, wp.y );
  color *= mix( 1.0, 0.62, shore * uShoreWet );
  float under = 1.0 - smoothstep( waterLine - 2.5, waterLine, wp.y );
  color = mix( color, color * vec3( 0.55, 0.62, 0.62 ), under );
  // painted water (Gaea-style: the river / lake / sea mask is part of the surface, no water
  // mesh): clear tint over the bed in the shallows, the water colour where it is deep
  float wdepth = waterLine - wp.y;
  float paint = uPaintWater * ( 1.0 - smoothstep( -0.05, 0.25, -wdepth ) ) * ( 1.0 - uIsRock );
  vec3 waterCol = mix( uWaterShallow, uWaterDeep, smoothstep( 0.0, uWaterClear * 3.0, wdepth ) );
  color = mix( color, mix( color * 0.6, waterCol, smoothstep( 0.0, uWaterClear, wdepth ) * 0.6 + 0.4 ), paint );

  float roughness = uBaseRoughness - wet * 0.4 - fresh * 0.05 - shore * 0.35 * uShoreWet;
  roughness = mix( roughness, 0.95, roadSurf );
  roughness = mix( roughness, 0.45, flakeCover * uFlakeSheen );
  float crystalMask = flakeCover * flakeCrystal;
  roughness = mix( roughness, 0.12, crystalMask );
  float metal = 0.35 * crystalMask;
  // glitter: each crystal plate is a tiny facet; a sharp highlight when it lines up with the sun
  vec3 V = normalize( cameraPosition - wp );
  vec3 H = normalize( V + normalize( uSunDir ) );
  vec3 facetN = normalize( nW + flakeFacet * 0.35 );
  float glint = pow( max( dot( facetN, H ), 0.0 ), 260.0 );
  vec3 emissive = vec3( glint * crystalMask * uFlakeSparkle * 2.5 ) * ( 1.0 - snow ) * ( 1.0 - veg );
  roughness = mix( roughness, mix( 0.93, 0.7, damp ), soil * ( 1.0 - vegHere ) );
  roughness = mix( roughness, mix( 0.97, 0.68, damp ), sedCover * ( 1.0 - vegHere ) );
  roughness = mix( roughness, 0.95, veg );
  roughness = mix( roughness, uSnowRoughness, snow );

  float ao = ( 1.0 - rim * 0.25 * uPeelShadow ) * ( 1.0 - pit * 0.4 ) * ( 1.0 - crackAO * 0.5 ) * ( 1.0 - sedSeam * 0.3 * sedCover ) * ( 1.0 - max( 0.0, -cavity ) * 0.4 * uCavityStrength );
  ao = mix( ao, 1.0, snow * 0.6 );

  // soil and sediment hide the rock micro-relief under a softer surface; the fill brings its own
  // ripple relief with it, so a bar reads as a bar and not as a beige wash
  vec3 soilN = normalize( n + clodN.yzw * 0.12 * uSoilClods );
  vec3 sedN = normalize( n - vec3( sedGrad.x, 0.0, sedGrad.y ) + clodN.yzw * 0.04 * uSedGrain );
  vec3 nFinal = normalize( mix( nW, soilN, soil * ( 1.0 - veg ) ) );
  nFinal = normalize( mix( nFinal, sedN, sedCover * ( 1.0 - 0.75 * vegHere ) ) );
  nFinal = normalize( mix( nFinal, n, max( snow, veg * 0.8 ) ) );
  nFinal = normalize( mix( nFinal, vec3( 0.0, 1.0, 0.0 ), paint * 0.9 ) );
  roughness = mix( roughness, 0.22, paint );

  Surface s;
  s.albedo = color;
  s.normalW = nFinal;
  s.roughness = clamp( roughness, 0.08, 1.0 );
  s.ao = ao;
  s.metal = metal;
  s.emissive = emissive;
  if ( uDebugView > 0.5 ) {
    // Isolate one layer so its contribution can be judged on its own (lit, flat normal).
    vec3 dbg = vec3( 0.0 );
    if ( uDebugView < 1.5 ) dbg = strataOnly;
    else if ( uDebugView < 2.5 ) dbg = vec3( 0.5 + 0.5 * grain.x ) * ( 1.0 - uGrainContrast * 0.5 + uGrainContrast * ( 0.5 + 0.5 * grainFine.x ) ) * ( 0.3 + 0.7 * uGrainStrength );
    else if ( uDebugView < 3.5 ) dbg = mix( vec3( 0.12 ), flakeCol + vec3( 0.25 ) * clamp( flakeEdge, 0.0, 1.0 ) * uFlakeEdge + vec3( 0.3, 0.3, 0.6 ) * flakeCrystal, flakeCover );
    else if ( uDebugView < 4.5 ) dbg = vec3( 0.15 ) + vec3( 0.0, 0.7, 0.9 ) * fresh + vec3( 0.9, 0.2, 0.1 ) * rim + vec3( 0.0, 0.0, 0.6 ) * pit;
    else if ( uDebugView < 5.5 ) dbg = mix( vec3( 0.1 ), vec3( 0.3 + 0.6 * pebbleTone ), pebbleCover ) * ( 0.3 + 0.7 * gravelHere );
    else if ( uDebugView < 6.5 ) dbg = vec3( 0.12 ) + vec3( 0.1, 0.7, 0.1 ) * veg + vec3( 0.05, 0.3, 0.15 ) * moss + vec3( 0.9 ) * snow + vec3( 0.5, 0.4, 0.3 ) * gravelMix + vec3( 0.45, 0.3, 0.15 ) * soil * ( 1.0 - veg );
    else if ( uDebugView < 7.5 ) dbg = vec3( rockMask, wet, hardness );
    else dbg = vec3( road, riverBed, lakeBed ) + vec3( 0.0, 0.0, 0.4 ) * under;
    s.albedo = dbg;
    s.normalW = n;
    s.roughness = 1.0;
    s.ao = 1.0;
    s.metal = 0.0;
    s.emissive = vec3( 0.0 );
  }
  return s;
}
`;

const fragmentBody = /* glsl */`
{
  vec3 geomN = inverseTransformDirection( normal, viewMatrix );
  Surface s = evaluateCliffSurface( vWorldPos, geomN, vAux, vAux2 );
  diffuseColor.rgb = s.albedo;
  #ifdef USE_COLOR
    diffuseColor.rgb *= vColor.rgb;
  #endif
  roughnessFactor = s.roughness;
  metalnessFactor = s.metal;
  normal = normalize( ( viewMatrix * vec4( s.normalW, 0.0 ) ).xyz );
  cgAO = s.ao;
  cgEmissive = s.emissive;
}
`;

const colorKeys = { uSedGravel: 'sedColorGravel', uSedSand: 'sedColorSand', uSedSilt: 'sedColorSilt', uSoilColor: 'soilColor', uSoilLight: 'soilLight', uRoad: 'roadColor', uSilt: 'siltColor', uRockA: 'rockA', uRockB: 'rockB', uRockC: 'rockC', uFresh: 'fresh', uOxide: 'oxide', uGrassA: 'grassA', uGrassB: 'grassB', uDry: 'dryColor', uMoss: 'mossColor', uSnow: 'snowColor', uGravel: 'gravelColor' };

// uniform → [value key, enable key (optional), scale]
const scalarKeys = {
  uStrataContrast: ['strataContrast', 'strataOn'], uSeamStrength: ['seamStrength', 'strataOn'], uSeamWidth: ['seamWidth'], uLaminae: ['laminae', 'strataOn'], uBedGradient: ['bedGradient', 'strataOn'], uHardnessTint: ['hardnessTint', 'strataOn'],
  uJointStrength: ['jointStrength', 'jointOn'], uJointSpacing: ['jointSpacing'], uJointWidth: ['jointWidth'], uJointDepth: ['jointDepth'], uJointStagger: ['jointStagger'], uJointBlocks: ['jointBlocks'], uJointDropout: ['jointDropout'],
  uTexWarp: ['texWarp'], uJointBend: ['jointBend'], uJointVary: ['jointVary'], uFacePatch: ['facePatch'],
  uGrainSize: ['grainSize'], uGrainStrength: ['grainStrength', 'grainOn'], uGrainContrast: ['grainContrast', 'grainOn'], uGrainFineness: ['grainFineness'],
  uOxideAmount: ['oxideAmount', 'oxideOn'], uOxideScale: ['oxideScale'], uCavityStrength: ['cavityStrength'],
  uFlakeStrength: ['flakeStrength', 'flakesOn'], uFlakeSheen: ['flakeSheen'], uFlakeEdge: ['flakeEdge'], uFlakeOxide: ['flakeOxide'], uFlakeSparkle: ['flakeSparkle'],
  uPeelStrength: ['peelStrength', 'peelOn'], uPeelScale: ['peelScale'], uPeelCoverage: ['peelCoverage'], uPeelThickness: ['peelThickness'], uPeelBedding: ['peelBedding'], uPeelFresh: ['peelFresh'], uPeelPits: ['peelPits'], uPeelShadow: ['peelShadow'], uPeelSecond: ['peelSecond'],
  uWetness: ['wetness', 'runoffOn'], uStreakScale: ['streakScale'], uStreakAmount: ['streakAmount'],
  uGravelAmount: ['gravelAmount', 'gravelOn'], uGravelScale: ['gravelScale'], uGravelRelief: ['gravelRelief'], uGravelVariation: ['gravelVariation'],
  uSoilAmount: ['soilAmount', 'soilOn'], uSoilClods: ['soilClods'], uSoilMoisture: ['soilMoisture'], uSoilAlluvium: ['soilAlluvium'],
  uSedAmount: ['sedimentAmount', 'sedimentOn'], uSedBands: ['sedBands', 'sedimentOn'], uSedBedScale: ['sedBedScale'], uSedLaminae: ['sedLaminae', 'sedimentOn'], uSedFacies: ['sedFacies'], uSedGrainBias: ['sedGrainBias'], uSedRipple: ['sedRipple', 'sedimentOn'], uSedRippleScale: ['sedRippleScale'], uSedCracks: ['sedCracks', 'sedimentOn'], uSedGrain: ['sedGrain', 'sedimentOn'],
  uVegetation: ['vegetation', 'vegOn'], uVegScale: ['vegScale'], uVegPatchiness: ['vegPatchiness'], uDryness: ['dryness'],
  uMossiness: ['mossiness', 'mossOn'], uMossScale: ['mossScale'],
  uSnowSoftness: ['snowSoftness'], uSnowRoughness: ['snowRoughness'],
  uBumpScale: ['bumpScale'], uBaseRoughness: ['baseRoughness'], uDebugView: ['debugView'], uShoreWet: ['shoreWet'], uRoadOn: ['roadShading'], uBedOn: ['bedShading'],
};

// per-layer flake uniforms (arrays of 3): uniform → value key prefix (key = prefix + layer index 1..3)
const flakeArrayKeys = { uFlakeOn: 'flakeOn', uFlakeSize: 'flakeSize', uFlakeDensity: 'flakeDensity', uFlakeHeight: 'flakeHeight', uFlakeCrystal: 'flakeCrystal', uFlakeVar: 'flakeVar', uFlakeShape: 'flakeShape', uFlakeReveal: 'flakeReveal' };

export function makeSurfaceUniforms() {
  const u = {};
  for (const k of Object.keys(colorKeys)) u[k] = { value: new THREE.Color(0.5, 0.5, 0.5) };
  for (const k of Object.keys(scalarKeys)) u[k] = { value: 0 };
  for (const k of Object.keys(flakeArrayKeys)) u[k] = { value: [0, 0, 0] };
  u.uFlakeCol = { value: [new THREE.Color(), new THREE.Color(), new THREE.Color()] };
  u.uSunDir = { value: new THREE.Vector3(0.3, 0.8, 0.5) };
  Object.assign(u, {
    uStrataBand: { value: 1 }, uDipX: { value: 0 }, uDipZ: { value: 0 },
    uBedTex: { value: makeBedTexture(new Float32Array(4), 1) }, uBedCount: { value: 1 }, uBedBase: { value: -1500 }, uWorldSize: { value: 2048 }, uBedLateral: { value: 0.18 },
    uVegSlope: { value: 0.72 }, uSoilSlopeCos: { value: 0.75 }, uSedSlopeCos: { value: 0.66 }, uSedDirRad: { value: 0 }, uSnowLine: { value: 430 }, uSnowSlopeCos: { value: 0.67 },
    uSeaLevel: { value: 0 }, uSeed: { value: 428 }, uJointDomain: { value: 0 },
    uPaintWater: { value: 1 }, uWaterClear: { value: 1.5 }, uWaterDeep: { value: new THREE.Color(0x1d4552) }, uWaterShallow: { value: new THREE.Color(0x4f7f7a) },
  });
  return u;
}

export function updateSurfaceUniforms(uniforms, v) {
  for (const [u, key] of Object.entries(colorKeys)) uniforms[u].value.setStyle(v[key] || '#808080');
  for (const [u, [key, enable]] of Object.entries(scalarKeys)) {
    const on = enable ? (v[enable] ? 1 : 0) : 1;
    uniforms[u].value = Number(v[key]) * on;
  }
  for (const [u, prefix] of Object.entries(flakeArrayKeys)) for (let L = 0; L < 3; L++) uniforms[u].value[L] = Number(v[`${prefix}${L + 1}`]);
  for (let L = 0; L < 3; L++) uniforms.uFlakeCol.value[L].setStyle(v[`flakeColor${L + 1}`] || '#808080');
  // the shader divides the tilted elevation by uStrataBand before the table lookup, so the
  // colour-band scale (default 1) stretches the painted beds relative to the carved ones
  uniforms.uStrataBand.value = v.strataBandScale || 1;
  uniforms.uWorldSize.value = v.worldSize;
  uniforms.uBedLateral.value = v.strataLateral == null ? 0.18 : v.strataLateral;
  // the block-size hash reads the same coarse domain grid as the true-3D chunks, so texture and
  // geometry go coarse and fine together instead of fighting each other
  uniforms.uJointDomain.value = v.sdfOn && v.sdfBlocks > 0 ? Math.max(14, (v.sdfBlockSize || 9) * 5) : 0;
  const bedKey = `${v.seed}|${v.strataBand}|${v.strataVariation}|${v.strataPackaging}|${v.strataHardShare}`;
  if (uniforms.uBedTex.key !== bedKey) {
    const table = makeBedTable(v);
    const count = Math.min(4096, table.count);
    const data = new Float32Array(count * 4);
    for (let i = 0; i < count; i++) { data[i * 4] = table.tops[i]; data[i * 4 + 1] = table.hard[i]; data[i * 4 + 2] = table.tint[i]; data[i * 4 + 3] = table.thick[i]; }
    if (uniforms.uBedTex.value) uniforms.uBedTex.value.dispose();
    uniforms.uBedTex.value = makeBedTexture(data, count);
    uniforms.uBedTex.key = bedKey;
    uniforms.uBedCount.value = count;
    uniforms.uBedBase.value = table.base;
  }
  const dipRad = (v.strataDip * Math.PI) / 180, dirRad = (v.strataDipDirection * Math.PI) / 180;
  uniforms.uDipX.value = Math.tan(dipRad) * Math.cos(dirRad);
  uniforms.uDipZ.value = Math.tan(dipRad) * Math.sin(dirRad);
  uniforms.uVegSlope.value = Math.cos((v.vegSlope * Math.PI) / 180);
  uniforms.uSoilSlopeCos.value = Math.cos(((v.soilSlope == null ? 40 : v.soilSlope) * Math.PI) / 180);
  uniforms.uSedSlopeCos.value = Math.cos(((v.sedSlope == null ? 48 : v.sedSlope) * Math.PI) / 180);
  uniforms.uSedDirRad.value = ((v.sedDir == null ? 35 : v.sedDir) * Math.PI) / 180;
  uniforms.uSnowLine.value = v.snowOn ? v.snowLine : 1e6;
  uniforms.uSnowSlopeCos.value = Math.cos((v.snowSlope * Math.PI) / 180);
  uniforms.uSeaLevel.value = v.waterEnabled ? v.seaLevel : -1e6;
  uniforms.uSeed.value = v.seed % 1000;
  uniforms.uPaintWater.value = v.waterMeshes ? 0 : 1;
  uniforms.uWaterClear.value = v.riverClearDepth == null ? 1.5 : Math.max(0.1, v.riverClearDepth);
  uniforms.uWaterDeep.value.setStyle(v.riverColor || '#1d4552');
  uniforms.uWaterShallow.value.setStyle(v.riverShallowColor || '#4f7f7a');
}

function makeBedTexture(data, count) {
  const tex = new THREE.DataTexture(data, count, 1, THREE.RGBAFormat, THREE.FloatType);
  tex.minFilter = THREE.NearestFilter; tex.magFilter = THREE.NearestFilter;
  tex.generateMipmaps = false; tex.needsUpdate = true;
  return tex;
}

export function makeSurfaceMaterial(uniforms, { isRock = false } = {}) {
  const material = new THREE.MeshStandardMaterial({ roughness: 1, metalness: 0, color: 0xffffff });
  material.onBeforeCompile = (shader) => {
    Object.assign(shader.uniforms, uniforms, { uIsRock: { value: isRock ? 1 : 0 } });
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', `#include <common>\n${vertexHead}`)
      .replace('#include <worldpos_vertex>', `#include <worldpos_vertex>\n${vertexBody}`);
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', `#include <common>\n${fragmentHead}\nfloat cgAO = 1.0;\nvec3 cgEmissive = vec3( 0.0 );`)
      .replace('#include <emissivemap_fragment>', '#include <emissivemap_fragment>\ntotalEmissiveRadiance += cgEmissive;')
      .replace('#include <normal_fragment_maps>', fragmentBody)
      .replace('#include <aomap_fragment>', `#include <aomap_fragment>\nreflectedLight.indirectDiffuse *= cgAO;\nreflectedLight.indirectSpecular *= cgAO;\nreflectedLight.directDiffuse *= mix( 1.0, cgAO, 0.5 );`);
  };
  material.customProgramCacheKey = () => `cliff-surface-${isRock ? 'rock' : 'terrain'}`;
  return material;
}
