// Headless scene smoke test: builds a full project through RF.scene with a stub renderer.
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
globalThis.RF = {};
for (const f of ['math', 'spline', 'mesh', 'profiles', 'network', 'roadmesh', 'structures', 'materials', 'props', 'rail', 'traffic', 'scene', 'templates']) vm.runInThisContext(readFileSync(new URL(`../src/${f}.js`, import.meta.url), 'utf8'), { filename: f + '.js' });
const RF = globalThis.RF;
class StubRenderer { constructor() { this.items = new Map(); } setItem(it) { this.items.set(it.id, { ...it, gpu: { count: it.mesh.indices.length, quadCount: it.mesh.quadCount, vertexCount: it.mesh.vertexCount }, instanceCount: it.instances ? it.instances.length / 24 : 1 }); } removeItem(id) { this.items.delete(id); } removeByPrefix(p) { for (const k of [...this.items.keys()]) if (k.startsWith(p)) this.items.delete(k); } updateMaterial() {} updateInstances(id, d) { const r = this.items.get(id); if (r) r.instanceCount = d.length / 24; } setLights() {} stats() { let tris = 0, quads = 0, draws = 0, verts = 0; for (const it of this.items.values()) { draws++; tris += (it.gpu.count / 3) * it.instanceCount; quads += it.gpu.quadCount * it.instanceCount; verts += it.gpu.vertexCount * it.instanceCount; } return { tris, quads, draws, verts, items: this.items.size }; } }
const names = process.argv[2] ? [process.argv[2]] : Object.keys(RF.templates.list);
let fail = 0;
for (const name of names) {
  const t0 = Date.now();
  const project = RF.templates.list[name].make();
  RF.scene.autoIntersect(project);
  const scene = new RF.scene.Scene(new StubRenderer());
  try {
    scene.setProject(project);
    const st = scene.stats;
    let nan = 0; const down = {};
    for (const [owner, mats] of scene.meshes) for (const [mat, m] of Object.entries(mats)) {
      let d = 0, n = m.vertices.length / 12;
      for (let i = 0; i < n; i++) { if (!Number.isFinite(m.vertices[i * 12]) || !Number.isFinite(m.vertices[i * 12 + 1]) || !Number.isFinite(m.vertices[i * 12 + 2])) nan++; if (m.vertices[i * 12 + 4] < -0.9) d++; }
      // surfaces that should face up (asphalt, paving, kerb tops, ground, markings): report if a large share faces down
      if (/^(asphalt|paving|paint|ground|grass|ballast|concrete_road)/.test(mat) && d / n > 0.1) { const k = owner.split(':')[0] + ':' + mat; down[k] = Math.max(down[k] || 0, d / n); }
    }
    // traffic + trains for a few seconds
    for (let i = 0; i < 120; i++) scene.tick(1 / 30, { target: [0, 0, 0] }, { night: 0 });
    const veh = scene.traffic ? scene.traffic.vehicles.length : 0;
    const errs = scene.diagnostics.filter((d) => d.level === 'error');
    console.log(`${name}: roads ${st.roads} nodes ${st.nodes} quads ${st.quads} tris ${st.tris} draws ${st.draws} vehicles ${veh} NaN ${nan} errors ${errs.length} (${Date.now() - t0} ms)`);
    for (const e of errs) console.log('   ', e.message);
    for (const [k, v] of Object.entries(down)) console.log(`    DOWN-FACING ${k} ${(v * 100).toFixed(0)}%`);
    if (nan) fail++;
  } catch (e) { fail++; console.log(`${name}: EXCEPTION`, e.stack); }
}
process.exit(fail ? 1 : 0);
