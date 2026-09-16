import * as THREE from 'three';
import { LAB, PARAMS } from './config.js';

// Buoy + wave-lab marker ring + center spar.
// The buoy and spar ride the SAME analytic WaveField the GPU draws,
// so they are live probes of the simulation, not decoration.
export class Props {
  constructor() {
    this.group = new THREE.Group();

    // --- instrument buoy (offshore) ---
    const buoy = new THREE.Group();
    const red = new THREE.MeshStandardMaterial({ color: 0xc23b2e, roughness: 0.55, metalness: 0.1 });
    const white = new THREE.MeshStandardMaterial({ color: 0xf2ede2, roughness: 0.6 });
    const dark = new THREE.MeshStandardMaterial({ color: 0x2a2d33, roughness: 0.7 });
    const hull = new THREE.Mesh(new THREE.CylinderGeometry(0.9, 1.3, 2.4, 14), red);
    hull.position.y = 0.6;
    buoy.add(hull);
    const band = new THREE.Mesh(new THREE.CylinderGeometry(0.96, 1.02, 0.5, 14), white);
    band.position.y = 1.35;
    buoy.add(band);
    const mast = new THREE.Mesh(new THREE.CylinderGeometry(0.08, 0.1, 2.4, 8), dark);
    mast.position.y = 3.0;
    buoy.add(mast);
    const lamp = new THREE.Mesh(
      new THREE.SphereGeometry(0.3, 12, 10),
      new THREE.MeshStandardMaterial({ color: 0xffa040, emissive: 0xff7a20, emissiveIntensity: 2.2 })
    );
    lamp.position.y = 4.3;
    buoy.add(lamp);
    this.lampLight = new THREE.PointLight(0xff9a40, 6, 26, 2);
    this.lampLight.position.y = 4.3;
    buoy.add(this.lampLight);
    const collar = new THREE.Mesh(new THREE.TorusGeometry(1.25, 0.14, 8, 20), dark);
    collar.rotation.x = Math.PI / 2;
    collar.position.y = -0.2;
    buoy.add(collar);
    this.buoyBase = { x: -152, z: 128 };
    buoy.position.set(this.buoyBase.x, 0, this.buoyBase.z);
    this.buoy = buoy;
    this.group.add(buoy);

    // --- wave-lab ring ---
    const ring = new THREE.Mesh(
      new THREE.RingGeometry(LAB.r - 1.6, LAB.r + 1.6, 96),
      new THREE.MeshBasicMaterial({
        color: 0x35e0ff, transparent: true, opacity: 0.18,
        side: THREE.DoubleSide, depthWrite: false,
      })
    );
    ring.rotation.x = -Math.PI / 2;
    ring.position.set(LAB.x, 1.1, LAB.z);
    ring.renderOrder = 2;
    this.group.add(ring);
    this.ring = ring;

    // --- center spar (watch it go still when waves cancel) ---
    const spar = new THREE.Group();
    const pole = new THREE.Mesh(
      new THREE.CylinderGeometry(0.22, 0.22, 5.5, 10),
      new THREE.MeshStandardMaterial({ color: 0xffc93b, roughness: 0.5 })
    );
    spar.add(pole);
    const tip = new THREE.Mesh(
      new THREE.SphereGeometry(0.42, 12, 10),
      new THREE.MeshStandardMaterial({ color: 0x35e0ff, emissive: 0x1899bb, emissiveIntensity: 1.4 })
    );
    tip.position.y = 3.1;
    spar.add(tip);
    spar.position.set(LAB.x, 0, LAB.z);
    this.group.add(spar);
    this.spar = spar;

    this.yaw = 0;
    this.buoyH = 0;
  }

  addTo(scene) {
    scene.add(this.group);
  }

  update(dt, simTime, wave) {
    const b = this.buoyBase;
    const h = wave.height(b.x, b.z, simTime);
    const g = wave.grad(b.x, b.z, simTime);
    this.buoyH = h;
    this.buoy.position.y = h + 0.4;
    this.yaw += dt * 0.06;
    this.buoy.rotation.set(g[1] * 0.9, this.yaw, -g[0] * 0.9);

    const sh = wave.height(LAB.x, LAB.z, simTime);
    this.spar.position.y = sh + 1.2;
    this.spar.rotation.x = Math.sin(simTime * 0.5) * 0.03;

    const active = PARAMS.labA.on || PARAMS.labB.on;
    this.ring.material.opacity = active ? 0.35 + 0.2 * Math.sin(simTime * 3.0) : 0.14;
    this.lampLight.intensity = 4 + Math.sin(simTime * 2.2) * 3;
  }
}
