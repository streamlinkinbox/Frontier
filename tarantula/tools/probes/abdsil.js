// Abdomen outline in the abdPivot frame, per 0.25 cm z-slice: `hair` = including the long setae strands,
// `body` = the abdomen surface itself (the short pile shells sit on it). Feeds ABD_* in Actions.js.
(() => {
  const g = __game, T = g.THREE, s = g.spider;
  g.step(1); s.abdPivot.updateMatrixWorld(true);
  const inv = new T.Matrix4().copy(s.abdPivot.matrixWorld).invert();
  const v = new T.Vector3(), m = new T.Matrix4();
  let body = null; s.abdPivot.traverse((o) => { if (o.name === 'abdomen:base') body = o; });
  const strands = body.parent.children.concat(body.children).find((o) => o.geometry && !o.isInstancedMesh && o !== body && o.geometry.attributes.position.count > 20000);
  const res = {};
  for (const [kind, o] of [['body', body], ['hair', strands]]) {
    if (!o) continue;
    const pos = o.geometry.attributes.position; m.copy(inv).multiply(o.matrixWorld);
    const sl = {};
    for (let i = 0; i < pos.count; i++) {
      v.fromBufferAttribute(pos, i).applyMatrix4(m);
      const b = (Math.round(v.z * 4) / 4).toFixed(2);
      const e = sl[b] || (sl[b] = { x: 0, ymax: -9, ymin: 9 });
      e.x = Math.max(e.x, Math.abs(v.x)); e.ymax = Math.max(e.ymax, v.y); e.ymin = Math.min(e.ymin, v.y);
    }
    const zs = Object.keys(sl).map(Number).sort((a, b) => b - a);
    res[kind] = { z: zs, x: zs.map((z) => +sl[z.toFixed(2)].x.toFixed(2)), ymax: zs.map((z) => +sl[z.toFixed(2)].ymax.toFixed(2)), ymin: zs.map((z) => +sl[z.toFixed(2)].ymin.toFixed(2)) };
  }
  return res;
})()
