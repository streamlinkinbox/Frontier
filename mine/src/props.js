// Timber support sets (posts + cap + knee braces) and hanging lamps,
// instanced along every tunnel spline frame (they follow banking & slope).
import * as THREE from 'three';

export function buildProps(mine, P) {
  const group = new THREE.Group();
  group.name = 'MineProps';
  const wood = new THREE.MeshStandardMaterial({ color: 0x5a3b22, roughness: 0.9, metalness: 0 });
  const steel = new THREE.MeshStandardMaterial({ color: 0x2b2b2b, roughness: 0.55, metalness: 0.8 });
  const bulbMat = new THREE.MeshStandardMaterial({ color: 0xffffff, emissive: 0xffc27a, emissiveIntensity: 12 });
  const cageMat = new THREE.MeshStandardMaterial({ color: 0x222222, roughness: 0.5, metalness: 0.8, wireframe: false });
  const n = mine.supports.length;
  const postH = P.springHeight + 0.05;
  const post = new THREE.InstancedMesh(new THREE.BoxGeometry(0.3, postH, 0.3), wood, n * 2);
  // unit-length cap, scaled per support to the local span (2 / 3 / 4 lane tunnels)
  const cap = new THREE.InstancedMesh(new THREE.BoxGeometry(1, 0.34, 0.34), wood, n);
  const brace = new THREE.InstancedMesh(new THREE.BoxGeometry(0.16, 1.2, 0.16), wood, n * 2);
  const plate = new THREE.InstancedMesh(new THREE.BoxGeometry(0.34, 0.06, 0.4), steel, n * 2);
  const m = new THREE.Matrix4(), basis = new THREE.Matrix4(), off = new THREE.Matrix4(), rot = new THREE.Matrix4();
  mine.supports.forEach((f, i) => {
    // basis: x = left (N), y = up (B), z = forward (T)
    basis.makeBasis(f.N, f.B, f.T).setPosition(f.p);
    const hwL = f.hwL ?? 4, hwR = f.hwR ?? 4;
    for (let s = 0; s < 2; s++) {
      const hw = s ? hwR : hwL;
      const x = (s ? -1 : 1) * (hw - 0.05);
      off.makeTranslation(x, 0.12 + postH / 2, 0);
      post.setMatrixAt(i * 2 + s, m.multiplyMatrices(basis, off));
      off.makeTranslation(x, 0.14, 0);
      plate.setMatrixAt(i * 2 + s, m.multiplyMatrices(basis, off));
      // knee brace (45 deg) between post and cap
      rot.makeRotationZ((s ? -1 : 1) * Math.PI / 4);
      off.makeTranslation((s ? -1 : 1) * (hw - 0.55), postH - 0.35, 0).multiply(rot);
      brace.setMatrixAt(i * 2 + s, m.multiplyMatrices(basis, off));
    }
    // wider spans get a deeper beam
    const span = hwL + hwR + P.wallBulge;
    const deep = 1 + Math.max(0, span - 8.5) * 0.12;
    off.makeTranslation((hwL - hwR) / 2, postH + 0.12 + 0.17 - 0.17 * deep + 0.12, 0).multiply(rot.makeScale(span, deep, 1));
    cap.setMatrixAt(i, m.multiplyMatrices(basis, off));
  });
  [post, cap, brace, plate].forEach((im) => { im.castShadow = true; im.receiveShadow = true; im.instanceMatrix.needsUpdate = true; group.add(im); });

  // lamps
  const L = mine.lamps.length;
  const bulb = new THREE.InstancedMesh(new THREE.SphereGeometry(0.11, 10, 8), bulbMat, L);
  const cage = new THREE.InstancedMesh(new THREE.CylinderGeometry(0.16, 0.24, 0.28, 8, 1, true), cageMat, L);
  const cable = new THREE.InstancedMesh(new THREE.CylinderGeometry(0.015, 0.015, 1, 4), steel, L);
  mine.lamps.forEach((l, i) => {
    const drop = l.hub ? 1.6 : 0.38;
    m.makeTranslation(l.p.x, l.p.y, l.p.z);
    bulb.setMatrixAt(i, m);
    m.makeTranslation(l.p.x, l.p.y + 0.1, l.p.z);
    cage.setMatrixAt(i, m);
    m.makeTranslation(l.p.x, l.p.y + 0.2 + drop / 2, l.p.z).multiply(new THREE.Matrix4().makeScale(1, drop, 1));
    cable.setMatrixAt(i, m);
  });
  [bulb, cage, cable].forEach((im) => { im.instanceMatrix.needsUpdate = true; group.add(im); });
  return group;
}

export function disposeGroup(g) {
  g.traverse((o) => {
    if (o.geometry) o.geometry.dispose();
    if (o.material) (Array.isArray(o.material) ? o.material : [o.material]).forEach((mm) => mm.dispose());
  });
}
