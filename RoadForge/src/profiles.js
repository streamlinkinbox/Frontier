/* RoadForge — road cross-section profiles (real-world dimensions, metres). */
(function (RF) {
  'use strict';
  // Lane widths follow typical design standards: 3.0–3.3 m local streets, 3.5 m arterials, 3.65 m (12 ft) motorways.
  const PROFILES = {
    street: { name: 'Local street', lanesF: 1, lanesB: 1, laneWidth: 3.3, parkingL: 0, parkingR: 0, median: 0, medianKind: 'none', shoulder: 0, sidewalkL: 2.4, sidewalkR: 2.4, curbH: 0.125, speed: 50, cornerRadius: 6, cycle: 0, verge: 0, kind: 'urban' },
    residential: { name: 'Residential street', lanesF: 1, lanesB: 1, laneWidth: 3.0, parkingL: 2.2, parkingR: 2.2, median: 0, medianKind: 'none', shoulder: 0, sidewalkL: 2.0, sidewalkR: 2.0, curbH: 0.125, speed: 30, cornerRadius: 5, cycle: 0, verge: 0, kind: 'urban' },
    collector: { name: 'Collector (2+2)', lanesF: 2, lanesB: 2, laneWidth: 3.3, parkingL: 0, parkingR: 0, median: 0, medianKind: 'painted', shoulder: 0, sidewalkL: 3.0, sidewalkR: 3.0, curbH: 0.125, speed: 50, cornerRadius: 9, cycle: 0, verge: 0, kind: 'urban' },
    avenue: { name: 'Avenue (2+2, raised median)', lanesF: 2, lanesB: 2, laneWidth: 3.5, parkingL: 2.2, parkingR: 2.2, median: 2.0, medianKind: 'raised', shoulder: 0, sidewalkL: 4.0, sidewalkR: 4.0, curbH: 0.125, speed: 60, cornerRadius: 10, cycle: 0, verge: 0, kind: 'urban' },
    boulevard: { name: 'Boulevard (3+3, cycle tracks)', lanesF: 3, lanesB: 3, laneWidth: 3.5, parkingL: 0, parkingR: 0, median: 4.0, medianKind: 'raised', shoulder: 0, sidewalkL: 5.0, sidewalkR: 5.0, curbH: 0.125, speed: 60, cornerRadius: 12, cycle: 2.0, verge: 0, kind: 'urban' },
    oneway: { name: 'One-way street (2 lanes)', lanesF: 2, lanesB: 0, laneWidth: 3.2, parkingL: 2.2, parkingR: 0, median: 0, medianKind: 'none', shoulder: 0, sidewalkL: 2.5, sidewalkR: 2.5, curbH: 0.125, speed: 50, cornerRadius: 6, cycle: 0, verge: 0, kind: 'urban' },
    highstreet: { name: 'High street (2+2, bus lanes)', lanesF: 2, lanesB: 2, laneWidth: 3.3, parkingL: 0, parkingR: 0, median: 0, medianKind: 'painted', shoulder: 0, sidewalkL: 3.5, sidewalkR: 3.5, curbH: 0.125, speed: 50, cornerRadius: 10, cycle: 0, bus: 3.3, verge: 0, kind: 'urban' },
    cyclestreet: { name: 'Cycle street (1+1, cycle lanes)', lanesF: 1, lanesB: 1, laneWidth: 3.25, parkingL: 0, parkingR: 2.0, median: 0, medianKind: 'none', shoulder: 0, sidewalkL: 2.5, sidewalkR: 2.5, curbH: 0.125, speed: 30, cornerRadius: 6, cycle: 1.8, verge: 0, kind: 'urban' },
    service: { name: 'Service lane', lanesF: 1, lanesB: 0, laneWidth: 3.5, parkingL: 0, parkingR: 0, median: 0, medianKind: 'none', shoulder: 0, sidewalkL: 1.2, sidewalkR: 1.2, curbH: 0.1, speed: 20, cornerRadius: 4, cycle: 0, verge: 0, kind: 'urban' },
    rural: { name: 'Rural road', lanesF: 1, lanesB: 1, laneWidth: 3.5, parkingL: 0, parkingR: 0, median: 0, medianKind: 'none', shoulder: 1.0, sidewalkL: 0, sidewalkR: 0, curbH: 0, speed: 80, cornerRadius: 12, cycle: 0, verge: 2.0, kind: 'rural' },
    highway: { name: 'Motorway (3+3, barrier median)', lanesF: 3, lanesB: 3, laneWidth: 3.65, parkingL: 0, parkingR: 0, median: 1.2, medianKind: 'barrier', shoulder: 3.0, shoulderInner: 1.0, sidewalkL: 0, sidewalkR: 0, curbH: 0, speed: 120, cornerRadius: 3, cycle: 0, verge: 3.0, kind: 'highway' },
    highway2: { name: 'Motorway (2+2, barrier median)', lanesF: 2, lanesB: 2, laneWidth: 3.65, parkingL: 0, parkingR: 0, median: 1.2, medianKind: 'barrier', shoulder: 3.0, shoulderInner: 1.0, sidewalkL: 0, sidewalkR: 0, curbH: 0, speed: 120, cornerRadius: 3, cycle: 0, verge: 3.0, kind: 'highway' },
    carriageway: { name: 'Motorway carriageway (one-way 2)', lanesF: 2, lanesB: 0, laneWidth: 3.65, parkingL: 0, parkingR: 0, median: 0, medianKind: 'none', shoulder: 3.0, shoulderInner: 1.0, sidewalkL: 0, sidewalkR: 0, curbH: 0, speed: 120, cornerRadius: 3, cycle: 0, verge: 3.0, kind: 'highway' },
    carriageway3: { name: 'Motorway carriageway (one-way 3)', lanesF: 3, lanesB: 0, laneWidth: 3.65, parkingL: 0, parkingR: 0, median: 0, medianKind: 'none', shoulder: 3.0, shoulderInner: 1.0, sidewalkL: 0, sidewalkR: 0, curbH: 0, speed: 120, cornerRadius: 3, cycle: 0, verge: 3.0, kind: 'highway' },
    ramp: { name: 'Interchange ramp (one-way)', lanesF: 1, lanesB: 0, laneWidth: 4.5, parkingL: 0, parkingR: 0, median: 0, medianKind: 'none', shoulder: 2.0, shoulderInner: 1.0, sidewalkL: 0, sidewalkR: 0, curbH: 0, speed: 60, cornerRadius: 2, cycle: 0, verge: 2.0, kind: 'highway' },
    loop: { name: 'Loop ramp (one-way)', lanesF: 1, lanesB: 0, laneWidth: 4.8, parkingL: 0, parkingR: 0, median: 0, medianKind: 'none', shoulder: 1.5, shoulderInner: 1.0, sidewalkL: 0, sidewalkR: 0, curbH: 0, speed: 40, cornerRadius: 2, cycle: 0, verge: 2.0, kind: 'highway' },
    tunnel: { name: 'Tunnel (2+2)', lanesF: 2, lanesB: 2, laneWidth: 3.5, parkingL: 0, parkingR: 0, median: 0.6, medianKind: 'barrier', shoulder: 0.5, sidewalkL: 1.0, sidewalkR: 1.0, curbH: 0.2, speed: 70, cornerRadius: 8, cycle: 0, verge: 0, kind: 'tunnel' },
  };
  const PROFILE_KEYS = Object.keys(PROFILES);

  /** Effective profile = base profile merged with per-road overrides. */
  function effective(road) {
    const base = PROFILES[road.profile] || PROFILES.street;
    return Object.assign({}, base, road.overrides || {});
  }

  /**
   * Lateral layout along +n (right-hand side of travel direction for right-hand traffic).
   * Returns breakpoints and lane descriptors. All offsets in metres from the centreline.
   */
  function layout(prof, handedness = 'right') {
    const lw = prof.laneWidth;
    const lanes = [];
    const medW = prof.median || 0;
    const shoulder = prof.shoulder || 0, shoulderIn = prof.shoulderInner || 0;
    const cycle = prof.cycle || 0, bus = prof.bus || 0;
    // forward lanes on +n for right-hand traffic
    const sgn = handedness === 'right' ? 1 : -1;
    const half = medW / 2;
    // forward side
    let t = half;
    if (prof.lanesF > 0 && prof.lanesB > 0 && shoulderIn > 0 && medW > 0) { lanes.push({ t0: t, t1: t + shoulderIn, kind: 'shoulder', dir: 1 }); t += shoulderIn; }
    for (let i = 0; i < prof.lanesF; i++) { lanes.push({ t0: t, t1: t + lw, kind: 'drive', dir: 1, index: i }); t += lw; }
    if (bus > 0) { lanes.push({ t0: t, t1: t + bus, kind: 'bus', dir: 1 }); t += bus; }
    if (cycle > 0) { lanes.push({ t0: t, t1: t + cycle, kind: 'cycle', dir: 1 }); t += cycle; }
    if (prof.parkingR > 0) { lanes.push({ t0: t, t1: t + prof.parkingR, kind: 'parking', dir: 1 }); t += prof.parkingR; }
    if (shoulder > 0) { lanes.push({ t0: t, t1: t + shoulder, kind: 'shoulder', dir: 1 }); t += shoulder; }
    const hwF = t;
    // backward side
    t = -half;
    if (prof.lanesB > 0) {
      if (prof.lanesF > 0 && shoulderIn > 0 && medW > 0) { lanes.push({ t0: t - shoulderIn, t1: t, kind: 'shoulder', dir: -1 }); t -= shoulderIn; }
      for (let i = 0; i < prof.lanesB; i++) { lanes.push({ t0: t - lw, t1: t, kind: 'drive', dir: -1, index: i }); t -= lw; }
      if (bus > 0) { lanes.push({ t0: t - bus, t1: t, kind: 'bus', dir: -1 }); t -= bus; }
      if (cycle > 0) { lanes.push({ t0: t - cycle, t1: t, kind: 'cycle', dir: -1 }); t -= cycle; }
      if (prof.parkingL > 0) { lanes.push({ t0: t - prof.parkingL, t1: t, kind: 'parking', dir: -1 }); t -= prof.parkingL; }
      if (shoulder > 0) { lanes.push({ t0: t - shoulder, t1: t, kind: 'shoulder', dir: -1 }); t -= shoulder; }
    } else {
      // one-way: left side parking/shoulder, no median
      if (prof.parkingL > 0) { lanes.push({ t0: t - prof.parkingL, t1: t, kind: 'parking', dir: 1 }); t -= prof.parkingL; }
      if (shoulderIn > 0 && prof.kind === 'highway') { lanes.push({ t0: t - shoulderIn, t1: t, kind: 'shoulder', dir: 1 }); t -= shoulderIn; }
      else if (shoulder > 0 && prof.kind !== 'highway') { lanes.push({ t0: t - shoulder, t1: t, kind: 'shoulder', dir: 1 }); t -= shoulder; }
    }
    const hwB = -t;
    if (medW > 0 && prof.lanesB > 0) lanes.push({ t0: -half, t1: half, kind: 'median', dir: 0 });
    let hwL, hwR;
    if (sgn === 1) { hwR = hwF; hwL = hwB; } else { hwR = hwB; hwL = hwF; lanes.forEach((l) => { const a = -l.t1, b = -l.t0; l.t0 = a; l.t1 = b; l.dir = -l.dir; }); }
    lanes.sort((a, b) => a.t0 - b.t0);
    const hasCurb = prof.curbH > 0;
    const curbW = hasCurb ? 0.15 : 0;
    const sidewalkL = prof.sidewalkL || 0, sidewalkR = prof.sidewalkR || 0;
    const verge = prof.verge || 0;
    return {
      lanes, hwL, hwR, hasCurb, curbW, curbH: prof.curbH || 0, sidewalkL, sidewalkR, verge,
      medianKind: medW > 0 && prof.lanesB > 0 ? prof.medianKind : 'none', medianW: prof.lanesB > 0 ? medW : 0,
      outerL: hwL + curbW + sidewalkL + verge, outerR: hwR + curbW + sidewalkR + verge,
      driveLanes: lanes.filter((l) => l.kind === 'drive'),
      kind: prof.kind, speed: prof.speed,
    };
  }

  RF.PROFILES = PROFILES; RF.PROFILE_KEYS = PROFILE_KEYS;
  RF.profiles = { effective, layout };
})(globalThis.RF || (globalThis.RF = {}));
