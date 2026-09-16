import * as THREE from 'three';
import { SEABED_VS, SEABED_FS } from './glsl.js';
import { bathyJS } from './bathy.js';

export class Seabed {
  constructor(uniforms) {
    this.material = new THREE.ShaderMaterial({
      uniforms,
      vertexShader: SEABED_VS,
      fragmentShader: SEABED_FS,
      side: THREE.DoubleSide,
    });
    this.mesh = new THREE.Mesh(this.buildGeometry(), this.material);
    this.mesh.frustumCulled = false;
    this.mesh.renderOrder = -2;
  }

  buildGeometry() {
    const g = new THREE.PlaneGeometry(1500, 1500, 150, 150);
    g.rotateX(-Math.PI / 2);
    const pos = g.getAttribute('position');
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i), z = pos.getZ(i);
      pos.setY(i, -bathyJS(x, z) - 0.25);
    }
    pos.needsUpdate = true;
    g.computeVertexNormals();
    return g;
  }

  // Rebuild when the user reshapes the reef/shore so sand and surf agree.
  rebuild() {
    const old = this.mesh.geometry;
    this.mesh.geometry = this.buildGeometry();
    old.dispose();
  }

  addTo(scene) {
    scene.add(this.mesh);
  }
}
