import * as THREE from 'three';
import { WORLD } from './layout.js';

// ---------------------------------------------------------------------------
// Low-poly ocean with a rising tide. The whole water body slowly climbs from
// seaLevel to seaLevelMax, flooding the shore behind the player.
// ---------------------------------------------------------------------------

export function buildOcean(scene) {
  const width = 900, depth = 700;
  const segX = 72, segZ = 48;
  const geo = new THREE.PlaneGeometry(width, depth, segX, segZ);
  geo.rotateX(-Math.PI / 2);
  // spans far out at sea to well inland (hidden under terrain until the
  // rising tide lifts it above the beach slope)
  const inlandEdge = 120;
  geo.translate(0, 0, inlandEdge - depth / 2);

  const pos = geo.attributes.position;
  const baseY = new Float32Array(pos.count);
  for (let i = 0; i < pos.count; i++) baseY[i] = 0;

  const mat = new THREE.MeshStandardMaterial({
    color: '#2f7f9d',
    flatShading: true,
    roughness: 0.42,
    metalness: 0.05,
    transparent: true,
    opacity: 0.92,
  });

  const mesh = new THREE.Mesh(geo, mat);
  mesh.position.y = WORLD.seaLevel;
  mesh.name = 'ocean';
  mesh.receiveShadow = true;
  scene.add(mesh);

  // seabed under the water so it never shows sky through
  const bed = new THREE.Mesh(
    new THREE.PlaneGeometry(width, depth),
    new THREE.MeshStandardMaterial({ color: '#173c4c', roughness: 1 })
  );
  bed.rotation.x = -Math.PI / 2;
  bed.position.set(0, -2.6, inlandEdge - depth / 2);
  scene.add(bed);

  // foam strip hugging the shoreline
  const foam = new THREE.Mesh(
    new THREE.PlaneGeometry(700, 14),
    new THREE.MeshBasicMaterial({ color: '#dff0f0', transparent: true, opacity: 0.55 })
  );
  foam.rotation.x = -Math.PI / 2;
  foam.position.set(0, WORLD.seaLevel + 0.06, WORLD.shoreZ - 4);
  scene.add(foam);

  const api = {
    mesh,
    foam,
    level: WORLD.seaLevel,
    /** tide progress 0..1 */
    t: 0,
    update(time, dt) {
      api.t = Math.min(1, api.t + dt / WORLD.tideDuration);
      api.level = THREE.MathUtils.lerp(WORLD.seaLevel, WORLD.seaLevelMax, api.t);
      mesh.position.y = api.level;

      // track the visual shoreline along the beach slope
      const slope = api.level < 0 ? 0.05 : 0.0175;
      const shoreZ = WORLD.shoreZ + api.level / slope;
      foam.position.y = api.level + 0.07;
      foam.position.z = shoreZ - 2.5;

      // rolling low-poly swell
      const arr = pos.array;
      for (let i = 0; i < pos.count; i++) {
        const x = arr[i * 3], z = arr[i * 3 + 2];
        arr[i * 3 + 1] =
          Math.sin(x * 0.055 + time * 1.05) * 0.32 +
          Math.sin(z * 0.075 + time * 0.85) * 0.26 +
          Math.sin((x + z) * 0.028 + time * 0.55) * 0.34;
      }
      pos.needsUpdate = true;
      geo.computeVertexNormals();
    },
  };

  return api;
}
