// Particle Editor application: scene state, frame loop, camera, outliner and inspector.
// 📝 Converted from an IIFE to an ES module; the body keeps its two-space indent so that `git blame`
//    still points at whoever wrote each line rather than at the conversion.
// The entry module. These are imported for their effect on the shared namespace, in the order
//    they depend on one another: fibres defines hexLinear that presets reads, engine defines
//    WIND that this file reads below. ES modules evaluate in import order, so this is the old
//    script-tag order made explicit and checkable instead of implied by the HTML.
import { PE } from "./pe.js";
import "./fibres.js";
import "./presets.js";
import "./shaders.js";
import "./engine.js";
import "./lightning.js";
import "./forcefields.js";
import "./edits.js";
import "./handoff.js";
  const $ = (sel) => document.querySelector(sel);

  // ----------------------------------------------------------------- small helpers
  function el(tag, attrs = {}, ...kids) {
    const node = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (v === undefined || v === null || v === false) continue;
      if (k === "class") node.className = v;
      else if (k === "text") node.textContent = v;
      else if (k.startsWith("on")) node.addEventListener(k.slice(2), v);
      else if (k === "value") node.value = v;
      else if (k === "checked") node.checked = !!v;
      else node.setAttribute(k, v === true ? "" : v);
    }
    for (const kid of kids.flat()) {
      if (kid === null || kid === undefined || kid === false) continue;
      node.append(kid.nodeType ? kid : document.createTextNode(String(kid)));
    }
    return node;
  }
  const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
  const fmt = (v, d) => (d == null ? String(+Number(v).toPrecision(4)) : Number(v).toFixed(d));
  const toHex = (c) => "#" + c.slice(0, 3).map((x) => Math.round(clamp(x, 0, 1) * 255).toString(16).padStart(2, "0")).join("");
  const fromHex = (h) => [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16) / 255);
  const norm3 = (a) => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
  const cross3 = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const sub3 = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const dot3 = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const fmtInt = (n) => Math.round(n).toLocaleString("en-US");

  // ----------------------------------------------------------------- matrices (column-major)
  function mul4(a, b) {
    const o = new Float32Array(16);
    for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
      let s = 0;
      for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
      o[c * 4 + r] = s;
    }
    return o;
  }
  function lookAt(e, c, up) {
    const f = norm3(sub3(c, e));
    const s = norm3(cross3(f, up));
    const u = cross3(s, f);
    return Float32Array.of(
      s[0], u[0], -f[0], 0,
      s[1], u[1], -f[1], 0,
      s[2], u[2], -f[2], 0,
      -dot3(s, e), -dot3(u, e), dot3(f, e), 1,
    );
  }
  function perspective(fovy, aspect, n, f) {
    const t = 1 / Math.tan(fovy / 2);
    return Float32Array.of(t / aspect, 0, 0, 0, 0, t, 0, 0, 0, 0, f / (n - f), -1, 0, 0, (n * f) / (n - f), 0);
  }

  // ----------------------------------------------------------------- state
  const state = {
    engine: null,
    lightning: null,
    systems: [],
    nextId: 1,
    selection: { type: "wind" },
    // 🔴 ONE LIST. Wind components and force fields used to be two separate arrays with two separate
    //    shapes, edited in two panels, because nothing had noticed they were the same entity seen twice.
    //    They are now one authored list of force fields; `wind` keeps only the lattice's own display and
    //    global terms, which are settings of the sampling grid rather than fields in it.
    forces: PE.Forces.DefaultForces(),
    wind: PE.defaultWind(),
    cam: { yaw: 0.6, pitch: 0.3, dist: 12.5, target: [0, 1.6, 0] },
    playing: true,
    timeScale: 1,
    time: 0,
    frame: 0,
    fps: 0,
    cpuMs: 0,
    probeOn: true,
    probe: [0, 1, 0],
    web: { enabled: false, interval: 0.6, nextAt: 0, k: 2 },
    ui: { liveEls: {} },
  };
  window.ParticleEditorState = state; // for diagnostics in the console

  const DOMAIN = { min: PE.WIND.min, size: PE.WIND.size, dim: PE.WIND.dim };
  const COLORS = { bg: [0.035, 0.037, 0.045] };

  // ----------------------------------------------------------------- systems
  function makeSystem(presetId, nameOverride) {
    applyWindLink(PE.presetById(presetId));
    const preset = PE.presetById(presetId);
    const p = JSON.parse(JSON.stringify(preset.p));
    const sys = {
      id: state.nextId++,
      presetId,
      name: nameOverride || preset.name,
      p,
      gpu: null,
      acc: 0,
      pendingBurst: 0,
      overrideOrigin: null,
      needFill: true,
      head: 0,
    };
    sys.gpu = state.engine.createSystem(p);
    return sys;
  }

  function rebuildGpu(sys) {
    if (sys.gpu) state.engine.destroySystem(sys.gpu);
    sys.gpu = state.engine.createSystem(sys.p);
    sys.needFill = true;
    sys.head = 0;
    sys.acc = 0;
    sys.pendingBurst = 0;
  }

  // Some presets need a matching wind component to look right (a tornado needs its vortex,
  // a sandstorm needs a strong prevailing wind). Enable or create that component when added.
  // A preset's windLink still speaks the lattice's language (type number, bearing, x/z), because that is
  //    how fifty-two presets are written and rewriting them is a separate job from unifying the lists.
  //    This is the one place that translation lives.
  const FLOW_KIND = ["prevailing", "gust", "tornado", "outflow"];

  function applyWindLink(preset) {
    const link = preset.windLink;
    if (!link) return;
    const Kind = FLOW_KIND[link.type];
    if (!Kind) return;
    let Field = state.forces.find((One) => One.Kind === Kind);
    if (!Field) {
      if (flowFields().length >= PE.WIND.maxComps) return;
      Field = { ...PE.Forces.BaseField(Kind), Name: link.name, Enabled: false,
                Centre: [0, 0, 0], Radius: 2, Strength: 4, Rate: 0, Direction: PE.Forces.Along(0) };
      state.forces.push(Field);
    }
    const Set = link.set || {};
    if (Set.radius !== undefined) Field.Radius = Set.radius;
    if (Set.strength !== undefined) Field.Strength = Set.strength;
    if (Set.bearing !== undefined) Field.Direction = PE.Forces.Along(Set.bearing);
    if (Set.freq !== undefined) Field.Rate = Set.freq;
    if (link.local) { Field.Centre[0] = preset.p.origin[0]; Field.Centre[2] = preset.p.origin[2]; }
    Field.Enabled = true;
  }

  // The flow fields, in authored order. buildWind reads a fixed count and tests each entry's own enabled
  //    flag, so a disabled one keeps its slot rather than shuffling the ones behind it.
  function flowFields() {
    return state.forces.filter((One) => {
      const Kind = PE.Forces.KindById(One.Kind);
      return Kind && Kind.Give === PE.Forces.Contribution.Flow;
    });
  }

  function addSystem(presetId) {
    const same = state.systems.filter((s) => s.presetId === presetId).length;
    const preset = PE.presetById(presetId);
    const sys = makeSystem(presetId, same ? preset.name + " " + (same + 1) : preset.name);
    state.systems.push(sys);
    select({ type: "system", id: sys.id });
    return sys;
  }

  function removeSystem(sys) {
    state.engine.destroySystem(sys.gpu);
    state.systems = state.systems.filter((s) => s !== sys);
    select({ type: "wind" });
  }

  function systemById(id) {
    return state.systems.find((s) => s.id === id) || null;
  }

  function resetAll() {
    for (const sys of state.systems) rebuildGpu(sys);
    state.lightning.bolts = [];
    state.lightning.flash = 0;
  }

  // Strikes place a burst on any "strike sparks" system, at the ground point.
  function onStrike(point) {
    for (const sys of state.systems) {
      if (sys.presetId === "strikeSparks") {
        sys.pendingBurst += 160;
        sys.overrideOrigin = point;
      }
    }
  }

  function strikeNow() {
    const hit = state.lightning.strike(state.time);
    onStrike(hit);
    refreshLightningReadout();
  }

  // ----------------------------------------------------------------- camera
  function cameraMatrices(aspect) {
    const c = state.cam;
    const cp = Math.cos(c.pitch);
    const sp = Math.sin(c.pitch);
    const eye = [
      c.target[0] + c.dist * cp * Math.sin(c.yaw),
      c.target[1] + c.dist * sp,
      c.target[2] + c.dist * cp * Math.cos(c.yaw),
    ];
    const view = lookAt(eye, c.target, [0, 1, 0]);
    const proj = perspective((45 * Math.PI) / 180, aspect, 0.05, 400);
    const f = norm3(sub3(c.target, eye));
    const r = norm3(cross3(f, [0, 1, 0]));
    const u = cross3(r, f);
    return { vp: mul4(proj, view), eye, r, u };
  }

  // ----------------------------------------------------------------- frame
  let lastT = 0;
  let uiTimer = 0;
  let cpuAcc = 0;
  let cpuFrames = 0;

  function buildLines(cam) {
    const out = [];
    const line = (a, b, col) => out.push(a[0], a[1], a[2], col[0], col[1], col[2], col[3], b[0], b[1], b[2], col[0], col[1], col[2], col[3]);
    const cross = (c, s, col) => {
      line([c[0] - s, c[1], c[2]], [c[0] + s, c[1], c[2]], col);
      line([c[0], c[1] - s, c[2]], [c[0], c[1] + s, c[2]], col);
      line([c[0], c[1], c[2] - s], [c[0], c[1], c[2] + s], col);
    };
    const box = (lo, hi, col) => {
      const c = [[lo[0], lo[1], lo[2]], [hi[0], lo[1], lo[2]], [hi[0], lo[1], hi[2]], [lo[0], lo[1], hi[2]],
                 [lo[0], hi[1], lo[2]], [hi[0], hi[1], lo[2]], [hi[0], hi[1], hi[2]], [lo[0], hi[1], hi[2]]];
      for (const [a, b] of [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]]) {
        line(c[a], c[b], col);
      }
    };
    if (state.wind.showDomain) {
      box(DOMAIN.min, [DOMAIN.min[0] + DOMAIN.size[0], DOMAIN.min[1] + DOMAIN.size[1], DOMAIN.min[2] + DOMAIN.size[2]], [0.45, 0.6, 0.85, 0.55]);
    }
    for (const sys of state.systems) {
      if (!sys.p.visible) continue;
      const o = sys.p.origin;
      const selected = state.selection.type === "system" && state.selection.id === sys.id;
      cross(o, 0.12, selected ? [1, 0.75, 0.25, 1] : [0.6, 0.65, 0.75, 0.6]);
      if (sys.p.kind === 3 || sys.p.kind === 4) {
        const B = sys.p.boxHalf[0];
        box([o[0] - B, o[1] - B, o[2] - B], [o[0] + B, o[1] + B, o[2] + B], selected ? [1, 0.75, 0.25, 0.9] : [0.5, 0.9, 1, 0.35]);
      }
      if (sys.p.emitShape === 3 && sys.p.kind < 3) {
        const h = sys.p.boxHalf;
        box([o[0] - h[0], o[1] - h[1], o[2] - h[2]], [o[0] + h[0], o[1] + h[1], o[2] + h[2]], [0.6, 0.7, 0.9, 0.3]);
      }
    }
    if (state.probeOn) cross(state.probe, 0.18, [1, 0.9, 0.3, 1]);
    void cam;
    return new Float32Array(out);
  }

  function probeVoxel() {
    const p = state.probe;
    const cell = DOMAIN.size.map((s, i) => s / DOMAIN.dim[i]);
    return {
      ix: clamp(Math.floor((p[0] - DOMAIN.min[0]) / cell[0]), 0, DOMAIN.dim[0] - 1),
      iy: clamp(Math.floor((p[1] - DOMAIN.min[1]) / cell[1]), 0, DOMAIN.dim[1] - 1),
      iz: clamp(Math.floor((p[2] - DOMAIN.min[2]) / cell[2]), 0, DOMAIN.dim[2] - 1),
    };
  }

  function frame(now) {
    requestAnimationFrame(frame);
    // 📝 A hidden tab still gets rAF in some browsers and still gets the 1 Hz fallback in all of them.
    //    Encoding a frame nobody can see costs the same GPU time as one they can, so skip it and let the
    //    clock resume from the next visible frame rather than jumping the simulation forward.
    if (document.hidden) { lastT = 0; return; }
    const t0 = performance.now();
    const dtReal = lastT ? Math.min(0.05, (now - lastT) / 1000) : 0;
    lastT = now;
    if (dtReal > 0) state.fps = state.fps * 0.9 + (1 / dtReal) * 0.1;
    const dt = state.playing ? dtReal * state.timeScale : 0;
    state.time += dt;
    state.frame++;

    // Lightning and strike sparks.
    const hits = state.lightning.update(dt, state.time);
    for (const hit of hits) onStrike(hit);
    // 📝 Lightning web: re-strike arcs between visible system origins on an interval.
    if (state.web.enabled && state.time >= state.web.nextAt) {
      const nodes = state.systems.filter((s) => s.p.visible).map((s) => [s.p.origin[0], s.p.origin[1], s.p.origin[2]]);
      if (nodes.length >= 2) state.lightning.web(state.time, nodes, { k: state.web.k, life: state.web.interval * 0.9 });  // arcs stay lit until the next re-strike
      state.web.nextAt = state.time + state.web.interval;
    }

    const canvas = state.engine.canvas;
    const aspect = canvas.width / Math.max(1, canvas.height);
    const cam = cameraMatrices(aspect);

    const glob = new Float32Array(52);
    const G = PE.GLOB_OFF;
    glob.set(cam.vp, G.viewProj);
    glob.set([cam.r[0], cam.r[1], cam.r[2], 0], G.camRight);
    glob.set([cam.u[0], cam.u[1], cam.u[2], 0], G.camUp);
    glob.set([cam.eye[0], cam.eye[1], cam.eye[2], 1], G.camPos);
    glob.set([DOMAIN.min[0], DOMAIN.min[1], DOMAIN.min[2], 0], G.windMin);
    glob.set([DOMAIN.size[0], DOMAIN.size[1], DOMAIN.size[2], 0], G.windSize);
    const Flow = PE.Forces.ForGpu(state.forces).Lattice;
    glob.set([DOMAIN.dim[0], DOMAIN.dim[1], DOMAIN.dim[2], Flow.length], G.windDim);
    glob.set([state.time, dt, state.frame, state.wind.windScale], G.timing);
    glob.set([state.wind.arrowRef, 0.012, state.lightning.flash, state.wind.turbulence], G.viz);
    glob.set([state.wind.swirl, 0.45, 0.22, state.wind.arrowStride || 3], G.swirl);

    const comps = new Float32Array(PE.WIND.maxComps * 8);
    Flow.slice(0, PE.WIND.maxComps).forEach((c, i) => {
      comps.set([c.x, c.z, Math.max(0.5, c.radius), c.type, c.strength, (c.bearing * Math.PI) / 180, c.freq, c.enabled ? 1 : 0], i * 8);
    });

    const jobs = [];
    for (const sys of state.systems) {
      if (!sys.p.visible || !sys.gpu) continue;
      const p = sys.p;
      const g = sys.gpu;
      if (p.kind === 7) {
        // Light fibres are analytic: no simulation, just a uniform and (for trails) a path table.
        const f = p.fibre;
        const key = f.pathShape + "|" + f.pathSize;
        let path = null;
        if (g.pathKey !== key) {
          g.pathKey = key;
          path = PE.samplePath(f.pathShape, f.pathSize, PE.PATH_SAMPLES);
        }
        PE.fillFibre(g.fibreCpu, p, {
          lf: (state.time / f.loopSeconds) % 1,
          projScale: (0.5 * canvas.height) / Math.tan(Math.PI / 8),
          viewW: canvas.width,
          viewH: canvas.height,
        });
        jobs.push({
          gpu: g, cpu: g.fibreCpu, path, fibre: true, simulate: false, draw: false,
          strands: f.strands, segments: f.segments, sparks: f.sparks > 0, alpha: false,
          mol: false, emitN: 0, steps: 0, gd: 1,
        });
        continue;
      }
      const mol = p.kind === 3 || p.kind === 4;
      const simulate = state.playing && dt > 0;
      let emitN = 0;
      let steps = 0;
      let stepDt = dt;
      let head = sys.head;
      let origin = p.origin;
      if (simulate) {
        if (p.burstEvery > 0) {
          // Timed bursts (fireworks): each shell fires from a random point in the sky.
          sys.burstTimer = (sys.burstTimer ?? 0) - dt;
          if (sys.burstTimer <= 0) {
            sys.burstTimer = p.burstEvery * (0.7 + 0.6 * Math.random());
            sys.pendingBurst += p.burstCount;
            sys.overrideOrigin = p.burstAtOrigin
              ? null
              : [-3 + 6 * Math.random(), 3.5 + 2.5 * Math.random(), -3 + 6 * Math.random()];
          }
        }
        if (sys.overrideOrigin) {
          origin = sys.overrideOrigin;
          sys.overrideOrigin = null;
        }
        if (mol) {
          // Keep the integration step at or below ~1.25 × simDt so the LJ forces stay stable
          // even when the frame rate drops and dt grows (simulated time then lags real time).
          steps = clamp(Math.round(dt / p.simDt), 1, 8);
          stepDt = Math.min(dt / steps, p.simDt * 1.25);
          if (sys.needFill) {
            emitN = g.cap;
            sys.needFill = false;
            head = 0;
          }
        } else {
          sys.acc += p.rate * dt;
          emitN = Math.floor(sys.acc);
          sys.acc -= emitN;
          emitN = Math.min(g.cap, emitN + sys.pendingBurst);
          sys.pendingBurst = 0;
          sys.head = (head + emitN) % g.cap;
        }
      }
      const B = p.boxHalf[0];
      const rc = 2.5 * p.sigma;
      const gd = mol ? clamp(Math.floor((2 * B) / rc), 1, PE.MAX_GD) : 1;
      PE.fillSys(g.cpu, p, {
        origin, dt: stepDt, time: state.time, frame: state.frame, cap: g.cap, emitN, head,
        seed: (state.frame * 7919 + sys.id * 104729) >>> 0, mol, gd, cellSize: mol ? (2 * B) / gd : 1,
      });
      jobs.push({
        gpu: g, cpu: g.cpu, simulate, mol, emitN, steps, gd, swarm: p.kind === 6,
        readStats: simulate && state.frame % 3 === 0,
        draw: true, alpha: p.blend === "alpha",
        cube: p.shape === 4,
        shatter: !!(p.transition && p.transition.fragment),
        fly: !!(p.transition && p.transition.after === "butterfly"),
      });
    }

    const segCount = state.lightning.pack(state.time);
    const lines = buildLines(cam);
    const probe = state.probeOn && !state.engine.probePending && state.frame % 4 === 0 ? probeVoxel() : null;

    const lens = packLens(cam, aspect);
    state.engine.render({
      glob,
      fields: packFields(),
      lensCount: lens.count + lens.shCount,
      lens: lens.data,
      comps,
      clear: COLORS.bg.map((c) => c + state.lightning.flash * 0.22),
      floor: state.wind.showFloor,
      arrows: state.wind.showArrows,
      lines,
      lineCount: lines.length / 7,
      segCount,
      segData: state.lightning.dataBuf,
      jobs,
      probe,
    });

    cpuAcc += performance.now() - t0;
    cpuFrames++;
    if (now - uiTimer > 250) {
      uiTimer = now;
      state.cpuMs = cpuFrames ? cpuAcc / cpuFrames : 0;
      cpuAcc = 0;
      cpuFrames = 0;
      updateLive();
    }
  }

  // ----------------------------------------------------------------- live readouts
  function updateLive() {
    const live = state.ui.liveEls;
    const total = state.systems.filter((s) => s.p.visible).reduce((a, s) => a + (s.gpu.stats_ ? s.gpu.stats_.alive : 0), 0);
    const cap = state.systems.filter((s) => s.p.visible && s.p.kind !== 7).reduce((a, s) => a + s.gpu.cap, 0);
    $("#st-particles").textContent = `${fmtInt(total)} / ${fmtInt(cap)} particles`;
    $("#st-fps").textContent = `${Math.round(state.fps)} fps · CPU ${state.cpuMs.toFixed(2)} ms`;
    const reads = state.systems.reduce((a, s) => a + s.gpu.readbackCount, 0);
    const lastMs = state.systems.length ? state.systems[state.systems.length - 1].gpu.readbackMs : 0;
    $("#st-readback").textContent = `readback ${lastMs ? lastMs.toFixed(2) : "–"} ms · ${reads} total`;
    $("#outliner-count").textContent = `${state.systems.length + 2} entries`;
    const vis = state.systems.filter((s) => s.p.visible).length;
    if (live.vis) live.vis.textContent = String(vis);
    if (live.hid) live.hid.textContent = String(state.systems.length - vis);

    for (const sys of state.systems) {
      const meta = sys.ui_meta;
      if (meta) meta.textContent = `${kindLabel(sys.p.kind)} · ${aliveText(sys)}`;
    }
    if (live.sys && state.selection.type === "system") {
      const sys = systemById(state.selection.id);
      if (sys && sys.gpu) {
        const s = sys.gpu.stats_;
        live.alive.textContent = s ? fmtInt(s.alive) : "–";
        live.readMs.textContent = sys.gpu.readbackMs ? sys.gpu.readbackMs.toFixed(2) + " ms" : "–";
        live.reads.textContent = fmtInt(sys.gpu.readbackCount);
        if (s && live.species) {
          live.species.textContent = `A ${fmtInt(s.A)} · B ${fmtInt(s.B)} · C ${fmtInt(s.C)}`;
        }
        if (s && live.temp) {
          const T = s.alive ? s.energy / s.alive / 3 : 0;
          live.temp.textContent = T.toFixed(3);
        }
      }
    }
    if (live.probe && state.engine.probeResult) {
      const r = state.engine.probeResult;
      live.probe.textContent = `${r.value.map((v) => v.toFixed(2)).join(", ")} m/s · |v| ${r.mag.toFixed(2)}`;
      live.probeMs.textContent = `${r.ms.toFixed(2)} ms · ${r.bytes} bytes`;
    }
    if (live.light) refreshLightningReadout();
  }

  function refreshLightningReadout() {
    const live = state.ui.liveEls;
    if (live.strikes) live.strikes.textContent = fmtInt(state.lightning.strikes);
    if (live.lastStrike) {
      const s = state.lightning.lastStrike;
      live.lastStrike.textContent = s ? `(${s.at.map((v) => v.toFixed(1)).join(", ")}) m` : "–";
    }
    if (live.soundBtn) live.soundBtn.textContent = state.lightning.thunder.enabled ? "Sound on" : "Sound off";
  }

  function kindLabel(k) {
    return { 0: "Streak", 1: "Wind-driven", 3: "Molecular · LJ", 4: "Molecular · reactive", 5: "VFX", 6: "Swarm · flocking", 7: "Light fibres" }[k] || "Particles";
  }

  // Outliner subtitle: particle systems count alive particles; fibres count strands.
  function aliveText(sys) {
    if (sys.p.kind === 7) return fmtInt(sys.p.fibre.strands) + " strands";
    return (sys.gpu.stats_ ? fmtInt(sys.gpu.stats_.alive) : 0) + " alive";
  }

  // ----------------------------------------------------------------- outliner
  function renderOutliner() {
    const list = $("#outliner-list");
    list.innerHTML = "";
    const row = (key, title, meta, dotStyle, active, onclick, eye) => {
      const item = el("div", { class: "ol-item" + (active ? " is-active" : ""), onclick, role: "button", tabindex: 0 },
        el("span", { class: "ol-dot", style: dotStyle }),
        el("div", { class: "ol-text" }, el("div", { class: "ol-title", text: title }), el("div", { class: "ol-meta", text: meta })),
        eye);
      return item;
    };
    const sel = state.selection;
    const q = (state.ui.olQuery || "").trim().toLowerCase();
    const match = (t) => !q || t.toLowerCase().includes(q);
    if (match("Environment") || match("Wind field") || match("Lightning") || match("Force fields")) list.append(el("div", { class: "ol-group", text: "Environment" }));
    if (match("Wind field")) list.append(row("wind", "Wind field", `${flowFields().filter((c) => c.Enabled).length} active · ${PE.WIND.dim.join("×")} grid`,
      "background: linear-gradient(90deg,#3a7bff,#ffd34a,#ff4a2a)", sel.type === "wind", () => select({ type: "wind" })));
    if (match("Force fields")) list.append(row("forces", "Force fields", forcesRowSummary(),
      "background:#9ad0ff", sel.type === "forces", () => select({ type: "forces" })));
    if (match("Lightning")) list.append(row("light", "Lightning", `${state.lightning.strikes} strikes · ${state.lightning.auto ? "auto" : "manual"}`,
      "background:#c9e2ff", sel.type === "lightning", () => select({ type: "lightning" })));
    list.append(el("div", { class: "ol-group", text: "Particle systems" }));
    for (const sys of state.systems) {
      if (!match(sys.name)) continue;
      const eye = el("button", {
        class: "ol-eye" + (sys.p.visible ? " is-on" : ""), title: sys.p.visible ? "Hide" : "Show",
        "aria-pressed": sys.p.visible ? "true" : "false",
        onclick: (e) => { e.stopPropagation(); sys.p.visible = !sys.p.visible; renderOutliner(); },
      }, "");
      const item = row(sys.id, sys.name, "", `background:${toHex(sys.p.colA)}`,
        sel.type === "system" && sel.id === sys.id, () => select({ type: "system", id: sys.id }), eye);
      sys.ui_meta = item.querySelector(".ol-meta");
      sys.ui_meta.textContent = `${kindLabel(sys.p.kind)} · ${aliveText(sys)}`;
      list.append(item);
    }
    const add = el("select", { class: "ol-add", "aria-label": "Add particle system", onchange: (e) => {
      if (!e.target.value) return;
      addSystem(e.target.value);
    } }, el("option", { value: "", text: "+ Add particle system…" }));
    let group = null;
    for (const preset of PE.Presets) {
      if (preset.group !== group) {
        group = preset.group;
        add.append(el("optgroup", { label: group }));
      }
      add.lastChild.append(el("option", { value: preset.id, text: preset.name }));
    }
    list.append(add);
    const counts = state.systems.filter((s) => s.p.visible).length;
    state.ui.liveEls.vis = null;
    $("#outliner-visible").textContent = String(counts);
    $("#outliner-hidden").textContent = String(state.systems.length - counts);
  }

  // The outliner line for the force fields, counted by contribution so that the taxonomy is visible
  //    without opening the panel.
  function forcesRowSummary() {
    const Count = { flow: 0, accelerate: 0, damp: 0 };
    for (const Field of state.forces) {
      const Kind = PE.Forces.KindById(Field.Kind);
      if (Kind && Field.Enabled) Count[Kind.Give]++;
    }
    const Own = state.systems.filter((One) => One.p.attractor && One.p.attractor.enabled).length;
    return `${Count.flow} flow · ${Count.accelerate} accel` + (Count.damp ? ` · ${Count.damp} damp` : "")
         + (Own ? ` · ${Own} black hole` : "");
  }

  function select(sel) {
    state.selection = sel;
    renderOutliner();
    renderInspector();
  }

  // ----------------------------------------------------------------- controls
  // Every row helper below funnels its write through PE.Edits, so that an edit is observable without
  //    thirty-two closures having to remember to say so. See js/edits.js for why the funnel is here
  //    rather than at the parameter.
  function rangeRow(label, get, set0, o) {
    const set = PE.Edits.Through(label, set0);
    const digits = o.digits;
    const range = el("input", { type: "range", min: o.min, max: o.max, step: o.step, value: get() });
    const num = el("input", { type: "number", min: o.min, max: o.max, step: o.step, value: fmt(get(), digits), class: "num" });
    const fill = () => range.style.setProperty("--fill", ((+range.value - o.min) / (o.max - o.min) * 100) + "%");
    fill();
    range.addEventListener("input", () => { const v = +range.value; num.value = fmt(v, digits); set(v); fill(); });
    num.addEventListener("change", () => { const v = +num.value; if (Number.isFinite(v)) { const c = clamp(v, o.min, o.max); range.value = c; num.value = fmt(c, digits); set(c); fill(); } });
    return el("label", { class: "row" }, el("span", { class: "row-k", text: label }), range, num, el("em", { class: "unit", text: o.unit || "" }));
  }
  function vecRow(label, arr, o) {
    const inputs = [0, 1, 2].map((i) => {
      const input = el("input", { type: "number", step: o.step, value: fmt(arr[i], o.digits), class: "num vec", "aria-label": label + " " + "xyz"[i] });
      input.addEventListener("change", () => {
        const v = +input.value;
        if (Number.isFinite(v)) {
          arr[i] = clamp(v, o.min, o.max);
          input.value = fmt(arr[i], o.digits);
          o.onChange?.();
          PE.Edits.Announce(label + " " + "XYZ"[i], arr[i]);
        }
      });
      return input;
    });
    return el("div", { class: "row vec-row" }, el("span", { class: "row-k", text: label }), el("div", { class: "axes" }, ...inputs.map((inp, i) => el("label", {}, el("b", { text: "XYZ"[i] }), inp))), el("em", { class: "unit", text: o.unit || "" }));
  }
  function colorRow(label, arr) {
    const picker = el("input", { type: "color", value: toHex(arr), "aria-label": label + " colour" });
    const alpha = el("input", { type: "range", min: 0, max: 1, step: 0.01, value: arr[3] ?? 1, "aria-label": label + " alpha" });
    picker.addEventListener("input", () => {
      const c = fromHex(picker.value);
      arr[0] = c[0]; arr[1] = c[1]; arr[2] = c[2];
      PE.Edits.Announce(label, arr.slice(0, 3));
    });
    alpha.addEventListener("input", () => { arr[3] = +alpha.value; PE.Edits.Announce(label + " alpha", arr[3]); });
    return el("label", { class: "row" }, el("span", { class: "row-k", text: label }), picker, alpha, el("em", { class: "unit", text: "α" }));
  }
  function selectRow(label, options, get, set0) {
    const set = PE.Edits.Through(label, set0);
    const s = el("select", { "aria-label": label }, ...options.map(([v, t]) => el("option", { value: v, text: t, selected: String(get()) === String(v) })));
    s.addEventListener("change", () => set(isNaN(+s.value) ? s.value : +s.value));
    return el("label", { class: "row" }, el("span", { class: "row-k", text: label }), s);
  }
  function checkRow(label, get, set0) {
    const set = PE.Edits.Through(label, set0);
    const c = el("input", { type: "checkbox", checked: get(), "aria-label": label });
    c.addEventListener("change", () => set(c.checked));
    return el("label", { class: "row check" }, el("span", { class: "row-k", text: label }), c);
  }
  function button(label, onclick, cls = "") {
    const Press = (Event) => { const Answer = onclick(Event); PE.Edits.Announce(label, true); return Answer; };
    return el("button", { class: "btn " + cls, onclick: Press, type: "button" }, label);
  }
  function card(title, kicker, ...body) {
    return el("section", { class: "pcard" },
      el("header", { class: "pcard-h" }, el("h3", { text: title }), kicker ? el("span", { class: "kicker", text: kicker }) : null),
      el("div", { class: "pcard-b" }, ...body));
  }
  function note(text) {
    return el("p", { class: "note", text });
  }

  // ----------------------------------------------------------------- inspector
  // 📝 Force fields for the GPU: attractors on black hole systems, plus the global fields. Max 8.
  // Layout matches fieldAccel in shaders.js: fields[0] = (count, time), then 3 vec4 per field.
  function packFields() {
    const out = new Float32Array(128);
    let n = 0;
    const put = (type, pos, f) => {
      if (n >= 8) return;
      const o = (1 + 3 * n) * 4;
      out.set([pos[0], pos[1], pos[2], f.radius], o);
      out.set([type, f.strength, f.swirl || 0, f.swallow || 0], o + 4);
      out.set([f.t0 || 0, f.duration, f.period || 0, 0], o + 8);
      n++;
    };
    for (const sys of state.systems) {
      const at = sys.p.attractor;
      if (!sys.p.visible || !at || !at.enabled) continue;
      put(0, sys.p.origin, { radius: at.radius, strength: at.strength, swirl: at.swirl, swallow: at.swallow, t0: 0, duration: 1e9, period: 0 });
    }
    // 📝 Magnetic dipole fields: type 3, strength = drive, swirl slot = guide rate.
    for (const sys of state.systems) {
      const mf = sys.p.magneticField;
      if (!sys.p.visible || !mf) continue;
      put(3, sys.p.origin, { radius: mf.radius, strength: mf.strength, swirl: mf.guide, swallow: 0, t0: 0, duration: 1e9, period: 0 });
    }
    // The authored acceleration fields. Per-system attractors above are not in the list because they
    //    belong to a system rather than to the scene — they move with it and die with it.
    for (const Entry of PE.Forces.ForGpu(state.forces, state.time).Forces) put(Entry.type, Entry.pos, Entry);
    out[0] = n;
    out[1] = state.time;
    return out;
  }

  // 📝 Black hole lensing inputs: each visible black hole's screen centre (uv, y down) and radii as fractions of the
  // screen height: horizon H and Einstein radius E. Radii are world metres scaled by 0.5 / (distance · tan(22.5°)).
  function packLens(cam, aspect) {
    // Layout matches LensU in shaders.js: bh0, bh1, misc, then two shimmers (sa, sb each).
    const out = new Float32Array(32);
    let n = 0;
    let sh = 0;
    const vp = cam.vp;
    const k0 = 0.5 / Math.tan(Math.PI / 8);
    const project = (o) => {
      const clip = [0, 1, 2, 3].map((r) => vp[r] * o[0] + vp[4 + r] * o[1] + vp[8 + r] * o[2] + vp[12 + r]);
      if (clip[3] <= 0.01) return null;
      const dist = Math.hypot(o[0] - cam.eye[0], o[1] - cam.eye[1], o[2] - cam.eye[2]);
      return { u: (clip[0] / clip[3]) * 0.5 + 0.5, v: 0.5 - (clip[1] / clip[3]) * 0.5, k: k0 / Math.max(dist, 0.01) };
    };
    for (const sys of state.systems) {
      const bh = sys.p.blackHole;
      if (!sys.p.visible || !bh || n >= 2) continue;
      const pr = project(sys.p.origin);
      if (!pr) continue;
      out.set([pr.u, pr.v, bh.horizon * (bh.einstein || 0) * pr.k, bh.horizon * pr.k], n * 4);
      n++;
    }
    for (const sys of state.systems) {
      const hs = sys.p.heatShimmer;
      if (!sys.p.visible || !hs || sh >= 2) continue;
      const pr = project(sys.p.origin);
      if (!pr) continue;
      out.set([pr.u, pr.v, hs.radius * pr.k, hs.strength], 12 + sh * 8);
      out.set([state.time * hs.rate, hs.freq, 0, 0], 16 + sh * 8);
      sh++;
    }
    out[8] = n;
    out[9] = aspect;
    out[10] = 0.35;
    out[11] = sh;
    return { count: n, shCount: sh, data: out };
  }

  // 📝 Any kind in the taxonomy can be added, including the ones with no GPU path yet. Those are drawn
  //    with a plain warning rather than hidden, because a kind that silently does nothing is worse than
  //    one that says it does nothing.
  function addForce(Id) {
    const Fresh = PE.Forces.BaseField(Id);
    const Same = state.forces.filter((One) => One.Kind === Id).length;
    if (Same) Fresh.Name = Fresh.Name + " " + (Same + 1);
    if (Fresh.Reaches !== PE.Forces.Reach.Everywhere) Fresh.Centre = [0, 2, 0];
    state.forces.push(Fresh);
    select({ type: "forces" });
    return Fresh;
  }

  // ─── One panel for every field ───────────────────────────────────────────────────────────────────────
  //
  // Grouped by what each field contributes rather than by where it used to be edited. A reader can see
  //    at a glance that wind and gravity are different quantities, which is the point of the taxonomy,
  //    and the grouping is read from the spec rather than hand-sorted here.
  const GIVE_PROSE = {
    flow: ["FLOW \u00b7 m/s", "Velocity the air carries. A receiver is dragged toward it at its own coupling, so the same wind moves a leaf and a hailstone differently, and nothing in it accelerates forever. These sum into one lattice however many there are."],
    accelerate: ["ACCELERATE \u00b7 m/s\u00b2", "Added straight to velocity. Coupling has no say, which is why gravity belongs here and not in the wind: it must move everything by the same amount. Evaluated per receiver, so these cost more than flow does."],
    damp: ["DAMP \u00b7 1/s", "Scales velocity down. Removes energy rather than adding a direction, so unlike a force it can never start something moving."],
  };

  function renderForcesInspector(root) {
    root.append(el("div", { class: "insp-head" },
      el("span", { class: "insp-path", text: "Environment / Force fields" }),
      el("h2", { text: "Force fields" }),
      el("p", { class: "insp-sub", text: "Every field in the scene, grouped by what it contributes. Wind is the flow kinds; gravity and attraction are acceleration. Black hole and magnetic systems carry their own, which move with them and are not listed here." })));

    const Adders = el("div", { class: "btn-row wrap" });
    for (const Kind of PE.Forces.ForceFieldKinds) {
      Adders.append(button("+ " + Kind.Name, () => { addForce(Kind.Id); renderInspector(); },
                           Kind.Id === "gravity" ? "accent" : ""));
    }
    root.append(card("Add", null, Adders));

    const { Baked, Live } = PE.Forces.Bakeable(state.forces);
    root.append(card("Cost", "WHAT THIS SCENE PAYS",
      el("div", { class: "stat-grid" },
        el("div", { class: "stat" }, el("span", { class: "stat-k", text: "Summed into the lattice" }),
          el("span", { class: "stat-v", text: String(Baked.length) })),
        el("div", { class: "stat" }, el("span", { class: "stat-k", text: "Evaluated per receiver" }),
          el("span", { class: "stat-v", text: String(Live.length) }))),
      note("Flow fields sum into one velocity texture and then cost one sample each step no matter how many there are. Acceleration fields cannot: the useful ones are unbounded and a bounded texture would clip them, so each is evaluated per receiver per step.")));

    for (const Give of ["flow", "accelerate", "damp"]) {
      const Mine = state.forces
        .map((Field, Index) => ({ Field, Index }))
        .filter(({ Field }) => PE.Forces.KindById(Field.Kind)?.Give === Give);
      if (!Mine.length) continue;
      const [Kicker, Prose] = GIVE_PROSE[Give];
      root.append(el("p", { class: "note group-note", text: Prose }));
      for (const { Field, Index } of Mine) root.append(forceCard(Field, Index, Kicker));
    }

    if (!state.forces.length) root.append(note("No fields. Add one above."));
  }

  function forceCard(Field, Index, Kicker) {
    const Kind = PE.Forces.KindById(Field.Kind);
    const Body = [
      el("label", { class: "row" }, el("span", { class: "row-k", text: "Name" }),
        el("input", { type: "text", value: Field.Name, "aria-label": "Field name",
                      oninput: (Event) => { Field.Name = Event.target.value || Kind.Name; renderOutliner(); } })),
      checkRow("Enabled", () => Field.Enabled, (v) => (Field.Enabled = v)),
      rangeRow("Strength", () => Field.Strength, (v) => (Field.Strength = v),
               { min: -20, max: 20, step: 0.05, digits: 2, unit: Kind.Unit }),
    ];

    if (Field.Reaches === PE.Forces.Reach.Everywhere) {
      Body.push(note("Reaches everywhere. This is not a very large sphere \u2014 it has no centre and no edge, which is the case a bounded lattice could not have held."));
    } else {
      Body.push(vecRow("Centre", Field.Centre, { min: -20, max: 20, step: 0.05, digits: 2, unit: "m" }));
      Body.push(rangeRow("Radius", () => Field.Radius, (v) => (Field.Radius = v),
                         { min: 0.5, max: 30, step: 0.05, digits: 2, unit: "m" }));
      Body.push(selectRow("Falloff", Object.values(PE.Forces.Falloff).map((One) => [One, One]),
                          () => Field.Fades, (v) => (Field.Fades = v)));
    }

    if (Kind.Give === PE.Forces.Contribution.Flow) {
      Body.push(rangeRow("Bearing", () => Math.round(PE.Forces.Bearing(Field)),
                         (v) => (Field.Direction = PE.Forces.Along(v)),
                         { min: 0, max: 360, step: 1, digits: 0, unit: "\u00b0" }));
      if (Field.Kind === "gust") {
        Body.push(rangeRow("Band speed", () => Field.Rate, (v) => (Field.Rate = v),
                           { min: 0, max: 2, step: 0.01, digits: 2 }));
      }
    }
    if (Field.Kind === "attract") {
      Body.push(rangeRow("Swirl", () => Field.Swirl, (v) => (Field.Swirl = v),
                         { min: 0, max: 12, step: 0.05, digits: 2 }));
      Body.push(rangeRow("Swallow radius", () => Field.Swallow, (v) => (Field.Swallow = v),
                         { min: 0, max: 3, step: 0.01, digits: 2, unit: "m" }));
    }

    Body.push(rangeRow("Start", () => Field.Begins, (v) => (Field.Begins = v),
                       { min: 0, max: 60, step: 0.1, digits: 1, unit: "s" }));
    Body.push(rangeRow("Lasts (0 = forever)", () => Field.Lasts, (v) => (Field.Lasts = v),
                       { min: 0, max: 120, step: 0.1, digits: 1, unit: "s" }));
    Body.push(rangeRow("Repeats (0 = once)", () => Field.Repeats, (v) => (Field.Repeats = v),
                       { min: 0, max: 120, step: 0.1, digits: 1, unit: "s" }));

    if (!PE.Forces.Packable(Field)) {
      Body.push(note("\u26a0\ufe0f This kind has no GPU path yet, so it is authored and described but does not move anything. It is listed rather than hidden because a control that silently does nothing is worse than one that says so."));
    }

    Body.push(el("div", { class: "btn-row" },
      button("Remove", () => { state.forces.splice(Index, 1); renderInspector(); }, "danger")));

    return card(Field.Name, Kicker, ...Body);
  }

  function renderInspector() {
    const root = $("#inspector");
    root.innerHTML = "";
    const live = (state.ui.liveEls = { vis: state.ui.liveEls.vis });
    const sel = state.selection;
    // Rows capture their system in a closure, so the edit funnel is told which one they belong to.
    //    Cleared for the panels that edit the world rather than a system.
    PE.Edits.For(null);
    if (sel.type === "system") {
      const sys = systemById(sel.id);
      if (sys) { PE.Edits.For(sys); renderSystemInspector(root, sys, live); }
    } else if (sel.type === "wind") {
      renderWindInspector(root, live);
    } else if (sel.type === "lightning") {
      renderLightningInspector(root, live);
    } else if (sel.type === "forces") {
      renderForcesInspector(root);
    }
    renderOutliner();
    updateLive();
  }

  // Light fibres: the same card language as particle systems, with fibre controls in place of emitter ones.
  // 📝 Fibre colour: solid, ramp along the fibre (stops at chosen positions) or palette (one colour per strand).
  function colourCard(f) {
    const body = [
      selectRow("Mode", [["solid", "Solid"], ["ramp", "Ramp along fibre"], ["palette", "Palette per strand"]],
        () => f.colourMode, (v) => { f.colourMode = v; renderInspector(); }),
      colorRow("Accent colour", f.colC),
    ];
    f.stops.forEach((st, i) => {
      body.push(colorRow(`Stop ${i + 1}`, st.col));
      if (f.colourMode === "ramp") {
        body.push(rangeRow("Position", () => st.pos, (v) => (st.pos = v), { min: 0, max: 1, step: 0.01, digits: 2 }));
      }
      if (f.stops.length > 1) {
        body.push(button("Remove stop", () => { f.stops.splice(i, 1); renderInspector(); }, "danger"));
      }
    });
    if (f.stops.length < PE.MAX_STOPS) {
      body.push(button("+ Add stop", () => {
        const last = f.stops[f.stops.length - 1];
        f.stops.push({ pos: Math.min(1, last.pos + 0.2), col: last.col.slice() });
        renderInspector();
      }, "accent"));
    }
    return card("Colour", f.colourMode.toUpperCase(), ...body);
  }

  function renderFibreInspector(root, sys) {
    const p = sys.p;
    const f = p.fibre;
    const preset = PE.presetById(sys.presetId);
    const trail = f.shape === "trail";
    const ribbon = f.shape === "ribbon";
    root.append(el("div", { class: "insp-head" },
      el("span", { class: "insp-path", text: "Light fibres / " + f.shape }),
      el("h2", { text: sys.name }),
      el("p", { class: "insp-sub", text: preset.blurb })));

    root.append(card("System", null,
      el("label", { class: "row" }, el("span", { class: "row-k", text: "Name" }),
        el("input", { type: "text", value: sys.name, "aria-label": "System name", oninput: (e) => { sys.name = e.target.value || "Untitled"; renderOutliner(); } })),
      el("div", { class: "btn-row" },
        button(p.visible ? "Hide" : "Show", () => { p.visible = !p.visible; renderInspector(); }),
        button("Delete", () => removeSystem(sys), "danger")),
      note("Fibres are analytic: the GPU evaluates each curve from the loop phase, so they cost no simulation. Wind does not move them.")));

    const form = [
      selectRow("Form", [["streak", "Streak (light head)"], ["ribbon", "Ribbon (rippling sheet)"], ["trail", "Trail (rides a path)"]],
        () => f.shape, (v) => { f.shape = v; renderInspector(); }),
      rangeRow("Strands", () => f.strands, (v) => (f.strands = v), { min: 1, max: 1200, step: 1, digits: 0 }),
      rangeRow("Segments", () => f.segments, (v) => (f.segments = v), { min: 4, max: 160, step: 1, digits: 0 }),
      rangeRow("Seed", () => f.seed, (v) => (f.seed = v), { min: 1, max: 999, step: 1, digits: 0 }),
    ];
    if (!trail) form.push(rangeRow("Length", () => f.length, (v) => (f.length = v), { min: 0.5, max: 30, step: 0.01, digits: 2, unit: "m" }));
    form.push(rangeRow("Spread", () => f.spread, (v) => (f.spread = v), { min: 0, max: 10, step: 0.01, digits: 2, unit: "m" }));
    form.push(rangeRow("Wobble", () => f.amplitude, (v) => (f.amplitude = v), { min: 0, max: 4, step: 0.01, digits: 2, unit: "m" }));
    form.push(rangeRow("Waves per loop", () => f.frequency, (v) => (f.frequency = v), { min: 0, max: 8, step: 0.01, digits: 2 }));
    if (ribbon) {
      form.push(rangeRow("Sheet width", () => f.sheetWidth, (v) => (f.sheetWidth = v), { min: 0.5, max: 30, step: 0.01, digits: 2, unit: "m" }));
      form.push(rangeRow("Ripple", () => f.ripple, (v) => (f.ripple = v), { min: 0, max: 4, step: 0.01, digits: 2, unit: "m" }));
      form.push(rangeRow("Sheet waves", () => f.waves, (v) => (f.waves = v), { min: 0, max: 6, step: 0.01, digits: 2 }));
    }
    if (ribbon || trail) {
      form.push(rangeRow("Phase spread", () => f.phaseSpread, (v) => (f.phaseSpread = v), { min: 0, max: 6.283, step: 0.01, digits: 2, unit: "rad" }));
    }
    if (trail) {
      form.push(selectRow("Path", PE.FIBRE_PATHS.map((n) => [n, n]), () => f.pathShape, (v) => (f.pathShape = v)));
      form.push(rangeRow("Path size", () => f.pathSize, (v) => (f.pathSize = v), { min: 1, max: 10, step: 0.1, digits: 1, unit: "m" }));
      form.push(rangeRow("Trail length", () => f.trailLength, (v) => (f.trailLength = v), { min: 0.02, max: 1, step: 0.01, digits: 2, unit: "share" }));
    }
    root.append(card("Form", null, ...form));

    root.append(card("Motion", "LOOP", 
      rangeRow("Loop period", () => f.loopSeconds, (v) => (f.loopSeconds = v), { min: 2, max: 60, step: 0.5, digits: 1, unit: "s" }),
      rangeRow("Speed multiple", () => f.harmonic, (v) => (f.harmonic = v), { min: 1, max: 4, step: 1, digits: 0 }),
      rangeRow("Light heads per loop", () => f.windowCycles, (v) => (f.windowCycles = v), { min: 1, max: 6, step: 1, digits: 0 }),
      rangeRow("Light window", () => f.window, (v) => (f.window = v), { min: 0.02, max: 1, step: 0.01, digits: 2 }),
      selectRow("Pulse shape", PE.PULSE_SHAPES.map((n) => [n, n]), () => f.pulseShape, (v) => (f.pulseShape = v)),
      rangeRow("Pulses per loop", () => f.pulseRate, (v) => (f.pulseRate = v), { min: 0, max: 4, step: 1, digits: 0 }),
      rangeRow("Pulse depth", () => f.pulseDepth, (v) => (f.pulseDepth = v), { min: 0, max: 1, step: 0.01, digits: 2 })));

    const placement = [
      vecRow("Origin", p.origin, { min: -20, max: 20, step: 0.05, digits: 2, unit: "m" }),
      rangeRow("Scale", () => f.scale, (v) => (f.scale = v), { min: 0.05, max: 10, step: 0.01, digits: 2, unit: "×" }),
    ];
    if (!trail && !ribbon) placement.push(vecRow("Direction", p.dir, { min: -1, max: 1, step: 0.05, digits: 2 }));
    root.append(card("Placement", null, ...placement));

    root.append(card("Look", "RENDER",
      rangeRow("Thickness", () => f.thickness, (v) => (f.thickness = v), { min: 0.5, max: 8, step: 0.05, digits: 2, unit: "px" }),
      rangeRow("Taper", () => f.taper, (v) => (f.taper = v), { min: 0, max: 1, step: 0.01, digits: 2 }),
      rangeRow("Intensity", () => f.intensity, (v) => (f.intensity = v), { min: 0, max: 12, step: 0.01, digits: 2 }),
      rangeRow("Halo", () => f.halo, (v) => (f.halo = v), { min: 0, max: 2, step: 0.01, digits: 2 }),
      rangeRow("Idle brightness", () => f.baseline, (v) => (f.baseline = v), { min: 0, max: 1, step: 0.01, digits: 2 }),
      rangeRow("Accent amount", () => f.accentMix, (v) => (f.accentMix = v), { min: 0, max: 1, step: 0.01, digits: 2 })));

    root.append(colourCard(f));

    root.append(card("Head sparks", "SPARKS",
      rangeRow("Sparks", () => f.sparks, (v) => (f.sparks = v), { min: 0, max: 1, step: 0.01, digits: 2 }),
      rangeRow("Spark size", () => f.sparkSize, (v) => (f.sparkSize = v), { min: 0, max: 0.4, step: 0.001, digits: 3, unit: "m" }),
      rangeRow("Spark brightness", () => f.sparkBrightness, (v) => (f.sparkBrightness = v), { min: 0, max: 12, step: 0.01, digits: 2 })));
  }

  function renderSystemInspector(root, sys, live) {
    const p = sys.p;
    if (p.kind === 7) return renderFibreInspector(root, sys);
    const mol = p.kind === 3 || p.kind === 4;
    const preset = PE.presetById(sys.presetId);
    root.append(el("div", { class: "insp-head" },
      el("span", { class: "insp-path", text: "Particle system / " + kindLabel(p.kind) }),
      el("h2", { text: sys.name }),
      el("p", { class: "insp-sub", text: preset.blurb })));

    root.append(card("System", null,
      el("label", { class: "row" }, el("span", { class: "row-k", text: "Name" }),
        el("input", { type: "text", value: sys.name, "aria-label": "System name", oninput: (e) => { sys.name = e.target.value || "Untitled"; renderOutliner(); } })),
      el("div", { class: "btn-row" },
        button(sys.p.visible ? "Hide" : "Show", () => { sys.p.visible = !sys.p.visible; renderInspector(); }),
        button(mol ? "Re-seed" : "Clear", () => rebuildGpu(sys)),
        button("Burst", () => { sys.pendingBurst += p.transition ? sys.gpu.cap : Math.max(60, Math.round(sys.gpu.cap * 0.4)); sys.overrideOrigin = null; }, "accent"),
        button("Delete", () => removeSystem(sys), "danger")),
      note(mol ? "Molecular systems keep every particle alive and integrate them on the GPU each sub-step." : "Burst spawns a one-off batch at the emitter origin.")));

    // 📝 Transition presets use 384 (coins) and 1536 (derez cubes = 6 x 16 x 16 surface cells); show the current size too.
    const capOpts = [384, 512, 1024, 1536, 2048, 4096, 8192];
    if (!capOpts.includes(p.capacity)) capOpts.push(p.capacity);
    capOpts.sort((a, b) => a - b);
    const capSel = selectRow("Capacity", capOpts.map((n) => [n, String(n).replace(/\B(?=(\d{3})+$)/g, " ")]), () => p.capacity,
      (v) => { p.capacity = v; rebuildGpu(sys); renderInspector(); });
    const emitCard = [capSel];
    if (!mol) {
      emitCard.push(rangeRow("Rate", () => p.rate, (v) => (p.rate = v), { min: 0, max: 2000, step: 1, unit: "/s" }));
    }
    emitCard.push(selectRow("Shape", [[0, "Point"], [1, "Sphere volume"], [2, "Disc"], [3, "Box"], [4, "Box surface grid"]], () => p.emitShape, (v) => (p.emitShape = v)));
    emitCard.push(vecRow("Origin", p.origin, { min: -20, max: 20, step: 0.05, digits: 2, unit: "m" }));
    emitCard.push(rangeRow("Radius", () => p.radius, (v) => (p.radius = v), { min: 0, max: 8, step: 0.01, digits: 2, unit: "m" }));
    if (mol) {
      emitCard.push(rangeRow("Box half", () => p.boxHalf[0], (v) => { p.boxHalf = [v, v, v]; }, { min: 0.2, max: 4, step: 0.01, digits: 2, unit: "m" }));
    } else {
      emitCard.push(vecRow("Box half", p.boxHalf, { min: 0, max: 10, step: 0.1, digits: 1, unit: "m" }));
      emitCard.push(vecRow("Direction", p.dir, { min: -1, max: 1, step: 0.05, digits: 2 }));
      emitCard.push(rangeRow("Spread", () => p.spread, (v) => (p.spread = v), { min: 0, max: 3.1416, step: 0.01, digits: 2, unit: "rad" }));
      emitCard.push(rangeRow("Speed min", () => p.speedMin, (v) => (p.speedMin = Math.min(v, p.speedMax)), { min: 0, max: 20, step: 0.05, digits: 2, unit: "m/s" }));
      emitCard.push(rangeRow("Speed max", () => p.speedMax, (v) => (p.speedMax = Math.max(v, p.speedMin)), { min: 0, max: 20, step: 0.05, digits: 2, unit: "m/s" }));
      emitCard.push(rangeRow("Life min", () => p.lifeMin, (v) => (p.lifeMin = Math.min(v, p.lifeMax)), { min: 0.05, max: 20, step: 0.05, digits: 2, unit: "s" }));
      emitCard.push(rangeRow("Life max", () => p.lifeMax, (v) => (p.lifeMax = Math.max(v, p.lifeMin)), { min: 0.05, max: 20, step: 0.05, digits: 2, unit: "s" }));
    }
    root.append(card("Emitter", mol ? "INITIAL FILL" : "SPAWN", ...emitCard));
    if (p.transition) {
      // 📝 Transition card: hold timing, burst/fall split, and derez child cubes (cube shape only).
      const tr = p.transition;
      const trCard = [
        rangeRow("Hold base", () => tr.holdBase, (v) => (tr.holdBase = v), { min: 0, max: 10, step: 0.05, digits: 2, unit: "s" }),
        rangeRow("Release spread", () => tr.holdSpread, (v) => (tr.holdSpread = v), { min: 0, max: 10, step: 0.05, digits: 2, unit: "s" }),
        rangeRow("Burst share", () => tr.burstShare, (v) => (tr.burstShare = v), { min: 0, max: 1, step: 0.01, digits: 2 }),
      ];
      trCard.unshift(
        selectRow("Flight", [[0, "Burst or fall"], [1, "Rebuild from outside"], [2, "Stack in column"]],
          () => tr.assemble || 0, (v) => { tr.assemble = v; renderInspector(); }),
        selectRow("After release", [["none", "Cube (fades out)"], ["butterfly", "Becomes a butterfly"]],
          () => tr.after || "none", (v) => (tr.after = v)),
        selectRow("Release wave", [["corner", "Corner (derez)"], ["melt", "Melt (patchy)"], ["glitch", "Glitch (blocky)"]],
          () => tr.wave || "corner", (v) => (tr.wave = v)),
      );
      if (tr.assemble > 0) {
        trCard.push(rangeRow(tr.assemble === 2 ? "Drop height" : "Start distance", () => tr.shell || 0, (v) => (tr.shell = v),
          { min: 0.5, max: 8, step: 0.05, digits: 2, unit: "m" }));
      }
      if (p.shape === 4) {
        trCard.push(checkRow("Break on floor impact", () => tr.fragment, (v) => (tr.fragment = v)));
        trCard.push(rangeRow("Child cube life", () => tr.childLife, (v) => (tr.childLife = v), { min: 0.1, max: 6, step: 0.05, digits: 2, unit: "s" }));
      }
      root.append(card("Transition", "RELEASE", ...trCard));
    }
    if (p.blackHole && p.attractor) {
      const bh = p.blackHole;
      const at = p.attractor;
      root.append(card("Black hole", "PULL + LENS",
        rangeRow("Horizon", () => bh.horizon, (v) => (bh.horizon = v), { min: 0.1, max: 2, step: 0.01, digits: 2, unit: "m" }),
        rangeRow("Lensing strength", () => bh.einstein, (v) => (bh.einstein = v), { min: 0, max: 6, step: 0.05, digits: 2, unit: "×H" }),
        rangeRow("Pull strength", () => at.strength, (v) => (at.strength = v), { min: 0, max: 30, step: 0.1, digits: 1, unit: "m/s²" }),
        rangeRow("Pull radius", () => at.radius, (v) => (at.radius = v), { min: 1, max: 40, step: 0.1, digits: 1, unit: "m" }),
        rangeRow("Swirl", () => at.swirl, (v) => (at.swirl = v), { min: 0, max: 12, step: 0.05, digits: 2 }),
        rangeRow("Swallow radius", () => at.swallow, (v) => (at.swallow = v), { min: 0, max: 3, step: 0.01, digits: 2, unit: "m" })));
    }
    if (p.heatShimmer) {
      const hs = p.heatShimmer;
      root.append(card("Heat shimmer", "DISTORTION",
        rangeRow("Radius", () => hs.radius, (v) => (hs.radius = v), { min: 0.2, max: 6, step: 0.05, digits: 2, unit: "m" }),
        rangeRow("Strength", () => hs.strength, (v) => (hs.strength = v), { min: 0, max: 0.05, step: 0.001, digits: 3 }),
        rangeRow("Frequency", () => hs.freq, (v) => (hs.freq = v), { min: 2, max: 40, step: 0.5, digits: 1 }),
        rangeRow("Rise speed", () => hs.rate, (v) => (hs.rate = v), { min: 0, max: 5, step: 0.05, digits: 2, unit: "×" })));
    }
    if (p.magneticField) {
      const mf = p.magneticField;
      root.append(card("Magnetic dipole", "FIELD LINES",
        rangeRow("Drive strength", () => mf.strength, (v) => (mf.strength = v), { min: 0, max: 10, step: 0.05, digits: 2, unit: "m/s²" }),
        rangeRow("Guide rate", () => mf.guide, (v) => (mf.guide = v), { min: 0, max: 12, step: 0.05, digits: 2, unit: "1/s" }),
        rangeRow("Radius", () => mf.radius, (v) => (mf.radius = v), { min: 1, max: 20, step: 0.1, digits: 1, unit: "m" })));
    }

    const partCard = [];
    if (!mol) {
      partCard.push(selectRow("Shape", [[0, "Streak"], [1, "Sphere"], [2, "Leaf / paper"], [3, "Soft glow"], [4, "Chip cube"], [5, "Coin"], [6, "Bubble"]], () => p.shape, (v) => (p.shape = v)));
      if (p.kind === 1) partCard.push(selectRow("Leaf mode", [[0, "Leaf"], [1, "Paper"]], () => p.leafMode, (v) => (p.leafMode = v)));
      partCard.push(selectRow("Blend", [["add", "Additive (glow)"], ["alpha", "Alpha (premultiplied)"]], () => p.blend, (v) => (p.blend = v)));
      partCard.push(rangeRow("Size start", () => p.sizeStart, (v) => (p.sizeStart = v), { min: 0.002, max: 2, step: 0.001, digits: 3, unit: "m" }));
      partCard.push(rangeRow("Size end", () => p.sizeEnd, (v) => (p.sizeEnd = v), { min: 0.002, max: 3, step: 0.001, digits: 3, unit: "m" }));
    } else {
      partCard.push(selectRow("Blend", [["add", "Additive (glow)"], ["alpha", "Alpha (premultiplied)"]], () => p.blend, (v) => (p.blend = v)));
    }
    partCard.push(rangeRow("Size scale", () => p.sizeScale, (v) => (p.sizeScale = v), { min: 0.1, max: 4, step: 0.01, digits: 2, unit: "×" }));
    partCard.push(colorRow(mol ? "Colour A" : "Colour start", p.colA));
    partCard.push(colorRow(mol ? "Colour B" : "Colour end", p.colB));
    if (p.kind === 4) partCard.push(colorRow("Colour C", p.colC));
    if (!mol) {
      partCard.push(rangeRow("Drag", () => p.drag, (v) => (p.drag = v), { min: 0, max: 5, step: 0.01, digits: 2, unit: "1/s" }));
      partCard.push(rangeRow("Gravity", () => p.gravity, (v) => (p.gravity = v), { min: -1, max: 2, step: 0.01, digits: 2, unit: "g" }));
      partCard.push(rangeRow("Buoyancy", () => p.buoyancy, (v) => (p.buoyancy = v), { min: -4, max: 6, step: 0.05, digits: 2, unit: "m/s²" }));
      partCard.push(rangeRow("Wind coupling", () => p.windCoupling, (v) => (p.windCoupling = v), { min: 0, max: 5, step: 0.01, digits: 2, unit: "1/s" }));
      partCard.push(rangeRow("Bounce", () => p.bounce, (v) => (p.bounce = v), { min: 0, max: 1, step: 0.01, digits: 2 }));
      partCard.push(rangeRow("Flutter", () => p.flutter, (v) => (p.flutter = v), { min: 0, max: 5, step: 0.05, digits: 2, unit: "m/s²" }));
    }
    root.append(card("Particle", p.kind === 3 || p.kind === 4 ? "APPEARANCE" : "RENDER", ...partCard));

    if (mol) {
      const phys = [
        rangeRow("Temperature kT", () => p.temperature, (v) => (p.temperature = v), { min: 0.02, max: 3, step: 0.01, digits: 2, unit: "ε" }),
        rangeRow("Thermostat γ", () => p.damping, (v) => (p.damping = v), { min: 0, max: 10, step: 0.05, digits: 2, unit: "1/s" }),
        rangeRow("Sim step", () => p.simDt, (v) => (p.simDt = v), { min: 0.001, max: 0.01, step: 0.0005, digits: 4, unit: "s" }),
        rangeRow("Diameter σ", () => p.sigma, (v) => (p.sigma = v), { min: 0.03, max: 0.2, step: 0.001, digits: 3, unit: "m" }),
      ];
      if (p.kind === 3) {
        phys.push(rangeRow("Well depth ε", () => p.epsilon, (v) => (p.epsilon = v), { min: 0, max: 1, step: 0.01, digits: 2 }));
      } else {
        phys.push(rangeRow("Reaction rate", () => p.reactRate, (v) => (p.reactRate = v), { min: 0, max: 30, step: 0.1, digits: 1, unit: "1/s" }));
        phys.push(rangeRow("Dissociation", () => p.dissociation, (v) => (p.dissociation = v), { min: 0, max: 5, step: 0.05, digits: 2, unit: "1/s" }));
        phys.push(rangeRow("Reaction radius", () => p.reactRadius, (v) => (p.reactRadius = v), { min: 0.02, max: 0.2, step: 0.005, digits: 3, unit: "m" }));
        phys.push(rangeRow("Initial A fraction", () => p.fracA, (v) => (p.fracA = v), { min: 0, max: 1, step: 0.01, digits: 2 }));
      }
      phys.push(note(p.kind === 3
        ? "Lennard-Jones pairs on a spatial hash grid, Langevin thermostat, reflecting box walls. The wind field does not act on atoms."
        : "A + B → C on contact (probability per second), C → A or B spontaneously. Species counts come back to the CPU as a 32-byte readback."));
      root.append(card("Physics", "MOLECULAR", ...phys));
    }

    const read = [];
    const liveEls = {};
    const stat = (label, key) => {
      const v = el("span", { class: "stat-v", text: "–" });
      liveEls[key] = v;
      return el("div", { class: "stat" }, el("span", { class: "stat-k", text: label }), v);
    };
    read.push(el("div", { class: "stat-grid" },
      stat("Alive", "alive"), stat("Readback latency", "readMs"), stat("Readbacks", "reads"),
      mol ? stat("Temperature (<v²>/3)", "temp") : null,
      p.kind === 4 ? stat("Species", "species") : null));
    read.push(note("Each readback copies 32 bytes (counts and energy) from a GPU reduction. The map is asynchronous, so the frame never waits for it."));
    read.push(el("div", { class: "btn-row" },
      button("Benchmark full readback", async (e) => {
        const btn = e.currentTarget;
        btn.disabled = true;
        btn.textContent = "Measuring…";
        const r = await state.engine.benchmarkFullReadback(sys.gpu);
        btn.disabled = false;
        btn.textContent = "Benchmark full readback";
        out.textContent = `${fmtInt(r.bytes / 1024)} KiB (${fmtInt(sys.gpu.cap)} × 80 B) in ${r.ms.toFixed(2)} ms ≈ ${r.mbPerSecond.toFixed(0)} MiB/s · ${fmtInt(r.alive)} alive. Measured on this browser's adapter; the full buffer is not used by the editor.`;
      })));
    const out = el("p", { class: "note bench", text: "Not measured yet." });
    read.push(out);
    root.append(card("Readback", "CPU ← GPU", ...read));
    state.ui.liveEls = Object.assign(live, liveEls, { sys: true });
  }

  function renderWindInspector(root, live) {
    const w = state.wind;
    root.append(el("div", { class: "insp-head" },
      el("span", { class: "insp-path", text: "Environment / Wind field" }),
      el("h2", { text: "Wind field" }),
      el("p", { class: "insp-sub", text: "A 3D grid of velocities (24 × 12 × 24 voxels over 12 × 8 × 12 m). Every particle system samples it each step; arrows show the grid itself." })));
    root.append(card("Field", "GRID",
      rangeRow("Wind scale", () => w.windScale, (v) => (w.windScale = v), { min: 0, max: 3, step: 0.01, digits: 2, unit: "×" }),
      rangeRow("Turbulence", () => w.turbulence, (v) => (w.turbulence = v), { min: 0, max: 2, step: 0.01, digits: 2, unit: "m/s" }),
      rangeRow("Swirl", () => w.swirl, (v) => (w.swirl = v), { min: 0, max: 4, step: 0.05, digits: 2, unit: "m/s" }),
      rangeRow("Arrow full scale", () => w.arrowRef, (v) => (w.arrowRef = v), { min: 1, max: 15, step: 0.1, digits: 1, unit: "m/s" }),
      checkRow("Show grid arrows", () => w.showArrows, (v) => (w.showArrows = v)),
      rangeRow("Arrow density", () => w.arrowStride, (v) => (w.arrowStride = v), { min: 1, max: 6, step: 1, digits: 0, unit: "voxel step" }),
      checkRow("Show floor", () => w.showFloor, (v) => (w.showFloor = v)),
      checkRow("Show domain box", () => w.showDomain, (v) => (w.showDomain = v)),
      note("Arrow colour is speed: blue calm, cyan, yellow, red fast. Density is the voxel step between arrows: 1 draws every voxel, 3 draws every third.")));

    // 📝 The components that used to be edited here are force fields now, and live in the one panel with
    //    the rest. What is left is the lattice itself: its global terms and how it is drawn. Turbulence
    //    and swirl stay because they are properties of the sampling grid, added to every cell of it,
    //    rather than fields anybody placed.
    const Flowing = flowFields();
    root.append(card("Fields in this lattice", "FLOW", 
      ...(Flowing.length
        ? Flowing.map((Field) => el("div", { class: "stat" },
            el("span", { class: "stat-k", text: Field.Name }),
            el("span", { class: "stat-v", text: Field.Enabled ? Field.Strength.toFixed(2) + " m/s" : "off" })))
        : [note("No flow fields. The lattice is empty and every receiver coasts.")]),
      el("div", { class: "btn-row" },
        button("Edit force fields", () => select({ type: "forces" }), "accent"))));

    const probeR = el("span", { class: "stat-v", text: "–" });
    const probeMs = el("span", { class: "stat-v", text: "–" });
    live.probe = probeR;
    live.probeMs = probeMs;
    const px = [0, 1, 2].map((i) => state.probe[i]);
    root.append(card("Probe", "CPU READBACK", 
      vecRow("Position", state.probe, { min: -6, max: 8, step: 0.05, digits: 2, unit: "m", onChange: () => {} }),
      checkRow("Probe on", () => state.probeOn, (v) => (state.probeOn = v)),
      el("div", { class: "stat-grid one" }, el("div", { class: "stat" }, el("span", { class: "stat-k", text: "Wind at probe" }), probeR),
        el("div", { class: "stat" }, el("span", { class: "stat-k", text: "Readback" }), probeMs)),
      note("One texel (8 bytes, rgba16float) is copied from the wind grid each few frames and mapped asynchronously. This is the cheap direction of CPU readback.")));
    void px;
    state.ui.liveEls = Object.assign(live, { probe: probeR, probeMs });
  }

  function renderLightningInspector(root, live) {
    const L = state.lightning;
    root.append(el("div", { class: "insp-head" },
      el("span", { class: "insp-path", text: "Effects / Lightning" }),
      el("h2", { text: "Lightning" }),
      el("p", { class: "insp-sub", text: "Branching fractal bolts built on the CPU each strike and drawn as glowing GPU ribbons. The flash lights the frame; thunder is delayed by distance ÷ 343 m/s." })));
    const strikeBtn = button("Strike now", () => strikeNow(), "accent");
    const soundBtn = button(L.thunder.enabled ? "Sound on" : "Sound off", () => {
      if (!L.thunder.enabled) {
        if (!L.thunder.enable()) return;
      } else {
        L.thunder.disable();
      }
      refreshLightningReadout();
    });
    live.soundBtn = soundBtn;
    const strikes = el("span", { class: "stat-v", text: fmtInt(L.strikes) });
    const last = el("span", { class: "stat-v", text: L.lastStrike ? "" : "–" });
    live.strikes = strikes;
    live.lastStrike = last;
    live.light = true;
    root.append(card("Strikes", "AUTO / MANUAL",
      checkRow("Automatic strikes", () => L.auto, (v) => (L.auto = v)),
      rangeRow("Interval min", () => L.intervalMin, (v) => { L.intervalMin = Math.min(v, L.intervalMax); }, { min: 0.2, max: 30, step: 0.1, digits: 1, unit: "s" }),
      rangeRow("Interval max", () => L.intervalMax, (v) => { L.intervalMax = Math.max(v, L.intervalMin); }, { min: 0.2, max: 60, step: 0.1, digits: 1, unit: "s" }),
      rangeRow("Distance", () => L.distance, (v) => (L.distance = v), { min: 50, max: 6000, step: 10, digits: 0, unit: "m" }),
      el("div", { class: "btn-row" }, strikeBtn, soundBtn),
      el("div", { class: "stat-grid one" },
        el("div", { class: "stat" }, el("span", { class: "stat-k", text: "Strikes" }), strikes),
        el("div", { class: "stat" }, el("span", { class: "stat-k", text: "Last strike point" }), last)),
      note("Strikes fire the Strike sparks system. Sound needs one click (browser autoplay rule), then plays a rumble after distance ÷ 343 s.")));
    root.append(card("Bolt shape", "FRACTAL",
      rangeRow("Roughness", () => L.amp, (v) => (L.amp = v), { min: 0.2, max: 5, step: 0.05, digits: 2, unit: "m" }),
      rangeRow("Detail levels", () => L.levels, (v) => (L.levels = Math.round(v)), { min: 3, max: 7, step: 1, digits: 0 }),
      rangeRow("Branch chance", () => L.branchChance, (v) => (L.branchChance = v), { min: 0, max: 1, step: 0.01, digits: 2 })));
    root.append(card("Lightning web", "ARCS",
      checkRow("Arcs between systems", () => state.web.enabled, (v) => { state.web.enabled = v; state.web.nextAt = 0; }),
      rangeRow("Re-strike every", () => state.web.interval, (v) => (state.web.interval = v), { min: 0.1, max: 3, step: 0.05, digits: 2, unit: "s" }),
      rangeRow("Links per system", () => state.web.k, (v) => (state.web.k = Math.round(v)), { min: 1, max: 4, step: 1, digits: 0 }),
      note("Each visible particle system's origin is a node. Every node links to its nearest neighbours with a branching bolt, redrawn each interval. Bolts are drawn only; they do not light or strike particles.")));
    state.ui.liveEls = Object.assign(live, { light: true });
    refreshLightningReadout();
  }

  // ----------------------------------------------------------------- toolbar
  function setupToolbar() {
    $("#ol-search").addEventListener("input", (e) => { state.ui.olQuery = e.target.value; renderOutliner(); });
    const playBtn = $("#btn-play");
    const sync = () => {
      playBtn.textContent = state.playing ? "Pause" : "Play";
      playBtn.setAttribute("aria-pressed", String(!state.playing));
      $("#st-state").textContent = state.playing ? "Live" : "Paused";
      $("#st-state").className = "pill " + (state.playing ? "ok" : "warn");
      $("#btn-top").setAttribute("aria-pressed", String(state.cam.pitch > 1.2));
    };
    playBtn.addEventListener("click", () => { state.playing = !state.playing; sync(); });
    $("#btn-reset").addEventListener("click", resetAll);
    $("#btn-strike").addEventListener("click", strikeNow);
    $("#btn-sound").addEventListener("click", () => {
      const L = state.lightning;
      if (!L.thunder.enabled) L.thunder.enable(); else L.thunder.disable();
      refreshLightningReadout();
      renderInspector();
    });
    $("#btn-top").addEventListener("click", () => {
      if (state.cam.pitch > 1.2) { state.cam.pitch = 0.3; state.cam.yaw = 0.6; } else { state.cam.pitch = 1.45; state.cam.yaw = 0; }
      sync();
    });
    $("#btn-arrows").addEventListener("click", (e) => { state.wind.showArrows = !state.wind.showArrows; e.currentTarget.setAttribute("aria-pressed", String(state.wind.showArrows)); });
    $("#btn-arrows").setAttribute("aria-pressed", String(state.wind.showArrows));
    $("#btn-floor").addEventListener("click", (e) => { state.wind.showFloor = !state.wind.showFloor; e.currentTarget.setAttribute("aria-pressed", String(state.wind.showFloor)); });
    $("#btn-floor").setAttribute("aria-pressed", String(state.wind.showFloor));
    const speed = $("#time-scale");
    speed.addEventListener("input", () => { state.timeScale = +speed.value; $("#time-scale-v").textContent = state.timeScale.toFixed(2) + "×"; });
    $("#time-scale-v").textContent = state.timeScale.toFixed(2) + "×";
    window.addEventListener("keydown", (e) => {
      if (e.target.matches("input, select, textarea")) return;
      if (e.code === "Space") { e.preventDefault(); playBtn.click(); }
      if (e.key === "l" || e.key === "L") strikeNow();
    });
    sync();
  }

  function setupViewport() {
    const canvas = $("#view");
    let drag = null;
    canvas.addEventListener("contextmenu", (e) => e.preventDefault());
    canvas.addEventListener("pointerdown", (e) => {
      drag = { x: e.clientX, y: e.clientY, pan: e.button === 2 || e.shiftKey || e.button === 1 };
      canvas.setPointerCapture(e.pointerId);
    });
    canvas.addEventListener("pointermove", (e) => {
      if (!drag) return;
      const dx = e.clientX - drag.x;
      const dy = e.clientY - drag.y;
      drag.x = e.clientX;
      drag.y = e.clientY;
      const c = state.cam;
      if (drag.pan) {
        const s = c.dist * 0.0015;
        const cp = Math.cos(c.yaw), sp = Math.sin(c.yaw);
        c.target[0] += (-dx * cp) * s;
        c.target[2] += (dx * sp) * s;
        c.target[1] += dy * s;
      } else {
        c.yaw -= dx * 0.005;
        c.pitch = clamp(c.pitch + dy * 0.005, -1.45, 1.45);
      }
      $("#btn-top").setAttribute("aria-pressed", String(c.pitch > 1.2));
    });
    const up = () => { drag = null; };
    canvas.addEventListener("pointerup", up);
    canvas.addEventListener("pointercancel", up);
    canvas.addEventListener("wheel", (e) => {
      e.preventDefault();
      state.cam.dist = clamp(state.cam.dist * Math.exp(e.deltaY * 0.001), 2, 60);
    }, { passive: false });
  }

  function resizeCanvas() {
    const canvas = $("#view");
    const r = canvas.parentElement.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.max(64, Math.floor(r.width * dpr));
    const h = Math.max(64, Math.floor(r.height * dpr));
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
      state.engine?.resize(w, h);
    }
  }

  // ----------------------------------------------------------------- boot
  async function boot() {
    const canvas = $("#view");
    const banner = $("#boot-error");
    try {
      resizeCanvas();
      state.engine = new PE.Engine(canvas);
      const info = await state.engine.init();
      state.lightning = new PE.Lightning();
      $("#st-adapter").textContent = info.adapter;
      state.engine.onLost = (info2) => {
        state.lostAtFrame = state.frame;
        banner.hidden = false;
        banner.textContent = "WebGPU device lost: " + (info2.message || info2.reason) + ". Reload the page.";
      };
      // ?scene=none|id,id,... overrides the default scene (handy for testing one system).
      const q = new URLSearchParams(location.search).get("scene");
      const ids = q === null ? PE.defaultScene : q === "none" || q === "" ? [] : q.split(",");
      for (const id of ids) if (PE.presetById(id)) state.systems.push(makeSystem(id));
      setupToolbar();
      setupViewport();
      window.addEventListener("resize", resizeCanvas);
      new ResizeObserver(resizeCanvas).observe(canvas.parentElement);
      select({ type: "wind" });

      // 📝 When this page is framed by a host editor -- the gas emitter card's Open ParticleEditor -- the
      //    handoff gives it one message in and one message out. Standalone, Install() finds no parent and
      //    does nothing at all.
      PE.Handoff.Install(state, {
        Open: (presetId, name) => {
          const Existing = state.systems.find((s) => s.presetId === presetId);
          if (Existing) { select({ type: "system", id: Existing.id }); return Existing; }
          if (!PE.presetById(presetId)) return null;
          const Fresh = addSystem(presetId);
          if (name) Fresh.name = name;
          return Fresh;
        },
        Rebuild: rebuildGpu,
        Refresh: renderInspector,
      });

      requestAnimationFrame(frame);
    } catch (err) {
      console.error(err);
      banner.hidden = false;
      banner.textContent = "Particle Editor could not start: " + (err && err.message ? err.message : err);
    }
  }

  // 🔴 A module script is deferred by definition: it runs after the document has been parsed, so
  //    DOMContentLoaded has usually ALREADY FIRED by the time this line is reached and a listener would
  //    never be called. Boot now if the parse is done, and wait only if it somehow is not.
  if (document.readyState === "loading") window.addEventListener("DOMContentLoaded", boot);
  else boot();
