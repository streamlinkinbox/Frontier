//============================================================================================================================================
//                                                             ALIGNMENT.JS
//============================================================================================================================================
// Vertical alignment: the long section of a corridor, and whether it is any good.
//
// Plan geometry is only half of a road. The other half is the profile — chainage against elevation — and the rules
// that go with it: how steep a grade a lorry will climb, how long a crest has to be before a driver can see over
// it, how tight a sag can get before the suspension bottoms out, and how tight a bend can be at speed.
//
// Everything here is pure maths over a sampled polyline: no three.js, no DOM. `profileOf` turns a corridor into
// stations carrying grade, vertical curvature and plan radius; `designChecks` reads those against the standards
// table for a design speed and hands back plain-language warnings with the chainage they apply to.
//
// Vertical curves are described by K, the length of curve per percent of grade change (K = L / A). A crest with
// K = 11 at 60 km/h just gives stopping sight distance; one with K = 3 does not. The tables below are the usual
// AASHTO/Austroads-style minima, rounded, and are meant as a sanity check rather than a substitute for a designer.

// Design speed (km/h) → { radius: minimum plan radius (m), crest/sag: minimum K, grade: maximum grade (%) }
export const DESIGN_STANDARDS = {
  30: { radius: 25, crest: 2, sag: 6, grade: 9 },
  40: { radius: 47, crest: 4, sag: 9, grade: 9 },
  50: { radius: 79, crest: 7, sag: 13, grade: 8 },
  60: { radius: 123, crest: 11, sag: 18, grade: 7 },
  70: { radius: 175, crest: 17, sag: 23, grade: 7 },
  80: { radius: 229, crest: 26, sag: 30, grade: 6 },
  90: { radius: 304, crest: 39, sag: 38, grade: 6 },
  100: { radius: 394, crest: 52, sag: 45, grade: 5 },
  110: { radius: 501, crest: 74, sag: 55, grade: 5 },
  120: { radius: 667, crest: 95, sag: 63, grade: 4 },
};

export const DESIGN_SPEEDS = Object.keys(DESIGN_STANDARDS).map(Number).sort((a, b) => a - b);

export function standardsFor(speed) {
  let best = DESIGN_SPEEDS[0];
  for (const s of DESIGN_SPEEDS) if (Math.abs(s - speed) < Math.abs(best - speed)) best = s;
  return { speed: best, ...DESIGN_STANDARDS[best] };
}

// ── profile sampling ──────────────────────────────────────────────────────────────────────────────────────────────

// `points` is a sampled polyline ({x,y,z}); `window` is the half-length in metres over which grades and curvatures
// are measured, which keeps the numbers readable instead of chasing sampling noise.
export function profileOf(points, { window: win = 8 } = {}) {
  const stations = [];
  if (!points || points.length < 2) return { stations, length: 0 };
  const chain = [0];
  for (let i = 1; i < points.length; i++) {
    chain.push(chain[i - 1] + Math.hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y));
  }
  const length = chain[chain.length - 1];

  const at = (s) => {
    const t = Math.max(0, Math.min(length, s));
    let i = 1;
    while (i < chain.length - 1 && chain[i] < t) i++;
    const a = points[i - 1];
    const b = points[i];
    const span = chain[i] - chain[i - 1] || 1;
    const f = (t - chain[i - 1]) / span;
    return { x: a.x + (b.x - a.x) * f, y: a.y + (b.y - a.y) * f, z: a.z + (b.z - a.z) * f };
  };

  for (let i = 0; i < points.length; i++) {
    const s = chain[i];
    const back = at(s - win);
    const fwd = at(s + win);
    const here = points[i];
    const span = Math.min(length, s + win) - Math.max(0, s - win);
    const grade = span > 1e-6 ? ((fwd.z - back.z) / span) * 100 : 0;
    // Second derivative of elevation → K = 1 / (100 · z''). Signed: negative z'' is a crest.
    const h = Math.max(1, win);
    const zpp = (at(s + h).z - 2 * here.z + at(s - h).z) / (h * h);
    const k = Math.abs(zpp) > 1e-7 ? 1 / (100 * Math.abs(zpp)) : Infinity;
    stations.push({
      s,
      point: here,
      z: here.z,
      grade,
      curveType: Math.abs(zpp) < 1e-7 ? 'straight' : zpp < 0 ? 'crest' : 'sag',
      k,
      radius: planRadius(points, i),
    });
  }
  return { stations, length };
}

// Menger curvature of three consecutive plan points → radius of the circle through them.
export function planRadius(points, i) {
  if (i <= 0 || i >= points.length - 1) return Infinity;
  const a = points[i - 1];
  const b = points[i];
  const c = points[i + 1];
  const ab = Math.hypot(b.x - a.x, b.y - a.y);
  const bc = Math.hypot(c.x - b.x, c.y - b.y);
  const ca = Math.hypot(a.x - c.x, a.y - c.y);
  const area2 = Math.abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
  if (area2 < 1e-9) return Infinity;
  return (ab * bc * ca) / (2 * area2);
}

// ── design checks ─────────────────────────────────────────────────────────────────────────────────────────────────

// Returns one entry per offending stretch, merged so a long steep hill is a single warning rather than fifty.
export function designChecks(profile, { designSpeed = 60, radiusWindow = 12 } = {}) {
  const std = standardsFor(designSpeed);
  const issues = [];
  const open = new Map();

  const flag = (kind, station, text, value) => {
    const current = open.get(kind);
    if (current && station.s - current.to <= radiusWindow) {
      current.to = station.s;
      current.worst = kind === 'radius' || kind === 'crest' || kind === 'sag'
        ? Math.min(current.worst, value)
        : Math.max(current.worst, Math.abs(value));
      return;
    }
    if (current) issues.push(current);
    open.set(kind, { kind, from: station.s, to: station.s, worst: kind === 'grade' ? Math.abs(value) : value, text });
  };
  const close = (kind) => {
    const current = open.get(kind);
    if (current) {
      issues.push(current);
      open.delete(kind);
    }
  };

  for (const station of profile.stations) {
    if (Math.abs(station.grade) > std.grade) flag('grade', station, 'gradient steeper than the standard', station.grade);
    else close('grade');

    if (station.curveType === 'crest' && station.k < std.crest) flag('crest', station, 'crest curve too sharp for sight distance', station.k);
    else close('crest');

    if (station.curveType === 'sag' && station.k < std.sag) flag('sag', station, 'sag curve too sharp', station.k);
    else close('sag');

    if (station.radius < std.radius) flag('radius', station, 'plan radius below the minimum', station.radius);
    else close('radius');
  }
  for (const kind of [...open.keys()]) close(kind);

  return issues.map((issue) => ({
    ...issue,
    speed: std.speed,
    limit: issue.kind === 'grade' ? std.grade : issue.kind === 'radius' ? std.radius : issue.kind === 'crest' ? std.crest : std.sag,
    label: describe(issue, std),
  }));
}

function describe(issue, std) {
  const span = issue.to - issue.from < 2 ? `ch ${issue.from.toFixed(0)} m` : `ch ${issue.from.toFixed(0)}–${issue.to.toFixed(0)} m`;
  switch (issue.kind) {
    case 'grade':
      return `${span}: ${issue.worst.toFixed(1)} % grade exceeds the ${std.grade} % maximum at ${std.speed} km/h`;
    case 'crest':
      return `${span}: crest K ${issue.worst.toFixed(1)} is below the ${std.crest} needed at ${std.speed} km/h`;
    case 'sag':
      return `${span}: sag K ${issue.worst.toFixed(1)} is below the ${std.sag} needed at ${std.speed} km/h`;
    default:
      return `${span}: radius ${issue.worst.toFixed(0)} m is below the ${std.radius} m minimum at ${std.speed} km/h`;
  }
}

// Handy summary for the inspector: the extremes of a corridor in one object.
export function profileSummary(profile) {
  let maxGrade = 0;
  let minRadius = Infinity;
  let minCrest = Infinity;
  let minSag = Infinity;
  let rise = 0;
  let fall = 0;
  const stations = profile.stations;
  for (let i = 0; i < stations.length; i++) {
    const st = stations[i];
    if (Math.abs(st.grade) > Math.abs(maxGrade)) maxGrade = st.grade;
    if (st.radius < minRadius) minRadius = st.radius;
    if (st.curveType === 'crest') minCrest = Math.min(minCrest, st.k);
    if (st.curveType === 'sag') minSag = Math.min(minSag, st.k);
    if (i > 0) {
      const dz = st.z - stations[i - 1].z;
      if (dz > 0) rise += dz;
      else fall -= dz;
    }
  }
  return { maxGrade, minRadius, minCrest, minSag, rise, fall, length: profile.length };
}
