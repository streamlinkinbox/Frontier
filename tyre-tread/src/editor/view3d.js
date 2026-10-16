/**
 * 3D preview of the quad tyre. three.js is loaded from a CDN through the import map in index.html.
 * Quads are split into triangles for rendering only; the exported mesh stays quad-only.
 */
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

export function createView3d(container) {
    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
    container.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x0b0f14);
    const camera = new THREE.PerspectiveCamera(35, 1, 0.01, 10);
    camera.position.set(0.55, 0.42, 0.85);
    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.target.set(0, 0, 0);

    scene.add(new THREE.HemisphereLight(0xdfe9ff, 0x14181f, 1.1));
    const key = new THREE.DirectionalLight(0xffffff, 1.6);
    key.position.set(1, 2, 1.5);
    scene.add(key);

    const material = new THREE.MeshStandardMaterial({ color: 0x3b4450, roughness: 0.85, metalness: 0, side: THREE.DoubleSide });
    const wireMaterial = new THREE.MeshBasicMaterial({ color: 0x7fd1ff, wireframe: true, transparent: true, opacity: 0.25 });
    let mesh = null;
    let wire = null;

    function resize() {
        const w = container.clientWidth, h = container.clientHeight;
        renderer.setSize(w, h, false);
        renderer.domElement.style.width = `${w}px`;
        renderer.domElement.style.height = `${h}px`;
        camera.aspect = w / Math.max(1, h);
        camera.updateProjectionMatrix();
    }
    new ResizeObserver(resize).observe(container);
    resize();

    renderer.setAnimationLoop(() => {
        controls.update();
        renderer.render(scene, camera);
    });

    function setMesh({ positions, quads }) {
        const index = new Uint32Array(quads.length / 4 * 6);
        for (let f = 0, o = 0; f < quads.length; f += 4) {
            const a = quads[f], b = quads[f + 1], c = quads[f + 2], d = quads[f + 3];
            index[o++] = a; index[o++] = b; index[o++] = c;
            index[o++] = a; index[o++] = c; index[o++] = d;
        }
        const geo = new THREE.BufferGeometry();
        geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        geo.setIndex(new THREE.BufferAttribute(index, 1));
        geo.computeVertexNormals();
        geo.computeBoundingSphere();
        if (mesh) { mesh.geometry.dispose(); scene.remove(mesh); }
        if (wire) { wire.geometry.dispose(); scene.remove(wire); }
        mesh = new THREE.Mesh(geo, material);
        wire = new THREE.Mesh(geo, wireMaterial);
        wire.visible = wireOn;
        scene.add(mesh, wire);
    }

    let wireOn = false;
    function setWireframe(on) {
        wireOn = on;
        if (wire) wire.visible = on;
    }

    return { setMesh, setWireframe };
}
