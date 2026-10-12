// Junction surface, curb returns, sidewalk corners, roundabout islands and through-tram rails.
import { ShapeUtils, Vector2 } from 'three';
import { Out, strip, polyWall, polygon, ribbon } from './out.js';
import { CURB_W, GROUND } from './edge.js';
import { LEVEL, getProfile, layout } from '../profiles.js';
import { line } from './markings.js';
import { nodeControl } from './control.js';

const H = LEVEL.raised;

function inset(curb, outer, d) {
  return curb.map((p, k) => {
    const q = outer[k], dx = q[0] - p[0], dz = q[2] - p[2], l = Math.hypot(dx, dz);
    if (l < 1e-6) return p.slice();
    const t = Math.min(d, l * 0.5) / l;
    return [p[0] + dx * t, p[1], p[2] + dz * t];
  });
}
const lift = (P, h) => P.map(p => [p[0], p[1] + h, p[2]]);

export function buildJunction(net, J, ctx) {
  const out = new Out(J.id);
  out.kind = J.kind;
  if (!J.polygon || J.polygon.length < 3) {
    if (J.node) out.grow(J.node.x, J.node.y, J.node.z);
    return out;
  }
  // surface material = shared surface of incident roads, else asphalt
  const surfaces = new Set(J.ends.map(e => getProfile(e.edge).surface || 'asphalt'));
  const mat = surfaces.size === 1 ? [...surfaces][0] : (surfaces.has('asphalt') ? 'asphalt' : [...surfaces][0]);

  // --- surface polygon (with island hole for roundabouts)
  const contour = J.polygon.map(p => new Vector2(p[0], p[2]));
  const holes = J.hole ? [J.hole.map(p => new Vector2(p[0], p[2]))] : [];
  let tris;
  try {
    const clean = dedupe(contour);
    const faces = ShapeUtils.triangulateShape(clean.pts, holes);
    const all = [...clean.src.map(i => J.polygon[i]), ...(J.hole || [])];
    tris = [];
    for (const [a, b, c] of faces) {
      const A = all[a], B = all[b], C = all[c];
      // upward facing ⇔ (C-B)×(A-B) has +y ⇔ cross in xz of (B-A),(C-A) negative
      const cr = (B[0] - A[0]) * (C[2] - A[2]) - (B[2] - A[2]) * (C[0] - A[0]);
      if (cr < 0) tris.push(a, b, c); else tris.push(a, c, b);
    }
    polygon(out, mat, all, tris);
  } catch (e) { out.warning = 'triangulation failed'; }

  // --- corners: curb returns + sidewalks
  for (const g of J.gaps) {
    if (!g.hasCurb) continue;
    const hasSw = Math.max(g.swA, g.swB) > 0.05;
    if (!hasSw) continue;
    const curbIn = inset(g.curb, g.outer, CURB_W);
    strip(out, 'curb', lift(g.curb, H), lift(curbIn, H));
    strip(out, 'sidewalk', lift(curbIn, H), lift(g.outer, H));
    polyWall(out, 'curb', g.curb, 0, H, +1);
    polyWall(out, 'curb', g.outer, GROUND, H, -1);
  }

  // --- roundabout island
  if (J.kind === 'roundabout' && J.hole) {
    const N = J.node, ring = J.hole.slice().reverse(); // CCW
    const inner = ring.map(p => { const dx = p[0] - N.x, dz = p[2] - N.z, l = Math.hypot(dx, dz); return [N.x + dx / l * (l - CURB_W * 1.5), p[1], N.z + dz / l * (l - CURB_W * 1.5)]; });
    ring.push(ring[0]); inner.push(inner[0]);
    strip(out, 'curb', lift(inner, H), lift(ring, H));
    polyWall(out, 'curb', ring, 0, H, -1);
    // apron (truck mountable) ring of pavers
    const apron = inner.map(p => { const dx = p[0] - N.x, dz = p[2] - N.z, l = Math.hypot(dx, dz); return [N.x + dx / l * (l - 2.2), p[1], N.z + dz / l * (l - 2.2)]; });
    strip(out, 'cobble', lift(apron, H - 0.05), lift(inner, H));
    const pc = out.piece('grass'), base = pc.p.length / 3;
    out.vert(pc, N.x, N.y + H + 0.25, N.z, N.x, N.z);
    for (const p of apron) out.vert(pc, p[0], p[1] + H - 0.05, p[2], p[0], p[2]);
    for (let k = 0; k < apron.length - 1; k++) pc.i.push(base, base + 2 + k, base + 1 + k);
    if (ctx.props !== false) {
      out.prop('treeA', N.x, N.y + H + 0.2, N.z, 0, 1.5, 1.5, 1.5);
      for (let k = 0; k < 5; k++) { const a = k / 5 * Math.PI * 2, r = (J.Ri - 3) * 0.55; out.prop('treeB', N.x + Math.cos(a) * r, N.y + H + 0.1, N.z + Math.sin(a) * r, a, 0.8, 0.8, 0.8); }
    }
    // circulating lane edge line along island
    out.grow(N.x - J.Rc, N.y, N.z - J.Rc); out.grow(N.x + J.Rc, N.y, N.z + J.Rc);
  }

  // --- tram rails straight through junction for opposing tram approaches
  const trams = J.ends.filter(e => layout(getProfile(e.edge)).lanes.some(l => l.type === 'tram'));
  for (let i = 0; i < trams.length; i++) for (let j = i + 1; j < trams.length; j++) {
    const A = trams[i], B = trams[j];
    if (A.dx * B.dx + A.dz * B.dz > -0.9) continue;
    for (const ln of layout(getProfile(A.edge)).lanes.filter(l => l.type === 'tram')) {
      const oc = (ln.o0 + ln.o1) / 2 * (A.atA ? 1 : -1); // into right-of-d frame
      for (const g of [-0.7175, 0.7175]) {
        const o = oc + g;
        const N = J.node;
        const pa = [N.x + A.dx * A.setback + A.rx * o, N.z + A.dz * A.setback + A.rz * o];
        const pb = [N.x + B.dx * B.setback - B.rx * o, N.z + B.dz * B.setback - B.rz * o];
        const fake = straightCl(pa, pb, N.y);
        ribbon(out, 'steel', fake.frames, -0.04, 0.04, 0.006);
      }
    }
  }

  // --- yellow box / centre markings: roundabout circulating line
  if (J.kind === 'roundabout' && ctx.markings !== false) {
    const N = J.node, r = J.Ri + 0.35 + CURB_W;
    const m = 72, frames = [];
    for (let k = 0; k <= m; k++) { const a = k / m * Math.PI * 2; frames.push({ x: N.x + Math.cos(a) * r, z: N.z + Math.sin(a) * r, y: N.y, s: a * r, tx: -Math.sin(a), tz: Math.cos(a), nx: -Math.cos(a), nz: -Math.sin(a) }); }
    ribbon(out, 'markYellow', frames, -0.06, 0.06, 0.012);
  }
  out.control = nodeControl(J);
  return out;
}

function straightCl(a, b, y) {
  const dx = b[0] - a[0], dz = b[1] - a[1], l = Math.hypot(dx, dz) || 1;
  const tx = dx / l, tz = dz / l;
  return { frames: [0, 1].map(t => ({ x: a[0] + dx * t, z: a[1] + dz * t, y, s: l * t, tx, tz, nx: -tz, nz: tx })) };
}

function dedupe(contour) {
  const pts = [], src = [];
  contour.forEach((p, i) => {
    const q = pts[pts.length - 1];
    if (q && Math.abs(q.x - p.x) < 1e-3 && Math.abs(q.y - p.y) < 1e-3) return;
    pts.push(p); src.push(i);
  });
  if (pts.length > 2) { const a = pts[0], b = pts[pts.length - 1]; if (Math.abs(a.x - b.x) < 1e-3 && Math.abs(a.y - b.y) < 1e-3) { pts.pop(); src.pop(); } }
  return { pts, src };
}
