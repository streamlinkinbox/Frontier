/* RoadForge — scenario templates (networks are plain project data; everything else is generated). */
(function (RF) {
  'use strict';
  const V = RF.V;
  function P(name) { const p = RF.scene.emptyProject(name); p._n = 0; return p; }
  function N(p, id, x, y, z, extra) { p.nodes[id] = { id, p: [x, y, z], control: 'auto', name: extra?.name || 'Node', ...(extra || {}) }; return id; }
  function R(p, id, a, b, profile, pts = [], extra = {}) { p.roads[id] = { id, name: extra.name || (RF.PROFILES[profile]?.name || profile), a, b, profile, pts, features: {}, props: {}, ...extra }; return id; }
  function block(p, id, poly, extra = {}) { p.blocks[id] = { id, name: extra.name || 'City block', poly, surface: extra.surface || 'paving', sidewalk: extra.sidewalk ?? 3, ...extra }; }
  const arc = (cx, cz, r, a0, a1, n, y = 0) => { const out = []; for (let i = 1; i < n; i++) { const a = a0 + ((a1 - a0) * i) / n; out.push([cx + Math.cos(a) * r, y, cz + Math.sin(a) * r]); } return out; };

  const list = {
    crossroads: {
      name: 'Downtown crossroads', blurb: 'Avenue × street signalised crossroads with potholes, a speed table and parking.',
      make() {
        const p = P('Downtown crossroads');
        N(p, 'c', 0, 0, 0, { name: 'Main & 1st' }); N(p, 'n', 0, 0, -160); N(p, 's', 0, 0, 160); N(p, 'e', 170, 0, 0); N(p, 'w', -170, 0, 0);
        R(p, 'r1', 'n', 'c', 'avenue', [], { name: 'Main Avenue (N)' }); R(p, 'r2', 'c', 's', 'avenue', [], { name: 'Main Avenue (S)' });
        R(p, 'r3', 'w', 'c', 'street', [], { name: '1st Street (W)', features: { potholes: [{ s: 60, t: 1.4, r: 0.7, depth: 0.07 }, { s: 75, t: -1.2, r: 0.5, depth: 0.05 }], speedBumps: [{ s: 110 }] }, props: { bollards: 'steel' } });
        R(p, 'r4', 'c', 'e', 'residential', [], { name: '1st Street (E)', props: { bollards: 'cast' } });
        for (const [sx, sz] of [[1, 1], [1, -1], [-1, 1], [-1, -1]]) block(p, 'b' + sx + sz, [[sx * 16, sz * 12], [sx * 150, sz * 12], [sx * 150, sz * 140], [sx * 16, sz * 140]], { surface: 'paving' });
        return p;
      },
    },
    downtown: {
      name: 'Downtown grid', blurb: '3 × 3 city grid with a boulevard, one-way pairs, a collector and generated city blocks.',
      make() {
        const p = P('Downtown grid');
        const xs = [-220, 0, 220], zs = [-200, 0, 200];
        const prof = { x: ['street', 'boulevard', 'oneway'], z: ['collector', 'avenue', 'residential'] };
        for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) N(p, `n${i}${j}`, xs[i], 0, zs[j], { name: `Node ${i}${j}` });
        for (let i = 0; i < 3; i++) { N(p, `n${i}S`, xs[i], 0, -360); N(p, `n${i}N`, xs[i], 0, 360); }
        for (let j = 0; j < 3; j++) { N(p, `nW${j}`, -380, 0, zs[j]); N(p, `nE${j}`, 380, 0, zs[j]); }
        for (let i = 0; i < 3; i++) { const pr = prof.z[i]; R(p, `rz${i}_0`, `n${i}S`, `n${i}0`, pr); R(p, `rz${i}_1`, `n${i}0`, `n${i}1`, pr); R(p, `rz${i}_2`, `n${i}1`, `n${i}2`, pr); R(p, `rz${i}_3`, `n${i}2`, `n${i}N`, pr); }
        for (let j = 0; j < 3; j++) { const pr = prof.x[j]; R(p, `rx${j}_0`, `nW${j}`, `n0${j}`, pr); R(p, `rx${j}_1`, `n0${j}`, `n1${j}`, pr); R(p, `rx${j}_2`, `n1${j}`, `n2${j}`, pr); R(p, `rx${j}_3`, `n2${j}`, `nE${j}`, pr); }
        p.roads.rz2_1.features = { speedBumps: [{ s: 80 }, { s: 130 }] };
        p.roads.rx1_1.props = { bollards: 'steel' };
        const hw = { street: 10.5, boulevard: 20, oneway: 9.6, collector: 11.8, avenue: 16, residential: 9.2 };
        for (let i = 0; i < 2; i++) for (let j = 0; j < 2; j++) {
          const x0 = xs[i] + hw[prof.z[i]] + 0.3, x1 = xs[i + 1] - hw[prof.z[i + 1]] - 0.3, z0 = zs[j] + hw[prof.x[j]] + 0.3, z1 = zs[j + 1] - hw[prof.x[j + 1]] - 0.3;
          block(p, `b${i}${j}`, [[x0, z0], [x1, z0], [x1, z1], [x0, z1]], { surface: (i + j) % 2 ? 'grass' : 'paving', name: `Block ${i}${j}` });
        }
        return p;
      },
    },
    diamond: {
      name: 'Diamond interchange', blurb: 'Elevated dual carriageway over an avenue with four ramps and signalised ramp terminals.',
      make() {
        const p = P('Diamond interchange');
        const H = 7.0; // deck level: 5.3 m headroom + 1.5 m structure
        // carriageways (one-way pairs) along x at z = ±9
        for (const [tag, z, dir] of [['EB', 9, 1], ['WB', -9, -1]]) {
          const xs = dir > 0 ? [-700, -330, 330, 700] : [700, 330, -330, -700];
          xs.forEach((x, i) => N(p, `${tag}${i}`, x, i === 0 || i === 3 ? 0 : H, z, { name: `${tag} ${i}` }));
          const pts = (i) => (i === 0 ? [[dir * -520, 0, z], [dir * -420, H, z]] : i === 2 ? [[dir * 420, H, z], [dir * 520, 0, z]] : []);
          for (let i = 0; i < 3; i++) R(p, `${tag}r${i}`, `${tag}${i}`, `${tag}${i + 1}`, 'carriageway', pts(i), { name: `Motorway ${tag} ${i + 1}` });
        }
        // avenue N-S at ground with ramp terminals at z = ±95
        N(p, 'aN', 0, 0, -420); N(p, 'tN', 0, 0, -95, { name: 'North terminal', control: 'signals' }); N(p, 'aC', 0, 0, 0); N(p, 'tS', 0, 0, 95, { name: 'South terminal', control: 'signals' }); N(p, 'aS', 0, 0, 420);
        R(p, 'av0', 'aN', 'tN', 'avenue'); R(p, 'av1', 'tN', 'tS', 'avenue', [], { name: 'Avenue (under)' }); R(p, 'av2', 'tS', 'aS', 'avenue');
        // ramps: EB off-ramp from EB1 (x=-330, z=9) to tS (0, 95); EB on-ramp from tS to EB2 (330, 9)
        R(p, 'rampEBoff', 'EB1', 'tS', 'ramp', [[-250, 5.5, 22], [-120, 2, 60], [-40, 0, 90]], { name: 'EB off-ramp' });
        R(p, 'rampEBon', 'tS', 'EB2', 'ramp', [[40, 0, 90], [120, 2, 60], [250, 5.5, 22]], { name: 'EB on-ramp' });
        R(p, 'rampWBoff', 'WB1', 'tN', 'ramp', [[250, 5.5, -22], [120, 2, -60], [40, 0, -90]], { name: 'WB off-ramp' });
        R(p, 'rampWBon', 'tN', 'WB2', 'ramp', [[-40, 0, -90], [-120, 2, -60], [-250, 5.5, -22]], { name: 'WB on-ramp' });
        p.roads.EBr1.props.billboards = [{ s: 120, side: 1 }];
        return p;
      },
    },
    cloverleaf: {
      name: 'Cloverleaf', blurb: 'Full cloverleaf: two motorways, four loop ramps (R 62 m) and four outer connectors (R 400 m) built parametrically around the crossing.',
      make() {
        const p = P('Cloverleaf');
        const H = 7.5, off = 10; // deck level of road B; carriageway centre offset from each axis
        const Rl = 62, Dl = 16, Lel = 40, Lp = 40; // loop radius, running offset from the mainline centreline, exit/entry taper, parallel run before the arc
        const Ro = 400, Do = 55, Leo = 150;        // outer connector radius, running offset, taper length (clears the loops: (Ro+Do)·√2 − Ro > loop extent)
        const SS = (u) => RF.smoothstep(0, 1, u);
        // carriageways: travel direction d, right-hand normal r (right-hand traffic: r = (-d.z, d.x)), centreline o = r * off
        const cw = {
          AE: { d: [1, 0], y: 0, name: 'Motorway A eastbound' }, AW: { d: [-1, 0], y: 0, name: 'Motorway A westbound' },
          BS: { d: [0, 1], y: H, name: 'Motorway B southbound' }, BN: { d: [0, -1], y: H, name: 'Motorway B northbound' },
        };
        for (const c of Object.values(cw)) { c.r = [-c.d[1], c.d[0]]; c.o = [c.r[0] * off, c.r[1] * off]; c.marks = []; }
        const byDir = (d) => Object.entries(cw).find(([, c]) => Math.abs(c.d[0] - d[0]) < 1e-6 && Math.abs(c.d[1] - d[1]) < 1e-6)[0];
        const at = (c, t, lat = 0) => [c.o[0] + c.d[0] * t + c.r[0] * lat, c.o[1] + c.d[1] * t + c.r[1] * lat];
        const tOf = (c, q) => (q[0] - c.o[0]) * c.d[0] + (q[1] - c.o[1]) * c.d[1];
        const addMark = (tag, t, kind) => { cw[tag].marks.push({ t, kind }); return `${tag}_${kind}`; };
        const ramps = [];
        for (const [fromTag, from] of Object.entries(cw)) {
          // --- loop (left turn): exit after the crossing on the right, 270° right-hand loop, merge from the right before the crossing of the target
          const toTag = byDir([-from.r[0], -from.r[1]]), to = cw[toTag];
          const X = [from.o[0] + to.o[0], from.o[1] + to.o[1]];
          const tDiv = tOf(from, X) + (Rl + Dl + Lp - Lel), tMer = tOf(to, X) - (Rl + Dl + Lp - Lel);
          const nA = addMark(fromTag, tDiv, 'loopDiv'), nB = addMark(toTag, tMer, 'loopMer');
          const c = [X[0] + (from.d[0] + from.r[0]) * (Rl + Dl + Lp), X[1] + (from.d[1] + from.r[1]) * (Rl + Dl + Lp)];
          const pts = [];
          for (let k = 1; k <= 3; k++) pts.push(at(from, tDiv + (Lel * k) / 4, Dl * SS(k / 4)));
          for (let i = 0; i <= 18; i++) { const a = (i / 18) * Math.PI * 1.5; pts.push([c[0] + Rl * (-from.r[0] * Math.cos(a) + from.d[0] * Math.sin(a)), c[1] + Rl * (-from.r[1] * Math.cos(a) + from.d[1] * Math.sin(a))]); }
          for (let k = 1; k <= 3; k++) pts.push(at(to, tMer - Lel + (Lel * k) / 4, Dl * (1 - SS(k / 4))));
          ramps.push({ id: `loop${fromTag}${toTag}`, a: nA, b: nB, profile: 'loop', pts, y0: from.y, y1: to.y, name: `Loop ${fromTag}→${toTag}` });
          // --- outer connector (right turn): exit well before the crossing, R 300 m quarter circle outside the loop, merge well after
          const to2Tag = byDir(from.r), to2 = cw[to2Tag];
          const X2 = [from.o[0] + to2.o[0], from.o[1] + to2.o[1]];
          const tDiv2 = tOf(from, X2) - (Ro + Do + Leo), tMer2 = tOf(to2, X2) + (Ro + Do + Leo);
          const nC = addMark(fromTag, tDiv2, 'outDiv'), nD = addMark(to2Tag, tMer2, 'outMer');
          const c2 = [X2[0] - from.d[0] * (Ro + Do) + from.r[0] * (Ro + Do), X2[1] - from.d[1] * (Ro + Do) + from.r[1] * (Ro + Do)];
          const pts2 = [];
          for (let k = 1; k <= 5; k++) pts2.push(at(from, tDiv2 + (Leo * k) / 6, Do * SS(k / 6)));
          for (let i = 0; i <= 12; i++) { const a = (i / 12) * Math.PI * 0.5; pts2.push([c2[0] + Ro * (-from.r[0] * Math.cos(a) + from.d[0] * Math.sin(a)), c2[1] + Ro * (-from.r[1] * Math.cos(a) + from.d[1] * Math.sin(a))]); }
          for (let k = 1; k <= 5; k++) pts2.push(at(to2, tMer2 - Leo + (Leo * k) / 6, Do * (1 - SS(k / 6))));
          ramps.push({ id: `outer${fromTag}${to2Tag}`, a: nC, b: nD, profile: 'ramp', pts: pts2, y0: from.y, y1: to2.y, name: `Connector ${fromTag}→${to2Tag}` });
        }
        // mainline nodes + segments (sorted by station)
        for (const [tag, c] of Object.entries(cw)) {
          const marks = [{ t: -900, kind: 'start' }, ...c.marks, { t: 900, kind: 'end' }].sort((u, v) => u.t - v.t);
          const ids = marks.map((m) => { const q = at(c, m.t); return N(p, `${tag}_${m.kind}`, q[0], c.y, q[1], { name: `${c.name} ${m.kind}` }); });
          for (let i = 0; i < ids.length - 1; i++) R(p, `${tag}r${i}`, ids[i], ids[i + 1], 'carriageway', [], { name: `${c.name} ${i + 1}` });
        }
        // ramps with a smooth grade between the two levels (by arc length)
        for (const rp of ramps) {
          const cum = [0]; for (let i = 1; i < rp.pts.length; i++) cum.push(cum[i - 1] + Math.hypot(rp.pts[i][0] - rp.pts[i - 1][0], rp.pts[i][1] - rp.pts[i - 1][1]));
          const L = cum[cum.length - 1];
          const pts = rp.pts.map((q, i) => [q[0], rp.y0 + (rp.y1 - rp.y0) * RF.smoothstep(0.12, 0.88, cum[i] / L), q[1]]);
          R(p, rp.id, rp.a, rp.b, rp.profile, pts, { name: rp.name });
        }
        return p;
      },
    },
    rail: {
      name: 'Rail district', blurb: 'Level crossing, road underpass beneath the railway, a road bridge over it and a subway line with trains.',
      make() {
        const p = P('Rail district');
        N(p, 'a0', -300, 0, -120); N(p, 'a1', 300, 0, -120); R(p, 'lvl', 'a0', 'a1', 'collector', [], { name: 'Crossing Street (level crossing)' });
        N(p, 'u0', -300, 0, 40); N(p, 'u1', 300, 0, 40); R(p, 'under', 'u0', 'u1', 'avenue', [[-140, 0, 40], [-60, -6.8, 40], [60, -6.8, 40], [140, 0, 40]], { name: 'Underpass Avenue' });
        N(p, 'b0', -300, 0, 200); N(p, 'b1', 300, 0, 200); R(p, 'over', 'b0', 'b1', 'street', [[-170, 0, 200], [-70, 7.2, 200], [70, 7.2, 200], [170, 0, 200]], { name: 'Overbridge Street' });
        N(p, 't0', -40, 0, -300); N(p, 't1', -40, 0, 320); R(p, 'tun', 't0', 't1', 'tunnel', [[-40, 0, -240], [-40, -9, -170], [-40, -9, 250], [-40, 0, 300]], { name: 'Tunnel Road', structure: 'auto' });
        p.rails.main = { id: 'main', name: 'Main line', kind: 'rail', electrified: true, trains: 1, cars: 5, pts: [[0, 0, -420], [0, 0, 420]] };
        p.rails.sub = { id: 'sub', name: 'Metro line', kind: 'subway', trains: 2, cars: 4, pts: [[-420, -13, 110], [420, -13, 110]] };
        return p;
      },
    },
    highway: {
      name: 'Rural motorway', blurb: 'Dual carriageway on embankment with an overbridge, billboards, guardrails and a works zone.',
      make() {
        const p = P('Rural motorway');
        N(p, 'h0', -900, 0, 0); N(p, 'h1', 900, 0, 0); R(p, 'hw', 'h0', 'h1', 'highway', [[-500, 1.5, -40], [-100, 2.5, 20], [300, 2.5, -10], [600, 1.2, 10]], { name: 'Motorway A1', features: { construction: [{ s0: 1150, s1: 1320, side: 1, width: 3.6 }] } });
        N(p, 'b0', 60, 0, -300); N(p, 'b1', 60, 0, 300); R(p, 'ob', 'b0', 'b1', 'rural', [[60, 0, -180], [60, 9.0, -60], [60, 9.0, 60], [60, 0, 180]], { name: 'Overbridge lane' });
        N(p, 'c0', -300, 0, 260); N(p, 'c1', 400, 0, 260); R(p, 'cr', 'c0', 'c1', 'rural', [[0, 0, 300]], { name: 'Country road' });
        return p;
      },
    },
    construction: {
      name: 'Street works', blurb: 'Urban street with a works zone, potholes, speed tables, pedestrian railings and bollard designs.',
      make() {
        const p = P('Street works');
        N(p, 'a', -220, 0, 0); N(p, 'j', 0, 0, 0, { name: 'T-junction' }); N(p, 'b', 220, 0, 0); N(p, 'c', 0, 0, 200);
        R(p, 'w', 'a', 'j', 'collector', [], { name: 'Works Street (W)', features: { construction: [{ s0: 70, s1: 150, side: 1, width: 3.4 }], potholes: [{ s: 40, t: 2, r: 0.6, depth: 0.06 }] }, props: { bollards: 'striped' } });
        R(p, 'e', 'j', 'b', 'collector', [[120, 0, -30]], { name: 'Works Street (E)', features: { speedBumps: [{ s: 90 }] }, props: { bollards: 'wood' } });
        R(p, 's', 'j', 'c', 'residential', [[20, 0, 110]], { name: 'Side Street', features: { potholes: [{ s: 50, t: -1, r: 0.9, depth: 0.08 }] }, props: { bollards: 'concrete' } });
        block(p, 'b1', [[12, 12], [200, 10], [200, 150], [40, 150]], { surface: 'paving' });
        block(p, 'b2', [[-200, 12], [-14, 12], [-14, 150], [-200, 150]], { surface: 'grass' });
        return p;
      },
    },
  };
  RF.templates = { list, helpers: { N, R, block, arc } };
})(globalThis.RF || (globalThis.RF = {}));
