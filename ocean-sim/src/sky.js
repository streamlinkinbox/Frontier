import * as THREE from 'three';
import { SKY_VS, SKY_FS } from './glsl.js';

export class Sky {
  constructor(uniforms) {
    this.material = new THREE.ShaderMaterial({
      uniforms,
      vertexShader: SKY_VS,
      fragmentShader: SKY_FS,
      side: THREE.BackSide,
      depthWrite: false,
    });
    this.mesh = new THREE.Mesh(new THREE.SphereGeometry(12000, 32, 16), this.material);
    this.mesh.frustumCulled = false;
    this.mesh.renderOrder = -10;
  }

  addTo(scene) {
    scene.add(this.mesh);
  }

  follow(camera) {
    this.mesh.position.copy(camera.position);
  }
}
