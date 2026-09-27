// Installs window.__cam(dist, elev, azimuth, targetUp): places the camera around the spider at a
// spot in open air with a clear line of sight (az 0 = in front, +PI/2 = spider's right side).
window.__cam = (dist, elev, azPref, targetUp = 0.6) => {
  const g = __game, T = g.THREE, s = g.spider, w = g.world, cam = g.camera;
  g.freezeCam = true; s.root.updateMatrixWorld(true);
  const up = s.up.clone(), fwd = s.fwd.clone(), r = new T.Vector3().crossVectors(fwd, up).normalize();
  const tgt = s.root.getWorldPosition(new T.Vector3()).addScaledVector(up, targetUp);
  for (let a = 0; a < 24; a++) for (const de of [0, 0.15, -0.1, 0.3, 0.5]) {
    const ang = azPref + (a % 2 ? 1 : -1) * Math.ceil(a / 2) * (Math.PI / 12), el = elev + de;
    const d = new T.Vector3().addScaledVector(fwd, Math.cos(ang) * Math.cos(el)).addScaledVector(r, Math.sin(ang) * Math.cos(el)).addScaledVector(up, Math.sin(el)).normalize();
    const c = tgt.clone().addScaledVector(d, dist);
    const h = w.closest(c, 3); if (h && h.distance < 0.7) continue;
    const dir = tgt.clone().sub(c); const len = dir.length(); dir.normalize();
    if (w.raycast(c, dir, len - 1.2)) continue;
    cam.position.copy(c); cam.up.copy(up); cam.lookAt(tgt); g.render(); return 'ok';
  }
  return 'nocam';
};
1
