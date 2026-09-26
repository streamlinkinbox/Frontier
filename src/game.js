import * as THREE from 'three';
import { WORLD, PLAYER, CAR } from './layout.js';
import { terrainHeight } from './terrain.js';
import { hud } from './hud.js';

// ---------------------------------------------------------------------------
// Game logic: on-foot controller, drivable car, sentry gun AI, exploding
// mines, rising tide, health / win / lose.
// ---------------------------------------------------------------------------

const UP = new THREE.Vector3(0, 1, 0);
const tmpV = new THREE.Vector3();
const tmpV2 = new THREE.Vector3();

export function createGame({ scene, camera, ocean, sentries, tankMines, apMines, tanks, car }) {
  const state = {
    playing: false,
    over: false,
    time: 0,
    keys: Object.create(null),
    player: {
      pos: new THREE.Vector3(PLAYER.spawn.x, 0, PLAYER.spawn.z),
      vel: new THREE.Vector3(),
      yaw: PLAYER.spawn.yaw,
      pitch: -0.06,
      onGround: true,
      hp: 100,
    },
    carState: {
      yaw: CAR.spawn.yaw,
      speed: 0,
      hp: CAR.hp,
      occupied: false,
      dead: false,
      respawn: 0,
    },
    shake: 0,
  };

  car.position.set(CAR.spawn.x, terrainHeight(CAR.spawn.x, CAR.spawn.z) + 0.12, CAR.spawn.z);
  car.rotation.y = state.carState.yaw;
  state.player.pos.y = terrainHeight(PLAYER.spawn.x, PLAYER.spawn.z);

  // ---------------------------------------------------------------- effects
  const effects = [];
  const flashLight = new THREE.PointLight('#ffb36b', 0, 30, 1.8);
  scene.add(flashLight);

  const particleGeo = new THREE.BoxGeometry(0.16, 0.16, 0.16);
  const particleMats = [
    new THREE.MeshBasicMaterial({ color: '#ffb36b' }),
    new THREE.MeshBasicMaterial({ color: '#e2662e' }),
    new THREE.MeshBasicMaterial({ color: '#5c5c58' }),
    new THREE.MeshBasicMaterial({ color: '#2f2c28' }),
  ];
  const tracerGeo = new THREE.BoxGeometry(0.045, 0.045, 1);
  const tracerMat = new THREE.MeshBasicMaterial({ color: '#ffd98c', transparent: true, opacity: 1 });

  function explode(x, y, z, big = true) {
    // debris particles
    const n = big ? 26 : 14;
    for (let i = 0; i < n; i++) {
      const m = new THREE.Mesh(particleGeo, particleMats[i % particleMats.length]);
      m.position.set(x, y + 0.3, z);
      const dir = new THREE.Vector3(
        (Math.random() - 0.5) * 2,
        Math.random() * 1.6,
        (Math.random() - 0.5) * 2,
      ).normalize();
      const sp = (big ? 6.5 : 3.5) * (0.4 + Math.random());
      effects.push({
        type: 'p', mesh: m, vel: dir.multiplyScalar(sp), life: 0.9 + Math.random() * 0.5, t: 0,
      });
      scene.add(m);
    }

    // flash + shockwave ring
    flashLight.position.set(x, y + 1.4, z);
    flashLight.intensity = big ? 260 : 110;

    const ring = new THREE.Mesh(
      new THREE.RingGeometry(0.2, 0.5, 22),
      new THREE.MeshBasicMaterial({ color: '#e8d5a8', transparent: true, opacity: 0.85, side: THREE.DoubleSide }),
    );
    ring.rotation.x = -Math.PI / 2;
    ring.position.set(x, terrainHeight(x, z) + 0.12, z);
    scene.add(ring);
    effects.push({ type: 'ring', mesh: ring, t: 0, life: 0.55 });

    // crater
    const crater = new THREE.Mesh(
      new THREE.CylinderGeometry(big ? 1.7 : 1.1, big ? 1.2 : 0.8, 0.12, 10),
      new THREE.MeshStandardMaterial({ color: '#4d4032', flatShading: true, roughness: 1 }),
    );
    crater.position.set(x, terrainHeight(x, z) + 0.04, z);
    scene.add(crater);

    // screen shake if close
    const d = state.player.pos.distanceTo(tmpV.set(x, y, z));
    if (d < 18) state.shake = Math.min(1, (18 - d) / 12);
  }

  const tracerTmp = new THREE.Vector3();
  const tracerTmp2 = new THREE.Vector3();

  function tracer(from, to) {
    const dir = tracerTmp.copy(to).sub(from);
    const len = dir.length();
    const m = new THREE.Mesh(tracerGeo, tracerMat.clone());
    m.position.copy(from).addScaledVector(dir, 0.5);
    m.lookAt(tracerTmp2.copy(from).add(dir)); // lookAt along dir, not at `to`
    m.scale.z = len;
    scene.add(m);
    effects.push({ type: 'tracer', mesh: m, t: 0, life: 0.09 });
  }

  function updateEffects(dt) {
    for (let i = effects.length - 1; i >= 0; i--) {
      const e = effects[i];
      e.t += dt;
      const k = e.t / e.life;
      if (e.type === 'p') {
        e.vel.y -= 12 * dt;
        e.mesh.position.addScaledVector(e.vel, dt);
        const s = Math.max(0.01, 1 - k);
        e.mesh.scale.setScalar(s);
      } else if (e.type === 'ring') {
        const s = 1 + k * 16;
        e.mesh.scale.setScalar(s);
        e.mesh.material.opacity = 0.85 * (1 - k);
      } else if (e.type === 'tracer') {
        e.mesh.material.opacity = 1 - k;
      }
      if (e.t >= e.life) {
        scene.remove(e.mesh);
        if (e.type === 'tracer') e.mesh.material.dispose();
        effects.splice(i, 1);
      }
    }
    flashLight.intensity *= Math.exp(-dt * 10);
  }

  // ---------------------------------------------------------------- mines
  function checkMines() {
    const pPos = state.player.pos;
    const cPos = car.position;

    for (const list of [tankMines, apMines]) {
      for (let i = list.length - 1; i >= 0; i--) {
        const m = list[i];
        if (m.exploded) continue;
        const dp = Math.hypot(pPos.x - m.x, pPos.z - m.z);
        const dc = Math.hypot(cPos.x - m.x, cPos.z - m.z);
        const trigger = Math.min(dp, dc);

        if (trigger < m.r + 0.45) {
          m.exploded = true;
          const y = terrainHeight(m.x, m.z);
          explode(m.x, y, m.z, true);

          if (m.mesh) m.mesh.visible = false;
          if (m.pin) m.pin.visible = false;

          // damage
          const pDmg = Math.max(0, 46 - dp * 6);
          if (!state.carState.occupied && pDmg > 0) damagePlayer(pDmg);
          const cDmg = Math.max(0, 38 - dc * 4.5);
          if (cDmg > 0 && !state.carState.dead) damageCar(cDmg);
        }
      }
    }
  }

  // ---------------------------------------------------------------- damage
  function damagePlayer(amount) {
    if (state.over) return;
    state.player.hp -= amount;
    hud.damage();
    if (state.player.hp <= 0) {
      state.player.hp = 0;
      endGame(false, 'You were cut down on the beach.');
    }
  }

  function damageCar(amount) {
    state.carState.hp -= amount;
    hud.damage();
    if (state.carState.hp <= 0 && !state.carState.dead) {
      state.carState.dead = true;
      state.carState.respawn = 9;
      explode(car.position.x, terrainHeight(car.position.x, car.position.z) + 0.6, car.position.z, true);
      if (state.carState.occupied) {
        state.carState.occupied = false;
        damagePlayer(28);
        state.player.pos.copy(car.position).add(new THREE.Vector3(1.6, 0, 0));
        state.player.vel.set(0, 2, 0);
      }
      car.visible = false;
    }
  }

  // ---------------------------------------------------------------- sentries
  function updateSentries(dt) {
    const targetIsCar = state.carState.occupied && !state.carState.dead;
    const targetPos = targetIsCar ? car.position : state.player.pos;

    for (const s of sentries) {
      const dx = targetPos.x - s.x;
      const dz = targetPos.z - s.z;
      const dist = Math.hypot(dx, dz);
      const inRange = dist < s.range && targetPos.z < WORLD.wallZ - 2;

      // aim: vehicles draw far more attention than infantry
      const wantsToAim = inRange && (targetIsCar || dist < s.range * 0.45);
      if (wantsToAim) {
        const targetYaw = Math.atan2(dx, dz);
        let diff = targetYaw - s.yaw;
        while (diff > Math.PI) diff -= Math.PI * 2;
        while (diff < -Math.PI) diff += Math.PI * 2;
        s.yaw += THREE.MathUtils.clamp(diff, -1.15 * dt, 1.15 * dt);
        s.group.rotation.y = s.yaw;

        s.fireTimer -= dt;
        const aimed = Math.abs(diff) < 0.16;
        if (aimed && s.fireTimer <= 0) {
          fireSentry(s, targetPos, dist, targetIsCar);
          s.fireTimer = 1.7 + Math.random() * 2.4;
        }
      } else {
        // idle: slowly sweep back to watch position
        let diff = s.homeYaw - s.yaw;
        while (diff > Math.PI) diff -= Math.PI * 2;
        while (diff < -Math.PI) diff += Math.PI * 2;
        s.yaw += THREE.MathUtils.clamp(diff, -0.25 * dt, 0.25 * dt);
        s.group.rotation.y = s.yaw;
      }
    }
  }

  function fireSentry(s, targetPos, dist, targetIsCar) {
    const muzzle = tmpV.set(s.x, s.y - 0.55, s.z)
      .add(new THREE.Vector3(Math.sin(s.yaw), 0, Math.cos(s.yaw)).multiplyScalar(1.15));

    // burst of 3 rounds
    for (let i = 0; i < 3; i++) {
      const spread = 0.018 * dist * (0.35 + Math.random());
      const to = tmpV2.copy(targetPos).add(new THREE.Vector3(
        (Math.random() - 0.5) * spread,
        (Math.random() - 0.4) * spread * 0.4 + 0.9,
        (Math.random() - 0.5) * spread,
      ));
      tracer(muzzle, to);
    }

    // hit resolution: vehicles are the preferred targets
    const acc = targetIsCar
      ? THREE.MathUtils.clamp(0.62 - dist / 320, 0.12, 0.62)
      : THREE.MathUtils.clamp(0.3 - dist / 420, 0.05, 0.3);

    if (Math.random() < acc) {
      if (targetIsCar) damageCar(6 + Math.random() * 5);
      else damagePlayer(7 + Math.random() * 6);
    }
  }

  // ---------------------------------------------------------------- movement
  function collideWall(pos, radius) {
    // great wall (z front face ≈ 150). Gate opening |x| < gate + pillar inner
    const zFront = WORLD.wallZ - 0.4;
    const zBack = WORLD.wallZ + 11.4;
    const gateX = WORLD.gateHalfWidth + 0.4;

    if (pos.z > zFront - radius && pos.z < zBack) {
      if (pos.x > -WORLD.wallHalfWidth - 6 && pos.x < -gateX) {
        pos.z = zFront - radius;
      } else if (pos.x < WORLD.wallHalfWidth + 6 && pos.x > gateX) {
        pos.z = zFront - radius;
      }
    }

    // big static props (tanks) — circle push-out
    for (const t of tanks) {
      const dx = pos.x - t.x, dz = pos.z - t.z;
      const d = Math.hypot(dx, dz);
      const min = t.r + radius;
      if (d < min && d > 0.001) {
        pos.x = t.x + (dx / d) * min;
        pos.z = t.z + (dz / d) * min;
      }
    }
  }

  function updatePlayer(dt) {
    const p = state.player;
    const k = state.keys;

    // camera looks along (-sin yaw, -cos yaw) with YXZ order
    const forward = tmpV.set(-Math.sin(p.yaw), 0, -Math.cos(p.yaw));
    const right = tmpV2.set(Math.cos(p.yaw), 0, -Math.sin(p.yaw));

    const wish = new THREE.Vector3();
    if (k['KeyW'] || k['ArrowUp']) wish.add(forward);
    if (k['KeyS'] || k['ArrowDown']) wish.sub(forward);
    if (k['KeyD'] || k['ArrowRight']) wish.add(right);
    if (k['KeyA'] || k['ArrowLeft']) wish.sub(right);
    if (wish.lengthSq() > 0) wish.normalize();

    const maxSpd = (k['ShiftLeft'] || k['ShiftRight']) ? PLAYER.sprint : PLAYER.walk;
    const accel = p.onGround ? 42 : 12;
    p.vel.x += (wish.x * maxSpd - p.vel.x) * Math.min(1, accel * dt / maxSpd);
    p.vel.z += (wish.z * maxSpd - p.vel.z) * Math.min(1, accel * dt / maxSpd);

    if ((k['Space']) && p.onGround) {
      p.vel.y = PLAYER.jump;
      p.onGround = false;
    }
    p.vel.y -= PLAYER.gravity * dt;

    p.pos.addScaledVector(p.vel, dt);
    collideWall(p.pos, PLAYER.radius);

    const groundY = terrainHeight(p.pos.x, p.pos.z);
    if (p.pos.y <= groundY) {
      p.pos.y = groundY;
      p.vel.y = 0;
      p.onGround = true;
    }

    // drowning / rising water
    const floor = groundY;
    if (floor < ocean.level - 0.22) {
      damagePlayer(14 * dt);
      if (Math.random() < dt * 0.6) hud.objective('Get above the waterline!');
    }

    camera.position.set(p.pos.x, p.pos.y + PLAYER.eye, p.pos.z);
    camera.rotation.set(p.pitch, p.yaw, 0, 'YXZ');
  }

  function updateCarDriving(dt) {
    const c = state.carState;
    const k = state.keys;
    const g = car;

    let throttle = 0;
    if (k['KeyW'] || k['ArrowUp']) throttle += 1;
    if (k['KeyS'] || k['ArrowDown']) throttle -= 1;
    let steer = 0;
    if (k['KeyA'] || k['ArrowLeft']) steer += 1;
    if (k['KeyD'] || k['ArrowRight']) steer -= 1;

    if (throttle > 0) c.speed += CAR.accel * dt * (1 - Math.abs(c.speed) / (CAR.maxSpeed * 1.25));
    else if (throttle < 0) {
      if (c.speed > 0.5) c.speed -= CAR.brake * dt;
      else c.speed -= CAR.accel * 0.55 * dt;
    }
    c.speed -= c.speed * CAR.drag * dt * 0.32;
    c.speed = THREE.MathUtils.clamp(c.speed, -7, CAR.maxSpeed);

    const steerEff = CAR.steer * THREE.MathUtils.clamp(Math.abs(c.speed) / 6, 0.18, 1) * Math.sign(c.speed || 1);
    c.yaw += steer * steerEff * dt;

    const fx = Math.sin(c.yaw), fz = Math.cos(c.yaw);
    g.position.x += fx * c.speed * dt;
    g.position.z += fz * c.speed * dt;
    collideWall(g.position, 2.1);

    // wheels stay on the ground
    const y = terrainHeight(g.position.x, g.position.z) + 0.12;
    g.position.y += (y - g.position.y) * Math.min(1, dt * 8);
    g.rotation.y = c.yaw;

    // subtle body roll / pitch
    g.rotation.z = THREE.MathUtils.lerp(g.rotation.z, -steer * c.speed * 0.006, dt * 6);
    g.rotation.x = THREE.MathUtils.lerp(g.rotation.x, -throttle * 0.018, dt * 6);

    // water floods the engine
    if (terrainHeight(g.position.x, g.position.z) < ocean.level - 0.18) {
      c.speed *= 0.94;
      damageCar(9 * dt);
      hud.objective('The tide is flooding the car!');
    }

    // chase camera
    const camTarget = tmpV.set(g.position.x - fx * 8.2, g.position.y + 3.4, g.position.z - fz * 8.2);
    const minY = terrainHeight(camTarget.x, camTarget.z) + 1.2;
    camTarget.y = Math.max(camTarget.y, minY);
    camera.position.lerp(camTarget, Math.min(1, dt * 3.2));
    camera.lookAt(g.position.x + fx * 6, g.position.y + 1.6, g.position.z + fz * 6);

    hud.speed(Math.abs(c.speed) * 3.6);
  }

  // ---------------------------------------------------------------- interaction
  function tryToggleCar() {
    const c = state.carState;
    if (c.dead) return;

    if (c.occupied) {
      // exit
      c.occupied = false;
      state.player.pos.copy(car.position).add(new THREE.Vector3(2.2 * Math.cos(c.yaw), 0, -2.2 * Math.sin(c.yaw)));
      state.player.pos.y = terrainHeight(state.player.pos.x, state.player.pos.z);
      state.player.yaw = c.yaw + Math.PI; // player-camera yaw faces -z at 0
      hud.speed(null);
    } else {
      const d = state.player.pos.distanceTo(car.position);
      if (d < 3.6) {
        c.occupied = true;
        hud.prompt('');
      }
    }
  }

  // ---------------------------------------------------------------- win/lose
  function endGame(won, text) {
    if (state.over) return;
    state.over = true;
    state.playing = false;
    if (typeof document !== 'undefined') document.exitPointerLock?.();
    hud.els.end?.classList.remove('hidden');
    hud.els.endTitle && (hud.els.endTitle.textContent = won ? 'OBJECTIVE COMPLETE' : 'KILLED IN ACTION');
    hud.els.endText && (hud.els.endText.textContent = text);
  }

  function checkWin() {
    const pos = state.carState.occupied ? car.position : state.player.pos;
    if (pos.z > WORLD.wallZ + 0.5 && Math.abs(pos.x) < WORLD.gateHalfWidth + 1.2) {
      endGame(true, 'You reached the Wall. The gate holds your only way through.');
    }
  }

  // ---------------------------------------------------------------- tide text
  let lastTideSec = -1;
  function updateTide() {
    hud.tide(ocean.t, ocean.level - WORLD.seaLevel);
    const sec = Math.ceil((1 - ocean.t) * WORLD.tideDuration);
    if (sec !== lastTideSec && sec % 15 === 0 && state.time > 2) {
      lastTideSec = sec;
      hud.objective(`Reach the Wall — tide full in ${Math.round(sec / 60)}:${String(sec % 60).padStart(2, '0')}`);
    }
  }

  // ---------------------------------------------------------------- main tick
  function update(dt) {
    if (!state.playing || state.over) return;
    state.time += dt;

    if (state.carState.occupied) updateCarDriving(dt);
    else updatePlayer(dt);

    // car respawn
    if (state.carState.dead) {
      state.carState.respawn -= dt;
      if (state.carState.respawn <= 0) {
        state.carState.dead = false;
        state.carState.hp = CAR.hp;
        car.position.set(CAR.spawn.x, terrainHeight(CAR.spawn.x, CAR.spawn.z) + 0.12, CAR.spawn.z);
        state.carState.yaw = CAR.spawn.yaw;
        car.rotation.y = CAR.spawn.yaw;
        car.visible = true;
      }
    }

    updateSentries(dt);
    checkMines();
    updateEffects(dt);
    updateTide();
    checkWin();

    // prompts
    if (!state.carState.occupied && !state.carState.dead) {
      const d = state.player.pos.distanceTo(car.position);
      hud.prompt(d < 3.6 ? 'E — ENTER VEHICLE' : (ocean.level > terrainHeight(state.player.pos.x, state.player.pos.z) + 0.22 ? 'MOVE TO HIGHER GROUND' : ''));
    }

    // camera shake
    if (state.shake > 0) {
      camera.position.x += (Math.random() - 0.5) * state.shake * 0.55;
      camera.position.y += (Math.random() - 0.5) * state.shake * 0.55;
      camera.position.z += (Math.random() - 0.5) * state.shake * 0.55;
      state.shake = Math.max(0, state.shake - dt * 2.2);
    }

    hud.health(state.player.hp);
  }

  return {
    state,
    update,
    start() {
      state.playing = true;
      state.over = false;
      hud.show();
    },
    keydown(code) {
      state.keys[code] = true;
      if (code === 'KeyE') tryToggleCar();
    },
    keyup(code) { state.keys[code] = false; },
    mouse(dx, dy) {
      if (state.carState.occupied) return;
      state.player.yaw -= dx * 0.0022;
      state.player.pitch = THREE.MathUtils.clamp(state.player.pitch - dy * 0.0022, -1.35, 1.35);
    },
    endGame,
  };
}
