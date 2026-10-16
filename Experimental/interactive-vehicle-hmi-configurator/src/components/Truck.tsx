import { useEffect, useMemo, useRef } from "react";
import { Html, Sparkles } from "@react-three/drei";
import { useFrame, useThree } from "@react-three/fiber";
import * as THREE from "three";
import { RoundedBoxGeometry } from "three-stdlib";
import { useConfig, getColor, getWheel, getMode } from "../store";
import { COLORS, WHEELS } from "../data/config";

const SCAN = "#8fd8f2";

/* ============================================================
   SHARED UNIFORMS
   - Scan: uScanX (travelling front) + uHoloAmt
   - Ripple: expanding sphere from uRipCenter interpolating
     previous -> next material (color, roughness, metalness)
   ============================================================ */
interface ScanUniforms {
  uScanX: { value: number };
  uHoloAmt: { value: number };
}

interface RippleUniforms {
  uRipCenter: { value: THREE.Vector3 };
  uRipR: { value: number };
  uRipNext: { value: THREE.Color };
  uRipNextRough: { value: number };
  uRipNextMetal: { value: number };
  uRipActive: { value: number };
}

function makeRippleUniforms(): RippleUniforms {
  return {
    uRipCenter: { value: new THREE.Vector3(2.75, 1.25, 0) },
    uRipR: { value: 0 },
    uRipNext: { value: new THREE.Color("#ffffff") },
    uRipNextRough: { value: 0.3 },
    uRipNextMetal: { value: 0.9 },
    uRipActive: { value: 0 },
  };
}

/** Patches a standard/physical material: scan fade + optional circular ripple. */
function makeScannable<T extends THREE.Material>(mat: T, u: ScanUniforms, rip?: RippleUniforms): T {
  mat.onBeforeCompile = (shader) => {
    shader.uniforms.uScanX = u.uScanX;
    shader.uniforms.uHoloAmt = u.uHoloAmt;
    if (rip) {
      shader.uniforms.uRipCenter = rip.uRipCenter;
      shader.uniforms.uRipR = rip.uRipR;
      shader.uniforms.uRipNext = rip.uRipNext;
      shader.uniforms.uRipNextRough = rip.uRipNextRough;
      shader.uniforms.uRipNextMetal = rip.uRipNextMetal;
      shader.uniforms.uRipActive = rip.uRipActive;
    }
    shader.vertexShader = shader.vertexShader
      .replace("#include <common>", "#include <common>\nvarying vec3 vScanWP;")
      .replace(
        "#include <project_vertex>",
        "#include <project_vertex>\nvScanWP = (modelMatrix * vec4(transformed, 1.0)).xyz;"
      );
    shader.fragmentShader = shader.fragmentShader
      .replace(
        "#include <common>",
        `#include <common>
        varying vec3 vScanWP;
        uniform float uScanX;
        uniform float uHoloAmt;${
          rip
            ? `
        uniform vec3 uRipCenter;
        uniform float uRipR;
        uniform vec3 uRipNext;
        uniform float uRipNextRough;
        uniform float uRipNextMetal;
        uniform float uRipActive;`
            : ""
        }`
      )
      .replace(
        "#include <color_fragment>",
        `#include <color_fragment>
        float _ripM = 0.0;${
          rip
            ? `
        _ripM = (1.0 - smoothstep(uRipR - 0.55, uRipR, distance(vScanWP, uRipCenter))) * uRipActive;
        diffuseColor.rgb = mix(diffuseColor.rgb, uRipNext, _ripM);`
            : ""
        }
        {
          float sd = vScanWP.x - uScanX;
          float sf = smoothstep(-0.9, -0.05, sd);
          diffuseColor.a *= 1.0 - sf * uHoloAmt * 0.965;
        }`
      );
    if (rip) {
      shader.fragmentShader = shader.fragmentShader
        .replace(
          "#include <roughnessmap_fragment>",
          "#include <roughnessmap_fragment>\nroughnessFactor = mix(roughnessFactor, uRipNextRough, _ripM);"
        )
        .replace(
          "#include <metalnessmap_fragment>",
          "#include <metalnessmap_fragment>\nmetalnessFactor = mix(metalnessFactor, uRipNextMetal, _ripM);"
        );
    }
  };
  return mat;
}

/* ============================================================
   HOLOGRAM GHOST SHADER — renders only behind the scan front
   ============================================================ */
const holoVert = /* glsl */ `
  varying vec3 vNormal;
  varying vec3 vView;
  varying vec3 vWorld;

  void main() {
    vec4 worldPos = modelMatrix * vec4(position, 1.0);
    vWorld = worldPos.xyz;
    vNormal = normalize(mat3(modelMatrix) * normal);
    vView = normalize(cameraPosition - worldPos.xyz);
    gl_Position = projectionMatrix * viewMatrix * worldPos;
  }
`;

const holoFrag = /* glsl */ `
  uniform float uTime;
  uniform vec3 uColor;
  uniform vec3 uAccent;
  uniform float uScanX;
  uniform float uHoloAmt;

  varying vec3 vNormal;
  varying vec3 vView;
  varying vec3 vWorld;

  void main() {
    float scanD = vWorld.x - uScanX;
    float region = smoothstep(-0.9, -0.05, scanD);
    float vis = region * uHoloAmt;
    if (vis < 0.003) discard;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(vView);

    float fres = pow(1.0 - abs(dot(N, V)), 2.3);
    float lines = smoothstep(0.72, 1.0, sin(vWorld.y * 85.0) * 0.5 + 0.5);
    float edge = exp(-pow((scanD + 0.18) * 4.2, 2.0));

    float a = (0.028 + fres * 0.50 + lines * 0.040 + edge * 0.30) * vis;
    vec3 col = uColor + uAccent * edge * 1.15;

    gl_FragColor = vec4(col, a);
  }
`;

type Vec3 = [number, number, number];

/* ---------- A part rendered twice: solid PBR + ghost overlay ---------- */
function GhostMesh({
  geo,
  mat,
  holo,
  edgeGeo,
  edgeMat,
  position = [0, 0, 0],
  rotation = [0, 0, 0],
  shadow = false,
}: {
  geo: THREE.BufferGeometry;
  mat: THREE.Material;
  holo: THREE.Material;
  edgeGeo?: THREE.BufferGeometry;
  edgeMat?: THREE.Material;
  position?: Vec3;
  rotation?: Vec3;
  shadow?: boolean;
}) {
  return (
    <group position={position} rotation={rotation}>
      <mesh geometry={geo} material={mat} castShadow={shadow} receiveShadow={shadow} />
      <mesh geometry={geo} material={holo} renderOrder={6} />
      {edgeGeo && edgeMat && <lineSegments geometry={edgeGeo} material={edgeMat} renderOrder={7} />}
    </group>
  );
}

interface PartMats {
  holo: THREE.Material;
  edgeMat: THREE.Material;
}

function BodyPart({
  args,
  radius = 0.14,
  position,
  mat,
  pm,
  withEdges = true,
}: {
  args: [number, number, number];
  radius?: number;
  position: Vec3;
  mat: THREE.Material;
  pm: PartMats;
  withEdges?: boolean;
}) {
  const [w, h, d] = args;
  const geo = useMemo(() => new RoundedBoxGeometry(w, h, d, 4, radius), [w, h, d, radius]);
  const edgeGeo = useMemo(
    () => (withEdges ? new THREE.EdgesGeometry(geo, 14) : undefined),
    [geo, withEdges]
  );
  return (
    <GhostMesh geo={geo} mat={mat} holo={pm.holo} edgeGeo={edgeGeo} edgeMat={pm.edgeMat} position={position} shadow />
  );
}

/* ---------- Wheel rim face ---------- */
function Rim({ rimMat, pm, z }: { rimMat: THREE.Material; pm: PartMats; z: number }) {
  const wheelId = useConfig((s) => s.wheelId);
  const wheel = getWheel(wheelId);
  const dishGeo = useMemo(() => new THREE.CylinderGeometry(0.44, 0.44, 0.07, 36), []);
  const hubGeo = useMemo(() => new THREE.CylinderGeometry(0.13, 0.13, 0.08, 24), []);
  const spokeGeo = useMemo(() => new THREE.BoxGeometry(0.74, 0.11, 0.05), []);
  const lugGeo = useMemo(() => new THREE.CylinderGeometry(0.05, 0.05, 0.03, 16), []);
  const spokes = Array.from({ length: wheel.spokes });

  return (
    <group position={[0, 0, z]}>
      <GhostMesh geo={dishGeo} mat={rimMat} holo={pm.holo} rotation={[Math.PI / 2, 0, 0]} shadow />
      <GhostMesh geo={hubGeo} mat={rimMat} holo={pm.holo} rotation={[Math.PI / 2, 0, 0]} position={[0, 0, z >= 0 ? 0.04 : -0.04]} />
      {spokes.map((_, i) => {
        const a = (i / wheel.spokes) * Math.PI * 2;
        return (
          <GhostMesh
            key={i}
            geo={spokeGeo}
            mat={rimMat}
            holo={pm.holo}
            rotation={[0, 0, a]}
            position={[0, 0, z >= 0 ? 0.02 : -0.02]}
          />
        );
      })}
      <GhostMesh geo={lugGeo} mat={rimMat} holo={pm.holo} rotation={[Math.PI / 2, 0, 0]} position={[0, 0, z >= 0 ? 0.075 : -0.075]} />
    </group>
  );
}

function WheelAssembly({
  position,
  tireMat,
  sidewallMat,
  rimMat,
  pm,
}: {
  position: Vec3;
  tireMat: THREE.Material;
  sidewallMat: THREE.Material;
  rimMat: THREE.Material;
  pm: PartMats;
}) {
  const tireGeo = useMemo(() => new THREE.CylinderGeometry(0.63, 0.63, 0.44, 40), []);
  const tireEdgeGeo = useMemo(() => new THREE.EdgesGeometry(tireGeo, 28), [tireGeo]);
  const sidewallGeo = useMemo(() => new THREE.CylinderGeometry(0.64, 0.64, 0.36, 40), []);

  return (
    <group position={position}>
      <GhostMesh
        geo={tireGeo}
        mat={tireMat}
        holo={pm.holo}
        edgeGeo={tireEdgeGeo}
        edgeMat={pm.edgeMat}
        rotation={[Math.PI / 2, 0, 0]}
        shadow
      />
      <GhostMesh geo={sidewallGeo} mat={sidewallMat} holo={pm.holo} rotation={[Math.PI / 2, 0, 0]} />
      <Rim rimMat={rimMat} pm={pm} z={0.2} />
      <Rim rimMat={rimMat} pm={pm} z={-0.2} />
    </group>
  );
}

/* ---------- small emissive lamp (unpatched, stays solid) ---------- */
function Lamp({
  args,
  position,
  color,
  on,
  intensity = 1.1,
}: {
  args: [number, number, number];
  position: Vec3;
  color: string;
  on: boolean;
  intensity?: number;
}) {
  return (
    <mesh position={position}>
      <boxGeometry args={args} />
      <meshStandardMaterial
        color={color}
        emissive={color}
        emissiveIntensity={on ? intensity : 0.04}
        toneMapped={false}
        roughness={0.35}
      />
    </mesh>
  );
}

/* ---------- Projector optics + physical road illumination ---------- */
function RealisticHeadlights({
  on,
  highBeams,
  fogLights,
}: {
  on: boolean;
  highBeams: boolean;
  fogLights: boolean;
}) {
  const lowTargets = useMemo(
    () => [new THREE.Object3D(), new THREE.Object3D()],
    []
  );
  const highTargets = useMemo(
    () => [new THREE.Object3D(), new THREE.Object3D()],
    []
  );
  const fogTargets = useMemo(
    () => [new THREE.Object3D(), new THREE.Object3D()],
    []
  );
  const sides = [0.72, -0.72];

  return (
    <group>
      {sides.map((z, i) => (
        <group key={z}>
          {/* Dark lamp housing and clear projector optics. */}
          <mesh position={[2.79, 1.08, z]}>
            <boxGeometry args={[0.13, 0.34, 0.42]} />
            <meshPhysicalMaterial color="#07090c" metalness={0.7} roughness={0.22} clearcoat={1} />
          </mesh>
          <mesh position={[2.87, 1.08, z]} rotation={[0, Math.PI / 2, 0]}>
            <circleGeometry args={[0.105, 32]} />
            <meshPhysicalMaterial
              color={on ? "#dcecff" : "#25303a"}
              emissive={on ? "#dcecff" : "#000000"}
              emissiveIntensity={on ? 0.55 : 0}
              roughness={0.08}
              metalness={0.15}
              clearcoat={1}
              toneMapped
            />
          </mesh>
          <mesh position={[2.881, 1.08, z]} rotation={[0, Math.PI / 2, 0]}>
            <ringGeometry args={[0.112, 0.145, 32]} />
            <meshStandardMaterial color="#bcc5cd" metalness={0.92} roughness={0.2} />
          </mesh>

          {/* Low beams are aimed downward and outward onto the road. */}
          <primitive object={lowTargets[i]} position={[13, 0.12, z * 1.42]} />
          <spotLight
            position={[2.91, 1.08, z]}
            target={lowTargets[i]}
            color="#d9eaff"
            intensity={on ? 115 : 0}
            distance={26}
            angle={0.24}
            penumbra={0.72}
            decay={2}
          />

          {/* High beams add a longer, tighter center throw. */}
          <primitive object={highTargets[i]} position={[19, 0.72, z * 0.75]} />
          <spotLight
            position={[2.92, 1.1, z]}
            target={highTargets[i]}
            color="#e8f3ff"
            intensity={on && highBeams ? 185 : 0}
            distance={40}
            angle={0.13}
            penumbra={0.58}
            decay={2}
          />

          {/* Wide, low fog pattern. */}
          <mesh position={[2.81, 0.77, z * 1.08]} rotation={[0, Math.PI / 2, 0]}>
            <circleGeometry args={[0.07, 24]} />
            <meshStandardMaterial
              color={fogLights ? "#fff0c9" : "#2a2c2e"}
              emissive={fogLights ? "#fff0c9" : "#000000"}
              emissiveIntensity={fogLights ? 0.35 : 0}
              roughness={0.18}
            />
          </mesh>
          <primitive object={fogTargets[i]} position={[9, 0.05, z * 2.5]} />
          <spotLight
            position={[2.86, 0.77, z * 1.08]}
            target={fogTargets[i]}
            color="#ffe7b0"
            intensity={fogLights ? 62 : 0}
            distance={14}
            angle={0.42}
            penumbra={0.88}
            decay={2}
          />
        </group>
      ))}
    </group>
  );
}

/* ---------- Floating HUD annotation (scan mode) ---------- */
function Tag({ position, label, value }: { position: Vec3; label: string; value: string }) {
  return (
    <Html position={position} zIndexRange={[4, 0]} wrapperClass="pointer-events-none" style={{ pointerEvents: "none" }}>
      <div className="flex -translate-y-1/2 translate-x-2 items-center gap-2 whitespace-nowrap">
        <span className="relative flex h-2 w-2 shrink-0">
          <span className="absolute h-full w-full animate-ping rounded-full bg-cyan-300/60" />
          <span className="h-2 w-2 rounded-full bg-cyan-100 ring-2 ring-cyan-200/40" />
        </span>
        <span className="h-px w-7 bg-gradient-to-r from-cyan-200/80 to-cyan-200/0" />
        <span className="rounded-lg bg-black/55 px-2.5 py-1.5 ring-1 ring-cyan-100/25 backdrop-blur-sm">
          <span className="block text-[8px] font-semibold uppercase tracking-[0.22em] text-cyan-200/70">{label}</span>
          <span className="block text-[10px] font-semibold text-white/90">{value}</span>
        </span>
      </div>
    </Html>
  );
}

const easeInOutCubic = (x: number) => (x < 0.5 ? 4 * x * x * x : 1 - Math.pow(-2 * x + 2, 3) / 2);

/* ---------- circular material ripple runtime ---------- */
interface MatTarget {
  color: THREE.Color;
  rough: number;
  metal: number;
}
interface RipRun {
  active: boolean;
  t: number;
  dur: number;
  maxR: number;
  u: RippleUniforms;
  mat: THREE.MeshPhysicalMaterial;
  next: MatTarget;
  after?: () => void;
}

export default function Truck() {
  const scan = useConfig((s) => s.scan);
  const headlights = useConfig((s) => s.headlights);
  const highBeams = useConfig((s) => s.highBeams);
  const fogLights = useConfig((s) => s.fogLights);
  const lightBar = useConfig((s) => s.lightBar);
  const lightBarColor = useConfig((s) => s.lightBarColor);
  const underglow = useConfig((s) => s.underglow);
  const underglowColor = useConfig((s) => s.underglowColor);
  const modeId = useConfig((s) => s.modeId);
  const applyReq = useConfig((s) => s.applyReq);

  const mode = getMode(modeId);

  const camera = useThree((s) => s.camera);
  const raycaster = useMemo(() => new THREE.Raycaster(), []);

  /* ---------- shared uniforms & helper materials ---------- */
  const scanU = useMemo<ScanUniforms>(() => ({ uScanX: { value: 3.3 }, uHoloAmt: { value: 0 } }), []);
  const paintRipU = useMemo(makeRippleUniforms, []);
  const rimRipU = useMemo(makeRippleUniforms, []);
  const clipPlane = useMemo(() => new THREE.Plane(new THREE.Vector3(1, 0, 0), -99), []);
  const edgeMat = useMemo(
    () =>
      new THREE.LineBasicMaterial({
        color: SCAN,
        transparent: true,
        opacity: 0,
        clippingPlanes: [clipPlane],
      }),
    [clipPlane]
  );

  const holoMat = useMemo(
    () =>
      new THREE.ShaderMaterial({
        vertexShader: holoVert,
        fragmentShader: holoFrag,
        uniforms: {
          uTime: { value: 0 },
          uColor: { value: new THREE.Color("#a9dcec") },
          uAccent: { value: new THREE.Color("#38bdf8") },
          uScanX: scanU.uScanX,
          uHoloAmt: scanU.uHoloAmt,
        },
        transparent: true,
        depthWrite: false,
        side: THREE.DoubleSide,
        blending: THREE.AdditiveBlending,
      }),
    [scanU]
  );

  /* ---------- truck materials ---------- */
  const paintMat = useMemo(() => {
    const m = new THREE.MeshPhysicalMaterial({
      color: COLORS[0].hex,
      metalness: 0.9,
      roughness: 0.26,
      clearcoat: 1,
      clearcoatRoughness: 0.12,
      envMapIntensity: 1.35,
    });
    return makeScannable(m, scanU, paintRipU);
  }, [scanU, paintRipU]);

  const cladMat = useMemo(
    () =>
      makeScannable(
        new THREE.MeshStandardMaterial({ color: "#111317", metalness: 0.35, roughness: 0.65 }),
        scanU
      ),
    [scanU]
  );
  const glassMat = useMemo(
    () =>
      makeScannable(
        new THREE.MeshPhysicalMaterial({
          color: "#0a0d12",
          metalness: 0.6,
          roughness: 0.08,
          envMapIntensity: 1.6,
          transparent: true,
          opacity: 0.92,
          clearcoat: 1,
          depthWrite: false,
        }),
        scanU
      ),
    [scanU]
  );
  const tireMat = useMemo(
    () =>
      makeScannable(
        new THREE.MeshStandardMaterial({ color: "#0c0d0f", metalness: 0.1, roughness: 0.92 }),
        scanU
      ),
    [scanU]
  );
  const sidewallMat = useMemo(
    () =>
      makeScannable(
        new THREE.MeshStandardMaterial({ color: "#17181a", metalness: 0.1, roughness: 0.85 }),
        scanU
      ),
    [scanU]
  );
  const rimMat = useMemo(() => {
    const m = new THREE.MeshStandardMaterial({
      color: WHEELS[0].rim,
      metalness: 0.7,
      roughness: 0.45,
    });
    return makeScannable(m, scanU, rimRipU);
  }, [scanU, rimRipU]);

  const pm: PartMats = { holo: holoMat, edgeMat };

  // accent follows drive mode
  useEffect(() => {
    (holoMat.uniforms.uAccent.value as THREE.Color).set(mode.accent);
  }, [holoMat, mode]);

  /* ---------- ripple engine ---------- */
  const paintRun = useRef<RipRun>({
    active: false,
    t: 0,
    dur: 1.35,
    maxR: 6.4,
    u: paintRipU,
    mat: paintMat,
    next: { color: new THREE.Color(COLORS[0].hex), rough: 0.26, metal: 0.9 },
  });
  const rimRun = useRef<RipRun>({
    active: false,
    t: 0,
    dur: 1.5,
    maxR: 4.6,
    u: rimRipU,
    mat: rimMat as unknown as THREE.MeshPhysicalMaterial,
    next: { color: new THREE.Color(WHEELS[0].rim), rough: 0.45, metal: 0.7 },
  });

  const finishRun = (run: RipRun) => {
    run.mat.color.copy(run.next.color);
    run.mat.roughness = run.next.rough;
    run.mat.metalness = run.next.metal;
    run.u.uRipActive.value = 0;
    run.active = false;
    run.after?.();
    run.after = undefined;
  };

  const beginRun = (
    run: RipRun,
    center: THREE.Vector3,
    next: MatTarget,
    dur: number,
    maxR: number,
    after?: () => void
  ) => {
    if (run.active) finishRun(run); // commit any in-flight ripple instantly
    run.next = next;
    run.after = after;
    run.dur = dur;
    run.maxR = maxR;
    run.t = 0;
    run.active = true;
    run.u.uRipCenter.value.copy(center);
    run.u.uRipNext.value.copy(next.color);
    run.u.uRipNextRough.value = next.rough;
    run.u.uRipNextMetal.value = next.metal;
    run.u.uRipR.value = 0;
    run.u.uRipActive.value = 1;
  };

  const stepRun = (run: RipRun, dt: number) => {
    if (!run.active) return;
    run.t += dt;
    const k = Math.min(run.t / run.dur, 1);
    const e = 1 - Math.pow(1 - k, 3); // easeOutCubic
    run.u.uRipR.value = e * run.maxR;
    if (k >= 1) finishRun(run);
  };

  /* ---------- respond to drag-drop / click apply requests ---------- */
  const group = useRef<THREE.Group>(null);
  const lastNonce = useRef(-1);
  useEffect(() => {
    if (!applyReq || applyReq.nonce === lastNonce.current) return;
    lastNonce.current = applyReq.nonce;
    const st = useConfig.getState();

    // ripple origin: raycast the drop point onto the truck, else defaults
    let center =
      applyReq.kind === "paint"
        ? new THREE.Vector3(2.75, 1.25, 0) // nose
        : new THREE.Vector3(1.55, 0.63, 1.0); // front wheel
    if (applyReq.ndc && group.current) {
      raycaster.setFromCamera(new THREE.Vector2(applyReq.ndc[0], applyReq.ndc[1]), camera);
      const hits = raycaster.intersectObject(group.current, true);
      if (hits.length) center = hits[0].point.clone();
    }

    if (applyReq.kind === "paint") {
      const c = getColor(st.colorId);
      const f = st.finish;
      const target: MatTarget = {
        color: new THREE.Color(c.hex),
        rough: f === "matte" ? 0.72 : c.metallic ? 0.26 : 0.45,
        metal: c.metallic ? 0.9 : 0.35,
      };
      beginRun(paintRun.current, center, target, 1.35, 6.4, () => {
        paintMat.clearcoat = f === "matte" ? 0.1 : 1;
        paintMat.clearcoatRoughness = f === "matte" ? 0.65 : 0.12;
      });
    } else {
      const w = getWheel(st.wheelId);
      const target: MatTarget = {
        color: new THREE.Color(w.rim),
        rough: w.machined ? 0.18 : 0.45,
        metal: w.machined ? 0.95 : 0.7,
      };
      beginRun(rimRun.current, center, target, 1.5, 4.6);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [applyReq]);

  /* ---------- scan animation state machine ---------- */
  const curtain = useRef<THREE.Group>(null);
  const curtainMats = useRef<THREE.MeshBasicMaterial[]>([]);
  const ramp = useRef(0);
  const sweepT = useRef(0);
  const flagsOn = useRef(false);
  const matsRef = useRef<THREE.Material[]>([]);
  matsRef.current = [paintMat, cladMat, tireMat, sidewallMat, rimMat];

  useEffect(() => {
    if (scan) sweepT.current = 0;
  }, [scan]);

  useFrame((state, dtRaw) => {
    const dt = Math.min(dtRaw, 0.05);
    const t = state.clock.elapsedTime;
    holoMat.uniforms.uTime.value = t;

    // material ripples
    stepRun(paintRun.current, dt);
    stepRun(rimRun.current, dt);

    // toggle ramp
    const target = scan ? 1 : 0;
    ramp.current = THREE.MathUtils.damp(ramp.current, target, 3.0, dt);
    if (Math.abs(ramp.current - target) < 0.002) ramp.current = target;

    // sweep cycle: front -> back, hold, fade back to solid, repeat
    const CYCLE = 6.6;
    const SWEEP = 4.6;
    const HOLD = 1.0;
    let cycleAmt = 1;
    let sweeping = false;
    if (scan) {
      sweepT.current += dt;
      const ph = sweepT.current % CYCLE;
      const sp = ph < SWEEP ? easeInOutCubic(ph / SWEEP) : 1;
      sweeping = ph < SWEEP;
      scanU.uScanX.value = THREE.MathUtils.lerp(3.25, -3.35, sp);
      cycleAmt = ph < SWEEP + HOLD ? 1 : Math.max(0, 1 - (ph - SWEEP - HOLD) / (CYCLE - SWEEP - HOLD));
    }
    scanU.uHoloAmt.value = ramp.current * (scan ? cycleAmt : 1);

    // flip solid materials between opaque and transparent-blend modes
    const wantTransparent = ramp.current > 0.02;
    if (wantTransparent !== flagsOn.current) {
      flagsOn.current = wantTransparent;
      matsRef.current.forEach((m) => {
        m.transparent = wantTransparent;
        m.depthWrite = !wantTransparent;
      });
    }

    // wireframe edges only live on the scanned side of the front
    clipPlane.constant = -(scanU.uScanX.value + 0.05);
    edgeMat.opacity = 0.55 * scanU.uHoloAmt.value;

    // scan curtain riding the wavefront
    if (curtain.current) {
      curtain.current.position.x = scanU.uScanX.value;
      const vis = ramp.current * cycleAmt * (sweeping ? 1 : 0.25);
      curtainMats.current.forEach((m, i) => {
        m.opacity = (i === 0 ? 0.075 : 0.3) * vis;
      });
    }

    // idle bob
    if (group.current) {
      group.current.position.y = -0.62 + Math.sin(t * 0.8) * 0.012;
    }
  });

  const wheelPositions: Vec3[] = [
    [1.55, 0.63, 0.98],
    [1.55, 0.63, -0.98],
    [-1.62, 0.63, 0.98],
    [-1.62, 0.63, -0.98],
  ];

  const glassGeo = useMemo(() => new THREE.BoxGeometry(2.32, 0.54, 1.96), []);
  const glassEdgeGeo = useMemo(() => new THREE.EdgesGeometry(glassGeo), [glassGeo]);
  const mirrorGeo = useMemo(() => new THREE.BoxGeometry(0.12, 0.2, 0.34), []);

  return (
    <group ref={group} position={[0, -0.62, 0]}>
      {/* ===== BODY ===== */}
      <BodyPart args={[5.0, 0.86, 2.02]} position={[0, 0.98, 0]} radius={0.22} mat={paintMat} pm={pm} />
      <BodyPart args={[1.5, 0.42, 1.9]} position={[1.62, 1.4, 0]} radius={0.12} mat={paintMat} pm={pm} />
      <BodyPart args={[2.25, 1.02, 1.86]} position={[-0.12, 1.62, 0]} radius={0.17} mat={paintMat} pm={pm} />
      <BodyPart args={[2.0, 0.16, 1.7]} position={[-0.12, 2.16, 0]} radius={0.07} mat={paintMat} pm={pm} />

      {/* glass band */}
      <GhostMesh
        geo={glassGeo}
        mat={glassMat}
        holo={holoMat}
        edgeGeo={glassEdgeGeo}
        edgeMat={edgeMat}
        position={[-0.12, 1.68, 0]}
      />

      {/* ===== BED ===== */}
      <BodyPart args={[2.3, 0.4, 0.12]} position={[-1.55, 1.42, 0.92]} radius={0.05} mat={paintMat} pm={pm} />
      <BodyPart args={[2.3, 0.4, 0.12]} position={[-1.55, 1.42, -0.92]} radius={0.05} mat={paintMat} pm={pm} />
      <BodyPart args={[0.12, 0.42, 1.92]} position={[-2.52, 1.42, 0]} radius={0.05} mat={paintMat} pm={pm} />

      {/* ===== CLADDING ===== */}
      <BodyPart args={[4.5, 0.26, 2.12]} position={[0, 0.56, 0]} radius={0.06} mat={cladMat} pm={pm} />
      <BodyPart args={[0.55, 0.52, 2.18]} position={[2.5, 0.72, 0]} radius={0.06} mat={cladMat} pm={pm} />
      <BodyPart args={[0.4, 0.42, 2.12]} position={[-2.62, 0.78, 0]} radius={0.06} mat={cladMat} pm={pm} />
      <BodyPart args={[0.22, 0.6, 1.9]} position={[2.66, 1.2, 0]} radius={0.04} mat={cladMat} pm={pm} />

      {/* side mirrors */}
      {[0.95, -0.95].map((z) => (
        <GhostMesh
          key={z}
          geo={mirrorGeo}
          mat={cladMat}
          holo={holoMat}
          position={[0.95, 1.78, z * 1.08]}
          shadow
        />
      ))}

      {/* ===== LIGHTING: projector optics + real spot illumination ===== */}
      <RealisticHeadlights on={headlights} highBeams={highBeams} fogLights={fogLights} />
      <Lamp args={[0.1, 0.14, 1.5]} position={[2.78, 1.28, 0]} color={lightBarColor} on={lightBar} intensity={1.2} />
      {[-0.4, -0.2, 0, 0.2, 0.4].map((z) => (
        <Lamp key={z} args={[0.14, 0.08, 0.1]} position={[0.9, 2.26, z]} color="#ffae3a" on={headlights} intensity={0.95} />
      ))}
      <Lamp args={[0.1, 0.44, 1.6]} position={[-2.68, 1.18, 0]} color="#ff2b2b" on={headlights} intensity={1.1} />

      {/* ===== WHEELS ===== */}
      {wheelPositions.map((p, i) => (
        <WheelAssembly key={i} position={p} tireMat={tireMat} sidewallMat={sidewallMat} rimMat={rimMat} pm={pm} />
      ))}

      {/* ===== UNDERGLOW ===== */}
      {underglow && (
        <>
          <mesh rotation={[-Math.PI / 2, 0, 0]} position={[0, 0.06, 0]}>
            <planeGeometry args={[4.6, 2.2]} />
            <meshStandardMaterial
              color={underglowColor}
              emissive={underglowColor}
              emissiveIntensity={1.1}
              toneMapped={false}
              transparent
              opacity={0.4}
            />
          </mesh>
          <pointLight position={[0, 0.15, 0]} color={underglowColor} intensity={3.5} distance={5} decay={2} />
        </>
      )}

      {/* mode accent wash */}
      <pointLight position={[0, 1.2, 2.6]} color={mode.accent} intensity={0.6} distance={8} decay={2} />

      {/* ===== SCAN CURTAIN (rides the wavefront) ===== */}
      <group ref={curtain} position={[3.3, 1.18, 0]}>
        {[
          { w: 2.4, h: 2.5 },
          { w: 0.55, h: 2.5 },
        ].map((p, i) => (
          <mesh key={i} rotation={[0, Math.PI / 2, 0]}>
            <planeGeometry args={[p.w, p.h]} />
            <meshBasicMaterial
              ref={(m) => {
                if (m) curtainMats.current[i] = m;
              }}
              color={mode.accent}
              transparent
              opacity={0}
              depthWrite={false}
              side={THREE.DoubleSide}
              blending={THREE.AdditiveBlending}
            />
          </mesh>
        ))}
      </group>

      {/* ===== SCAN-MODE EXTRAS ===== */}
      {scan && (
        <>
          <Sparkles count={130} scale={[7.5, 3.2, 4.2]} position={[0, 1.35, 0]} size={2.2} speed={0.35} opacity={0.55} color="#8fd8f2" />
          <Tag position={[2.35, 1.1, 0]} label="Front Motor" value="500 kW · e-diff lock" />
          <Tag position={[-0.5, 0.42, 0]} label="Ultium Battery Pack" value="212 kWh · 329 mi range" />
          <Tag position={[1.55, 0.63, 1.05]} label="35-in All-Terrain" value="TPMS 38 PSI · OK" />
          <Tag position={[-0.12, 2.1, 0]} label="Cabin" value="5 seats · panoramic glass" />
        </>
      )}
    </group>
  );
}
