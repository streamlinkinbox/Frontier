// Plan-view SVG of a template's centrelines + nodes for layout checking. Usage: node tools/template-svg.mjs <template> [out.svg] [--mesh]
import { readFileSync, writeFileSync } from 'node:fs'; import vm from 'node:vm';
globalThis.RF = {}; globalThis.window = globalThis;
for (const f of ['math', 'spline', 'mesh', 'profiles', 'network', 'roadmesh', 'structures', 'materials', 'props', 'rail', 'traffic', 'scene', 'templates']) vm.runInThisContext(readFileSync(new URL(`../src/${f}.js`, import.meta.url), 'utf8'), { filename: f });
const RF = globalThis.RF;
const name = process.argv[2] || 'cloverleaf', outFile = process.argv[3] || `/tmp/tpl-${name}.svg`;
const project = RF.templates.list[name].make(); RF.scene.autoIntersect(project);
const net = RF.network.solve(project, { layers: {} });
let minx = 1e9, minz = 1e9, maxx = -1e9, maxz = -1e9;
for (const n of Object.values(project.nodes)) { minx = Math.min(minx, n.p[0]); maxx = Math.max(maxx, n.p[0]); minz = Math.min(minz, n.p[2]); maxz = Math.max(maxz, n.p[2]); }
const pad = 40, W = maxx - minx + 2 * pad, Hh = maxz - minz + 2 * pad, sc = 1600 / Math.max(W, Hh);
const X = (x) => ((x - minx + pad) * sc).toFixed(1), Z = (z) => ((z - minz + pad) * sc).toFixed(1);
let svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${(W * sc).toFixed(0)}" height="${(Hh * sc).toFixed(0)}" style="background:#fff;font:10px sans-serif">`;
for (const R of net.roads.values()) {
  const hw = Math.max(R.lay.outerL, R.lay.outerR);
  svg += `<polyline points="${R.dense.map((p) => `${X(p[0])},${Z(p[2])}`).join(' ')}" fill="none" stroke="#99a" stroke-width="${(hw * 2 * sc).toFixed(1)}" stroke-opacity=".35"/>`;
  svg += `<polyline points="${R.dense.map((p) => `${X(p[0])},${Z(p[2])}`).join(' ')}" fill="none" stroke="#06c" stroke-width="1"/>`;
  const m = R.dense[Math.floor(R.dense.length / 2)];
  svg += `<text x="${X(m[0])}" y="${Z(m[2])}" fill="#06c">${R.id} y${m[1].toFixed(0)} L${R.length.toFixed(0)} trim(${(R.trimStart ?? 0).toFixed(0)},${(R.trimEnd ?? 0).toFixed(0)})</text>`;
}
for (const rec of net.nodes.values()) {
  const p = rec.node.p;
  svg += `<circle cx="${X(p[0])}" cy="${Z(p[2])}" r="4" fill="${rec.kind === 'junction' ? '#c00' : '#0a0'}"/><text x="${X(p[0])}" y="${+Z(p[2]) - 6}" fill="#c00">${rec.id} ${rec.kind}${rec.arms ? ' ' + rec.arms.length : ''}</text>`;
}
svg += '</svg>'; writeFileSync(outFile, svg); console.log('wrote', outFile, 'roads', net.roads.size, 'nodes', net.nodes.size);
