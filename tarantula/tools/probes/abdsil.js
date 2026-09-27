// Abdomen silhouette incl. setae, in the abdPivot frame: per 0.5 cm z-slice, max |x| and y range.
(() => {
  const g = __game, T = g.THREE, s = g.spider;
  g.step(1);
  s.abdPivot.updateMatrixWorld(true);
  const inv = new T.Matrix4().copy(s.abdPivot.matrixWorld).invert();
  const v = new T.Vector3(), m = new T.Matrix4(), im = new T.Matrix4();
  const slices = {}, names = [];
  s.abdPivot.traverse((o) => {
    if (!o.geometry || !o.visible) return;
    const pos = o.geometry.attributes.position;
    names.push(`${o.name}:${o.type}:${pos.count}${o.isInstancedMesh ? ':x' + o.count : ''}`);
    const inst = o.isInstancedMesh ? o.count : 1;
    for (let k = 0; k < inst; k++) {
      if (o.isInstancedMesh) o.getMatrixAt(k, im); else im.identity();
      m.copy(inv).multiply(o.matrixWorld).multiply(im);
      const step = Math.max(1, Math.floor(pos.count / (o.isInstancedMesh ? 40 : 6000)));
      for (let i = 0; i < pos.count; i += step) {
        v.fromBufferAttribute(pos, i).applyMatrix4(m);
        const b = (Math.round(v.z * 2) / 2).toFixed(1);
        const sl = slices[b] || (slices[b] = { x: 0, ymax: -9, ymin: 9 });
        sl.x = Math.max(sl.x, Math.abs(v.x)); sl.ymax = Math.max(sl.ymax, v.y); sl.ymin = Math.min(sl.ymin, v.y);
      }
    }
  });
  for (const k in slices) for (const q in slices[k]) slices[k][q] = +slices[k][q].toFixed(2);
  return { names, slices };
})()
