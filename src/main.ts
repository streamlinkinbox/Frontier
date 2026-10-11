import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TransformControls } from 'three/addons/controls/TransformControls.js';
import { WebGPURenderer } from 'three/webgpu';

import { RoadSpline } from './core/spline';
import { MaterialManager } from './core/materials';
import { RoadTopologyBuilder, RoadConfig } from './core/road-topology';
import { IntersectionSolver, IntersectionData } from './core/intersection-solver';
import { BridgeGenerator, BridgeConfig } from './core/bridge-generator';
import { UnderpassTunnelBuilder, TunnelConfig } from './core/underpass-tunnel';
import { RailwayBuilder, RailwayConfig } from './core/railway';
import { MarkingGenerator } from './core/markings';
import { DamageConstructionBuilder } from './core/damage-construction';
import { FurnitureBuilder } from './core/furniture';
import { TrafficAISystem } from './core/traffic-ai';
import { SceneExporter } from './core/export';
import { ProjectZeroUI } from './ui/project-zero-ui';

export class App {
  private renderer!: THREE.WebGLRenderer | any;
  private isWebGPU: boolean = false;
  private scene!: THREE.Scene;
  private camera!: THREE.PerspectiveCamera;
  private controls!: OrbitControls;
  private transformControls!: TransformControls;

  private materials!: MaterialManager;
  private trafficSystem!: TrafficAISystem;
  private ui!: ProjectZeroUI;

  // Scene Graphs
  private networkGroup: THREE.Group = new THREE.Group();
  private propGroup: THREE.Group = new THREE.Group();
  private trainGroup: THREE.Group = new THREE.Group();
  private gizmoTarget: THREE.Object3D = new THREE.Object3D();

  // Active Scenario Data
  private activeSplines: RoadSpline[] = [];
  private activeRoadConfigs: RoadConfig[] = [];
  private activeIntersections: IntersectionData[] = [];
  private railwaySpline: RoadSpline | null = null;
  private trainProgress: number = 0;

  // Telemetry
  private totalQuads: number = 0;
  private totalTriangles: number = 0;
  private lastTime: number = performance.now();
  private frameCount: number = 0;
  private currentFPS: number = 60;

  // Lights
  private dirLight!: THREE.DirectionalLight;
  private hemiLight!: THREE.HemisphereLight;
  private ambientLight!: THREE.AmbientLight;

  constructor() {
    this.init();
  }

  private async init() {
    // 1. Initialize Materials and UI first so DOM elements (including canvas) are created
    this.materials = new MaterialManager();
    this.trafficSystem = new TrafficAISystem();

    this.ui = new ProjectZeroUI(
      document.body,
      this.materials,
      {
        onScenarioChange: (scenarioId) => this.loadScenario(scenarioId),
        onToolChange: (toolId) => this.handleToolChange(toolId),
        onCameraPreset: (preset) => this.setCameraPreset(preset),
        onWireframeToggle: (enabled) => this.setWireframe(enabled),
        onLightingToggle: (isNight) => this.setLightingMode(isNight),
        onTrafficCountChange: (count) => this.trafficSystem.setMaxVehicles(count),
        onExportGLB: () => SceneExporter.exportGLB(this.networkGroup, 'Frontier_RoadNetwork.glb'),
        onExportOBJ: () => SceneExporter.exportOBJ(this.networkGroup, 'Frontier_RoadNetwork.obj'),
        onExportJSON: () => this.exportCurrentProject(),
        onRoadPropertyChange: (roadId, prop, val) => this.handleRoadPropChange(roadId, prop, val),
        onIntersectionPropertyChange: (nodeId, prop, val) => this.handleIntersectionPropChange(nodeId, prop, val),
        onMaterialLayerChange: () => this.materials.updateRoadTexture()
      }
    );

    const canvas = document.getElementById('viewport-canvas') as HTMLCanvasElement;
    const initialWidth = canvas.clientWidth || (window.innerWidth - 630);
    const initialHeight = canvas.clientHeight || (window.innerHeight - 98);

    // 2. Initialize WebGPU with WebGL2 Fallback
    try {
      if ('gpu' in navigator) {
        this.renderer = new WebGPURenderer({ canvas, antialias: true });
        await this.renderer.init();
        this.isWebGPU = true;
      } else {
        throw new Error('WebGPU not supported in navigator');
      }
    } catch (e) {
      console.warn('WebGPU not available or failed to initialize, falling back to WebGL2:', e);
      this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
      this.isWebGPU = false;
    }

    this.renderer.setSize(initialWidth, initialHeight);
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.05;

    // 3. Scene & Camera
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x0e0f12);
    this.scene.fog = new THREE.FogExp2(0x0e0f12, 0.0028);

    this.camera = new THREE.PerspectiveCamera(50, initialWidth / initialHeight, 0.5, 2000);
    this.camera.position.set(65, 45, 85);

    // 4. Orbit Controls
    this.controls = new OrbitControls(this.camera, canvas);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.05;
    this.controls.maxPolarAngle = Math.PI * 0.485; // prevent going underground
    this.controls.minDistance = 3;
    this.controls.maxDistance = 600;
    this.controls.target.set(0, 0, 0);

    // 5. Transform Gizmo for clickable & movable intersection nodes
    this.transformControls = new TransformControls(this.camera, canvas);
    this.transformControls.size = 0.85;
    this.transformControls.addEventListener('dragging-changed', (event) => {
      this.controls.enabled = !event.value;
    });

    this.scene.add(this.gizmoTarget);
    this.scene.add(this.transformControls.getHelper());

    this.transformControls.addEventListener('change', () => {
      if (this.ui.selectedEntity.type === 'intersection') {
        const id = this.ui.selectedEntity.id;
        const node = this.activeIntersections.find(n => n.id === id);
        if (node) {
          node.center.copy(this.gizmoTarget.position);
          this.rebuildScene();
        }
      }
    });

    this.scene.add(this.networkGroup);
    this.scene.add(this.propGroup);
    this.scene.add(this.trainGroup);
    this.scene.add(this.trafficSystem.group);

    // 6. Lighting
    this.setupLighting();

    // 7. Ground Plane (neutral dark graphite grid)
    this.setupGround();

    this.ui.setBackendStatus(this.isWebGPU);

    // 9. Load Default Scenario (Metropolitan Interchange)
    this.loadScenario('metropolitan');

    // 10. Raycasting selection
    this.setupRaycasting(canvas);

    // 11. Resize Handler
    window.addEventListener('resize', () => this.onWindowResize());

    // 12. Animation Loop
    this.animate();
  }

  private setupLighting() {
    this.ambientLight = new THREE.AmbientLight(0xffffff, 0.45);
    this.scene.add(this.ambientLight);

    this.hemiLight = new THREE.HemisphereLight(0xe8ecf5, 0x1f2329, 0.65);
    this.hemiLight.position.set(0, 50, 0);
    this.scene.add(this.hemiLight);

    this.dirLight = new THREE.DirectionalLight(0xfff5ea, 1.85);
    this.dirLight.position.set(120, 160, 90);
    this.dirLight.castShadow = true;
    this.dirLight.shadow.mapSize.width = 4096;
    this.dirLight.shadow.mapSize.height = 4096;
    this.dirLight.shadow.camera.near = 10;
    this.dirLight.shadow.camera.far = 450;
    const d = 160;
    this.dirLight.shadow.camera.left = -d;
    this.dirLight.shadow.camera.right = d;
    this.dirLight.shadow.camera.top = d;
    this.dirLight.shadow.camera.bottom = -d;
    this.dirLight.shadow.bias = -0.00015;
    this.scene.add(this.dirLight);
  }

  private setupGround() {
    // Large ground plane
    const groundGeom = new THREE.PlaneGeometry(1600, 1600);
    groundGeom.rotateX(-Math.PI * 0.5);
    const groundMat = new THREE.MeshStandardMaterial({
      color: 0x121316,
      roughness: 0.98,
      metalness: 0.02
    });
    const ground = new THREE.Mesh(groundGeom, groundMat);
    ground.position.y = -0.02;
    ground.receiveShadow = true;
    this.scene.add(ground);

    // Subtle coordinate grid
    const grid = new THREE.GridHelper(800, 80, 0x27272a, 0x1a1a1c);
    grid.position.y = -0.01;
    this.scene.add(grid);
  }

  private setLightingMode(isNight: boolean) {
    if (isNight) {
      this.scene.background = new THREE.Color(0x06070a);
      this.scene.fog = new THREE.FogExp2(0x06070a, 0.0035);
      this.dirLight.intensity = 0.15;
      this.dirLight.color.setHex(0x556688);
      this.hemiLight.intensity = 0.2;
      this.ambientLight.intensity = 0.1;
    } else {
      this.scene.background = new THREE.Color(0x0e0f12);
      this.scene.fog = new THREE.FogExp2(0x0e0f12, 0.0028);
      this.dirLight.intensity = 1.85;
      this.dirLight.color.setHex(0xfff5ea);
      this.hemiLight.intensity = 0.65;
      this.ambientLight.intensity = 0.45;
    }
    this.trafficSystem.setNightMode(isNight);
    this.rebuildScene();
  }

  private setWireframe(enabled: boolean) {
    this.networkGroup.traverse((obj) => {
      if (obj instanceof THREE.LineSegments && obj.material === this.materials.wireframeMaterial) {
        obj.visible = enabled;
      }
    });
  }

  private handleToolChange(toolId: string) {
    if (toolId === 'select') {
      this.transformControls.enabled = true;
      if (this.ui.selectedEntity.id) {
        this.transformControls.attach(this.gizmoTarget);
      }
    } else {
      this.transformControls.detach();
      this.transformControls.enabled = false;
    }
  }

  private setCameraPreset(preset: 'perspective' | 'top' | 'bridge' | 'intersection' | 'train') {
    if (preset === 'perspective') {
      this.camera.position.set(65, 45, 85);
      this.controls.target.set(0, 0, 0);
    } else if (preset === 'top') {
      this.camera.position.set(0, 160, 0.1);
      this.controls.target.set(0, 0, 0);
    } else if (preset === 'bridge') {
      this.camera.position.set(25, 14, 45);
      this.controls.target.set(0, 7.2, 0);
    } else if (preset === 'intersection') {
      this.camera.position.set(-20, 12, 25);
      this.controls.target.set(0, 0, 0);
    } else if (preset === 'train') {
      this.camera.position.set(-45, 8, -25);
      this.controls.target.set(-20, 1, 0);
    }
    this.controls.update();
  }

  /**
   * Click-to-select in 3D viewport
   */
  private setupRaycasting(canvas: HTMLCanvasElement) {
    const raycaster = new THREE.Raycaster();
    const mouse = new THREE.Vector2();

    canvas.addEventListener('pointerdown', (e) => {
      // Ignore if clicking gizmo
      if (this.transformControls.dragging) return;

      const rect = canvas.getBoundingClientRect();
      mouse.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
      mouse.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;

      raycaster.setFromCamera(mouse, this.camera);

      // Check intersections
      for (const node of this.activeIntersections) {
        const dist = raycaster.ray.distanceToPoint(node.center);
        if (dist < 4.5) {
          this.gizmoTarget.position.copy(node.center);
          this.transformControls.attach(this.gizmoTarget);
          this.ui.selectEntity('intersection', node.id);
          return;
        }
      }

      // Check road ribbons
      const intersects = raycaster.intersectObjects(this.networkGroup.children, true);
      if (intersects.length > 0) {
        let hit = intersects[0].object;
        while (hit.parent && hit.parent !== this.networkGroup) {
          hit = hit.parent;
        }
        const name = hit.name;
        if (name.startsWith('road_')) {
          const roadId = name.replace('road_', '');
          this.ui.selectEntity('road', roadId);
          this.transformControls.detach();
          return;
        } else if (name.startsWith('intersection_')) {
          const intId = name.replace('intersection_', '');
          const node = this.activeIntersections.find(n => n.id === intId);
          if (node) {
            this.gizmoTarget.position.copy(node.center);
            this.transformControls.attach(this.gizmoTarget);
            this.ui.selectEntity('intersection', intId);
            return;
          }
        }
      }

      // Deselect if clicking empty space
      if (e.button === 0) {
        this.transformControls.detach();
        this.ui.selectEntity(null, null);
      }
    });
  }

  // --- SCENARIO GENERATORS ---

  public loadScenario(scenarioId: string) {
    this.networkGroup.clear();
    this.propGroup.clear();
    this.trainGroup.clear();
    this.trafficSystem.clear();
    this.transformControls.detach();

    this.activeSplines = [];
    this.activeRoadConfigs = [];
    this.activeIntersections = [];
    this.railwaySpline = null;

    if (scenarioId === 'metropolitan') {
      this.buildMetropolitanScenario();
    } else if (scenarioId === 'railway_crossing') {
      this.buildRailwayCrossingScenario();
    } else if (scenarioId === 'construction_zone') {
      this.buildConstructionScenario();
    } else if (scenarioId === 'pedestrian_plaza') {
      this.buildPedestrianPlazaScenario();
    } else if (scenarioId === 'acute_y_junction') {
      this.buildAcuteYJunctionScenario();
    }

    this.rebuildScene();
    this.trafficSystem.setSplines(this.activeSplines);
    this.trafficSystem.initFleet(18);
  }

  /**
   * Scenario 1: Metropolitan Diamond Interchange & Highway Overpass Bridge
   */
  private buildMetropolitanScenario() {
    // 1. Lower Level Arterial Avenue (East-West)
    const splineAvenue = new RoadSpline([
      new THREE.Vector3(-140, 0, 0),
      new THREE.Vector3(-60, 0, 0),
      new THREE.Vector3(0, 0, 0),
      new THREE.Vector3(60, 0, 0),
      new THREE.Vector3(140, 0, 0)
    ]);
    const configAvenue: RoadConfig = {
      id: 'avenue_main',
      name: 'Grand Boulevard (4-Lane)',
      lanes: 4,
      laneWidth: 3.5,
      sidewalkLeft: true,
      sidewalkRight: true,
      sidewalkWidth: 3.5,
      curbHeight: 0.15,
      curbWidth: 0.20,
      crossfall: 0.02,
      speedLimit: 60,
      isHighway: false,
      isBridge: false,
      isUnderpass: false
    };

    // 2. Upper Level Elevated Highway Bridge (North-South, 7.2m elevation, 5.5m minimum clearance)
    const splineHighway = new RoadSpline([
      new THREE.Vector3(0, 0, -150),
      new THREE.Vector3(0, 4.5, -80),
      new THREE.Vector3(0, 7.2, -30),
      new THREE.Vector3(0, 7.2, 0),
      new THREE.Vector3(0, 7.2, 30),
      new THREE.Vector3(0, 4.5, 80),
      new THREE.Vector3(0, 0, 150)
    ]);
    const configHighway: RoadConfig = {
      id: 'highway_express',
      name: 'North-South Freeway (4-Lane Viaduct)',
      lanes: 4,
      laneWidth: 3.65,
      sidewalkLeft: false,
      sidewalkRight: false,
      sidewalkWidth: 0,
      curbHeight: 0,
      curbWidth: 0,
      crossfall: 0.02,
      speedLimit: 100,
      isHighway: true,
      isBridge: true,
      bridgeHeight: 7.2,
      isUnderpass: false
    };

    // 3. Four Directional Diamond Interchange Ramps
    const rampNW = new RoadSpline([
      new THREE.Vector3(-8, 7.2, -45),
      new THREE.Vector3(-25, 4.5, -35),
      new THREE.Vector3(-45, 1.5, -20),
      new THREE.Vector3(-45, 0, 0)
    ]);
    const rampNE = new RoadSpline([
      new THREE.Vector3(8, 7.2, -45),
      new THREE.Vector3(25, 4.5, -35),
      new THREE.Vector3(45, 1.5, -20),
      new THREE.Vector3(45, 0, 0)
    ]);
    const rampSW = new RoadSpline([
      new THREE.Vector3(-8, 7.2, 45),
      new THREE.Vector3(-25, 4.5, 35),
      new THREE.Vector3(-45, 1.5, 20),
      new THREE.Vector3(-45, 0, 0)
    ]);
    const rampSE = new RoadSpline([
      new THREE.Vector3(8, 7.2, 45),
      new THREE.Vector3(25, 4.5, 35),
      new THREE.Vector3(45, 1.5, 20),
      new THREE.Vector3(45, 0, 0)
    ]);

    const configRamp = (id: string, name: string): RoadConfig => ({
      id,
      name,
      lanes: 1,
      laneWidth: 4.2,
      sidewalkLeft: false,
      sidewalkRight: false,
      sidewalkWidth: 0,
      curbHeight: 0.12,
      curbWidth: 0.18,
      crossfall: 0.03,
      speedLimit: 50,
      isHighway: true,
      isBridge: false,
      isUnderpass: false
    });

    this.activeSplines = [splineAvenue, splineHighway, rampNW, rampNE, rampSW, rampSE];
    this.activeRoadConfigs = [
      configAvenue,
      configHighway,
      configRamp('ramp_nw', 'Ramp NW Exit'),
      configRamp('ramp_ne', 'Ramp NE Entry'),
      configRamp('ramp_sw', 'Ramp SW Entry'),
      configRamp('ramp_se', 'Ramp SE Exit')
    ];

    // Intersections where ramps meet avenue (clean merged 4-way junctions)
    const nodeWest: IntersectionData = {
      id: 'node_west',
      name: 'West Ramp Junction',
      center: new THREE.Vector3(-45, 0, 0),
      type: 'plus',
      cornerRadius: 8.5,
      arms: [
        {
          roadId: 'avenue_main',
          spline: splineAvenue,
          isStart: true,
          lanes: 4,
          laneWidth: 3.5,
          sidewalkWidth: 3.5,
          direction: new THREE.Vector3(-1, 0, 0),
          angle: Math.PI,
          setback: 10,
          mouthPoints: []
        },
        {
          roadId: 'avenue_main',
          spline: splineAvenue,
          isStart: false,
          lanes: 4,
          laneWidth: 3.5,
          sidewalkWidth: 3.5,
          direction: new THREE.Vector3(1, 0, 0),
          angle: 0,
          setback: 10,
          mouthPoints: []
        },
        {
          roadId: 'ramp_nw',
          spline: rampNW,
          isStart: false,
          lanes: 1,
          laneWidth: 4.2,
          sidewalkWidth: 0,
          direction: new THREE.Vector3(0, 0, -1),
          angle: -Math.PI * 0.5,
          setback: 8,
          mouthPoints: []
        },
        {
          roadId: 'ramp_sw',
          spline: rampSW,
          isStart: false,
          lanes: 1,
          laneWidth: 4.2,
          sidewalkWidth: 0,
          direction: new THREE.Vector3(0, 0, 1),
          angle: Math.PI * 0.5,
          setback: 8,
          mouthPoints: []
        }
      ]
    };

    const nodeEast: IntersectionData = {
      id: 'node_east',
      name: 'East Ramp Junction',
      center: new THREE.Vector3(45, 0, 0),
      type: 'plus',
      cornerRadius: 8.5,
      arms: [
        {
          roadId: 'avenue_main',
          spline: splineAvenue,
          isStart: true,
          lanes: 4,
          laneWidth: 3.5,
          sidewalkWidth: 3.5,
          direction: new THREE.Vector3(-1, 0, 0),
          angle: Math.PI,
          setback: 10,
          mouthPoints: []
        },
        {
          roadId: 'avenue_main',
          spline: splineAvenue,
          isStart: false,
          lanes: 4,
          laneWidth: 3.5,
          sidewalkWidth: 3.5,
          direction: new THREE.Vector3(1, 0, 0),
          angle: 0,
          setback: 10,
          mouthPoints: []
        },
        {
          roadId: 'ramp_ne',
          spline: rampNE,
          isStart: false,
          lanes: 1,
          laneWidth: 4.2,
          sidewalkWidth: 0,
          direction: new THREE.Vector3(0, 0, -1),
          angle: -Math.PI * 0.5,
          setback: 8,
          mouthPoints: []
        },
        {
          roadId: 'ramp_se',
          spline: rampSE,
          isStart: false,
          lanes: 1,
          laneWidth: 4.2,
          sidewalkWidth: 0,
          direction: new THREE.Vector3(0, 0, 1),
          angle: Math.PI * 0.5,
          setback: 8,
          mouthPoints: []
        }
      ]
    };

    this.activeIntersections = [nodeWest, nodeEast];

    // Bridge Structure for Elevated Highway
    const bridgeConfig: BridgeConfig = {
      id: 'freeway_overpass',
      name: 'Freeway Overpass Viaduct',
      startDistance: 60,
      endDistance: 240,
      deckWidth: 15.5,
      elevation: 7.2,
      minClearance: 5.5, // Reference-scaled AASHTO standard
      girderType: 'steel_i_beam',
      pierSpacing: 28,
      hasParapet: true,
      hasRailing: true
    };
    const bridgeGroup = BridgeGenerator.buildBridge(splineHighway, bridgeConfig, this.materials, [new THREE.Vector3(0, 0, 0)]);
    this.propGroup.add(bridgeGroup);

    // Highway Overhead Gantry Signs
    const gantry = FurnitureBuilder.buildHighwayGantry(16.0);
    gantry.position.set(0, 7.2, -65);
    this.propGroup.add(gantry);

    // Streetlights along Avenue
    for (const lx of [-90, -20, 20, 90]) {
      for (const sign of [-1, 1]) {
        const light = FurnitureBuilder.buildStreetlight('cobra_head', this.materials, this.ui.isNight);
        light.position.set(lx, 0, sign * 11);
        light.rotation.y = sign > 0 ? 0 : Math.PI;
        this.propGroup.add(light);
      }
    }
  }

  /**
   * Scenario 2: Urban Boulevard & At-Grade Railroad Crossing with Animated Train
   */
  private buildRailwayCrossingScenario() {
    // 1. City Boulevard (North-South)
    const splineBoulevard = new RoadSpline([
      new THREE.Vector3(0, 0, -120),
      new THREE.Vector3(0, 0, -50),
      new THREE.Vector3(0, 0, 0),
      new THREE.Vector3(0, 0, 50),
      new THREE.Vector3(0, 0, 120)
    ]);
    const configBoulevard: RoadConfig = {
      id: 'city_blvd',
      name: 'Downtown Commercial Boulevard',
      lanes: 4,
      laneWidth: 3.4,
      sidewalkLeft: true,
      sidewalkRight: true,
      sidewalkWidth: 4.0,
      curbHeight: 0.15,
      curbWidth: 0.20,
      crossfall: 0.02,
      speedLimit: 50,
      isHighway: false,
      isBridge: false,
      isUnderpass: false
    };

    // 2. Double-Track Railway Line (East-West)
    this.railwaySpline = new RoadSpline([
      new THREE.Vector3(-140, 0, 0),
      new THREE.Vector3(-60, 0, 0),
      new THREE.Vector3(0, 0, 0),
      new THREE.Vector3(60, 0, 0),
      new THREE.Vector3(140, 0, 0)
    ]);
    const railConfig: RailwayConfig = {
      id: 'freight_commuter_line',
      name: 'Double-Track Regional Line',
      gauge: 1.435,
      sleeperSpacing: 0.60,
      ballastWidth: 4.4,
      ballastHeight: 0.35
    };

    this.activeSplines = [splineBoulevard];
    this.activeRoadConfigs = [configBoulevard];

    // Build Railway Track
    const railGroup = RailwayBuilder.buildRailway(this.railwaySpline, railConfig, this.materials);
    this.propGroup.add(railGroup);

    // Build Grade Crossing Panels & Signal Masts
    const crossing = RailwayBuilder.buildCrossing(
      new THREE.Vector3(0, 0, 0),
      new THREE.Vector3(1, 0, 0),
      15.0,
      this.materials
    );
    this.propGroup.add(crossing);

    // Build Animated Commuter Train
    const train = RailwayBuilder.buildCommuterTrain();
    this.trainGroup.add(train);

    // Speed Bumps & Potholes along boulevard
    const speedBump = DamageConstructionBuilder.buildSpeedBump(
      splineBoulevard,
      35,
      'asphalt_hump',
      13.6,
      this.materials
    );
    this.propGroup.add(speedBump);

    const pothole = DamageConstructionBuilder.buildPothole(new THREE.Vector3(3.2, 0, -35), 0.7);
    this.propGroup.add(pothole);

    // Autonomous Sidewalk Delivery Robot roaming sidewalk
    const robot = FurnitureBuilder.buildDeliveryRobot();
    robot.position.set(-8.8, 0.15, 20);
    this.propGroup.add(robot);

    // Streetlights (Historic Victorian Lanterns)
    for (const lz of [-80, -40, 40, 80]) {
      for (const sign of [-1, 1]) {
        const light = FurnitureBuilder.buildStreetlight('victorian', this.materials, this.ui.isNight);
        light.position.set(sign * 9.2, 0, lz);
        this.propGroup.add(light);
      }
    }
  }

  /**
   * Scenario 3: Highway Viaduct & Active Construction Zone
   */
  private buildConstructionScenario() {
    // 1. Lower Roadway with active resurfacing / reconstruction zone
    const splineRoad = new RoadSpline([
      new THREE.Vector3(-120, 0, 0),
      new THREE.Vector3(-50, 0, 0),
      new THREE.Vector3(0, 0, 0),
      new THREE.Vector3(50, 0, 0),
      new THREE.Vector3(120, 0, 0)
    ]);
    const configRoad: RoadConfig = {
      id: 'road_resurfacing',
      name: 'County Arterial (Under Construction)',
      lanes: 4,
      laneWidth: 3.5,
      sidewalkLeft: false,
      sidewalkRight: false,
      sidewalkWidth: 0,
      curbHeight: 0,
      curbWidth: 0,
      crossfall: 0.02,
      speedLimit: 40,
      isHighway: false,
      isBridge: false,
      isUnderpass: false
    };

    // 2. High-Level Multi-Span Viaduct Crossing Above
    const splineViaduct = new RoadSpline([
      new THREE.Vector3(0, 8.5, -120),
      new THREE.Vector3(0, 8.5, -40),
      new THREE.Vector3(0, 8.5, 0),
      new THREE.Vector3(0, 8.5, 40),
      new THREE.Vector3(0, 8.5, 120)
    ]);
    const configViaduct: RoadConfig = {
      id: 'high_viaduct',
      name: 'High-Level Expressway Viaduct',
      lanes: 4,
      laneWidth: 3.65,
      sidewalkLeft: false,
      sidewalkRight: false,
      sidewalkWidth: 0,
      curbHeight: 0,
      curbWidth: 0,
      crossfall: 0.02,
      speedLimit: 100,
      isHighway: true,
      isBridge: true,
      bridgeHeight: 8.5,
      isUnderpass: false
    };

    this.activeSplines = [splineRoad, splineViaduct];
    this.activeRoadConfigs = [configRoad, configViaduct];

    // Viaduct Bridge Structure
    const bridgeConfig: BridgeConfig = {
      id: 'high_viaduct_struct',
      name: 'Concrete Box Girder Viaduct',
      startDistance: 10,
      endDistance: 230,
      deckWidth: 16.0,
      elevation: 8.5,
      minClearance: 6.2,
      girderType: 'concrete_box',
      pierSpacing: 35,
      hasParapet: true,
      hasRailing: true
    };
    const bridge = BridgeGenerator.buildBridge(splineViaduct, bridgeConfig, this.materials, [new THREE.Vector3(0, 0, 0)]);
    this.propGroup.add(bridge);

    // Active Construction Zone: Milled asphalt, cones, barriers, flashing arrow trailer
    const workZone = DamageConstructionBuilder.buildConstructionZone(splineRoad, 70, 150, 14.0, this.materials);
    this.propGroup.add(workZone);
  }

  /**
   * Scenario 4: Pedestrian City Block & Paved Plazas (No Buildings!)
   */
  private buildPedestrianPlazaScenario() {
    // 4 Roads forming an urban perimeter block
    const r1 = new RoadSpline([new THREE.Vector3(-45, 0, -45), new THREE.Vector3(45, 0, -45)]);
    const r2 = new RoadSpline([new THREE.Vector3(45, 0, -45), new THREE.Vector3(45, 0, 45)]);
    const r3 = new RoadSpline([new THREE.Vector3(45, 0, 45), new THREE.Vector3(-45, 0, 45)]);
    const r4 = new RoadSpline([new THREE.Vector3(-45, 0, 45), new THREE.Vector3(-45, 0, -45)]);

    const blockConfig = (id: string, name: string): RoadConfig => ({
      id,
      name,
      lanes: 2,
      laneWidth: 3.2,
      sidewalkLeft: true,
      sidewalkRight: true,
      sidewalkWidth: 3.5,
      curbHeight: 0.15,
      curbWidth: 0.20,
      crossfall: 0.02,
      speedLimit: 30,
      isHighway: false,
      isBridge: false,
      isUnderpass: false
    });

    this.activeSplines = [r1, r2, r3, r4];
    this.activeRoadConfigs = [
      blockConfig('street_n', 'North Promenade'),
      blockConfig('street_e', 'East Arcade'),
      blockConfig('street_s', 'South Avenue'),
      blockConfig('street_w', 'West Street')
    ];

    // Central Paved Pedestrian Plaza with tree grates (no buildings!)
    const plaza = FurnitureBuilder.buildPavedCityBlock(70, 70, this.materials);
    plaza.position.set(0, 0, 0);
    this.propGroup.add(plaza);

    // Architectural Bollards around perimeter
    for (let x = -32; x <= 32; x += 8) {
      for (const z of [-32, 32]) {
        const bollard = FurnitureBuilder.buildBollard('illuminated_led');
        bollard.position.set(x, 0.15, z);
        this.propGroup.add(bollard);
      }
    }
  }

  /**
   * Scenario 5: Complex Acute Y-Junction (Quad Stress Test)
   */
  private buildAcuteYJunctionScenario() {
    // 3 roads meeting at sharp 32 degree angle to prove acute angle quad solver
    const mainStem = new RoadSpline([
      new THREE.Vector3(0, 0, -90),
      new THREE.Vector3(0, 0, -40),
      new THREE.Vector3(0, 0, 0)
    ]);
    const branchLeft = new RoadSpline([
      new THREE.Vector3(0, 0, 0),
      new THREE.Vector3(-25, 0, 50),
      new THREE.Vector3(-50, 0, 95)
    ]);
    const branchRight = new RoadSpline([
      new THREE.Vector3(0, 0, 0),
      new THREE.Vector3(25, 0, 50),
      new THREE.Vector3(50, 0, 95)
    ]);

    const yConfig = (id: string, name: string): RoadConfig => ({
      id,
      name,
      lanes: 2,
      laneWidth: 3.5,
      sidewalkLeft: true,
      sidewalkRight: true,
      sidewalkWidth: 2.8,
      curbHeight: 0.15,
      curbWidth: 0.20,
      crossfall: 0.02,
      speedLimit: 60,
      isHighway: false,
      isBridge: false,
      isUnderpass: false
    });

    this.activeSplines = [mainStem, branchLeft, branchRight];
    this.activeRoadConfigs = [
      yConfig('stem', 'Main Approach Stem'),
      yConfig('branch_l', 'Northwest Branch (32° Acute Fork)'),
      yConfig('branch_r', 'Northeast Branch (32° Acute Fork)')
    ];

    const acuteYNode: IntersectionData = {
      id: 'acute_y_center',
      name: 'High-Speed Fork (32° Acute Nose)',
      center: new THREE.Vector3(0, 0, 0),
      type: 'y',
      cornerRadius: 6.0,
      arms: [
        {
          roadId: 'stem',
          spline: mainStem,
          isStart: false,
          lanes: 2,
          laneWidth: 3.5,
          sidewalkWidth: 2.8,
          direction: new THREE.Vector3(0, 0, -1),
          angle: -Math.PI * 0.5,
          setback: 10,
          mouthPoints: []
        },
        {
          roadId: 'branch_r',
          spline: branchRight,
          isStart: true,
          lanes: 2,
          laneWidth: 3.5,
          sidewalkWidth: 2.8,
          direction: new THREE.Vector3(0.46, 0, 0.88).normalize(),
          angle: Math.atan2(0.88, 0.46),
          setback: 12,
          mouthPoints: []
        },
        {
          roadId: 'branch_l',
          spline: branchLeft,
          isStart: true,
          lanes: 2,
          laneWidth: 3.5,
          sidewalkWidth: 2.8,
          direction: new THREE.Vector3(-0.46, 0, 0.88).normalize(),
          angle: Math.atan2(0.88, -0.46),
          setback: 12,
          mouthPoints: []
        }
      ]
    };

    this.activeIntersections = [acuteYNode];
  }

  /**
   * Rebuilds all road meshes, intersections, and markings cleanly with pure quads
   */
  private rebuildScene() {
    this.networkGroup.clear();
    this.totalQuads = 0;
    this.totalTriangles = 0;

    // 1. Build Merged Intersections
    for (const node of this.activeIntersections) {
      const result = IntersectionSolver.buildIntersection(node, this.materials);
      this.networkGroup.add(result.group);
      this.totalQuads += result.totalQuads;
      this.totalTriangles += result.totalTriangles;
    }

    // 2. Build Road Ribbons
    for (let i = 0; i < this.activeSplines.length; i++) {
      const spline = this.activeSplines[i];
      const config = this.activeRoadConfigs[i];

      let startDist = 0;
      let endDist = spline.totalLength;

      // Adjust for intersection setbacks
      for (const node of this.activeIntersections) {
        for (const arm of node.arms) {
          if (arm.roadId === config.id) {
            if (arm.isStart) {
              startDist = Math.max(startDist, arm.setback);
            } else {
              endDist = Math.min(endDist, spline.totalLength - arm.setback);
            }
          }
        }
      }

      const roadMeshResult = RoadTopologyBuilder.buildRoadSegment(
        spline,
        config,
        this.materials,
        startDist,
        endDist,
        [{ distance: spline.totalLength * 0.5, width: 3.0 }]
      );
      this.networkGroup.add(roadMeshResult.group);
      this.totalQuads += roadMeshResult.totalQuads;
      this.totalTriangles += roadMeshResult.totalTriangles;

      // Road Markings
      const markings = MarkingGenerator.buildMarkings(
        spline,
        {
          hasCenterline: true,
          centerlineType: config.isHighway ? 'dashed_white' : 'double_yellow',
          hasLaneDividers: config.lanes > 2,
          hasEdgeLines: true,
          laneCount: config.lanes,
          laneWidth: config.laneWidth,
          bikeLane: config.lanes === 4 && !config.isHighway
        },
        this.materials,
        startDist,
        endDist
      );
      this.networkGroup.add(markings);
    }

    // Update UI Outliner
    this.ui.updateOutliner(
      this.activeRoadConfigs,
      this.activeIntersections,
      this.activeRoadConfigs.some(r => r.isBridge),
      false,
      this.railwaySpline !== null,
      this.propGroup.children.some(c => c.name === 'construction_zone')
    );

    // Apply Wireframe setting
    this.setWireframe(this.ui.wireframeEnabled);
  }

  private handleRoadPropChange(roadId: string, prop: string, val: any) {
    const config = this.activeRoadConfigs.find(r => r.id === roadId);
    if (config) {
      (config as any)[prop] = val;
      this.rebuildScene();
    }
  }

  private handleIntersectionPropChange(nodeId: string, prop: string, val: any) {
    const node = this.activeIntersections.find(n => n.id === nodeId);
    if (node) {
      (node as any)[prop] = val;
      this.rebuildScene();
    }
  }

  private exportCurrentProject() {
    const data = {
      version: '1.0.0',
      roads: this.activeRoadConfigs,
      intersections: this.activeIntersections.map(n => ({
        id: n.id,
        name: n.name,
        type: n.type,
        center: [n.center.x, n.center.y, n.center.z],
        cornerRadius: n.cornerRadius
      }))
    };
    SceneExporter.exportJSON(data, 'Frontier_RoadProject.json');
  }

  private onWindowResize() {
    const canvas = document.getElementById('viewport-canvas') as HTMLCanvasElement;
    if (!canvas) return;
    this.camera.aspect = canvas.clientWidth / canvas.clientHeight;
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(canvas.clientWidth, canvas.clientHeight);
  }

  // --- ANIMATION LOOP ---
  private animate = () => {
    requestAnimationFrame(this.animate);

    const now = performance.now();
    const delta = (now - this.lastTime) * 0.001;
    this.lastTime = now;

    // FPS Telemetry
    this.frameCount++;
    if (this.frameCount % 15 === 0) {
      this.currentFPS = 1.0 / Math.max(0.001, delta);
      this.ui.updateTelemetry(this.currentFPS, this.totalQuads, this.totalTriangles);
    }

    // 1. Update AI Traffic System (capped <= 25 vehicles)
    this.trafficSystem.update(delta);

    // 2. Animate Commuter Train along Railway track
    if (this.railwaySpline && this.trainGroup.children.length > 0) {
      const trainSpeed = 16.0; // m/s (~58 km/h)
      this.trainProgress = (this.trainProgress + trainSpeed * delta) % this.railwaySpline.totalLength;
      const sp = this.railwaySpline.getSampleAtDistance(this.trainProgress);
      const train = this.trainGroup.children[0];
      train.position.copy(sp.position).addScaledVector(sp.binormal!, 0.35);
      train.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), sp.tangent!);
    }

    // 3. Orbit Controls Update
    this.controls.update();

    // 4. Render
    this.renderer.render(this.scene, this.camera);
  };
}

// Bootstrap Application on DOM Ready
window.addEventListener('DOMContentLoaded', () => {
  new App();
});
