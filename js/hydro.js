import * as THREE from 'three';

// Functional hydraulic pistons: two-part telescoping cylinders re-solved
// every frame to span live anchor points on the moving skeleton.

const _A = new THREE.Vector3();
const _B = new THREE.Vector3();

export function buildHydraulics(scene, defs, mats) {
  const group = new THREE.Group();
  group.name = 'hydraulics';
  scene.add(group);

  const sleeveGeo = new THREE.CylinderGeometry(1, 1, 1, 10);
  sleeveGeo.rotateX(Math.PI / 2); // axis -> Z, centered
  const rodGeo = new THREE.CylinderGeometry(1, 1, 1, 8);
  rodGeo.rotateX(Math.PI / 2);
  const capGeo = new THREE.SphereGeometry(1, 8, 8);
  const collarGeo = new THREE.CylinderGeometry(1, 1, 1, 10);
  collarGeo.rotateX(Math.PI / 2);
  const lugGeo = new THREE.BoxGeometry(1, 1, 1);

  const pistons = defs.map((d) => {
    const root = new THREE.Group();
    group.add(root);
    const sleeve = new THREE.Mesh(sleeveGeo, mats.sleeve);
    const rod = new THREE.Mesh(rodGeo, mats.chrome);
    const collar = new THREE.Mesh(collarGeo, mats.collar);
    const collarB = new THREE.Mesh(collarGeo, mats.collar);
    const capA = new THREE.Mesh(capGeo, mats.darkMetal);
    const capB = new THREE.Mesh(capGeo, mats.darkMetal);
    sleeve.castShadow = rod.castShadow = true;
    root.add(sleeve, rod, collar, collarB, capA, capB);
    capA.scale.setScalar(d.r * 1.25);

    // anchor lugs parented to the bones (static)
    const lugA = new THREE.Mesh(lugGeo, mats.darkMetal);
    lugA.position.set(d.a.at[0], d.a.at[1], d.a.at[2]);
    lugA.scale.setScalar(d.r * 2.4);
    lugA.castShadow = true;
    d.a.obj.add(lugA);
    const lugB = new THREE.Mesh(lugGeo, mats.darkMetal);
    lugB.position.set(d.b.at[0], d.b.at[1], d.b.at[2]);
    lugB.scale.setScalar(d.r * 2.4);
    lugB.castShadow = true;
    d.b.obj.add(lugB);

    return {
      def: d, root, sleeve, rod, collar, collarB, capB,
      atA: new THREE.Vector3(...d.a.at),
      atB: new THREE.Vector3(...d.b.at),
      rest: 0, ext: 1,
    };
  });

  function update() {
    const acc = {};
    for (const p of pistons) {
      _A.copy(p.atA).applyMatrix4(p.def.a.obj.matrixWorld);
      _B.copy(p.atB).applyMatrix4(p.def.b.obj.matrixWorld);
      const dist = Math.max(0.05, _A.distanceTo(_B));
      if (!p.rest) p.rest = dist;
      p.ext = dist / p.rest;

      p.root.position.copy(_A);
      p.root.lookAt(_B); // +Z toward B

      const r = p.def.r;
      const sleeveLen = dist * 0.58;
      const rodLen = dist * 0.62;
      p.sleeve.scale.set(r, r, sleeveLen);
      p.sleeve.position.z = sleeveLen / 2;
      p.rod.scale.set(r * 0.55, r * 0.55, rodLen);
      p.rod.position.z = dist - rodLen / 2;
      p.collar.scale.set(r * 1.18, r * 1.18, 0.05);
      p.collar.position.z = sleeveLen;
      p.collarB.scale.set(r * 1.18, r * 1.18, 0.05);
      p.collarB.position.z = 0.03;
      p.capB.scale.setScalar(r * 1.25);
      p.capB.position.z = dist;

      const g = p.def.group;
      acc[g] = acc[g] || { sum: 0, n: 0 };
      acc[g].sum += p.ext; acc[g].n++;
    }
    const out = {};
    for (const k in acc) out[k] = acc[k].sum / acc[k].n;
    return out;
  }

  return { group, pistons, update };
}
