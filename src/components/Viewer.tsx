import { forwardRef, useEffect, useImperativeHandle, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { GLTFExporter } from 'three/examples/jsm/exporters/GLTFExporter.js';
import { RoofParams } from '../lib/types';
import { buildRoof, Check, RoofStats } from '../lib/buildRoof';
import { tileGeometries } from '../lib/tiles';
import { getMaterials } from '../lib/materials';

export interface ViewOpts {
  wireframe: boolean;
  xray: boolean;
  autorotate: boolean;
  night: boolean;
}

export interface ViewerApi {
  exportGLB: () => void;
  resetView: () => void;
}

interface Props {
  params: RoofParams;
  view: ViewOpts;
  onView: (patch: Partial<ViewOpts>) => void;
  checks: Check[];
  stats: RoofStats | null;
  onReport: (checks: Check[], stats: RoofStats) => void;
}

const CAM_HOME = new THREE.Vector3(9.5, 6.6, 11.5);
const TGT_HOME = new THREE.Vector3(0, 2.3, 0);

interface SceneExtras {
  hemi: THREE.HemisphereLight;
  sun: THREE.DirectionalLight;
  fill: THREE.DirectionalLight;
  groundMat: THREE.MeshStandardMaterial;
}

function disposeGroup(g: THREE.Object3D): void {
  const shared = tileGeometries().set;
  g.traverse((o) => {
    const mesh = o as THREE.Mesh;
    if (mesh.geometry && !shared.has(mesh.geometry)) mesh.geometry.dispose();
    const im = o as unknown as THREE.InstancedMesh;
    if (im.isInstancedMesh) im.dispose();
    const mat = (mesh as THREE.Mesh).material as THREE.Material | THREE.Material[] | undefined;
    const mats = Array.isArray(mat) ? mat : mat ? [mat] : [];
    for (const mt of mats) {
      if (mt.userData.shared) continue;
      const sm = mt as THREE.MeshStandardMaterial;
      if (sm.map && !sm.map.userData.shared) sm.map.dispose();
      if (sm.emissiveMap && !sm.emissiveMap.userData.shared) sm.emissiveMap.dispose();
      mt.dispose();
    }
  });
}

function applyNight(scene: THREE.Scene, fx: SceneExtras, night: boolean): void {
  if (night) {
    scene.background = new THREE.Color('#070912');
    (scene.fog as THREE.Fog).color.set('#070912');
    fx.hemi.color.set('#8fa8d8');
    fx.hemi.groundColor.set('#1a1410');
    fx.hemi.intensity = 0.25;
    fx.sun.color.set('#9fb8ff');
    fx.sun.intensity = 0.35;
    fx.fill.intensity = 0.05;
    fx.groundMat.color.set('#14141c');
  } else {
    scene.background = new THREE.Color('#141318');
    (scene.fog as THREE.Fog).color.set('#141318');
    fx.hemi.color.set('#dfe8ff');
    fx.hemi.groundColor.set('#3a2f28');
    fx.hemi.intensity = 0.85;
    fx.sun.color.set('#fff1dd');
    fx.sun.intensity = 2.4;
    fx.fill.color.set('#bcd0ff');
    fx.fill.intensity = 0.5;
    fx.groundMat.color.set('#232228');
  }
}

const Viewer = forwardRef<ViewerApi, Props>(function Viewer({ params, view, onView, checks, stats, onReport }, ref) {
  const mountRef = useRef<HTMLDivElement>(null);
  const sceneRef = useRef<THREE.Scene | null>(null);
  const extrasRef = useRef<SceneExtras | null>(null);
  const cameraRef = useRef<THREE.PerspectiveCamera | null>(null);
  const controlsRef = useRef<OrbitControls | null>(null);
  const roofRef = useRef<THREE.Group | null>(null);
  const spinnersRef = useRef<THREE.Object3D[]>([]);
  const onReportRef = useRef(onReport);
  onReportRef.current = onReport;

  // ---- one-time scene setup ----
  useEffect(() => {
    const mount = mountRef.current!;
    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setSize(mount.clientWidth, mount.clientHeight);
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.05;
    mount.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    scene.background = new THREE.Color('#141318');
    scene.fog = new THREE.Fog('#141318', 34, 95);
    sceneRef.current = scene;

    const camera = new THREE.PerspectiveCamera(42, mount.clientWidth / mount.clientHeight, 0.1, 300);
    camera.position.copy(CAM_HOME);
    cameraRef.current = camera;

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.target.copy(TGT_HOME);
    controls.enableDamping = true;
    controls.dampingFactor = 0.06;
    controls.maxPolarAngle = Math.PI / 2 - 0.02;
    controls.minDistance = 3;
    controls.maxDistance = 50;
    controls.autoRotateSpeed = 0.9;
    controlsRef.current = controls;

    const hemi = new THREE.HemisphereLight(0xdfe8ff, 0x3a2f28, 0.85);
    scene.add(hemi);
    const sun = new THREE.DirectionalLight(0xfff1dd, 2.4);
    sun.position.set(10, 14, 7);
    sun.castShadow = true;
    sun.shadow.mapSize.set(2048, 2048);
    sun.shadow.camera.left = -11;
    sun.shadow.camera.right = 11;
    sun.shadow.camera.top = 11;
    sun.shadow.camera.bottom = -11;
    sun.shadow.camera.far = 50;
    sun.shadow.bias = -0.0004;
    sun.shadow.normalBias = 0.03;
    scene.add(sun);
    const fill = new THREE.DirectionalLight(0xbcd0ff, 0.5);
    fill.position.set(-8, 6, -9);
    scene.add(fill);

    const groundMat = new THREE.MeshStandardMaterial({ color: '#232228', roughness: 1 });
    const ground = new THREE.Mesh(new THREE.CircleGeometry(34, 64), groundMat);
    ground.rotation.x = -Math.PI / 2;
    ground.receiveShadow = true;
    scene.add(ground);
    const grid = new THREE.GridHelper(44, 44, 0x5a5a66, 0x35353e);
    grid.position.y = 0.01;
    (grid.material as THREE.Material).transparent = true;
    (grid.material as THREE.Material).opacity = 0.55;
    scene.add(grid);
    extrasRef.current = { hemi, sun, fill, groundMat };

    let raf = 0;
    const clock = new THREE.Clock();
    let cachedRoof: THREE.Group | null = null;
    const loop = () => {
      raf = requestAnimationFrame(loop);
      const dt = Math.min(clock.getDelta(), 0.05);
      const roof = roofRef.current;
      if (roof && roof !== cachedRoof) {
        cachedRoof = roof;
        spinnersRef.current = [];
        roof.traverse((o) => { if (o.userData.spin) spinnersRef.current.push(o); });
      } else if (!roof) {
        cachedRoof = null;
        spinnersRef.current = [];
      }
      for (const s of spinnersRef.current) s.rotation.y += (s.userData.spin as number) * dt;
      controls.update();
      renderer.render(scene, camera);
    };
    loop();

    const onResize = () => {
      const w = mount.clientWidth;
      const h = mount.clientHeight;
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
      renderer.setSize(w, h);
    };
    window.addEventListener('resize', onResize);
    return () => {
      cancelAnimationFrame(raf);
      window.removeEventListener('resize', onResize);
      controls.dispose();
      renderer.dispose();
      mount.removeChild(renderer.domElement);
      sceneRef.current = null;
    };
  }, []);

  // ---- rebuild roof when params change ----
  useEffect(() => {
    const scene = sceneRef.current;
    if (!scene) return;
    if (roofRef.current) {
      scene.remove(roofRef.current);
      disposeGroup(roofRef.current);
    }
    const built = buildRoof(params);
    roofRef.current = built.group;
    scene.add(built.group);
    onReportRef.current(built.checks, built.stats);
  }, [params]);

  // ---- view modes ----
  useEffect(() => {
    const mats = getMaterials();
    for (const m of mats.all) m.wireframe = view.wireframe;
    const ghost = [mats.tile, mats.ridge, mats.underlay, mats.underside, mats.plaster, mats.wood, mats.woodDark, mats.mortar, mats.flashing];
    for (const m of ghost) {
      m.transparent = view.xray;
      m.opacity = view.xray ? 0.45 : 1;
      m.depthWrite = !view.xray;
      m.needsUpdate = true;
    }
    if (controlsRef.current) controlsRef.current.autoRotate = view.autorotate;
    if (sceneRef.current && extrasRef.current) applyNight(sceneRef.current, extrasRef.current, view.night);
  }, [view]);

  useImperativeHandle(ref, () => ({
    resetView() {
      cameraRef.current?.position.copy(CAM_HOME);
      controlsRef.current?.target.copy(TGT_HOME);
      controlsRef.current?.update();
    },
    exportGLB() {
      const roof = roofRef.current;
      if (!roof) return;
      new GLTFExporter().parse(
        roof,
        (res) => {
          const blob = new Blob([res as ArrayBuffer], { type: 'model/gltf-binary' });
          const a = document.createElement('a');
          a.href = URL.createObjectURL(blob);
          a.download = `frontier-${params.style}-roof.glb`;
          a.click();
          URL.revokeObjectURL(a.href);
        },
        (err) => console.error('GLB export failed', err),
        { binary: true },
      );
    },
  }), [params.style]);

  const fails = checks.filter((c) => c.status === 'fail').length;
  const warns = checks.filter((c) => c.status === 'warn').length;
  const passes = checks.filter((c) => c.status === 'pass').length;

  return (
    <div className="viewer-wrap">
      <div className="viewer-mount" ref={mountRef} />
      <div className="viewer-toolbar">
        <button className={`pill ${view.wireframe ? 'on' : ''}`} onClick={() => onView({ wireframe: !view.wireframe })}>Wireframe</button>
        <button className={`pill ${view.xray ? 'on' : ''}`} onClick={() => onView({ xray: !view.xray })}>X-ray</button>
        <button className={`pill ${view.autorotate ? 'on' : ''}`} onClick={() => onView({ autorotate: !view.autorotate })}>Rotate</button>
        <button className={`pill ${view.night ? 'on' : ''}`} onClick={() => onView({ night: !view.night })}>Night</button>
        <button className="pill" onClick={() => { cameraRef.current?.position.copy(CAM_HOME); controlsRef.current?.target.copy(TGT_HOME); }}>Reset view</button>
      </div>
      <div className="viewer-badge">
        <span className={`dot ${fails > 0 ? 'fail' : warns > 0 ? 'warn' : 'pass'}`} />
        {fails > 0 ? `${fails} check${fails > 1 ? 's' : ''} failing` : `${passes} passed${warns > 0 ? ` · ${warns} warning${warns > 1 ? 's' : ''}` : ''}`}
        {stats && (
          <span className="badge-stats">
            {stats.tileCount.toLocaleString()} tiles · {stats.rafterCount} rafters · {(stats.weightKg / 1000).toFixed(1)} t
            {stats.lamps > 0 && ` · ${stats.lamps} lantern${stats.lamps > 1 ? 's' : ''}`}
          </span>
        )}
      </div>
    </div>
  );
});

export default Viewer;

