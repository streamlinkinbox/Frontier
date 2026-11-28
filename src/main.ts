import './style.css';
import * as THREE from 'three';
import { clamp, lerp } from './math/noise';
import { createProceduralTextureLibrary } from './graphics/proceduralTextures';
import {
  BIOME_SPECS,
  BiomeId,
  CrisscrossRootBridge,
  HollowLogTunnel,
  TrailCorridorWorld,
  WorldObstacle,
} from './world/trailCorridor';
import { VehicleControls, VehicleRigidBody } from './physics/vehiclePhysics';
import { VEHICLE_DESIGNS, VehicleDesignId } from './vehicle/vehicleConfigs';
import { VineEcosystem } from './vines/vineSystem';
import { FrontierBuggyVisual } from './vehicle/buggyMesh';
import { OffroadParticleSystem, RallyVineAudioEngine } from './graphics/particlesAndAudio';
import { UnrealObserverCamera } from './camera/unrealCamera';
import { ExploreMode, ExpeditionHud } from './ui/hud';

const BIOME_ORDER: BiomeId[] = [
  'FOREST',
  'SWAMP',
  'DESERT',
  'DEADWOOD',
  'SINGULARITY',
  'GIANT_LOG',
  'GLACIAL_ICE',
  'CARNIVOROUS_BOG',
  'UNDERGROUND_MARKET',
  'CANYON_BRIDGES',
  'CLOCKWORK_FOUNDRY',
  'CARGO_MEGASHIPS',
  'RUNAWAY_TRAINS',
  'STRIP_MINE',
];
const LEVEL_PORTAL_Z = -860;

class FrontierSimulationApp {
  private renderer: THREE.WebGLRenderer;
  private scene: THREE.Scene;
  private camera: THREE.PerspectiveCamera;
  private hemiLight: THREE.HemisphereLight;
  private sunLight: THREE.DirectionalLight;
  private sunTarget: THREE.Object3D;
  private skyDome: THREE.Mesh;
  private skyMat: THREE.ShaderMaterial;

  private world: TrailCorridorWorld;
  private vehicle: VehicleRigidBody;
  private vines: VineEcosystem;
  private buggyVisual: FrontierBuggyVisual;
  private particles: OffroadParticleSystem;
  private audio: RallyVineAudioEngine;
  private unrealCam: UnrealObserverCamera;
  private hud: ExpeditionHud;

  private exploreMode: ExploreMode = 'UNREAL_CAMERA';
  private keys = new Set<string>();
  private controls: VehicleControls = {
    throttle: 0,
    brake: 0,
    steer: 0,
    handbrake: false,
    boost: false,
  };

  private camCurrentPos = new THREE.Vector3(0, 4.5, 7.5);
  private camCurrentLook = new THREE.Vector3(0, 1.5, -10);

  private physicsAccumulator = 0;
  private elapsedTime = 0;
  private lastFrameTime = performance.now();
  private portalWarpCooldown = 0;
  private cliffFallTimer = 0;
  private _stormFogBase = new THREE.Color();
  private _lightningTint = new THREE.Color(0xdcf8ff);

  private nearbyObstaclesCache: WorldObstacle[] = [];
  private nearbyHollowLogsCache: HollowLogTunnel[] = [];
  private nearbyRootBridgesCache: CrisscrossRootBridge[] = [];

  constructor() {
    const appEl = document.getElementById('app')!;
    const canvas = document.getElementById('game-canvas') as HTMLCanvasElement;

    this.renderer = new THREE.WebGLRenderer({
      canvas,
      antialias: true,
      powerPreference: 'high-performance',
    });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.renderer.setSize(window.innerWidth, window.innerHeight);
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.1;

    this.scene = new THREE.Scene();
    const textures = createProceduralTextureLibrary();
    this.scene.environment = textures.skyEnvMap;
    this.scene.environmentIntensity = 0.72;
    this.scene.fog = new THREE.FogExp2(0xc5bf9e, 0.0034);

    const skyGeo = new THREE.SphereGeometry(720, 24, 16);
    this.skyMat = new THREE.ShaderMaterial({
      side: THREE.BackSide,
      depthWrite: false,
      uniforms: {
        uSunDir: { value: new THREE.Vector3(-0.42, 0.68, -0.6).normalize() },
        uHorizon: { value: new THREE.Color(0.82, 0.78, 0.64) },
        uZenith: { value: new THREE.Color(0.27, 0.49, 0.62) },
      },
      vertexShader: `
        varying vec3 vWorldDir;
        void main() {
          vWorldDir = position;
          gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        }
      `,
      fragmentShader: `
        varying vec3 vWorldDir;
        uniform vec3 uSunDir;
        uniform vec3 uHorizon;
        uniform vec3 uZenith;
        void main() {
          vec3 d = normalize(vWorldDir);
          float h = clamp(d.y, 0.0, 1.0);
          vec3 col = mix(uHorizon, uZenith, pow(h, 0.56));
          float sunAmt = max(dot(d, uSunDir), 0.0);
          col += vec3(1.0, 0.87, 0.58) * (pow(sunAmt, 24.0) * 0.78 + pow(sunAmt, 4.0) * 0.22);
          gl_FragColor = vec4(col, 1.0);
        }
      `,
    });
    this.skyDome = new THREE.Mesh(skyGeo, this.skyMat);
    this.scene.add(this.skyDome);

    this.camera = new THREE.PerspectiveCamera(68, window.innerWidth / window.innerHeight, 0.25, 820);

    this.hemiLight = new THREE.HemisphereLight(0xdce8dc, 0x3e3b26, 0.94);
    this.scene.add(this.hemiLight);

    this.sunLight = new THREE.DirectionalLight(0xfff0cc, 3.0);
    this.sunTarget = new THREE.Object3D();
    this.scene.add(this.sunLight);
    this.scene.add(this.sunTarget);
    this.sunLight.target = this.sunTarget;

    this.sunLight.castShadow = true;
    this.sunLight.shadow.mapSize.set(2048, 2048);
    this.sunLight.shadow.camera.near = 6;
    this.sunLight.shadow.camera.far = 225;
    const shadowHalf = 82;
    this.sunLight.shadow.camera.left = -shadowHalf;
    this.sunLight.shadow.camera.right = shadowHalf;
    this.sunLight.shadow.camera.top = shadowHalf;
    this.sunLight.shadow.camera.bottom = -shadowHalf;
    this.sunLight.shadow.bias = -0.0005;
    this.sunLight.shadow.normalBias = 0.04;

    this.world = new TrailCorridorWorld(textures);
    this.scene.add(this.world.group);

    this.vehicle = new VehicleRigidBody();
    // Protect parked showcase car while observing in Unreal Free-Cam
    this.vehicle.passiveObserverProtection = true;

    this.vines = new VineEcosystem(textures);
    this.scene.add(this.vines.group);

    this.world.onChunkSpawned((chunkIndex, anchors, rootAnchors) => {
      this.vines.spawnForChunk(chunkIndex, anchors, rootAnchors, this.world);
    });
    this.world.onChunkRemoved((chunkIndex) => {
      this.vines.removeChunkVines(chunkIndex);
    });

    this.buggyVisual = new FrontierBuggyVisual(textures, this.vines.chassisCreepGroup);
    this.scene.add(this.buggyVisual.root);

    this.particles = new OffroadParticleSystem(textures);
    this.scene.add(this.particles.points);
    this.scene.add(this.particles.pollenPoints);

    this.audio = new RallyVineAudioEngine();
    // Start muted in Observer Mode so inspecting levels is peaceful unless toggled ON
    this.audio.toggleMute();

    this.unrealCam = new UnrealObserverCamera(canvas, () => this.exploreMode === 'UNREAL_CAMERA');

    this.hud = new ExpeditionHud(appEl, this.vines, {
      onSelectBiome: (biome) => this.loadBiomeLevel(biome),
      onCycleNextBiome: () => this.cycleNextBiomeLevel(),
      onJumpCourseZ: (targetZ, label) => this.jumpObserverToCourseZ(targetZ, label),
      onJumpForwardDelta: (deltaM) => {
        const curZ =
          this.exploreMode === 'UNREAL_CAMERA' ? this.unrealCam.position.z : this.vehicle.position.z;
        this.jumpObserverToCourseZ(curZ - deltaM, `JUMPED +${deltaM}m DOWN COURSE`);
      },
      onSummonVehicleHere: () => this.placeVehicleInFrontOfObserver(),
      onToggleExploreMode: () => this.toggleExploreMode(),
      onSelectVehicle: (id) => this.selectVehicleDesign(id),
      onToggleAudio: () => {
        this.audio.unlock();
        return this.audio.toggleMute();
      },
      onChangeFlySpeed: (spd) => {
        this.unrealCam.baseSpeedMps = spd;
      },
    });

    this.unrealCam.onSpeedChanged = (spd) => {
      this.hud.syncFlySpeedUI(spd);
    };

    // Ambient World Visual FX (Falling Trees & Martian Sandstorm Lightning Strikes!)
    this.world.onTreeStartFalling = (basePos) => {
      this.particles.emitImpactDust(basePos, 1.0);
      this.audio.playTimberCrackSound();
    };
    this.world.onTreeSlamImpact = (impactPos) => {
      this.particles.emitTimberSlamShockwave(impactPos);
      this.audio.playTimberSlamSound();
    };
    this.world.singularityBuilder.onLightningStrike = (strikePos: THREE.Vector3) => {
      const dist = strikePos.distanceTo(this.camera.position);
      const intensity = clamp(1.25 - dist / 140.0, 0.45, 1.25);
      this.particles.emitImpactDust(strikePos, 1.45);
      this.audio.playThunderStrikeSound(intensity);
    };
    this.world.giantLogBuilder.onWormStrikeVehicle = (strikePos: THREE.Vector3) => {
      this.particles.emitImpactDust(strikePos, 1.15);
      this.audio.playVineLatchSound();
      this.hud.showCallout('PARASITE LOG-WORM LATCHED · BOOST [SHIFT] TO BREAK FREE!');
    };
    this.world.giantLogBuilder.onVehicleStuckInWeb = (stuckPos: THREE.Vector3) => {
      this.particles.emitImpactDust(stuckPos, 1.1);
      this.audio.playVineLatchSound();
      this.hud.showCallout('STUCK IN SPIDERWEB NETWORK · MASH [SHIFT] BOOST & [W/A/D] TO RIP FREE!');
    };
    this.world.giantLogBuilder.onVehicleBreakFreeWeb = (freePos: THREE.Vector3) => {
      this.particles.emitImpactDust(freePos, 1.35);
      this.audio.playVineSnapSound(true);
      this.hud.showCallout('TORE FREE FROM SPIDERWEB!');
    };
    this.world.giantLogBuilder.onMushroomBounce = (mushPos: THREE.Vector3) => {
      this.particles.emitImpactDust(mushPos, 1.25);
      this.audio.playFuelPickupSound();
      this.hud.showCallout('GIANT JELLY-CAP MUSHROOM LAUNCH!');
    };
    this.world.giantLogBuilder.onPuffballBurst = (puffPos: THREE.Vector3) => {
      this.particles.emitTimberSlamShockwave(puffPos);
      this.audio.playTimberCrackSound();
      this.hud.showCallout('SPORE PUFFBALL RUPTURED!');
    };
    this.world.glacialIceBuilder.onIceFractureBurst = (fracturePos: THREE.Vector3) => {
      const dist = fracturePos.distanceTo(this.camera.position);
      if (dist < 115) {
        this.particles.emitIceShatterBurst(fracturePos, 18);
        this.audio.playIceFracturePingSound(clamp(1.1 - dist / 115, 0.25, 1.0));
      }
    };
    this.world.glacialIceBuilder.onIcicleShatterSlam = (
      impactPos: THREE.Vector3,
      kind: 'POINTY_ICICLE' | 'ICE_BLOCK'
    ) => {
      const dist = impactPos.distanceTo(this.camera.position);
      if (dist < 130) {
        this.particles.emitIceShatterBurst(impactPos, kind === 'ICE_BLOCK' ? 36 : 26);
        this.audio.playIcicleShatterSound(clamp(1.2 - dist / 130, 0.3, 1.15));
      }
      if (dist < 42 && this.exploreMode === 'PEACEFUL_DRIVE') {
        this.hud.showCallout(
          kind === 'POINTY_ICICLE'
            ? 'FALLING CAVE ICICLE IMPALED ICE FLOOR!'
            : 'GLACIAL CAVE ICE BLOCK CRASHED DOWN!'
        );
      }
    };
    this.world.glacialIceBuilder.onVehicleCrossOpenCrack = (joltPos: THREE.Vector3) => {
      this.particles.emitIceShatterBurst(joltPos, 22);
      this.audio.playIceFracturePingSound(0.95);
      this.hud.showCallout('SLIPPERY ICE FRACTURE JOLT · COUNTER-STEER ON BLACK ICE!');
    };
    this.world.carnivorousBogBuilder.onFlytrapSnapShut = (
      snapPos: THREE.Vector3,
      caughtVehicle: boolean
    ) => {
      const dist = snapPos.distanceTo(this.camera.position);
      if (dist < 115) {
        this.particles.emitCarnivorousMistBurst(snapPos, true, caughtVehicle ? 34 : 22);
        this.audio.playFlytrapSnapSound(clamp(1.15 - dist / 115, 0.25, 1.1));
      }
      if (caughtVehicle) {
        this.hud.showCallout('CAUGHT IN COLOSSAL FLYTRAP JAWS · MASH [SHIFT] BOOST TO RIP FREE!');
      }
    };
    this.world.carnivorousBogBuilder.onVehicleBreakFreeFlytrap = (freePos: THREE.Vector3) => {
      this.particles.emitCarnivorousMistBurst(freePos, false, 28);
      this.audio.playVineSnapSound(true);
      this.hud.showCallout('TORE OUT OF INTERLOCKING FLYTRAP SPINES!');
    };
    this.world.carnivorousBogBuilder.onSundewMucilageHit = (hitPos: THREE.Vector3) => {
      this.particles.emitCarnivorousMistBurst(hitPos, true, 20);
      this.audio.playVineLatchSound();
      this.hud.showCallout('COATED IN STICKY SUNDEW MUCILAGE · [SHIFT] BOOST THROUGH!');
    };
    this.world.carnivorousBogBuilder.onLilyPadSubmerge = (padPos: THREE.Vector3) => {
      this.particles.emitCarnivorousMistBurst(padPos, false, 24);
      this.hud.showCallout('VICTORIA LILY PAD SUBMERGED · KEEP MOMENTUM ACROSS PADS!');
    };
    this.world.carnivorousBogBuilder.onPitcherGrappleLatch = (anchorPos: THREE.Vector3) => {
      this.particles.emitCarnivorousMistBurst(anchorPos, false, 18);
      this.audio.playPitcherGrappleSound();
      this.hud.showCallout('LATCHED PITCHER-PLANT VINE · HOLD [LMB] TO SWING ACROSS LAGOON!');
    };
    this.world.carnivorousBogBuilder.onPitcherGrappleRelease = () => {
      this.audio.playVineSnapSound(false);
    };
    this.world.undergroundMarketBuilder.onCrateSmash = (cratePos: THREE.Vector3) => {
      this.particles.emitImpactDust(cratePos, 1.15);
      this.audio.playTimberCrackSound();
    };
    this.world.undergroundMarketBuilder.onNeonSparkZap = (zapPos: THREE.Vector3) => {
      this.particles.emitVineSnapBurst(zapPos, true);
    };
    this.world.canyonBridgeBuilder.onRottenPlankSnap = (snapPos: THREE.Vector3) => {
      this.particles.emitImpactDust(snapPos, 1.45);
      this.audio.playTimberCrackSound();
      if (snapPos.distanceTo(this.camera.position) < 42) {
        this.hud.showCallout('ROTTEN BRIDGE PLANK SNAPPED · KEEP SPEED UP ACROSS THE GAP!');
      }
    };
    this.world.canyonBridgeBuilder.onBridgeCreak = () => {
      this.audio.playVineLatchSound();
    };
    this.world.clockworkBuilder.onSeesawAnvilSeat = (seatPos: THREE.Vector3) => {
      this.particles.emitVineSnapBurst(seatPos, true);
      this.audio.playTimberSlamSound();
      if (seatPos.distanceTo(this.camera.position) < 42) {
        this.hud.showCallout('COUNTERWEIGHT SEESAW BRIDGE SEATED FLUSH ON TARGET COGWHEEL!');
      }
    };
    this.world.clockworkBuilder.onGearPathwayLocked = (lockPos: THREE.Vector3) => {
      if (lockPos.distanceTo(this.camera.position) < 58) {
        this.particles.emitVineSnapBurst(lockPos, true);
        this.audio.playTimberSlamSound();
      }
      if (lockPos.distanceTo(this.camera.position) < 34) {
        this.hud.showCallout('GENEVA BRIDGE-GEAR LOCKED · PATHWAY ALIGNED TO CROSS!');
      }
    };
    this.world.clockworkBuilder.onPistonSteamVent = (ventPos: THREE.Vector3) => {
      if (ventPos.distanceTo(this.camera.position) < 60) {
        this.particles.emitImpactDust(ventPos, 0.9);
      }
    };
    this.world.cargoMegashipBuilder.onGantryBridgeLocked = (lockPos: THREE.Vector3) => {
      if (lockPos.distanceTo(this.camera.position) < 62) {
        this.particles.emitImpactDust(lockPos, 1.25);
        this.audio.playTimberSlamSound();
      }
      if (lockPos.distanceTo(this.camera.position) < 38) {
        this.hud.showCallout('GANTRY CRANE CONTAINER BRIDGE LOCKED · CLEAR TO CROSS!');
      }
    };
    this.world.cargoMegashipBuilder.onMagnetCapture = (magPos: THREE.Vector3) => {
      this.particles.emitIceShatterBurst(magPos, 20);
      this.audio.playPitcherGrappleSound();
      this.hud.showCallout(
        'ELECTROMAGNET CRANE LOCKED ON · HOISTING VEHICLE! [HOLD SHIFT BOOST TO SNAP FREE]'
      );
    };
    this.world.cargoMegashipBuilder.onMagnetRelease = (
      magPos: THREE.Vector3,
      brokeByBoost: boolean
    ) => {
      this.particles.emitVineSnapBurst(magPos, true);
      this.audio.playVineSnapSound(true);
      this.hud.showCallout(
        brokeByBoost
          ? 'BOOST OVERLOADED ELECTROMAGNET · BROKE FREE MID-AIR!'
          : 'CRANE COILS SHORTED OUT · DROPPING VEHICLE ONTO DECK!'
      );
    };
    this.world.cargoMegashipBuilder.onCraneBreakSpark = (sparkPos: THREE.Vector3) => {
      if (sparkPos.distanceTo(this.camera.position) < 75) {
        this.particles.emitVineSnapBurst(sparkPos, true);
        this.audio.playTimberCrackSound();
      }
    };
    this.world.cargoMegashipBuilder.onCraneCargoCrash = (crashPos: THREE.Vector3) => {
      if (crashPos.distanceTo(this.camera.position) < 78) {
        this.particles.emitTimberSlamShockwave(crashPos);
        this.particles.emitImpactDust(crashPos, 1.45);
        this.audio.playTimberSlamSound();
      }
      if (
        this.exploreMode === 'PEACEFUL_DRIVE' &&
        crashPos.distanceTo(this.vehicle.position) < 28
      ) {
        this.hud.showCallout('CRANE CABLE SNAPPED · LOOSE CONTAINER CRASHED ONTO DECK!');
      }
    };
    this.world.cargoMegashipBuilder.onLooseCargoSlam = (slamPos: THREE.Vector3) => {
      if (slamPos.distanceTo(this.camera.position) < 55) {
        this.particles.emitVineSnapBurst(slamPos, true);
        this.audio.playTimberCrackSound();
      }
    };
    this.world.cargoMegashipBuilder.onStormThunder = (intensity: number) => {
      this.audio.playThunderStrikeSound(intensity);
    };
    this.world.runawayTrainBuilder.onTrestleDynamiteBlast = (blastPos: THREE.Vector3) => {
      if (blastPos.distanceTo(this.camera.position) < 92) {
        this.particles.emitTimberSlamShockwave(blastPos);
        this.particles.emitVineSnapBurst(blastPos, true);
        this.particles.emitImpactDust(blastPos, 1.65);
        this.audio.playThunderStrikeSound(0.95);
        this.audio.playTimberSlamSound();
      }
      if (blastPos.distanceTo(this.camera.position) < 56) {
        this.hud.showCallout(
          'DYNAMITED TRESTLE COLLAPSING AHEAD · JUMP SIDEWAYS TO PARALLEL TRAIN!'
        );
      }
    };
    this.world.runawayTrainBuilder.onLogChainSnap = (snapPos: THREE.Vector3) => {
      if (snapPos.distanceTo(this.camera.position) < 62) {
        this.particles.emitVineSnapBurst(snapPos, true);
        this.audio.playTimberCrackSound();
      }
    };
    this.world.runawayTrainBuilder.onLocomotiveSteamBlast = (steamPos: THREE.Vector3) => {
      if (steamPos.distanceTo(this.camera.position) < 58) {
        this.particles.emitImpactDust(steamPos, 0.95);
      }
    };
    this.world.runawayTrainBuilder.onSignalBoomImpact = (hitPos: THREE.Vector3) => {
      this.particles.emitVineSnapBurst(hitPos, true);
      this.audio.playTimberSlamSound();
    };
    this.world.stripMineBuilder.onBenchDynamiteBlast = (
      blastPos: THREE.Vector3,
      chargeIndex: number
    ) => {
      if (blastPos.distanceTo(this.camera.position) < 96) {
        this.particles.emitTimberSlamShockwave(blastPos);
        this.particles.emitVineSnapBurst(blastPos, true);
        this.particles.emitImpactDust(blastPos, 1.55);
        this.audio.playThunderStrikeSound(0.78 + chargeIndex * 0.06);
        this.audio.playTimberSlamSound();
      }
      if (chargeIndex === 3 && blastPos.distanceTo(this.camera.position) < 62) {
        this.hud.showCallout(
          'RIPPLING HIGHWALL BLAST DROPPED FRESH LIMESTONE RAMP SHORTCUT!'
        );
      }
    };
    this.world.stripMineBuilder.onBucketWheelImpact = (hitPos: THREE.Vector3) => {
      this.particles.emitVineSnapBurst(hitPos, true);
      this.particles.emitImpactDust(hitPos, 1.35);
      this.audio.playTimberSlamSound();
      this.hud.showCallout('GRAZED 20M ROTATING BUCKET WHEEL · EXIT BOOM ON PORT RAMP!');
    };
    this.world.stripMineBuilder.onOreBoulderImpact = (hitPos: THREE.Vector3) => {
      this.particles.emitImpactDust(hitPos, 1.15);
      this.audio.playTimberCrackSound();
    };

    // Boot into Level [14] Open-Pit Strip Mine & Colossal Bucket-Wheel Excavators with Unreal Free-Fly Camera!
    this.loadBiomeLevel('STRIP_MINE', true);

    this.bindInputEvents();
    window.addEventListener('resize', () => this.onResize());

    requestAnimationFrame((t) => this.animate(t));
  }

  private applyBiomeAtmosphere(biome: BiomeId): void {
    const spec = BIOME_SPECS[biome];
    const fog = this.scene.fog as THREE.FogExp2;
    fog.color.setHex(spec.fogColorHex);
    fog.density = spec.fogDensity;

    this.sunLight.color.setHex(spec.sunColorHex);
    this.sunLight.intensity = spec.sunIntensity;
    this.hemiLight.color.setHex(spec.hemiSkyHex);
    this.hemiLight.groundColor.setHex(spec.hemiGroundHex);
    this.skyMat.uniforms.uHorizon.value.setRGB(
      spec.skyHorizonRGB[0],
      spec.skyHorizonRGB[1],
      spec.skyHorizonRGB[2]
    );
    this.skyMat.uniforms.uZenith.value.setRGB(
      spec.skyZenithRGB[0],
      spec.skyZenithRGB[1],
      spec.skyZenithRGB[2]
    );
  }

  private loadBiomeLevel(biome: BiomeId, initialBoot = false): void {
    this.vines.resetAll();
    this.applyBiomeAtmosphere(biome);

    this.world.setBiomeAndRebuild(biome, -8);
    this.world.setPortalGateAt(LEVEL_PORTAL_Z, false);

    // Park showcase vehicle peacefully just ahead of the camera so you can see scale & lighting!
    this.vehicle.resetAt(this.world, -14);
    this.vehicle.linearVelocity.set(0, 0, 0);
    this.vehicle.angularVelocity.set(0, 0, 0);

    // Position Unreal Camera slightly behind & above the showcase car looking down the new level!
    this.unrealCam.placeAtCourseZ(
      this.world,
      -3,
      biome === 'UNDERGROUND_MARKET' ? 3.8 : 6.2
    );
    this.snapDriveCameraImmediate();
    this.portalWarpCooldown = 1.5;

    this.hud.highlightSelectedBiome(biome);
    if (!initialBoot) {
      this.hud.flashPortalWarp();
      this.audio.playPortalWarpSound();
      this.hud.showCallout(`LOADED LEVEL: ${BIOME_SPECS[biome].title}`);
    }
  }

  private cycleNextBiomeLevel(): void {
    const curIdx = BIOME_ORDER.indexOf(this.world.currentBiome);
    const nextBiome = BIOME_ORDER[(curIdx + 1) % BIOME_ORDER.length];
    this.loadBiomeLevel(nextBiome);
  }

  private jumpObserverToCourseZ(targetZ: number, label: string): void {
    this.portalWarpCooldown = 1.5;
    this.world.updateStreaming(
      new THREE.Vector3(0, 0, targetZ),
      new THREE.Vector3(),
      this.elapsedTime,
      0,
      this.vehicle.position,
      0,
      this.exploreMode === 'UNREAL_CAMERA'
    );

    if (this.exploreMode === 'UNREAL_CAMERA') {
      this.unrealCam.placeAtCourseZ(
        this.world,
        targetZ,
        this.world.currentBiome === 'UNDERGROUND_MARKET' ? 4.0 : 8.5
      );
    } else {
      this.vehicle.resetAt(this.world, targetZ);
      this.snapDriveCameraImmediate();
    }
    this.hud.showCallout(label);
  }

  private placeVehicleInFrontOfObserver(): void {
    const fwd = this.unrealCam.getForwardVector();
    const horizFwd = new THREE.Vector3(fwd.x, 0, fwd.z);
    if (horizFwd.lengthSq() < 1e-4) horizFwd.set(0, 0, -1);
    horizFwd.normalize();

    const targetX = this.unrealCam.position.x + horizFwd.x * 11.0;
    const targetZ = this.unrealCam.position.z + horizFwd.z * 11.0;

    this.world.getNearbyRootBridges(targetZ - 35, targetZ + 35, this.nearbyRootBridgesCache);
    let groundY = this.world.sampleHeightRaw(targetX, targetZ).height;
    const rootY = this.world.sampleCrisscrossRootDeck(
      targetX,
      targetZ,
      this.unrealCam.position.y + 4.0,
      this.nearbyRootBridgesCache,
      new THREE.Vector3()
    );
    if (rootY !== null && rootY > groundY) {
      groundY = rootY;
    }

    this.vehicle.position.set(targetX, groundY + 0.85, targetZ);
    const yaw = Math.atan2(-horizFwd.x, -horizFwd.z);
    this.vehicle.quaternion.setFromAxisAngle(new THREE.Vector3(0, 1, 0), yaw);
    this.vehicle.linearVelocity.set(0, 0, 0);
    this.vehicle.angularVelocity.set(0, 0, 0);
    this.hud.showCallout('PLACED SHOWCASE VEHICLE AHEAD OF CAMERA');
  }

  private toggleExploreMode(): ExploreMode {
    if (this.exploreMode === 'UNREAL_CAMERA') {
      this.placeVehicleInFrontOfObserver();
      this.exploreMode = 'PEACEFUL_DRIVE';
      this.vehicle.passiveObserverProtection = false;
      this.camera.fov = 66;
      this.camera.updateProjectionMatrix();
      this.snapDriveCameraImmediate();
      this.hud.showCallout('DRIVE MODE · RACE THROUGH VILLAGE SHACKS, LIVE ROOTS & HAZARDS');
    } else {
      this.exploreMode = 'UNREAL_CAMERA';
      this.world.carnivorousBogBuilder.releasePitcherGrapple();
      this.vehicle.passiveObserverProtection = true;
      this.unrealCam.position.copy(this.camera.position);
      this.unrealCam.velocity.set(0, 0, 0);
      const fwd = new THREE.Vector3();
      this.camera.getWorldDirection(fwd);
      this.unrealCam.yaw = Math.atan2(-fwd.x, -fwd.z);
      this.unrealCam.pitch = clamp(Math.asin(clamp(fwd.y, -1, 1)), -1.45, 1.45);
      this.camera.fov = 68;
      this.camera.updateProjectionMatrix();
      this.hud.showCallout('UNREAL WASD FREE-CAM · DRAG MOUSE TO LOOK, WASD TO FLY');
    }
    return this.exploreMode;
  }

  private selectVehicleDesign(id: VehicleDesignId): void {
    const preset = VEHICLE_DESIGNS.find((d) => d.id === id);
    if (!preset) return;
    this.vehicle.applyPreset(preset);
    this.buggyVisual.setVehicleDesign(preset);
    this.hud.highlightSelectedVehicle(id);
    this.hud.showCallout(`VEHICLE MODEL: ${preset.name} (${preset.subtitle})`);
  }

  private bindInputEvents(): void {
    window.addEventListener('keydown', (e) => {
      const code = e.code;
      this.keys.add(code);

      if (code === 'Tab') {
        e.preventDefault();
        this.hud.toggleLab();
      } else if (code === 'Digit1') {
        this.loadBiomeLevel('FOREST');
      } else if (code === 'Digit2') {
        this.loadBiomeLevel('SWAMP');
      } else if (code === 'Digit3') {
        this.loadBiomeLevel('DESERT');
      } else if (code === 'Digit4') {
        this.loadBiomeLevel('DEADWOOD');
      } else if (code === 'Digit5') {
        this.loadBiomeLevel('SINGULARITY');
      } else if (code === 'Digit6') {
        this.loadBiomeLevel('GIANT_LOG');
      } else if (code === 'Digit7') {
        this.loadBiomeLevel('GLACIAL_ICE');
      } else if (code === 'Digit8') {
        this.loadBiomeLevel('CARNIVOROUS_BOG');
      } else if (code === 'Digit9') {
        this.loadBiomeLevel('UNDERGROUND_MARKET');
      } else if (code === 'Digit0') {
        this.loadBiomeLevel('CANYON_BRIDGES');
      } else if (code === 'Minus' || code === 'NumpadSubtract') {
        this.loadBiomeLevel('CLOCKWORK_FOUNDRY');
      } else if (code === 'Equal' || code === 'NumpadAdd') {
        this.loadBiomeLevel('CARGO_MEGASHIPS');
      } else if (code === 'Backslash' || code === 'BracketLeft') {
        this.loadBiomeLevel('RUNAWAY_TRAINS');
      } else if (code === 'BracketRight' || code === 'Backquote') {
        this.loadBiomeLevel('STRIP_MINE');
      } else if (code === 'KeyP') {
        this.cycleNextBiomeLevel();
      } else if (code === 'KeyR' && this.exploreMode === 'PEACEFUL_DRIVE') {
        this.world.giantLogBuilder.clearActiveWebSnare();
        this.world.carnivorousBogBuilder.releasePitcherGrapple();
        this.vehicle.resetAt(this.world, this.vehicle.position.z);
        this.hud.showCallout('VEHICLE RECOVERED ON ROAD');
      } else if (code === 'KeyJ') {
        const curZ =
          this.exploreMode === 'UNREAL_CAMERA' ? this.unrealCam.position.z : this.vehicle.position.z;
        this.jumpObserverToCourseZ(curZ - 200, 'JUMPED +200m DOWN COURSE');
      } else if (code === 'KeyT') {
        this.placeVehicleInFrontOfObserver();
      } else if (code === 'KeyV') {
        const mode = this.toggleExploreMode();
        this.hud.setExploreModeLabel(mode);
      } else if (code === 'KeyM') {
        this.audio.unlock();
        const muted = this.audio.toggleMute();
        this.hud.setAudioLabel(muted);
      }
    });

    window.addEventListener('keyup', (e) => {
      this.keys.delete(e.code);
    });

    // [LMB] Pitcher-Plant Vine Grapple-Swing in Drive Mode!
    window.addEventListener('mousedown', (e) => {
      if (
        e.button === 0 &&
        this.exploreMode === 'PEACEFUL_DRIVE' &&
        this.world.currentBiome === 'CARNIVOROUS_BOG' &&
        !(e.target as HTMLElement | null)?.closest('.hud-layer button, .hud-layer input')
      ) {
        this.audio.unlock();
        const chunks = this.world.getActiveCarnivorousBogChunks();
        this.world.carnivorousBogBuilder.tryLatchPitcherGrapple(chunks, this.vehicle.position);
      }
    });

    window.addEventListener('mouseup', (e) => {
      if (e.button === 0 && this.world.carnivorousBogBuilder.activeGrappleAnchor) {
        this.world.carnivorousBogBuilder.releasePitcherGrapple(this.vehicle.linearVelocity);
      }
    });

    window.addEventListener('blur', () => {
      this.keys.clear();
      this.world.carnivorousBogBuilder.releasePitcherGrapple();
    });
  }

  private pollDriveControls(dt: number): void {
    if (this.exploreMode !== 'PEACEFUL_DRIVE') {
      this.controls.throttle = 0;
      this.controls.brake = 1.0; // Keep parked showcase vehicle stationary on slopes!
      this.controls.steer = 0;
      this.controls.handbrake = true;
      this.controls.boost = false;
      return;
    }

    const accelPressed = this.keys.has('KeyW') || this.keys.has('ArrowUp');
    const brakePressed = this.keys.has('KeyS') || this.keys.has('ArrowDown');
    const leftPressed = this.keys.has('KeyA') || this.keys.has('ArrowLeft');
    const rightPressed = this.keys.has('KeyD') || this.keys.has('ArrowRight');

    this.controls.throttle = lerp(this.controls.throttle, accelPressed ? 1 : 0, clamp(14 * dt, 0, 1));
    this.controls.brake = lerp(this.controls.brake, brakePressed ? 1 : 0, clamp(16 * dt, 0, 1));

    const rawSteer = (rightPressed ? 1 : 0) - (leftPressed ? 1 : 0);
    this.controls.steer = lerp(this.controls.steer, rawSteer, clamp(14 * dt, 0, 1));
    this.controls.handbrake = this.keys.has('Space');
    this.controls.boost = this.keys.has('ShiftLeft') || this.keys.has('ShiftRight');
  }

  private snapDriveCameraImmediate(): void {
    const fwd = new THREE.Vector3(0, 0, -1).applyQuaternion(this.vehicle.quaternion);
    this.camCurrentPos
      .copy(this.vehicle.position)
      .addScaledVector(fwd, -7.8)
      .add(new THREE.Vector3(0, 2.9, 0));
    this.camCurrentLook
      .copy(this.vehicle.position)
      .addScaledVector(fwd, 11.0)
      .add(new THREE.Vector3(0, 1.9, 0));
  }

  private _camFwd = new THREE.Vector3();
  private _camVelDir = new THREE.Vector3();
  private _desiredCamPos = new THREE.Vector3();
  private _desiredLookAt = new THREE.Vector3();

  private updateCameraAndEnvironment(dt: number): void {
    if (this.exploreMode === 'UNREAL_CAMERA') {
      this.unrealCam.step(dt, this.keys, this.camera, this.world);
    } else {
      this._camFwd.set(0, 0, -1).applyQuaternion(this.vehicle.quaternion);
      this._camFwd.y = clamp(this._camFwd.y, -0.35, 0.35);
      this._camFwd.normalize();

      if (this.vehicle.linearVelocity.lengthSq() > 9.0 && this.vehicle.forwardSpeedMps > 1.5) {
        this._camVelDir.copy(this.vehicle.linearVelocity).normalize();
        this._camFwd.lerp(this._camVelDir, 0.38).normalize();
      }

      const inLog = this.vehicle.isInsideHollowLog;
      const distBack = inLog ? 5.8 : 7.9;
      const heightUp = inLog ? 1.85 : 2.85;

      this._desiredCamPos.copy(this.vehicle.position).addScaledVector(this._camFwd, -distBack);
      this._desiredCamPos.y = this.vehicle.position.y + heightUp;

      const camGround = this.world.sampleHeightRaw(
        this._desiredCamPos.x,
        this._desiredCamPos.z
      ).height;
      if (this.world.currentBiome === 'UNDERGROUND_MARKET') {
        this._desiredCamPos.y = clamp(this._desiredCamPos.y, 1.5, 6.9);
      } else {
        this._desiredCamPos.y = Math.max(this._desiredCamPos.y, camGround + 1.45);
      }

      this._desiredLookAt.copy(this.vehicle.position).addScaledVector(this._camFwd, 11.5);
      this._desiredLookAt.y = this.vehicle.position.y + (inLog ? 1.15 : 1.95);

      this.camCurrentPos.lerp(this._desiredCamPos, clamp(10.5 * dt, 0, 1));
      this.camCurrentLook.lerp(this._desiredLookAt, clamp(13.0 * dt, 0, 1));

      this.camera.position.copy(this.camCurrentPos);
      this.camera.lookAt(this.camCurrentLook);
    }

    // Center SkyDome, Shadow Frustum & Ambient Pollen around active observer viewpoint!
    const focus = this.camera.position;
    this.skyDome.position.copy(focus);

    const snap = 3.0;
    const focusX = Math.round(focus.x / snap) * snap;
    const focusZ = Math.round((focus.z - 24) / snap) * snap;
    const focusY = Math.round(this.world.sampleHeightRaw(focus.x, focus.z).height / snap) * snap;

    this.sunTarget.position.set(focusX, focusY, focusZ);
    this.sunLight.position.set(focusX - 42, focusY + 78, focusZ - 34);
  }

  private onResize(): void {
    this.camera.aspect = window.innerWidth / window.innerHeight;
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(window.innerWidth, window.innerHeight);
  }

  private animate(nowMs: number): void {
    requestAnimationFrame((t) => this.animate(t));

    const frameDt = Math.min((nowMs - this.lastFrameTime) / 1000, 0.05);
    this.lastFrameTime = nowMs;
    this.elapsedTime += frameDt;
    this.portalWarpCooldown = Math.max(0, this.portalWarpCooldown - frameDt);

    this.pollDriveControls(frameDt);
    this.updateCameraAndEnvironment(frameDt);

    const activeFocusPos =
      this.exploreMode === 'UNREAL_CAMERA' ? this.unrealCam.position : this.vehicle.position;
    const activeFocusVel =
      this.exploreMode === 'UNREAL_CAMERA' ? this.unrealCam.velocity : this.vehicle.linearVelocity;

    // Stream chunks & animate ambient level set-pieces around the active focus!
    this.world.updateStreaming(
      activeFocusPos,
      activeFocusVel,
      this.elapsedTime,
      frameDt,
      this.vehicle.position,
      Math.abs(this.vehicle.forwardSpeedMps),
      this.exploreMode === 'UNREAL_CAMERA',
      this.vehicle.linearVelocity,
      this.vehicle.isBoosting,
      this.controls.throttle,
      this.controls.steer
    );

    // Fly or drive through the Stone Pass Gate at the end of a course to warp to the next biome!
    if (this.portalWarpCooldown <= 0 && this.world.activePortal) {
      const pPos = this.world.activePortal.group.position;
      const distToPortal = Math.hypot(activeFocusPos.x - pPos.x, activeFocusPos.z - pPos.z);
      if (distToPortal < 16.5 && Math.abs(activeFocusPos.y - pPos.y) < 18.0) {
        this.cycleNextBiomeLevel();
      }
    }

    this.world.getNearbyObstacles(
      this.vehicle.position.z - 40,
      this.vehicle.position.z + 28,
      this.nearbyObstaclesCache
    );
    this.world.getNearbyHollowLogs(
      this.vehicle.position.z - 40,
      this.vehicle.position.z + 28,
      this.nearbyHollowLogsCache
    );
    this.world.getNearbyRootBridges(
      this.vehicle.position.z - 40,
      this.vehicle.position.z + 28,
      this.nearbyRootBridgesCache
    );

    const fixedDt = 1 / 240;
    this.physicsAccumulator = Math.min(this.physicsAccumulator + frameDt, fixedDt * 8);

    while (this.physicsAccumulator >= fixedDt) {
      // Vines wake, grow & sway around the observer camera, with allowVehicleAttack = FALSE in Unreal Cam!
      this.vines.stepPhysicsSubstep(
        fixedDt,
        this.elapsedTime,
        this.vehicle,
        this.world,
        this.nearbyObstaclesCache,
        activeFocusPos,
        this.exploreMode === 'PEACEFUL_DRIVE'
      );
      this.vehicle.stepPhysics(
        fixedDt,
        this.controls,
        this.world,
        this.nearbyObstaclesCache,
        this.nearbyHollowLogsCache,
        this.nearbyRootBridgesCache
      );
      if (this.exploreMode === 'PEACEFUL_DRIVE' && this.world.currentBiome === 'CARNIVOROUS_BOG') {
        this.world.carnivorousBogBuilder.stepPitcherGrappleSubstep(
          fixedDt,
          this.vehicle.position,
          this.vehicle.linearVelocity
        );
      }
      this.physicsAccumulator -= fixedDt;
    }

    // Auto-recover vehicle if it plunges off a sheer precipice, overboard into the storm sea, or into the trestle gorge!
    if (
      this.exploreMode === 'PEACEFUL_DRIVE' &&
      (this.world.currentBiome === 'SINGULARITY' ||
        this.world.currentBiome === 'CANYON_BRIDGES' ||
        this.world.currentBiome === 'CLOCKWORK_FOUNDRY' ||
        this.world.currentBiome === 'CARGO_MEGASHIPS' ||
        this.world.currentBiome === 'RUNAWAY_TRAINS' ||
        this.world.currentBiome === 'STRIP_MINE')
    ) {
      const cliffSpec = this.world.getTrailSpec(this.vehicle.position.z);
      const rescueThresholdY =
        this.world.currentBiome === 'CARGO_MEGASHIPS'
          ? -10.5
          : this.world.currentBiome === 'RUNAWAY_TRAINS' ||
            this.world.currentBiome === 'STRIP_MINE'
          ? -14.0
          : cliffSpec.baseY - 22.0;
      if (this.vehicle.position.y < rescueThresholdY) {
        this.cliffFallTimer += frameDt;
        if (this.cliffFallTimer > 0.75) {
          this.cliffFallTimer = 0;
          this.vehicle.resetAt(this.world, this.vehicle.position.z);
          this.hud.showCallout(
            this.world.currentBiome === 'STRIP_MINE'
              ? 'OPEN-PIT SUMP RESCUE · HOISTED ONTO NEAREST QUARRY HAUL BENCH!'
              : this.world.currentBiome === 'RUNAWAY_TRAINS'
              ? 'TRESTLE GORGE RESCUE · HOISTED ONTO NEAREST RUNAWAY TRAIN!'
              : this.world.currentBiome === 'CARGO_MEGASHIPS'
              ? 'MAN OVERBOARD RESCUE · HOISTED BACK ONTO NEAREST MEGASHIP DECK!'
              : this.world.currentBiome === 'CLOCKWORK_FOUNDRY'
              ? 'MOLTEN FOUNDRY RESCUE · RETURNED TO NEAREST BRASS COGWHEEL!'
              : this.world.currentBiome === 'CANYON_BRIDGES'
              ? 'CANYON CHASM RESCUE · RETURNED TO NEAREST SKY-ISLAND!'
              : 'CLIFF ABYSS RESCUE · RETURNED TO RIDGE'
          );
        }
      } else {
        this.cliffFallTimer = 0;
      }
    } else {
      this.cliffFallTimer = 0;
    }

    // Dynamic Storm Lightning Sky/Fog Flash Modulation (Martian Singularity & Storm-Tossed Cargo Megaships)!
    if (
      this.world.currentBiome === 'SINGULARITY' ||
      this.world.currentBiome === 'CARGO_MEGASHIPS'
    ) {
      const spec = BIOME_SPECS[this.world.currentBiome];
      const flash =
        this.world.currentBiome === 'CARGO_MEGASHIPS'
          ? this.world.cargoMegashipBuilder.currentLightningFlash
          : this.world.singularityBuilder.currentLightningFlash;
      const fog = this.scene.fog as THREE.FogExp2;
      this._stormFogBase.setHex(spec.fogColorHex);
      fog.color.copy(this._stormFogBase).lerp(this._lightningTint, flash * 0.62);
      this.sunLight.intensity = spec.sunIntensity + flash * 4.8;
      this.skyMat.uniforms.uHorizon.value.setRGB(
        lerp(spec.skyHorizonRGB[0], 0.88, flash * 0.72),
        lerp(spec.skyHorizonRGB[1], 0.96, flash * 0.72),
        lerp(spec.skyHorizonRGB[2], 1.0, flash * 0.72)
      );
      this.skyMat.uniforms.uZenith.value.setRGB(
        lerp(spec.skyZenithRGB[0], 0.55, flash * 0.65),
        lerp(spec.skyZenithRGB[1], 0.76, flash * 0.65),
        lerp(spec.skyZenithRGB[2], 0.88, flash * 0.65)
      );
    }

    this.buggyVisual.syncWithPhysics(this.vehicle, frameDt, false);
    this.vines.updateVisualGeometry();
    this.particles.update(frameDt, this.vehicle, this.elapsedTime, activeFocusPos);
    this.audio.updateContinuous(this.vehicle, 0);
    this.hud.update(this.exploreMode, this.unrealCam, this.vehicle, this.world);

    this.renderer.render(this.scene, this.camera);
  }
}

new FrontierSimulationApp();
