import { Suspense } from "react";
import { Canvas } from "@react-three/fiber";
import {
  OrbitControls,
  Environment,
  ContactShadows,
  MeshReflectorMaterial,
  Float,
} from "@react-three/drei";
import Truck from "./Truck";
import { useConfig, getEnv, getMode } from "../store";

function Lights() {
  const modeId = useConfig((s) => s.modeId);
  const mode = getMode(modeId);
  return (
    <>
      <ambientLight intensity={0.45} />
      <directionalLight
        position={[7, 11, 6]}
        intensity={2.6}
        castShadow
        shadow-mapSize-width={2048}
        shadow-mapSize-height={2048}
        shadow-camera-left={-8}
        shadow-camera-right={8}
        shadow-camera-top={8}
        shadow-camera-bottom={-8}
        shadow-bias={-0.0004}
      />
      <directionalLight position={[-8, 5, -6]} intensity={1.3} color={mode.accent} />
      <directionalLight position={[0, 4, -9]} intensity={0.9} color="#88aaff" />
      <spotLight position={[0, 9, 4]} angle={0.5} penumbra={1} intensity={30} distance={25} decay={2} />
    </>
  );
}

function Floor() {
  return (
    <mesh rotation={[-Math.PI / 2, 0, 0]} position={[0, -0.63, 0]} receiveShadow>
      <planeGeometry args={[60, 60]} />
      <MeshReflectorMaterial
        resolution={768}
        mixBlur={1}
        mixStrength={30}
        blur={[320, 90]}
        roughness={0.92}
        depthScale={1.1}
        minDepthThreshold={0.4}
        maxDepthThreshold={1.3}
        color="#0a0b0e"
        metalness={0.55}
        mirror={0}
      />
    </mesh>
  );
}

export default function Scene() {
  const envId = useConfig((s) => s.envId);
  const autoRotate = useConfig((s) => s.autoRotate);
  const env = getEnv(envId);

  return (
    <Canvas
      shadows
      dpr={[1, 2]}
      gl={{ alpha: true, antialias: true, powerPreference: "high-performance" }}
      camera={{ position: [6.8, 2.9, 7.6], fov: 34 }}
      onCreated={({ gl }) => {
        gl.localClippingEnabled = true;
      }}
    >
      <Lights />
      <Suspense fallback={null}>
        <Float speed={1} rotationIntensity={0} floatIntensity={0}>
          <Truck />
        </Float>
        <Floor />
        <ContactShadows position={[0, -0.6, 0]} opacity={0.6} scale={16} blur={2.6} far={6} resolution={1024} color="#000000" />
        <Environment preset={env.preset} />
      </Suspense>

      <OrbitControls
        enablePan={false}
        autoRotate={autoRotate}
        autoRotateSpeed={0.8}
        minPolarAngle={0.35}
        maxPolarAngle={Math.PI / 2.05}
        minDistance={6}
        maxDistance={16}
        target={[0, 0.55, 0]}
        enableDamping
        dampingFactor={0.06}
      />
    </Canvas>
  );
}
