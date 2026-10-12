// River channel: stone quay walls, coping, railing, riverbed and the water surface.
import { Centerline } from '../centerline.js';
import { Out, ribbon, wall, sweep, yawFor } from './out.js';

export const WATER_Y = -2.3;
export const BED_Y = -6;

export function buildRiver(river, ctx) {
  const out = new Out('river');
  const c = new Centerline(river.pts.map(p => ({ x: p[0], z: p[1], y: 0 })));
  const frames = c.stations(0, c.length, [], 12, 0.03).map(s => c.frameAt(s));
  const hw = river.width / 2;
  ribbon(out, 'water', frames, -hw - 0.2, hw + 0.2, WATER_Y);
  ribbon(out, 'riverbed', frames, -hw, hw, BED_Y);
  for (const side of [-1, 1]) {
    const o = side * hw;
    wall(out, 'quay', frames, o, BED_Y, 0, -side);
    // coping stones + hidden lip under the ground cut
    const a = side < 0 ? -hw - 4 : hw, b = side < 0 ? -hw : hw + 4;
    ribbon(out, 'quayTop', frames, a, b, -0.03);
    sweep(out, 'quay', frames, side < 0 ? [[0, 0], [0, 0.14], [-0.6, 0.14], [-0.6, -0.03]] : [[0.6, -0.03], [0.6, 0.14], [0, 0.14], [0, 0]], o);
  }
  // railing along the quay except where roads/rail cross
  for (const side of [-1, 1]) {
    const o = side * (hw + 0.6);
    let run = [];
    const flush = () => {
      if (run.length > 1) {
        sweep(out, 'steel', run, [[-0.03, 1.0], [-0.03, 1.07], [0.03, 1.07], [0.03, 1.0], [-0.03, 1.0]].map(([a, h]) => [a, h]), o);
        for (let s = run[0].s; s < run[run.length - 1].s; s += 2.2) { const f = c.frameAt(s); out.prop('railPost', f.x + f.nx * o, f.y, f.z + f.nz * o, yawFor(f.tx, f.tz)); }
      }
      run = [];
    };
    for (let s = 0; s < c.length; s += 3) {
      const f = c.frameAt(s);
      const blocked = ctx.roadAt && ctx.roadAt(f.x + f.nx * o, f.z + f.nz * o, 3);
      if (blocked) flush(); else run.push(f);
    }
    flush();
  }
  return out;
}
