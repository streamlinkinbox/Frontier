/* Dustline — a self-contained top-down mine race prototype. */
(() => {
  'use strict';

  const scene = document.getElementById('scene');
  const ctx = scene.getContext('2d');
  const minimap = document.getElementById('minimap');
  const mapCtx = minimap.getContext('2d');
  const experience = document.getElementById('experience');

  const $ = (id) => document.getElementById(id);
  const TAU = Math.PI * 2;
  const clamp = (n, min, max) => Math.max(min, Math.min(max, n));
  const lerp = (a, b, t) => a + (b - a) * t;
  const distance = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);
  const pad = (n, size = 2) => String(Math.max(0, Math.round(n))).padStart(size, '0');

  const viewport = { width: 0, height: 0, dpr: 1 };
  const worldBounds = { left: 45, right: 1600, top: 48, bottom: 963 };

  // The mine is deliberately built as a readable criss-crossing service network.
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
  const trafficRoutes = [
    makeRoute(roads[0].points),
    makeRoute(roads[2].points),
    makeRoute(roads[1].points),
    makeRoute(roads[3].points)
  ];

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
    [250, 500, -0.85], [300, 302, 0.15], [760, 775, 0.35], [995, 692, -0.3], [920, 532, 1.0],
    [940, 84, 0], [1180, 84, 0], [500, 936, 0]
  ].map(([x, y, angle], index) => ({ x, y, angle, index, phase: index * .73 }));

  const caveShape = [[38, 66], [170, 38], [418, 58], [612, 35], [816, 65], [1034, 37], [1252, 60], [1456, 40], [1608, 91], [1582, 258], [1632, 426], [1596, 608], [1624, 810], [1577, 952], [1390, 984], [1176, 950], [984, 987], [766, 956], [566, 986], [338, 955], [128, 980], [34, 895], [65, 710], [28, 526], [57, 332]];

  const dust = createDust(310);
  const rocks = createRocks(96);

  const keys = Object.create(null);
  const car = {
    x: 120, y: 874, angle: 0, speed: 0,
    health: 100, boost: 86, hitFlash: 0,
    offRoad: false, roadDistance: 0, checkpoint: 0,
    inputSeen: false
  };
  const camera = { x: car.x, y: car.y };
  let carts = [
    makeCart('C-12', 0, 1260, 112, 'amber'),
    makeCart('C-04', 1, 720, 92, 'orange'),
    makeCart('C-19', 2, 310, 72, 'pale'),
    makeCart('C-27', 3, 238, 80, 'amber')
  ];

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
    let d = ((distanceAlong % route.total) + route.total) % route.total;
    for (const segment of route.segments) {
      if (d <= segment.start + segment.length) {
        const t = segment.length ? (d - segment.start) / segment.length : 0;
        return {
          x: lerp(segment.a.x, segment.b.x, t),
          y: lerp(segment.a.y, segment.b.y, t),
          angle: segment.angle
        };
      }
    }
    const last = route.segments[route.segments.length - 1];
    return { x: last.b.x, y: last.b.y, angle: last.angle };
  }

  function makeCart(id, routeIndex, offset, speed, color) {
    return { id, routeIndex, offset, speed, color, x: 0, y: 0, angle: 0, hitCooldown: 0, distance: 0 };
  }

  function createDust(count) {
    let seed = 401;
    const next = () => { seed = (seed * 1664525 + 1013904223) % 4294967296; return seed / 4294967296; };
    return Array.from({ length: count }, () => ({
      x: 55 + next() * 1510,
      y: 52 + next() * 900,
      r: .4 + next() * 1.9,
      a: .12 + next() * .32,
      phase: next() * TAU
    }));
  }

  function createRocks(count) {
    let seed = 811;
    const next = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
    return Array.from({ length: count }, () => ({
      x: 65 + next() * 1510,
      y: 58 + next() * 900,
      r: 13 + next() * 38,
      rotation: next() * TAU,
      tone: next()
    }));
  }

  function resize() {
    viewport.width = window.innerWidth;
    viewport.height = window.innerHeight;
    viewport.dpr = Math.min(2, window.devicePixelRatio || 1);
    scene.width = Math.floor(viewport.width * viewport.dpr);
    scene.height = Math.floor(viewport.height * viewport.dpr);
    scene.style.width = `${viewport.width}px`;
    scene.style.height = `${viewport.height}px`;
    mapWidth = minimap.clientWidth || 238;
    mapHeight = minimap.clientHeight || 161;
    minimap.width = Math.floor(mapWidth * viewport.dpr);
    minimap.height = Math.floor(mapHeight * viewport.dpr);
  }

  function projectNearest(point, road) {
    let best = { distance: Infinity, x: point.x, y: point.y, angle: 0, width: road.width };
    for (const segment of road.segments) {
      const vx = segment.b.x - segment.a.x;
      const vy = segment.b.y - segment.a.y;
      const lengthSq = vx * vx + vy * vy || 1;
      const t = clamp(((point.x - segment.a.x) * vx + (point.y - segment.a.y) * vy) / lengthSq, 0, 1);
      const x = segment.a.x + vx * t;
      const y = segment.a.y + vy * t;
      const d = Math.hypot(point.x - x, point.y - y);
      if (d < best.distance) best = { distance: d, x, y, angle: segment.angle, width: road.width };
    }
    return best;
  }

  function nearestRoad(point) {
    let best = { distance: Infinity, x: point.x, y: point.y, angle: 0, width: 0 };
    for (const road of roads) {
      const candidate = projectNearest(point, makeRoute(road.points));
      if (candidate.distance < best.distance) best = candidate;
    }
    return best;
  }

  // Input / interaction -----------------------------------------------------
  const keyMap = new Set(['w', 'a', 's', 'd', 'arrowup', 'arrowleft', 'arrowdown', 'arrowright', ' ']);
  window.addEventListener('keydown', (event) => {
    const key = event.key.toLowerCase();
    if (keyMap.has(key)) {
      event.preventDefault();
      keys[key] = true;
      if (!car.inputSeen) {
        car.inputSeen = true;
        $('driveHint').classList.add('is-hidden');
      }
    }
    if (key === 'r') resetRun();
    if (key === 'c') toggleCinematic();
  });
  window.addEventListener('keyup', (event) => { keys[event.key.toLowerCase()] = false; });
  window.addEventListener('blur', () => { Object.keys(keys).forEach((key) => { keys[key] = false; }); });
  $('resetButton').addEventListener('click', resetRun);
  $('cinematicButton').addEventListener('click', toggleCinematic);

  function resetRun() {
    car.x = 120; car.y = 874; car.angle = 0; car.speed = 0; car.health = 100; car.boost = 86; car.hitFlash = 0; car.checkpoint = 0; car.inputSeen = false;
    elapsed = 0;
    camera.x = car.x; camera.y = car.y;
    carts = [
      makeCart('C-12', 0, 1260, 112, 'amber'),
      makeCart('C-04', 1, 720, 92, 'orange'),
      makeCart('C-19', 2, 310, 72, 'pale'),
      makeCart('C-27', 3, 238, 80, 'amber')
    ];
    $('driveHint').classList.remove('is-hidden');
    $('trackStatus').textContent = 'TRACK LIVE';
    $('incidentToast').classList.remove('visible');
    warningActive = false;
    toastTimer = 0;
  }

  function toggleCinematic() {
    cinematic = !cinematic;
    experience.classList.toggle('cinematic', cinematic);
    const badge = $('cinematicBadge');
    badge.innerHTML = cinematic ? 'CINEMATIC HUD ON <span>C</span>' : 'CINEMATIC HUD OFF <span>C</span>';
    badge.classList.add('visible');
    window.clearTimeout(toggleCinematic.timer);
    toggleCinematic.timer = window.setTimeout(() => badge.classList.remove('visible'), 1300);
  }

  // Simulation --------------------------------------------------------------
  function update(dt) {
    elapsed += dt;
    car.hitFlash = Math.max(0, car.hitFlash - dt * 2.8);
    toastTimer = Math.max(0, toastTimer - dt);

    const forward = keys.w || keys.arrowup;
    const reverse = keys.s || keys.arrowdown;
    const left = keys.a || keys.arrowleft;
    const right = keys.d || keys.arrowright;
    const throttle = forward ? 1 : reverse ? -1 : 0;
    const steering = (right ? 1 : 0) - (left ? 1 : 0);
    const boostOn = !!keys[' '] && forward && car.boost > 0 && car.speed > 28;

    const road = nearestRoad(car);
    car.roadDistance = road.distance;
    car.offRoad = road.distance > road.width * .58;
    const grip = car.offRoad ? .48 : 1;
    const maxSpeed = boostOn ? 390 : 276;
    const acceleration = boostOn ? 245 : 180;

    if (throttle !== 0) {
      car.speed += throttle * acceleration * dt * grip;
    } else {
      car.speed *= Math.pow(car.offRoad ? .89 : .935, dt * 60);
    }
    if (boostOn) car.boost = Math.max(0, car.boost - dt * 22);
    else car.boost = Math.min(100, car.boost + dt * 6.5);
    car.speed = clamp(car.speed, -115, maxSpeed);

    const steerStrength = (0.72 + Math.min(Math.abs(car.speed) / 200, .72)) * (car.offRoad ? .65 : 1);
    if (steering) car.angle += steering * steerStrength * dt * (car.speed >= 0 ? 1 : -1);

    // A small amount of momentum makes the boxy car feel weighty without needing a physics package.
    car.x += Math.cos(car.angle) * car.speed * dt;
    car.y += Math.sin(car.angle) * car.speed * dt;
    car.x = clamp(car.x, worldBounds.left, worldBounds.right);
    car.y = clamp(car.y, worldBounds.top, worldBounds.bottom);

    if (car.speed > 6 && car.checkpoint < raceCheckpoints.length - 1) {
      const nextCheckpoint = raceCheckpoints[car.checkpoint + 1];
      if (Math.hypot(car.x - nextCheckpoint.x, car.y - nextCheckpoint.y) < 76) {
        car.checkpoint += 1;
        showIncident(car.checkpoint === raceCheckpoints.length - 1 ? 'EXIT GATE AHEAD' : `SECTOR ${pad(car.checkpoint + 1)} CLEARED`, car.checkpoint === raceCheckpoints.length - 1 ? 'Finish line acquired. Keep it clean.' : 'Crosscut registered. Find the next beam marker.', 'info');
      }
    }

    carts.forEach((cart) => {
      cart.distance = (cart.distance + cart.speed * dt) % trafficRoutes[cart.routeIndex].total;
      const position = pointAtRoute(trafficRoutes[cart.routeIndex], cart.offset + cart.distance);
      cart.x = position.x; cart.y = position.y; cart.angle = position.angle;
      cart.hitCooldown = Math.max(0, cart.hitCooldown - dt);
      if (Math.hypot(car.x - cart.x, car.y - cart.y) < 43 && cart.hitCooldown <= 0) {
        cart.hitCooldown = 2.4;
        car.health = Math.max(0, car.health - 15);
        car.speed *= -.28;
        car.x -= Math.cos(cart.angle) * 18;
        car.y -= Math.sin(cart.angle) * 18;
        car.hitFlash = 1;
        showIncident('IMPACT // CART', 'Chassis damage registered. Give the ore line room.', 'danger');
      }
    });

    updateTrafficWarning();
    const cameraTargetX = car.x + Math.cos(car.angle) * 108;
    const cameraTargetY = car.y + Math.sin(car.angle) * 68;
    const cameraEase = 1 - Math.pow(.0008, dt);
    camera.x = lerp(camera.x, cameraTargetX, cameraEase);
    camera.y = lerp(camera.y, cameraTargetY, cameraEase);
    updateInterface();
  }

  function updateTrafficWarning() {
    let nearest = null;
    carts.forEach((cart) => {
      const dx = cart.x - car.x;
      const dy = cart.y - car.y;
      const d = Math.hypot(dx, dy);
      const ahead = dx * Math.cos(car.angle) + dy * Math.sin(car.angle);
      if (ahead > -46 && d < 210 && (!nearest || d < nearest.d)) nearest = { cart, d, ahead };
    });
    const shouldWarn = !!nearest && nearest.d < 172;
    if (shouldWarn && !warningActive && toastTimer <= 0) {
      const detail = nearest.d < 88 ? 'Crossing now. Brake or take the next cut.' : 'Ore line inbound. Check the crossing.';
      showIncident('CART INBOUND', detail, 'warning');
    }
    warningActive = shouldWarn;
    if (toastTimer <= 0 && (!shouldWarn || toastType !== 'warning')) $('incidentToast').classList.remove('visible');
    if (shouldWarn && toastType === 'warning') {
      $('incidentTitle').textContent = nearest.d < 80 ? 'CROSSING ACTIVE' : 'CART INBOUND';
      $('incidentDetail').textContent = nearest.d < 80 ? 'Brake or take the next cut.' : 'Ore line inbound. Check the crossing.';
    }
  }

  function showIncident(title, detail, type = 'warning') {
    toastType = type;
    toastTimer = type === 'info' ? 2.4 : 2.1;
    $('incidentTitle').textContent = title;
    $('incidentDetail').textContent = detail;
    const toast = $('incidentToast');
    toast.classList.toggle('is-info', type === 'info');
    toast.classList.toggle('is-danger', type === 'danger');
    toast.classList.add('visible');
  }

  function updateInterface() {
    const kmh = Math.abs(car.speed) * .36;
    $('speedValue').textContent = pad(kmh, 3);
    $('gearValue').textContent = car.speed < -4 ? 'R' : car.speed > 5 ? 'D' : 'N';
    $('boostValue').textContent = `${Math.round(car.boost)}%`;
    $('healthValue').textContent = `${Math.round(car.health)}%`;
    $('boostBar').style.width = `${car.boost}%`;
    $('healthBar').style.width = `${car.health}%`;
    $('sectorValue').textContent = pad(Math.min(car.checkpoint + 1, 8));
    const progress = (car.checkpoint / (raceCheckpoints.length - 1)) * 100;
    $('routeProgress').style.width = `${Math.max(3, progress)}%`;
    $('trackStatus').textContent = car.health <= 0 ? 'CHASSIS CRITICAL' : car.offRoad ? 'OFF ROUTE' : car.checkpoint === raceCheckpoints.length - 1 ? 'EXIT GATE' : 'TRACK LIVE';
    $('tractionState').textContent = car.offRoad ? 'GRIP / LOOSE' : boostActive() ? 'GRIP / BOOST' : 'GRIP / GOOD';
    $('surfaceValue').textContent = car.offRoad ? 'LOOSE SHALE' : 'GROOVED PAVING';
    $('cartCount').textContent = pad(carts.length);
    $('timer').textContent = formatTime(elapsed);
  }

  function boostActive() { return !!keys[' '] && (keys.w || keys.arrowup) && car.boost > 0 && car.speed > 28; }
  function formatTime(time) {
    const minutes = Math.floor(time / 60);
    const seconds = time % 60;
    return `${pad(minutes)}:${seconds.toFixed(2).padStart(5, '0')}`;
  }

  // Main scene --------------------------------------------------------------
  function render(time) {
    ctx.setTransform(viewport.dpr, 0, 0, viewport.dpr, 0, 0);
    ctx.clearRect(0, 0, viewport.width, viewport.height);
    drawBackdrop(time);

    const sceneScale = clamp(Math.min(viewport.width / 1290, viewport.height / 760), .65, 1.04);
    ctx.save();
    ctx.translate(viewport.width / 2 - camera.x * sceneScale, viewport.height / 2 - camera.y * sceneScale);
    ctx.scale(sceneScale, sceneScale);
    drawMine(time);
    ctx.restore();

    drawScreenAtmosphere(time);
    renderMinimap();
  }

  function drawBackdrop(time) {
    const gradient = ctx.createRadialGradient(viewport.width * .49, viewport.height * .48, 60, viewport.width * .5, viewport.height * .5, Math.max(viewport.width, viewport.height) * .75);
    gradient.addColorStop(0, '#162b2d');
    gradient.addColorStop(.44, '#0c1a1d');
    gradient.addColorStop(1, '#04090b');
    ctx.fillStyle = gradient;
    ctx.fillRect(0, 0, viewport.width, viewport.height);
    ctx.globalAlpha = .13;
    ctx.strokeStyle = '#88aaa5';
    ctx.lineWidth = 1;
    const drift = (time * .004) % 32;
    for (let x = -viewport.height; x < viewport.width + viewport.height; x += 32) {
      ctx.beginPath(); ctx.moveTo(x + drift, 0); ctx.lineTo(x - viewport.height + drift, viewport.height); ctx.stroke();
    }
    ctx.globalAlpha = 1;
  }

  function drawMine(time) {
    ctx.fillStyle = '#0a1214';
    ctx.fillRect(-500, -400, 2700, 1800);

    ctx.save();
    ctx.beginPath();
    caveShape.forEach(([x, y], index) => index ? ctx.lineTo(x, y) : ctx.moveTo(x, y));
    ctx.closePath();
    ctx.fillStyle = '#101e1f';
    ctx.fill();
    ctx.strokeStyle = 'rgba(149, 184, 174, .22)';
    ctx.lineWidth = 3;
    ctx.stroke();
    ctx.restore();

    drawCeilingRibs();
    drawDust(time);
    rocks.forEach(drawRock);
    roads.forEach(drawRoad);
    drawJunctions();
    drawSupportsAndLights(time);
    drawRouteDetails();
    drawFinishGate();
    carts.forEach((cart) => drawCart(cart, time));
    drawPlayerCar(time);
  }

  function drawCeilingRibs() {
    ctx.save();
    ctx.globalAlpha = .22;
    ctx.strokeStyle = '#314544';
    ctx.lineWidth = 1.5;
    for (let x = 80; x < 1640; x += 145) {
      ctx.beginPath();
      ctx.moveTo(x - 70, 65);
      ctx.quadraticCurveTo(x + 35, 140, x - 10, 232);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(x + 35, 780);
      ctx.quadraticCurveTo(x + 100, 850, x + 30, 958);
      ctx.stroke();
    }
    ctx.globalAlpha = .15;
    ctx.lineWidth = 7;
    ctx.strokeStyle = '#071012';
    caveShape.forEach(([x, y], index) => {
      if (index === caveShape.length - 1) return;
      const next = caveShape[index + 1];
      ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(next[0], next[1]); ctx.stroke();
    });
    ctx.restore();
  }

  function drawDust(time) {
    ctx.save();
    for (const mote of dust) {
      const bob = Math.sin(time * .0007 + mote.phase) * 3;
      ctx.globalAlpha = mote.a * (.65 + Math.sin(time * .001 + mote.phase) * .35);
      ctx.fillStyle = '#abc5bc';
      ctx.beginPath(); ctx.arc(mote.x, mote.y + bob, mote.r, 0, TAU); ctx.fill();
    }
    ctx.restore();
  }

  function drawRock(rock) {
    ctx.save();
    ctx.translate(rock.x, rock.y);
    ctx.rotate(rock.rotation);
    ctx.shadowColor = 'rgba(0, 0, 0, .55)'; ctx.shadowBlur = 15; ctx.shadowOffsetY = 8;
    const colors = rock.tone > .64 ? ['#182526', '#2a3937', '#42504a'] : rock.tone > .3 ? ['#111c1e', '#243230', '#35413d'] : ['#0f191b', '#202c2b', '#303b37'];
    ctx.fillStyle = colors[0];
    ctx.beginPath();
    for (let i = 0; i < 7; i += 1) {
      const angle = (i / 7) * TAU;
      const radius = rock.r * (.74 + ((i * 13) % 7) / 18);
      const x = Math.cos(angle) * radius;
      const y = Math.sin(angle) * radius * .72;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.closePath(); ctx.fill();
    ctx.shadowColor = 'transparent';
    ctx.strokeStyle = colors[1]; ctx.lineWidth = 2; ctx.stroke();
    ctx.globalAlpha = .55; ctx.fillStyle = colors[2];
    ctx.beginPath(); ctx.moveTo(-rock.r * .34, -rock.r * .15); ctx.lineTo(rock.r * .12, -rock.r * .44); ctx.lineTo(rock.r * .34, -rock.r * .04); ctx.lineTo(-rock.r * .05, rock.r * .08); ctx.closePath(); ctx.fill();
    ctx.restore();
  }

  function drawRoad(road) {
    const points = road.points;
    strokePolyline(points, road.width + 21, 'rgba(0, 0, 0, .46)');
    strokePolyline(points, road.width + 8, '#202b2a');
    strokePolyline(points, road.width, '#171e1e');
    strokePolyline(points, road.width - 10, '#1c2624');
    strokePolyline(points, road.width - 26, 'rgba(27, 36, 34, .82)');

    // Steel-edged service paving.
    strokePolyline(points, road.width - 8, 'rgba(88, 100, 91, .21)', 1.5);
    drawGrooves(points, road.width);
    drawPavingScars(points, road.width);
  }

  function strokePolyline(points, width, color, lineWidth = width) {
    ctx.save(); ctx.beginPath();
    points.forEach(([x, y], index) => index ? ctx.lineTo(x, y) : ctx.moveTo(x, y));
    ctx.lineCap = 'round'; ctx.lineJoin = 'round'; ctx.lineWidth = lineWidth; ctx.strokeStyle = color; ctx.stroke(); ctx.restore();
  }

  function drawGrooves(points, width) {
    const offsets = [-17, 17];
    offsets.forEach((offset) => {
      ctx.save(); ctx.lineCap = 'round'; ctx.lineJoin = 'round';
      ctx.strokeStyle = 'rgba(3, 8, 9, .98)'; ctx.lineWidth = 6;
      ctx.beginPath();
      for (let i = 0; i < points.length - 1; i += 1) {
        const [x1, y1] = points[i]; const [x2, y2] = points[i + 1];
        const length = Math.hypot(x2 - x1, y2 - y1) || 1; const nx = -(y2 - y1) / length; const ny = (x2 - x1) / length;
        if (i === 0) ctx.moveTo(x1 + nx * offset, y1 + ny * offset); else ctx.lineTo(x1 + nx * offset, y1 + ny * offset);
        ctx.lineTo(x2 + nx * offset, y2 + ny * offset);
      }
      ctx.stroke();
      ctx.strokeStyle = 'rgba(126, 139, 126, .42)'; ctx.lineWidth = 1.3;
      ctx.stroke(); ctx.restore();
    });

    // Short ties make the repeated mine-cart grooves read clearly at junctions.
    for (let i = 0; i < points.length - 1; i += 1) {
      const [x1, y1] = points[i]; const [x2, y2] = points[i + 1];
      const segmentLength = Math.hypot(x2 - x1, y2 - y1); const angle = Math.atan2(y2 - y1, x2 - x1);
      const count = Math.floor(segmentLength / 37);
      for (let j = 1; j < count; j += 1) {
        const t = j / count; const x = lerp(x1, x2, t); const y = lerp(y1, y2, t);
        ctx.save(); ctx.translate(x, y); ctx.rotate(angle);
        ctx.strokeStyle = 'rgba(62, 70, 64, .32)'; ctx.lineWidth = 2;
        ctx.beginPath(); ctx.moveTo(0, -30); ctx.lineTo(0, 30); ctx.stroke();
        ctx.restore();
      }
    }
  }

  function drawPavingScars(points, width) {
    ctx.save();
    ctx.strokeStyle = 'rgba(127, 151, 137, .15)'; ctx.lineWidth = 1;
    for (let i = 0; i < points.length - 1; i += 1) {
      const [x1, y1] = points[i]; const [x2, y2] = points[i + 1];
      const angle = Math.atan2(y2 - y1, x2 - x1); const length = Math.hypot(x2 - x1, y2 - y1);
      for (let d = 18; d < length - 15; d += 67) {
        const t = d / length; const x = lerp(x1, x2, t); const y = lerp(y1, y2, t);
        ctx.save(); ctx.translate(x, y); ctx.rotate(angle + Math.PI / 2);
        ctx.beginPath(); ctx.moveTo(-width * .28, 0); ctx.lineTo(width * .28, 0); ctx.stroke(); ctx.restore();
      }
    }
    ctx.restore();
  }

  function drawJunctions() {
    const nodes = [[320, 680], [518, 412], [760, 412], [1042, 392], [1146, 646], [760, 202], [518, 680], [995, 692]];
    nodes.forEach(([x, y]) => {
      ctx.save();
      ctx.globalAlpha = .22;
      ctx.fillStyle = '#94afa3'; ctx.beginPath(); ctx.arc(x, y, 33, 0, TAU); ctx.fill();
      ctx.globalAlpha = .34; ctx.strokeStyle = '#99b9aa'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.arc(x, y, 27, 0, TAU); ctx.stroke();
      ctx.restore();
    });
  }

  function drawSupportsAndLights(time) {
    lights.forEach((light) => drawLight(light, time));
    supports.forEach((support) => drawSupport(support));
  }

  function drawLight(light, time) {
    const pulse = .86 + Math.sin(time * .003 + light.phase) * .08;
    ctx.save();
    ctx.globalCompositeOperation = 'screen';
    const glow = ctx.createRadialGradient(light.x, light.y, 2, light.x, light.y, 94);
    glow.addColorStop(0, `rgba(176, 244, 219, ${.23 * pulse})`);
    glow.addColorStop(.32, `rgba(93, 192, 169, ${.08 * pulse})`);
    glow.addColorStop(1, 'rgba(62, 122, 112, 0)');
    ctx.fillStyle = glow; ctx.beginPath(); ctx.arc(light.x, light.y, 94, 0, TAU); ctx.fill();
    const beam = 130;
    const spread = .42;
    ctx.fillStyle = `rgba(142, 231, 203, ${.035 * pulse})`;
    ctx.beginPath(); ctx.moveTo(light.x, light.y); ctx.lineTo(light.x + Math.cos(light.angle - spread) * beam, light.y + Math.sin(light.angle - spread) * beam); ctx.lineTo(light.x + Math.cos(light.angle + spread) * beam, light.y + Math.sin(light.angle + spread) * beam); ctx.closePath(); ctx.fill();
    ctx.globalCompositeOperation = 'source-over';
    ctx.fillStyle = '#a8ffe5'; ctx.shadowColor = '#9effdb'; ctx.shadowBlur = 10;
    ctx.beginPath(); ctx.arc(light.x, light.y, 3.2, 0, TAU); ctx.fill();
    ctx.restore();
  }

  function drawSupport(support) {
    const { x, y, angle } = support;
    ctx.save(); ctx.translate(x, y); ctx.rotate(angle);
    ctx.globalAlpha = .7;
    ctx.fillStyle = 'rgba(0,0,0,.48)';
    ctx.fillRect(-6, -58, 13, 18); ctx.fillRect(-6, 40, 13, 18);
    ctx.strokeStyle = '#0a1010'; ctx.lineWidth = 14; ctx.lineCap = 'square'; ctx.beginPath(); ctx.moveTo(0, -62); ctx.lineTo(0, 62); ctx.stroke();
    ctx.strokeStyle = '#4c5f57'; ctx.lineWidth = 8; ctx.beginPath(); ctx.moveTo(0, -62); ctx.lineTo(0, 62); ctx.stroke();
    ctx.strokeStyle = '#82948a'; ctx.lineWidth = 1.5; ctx.beginPath(); ctx.moveTo(-2, -61); ctx.lineTo(-2, 61); ctx.stroke();
    // roof brace, a second member set back from the light bar
    ctx.strokeStyle = 'rgba(100, 129, 116, .55)'; ctx.lineWidth = 4; ctx.beginPath(); ctx.moveTo(-18, -55); ctx.lineTo(18, -55); ctx.stroke();
    ctx.strokeStyle = 'rgba(25, 40, 36, .9)'; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(-17, -54); ctx.lineTo(17, -54); ctx.stroke();
    ctx.fillStyle = '#1d302d'; ctx.fillRect(-9, -68, 18, 8);
    ctx.fillStyle = '#b5f6d9'; ctx.shadowColor = '#93e2c6'; ctx.shadowBlur = 7; ctx.beginPath(); ctx.arc(0, -63, 2.5, 0, TAU); ctx.fill();
    ctx.restore();
  }

  function drawRouteDetails() {
    const labels = [
      [196, 854, 'ORE LINE 05', 0], [350, 659, 'CROSSCUT // 01', -Math.PI / 2], [550, 391, 'BEAMMARK 02', 0],
      [788, 181, 'NORTH HAUL', 0], [1067, 370, 'CROSSCUT // 03', 0], [1290, 625, 'DEEP LEVEL', 0],
      [720, 807, 'LOWER BYPASS', .1]
    ];
    ctx.save();
    labels.forEach(([x, y, label, angle]) => {
      ctx.save(); ctx.translate(x, y); ctx.rotate(angle); ctx.fillStyle = 'rgba(178, 217, 201, .35)'; ctx.font = '8px "DM Mono", monospace'; ctx.letterSpacing = '1px'; ctx.fillText(label, 0, 0); ctx.restore();
    });
    ctx.restore();
  }

  function drawFinishGate() {
    const x = 1518; const y = 874; const angle = 0;
    ctx.save(); ctx.translate(x, y); ctx.rotate(angle);
    ctx.globalAlpha = .85; ctx.strokeStyle = '#6fb9b5'; ctx.lineWidth = 2; ctx.setLineDash([7, 5]); ctx.beginPath(); ctx.moveTo(0, -44); ctx.lineTo(0, 44); ctx.stroke(); ctx.setLineDash([]);
    ctx.fillStyle = 'rgba(141, 232, 227, .18)'; ctx.fillRect(-2, -46, 4, 92);
    ctx.fillStyle = '#a8fff0'; ctx.shadowColor = '#8de8e3'; ctx.shadowBlur = 12; ctx.beginPath(); ctx.arc(0, -48, 4, 0, TAU); ctx.fill();
    ctx.shadowBlur = 0; ctx.fillStyle = '#bafbf0'; ctx.font = '9px "DM Mono", monospace'; ctx.fillText('EXIT GATE', 12, -57);
    ctx.restore();
  }

  function drawCart(cart, time) {
    ctx.save(); ctx.translate(cart.x, cart.y); ctx.rotate(cart.angle);
    ctx.globalAlpha = .55; ctx.fillStyle = '#020607'; ctx.beginPath(); ctx.ellipse(0, 19, 39, 13, 0, 0, TAU); ctx.fill();
    // wheels and low undercarriage
    ctx.fillStyle = '#090f10'; ctx.fillRect(-24, -22, 12, 8); ctx.fillRect(12, -22, 12, 8); ctx.fillRect(-24, 14, 12, 8); ctx.fillRect(12, 14, 12, 8);
    ctx.strokeStyle = '#5a4d3e'; ctx.lineWidth = 3; ctx.beginPath(); ctx.moveTo(-30, -18); ctx.lineTo(31, -18); ctx.moveTo(-30, 18); ctx.lineTo(31, 18); ctx.stroke();
    const body = cart.color === 'orange' ? '#a9552e' : cart.color === 'pale' ? '#6b806e' : '#b17a38';
    ctx.fillStyle = '#35271f'; ctx.fillRect(-28, -15, 57, 30);
    ctx.fillStyle = body; ctx.fillRect(-22, -12, 45, 24);
    ctx.fillStyle = cart.color === 'pale' ? '#9fbda0' : '#e0a04e';
    ctx.globalAlpha = .72; ctx.beginPath(); ctx.moveTo(-18, -8); ctx.lineTo(-8, -14); ctx.lineTo(5, -9); ctx.lineTo(17, -13); ctx.lineTo(21, 5); ctx.lineTo(-20, 7); ctx.closePath(); ctx.fill(); ctx.globalAlpha = 1;
    ctx.strokeStyle = 'rgba(255,224,157,.45)'; ctx.lineWidth = 1; ctx.strokeRect(-22, -12, 45, 24);
    ctx.fillStyle = '#f7c168'; ctx.shadowColor = '#ffbd57'; ctx.shadowBlur = 9; ctx.beginPath(); ctx.arc(28, -8, 3, 0, TAU); ctx.arc(28, 8, 3, 0, TAU); ctx.fill();
    ctx.shadowBlur = 0; ctx.fillStyle = '#0a1111'; ctx.font = '7px "DM Mono", monospace'; ctx.fillText(cart.id, -17, 3);
    if (cart.hitCooldown > 1.6) { ctx.strokeStyle = '#ff8060'; ctx.lineWidth = 2; ctx.strokeRect(-35, -25, 70, 50); }
    ctx.restore();
  }

  function drawPlayerCar(time) {
    ctx.save(); ctx.translate(car.x, car.y); ctx.rotate(car.angle);
    ctx.globalAlpha = .6; ctx.fillStyle = '#010405'; ctx.beginPath(); ctx.ellipse(-2, 24, 50, 24, 0, 0, TAU); ctx.fill();
    // chunky wheels, slightly offset to read as a boxy vehicle from above
    ctx.fillStyle = '#050a0b';
    roundedRect(ctx, -27, -27, 20, 12, 3); roundedRect(ctx, 14, -27, 20, 12, 3); roundedRect(ctx, -27, 15, 20, 12, 3); roundedRect(ctx, 14, 15, 20, 12, 3);
    ctx.strokeStyle = '#34484a'; ctx.lineWidth = 2; ctx.strokeRect(-27, -27, 20, 12); ctx.strokeRect(14, -27, 20, 12); ctx.strokeRect(-27, 15, 20, 12); ctx.strokeRect(14, 15, 20, 12);

    const body = ctx.createLinearGradient(-38, 0, 40, 0); body.addColorStop(0, '#213a42'); body.addColorStop(.46, '#4f808a'); body.addColorStop(1, '#a8e4dc');
    ctx.fillStyle = body; ctx.strokeStyle = '#c2fff1'; ctx.lineWidth = 1.4;
    ctx.beginPath(); ctx.moveTo(39, -18); ctx.lineTo(28, -24); ctx.lineTo(-25, -21); ctx.lineTo(-36, -13); ctx.lineTo(-36, 13); ctx.lineTo(-25, 21); ctx.lineTo(28, 24); ctx.lineTo(39, 17); ctx.closePath(); ctx.fill(); ctx.stroke();
    // cabin and glass
    ctx.fillStyle = '#101e24'; ctx.strokeStyle = 'rgba(193, 252, 242, .62)'; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(11, -17); ctx.lineTo(26, -14); ctx.lineTo(29, 14); ctx.lineTo(11, 17); ctx.closePath(); ctx.fill(); ctx.stroke();
    ctx.fillStyle = 'rgba(131, 220, 225, .34)'; ctx.beginPath(); ctx.moveTo(13, -13); ctx.lineTo(24, -11); ctx.lineTo(25, -2); ctx.lineTo(13, -2); ctx.closePath(); ctx.fill(); ctx.beginPath(); ctx.moveTo(13, 2); ctx.lineTo(25, 2); ctx.lineTo(24, 11); ctx.lineTo(13, 13); ctx.closePath(); ctx.fill();
    // hood panel, doors, bumpers and marker lights
    ctx.strokeStyle = 'rgba(205,255,242,.42)'; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(-26, -17); ctx.lineTo(7, -14); ctx.moveTo(-26, 17); ctx.lineTo(7, 14); ctx.moveTo(-4, -19); ctx.lineTo(-4, 19); ctx.stroke();
    ctx.fillStyle = '#dffff1'; ctx.shadowColor = '#a7fff1'; ctx.shadowBlur = 12; ctx.beginPath(); ctx.arc(36, -10, 3.1, 0, TAU); ctx.arc(36, 10, 3.1, 0, TAU); ctx.fill();
    ctx.shadowBlur = 0; ctx.fillStyle = '#f07059'; ctx.beginPath(); ctx.arc(-34, -10, 2, 0, TAU); ctx.arc(-34, 10, 2, 0, TAU); ctx.fill();
    ctx.fillStyle = '#14282c'; ctx.fillRect(-39, -5, 5, 10);
    // roof rail, the tiny extra detail that sells the industrial boxcar silhouette
    ctx.strokeStyle = '#d0fff0'; ctx.globalAlpha = .62; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(2, -19); ctx.lineTo(21, -20); ctx.moveTo(2, 19); ctx.lineTo(21, 20); ctx.stroke();
    if (boostActive()) { ctx.globalAlpha = .65; ctx.fillStyle = '#84f6ff'; ctx.shadowColor = '#84f6ff'; ctx.shadowBlur = 12; ctx.beginPath(); ctx.moveTo(-40, -9); ctx.lineTo(-59, -5); ctx.lineTo(-42, 0); ctx.lineTo(-59, 5); ctx.lineTo(-40, 9); ctx.closePath(); ctx.fill(); }
    if (car.hitFlash > 0) { ctx.globalAlpha = car.hitFlash * .8; ctx.strokeStyle = '#ff8060'; ctx.lineWidth = 3; ctx.beginPath(); ctx.arc(0, 0, 48 + (1 - car.hitFlash) * 10, 0, TAU); ctx.stroke(); }
    ctx.restore();
  }

  function drawScreenAtmosphere(time) {
    const gradient = ctx.createRadialGradient(viewport.width / 2, viewport.height / 2, Math.min(viewport.width, viewport.height) * .16, viewport.width / 2, viewport.height / 2, Math.max(viewport.width, viewport.height) * .7);
    gradient.addColorStop(0, 'rgba(0,0,0,0)'); gradient.addColorStop(.74, 'rgba(0, 5, 6, .08)'); gradient.addColorStop(1, 'rgba(0, 2, 3, .64)');
    ctx.fillStyle = gradient; ctx.fillRect(0, 0, viewport.width, viewport.height);
    if (car.hitFlash > 0) { ctx.fillStyle = `rgba(255, 72, 50, ${car.hitFlash * .12})`; ctx.fillRect(0, 0, viewport.width, viewport.height); }
  }

  // Minimap -----------------------------------------------------------------
  function renderMinimap() {
    mapCtx.setTransform(viewport.dpr, 0, 0, viewport.dpr, 0, 0);
    mapCtx.clearRect(0, 0, mapWidth, mapHeight);
    mapCtx.fillStyle = 'rgba(2, 8, 9, .88)'; mapCtx.fillRect(0, 0, mapWidth, mapHeight);
    const padMap = 10;
    const sx = (mapWidth - padMap * 2) / (worldBounds.right - worldBounds.left);
    const sy = (mapHeight - padMap * 2) / (worldBounds.bottom - worldBounds.top);
    const scale = Math.min(sx, sy);
    const ox = (mapWidth - (worldBounds.right - worldBounds.left) * scale) / 2 - worldBounds.left * scale;
    const oy = (mapHeight - (worldBounds.bottom - worldBounds.top) * scale) / 2 - worldBounds.top * scale;
    const toMap = (x, y) => ({ x: ox + x * scale, y: oy + y * scale });

    // subtle mine boundary
    mapCtx.save(); mapCtx.strokeStyle = 'rgba(141, 232, 227, .12)'; mapCtx.lineWidth = 1; mapCtx.beginPath();
    caveShape.forEach(([x, y], index) => { const p = toMap(x, y); index ? mapCtx.lineTo(p.x, p.y) : mapCtx.moveTo(p.x, p.y); }); mapCtx.closePath(); mapCtx.stroke(); mapCtx.restore();

    roads.forEach((road) => {
      mapCtx.save(); mapCtx.lineCap = 'round'; mapCtx.lineJoin = 'round'; mapCtx.beginPath();
      road.points.forEach(([x, y], index) => { const p = toMap(x, y); index ? mapCtx.lineTo(p.x, p.y) : mapCtx.moveTo(p.x, p.y); });
      mapCtx.strokeStyle = 'rgba(105, 133, 124, .24)'; mapCtx.lineWidth = Math.max(3, road.width * scale + 2); mapCtx.stroke();
      mapCtx.strokeStyle = 'rgba(24, 43, 42, .96)'; mapCtx.lineWidth = Math.max(2, road.width * scale); mapCtx.stroke();
      mapCtx.restore();
    });
    mapCtx.save(); mapCtx.beginPath(); mainRoute.points.forEach(([x, y], index) => { const p = toMap(x, y); index ? mapCtx.lineTo(p.x, p.y) : mapCtx.moveTo(p.x, p.y); }); mapCtx.strokeStyle = '#6cb8ff'; mapCtx.globalAlpha = .9; mapCtx.lineWidth = 1.5; mapCtx.setLineDash([3, 3]); mapCtx.stroke(); mapCtx.restore();
    raceCheckpoints.forEach((checkpoint, index) => { const p = toMap(checkpoint.x, checkpoint.y); mapCtx.fillStyle = index <= car.checkpoint ? '#8de8e3' : 'rgba(141, 232, 227, .35)'; mapCtx.beginPath(); mapCtx.arc(p.x, p.y, index === car.checkpoint + 1 ? 2.8 : 1.4, 0, TAU); mapCtx.fill(); });
    carts.forEach((cart) => { const p = toMap(cart.x, cart.y); mapCtx.fillStyle = '#f5b24b'; mapCtx.shadowColor = '#f5b24b'; mapCtx.shadowBlur = 6; mapCtx.beginPath(); mapCtx.arc(p.x, p.y, 2.6, 0, TAU); mapCtx.fill(); mapCtx.shadowBlur = 0; });
    const player = toMap(car.x, car.y);
    mapCtx.save(); mapCtx.translate(player.x, player.y); mapCtx.rotate(car.angle); mapCtx.fillStyle = '#d2fff7'; mapCtx.shadowColor = '#8de8e3'; mapCtx.shadowBlur = 8; mapCtx.beginPath(); mapCtx.moveTo(6, 0); mapCtx.lineTo(-4, -4); mapCtx.lineTo(-2, 0); mapCtx.lineTo(-4, 4); mapCtx.closePath(); mapCtx.fill(); mapCtx.restore();
  }

  function roundedRect(context, x, y, width, height, radius) {
    const r = Math.min(radius, width / 2, height / 2);
    context.beginPath(); context.moveTo(x + r, y); context.arcTo(x + width, y, x + width, y + height, r); context.arcTo(x + width, y + height, x, y + height, r); context.arcTo(x, y + height, x, y, r); context.arcTo(x, y, x + width, y, r); context.closePath(); context.fill();
  }

  resize();
  window.addEventListener('resize', resize);
  carts.forEach((cart) => { const start = pointAtRoute(trafficRoutes[cart.routeIndex], cart.offset); cart.x = start.x; cart.y = start.y; cart.angle = start.angle; });

  function frame(now) {
    const dt = Math.min(.045, Math.max(.001, (now - lastTime) / 1000));
    lastTime = now;
    update(dt);
    render(now);
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
})();
