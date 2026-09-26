/* Dustline — real WebGL mine-race prototype. No 2D scene fallback or external engine required. */
(() => {
  'use strict';

  const canvas = document.getElementById('scene');
  const minimap = document.getElementById('minimap');
  const mapCtx = minimap.getContext('2d');
  const experience = document.getElementById('experience');
  const $ = (id) => document.getElementById(id);
  const TAU = Math.PI * 2;
  const clamp = (n, min, max) => Math.max(min, Math.min(max, n));
  const lerp = (a, b, t) => a + (b - a) * t;
  const pad = (n, size = 2) => String(Math.max(0, Math.round(n))).padStart(size, '0');
  const distance = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);
  const color = (hex) => {
    const value = hex.replace('#', '');
    return [parseInt(value.slice(0, 2), 16) / 255, parseInt(value.slice(2, 4), 16) / 255, parseInt(value.slice(4, 6), 16) / 255];
  };

  const viewport = { width: 0, height: 0, dpr: 1 };
  const worldBounds = { left: 45, right: 1600, top: 48, bottom: 963 };

  // The mine is a real 3D space; these are the centerlines used to build the road mesh and gameplay routes.
  const roads = [
    { id: 'ore-line', width: 86, points: [[120, 874], [320, 874], [320, 680], [518, 680], [518, 412], [760, 412], [760, 202], [1042, 202], [1042, 392], [1375, 392], [1375, 646], [1146, 646], [1146, 874], [1518, 874]] },
    { id: 'west-cut', width: 76, points: [[320, 680], [142, 500], [300, 302], [518, 412]] },
    { id: 'deep-loop', width: 78, points: [[518, 680], [760, 775], [995, 692], [1146, 646]] },
    { id: 'north-cross', width: 72, points: [[760, 202], [872, 84], [1248, 84], [1375, 392]] },
    { id: 'east-return', width: 74, points: [[1042, 392], [918, 532], [995, 692]] },
    { id: 'lower-bypass', width: 70, points: [[320, 874], [500, 936], [823, 918], [1146, 874]] },
    { id: 'service-spur', width: 63, points: [[760, 412], [900, 350], [1042, 392]] }
  ];
  const mainRoute = makeRoute(roads[0].points);
  const trafficRoutes = [makeRoute(roads[0].points), makeRoute(roads[2].points), makeRoute(roads[1].points), makeRoute(roads[3].points)];
  const raceCheckpoints = [
    { x: 120, y: 874 }, { x: 320, y: 680 }, { x: 518, y: 412 }, { x: 760, y: 202 },
    { x: 1042, y: 392 }, { x: 1375, y: 392 }, { x: 1375, y: 646 }, { x: 1518, y: 874 }
  ];
  const supports = [
    [254, 874, 0], [320, 754, Math.PI / 2], [438, 680, 0], [518, 535, Math.PI / 2],
    [635, 412, 0], [760, 310, Math.PI / 2], [903, 202, 0], [1042, 300, Math.PI / 2],
    [1198, 392, 0], [1375, 520, Math.PI / 2], [1257, 646, 0], [1146, 762, Math.PI / 2],
    [690, 747, 0], [1000, 694, 0], [875, 84, 0], [1114, 84, 0], [300, 302, 0], [500, 936, 0]
  ].map(([x, y, angle], index) => ({ x, y, angle, index }));
  const lights = [
    [185, 874, 0], [320, 805, Math.PI / 2], [320, 680, 0], [425, 680, 0], [518, 598, Math.PI / 2],
    [518, 412, 0], [640, 412, 0], [760, 307, Math.PI / 2], [760, 202, 0], [900, 202, 0],
    [1042, 294, Math.PI / 2], [1042, 392, 0], [1210, 392, 0], [1375, 515, Math.PI / 2],
    [1375, 646, 0], [1256, 646, 0], [1146, 762, Math.PI / 2], [1146, 874, 0], [1420, 874, 0],
    [250, 500, -.85], [300, 302, .15], [760, 775, .35], [995, 692, -.3], [920, 532, 1],
    [940, 84, 0], [1180, 84, 0], [500, 936, 0]
  ].map(([x, y, angle], index) => ({ x, y, angle, index, phase: index * .73 }));
  const caveShape = [[38, 66], [170, 38], [418, 58], [612, 35], [816, 65], [1034, 37], [1252, 60], [1456, 40], [1608, 91], [1582, 258], [1632, 426], [1596, 608], [1624, 810], [1577, 952], [1390, 984], [1176, 950], [984, 987], [766, 956], [566, 986], [338, 955], [128, 980], [34, 895], [65, 710], [28, 526], [57, 332]];
  const dust = createDust(160);
  const rocks = createRocks(76);

  const car = { x: 120, y: 874, angle: 0, speed: 0, health: 100, boost: 86, hitFlash: 0, offRoad: false, roadDistance: 0, checkpoint: 0, inputSeen: false };
  const camera = { x: car.x, y: car.y };
  let carts = [makeCart('C-12', 0, 1260, 112, 'amber'), makeCart('C-04', 1, 720, 92, 'orange'), makeCart('C-19', 2, 310, 72, 'pale'), makeCart('C-27', 3, 238, 80, 'amber')];
  let elapsed = 0;
  let lastTime = performance.now();
  let toastTimer = 0;
  let toastType = '';
  let warningActive = false;
  let cinematic = false;
  let mapWidth = 0;
  let mapHeight = 0;

  function makeRoute(points) {
    const segments = [];
    let total = 0;
    for (let i = 0; i < points.length - 1; i += 1) {
      const a = { x: points[i][0], y: points[i][1] };
      const b = { x: points[i + 1][0], y: points[i + 1][1] };
      const length = Math.hypot(b.x - a.x, b.y - a.y);
      segments.push({ a, b, length, start: total, angle: Math.atan2(b.y - a.y, b.x - a.x) });
      total += length;
    }
    return { points, segments, total };
  }
  function pointAtRoute(route, distanceAlong) {
    const d = ((distanceAlong % route.total) + route.total) % route.total;
    for (const segment of route.segments) {
      if (d <= segment.start + segment.length) {
        const t = segment.length ? (d - segment.start) / segment.length : 0;
        return { x: lerp(segment.a.x, segment.b.x, t), y: lerp(segment.a.y, segment.b.y, t), angle: segment.angle };
      }
    }
    const last = route.segments[route.segments.length - 1];
    return { x: last.b.x, y: last.b.y, angle: last.angle };
  }
  function makeCart(id, routeIndex, offset, speed, colorName) { return { id, routeIndex, offset, speed, color: colorName, x: 0, y: 0, angle: 0, hitCooldown: 0, distance: 0 }; }
  function createDust(count) {
    let seed = 401; const next = () => { seed = (seed * 1664525 + 1013904223) % 4294967296; return seed / 4294967296; };
    return Array.from({ length: count }, () => ({ x: 55 + next() * 1510, y: 52 + next() * 900, r: .5 + next() * 1.8, a: .12 + next() * .3, phase: next() * TAU }));
  }
  function createRocks(count) {
    let seed = 811; const next = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
    return Array.from({ length: count }, () => ({ x: 65 + next() * 1510, y: 58 + next() * 900, r: 13 + next() * 38, rotation: next() * TAU, tone: next() }));
  }

  // -------------------------------------------------------------------------
  // Tiny WebGL renderer: enough geometry for a proper perspective mine while
  // keeping the prototype dependency-free and instantly runnable.
  // -------------------------------------------------------------------------
  const gl = canvas.getContext('webgl', { antialias: true, alpha: false, preserveDrawingBuffer: false });
  if (!gl) {
    const fallback = document.createElement('div'); fallback.textContent = 'WEBGL REQUIRED // ENABLE HARDWARE ACCELERATION'; fallback.style.cssText = 'position:absolute;inset:0;display:grid;place-items:center;color:#8de8e3;font:12px monospace;letter-spacing:.15em;background:#050a0c;z-index:20;';
    experience.appendChild(fallback);
    return;
  }

  const vertexSource = `
    attribute vec3 aPosition;
    attribute vec3 aNormal;
    uniform mat4 uProjection;
    uniform mat4 uView;
    uniform mat4 uModel;
    varying vec3 vWorldPosition;
    varying vec3 vNormal;
    void main() {
      vec4 worldPosition = uModel * vec4(aPosition, 1.0);
      vWorldPosition = worldPosition.xyz;
      vNormal = normalize(mat3(uModel) * aNormal);
      gl_Position = uProjection * uView * worldPosition;
    }
  `;
  const fragmentSource = `
    precision mediump float;
    uniform vec3 uColor;
    uniform vec3 uEmissive;
    uniform vec3 uFogColor;
    uniform vec3 uLightPos[8];
    uniform vec3 uLightColor[8];
    uniform float uLightPower[8];
    uniform vec3 uCamera;
    uniform float uAlpha;
    uniform float uFogNear;
    uniform float uFogFar;
    varying vec3 vWorldPosition;
    varying vec3 vNormal;
    void main() {
      vec3 normal = normalize(vNormal);
      vec3 lit = vec3(0.10, 0.13, 0.13);
      for (int i = 0; i < 8; i++) {
        vec3 toLight = uLightPos[i] - vWorldPosition;
        float lightDistance = length(toLight);
        vec3 lightDir = normalize(toLight);
        float diffuse = max(dot(normal, lightDir), 0.0);
        float falloff = max(0.0, 1.0 - lightDistance / 270.0);
        lit += uLightColor[i] * diffuse * falloff * falloff * uLightPower[i];
      }
      vec3 shaded = uColor * lit + uEmissive;
      float distanceToCamera = distance(uCamera, vWorldPosition);
      float fogAmount = smoothstep(uFogNear, uFogFar, distanceToCamera);
      shaded = mix(shaded, uFogColor, fogAmount * 0.82);
      gl_FragColor = vec4(shaded, uAlpha);
    }
  `;

  function compileShader(type, source) {
    const shader = gl.createShader(type); gl.shaderSource(shader, source); gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(shader));
    return shader;
  }
  const program = gl.createProgram();
  gl.attachShader(program, compileShader(gl.VERTEX_SHADER, vertexSource));
  gl.attachShader(program, compileShader(gl.FRAGMENT_SHADER, fragmentSource));
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program));
  gl.useProgram(program);

  const attrib = { position: gl.getAttribLocation(program, 'aPosition'), normal: gl.getAttribLocation(program, 'aNormal') };
  const uniforms = {
    projection: gl.getUniformLocation(program, 'uProjection'), view: gl.getUniformLocation(program, 'uView'), model: gl.getUniformLocation(program, 'uModel'),
    color: gl.getUniformLocation(program, 'uColor'), emissive: gl.getUniformLocation(program, 'uEmissive'), fogColor: gl.getUniformLocation(program, 'uFogColor'),
    lightPos: gl.getUniformLocation(program, 'uLightPos[0]'), lightColor: gl.getUniformLocation(program, 'uLightColor[0]'), lightPower: gl.getUniformLocation(program, 'uLightPower[0]'),
    camera: gl.getUniformLocation(program, 'uCamera'), alpha: gl.getUniformLocation(program, 'uAlpha'), fogNear: gl.getUniformLocation(program, 'uFogNear'), fogFar: gl.getUniformLocation(program, 'uFogFar')
  };

  function createMesh(data) {
    const positionBuffer = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(data.positions), gl.STATIC_DRAW);
    const normalBuffer = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, normalBuffer); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(data.normals), gl.STATIC_DRAW);
    return { position: positionBuffer, normal: normalBuffer, count: data.positions.length / 3 };
  }
  function cubeGeometry() {
    const positions = [], normals = [];
    const faces = [
      [[0, 0, 1], [[-.5, -.5, .5], [.5, -.5, .5], [.5, .5, .5], [-.5, .5, .5]]],
      [[0, 0, -1], [[.5, -.5, -.5], [-.5, -.5, -.5], [-.5, .5, -.5], [.5, .5, -.5]]],
      [[1, 0, 0], [[.5, -.5, .5], [.5, -.5, -.5], [.5, .5, -.5], [.5, .5, .5]]],
      [[-1, 0, 0], [[-.5, -.5, -.5], [-.5, -.5, .5], [-.5, .5, .5], [-.5, .5, -.5]]],
      [[0, 1, 0], [[-.5, .5, .5], [.5, .5, .5], [.5, .5, -.5], [-.5, .5, -.5]]],
      [[0, -1, 0], [[-.5, -.5, -.5], [.5, -.5, -.5], [.5, -.5, .5], [-.5, -.5, .5]]]
    ];
    faces.forEach(([normal, corners]) => {
      [0, 1, 2, 0, 2, 3].forEach((index) => { positions.push(...corners[index]); normals.push(...normal); });
    });
    return createMesh({ positions, normals });
  }
  function cylinderGeometry(radius = 1, height = 1, sides = 12) {
    const positions = [], normals = [];
    for (let i = 0; i < sides; i += 1) {
      const a = (i / sides) * TAU; const b = ((i + 1) / sides) * TAU;
      const ca = Math.cos(a), sa = Math.sin(a), cb = Math.cos(b), sb = Math.sin(b);
      const verts = [[radius * ca, -height / 2, radius * sa], [radius * cb, -height / 2, radius * sb], [radius * cb, height / 2, radius * sb], [radius * ca, height / 2, radius * sa]];
      const ns = [[ca, 0, sa], [cb, 0, sb], [cb, 0, sb], [ca, 0, sa]];
      [0, 1, 2, 0, 2, 3].forEach((index) => { positions.push(...verts[index]); normals.push(...ns[index]); });
      // caps
      positions.push(0, height / 2, 0, radius * cb, height / 2, radius * sb, radius * ca, height / 2, radius * sa); normals.push(0, 1, 0, 0, 1, 0, 0, 1, 0);
      positions.push(0, -height / 2, 0, radius * ca, -height / 2, radius * sa, radius * cb, -height / 2, radius * sb); normals.push(0, -1, 0, 0, -1, 0, 0, -1, 0);
    }
    return createMesh({ positions, normals });
  }
  function sphereGeometry(rows = 8, columns = 12) {
    const positions = [], normals = [];
    for (let y = 0; y < rows; y += 1) {
      const v0 = y / rows; const v1 = (y + 1) / rows; const p0 = Math.PI * v0; const p1 = Math.PI * v1;
      for (let x = 0; x < columns; x += 1) {
        const u0 = x / columns; const u1 = (x + 1) / columns;
        const vertex = (p, u) => [Math.sin(p) * Math.cos(u * TAU), Math.cos(p), Math.sin(p) * Math.sin(u * TAU)];
        const a = vertex(p0, u0), b = vertex(p0, u1), c = vertex(p1, u1), d = vertex(p1, u0);
        [a, b, c, a, c, d].forEach((v) => { positions.push(...v); normals.push(...v); });
      }
    }
    return createMesh({ positions, normals });
  }
  function frustumGeometry(topRadius = .1, bottomRadius = 1, height = 1, sides = 16) {
    const positions = [], normals = [];
    for (let i = 0; i < sides; i += 1) {
      const a = i / sides * TAU; const b = (i + 1) / sides * TAU; const ca = Math.cos(a), sa = Math.sin(a), cb = Math.cos(b), sb = Math.sin(b);
      const slope = bottomRadius - topRadius;
      const normalA = [ca, slope, sa]; const normalB = [cb, slope, sb];
      const va = [[bottomRadius * ca, -height / 2, bottomRadius * sa], [bottomRadius * cb, -height / 2, bottomRadius * sb], [topRadius * cb, height / 2, topRadius * sb], [topRadius * ca, height / 2, topRadius * sa]];
      [0, 1, 2, 0, 2, 3].forEach((index) => { positions.push(...va[index]); normals.push(...index === 1 || index === 2 ? normalB : normalA); });
    }
    return createMesh({ positions, normals });
  }
  const meshes = { cube: cubeGeometry(), cylinder: cylinderGeometry(1, 1, 12), wheel: cylinderGeometry(1, 1, 12), sphere: sphereGeometry(), rock: sphereGeometry(5, 7), beam: frustumGeometry(.08, 1, 1, 16) };

  // Matrix helpers, column-major to match WebGL.
  const identity = () => new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
  function multiply(a, b) {
    const out = new Float32Array(16);
    for (let column = 0; column < 4; column += 1) for (let row = 0; row < 4; row += 1) out[column * 4 + row] = a[row] * b[column * 4] + a[4 + row] * b[column * 4 + 1] + a[8 + row] * b[column * 4 + 2] + a[12 + row] * b[column * 4 + 3];
    return out;
  }
  function translation(x, y, z) { const m = identity(); m[12] = x; m[13] = y; m[14] = z; return m; }
  function scaling(x, y, z) { const m = identity(); m[0] = x; m[5] = y; m[10] = z; return m; }
  function rotateY(angle) { const m = identity(); const c = Math.cos(angle), s = Math.sin(angle); m[0] = c; m[2] = -s; m[8] = s; m[10] = c; return m; }
  function rotateX(angle) { const m = identity(); const c = Math.cos(angle), s = Math.sin(angle); m[5] = c; m[6] = s; m[9] = -s; m[10] = c; return m; }
  function modelMatrix(x, y, z, angle, sx, sy, sz, localX = 0, localY = 0, localZ = 0, localRX = 0, localRY = 0) {
    let m = translation(x, y, z); m = multiply(m, rotateY(angle)); m = multiply(m, translation(localX, localY, localZ));
    if (localRX) m = multiply(m, rotateX(localRX)); if (localRY) m = multiply(m, rotateY(localRY));
    return multiply(m, scaling(sx, sy, sz));
  }
  function perspective(fov, aspect, near, far) {
    const f = 1 / Math.tan(fov / 2); const range = near - far; const m = new Float32Array(16);
    m[0] = f / aspect; m[5] = f; m[10] = (near + far) / range; m[11] = -1; m[14] = 2 * near * far / range; return m;
  }
  function lookAt(eye, target) {
    let zx = eye[0] - target[0], zy = eye[1] - target[1], zz = eye[2] - target[2]; let length = Math.hypot(zx, zy, zz); zx /= length; zy /= length; zz /= length;
    let xx = -zz, xy = 0, xz = zx; length = Math.hypot(xx, xy, xz) || 1; xx /= length; xz /= length;
    const yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
    const m = identity(); m[0] = xx; m[1] = yx; m[2] = zx; m[4] = xy; m[5] = yy; m[6] = zy; m[8] = xz; m[9] = yz; m[10] = zz; m[12] = -(xx * eye[0] + xy * eye[1] + xz * eye[2]); m[13] = -(yx * eye[0] + yy * eye[1] + yz * eye[2]); m[14] = -(zx * eye[0] + zy * eye[1] + zz * eye[2]); return m;
  }
  const hex = { floor: color('#101b1d'), floorEdge: color('#1e2b2a'), asphalt: color('#1b2323'), asphaltEdge: color('#364440'), steel: color('#667971'), steelDark: color('#263532'), timber: color('#4e5b50'), rock: color('#172528'), rockLight: color('#33413e'), black: color('#061012'), cyan: color('#a8ffe8'), blue: color('#5aade7'), amber: color('#c27a30'), orange: color('#a4512f'), pale: color('#879d86'), glass: color('#4e9ea5'), white: color('#eefef2') };
  const emissive = { none: [0, 0, 0], cyan: color('#57dec8'), white: color('#c9fff1'), amber: color('#e9983b'), red: color('#e74d3d'), blue: color('#4d9ae2') };

  function draw(mesh, matrix, material, glow = emissive.none, alpha = 1) {
    gl.bindBuffer(gl.ARRAY_BUFFER, mesh.position); gl.enableVertexAttribArray(attrib.position); gl.vertexAttribPointer(attrib.position, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, mesh.normal); gl.enableVertexAttribArray(attrib.normal); gl.vertexAttribPointer(attrib.normal, 3, gl.FLOAT, false, 0, 0);
    gl.uniformMatrix4fv(uniforms.model, false, matrix); gl.uniform3fv(uniforms.color, material); gl.uniform3fv(uniforms.emissive, glow); gl.uniform1f(uniforms.alpha, alpha); gl.drawArrays(gl.TRIANGLES, 0, mesh.count);
  }
  function worldXY(x, y) { return [x - 800, y - 500]; }
  function segmentData(a, b) {
    const dx = b[0] - a[0], dz = b[1] - a[1], length = Math.hypot(dx, dz); return { x: (a[0] + b[0]) / 2 - 800, z: (a[1] + b[1]) / 2 - 500, length, angle: Math.atan2(dz, dx) };
  }

  const renderables = { road: [], groove: [], tie: [], junction: [], support: [], rock: [], wallRock: [], floor: null, roof: null };
  function buildMine() {
    renderables.floor = { x: 0, y: -1, z: 0, sx: 1820, sy: 2, sz: 1190, material: hex.floor };
    renderables.roof = { x: 0, y: 82, z: 0, sx: 1820, sy: 5, sz: 1190, material: hex.black };
    roads.forEach((road) => {
      for (let i = 0; i < road.points.length - 1; i += 1) {
        const part = segmentData(road.points[i], road.points[i + 1]); part.width = road.width; renderables.road.push(part);
        [-17, 17].forEach((offset) => { const normalX = -Math.sin(part.angle), normalZ = Math.cos(part.angle); renderables.groove.push({ ...part, x: part.x + normalX * offset, z: part.z + normalZ * offset, offset }); });
        const count = Math.floor(part.length / 37);
        for (let j = 1; j < count; j += 1) { const t = j / count; const px = lerp(road.points[i][0], road.points[i + 1][0], t); const pz = lerp(road.points[i][1], road.points[i + 1][1], t); renderables.tie.push({ ...part, x: px - 800, z: pz - 500 }); }
      }
    });
    [[320, 680], [518, 412], [760, 412], [1042, 392], [1146, 646], [760, 202], [518, 680], [995, 692]].forEach(([x, y]) => renderables.junction.push({ x: x - 800, z: y - 500 }));
    supports.forEach((support) => {
      const p = worldXY(support.x, support.y); const normalX = -Math.sin(support.angle), normalZ = Math.cos(support.angle);
      renderables.support.push({ ...support, x: p[0], z: p[1], leftX: p[0] + normalX * 48, leftZ: p[1] + normalZ * 48, rightX: p[0] - normalX * 48, rightZ: p[1] - normalZ * 48 });
    });
    rocks.forEach((rock) => {
      if (nearestRoad(rock).distance > 70) renderables.rock.push(rock);
    });
    // Rockfall along the cavern walls, so the roof feels supported by a real chamber rather than a flat plane.
    for (let i = 0; i < 34; i += 1) {
      const side = i % 4; const t = (i * .193) % 1; const r = 28 + (i % 5) * 9;
      const point = side === 0 ? { x: 68 + t * 1500, y: 70 + (i % 3) * 28 } : side === 1 ? { x: 1530 - (i % 3) * 24, y: 100 + t * 820 } : side === 2 ? { x: 80 + t * 1450, y: 915 - (i % 4) * 25 } : { x: 78 + (i % 3) * 24, y: 100 + t * 820 };
      renderables.wallRock.push({ x: point.x, y: point.y, r, rotation: i * .51, tone: .3 + (i % 3) * .2 });
    }
  }
  buildMine();

  // Input / interaction -----------------------------------------------------
  const keys = Object.create(null); const keyMap = new Set(['w', 'a', 's', 'd', 'arrowup', 'arrowleft', 'arrowdown', 'arrowright', ' ']);
  window.addEventListener('keydown', (event) => {
    const key = event.key.toLowerCase();
    if (keyMap.has(key)) { event.preventDefault(); keys[key] = true; if (!car.inputSeen) { car.inputSeen = true; $('driveHint').classList.add('is-hidden'); } }
    if (key === 'r') resetRun(); if (key === 'c') toggleCinematic();
  });
  window.addEventListener('keyup', (event) => { keys[event.key.toLowerCase()] = false; });
  window.addEventListener('blur', () => { Object.keys(keys).forEach((key) => { keys[key] = false; }); });
  $('resetButton').addEventListener('click', resetRun); $('cinematicButton').addEventListener('click', toggleCinematic);
  function resetRun() {
    car.x = 120; car.y = 874; car.angle = 0; car.speed = 0; car.health = 100; car.boost = 86; car.hitFlash = 0; car.checkpoint = 0; car.inputSeen = false; elapsed = 0; camera.x = car.x; camera.y = car.y;
    carts = [makeCart('C-12', 0, 1260, 112, 'amber'), makeCart('C-04', 1, 720, 92, 'orange'), makeCart('C-19', 2, 310, 72, 'pale'), makeCart('C-27', 3, 238, 80, 'amber')];
    carts.forEach((cart) => { const p = pointAtRoute(trafficRoutes[cart.routeIndex], cart.offset); cart.x = p.x; cart.y = p.y; cart.angle = p.angle; });
    $('driveHint').classList.remove('is-hidden'); $('trackStatus').textContent = 'TRACK LIVE'; $('incidentToast').classList.remove('visible'); warningActive = false; toastTimer = 0;
  }
  function toggleCinematic() {
    cinematic = !cinematic; experience.classList.toggle('cinematic', cinematic); const badge = $('cinematicBadge'); badge.innerHTML = cinematic ? 'CINEMATIC HUD ON <span>C</span>' : 'CINEMATIC HUD OFF <span>C</span>'; badge.classList.add('visible'); window.clearTimeout(toggleCinematic.timer); toggleCinematic.timer = window.setTimeout(() => badge.classList.remove('visible'), 1300);
  }

  // Simulation --------------------------------------------------------------
  function update(dt) {
    elapsed += dt; car.hitFlash = Math.max(0, car.hitFlash - dt * 2.8); toastTimer = Math.max(0, toastTimer - dt);
    const forward = keys.w || keys.arrowup; const reverse = keys.s || keys.arrowdown; const left = keys.a || keys.arrowleft; const right = keys.d || keys.arrowright; const throttle = forward ? 1 : reverse ? -1 : 0; const steering = (right ? 1 : 0) - (left ? 1 : 0); const boostOn = !!keys[' '] && forward && car.boost > 0 && car.speed > 28;
    const road = nearestRoad(car); car.roadDistance = road.distance; car.offRoad = road.distance > road.width * .58; const grip = car.offRoad ? .48 : 1; const maxSpeed = boostOn ? 390 : 276; const acceleration = boostOn ? 245 : 180;
    if (throttle !== 0) car.speed += throttle * acceleration * dt * grip; else car.speed *= Math.pow(car.offRoad ? .89 : .935, dt * 60);
    if (boostOn) car.boost = Math.max(0, car.boost - dt * 22); else car.boost = Math.min(100, car.boost + dt * 6.5); car.speed = clamp(car.speed, -115, maxSpeed);
    const steerStrength = (.72 + Math.min(Math.abs(car.speed) / 200, .72)) * (car.offRoad ? .65 : 1); if (steering) car.angle += steering * steerStrength * dt * (car.speed >= 0 ? 1 : -1);
    car.x = clamp(car.x + Math.cos(car.angle) * car.speed * dt, worldBounds.left, worldBounds.right); car.y = clamp(car.y + Math.sin(car.angle) * car.speed * dt, worldBounds.top, worldBounds.bottom);
    if (car.speed > 6 && car.checkpoint < raceCheckpoints.length - 1) { const next = raceCheckpoints[car.checkpoint + 1]; if (Math.hypot(car.x - next.x, car.y - next.y) < 76) { car.checkpoint += 1; showIncident(car.checkpoint === raceCheckpoints.length - 1 ? 'EXIT GATE AHEAD' : `SECTOR ${pad(car.checkpoint + 1)} CLEARED`, car.checkpoint === raceCheckpoints.length - 1 ? 'Finish line acquired. Keep it clean.' : 'Crosscut registered. Find the next beam marker.', 'info'); } }
    carts.forEach((cart) => {
      cart.distance = (cart.distance + cart.speed * dt) % trafficRoutes[cart.routeIndex].total; const p = pointAtRoute(trafficRoutes[cart.routeIndex], cart.offset + cart.distance); cart.x = p.x; cart.y = p.y; cart.angle = p.angle; cart.hitCooldown = Math.max(0, cart.hitCooldown - dt);
      if (Math.hypot(car.x - cart.x, car.y - cart.y) < 43 && cart.hitCooldown <= 0) { cart.hitCooldown = 2.4; car.health = Math.max(0, car.health - 15); car.speed *= -.28; car.x -= Math.cos(cart.angle) * 18; car.y -= Math.sin(cart.angle) * 18; car.hitFlash = 1; showIncident('IMPACT // CART', 'Chassis damage registered. Give the ore line room.', 'danger'); }
    });
    updateTrafficWarning();
    const cameraTargetX = car.x + Math.cos(car.angle) * 108; const cameraTargetY = car.y + Math.sin(car.angle) * 68; const cameraEase = 1 - Math.pow(.0008, dt); camera.x = lerp(camera.x, cameraTargetX, cameraEase); camera.y = lerp(camera.y, cameraTargetY, cameraEase);
    updateInterface();
  }
  function updateTrafficWarning() {
    let nearest = null;
    carts.forEach((cart) => { const dx = cart.x - car.x, dy = cart.y - car.y, d = Math.hypot(dx, dy), ahead = dx * Math.cos(car.angle) + dy * Math.sin(car.angle); if (ahead > -46 && d < 210 && (!nearest || d < nearest.d)) nearest = { cart, d, ahead }; });
    const shouldWarn = !!nearest && nearest.d < 172;
    if (shouldWarn && !warningActive && toastTimer <= 0) showIncident('CART INBOUND', nearest.d < 88 ? 'Crossing now. Brake or take the next cut.' : 'Ore line inbound. Check the crossing.', 'warning');
    warningActive = shouldWarn;
    if (toastTimer <= 0 && (!shouldWarn || toastType !== 'warning')) $('incidentToast').classList.remove('visible');
    if (shouldWarn && toastType === 'warning') { $('incidentTitle').textContent = nearest.d < 80 ? 'CROSSING ACTIVE' : 'CART INBOUND'; $('incidentDetail').textContent = nearest.d < 80 ? 'Brake or take the next cut.' : 'Ore line inbound. Check the crossing.'; }
  }
  function showIncident(title, detail, type = 'warning') {
    toastType = type; toastTimer = type === 'info' ? 2.4 : 2.1; $('incidentTitle').textContent = title; $('incidentDetail').textContent = detail; const toast = $('incidentToast'); toast.classList.toggle('is-info', type === 'info'); toast.classList.toggle('is-danger', type === 'danger'); toast.classList.add('visible');
  }
  function updateInterface() {
    const kmh = Math.abs(car.speed) * .36; $('speedValue').textContent = pad(kmh, 3); $('gearValue').textContent = car.speed < -4 ? 'R' : car.speed > 5 ? 'D' : 'N'; $('boostValue').textContent = `${Math.round(car.boost)}%`; $('healthValue').textContent = `${Math.round(car.health)}%`; $('boostBar').style.width = `${car.boost}%`; $('healthBar').style.width = `${car.health}%`; $('sectorValue').textContent = pad(Math.min(car.checkpoint + 1, 8)); $('routeProgress').style.width = `${Math.max(3, car.checkpoint / (raceCheckpoints.length - 1) * 100)}%`; $('trackStatus').textContent = car.health <= 0 ? 'CHASSIS CRITICAL' : car.offRoad ? 'OFF ROUTE' : car.checkpoint === raceCheckpoints.length - 1 ? 'EXIT GATE' : 'TRACK LIVE'; $('tractionState').textContent = car.offRoad ? 'GRIP / LOOSE' : boostActive() ? 'GRIP / BOOST' : 'GRIP / GOOD'; $('surfaceValue').textContent = car.offRoad ? 'LOOSE SHALE' : 'GROOVED PAVING'; $('cartCount').textContent = pad(carts.length); $('timer').textContent = formatTime(elapsed);
  }
  function boostActive() { return !!keys[' '] && (keys.w || keys.arrowup) && car.boost > 0 && car.speed > 28; }
  function formatTime(time) { return `${pad(Math.floor(time / 60))}:${(time % 60).toFixed(2).padStart(5, '0')}`; }

  // 3D scene ---------------------------------------------------------------
  function resize() {
    viewport.width = window.innerWidth; viewport.height = window.innerHeight; viewport.dpr = Math.min(2, window.devicePixelRatio || 1); canvas.width = Math.floor(viewport.width * viewport.dpr); canvas.height = Math.floor(viewport.height * viewport.dpr); canvas.style.width = `${viewport.width}px`; canvas.style.height = `${viewport.height}px`; gl.viewport(0, 0, canvas.width, canvas.height);
    mapWidth = minimap.clientWidth || 238; mapHeight = minimap.clientHeight || 161; minimap.width = Math.floor(mapWidth * viewport.dpr); minimap.height = Math.floor(mapHeight * viewport.dpr);
  }

  function setLights() {
    const carWorld = worldXY(car.x, car.y); const nearLights = lights.map((light) => { const p = worldXY(light.x, light.y); return { d: Math.hypot(p[0] - carWorld[0], p[1] - carWorld[1]), p, light }; }).sort((a, b) => a.d - b.d).slice(0, 8);
    const positions = new Float32Array(24); const colors = new Float32Array(24); const powers = new Float32Array(8);
    for (let i = 0; i < 8; i += 1) { const item = nearLights[i] || nearLights[0]; const p = item ? item.p : [0, 0]; positions.set([p[0], 52, p[1]], i * 3); const c = item && item.light.index % 4 === 0 ? [1, .64, .28] : [.44, 1, .84]; colors.set(c, i * 3); powers[i] = item ? 1.2 : 0; }
    gl.uniform3fv(uniforms.lightPos, positions); gl.uniform3fv(uniforms.lightColor, colors); gl.uniform1fv(uniforms.lightPower, powers);
  }

  function render(time) {
    gl.viewport(0, 0, canvas.width, canvas.height); gl.clearColor(.018, .035, .04, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST); gl.enable(gl.CULL_FACE); gl.useProgram(program);
    const carWorld = worldXY(car.x, car.y); const forwardX = Math.cos(car.angle), forwardZ = Math.sin(car.angle); const cameraWorld = [carWorld[0] - forwardX * 86, 22 + Math.min(Math.abs(car.speed) * .018, 3), carWorld[1] - forwardZ * 86]; const targetWorld = [carWorld[0] + forwardX * 156, 5.8, carWorld[1] + forwardZ * 156];
    const projection = perspective(Math.PI / 3.35, viewport.width / Math.max(1, viewport.height), .1, 1900); const view = lookAt(cameraWorld, targetWorld);
    gl.uniformMatrix4fv(uniforms.projection, false, projection); gl.uniformMatrix4fv(uniforms.view, false, view); gl.uniform3fv(uniforms.camera, new Float32Array(cameraWorld)); gl.uniform3fv(uniforms.fogColor, color('#071315')); gl.uniform1f(uniforms.fogNear, 380); gl.uniform1f(uniforms.fogFar, 950); setLights();

    // Opaque cavern shell.
    const f = renderables.floor; draw(meshes.cube, modelMatrix(f.x, f.y, f.z, 0, f.sx, f.sy, f.sz), f.material);
    const r = renderables.roof; draw(meshes.cube, modelMatrix(r.x, r.y, r.z, 0, r.sx, r.sy, r.sz), r.material);
    drawCavernWalls(); drawRocks(); drawRoads(); drawSupports(); drawCeilingDetails(); drawLabels3D(); drawFinish3D(); drawCarts3D(); drawPlayer3D();
    drawLightBeams(time); drawDust3D(time);
  }

  function drawCavernWalls() {
    // Low side shelves catch light and make the playable chamber feel carved out of rock.
    const walls = [
      [-795, 26, -340, 20, 54, 880], [795, 26, -340, 20, 54, 880], [0, 26, -480, 1560, 54, 18], [0, 26, 480, 1560, 54, 18]
    ];
    walls.forEach(([x, y, z, sx, sy, sz]) => draw(meshes.cube, modelMatrix(x, y, z, 0, sx, sy, sz), hex.rock));
  }
  function drawRocks() {
    renderables.wallRock.forEach((rock) => drawRock3D(rock, true)); renderables.rock.forEach((rock) => drawRock3D(rock, false));
  }
  function drawRock3D(rock, wall) {
    const [x, z] = worldXY(rock.x, rock.y); const scale = wall ? [rock.r * 1.45, rock.r * .72, rock.r * .92] : [rock.r, rock.r * .7, rock.r * .82]; const material = rock.tone > .62 ? hex.rockLight : hex.rock;
    draw(meshes.rock, modelMatrix(x, wall ? rock.r * .45 : rock.r * .38, z, rock.rotation, scale[0], scale[1], scale[2]), material);
  }
  function drawRoads() {
    renderables.road.forEach((part) => {
      draw(meshes.cube, modelMatrix(part.x, .25, part.z, -part.angle, part.length + 2, .5, part.width), hex.asphalt);
      draw(meshes.cube, modelMatrix(part.x, .53, part.z, -part.angle, part.length, .05, part.width - 15), hex.asphaltEdge);
    });
    renderables.groove.forEach((part) => { draw(meshes.cube, modelMatrix(part.x, .61, part.z, -part.angle, part.length, .09, 4), hex.steelDark); draw(meshes.cube, modelMatrix(part.x, .67, part.z, -part.angle, part.length, .025, 1.2), hex.steel); });
    renderables.tie.forEach((tie) => draw(meshes.cube, modelMatrix(tie.x, .59, tie.z, -tie.angle, 6, .08, tie.width * .88), hex.timber));
    renderables.junction.forEach((node) => draw(meshes.cylinder, modelMatrix(node.x, .55, node.z, 0, 42, .15, 42), hex.asphaltEdge));
  }
  function drawSupports() {
    renderables.support.forEach((support) => {
      // Tall timber legs, a cross-beam over the road, and a second roof rail make the tunnel height legible in chase view.
      draw(meshes.cube, modelMatrix(support.leftX, 27, support.leftZ, -support.angle, 8, 54, 8), hex.timber);
      draw(meshes.cube, modelMatrix(support.rightX, 27, support.rightZ, -support.angle, 8, 54, 8), hex.timber);
      draw(meshes.cube, modelMatrix(support.x, 55, support.z, -support.angle, 8, 5, 138), hex.steelDark);
      draw(meshes.cube, modelMatrix(support.x, 57, support.z, -support.angle, 3, .6, 128), hex.steel);
      draw(meshes.cube, modelMatrix(support.x, 64, support.z, -support.angle, 138, 3, 6), hex.steelDark);
      draw(meshes.cube, modelMatrix(support.x, 65.6, support.z, -support.angle, 126, .45, 1.5), hex.timber);
    });
  }
  function drawCeilingDetails() {
    // Repeated dark ribs connect to the support beams and make the roof readable in perspective.
    for (let x = -720; x <= 720; x += 150) draw(meshes.cube, modelMatrix(x, 75, 0, 0, 7, 5, 930), hex.rock);
    lights.forEach((light) => { const p = worldXY(light.x, light.y); draw(meshes.cube, modelMatrix(p[0], 58, p[1], 0, 8, 2, 8), hex.steelDark); draw(meshes.sphere, modelMatrix(p[0], 55.8, p[1], 0, 4.5, 4.5, 4.5), hex.white, light.index % 4 === 0 ? emissive.amber : emissive.cyan); });
  }
  function drawLightBeams(time) {
    gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE); gl.depthMask(false);
    lights.forEach((light) => { const p = worldXY(light.x, light.y); const pulse = .045 + Math.sin(time * .002 + light.phase) * .01; draw(meshes.beam, modelMatrix(p[0], 28, p[1], 0, 30, 56, 30), hex.cyan, light.index % 4 === 0 ? emissive.amber : emissive.cyan, pulse); });
    gl.depthMask(true); gl.disable(gl.BLEND);
  }
  function drawDust3D(time) {
    // Small emissive dust motes sell depth as they drift through the lamps.
    gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE); gl.depthMask(false);
    dust.slice(0, 50).forEach((mote) => { const [x, z] = worldXY(mote.x, mote.y); const bob = Math.sin(time * .0007 + mote.phase) * 2.5; draw(meshes.sphere, modelMatrix(x, 5 + bob, z, 0, mote.r, mote.r, mote.r), hex.white, emissive.cyan, mote.a * .28); });
    gl.depthMask(true); gl.disable(gl.BLEND);
  }
  function drawLabels3D() {
    // Thin raised markers are intentionally geometric; the HTML HUD carries the type labels.
    [[196, 854], [550, 391], [788, 181], [1067, 370], [1290, 625]].forEach(([x, z], index) => { const p = worldXY(x, z); draw(meshes.cube, modelMatrix(p[0], 1.2, p[1], 0, 18, .18, 1.4), index % 2 ? hex.blue : hex.steel); });
  }
  function drawFinish3D() { const p = worldXY(1518, 874); draw(meshes.cube, modelMatrix(p[0], 9, p[1] - 48, 0, 5, 18, 5), hex.steel); draw(meshes.cube, modelMatrix(p[0], 18, p[1], 0, 5, 4, 92), hex.steel); draw(meshes.sphere, modelMatrix(p[0], 28, p[1], 0, 5, 5, 5), hex.white, emissive.cyan); }

  function entityMatrix(worldX, worldY, worldZ, angle, localX, localY, localZ, sx, sy, sz, localRX = 0, localRY = 0) { return modelMatrix(worldX, worldY, worldZ, -angle, sx, sy, sz, localX, localY, localZ, localRX, localRY); }
  function drawCarts3D(time) {
    const cartMaterials = { amber: hex.amber, orange: hex.orange, pale: hex.pale };
    carts.forEach((cart) => {
      const [x, z] = worldXY(cart.x, cart.y); const body = cartMaterials[cart.color];
      draw(meshes.cube, entityMatrix(x, 4.5, z, cart.angle, 0, 0, 0, 62, 8, 34), body);
      draw(meshes.cube, entityMatrix(x, 9.2, z, cart.angle, -2, 0, 0, 51, 2.5, 27), body);
      draw(meshes.cube, entityMatrix(x, 10.5, z, cart.angle, -5, 0, 0, 43, 3.5, 23), cart.color === 'pale' ? hex.rockLight : hex.amber);
      [-22, 22].forEach((localX) => [-20, 20].forEach((localZ) => draw(meshes.wheel, entityMatrix(x, 2.4, z, cart.angle, localX, 0, localZ, 7, 4, 7, Math.PI / 2), hex.black)));
      draw(meshes.cube, entityMatrix(x, 6, z, cart.angle, 32, 0, -10, 5, 3, 5), hex.white, emissive.amber);
      draw(meshes.cube, entityMatrix(x, 6, z, cart.angle, 32, 0, 10, 5, 3, 5), hex.white, emissive.amber);
      if (cart.hitCooldown > 1.6) draw(meshes.cube, entityMatrix(x, 9, z, cart.angle, 0, 0, 0, 76, 2, 45), hex.white, emissive.red, .42);
    });
  }
  function drawPlayer3D(time) {
    const [x, z] = worldXY(car.x, car.y); const body = car.hitFlash > 0 ? hex.white : color('#4c7c85');
    draw(meshes.cube, entityMatrix(x, 4.5, z, car.angle, 0, 0, 0, 76, 8, 43), body);
    draw(meshes.cube, entityMatrix(x, 8.5, z, car.angle, -6, 0, 0, 33, 8, 35), hex.glass);
    draw(meshes.cube, entityMatrix(x, 11.7, z, car.angle, -6, 0, 0, 36, 1.4, 38), hex.steel);
    // glass panels split the roof cabin into two readable windows
    draw(meshes.cube, entityMatrix(x, 9.2, z, car.angle, 10, 0, -17.4, 13, 3.3, 1.2), hex.glass, emissive.blue);
    draw(meshes.cube, entityMatrix(x, 9.2, z, car.angle, 10, 0, 17.4, 13, 3.3, 1.2), hex.glass, emissive.blue);
    draw(meshes.cube, entityMatrix(x, 5, z, car.angle, -18, 0, 0, 24, 1.4, 39), hex.steel);
    draw(meshes.cube, entityMatrix(x, 4.2, z, car.angle, 10, 0, -22, 9, 1, 2), hex.white, emissive.white);
    draw(meshes.cube, entityMatrix(x, 4.2, z, car.angle, 10, 0, 22, 9, 1, 2), hex.white, emissive.white);
    draw(meshes.cube, entityMatrix(x, 4.2, z, car.angle, -37, 0, -15, 5, 2, 4), hex.red, emissive.red);
    draw(meshes.cube, entityMatrix(x, 4.2, z, car.angle, -37, 0, 15, 5, 2, 4), hex.red, emissive.red);
    [-23, 23].forEach((localX) => [-23, 23].forEach((localZ) => draw(meshes.wheel, entityMatrix(x, 2.5, z, car.angle, localX, 0, localZ, 9, 5, 8, Math.PI / 2), hex.black)));
    // roof rails
    draw(meshes.cylinder, entityMatrix(x, 14, z, car.angle, -20, 0, -17, 1.5, 28, 1.5, 0, Math.PI / 2), hex.steel);
    draw(meshes.cylinder, entityMatrix(x, 14, z, car.angle, 14, 0, -17, 1.5, 28, 1.5, 0, Math.PI / 2), hex.steel);
    if (boostActive()) { draw(meshes.cube, entityMatrix(x, 4, z, car.angle, -48, 0, -9, 22, 2, 4), hex.cyan, emissive.cyan, .8); draw(meshes.cube, entityMatrix(x, 4, z, car.angle, -48, 0, 9, 22, 2, 4), hex.cyan, emissive.cyan, .8); }
    if (car.hitFlash > 0) { gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE); gl.depthMask(false); draw(meshes.sphere, modelMatrix(x, 8, z, 0, 48, 20, 38), hex.white, emissive.red, car.hitFlash * .15); gl.depthMask(true); gl.disable(gl.BLEND); }
  }

  // Gameplay geometry query -------------------------------------------------
  function projectNearest(point, road) {
    let best = { distance: Infinity, x: point.x, y: point.y, angle: 0, width: road.width };
    const route = road.segments || makeRoute(road.points).segments;
    for (const segment of route) { const vx = segment.b.x - segment.a.x, vy = segment.b.y - segment.a.y, lengthSq = vx * vx + vy * vy || 1; const t = clamp(((point.x - segment.a.x) * vx + (point.y - segment.a.y) * vy) / lengthSq, 0, 1); const x = segment.a.x + vx * t, y = segment.a.y + vy * t, d = Math.hypot(point.x - x, point.y - y); if (d < best.distance) best = { distance: d, x, y, angle: segment.angle, width: road.width }; }
    return best;
  }
  function nearestRoad(point) { let best = { distance: Infinity, x: point.x, y: point.y, angle: 0, width: 0 }; for (const road of roads) { const candidate = projectNearest(point, road); if (candidate.distance < best.distance) best = candidate; } return best; }

  // Minimap -----------------------------------------------------------------
  function renderMinimap() {
    mapCtx.setTransform(viewport.dpr, 0, 0, viewport.dpr, 0, 0); mapCtx.clearRect(0, 0, mapWidth, mapHeight); mapCtx.fillStyle = 'rgba(2, 8, 9, .88)'; mapCtx.fillRect(0, 0, mapWidth, mapHeight);
    const padMap = 10, sx = (mapWidth - padMap * 2) / (worldBounds.right - worldBounds.left), sy = (mapHeight - padMap * 2) / (worldBounds.bottom - worldBounds.top), scale = Math.min(sx, sy), ox = (mapWidth - (worldBounds.right - worldBounds.left) * scale) / 2 - worldBounds.left * scale, oy = (mapHeight - (worldBounds.bottom - worldBounds.top) * scale) / 2 - worldBounds.top * scale, toMap = (x, y) => ({ x: ox + x * scale, y: oy + y * scale });
    mapCtx.strokeStyle = 'rgba(141, 232, 227, .12)'; mapCtx.lineWidth = 1; mapCtx.beginPath(); caveShape.forEach(([x, y], index) => { const p = toMap(x, y); index ? mapCtx.lineTo(p.x, p.y) : mapCtx.moveTo(p.x, p.y); }); mapCtx.closePath(); mapCtx.stroke();
    roads.forEach((road) => { mapCtx.save(); mapCtx.lineCap = 'round'; mapCtx.lineJoin = 'round'; mapCtx.beginPath(); road.points.forEach(([x, y], index) => { const p = toMap(x, y); index ? mapCtx.lineTo(p.x, p.y) : mapCtx.moveTo(p.x, p.y); }); mapCtx.strokeStyle = 'rgba(105, 133, 124, .24)'; mapCtx.lineWidth = Math.max(3, road.width * scale + 2); mapCtx.stroke(); mapCtx.strokeStyle = 'rgba(24, 43, 42, .96)'; mapCtx.lineWidth = Math.max(2, road.width * scale); mapCtx.stroke(); mapCtx.restore(); });
    mapCtx.save(); mapCtx.beginPath(); mainRoute.points.forEach(([x, y], index) => { const p = toMap(x, y); index ? mapCtx.lineTo(p.x, p.y) : mapCtx.moveTo(p.x, p.y); }); mapCtx.strokeStyle = '#6cb8ff'; mapCtx.globalAlpha = .9; mapCtx.lineWidth = 1.5; mapCtx.setLineDash([3, 3]); mapCtx.stroke(); mapCtx.restore();
    raceCheckpoints.forEach((checkpoint, index) => { const p = toMap(checkpoint.x, checkpoint.y); mapCtx.fillStyle = index <= car.checkpoint ? '#8de8e3' : 'rgba(141, 232, 227, .35)'; mapCtx.beginPath(); mapCtx.arc(p.x, p.y, index === car.checkpoint + 1 ? 2.8 : 1.4, 0, TAU); mapCtx.fill(); });
    carts.forEach((cart) => { const p = toMap(cart.x, cart.y); mapCtx.fillStyle = '#f5b24b'; mapCtx.shadowColor = '#f5b24b'; mapCtx.shadowBlur = 6; mapCtx.beginPath(); mapCtx.arc(p.x, p.y, 2.6, 0, TAU); mapCtx.fill(); mapCtx.shadowBlur = 0; });
    const player = toMap(car.x, car.y); mapCtx.save(); mapCtx.translate(player.x, player.y); mapCtx.rotate(car.angle); mapCtx.fillStyle = '#d2fff7'; mapCtx.shadowColor = '#8de8e3'; mapCtx.shadowBlur = 8; mapCtx.beginPath(); mapCtx.moveTo(6, 0); mapCtx.lineTo(-4, -4); mapCtx.lineTo(-2, 0); mapCtx.lineTo(-4, 4); mapCtx.closePath(); mapCtx.fill(); mapCtx.restore();
  }

  function initCarts() { carts.forEach((cart) => { const p = pointAtRoute(trafficRoutes[cart.routeIndex], cart.offset); cart.x = p.x; cart.y = p.y; cart.angle = p.angle; }); }
  function frame(now) { const dt = Math.min(.045, Math.max(.001, (now - lastTime) / 1000)); lastTime = now; update(dt); render(now); renderMinimap(); requestAnimationFrame(frame); }

  gl.enable(gl.DEPTH_TEST); gl.enable(gl.CULL_FACE); gl.clearDepth(1); initCarts(); resize(); window.addEventListener('resize', resize); requestAnimationFrame(frame);
})();
