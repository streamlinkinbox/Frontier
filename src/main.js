import './styles.css';
import * as THREE from 'three';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';

const $ = (selector) => document.querySelector(selector);
const app = $('#app');
const mount = $('#webgl');
const loading = $('#loading');
const speedValue = $('#speed-value');
const timerValue = $('#timer-value');
const lapValue = $('#lap-value');
const boostFill = $('#boost-fill');
const countdown = $('#countdown');
const raceMessage = $('#race-message');
const mapPath = $('#map-path');
const mapDot = $('#map-dot');
const hudMapDot = $('#hud-map-dot');

const clamp = THREE.MathUtils.clamp;
const damp = THREE.MathUtils.damp;
const TAU = Math.PI * 2;

class CrucibleArena {
  constructor(container) {
    this.container = container;
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x020609);
    this.scene.fog = new THREE.FogExp2(0x061016, 0.0082);

    this.clock = new THREE.Clock();
    this.elapsed = 0;
    this.state = 'briefing';
    this.keys = Object.create(null);
    this.progress = 0.025;
    this.previousProgress = this.progress;
    this.lap = 1;
    this.raceTime = 0;
    this.speed = 0;
    this.lateral = 0;
    this.lateralVelocity = 0;
    this.boost = 1;
    this.cameraMode = 0;
    this.tourTime = 0;
    this.lastMessageAt = -10;
    this.audioEnabled = false;
    this.audio = null;
    this.frameSide = new THREE.Vector3();
    this.framePoint = new THREE.Vector3();
    this.frameTangent = new THREE.Vector3();
    this._dummy = new THREE.Object3D();

    this.trackHalfWidth = 16.5;
    this.wallHeight = 9.4;
    this.wallPower = 2.42;

    this.initRenderer();
    this.initScene();
    this.bindUI();
    this.resize();
    this.renderOnce();
    this.animate = this.animate.bind(this);
    requestAnimationFrame(this.animate);
  }

  initRenderer() {
    this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.75));
    this.renderer.setSize(window.innerWidth, window.innerHeight);
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 0.94;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.container.appendChild(this.renderer.domElement);

    this.camera = new THREE.PerspectiveCamera(68, 1, 0.08, 420);
    this.camera.position.set(0, 5, 12);

    const pmrem = new THREE.PMREMGenerator(this.renderer);
    const environment = new RoomEnvironment();
    this.scene.environment = pmrem.fromScene(environment, 0.025).texture;
    environment.dispose();
    pmrem.dispose();

    this.composer = new EffectComposer(this.renderer);
    this.composer.addPass(new RenderPass(this.scene, this.camera));
    this.bloom = new UnrealBloomPass(new THREE.Vector2(1, 1), 0.42, 0.55, 0.72);
    this.composer.addPass(this.bloom);
    this.composer.addPass(new OutputPass());
  }

  initScene() {
    this.buildLights();
    this.buildTrack();
    this.buildStadium();
    this.buildStartGantry();
    this.buildAtmosphere();
    this.buildRacers();

    const initial = this.trackFrame(this.progress, this.lateral);
    this.placeRacer(this.player, initial, 0);
    this.camera.position.copy(initial.point).addScaledVector(initial.tangent, -10).add(new THREE.Vector3(0, 4.8, 0));
    this.camera.lookAt(initial.point.clone().addScaledVector(initial.tangent, 16).add(new THREE.Vector3(0, 1.3, 0)));
  }

  buildLights() {
    const hemi = new THREE.HemisphereLight(0xcaf4ff, 0x030506, 1.5);
    this.scene.add(hemi);

    const key = new THREE.DirectionalLight(0xd8f7ff, 3.8);
    key.position.set(-48, 64, 28);
    key.castShadow = true;
    key.shadow.mapSize.set(2048, 2048);
    key.shadow.camera.left = -105;
    key.shadow.camera.right = 105;
    key.shadow.camera.top = 105;
    key.shadow.camera.bottom = -105;
    key.shadow.camera.near = 15;
    key.shadow.camera.far = 180;
    key.shadow.bias = -0.00035;
    this.scene.add(key);

    const coldFill = new THREE.DirectionalLight(0x5ecfff, 1.4);
    coldFill.position.set(68, 26, -60);
    this.scene.add(coldFill);

    const warmFill = new THREE.PointLight(0xff9d35, 35, 70, 2);
    warmFill.position.set(-50, 9, 34);
    this.scene.add(warmFill);

    const lightPositions = [
      [-80, 31, -54], [-22, 34, -105], [55, 32, -88], [98, 31, -20],
      [83, 30, 62], [18, 34, 103], [-66, 31, 80], [-104, 31, 12]
    ];
    this.arenaSpots = [];
    lightPositions.forEach(([x, y, z], index) => {
      const spot = new THREE.SpotLight(index % 3 === 0 ? 0xb9ebff : 0xe9fbff, 1150, 165, 0.48, 0.78, 1.35);
      spot.position.set(x, y, z);
      spot.target.position.set(x * 0.16, 0, z * 0.16);
      this.scene.add(spot, spot.target);
      this.arenaSpots.push(spot);
    });
  }

  makeSteelTexture() {
    const size = 1024;
    const canvas = document.createElement('canvas');
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext('2d');
    const gradient = ctx.createLinearGradient(0, 0, size, size);
    gradient.addColorStop(0, '#5d7077');
    gradient.addColorStop(.42, '#35484f');
    gradient.addColorStop(.72, '#66777c');
    gradient.addColorStop(1, '#2f4148');
    ctx.fillStyle = gradient;
    ctx.fillRect(0, 0, size, size);

    let seed = 918273;
    const random = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);

    ctx.globalAlpha = .15;
    for (let i = 0; i < 18000; i++) {
      const shade = Math.floor(60 + random() * 110);
      ctx.fillStyle = `rgb(${shade},${shade + 8},${shade + 11})`;
      const x = random() * size;
      const y = random() * size;
      ctx.fillRect(x, y, random() * 2 + .25, random() * 11 + 1);
    }

    ctx.globalAlpha = .48;
    ctx.strokeStyle = '#17282f';
    ctx.lineWidth = 3;
    for (let x = 0; x <= size; x += 128) {
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, size); ctx.stroke();
      ctx.strokeStyle = 'rgba(210,229,232,.22)';
      ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(x + 4, 0); ctx.lineTo(x + 4, size); ctx.stroke();
      ctx.strokeStyle = '#17282f'; ctx.lineWidth = 3;
    }
    for (let y = 0; y <= size; y += 256) {
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(size, y); ctx.stroke();
      for (let x = 48; x < size; x += 128) {
        ctx.fillStyle = '#13252c'; ctx.beginPath(); ctx.arc(x, y + 5, 3, 0, TAU); ctx.fill();
      }
    }

    ctx.globalAlpha = .55;
    ctx.lineCap = 'round';
    for (let i = 0; i < 34; i++) {
      const x = random() * size;
      const y = random() * size;
      ctx.strokeStyle = random() > .5 ? 'rgba(8,18,22,.72)' : 'rgba(202,220,224,.24)';
      ctx.lineWidth = random() * 3 + .5;
      ctx.beginPath(); ctx.moveTo(x, y); ctx.bezierCurveTo(x + random()*90, y + random()*30 - 15, x + random()*170, y + random()*70 - 35, x + random()*260, y + random()*100 - 50); ctx.stroke();
    }

    // Broad rubber trails, like the looping black marks in the references.
    ctx.globalAlpha = .3;
    ctx.strokeStyle = '#061116';
    for (let i = 0; i < 7; i++) {
      ctx.lineWidth = 5 + random() * 8;
      ctx.beginPath();
      const y = 280 + i * 41 + random() * 30;
      ctx.moveTo(-30, y);
      ctx.bezierCurveTo(220, y - 130, 620, y + 160, 1080, y - 30);
      ctx.stroke();
    }

    ctx.globalAlpha = 1;
    const texture = new THREE.CanvasTexture(canvas);
    texture.colorSpace = THREE.SRGBColorSpace;
    texture.wrapS = texture.wrapT = THREE.RepeatWrapping;
    texture.anisotropy = this.renderer.capabilities.getMaxAnisotropy();
    return texture;
  }

  makeHazardTexture() {
    const canvas = document.createElement('canvas');
    canvas.width = 512;
    canvas.height = 96;
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = '#e8b91e';
    ctx.fillRect(0, 0, 512, 96);
    ctx.fillStyle = '#161a19';
    for (let x = -96; x < 608; x += 96) {
      ctx.beginPath();
      ctx.moveTo(x, 0); ctx.lineTo(x + 45, 0); ctx.lineTo(x + 96, 96); ctx.lineTo(x + 51, 96); ctx.closePath(); ctx.fill();
    }
    ctx.fillStyle = 'rgba(255,255,255,.17)'; ctx.fillRect(0, 2, 512, 3);
    ctx.fillStyle = 'rgba(0,0,0,.3)'; ctx.fillRect(0, 88, 512, 8);
    const texture = new THREE.CanvasTexture(canvas);
    texture.colorSpace = THREE.SRGBColorSpace;
    texture.wrapS = texture.wrapT = THREE.RepeatWrapping;
    texture.anisotropy = this.renderer.capabilities.getMaxAnisotropy();
    return texture;
  }

  makeCrowdTexture() {
    const canvas = document.createElement('canvas');
    canvas.width = 1024;
    canvas.height = 256;
    const ctx = canvas.getContext('2d');
    const grad = ctx.createLinearGradient(0, 0, 0, 256);
    grad.addColorStop(0, '#0e181d'); grad.addColorStop(.5, '#10191d'); grad.addColorStop(1, '#05090b');
    ctx.fillStyle = grad; ctx.fillRect(0, 0, 1024, 256);
    let seed = 41981;
    const random = () => ((seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
    for (let row = 0; row < 9; row++) {
      for (let x = 3 + (row % 2) * 5; x < 1024; x += 10 + random() * 5) {
        const y = 35 + row * 20 + random() * 7;
        const light = random();
        ctx.fillStyle = light > .975 ? '#bdeeff' : light > .94 ? '#ddaa38' : `rgba(120,145,151,${.2 + random() * .35})`;
        ctx.beginPath(); ctx.arc(x, y, 1.2 + random() * 1.5, 0, TAU); ctx.fill();
        ctx.fillRect(x - 1.5, y + 3, 3, 5 + random() * 4);
      }
    }
    ctx.fillStyle = 'rgba(183,222,231,.12)';
    for (let y = 20; y < 230; y += 22) ctx.fillRect(0, y, 1024, 1);
    const texture = new THREE.CanvasTexture(canvas);
    texture.colorSpace = THREE.SRGBColorSpace;
    texture.wrapS = THREE.RepeatWrapping;
    texture.repeat.x = 5;
    return texture;
  }

  buildTrack() {
    const p = (x, z) => new THREE.Vector3(x, 0, z);
    this.trackCurve = new THREE.CatmullRomCurve3([
      p(-14, 72), p(29, 67), p(67, 45), p(79, 9), p(69, -30),
      p(34, -57), p(-7, -67), p(-51, -54), p(-78, -20), p(-76, 20),
      p(-53, 52)
    ], true, 'centripetal', .43);
    this.trackLength = this.trackCurve.getLength();

    const segments = 520;
    const widthSegments = 24;
    const positions = [];
    const uvs = [];
    const indices = [];

    for (let i = 0; i <= segments; i++) {
      const t = i / segments;
      const center = this.trackCurve.getPointAt(t);
      const tangent = this.trackCurve.getTangentAt(t).normalize();
      const side = new THREE.Vector3(tangent.z, 0, -tangent.x).normalize();
      for (let j = 0; j <= widthSegments; j++) {
        const u = j / widthSegments * 2 - 1;
        const lift = this.crossSectionHeight(u);
        const point = center.clone().addScaledVector(side, u * this.trackHalfWidth);
        point.y += lift;
        positions.push(point.x, point.y, point.z);
        uvs.push(t * 34, (u + 1) * 1.45);
      }
    }

    for (let i = 0; i < segments; i++) {
      for (let j = 0; j < widthSegments; j++) {
        const a = i * (widthSegments + 1) + j;
        const b = a + widthSegments + 1;
        indices.push(a, b, a + 1, b, b + 1, a + 1);
      }
    }

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    geometry.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
    geometry.setIndex(indices);
    geometry.computeVertexNormals();

    const steelTexture = this.makeSteelTexture();
    const surfaceMaterial = new THREE.MeshPhysicalMaterial({
      map: steelTexture,
      color: 0x789099,
      metalness: .68,
      roughness: .38,
      clearcoat: .26,
      clearcoatRoughness: .52,
      envMapIntensity: .72,
      side: THREE.DoubleSide
    });
    this.track = new THREE.Mesh(geometry, surfaceMaterial);
    this.track.receiveShadow = true;
    this.scene.add(this.track);

    // Dark structural shell visible below the elevated edges.
    const shell = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color: 0x10191d, metalness: .88, roughness: .66, side: THREE.BackSide }));
    shell.position.y = -.32;
    this.scene.add(shell);

    const hazardTexture = this.makeHazardTexture();
    const hazardMaterial = new THREE.MeshStandardMaterial({ map: hazardTexture, color: 0xffffff, metalness: .28, roughness: .56, side: THREE.DoubleSide });
    this.scene.add(this.makeTrackStrip(-.99, -.88, .045, hazardMaterial, 62));
    this.scene.add(this.makeTrackStrip(.88, .99, .045, hazardMaterial, 62));

    const ledMaterial = new THREE.MeshBasicMaterial({ color: 0xd8fbff, toneMapped: false });
    [-.835, .835].forEach((u) => {
      const curve = this.makeOffsetCurve(u, .22, 320);
      const tube = new THREE.Mesh(new THREE.TubeGeometry(curve, 420, .075, 5, true), ledMaterial);
      this.scene.add(tube);
    });

    this.buildRails();
    this.buildTrackLights();
  }

  crossSectionHeight(u) {
    return this.wallHeight * Math.pow(Math.abs(u), this.wallPower);
  }

  crossSectionSlope(u) {
    if (Math.abs(u) < .0001) return 0;
    return (this.wallHeight * this.wallPower * Math.pow(Math.abs(u), this.wallPower - 1) / this.trackHalfWidth) * Math.sign(u);
  }

  makeTrackStrip(u0, u1, yOffset, material, repeat = 40) {
    const segments = 480;
    const positions = [];
    const uvs = [];
    const indices = [];
    for (let i = 0; i <= segments; i++) {
      const t = i / segments;
      [u0, u1].forEach((u, j) => {
        const frame = this.trackFrame(t, u * this.trackHalfWidth);
        positions.push(frame.point.x, frame.point.y + yOffset, frame.point.z);
        uvs.push(t * repeat, j);
      });
    }
    for (let i = 0; i < segments; i++) {
      const a = i * 2;
      indices.push(a, a + 2, a + 1, a + 2, a + 3, a + 1);
    }
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    geometry.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
    geometry.setIndex(indices);
    geometry.computeVertexNormals();
    const mesh = new THREE.Mesh(geometry, material);
    mesh.receiveShadow = true;
    return mesh;
  }

  makeOffsetCurve(u, verticalOffset = 0, samples = 220) {
    const points = [];
    for (let i = 0; i < samples; i++) {
      const frame = this.trackFrame(i / samples, u * this.trackHalfWidth);
      frame.point.y += verticalOffset;
      points.push(frame.point);
    }
    return new THREE.CatmullRomCurve3(points, true, 'centripetal', .4);
  }

  buildRails() {
    const railMaterial = new THREE.MeshStandardMaterial({ color: 0x77888d, metalness: .94, roughness: .25, envMapIntensity: 1.2 });
    [-1.025, 1.025].forEach((u) => {
      [1.0, 1.75, 2.5].forEach((height, index) => {
        const curve = this.makeOffsetCurve(u, height, 260);
        const rail = new THREE.Mesh(new THREE.TubeGeometry(curve, 440, index === 0 ? .13 : .105, 7, true), railMaterial);
        rail.castShadow = true;
        this.scene.add(rail);
      });
    });

    const countPerSide = 52;
    const geometry = new THREE.CylinderGeometry(.105, .13, 3.05, 7);
    const posts = new THREE.InstancedMesh(geometry, railMaterial, countPerSide * 2);
    posts.castShadow = true;
    let index = 0;
    [-1.025, 1.025].forEach((u) => {
      for (let i = 0; i < countPerSide; i++) {
        const frame = this.trackFrame(i / countPerSide, u * this.trackHalfWidth);
        this._dummy.position.copy(frame.point);
        this._dummy.position.y += 1.5;
        this._dummy.rotation.set(0, 0, 0);
        this._dummy.updateMatrix();
        posts.setMatrixAt(index++, this._dummy.matrix);
      }
    });
    this.scene.add(posts);
  }

  buildTrackLights() {
    const countPerSide = 54;
    const boxGeometry = new THREE.BoxGeometry(.22, .14, 1.65);
    const lightMaterial = new THREE.MeshBasicMaterial({ color: 0xdaf9ff, toneMapped: false });
    const lights = new THREE.InstancedMesh(boxGeometry, lightMaterial, countPerSide * 2);
    let index = 0;
    [-1.04, 1.04].forEach((u) => {
      for (let i = 0; i < countPerSide; i++) {
        const t = i / countPerSide;
        const frame = this.trackFrame(t, u * this.trackHalfWidth);
        this._dummy.position.copy(frame.point);
        this._dummy.position.y += 3.35;
        this._dummy.rotation.set(0, Math.atan2(frame.tangent.x, frame.tangent.z), 0);
        this._dummy.updateMatrix();
        lights.setMatrixAt(index++, this._dummy.matrix);
      }
    });
    this.scene.add(lights);
  }

  buildStadium() {
    const crowdTexture = this.makeCrowdTexture();
    const arenaWall = new THREE.Mesh(
      new THREE.CylinderGeometry(118, 112, 25, 128, 1, true),
      new THREE.MeshStandardMaterial({ map: crowdTexture, color: 0x536267, emissive: 0x10191d, emissiveMap: crowdTexture, emissiveIntensity: .32, metalness: .25, roughness: .88, side: THREE.BackSide })
    );
    arenaWall.position.y = 17.8;
    this.scene.add(arenaWall);

    const outerFloor = new THREE.Mesh(
      new THREE.CircleGeometry(145, 128),
      new THREE.MeshStandardMaterial({ color: 0x0a1013, metalness: .72, roughness: .82 })
    );
    outerFloor.rotation.x = -Math.PI / 2;
    outerFloor.position.y = -.7;
    outerFloor.receiveShadow = true;
    this.scene.add(outerFloor);

    const darkMetal = new THREE.MeshStandardMaterial({ color: 0x1d292e, metalness: .9, roughness: .39 });
    const lightMetal = new THREE.MeshStandardMaterial({ color: 0x46575d, metalness: .94, roughness: .27 });
    const ringSpecs = [[106, 12.5, .55], [110, 17.8, .72], [114, 24.2, .78], [119, 30.4, .62], [101, 33.2, .4]];
    ringSpecs.forEach(([radius, y, tube], i) => {
      const ring = new THREE.Mesh(new THREE.TorusGeometry(radius, tube, 7, 160), i % 2 ? lightMetal : darkMetal);
      ring.rotation.x = Math.PI / 2;
      ring.position.y = y;
      this.scene.add(ring);
    });

    // Tier fronts create the dense stepped grandstand silhouette.
    [103, 108, 113].forEach((radius, i) => {
      const tier = new THREE.Mesh(
        new THREE.CylinderGeometry(radius + 1.5, radius, 2.2, 128, 1, true),
        new THREE.MeshStandardMaterial({ color: i === 1 ? 0x172329 : 0x10191d, metalness: .64, roughness: .73, side: THREE.BackSide })
      );
      tier.position.y = 10.5 + i * 5.7;
      this.scene.add(tier);
    });

    const columnGeometry = new THREE.CylinderGeometry(.36, .56, 31, 7);
    const columns = new THREE.InstancedMesh(columnGeometry, darkMetal, 32);
    for (let i = 0; i < 32; i++) {
      const a = i / 32 * TAU;
      this._dummy.position.set(Math.cos(a) * 116, 17, Math.sin(a) * 116);
      this._dummy.rotation.set(0, 0, 0);
      this._dummy.updateMatrix();
      columns.setMatrixAt(i, this._dummy.matrix);
    }
    this.scene.add(columns);

    // Radial roof trusses.
    const trussMaterial = new THREE.MeshStandardMaterial({ color: 0x26353a, metalness: .92, roughness: .38 });
    for (let i = 0; i < 20; i++) {
      const a = i / 20 * TAU;
      const beam = new THREE.Mesh(new THREE.BoxGeometry(39, .35, .35), trussMaterial);
      beam.position.set(Math.cos(a) * 101, 33.2, Math.sin(a) * 101);
      beam.rotation.y = -a;
      beam.castShadow = true;
      this.scene.add(beam);
    }

    this.buildFloodlightRing();
    this.buildOverheadTrusses(trussMaterial);
  }

  buildFloodlightRing() {
    const count = 104;
    const geometry = new THREE.BoxGeometry(2.65, .28, .7);
    const material = new THREE.MeshBasicMaterial({ color: 0xe4fbff, toneMapped: false });
    const fixtures = new THREE.InstancedMesh(geometry, material, count);
    for (let i = 0; i < count; i++) {
      const a = i / count * TAU;
      this._dummy.position.set(Math.cos(a) * 104, 29.8 + Math.sin(a * 3) * .35, Math.sin(a) * 104);
      this._dummy.rotation.set(0, -a, -.07);
      this._dummy.updateMatrix();
      fixtures.setMatrixAt(i, this._dummy.matrix);
    }
    this.scene.add(fixtures);
  }

  buildOverheadTrusses(material) {
    const makeBeam = (a, b, radius = .14) => {
      const direction = new THREE.Vector3().subVectors(b, a);
      const length = direction.length();
      const beam = new THREE.Mesh(new THREE.CylinderGeometry(radius, radius, length, 6), material);
      beam.position.copy(a).add(b).multiplyScalar(.5);
      beam.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), direction.normalize());
      beam.castShadow = true;
      this.scene.add(beam);
      return beam;
    };

    const spans = [
      [new THREE.Vector3(-95, 35, -36), new THREE.Vector3(94, 36, 20)],
      [new THREE.Vector3(-74, 39, 74), new THREE.Vector3(62, 38, -84)]
    ];
    spans.forEach(([start, end]) => {
      makeBeam(start, end, .25);
      makeBeam(start.clone().add(new THREE.Vector3(0, 2.3, 0)), end.clone().add(new THREE.Vector3(0, 2.3, 0)), .18);
      for (let i = 0; i <= 16; i++) {
        const t = i / 16;
        const low = start.clone().lerp(end, t);
        const high = low.clone().add(new THREE.Vector3(0, 2.3, 0));
        if (i % 2) high.add(new THREE.Vector3((end.z - start.z) * .012, 0, -(end.x - start.x) * .012));
        makeBeam(low, high, .08);
      }
    });
  }

  makeSignTexture(title, subtitle, accent = '#eec22e') {
    const canvas = document.createElement('canvas');
    canvas.width = 1024; canvas.height = 256;
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = '#071015'; ctx.fillRect(0, 0, 1024, 256);
    ctx.strokeStyle = '#6a7b80'; ctx.lineWidth = 4; ctx.strokeRect(8, 8, 1008, 240);
    ctx.fillStyle = accent; ctx.fillRect(0, 0, 18, 256);
    ctx.fillStyle = '#e8f3f4'; ctx.font = '800 108px Arial Narrow, sans-serif'; ctx.fillText(title, 64, 135);
    ctx.fillStyle = '#829397'; ctx.font = '600 28px Arial, sans-serif'; ctx.letterSpacing = '8px'; ctx.fillText(subtitle, 68, 193);
    const texture = new THREE.CanvasTexture(canvas); texture.colorSpace = THREE.SRGBColorSpace;
    return texture;
  }

  buildStartGantry() {
    const t = 0;
    const frame = this.trackFrame(t, 0);
    const gantry = new THREE.Group();
    gantry.position.copy(frame.point);
    gantry.rotation.y = Math.atan2(frame.tangent.x, frame.tangent.z);

    const metal = new THREE.MeshStandardMaterial({ color: 0x26363c, metalness: .94, roughness: .3 });
    const makeBox = (w, h, d, x, y, z) => {
      const mesh = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), metal);
      mesh.position.set(x, y, z); mesh.castShadow = true; gantry.add(mesh); return mesh;
    };
    makeBox(.45, 14.5, .55, -15.2, 8, 0);
    makeBox(.45, 14.5, .55, 15.2, 8, 0);
    makeBox(31, .52, .68, 0, 15, 0);
    makeBox(31, .22, .35, 0, 12.5, 0);
    for (let x = -14; x <= 14; x += 3.5) {
      const brace = makeBox(.16, 3.1, .2, x, 13.75, 0);
      brace.rotation.z = (Math.round(x / 3.5) % 2 ? 1 : -1) * .48;
    }

    const signMaterial = new THREE.MeshBasicMaterial({ map: this.makeSignTexture('SECTOR 09', 'THE CRUCIBLE // NIGHT HEAT'), toneMapped: false });
    const sign = new THREE.Mesh(new THREE.PlaneGeometry(11.5, 2.85), signMaterial);
    sign.position.set(0, 13.7, .38);
    gantry.add(sign);

    const lampMaterial = new THREE.MeshBasicMaterial({ color: 0xffb027, toneMapped: false });
    for (let x = -2.1; x <= 2.1; x += 1.4) {
      const lamp = new THREE.Mesh(new THREE.CircleGeometry(.26, 16), lampMaterial);
      lamp.position.set(x, 11.9, .42); gantry.add(lamp);
    }
    this.scene.add(gantry);
  }

  buildAtmosphere() {
    const count = 1450;
    const positions = new Float32Array(count * 3);
    let seed = 1107;
    const random = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
    for (let i = 0; i < count; i++) {
      const r = Math.sqrt(random()) * 126;
      const a = random() * TAU;
      positions[i * 3] = Math.cos(a) * r;
      positions[i * 3 + 1] = 1 + random() * 36;
      positions[i * 3 + 2] = Math.sin(a) * r;
    }
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    const material = new THREE.PointsMaterial({ color: 0xaad5df, size: .075, transparent: true, opacity: .36, depthWrite: false });
    this.dust = new THREE.Points(geometry, material);
    this.scene.add(this.dust);

    this.sparkCount = 90;
    const sparkPositions = new Float32Array(this.sparkCount * 3);
    this.sparkVelocity = [];
    const sparkGeometry = new THREE.BufferGeometry();
    sparkGeometry.setAttribute('position', new THREE.BufferAttribute(sparkPositions, 3));
    const sparkMaterial = new THREE.PointsMaterial({ color: 0xffa62d, size: .14, transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false });
    this.sparks = new THREE.Points(sparkGeometry, sparkMaterial);
    this.scene.add(this.sparks);
  }

  buildRacers() {
    this.player = this.createRacer(0xe3edf0, 0x62e7ff, true);
    this.scene.add(this.player);

    const configs = [
      { offset: .032, lateral: -4.2, speed: 50, color: 0xe6752f, glow: 0xff6b28 },
      { offset: .068, lateral: 5.5, speed: 52, color: 0x8d8f94, glow: 0xf4bc31 },
      { offset: .11, lateral: -1.2, speed: 49, color: 0x5c7184, glow: 0x4ee0ff }
    ];
    this.opponents = configs.map((config) => {
      const racer = this.createRacer(config.color, config.glow, false);
      racer.userData.progress = (this.progress + config.offset) % 1;
      racer.userData.lateral = config.lateral;
      racer.userData.speed = config.speed;
      this.scene.add(racer);
      const frame = this.trackFrame(racer.userData.progress, racer.userData.lateral);
      this.placeRacer(racer, frame, 0);
      return racer;
    });
  }

  createRacer(bodyColor, glowColor, player) {
    const group = new THREE.Group();
    group.rotation.order = 'YXZ';
    const body = new THREE.MeshStandardMaterial({ color: bodyColor, metalness: .92, roughness: .24, envMapIntensity: 1.4 });
    const dark = new THREE.MeshStandardMaterial({ color: 0x0c1215, metalness: .78, roughness: .4 });
    const glow = new THREE.MeshBasicMaterial({ color: glowColor, toneMapped: false });

    const core = new THREE.Mesh(new THREE.BoxGeometry(1.25, .42, 2.75), body);
    core.position.y = .62; core.castShadow = true; group.add(core);
    const nose = new THREE.Mesh(new THREE.ConeGeometry(.65, 1.8, 5), body);
    nose.rotation.x = Math.PI / 2; nose.position.set(0, .61, 1.7); nose.castShadow = true; group.add(nose);
    const tail = new THREE.Mesh(new THREE.BoxGeometry(1.6, .18, 1), dark);
    tail.position.set(0, .72, -1.25); group.add(tail);
    const canopy = new THREE.Mesh(new THREE.SphereGeometry(.53, 16, 8, 0, TAU, 0, Math.PI / 2), new THREE.MeshPhysicalMaterial({ color: 0x071116, metalness: .2, roughness: .1, transmission: .18, transparent: true, opacity: .83 }));
    canopy.scale.set(.8, .75, 1.4); canopy.position.set(0, .82, .15); group.add(canopy);

    const wheels = [];
    [[-.82,.58], [.82,.58], [-.82,-.82], [.82,-.82]].forEach(([x,z]) => {
      const wheel = new THREE.Mesh(new THREE.CylinderGeometry(.41, .41, .28, 14), dark);
      wheel.rotation.z = Math.PI / 2; wheel.position.set(x, .37, z); wheel.castShadow = true; group.add(wheel); wheels.push(wheel);
      const hub = new THREE.Mesh(new THREE.CircleGeometry(.18, 12), glow);
      hub.position.set(x + Math.sign(x) * .15, .37, z); hub.rotation.y = Math.sign(x) * Math.PI / 2; group.add(hub);
    });

    const tailLight = new THREE.Mesh(new THREE.BoxGeometry(.72, .12, .09), glow);
    tailLight.position.set(0, .65, -1.79); group.add(tailLight);
    const underglow = new THREE.Mesh(new THREE.PlaneGeometry(1.25, 2.4), new THREE.MeshBasicMaterial({ color: glowColor, transparent: true, opacity: player ? .17 : .11, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide, toneMapped: false }));
    underglow.rotation.x = -Math.PI / 2; underglow.position.y = .14; group.add(underglow);

    group.scale.setScalar(player ? 1.02 : .92);
    group.userData.wheels = wheels;
    return group;
  }

  trackFrame(progress, lateral = 0) {
    const t = ((progress % 1) + 1) % 1;
    const center = this.trackCurve.getPointAt(t);
    const tangent = this.trackCurve.getTangentAt(t).normalize();
    const side = new THREE.Vector3(tangent.z, 0, -tangent.x).normalize();
    const u = clamp(lateral / this.trackHalfWidth, -1.08, 1.08);
    const point = center.clone().addScaledVector(side, lateral);
    point.y += this.crossSectionHeight(u);
    return { point, tangent, side, u, slope: this.crossSectionSlope(u) };
  }

  placeRacer(racer, frame, steering = 0) {
    racer.position.copy(frame.point);
    racer.position.y += .15;
    const yaw = Math.atan2(frame.tangent.x, frame.tangent.z) + steering * .025;
    racer.rotation.set(0, yaw, -Math.atan(frame.slope) * .88 - steering * .045);
  }

  bindUI() {
    $('#race-button').addEventListener('click', () => this.startRace());
    $('#tour-button').addEventListener('click', () => this.startTour());
    $('#audio-button').addEventListener('click', () => this.toggleAudio());
    $('#fullscreen-button').addEventListener('click', () => this.toggleFullscreen());

    window.addEventListener('keydown', (event) => {
      if (['ArrowUp','ArrowDown','ArrowLeft','ArrowRight','Space'].includes(event.code)) event.preventDefault();
      this.keys[event.code] = true;
      if (event.repeat) return;
      if ((event.code === 'Enter' || event.code === 'Space') && this.state === 'briefing') this.startRace();
      if (event.code === 'KeyC' && this.state === 'playing') {
        this.cameraMode = (this.cameraMode + 1) % 3;
        this.showMessage(['CHASE CAMERA', 'DECK CAMERA', 'ARENA CAMERA'][this.cameraMode]);
      }
      if (event.code === 'KeyR' && (this.state === 'playing' || this.state === 'countdown')) this.resetRace(false);
      if (event.code === 'Escape') this.returnToBriefing();
    }, { passive: false });
    window.addEventListener('keyup', (event) => { this.keys[event.code] = false; });
    window.addEventListener('blur', () => { this.keys = Object.create(null); });
    window.addEventListener('resize', () => this.resize());

    document.querySelectorAll('[data-key]').forEach((button) => {
      const key = button.dataset.key;
      const down = (event) => { event.preventDefault(); this.keys[key] = true; button.classList.add('is-held'); };
      const up = (event) => { event.preventDefault(); this.keys[key] = false; button.classList.remove('is-held'); };
      button.addEventListener('pointerdown', down);
      button.addEventListener('pointerup', up);
      button.addEventListener('pointercancel', up);
      button.addEventListener('pointerleave', up);
    });
  }

  startRace() {
    if (this.state !== 'briefing' && this.state !== 'tour') return;
    this.resetRace(true);
    this.state = 'countdown';
    app.className = 'is-racing';
    this.cameraMode = 0;
    this.runCountdown();
  }

  resetRace(silent = false) {
    this.progress = .025;
    this.previousProgress = this.progress;
    this.lap = 1;
    this.raceTime = 0;
    this.speed = 0;
    this.lateral = 0;
    this.lateralVelocity = 0;
    this.boost = 1;
    lapValue.textContent = '01 / 03';
    if (!silent) this.showMessage('RUN RESET');
  }

  runCountdown() {
    const sequence = ['3', '2', '1', 'GO'];
    sequence.forEach((value, index) => {
      window.setTimeout(() => {
        countdown.textContent = value;
        countdown.classList.remove('show');
        void countdown.offsetWidth;
        countdown.classList.add('show');
        if (value === 'GO') {
          this.state = 'playing';
          this.speed = 13;
        }
      }, index * 720);
    });
  }

  startTour() {
    this.state = 'tour';
    this.tourTime = 0;
    app.className = 'is-tour';
    this.showMessage('CINEMATIC SURVEY // ESC TO EXIT');
  }

  returnToBriefing() {
    if (this.state === 'briefing') return;
    this.state = 'briefing';
    this.speed = 0;
    app.className = 'is-briefing';
    countdown.classList.remove('show');
  }

  showMessage(message) {
    raceMessage.textContent = message;
    raceMessage.classList.remove('show');
    void raceMessage.offsetWidth;
    raceMessage.classList.add('show');
  }

  toggleFullscreen() {
    if (!document.fullscreenElement) document.documentElement.requestFullscreen?.();
    else document.exitFullscreen?.();
  }

  toggleAudio() {
    this.audioEnabled = !this.audioEnabled;
    $('#audio-button').classList.toggle('audio-off', !this.audioEnabled);
    if (this.audioEnabled) {
      if (!this.audio) this.createAudio();
      this.audio.context.resume();
      this.audio.master.gain.setTargetAtTime(.065, this.audio.context.currentTime, .08);
    } else if (this.audio) {
      this.audio.master.gain.setTargetAtTime(0, this.audio.context.currentTime, .06);
    }
  }

  createAudio() {
    const AudioContextClass = window.AudioContext || window.webkitAudioContext;
    if (!AudioContextClass) return;
    const context = new AudioContextClass();
    const master = context.createGain();
    const filter = context.createBiquadFilter();
    filter.type = 'lowpass'; filter.frequency.value = 280;
    master.gain.value = 0;
    filter.connect(master); master.connect(context.destination);

    const oscillators = [
      { type: 'sawtooth', frequency: 41, gain: .72 },
      { type: 'triangle', frequency: 82, gain: .28 }
    ].map((spec) => {
      const oscillator = context.createOscillator();
      const gain = context.createGain();
      oscillator.type = spec.type; oscillator.frequency.value = spec.frequency; gain.gain.value = spec.gain;
      oscillator.connect(gain); gain.connect(filter); oscillator.start();
      return { oscillator, gain, base: spec.frequency };
    });
    this.audio = { context, master, filter, oscillators };
  }

  updateAudio() {
    if (!this.audio) return;
    const time = this.audio.context.currentTime;
    const ratio = this.speed / 70;
    this.audio.oscillators.forEach((voice) => voice.oscillator.frequency.setTargetAtTime(voice.base + ratio * voice.base * 2.4, time, .08));
    this.audio.filter.frequency.setTargetAtTime(180 + ratio * 920, time, .12);
  }

  resize() {
    const width = this.container.clientWidth || window.innerWidth;
    const height = this.container.clientHeight || window.innerHeight;
    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();
    this.renderer?.setSize(width, height);
    this.composer?.setSize(width, height);
    this.renderer?.setPixelRatio(Math.min(window.devicePixelRatio, width < 800 ? 1.35 : 1.75));
  }

  updateRace(dt) {
    const throttle = (this.keys.KeyW || this.keys.ArrowUp ? 1 : 0) - (this.keys.KeyS || this.keys.ArrowDown ? 1 : 0);
    const steer = (this.keys.KeyD || this.keys.ArrowRight ? 1 : 0) - (this.keys.KeyA || this.keys.ArrowLeft ? 1 : 0);
    const boosting = (this.keys.ShiftLeft || this.keys.ShiftRight) && this.boost > .015 && this.speed > 16;
    const maxSpeed = boosting ? 91 : 66;

    if (throttle > 0) this.speed += (20.5 + (this.speed < 12 ? 9 : 0)) * dt;
    else if (throttle < 0) this.speed -= 30 * dt;
    else this.speed -= (3.2 + this.speed * .018) * dt;
    if (boosting) {
      this.speed += 29 * dt;
      this.boost -= .24 * dt;
    } else {
      this.boost += .075 * dt;
    }
    this.boost = clamp(this.boost, 0, 1);
    this.speed = clamp(this.speed, 0, maxSpeed);

    const steerPower = (4.5 + Math.min(this.speed, 62) * .09) * (this.speed < 2 ? .25 : 1);
    this.lateralVelocity += steer * steerPower * dt;
    this.lateralVelocity *= Math.exp(-3.25 * dt);
    this.lateral += this.lateralVelocity * dt * 2.25;

    const boundary = this.trackHalfWidth * .79;
    if (Math.abs(this.lateral) > boundary) {
      this.lateral = Math.sign(this.lateral) * boundary;
      this.lateralVelocity *= -.27;
      this.speed *= Math.pow(.985, dt * 60);
      this.emitSparks();
      if (this.elapsed - this.lastMessageAt > 3.2) {
        this.showMessage('WALL CONTACT // TRACTION LOSS');
        this.lastMessageAt = this.elapsed;
      }
    }

    this.previousProgress = this.progress;
    this.progress = (this.progress + this.speed * dt / this.trackLength) % 1;
    if (this.progress < this.previousProgress && this.speed > 10) {
      this.lap += 1;
      if (this.lap > 3) {
        this.lap = 1;
        this.raceTime = 0;
        this.showMessage('HEAT COMPLETE // NEW RUN');
      } else {
        this.showMessage(`LAP ${String(this.lap).padStart(2,'0')} // HOLD THE LINE`);
      }
      lapValue.textContent = `${String(this.lap).padStart(2,'0')} / 03`;
    }
    this.raceTime += dt;

    const frame = this.trackFrame(this.progress, this.lateral);
    this.placeRacer(this.player, frame, steer);
    this.player.userData.wheels.forEach((wheel) => { wheel.rotation.x -= this.speed * dt * 2.25; });
    this.updateCamera(frame, steer, dt);
    this.updateOpponents(dt);

    app.classList.toggle('is-boosting', boosting);
    const displaySpeed = Math.round(this.speed * 4.28);
    speedValue.textContent = String(displaySpeed).padStart(3, '0');
    boostFill.style.transform = `scaleX(${this.boost})`;
    const minutes = Math.floor(this.raceTime / 60);
    const seconds = Math.floor(this.raceTime % 60);
    const millis = Math.floor((this.raceTime % 1) * 1000);
    timerValue.textContent = `${String(minutes).padStart(2,'0')}:${String(seconds).padStart(2,'0')}.${String(millis).padStart(3,'0')}`;
    this.updateMap(this.progress, frame);
    this.updateAudio();
  }

  updateCountdown(dt) {
    const frame = this.trackFrame(this.progress, this.lateral);
    this.placeRacer(this.player, frame, 0);
    this.updateCamera(frame, 0, dt);
    this.updateOpponents(dt * .25);
  }

  updateOpponents(dt) {
    this.opponents.forEach((opponent, index) => {
      let speed = opponent.userData.speed;
      if (this.state === 'playing') speed += Math.sin(this.elapsed * .65 + index * 1.7) * 3;
      opponent.userData.progress = (opponent.userData.progress + speed * dt / this.trackLength) % 1;
      opponent.userData.lateral += Math.sin(this.elapsed * .35 + index * 2.8) * dt * .16;
      const frame = this.trackFrame(opponent.userData.progress, opponent.userData.lateral);
      this.placeRacer(opponent, frame, Math.sin(this.elapsed + index) * .22);
      opponent.userData.wheels.forEach((wheel) => { wheel.rotation.x -= speed * dt * 2.2; });
    });
  }

  updateCamera(frame, steering, dt) {
    const up = new THREE.Vector3(0, 1, 0);
    let desired;
    let target;
    let fov;

    if (this.cameraMode === 1) {
      desired = frame.point.clone().addScaledVector(frame.tangent, -3.6).addScaledVector(frame.side, -.4).addScaledVector(up, 1.15);
      target = frame.point.clone().addScaledVector(frame.tangent, 23).addScaledVector(up, .75);
      fov = 76 + this.speed * .08;
    } else if (this.cameraMode === 2) {
      desired = frame.point.clone().addScaledVector(frame.tangent, -15).addScaledVector(frame.side, 9).addScaledVector(up, 12.5);
      target = frame.point.clone().addScaledVector(frame.tangent, 17).addScaledVector(up, 1.4);
      fov = 58;
    } else {
      desired = frame.point.clone().addScaledVector(frame.tangent, -10.7).addScaledVector(frame.side, -steering * .8).addScaledVector(up, 4.6);
      target = frame.point.clone().addScaledVector(frame.tangent, 16 + this.speed * .06).addScaledVector(up, 1.05);
      fov = 66 + this.speed * .14;
    }

    const cameraDamping = this.cameraMode === 1 ? 9 : 6.2;
    const alpha = 1 - Math.exp(-cameraDamping * dt);
    this.camera.position.lerp(desired, alpha);
    if (!this.cameraTarget) this.cameraTarget = target.clone();
    this.cameraTarget.lerp(target, 1 - Math.exp(-8 * dt));
    this.camera.lookAt(this.cameraTarget);
    this.camera.fov = damp(this.camera.fov, fov, 5, dt);
    this.camera.updateProjectionMatrix();
  }

  updateTour(dt) {
    this.tourTime += dt;
    const phase = this.tourTime % 32;
    let t;
    let lateral;
    let height;
    let lookAhead;

    if (phase < 10) {
      t = .01 + phase * .0065;
      lateral = -8 + Math.sin(phase * .35) * 1.5;
      height = 2.2;
      lookAhead = .032;
    } else if (phase < 20) {
      t = .28 + (phase - 10) * .004;
      lateral = 11.7;
      height = 3.8;
      lookAhead = .025;
    } else {
      t = .59 + (phase - 20) * .006;
      lateral = Math.sin(phase * .25) * 3;
      height = 12 + Math.sin((phase - 20) / 12 * Math.PI) * 7;
      lookAhead = .045;
    }

    const frame = this.trackFrame(t, lateral);
    const ahead = this.trackFrame((t + lookAhead) % 1, lateral * .35);
    const desired = frame.point.clone().add(new THREE.Vector3(0, height, 0));
    this.camera.position.lerp(desired, 1 - Math.exp(-2.2 * dt));
    if (!this.cameraTarget) this.cameraTarget = ahead.point.clone();
    this.cameraTarget.lerp(ahead.point.clone().add(new THREE.Vector3(0, 1.6, 0)), 1 - Math.exp(-3 * dt));
    this.camera.lookAt(this.cameraTarget);
    this.camera.fov = damp(this.camera.fov, phase < 20 ? 71 : 59, 2.5, dt);
    this.camera.updateProjectionMatrix();
    this.updateOpponents(dt);
    this.updateMap(t, frame);
  }

  updateBriefing(dt) {
    // The WebGL scene keeps moving behind the poster so entry into the race feels continuous.
    this.progress = (this.progress + dt * .003) % 1;
    const frame = this.trackFrame(this.progress, Math.sin(this.elapsed * .22) * 2);
    this.placeRacer(this.player, frame, 0);
    this.updateOpponents(dt * .38);
    this.updateMap((this.elapsed * .018) % 1, frame);
  }

  updateMap(progress, frame) {
    if (mapPath && mapDot) {
      const length = mapPath.getTotalLength();
      const point = mapPath.getPointAtLength(progress * length);
      mapDot.setAttribute('cx', point.x);
      mapDot.setAttribute('cy', point.y);
    }
    if (hudMapDot && frame) {
      const x = clamp((frame.point.x + 92) / 184, 0, 1);
      const y = clamp((frame.point.z + 82) / 164, 0, 1);
      hudMapDot.style.left = `${x * 94}%`;
      hudMapDot.style.top = `${(1 - y) * 88}%`;
    }
  }

  emitSparks() {
    const positions = this.sparks.geometry.attributes.position.array;
    const origin = this.player.position;
    for (let i = 0; i < this.sparkCount; i++) {
      positions[i * 3] = origin.x + (Math.random() - .5) * .8;
      positions[i * 3 + 1] = origin.y + Math.random() * .45;
      positions[i * 3 + 2] = origin.z + (Math.random() - .5) * .8;
      this.sparkVelocity[i] = new THREE.Vector3((Math.random() - .5) * 4, Math.random() * 2.4, (Math.random() - .5) * 4);
    }
    this.sparks.geometry.attributes.position.needsUpdate = true;
    this.sparks.material.opacity = .92;
  }

  updateSparks(dt) {
    if (this.sparks.material.opacity <= .01) return;
    const positions = this.sparks.geometry.attributes.position.array;
    for (let i = 0; i < this.sparkCount; i++) {
      const velocity = this.sparkVelocity[i];
      if (!velocity) continue;
      velocity.y -= 5.5 * dt;
      positions[i * 3] += velocity.x * dt;
      positions[i * 3 + 1] += velocity.y * dt;
      positions[i * 3 + 2] += velocity.z * dt;
    }
    this.sparks.material.opacity *= Math.exp(-4.8 * dt);
    this.sparks.geometry.attributes.position.needsUpdate = true;
  }

  renderOnce() {
    this.composer.render();
  }

  animate() {
    requestAnimationFrame(this.animate);
    const dt = Math.min(this.clock.getDelta(), .04);
    this.elapsed += dt;
    this.dust.rotation.y += dt * .003;

    if (this.state === 'playing') this.updateRace(dt);
    else if (this.state === 'countdown') this.updateCountdown(dt);
    else if (this.state === 'tour') this.updateTour(dt);
    else this.updateBriefing(dt);

    this.updateSparks(dt);
    this.composer.render();
  }
}

let game;
try {
  game = new CrucibleArena(mount);
  window.crucibleArena = game;
  window.setTimeout(() => loading.classList.add('done'), 650);
} catch (error) {
  console.error('Unable to initialize the 3D arena:', error);
  loading.classList.add('done');
  $('#race-button').querySelector('span').textContent = 'VIEW CIRCUIT';
  $('#tour-button').style.display = 'none';
}
