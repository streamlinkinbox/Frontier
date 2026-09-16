import * as THREE from 'three';

// ---------------------------------------------------------------------------
// Global constants
// ---------------------------------------------------------------------------
export const G = 9.81;
export const NCOMP = 80;          // spectral wave components (vertex-shader sum)
export const SWELL_N = 20;        //   cascade 0: swell   (must be first: foam + spray sample it)
export const SEA_N = 28;          //   cascade 1: wind sea
export const CHOP_N = 32;         //   cascade 2: chop
export const GRID_HALF = 650;     // near-field graded grid half-size (m)

// Wave-lab site (destructive-interference / standing-wave / crossing-seas demo)
export const LAB = { x: -190, z: 80, r: 75 };

// ---------------------------------------------------------------------------
// Live parameters (single source of truth; UI writes, renderer reads)
// ---------------------------------------------------------------------------
export const PARAMS = {
  // sea state
  beaufort: 6,
  wind: 13.8,        // U10 (m/s)
  fetch: 150,         // km (kept in the fetch-limited, responsive range)
  windDir: 38,        // deg, math angle in XZ plane (0 = +X, toward shore)
  chop: 1.15,         // global steepness / choppiness multiplier
  relief: 1.6,        // vertical exaggeration (maritime-sim style readability)
  cascSwell: 1.0, cascSea: 1.0, cascChop: 1.0,

  // surf break
  surfOn: 1,
  shoalGain: 1.0,
  breakAmp: 1.0,
  barrel: 1.6,        // lip throw toward shore
  peelSpeed: 7.0,     // m/s alongshore travel of the break pulse
  peelWidth: 26.0,
  reefAngle: 12,      // deg skew of the reef bar
  reefDepth: 1.4,     // bar crest depth (m)
  reefX: -50, shoreX: 60, shoreAngle: 0,

  // wave lab (two localized trains with gaussian envelope at LAB site)
  labA: { on: false, lambda: 60, amp: 1.4, dir: 90, phase: 0 },
  labB: { on: false, lambda: 60, amp: 1.4, dir: 90, phase: 180 },

  // foam & spray
  foamAmt: 1.0, whitecap: 1.0, spray: 1.0, particles: 22000,

  // environment
  hour: 15.5, cloud: 0.45, fog: 1.0, exposure: 1.05,
  micro: 0.65, sss: 1.0,

  // transport / view
  timeScale: 1.0, paused: false,
  wireframe: false, autorotate: false, quality: 'high',
};

export const BEAUFORT_WIND = [0.3, 1.5, 3.3, 5.4, 7.9, 10.7, 13.8, 17.1, 20.7, 24.4];
export const BEAUFORT_NAMES = [
  '0 · Calm', '1 · Light air', '2 · Light breeze', '3 · Gentle', '4 · Moderate',
  '5 · Fresh', '6 · Strong', '7 · Near gale', '8 · Gale', '9 · Strong gale',
];

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
export const PRESETS = {
  glassy: {
    label: 'Glassy Dawn',
    p: { beaufort: 1, wind: 1.5, fetch: 120, chop: 0.7, relief: 1.2, hour: 6.4, cloud: 0.25,
         cascChop: 0.6, breakAmp: 0.4, spray: 0.5, particles: 12000, foamAmt: 0.8 },
    lab: { a: false, b: false }, cam: 'orbit',
  },
  trades: {
    label: 'Trade Winds',
    p: { beaufort: 6, wind: 13.8, fetch: 150, windDir: 38, chop: 1.15, relief: 1.6,
         hour: 15.5, cloud: 0.45,
         cascSwell: 1, cascSea: 1, cascChop: 1, surfOn: 1, breakAmp: 1.0, barrel: 1.6,
         spray: 1.0, particles: 22000, foamAmt: 1.0 },
    lab: { a: false, b: false }, cam: 'orbit',
  },
  storm: {
    label: 'Open Storm',
    p: { beaufort: 8, wind: 20.7, fetch: 400, chop: 1.3, relief: 1.6, hour: 11.2, cloud: 0.85,
         cascChop: 1.2, breakAmp: 1.3, spray: 1.4, particles: 45000, foamAmt: 1.25, whitecap: 1.2 },
    lab: { a: false, b: false }, cam: 'aerial',
  },
  surf: {
    label: 'Surf Break',
    p: { beaufort: 4, wind: 9.0, fetch: 160, windDir: 14, chop: 0.95, relief: 1.6,
         hour: 16.8, cloud: 0.3,
         cascSwell: 1.25, cascSea: 1.0, cascChop: 0.35, surfOn: 1,
         breakAmp: 1.35, barrel: 2.2, peelSpeed: 8.0, spray: 1.2, particles: 34000 },
    lab: { a: false, b: false }, cam: 'surf',
  },
  cancel: {
    label: 'Wave Cancel',
    p: { beaufort: 4, wind: 7.9, fetch: 140, windDir: 38, chop: 1.0, relief: 1.4,
         hour: 10.5, cloud: 0.4 },
    lab: { a: { on: true, lambda: 60, amp: 1.4, dir: 90, phase: 0 },
           b: { on: true, lambda: 60, amp: 1.4, dir: 90, phase: 180 } },
    cam: 'lab',
  },
  cross: {
    label: 'Crossing Seas',
    p: { beaufort: 3, wind: 5.4, fetch: 140, chop: 0.9, relief: 1.4, hour: 13.5, cloud: 0.5 },
    lab: { a: { on: true, lambda: 80, amp: 1.1, dir: 60, phase: 0 },
           b: { on: true, lambda: 55, amp: 1.0, dir: -35, phase: 90 } },
    cam: 'lab',
  },
};

export const CAMS = {
  orbit: { pos: [8, 7, 54],      tgt: [-45, 2, -12] },
  surf:  { pos: [-2, 4.5, 52],   tgt: [-75, 4, -18] },
  shore: { pos: [112, 9, 62],    tgt: [-60, 2, -20] },
  aerial:{ pos: [-60, 230, 190], tgt: [-60, 0, -10] },
  lab:   { pos: [-190, 46, 195], tgt: [-190, 0, 80] },
  buoy:  { pos: [-128, 7, 152],  tgt: [-152, 1, 128] },
};

// ---------------------------------------------------------------------------
// Time-of-day palette (keyframed grading; sun path is analytic)
// ---------------------------------------------------------------------------
const TOD_STOPS = [
  { h: 5.0,  zen: 0x2a3f66, hor: 0xe8956b, gnd: 0x1a2233, sun: 0xffb37a, sunI: 0.75, fog: 0xc9a189, deep: 0x0a2438, shal: 0x1f7a74, sss: 0x2fd8c8 },
  { h: 7.0,  zen: 0x2f6fd0, hor: 0xcfe3ef, gnd: 0x2a3547, sun: 0xffdfb0, sunI: 1.05, fog: 0xc3d7e2, deep: 0x07304a, shal: 0x23908a, sss: 0x35e0d0 },
  { h: 12.0, zen: 0x1e5fc4, hor: 0xbfd9e8, gnd: 0x323c4e, sun: 0xfff4e0, sunI: 1.30, fog: 0xbcd3e2, deep: 0x062a44, shal: 0x1b8f8b, sss: 0x40e8d8 },
  { h: 16.5, zen: 0x2263c6, hor: 0xc4dcee, gnd: 0x323c4e, sun: 0xfff0d0, sunI: 1.20, fog: 0xc2d6e4, deep: 0x062b46, shal: 0x1e918c, sss: 0x40e0d0 },
  { h: 18.5, zen: 0x274b8f, hor: 0xff9e57, gnd: 0x2c2a40, sun: 0xff9a4d, sunI: 0.85, fog: 0xd9a37e, deep: 0x0a2c46, shal: 0x2a8d84, sss: 0x30d8c0 },
  { h: 19.75, zen: 0x1c2a52, hor: 0xb56576, gnd: 0x1e2033, sun: 0xff6f59, sunI: 0.35, fog: 0x8d6b7e, deep: 0x081d33, shal: 0x14605e, sss: 0x18a090 },
  { h: 20.5, zen: 0x101a38, hor: 0x4a4a72, gnd: 0x141625, sun: 0x7a5a8c, sunI: 0.12, fog: 0x3d4266, deep: 0x061423, shal: 0x0d3f3e, sss: 0x0a5048 },
];

const _a = new THREE.Color(), _b = new THREE.Color();
function lerpHex(out, ha, hb, f) {
  _a.setHex(ha); _b.setHex(hb);
  out.copy(_a).lerp(_b, f);
  return out;
}

export function todPalette(hour, out) {
  out = out || {};
  out.zen = out.zen || new THREE.Color();
  out.hor = out.hor || new THREE.Color();
  out.gnd = out.gnd || new THREE.Color();
  out.sun = out.sun || new THREE.Color();
  out.fog = out.fog || new THREE.Color();
  out.deep = out.deep || new THREE.Color();
  out.shal = out.shal || new THREE.Color();
  out.sss = out.sss || new THREE.Color();
  out.sunDir = out.sunDir || new THREE.Vector3(0, 1, 0);

  const S = TOD_STOPS;
  let a = S[0], b = S[1];
  if (hour <= S[0].h) { a = S[0]; b = S[1]; }
  else if (hour >= S[S.length - 1].h) { a = S[S.length - 2]; b = S[S.length - 1]; }
  else {
    for (let k = 0; k < S.length - 1; k++) {
      if (hour >= S[k].h && hour <= S[k + 1].h) { a = S[k]; b = S[k + 1]; break; }
    }
  }
  let f = (hour - a.h) / Math.max(b.h - a.h, 1e-4);
  f = Math.min(Math.max(f, 0), 1);
  f = f * f * (3 - 2 * f);

  lerpHex(out.zen, a.zen, b.zen, f);
  lerpHex(out.hor, a.hor, b.hor, f);
  lerpHex(out.gnd, a.gnd, b.gnd, f);
  lerpHex(out.sun, a.sun, b.sun, f);
  lerpHex(out.fog, a.fog, b.fog, f);
  lerpHex(out.deep, a.deep, b.deep, f);
  lerpHex(out.shal, a.shal, b.shal, f);
  lerpHex(out.sss, a.sss, b.sss, f);
  out.sunI = a.sunI + (b.sunI - a.sunI) * f;

  // analytic sun path: rises +X (over the beach), sets -X (over the ocean)
  const dayT = Math.min(Math.max((hour - 5.2) / (20.3 - 5.2), 0), 1);
  const elev = Math.sin(dayT * Math.PI) * 1.02 + 0.035;
  const azim = -1.15 + dayT * 2.30;
  out.sunDir.set(Math.cos(elev) * Math.cos(azim), Math.sin(elev), Math.cos(elev) * Math.sin(azim)).normalize();
  out.elev01 = Math.min(Math.max(Math.sin(elev) * 2.2, 0), 1);
  return out;
}
