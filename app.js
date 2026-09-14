/* STRATA / procedural terrain lab
   The preview is intentionally small and deterministic: every cell stores
   surface elevation, water depth, sediment and an XY flow vector. */
(() => {
  'use strict';

  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => Array.from(root.querySelectorAll(selector));
  const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
  const lerp = (a, b, t) => a + (b - a) * t;
  const mix = (a, b, t) => a.map((v, index) => Math.round(lerp(v, b[index], t)));
  const indexOf = (x, y, size) => y * size + x;

  const state = {
    size: 72,
    seed: 23.41,
    sourceName: 'Canyon seed',
    base: null,
    terrain: null,
    water: null,
    sediment: null,
    flowX: null,
    flowY: null,
    hardness: null,
    renderMode: 'surface',
    showWater: true,
    showContours: true,
    showVectors: false,
    graphZoom: 1,
    selectedNode: 'hydraulic',
    running: false,
    step: 72,
    params: { rain: 18, erosion: 42, talus: 34 },
    defaultPositions: {
      source: [4, 12], rain: [4, 59], ridge: [31, 7], hydraulic: [33, 39],
      thermal: [34, 75], layer: [65, 29], output: [70, 64]
    }
  };

  const canvas = $('#terrainCanvas');
  const ctx = canvas.getContext('2d');
  const renderStage = $('#renderStage');
  let canvasWidth = 0;
  let canvasHeight = 0;
  let devicePixelRatio = 1;
  let simulationFrame = null;
  let saveTimer = null;

  // ---------------------------------------------------------------------------
  // Deterministic terrain source
  // ---------------------------------------------------------------------------
  function hash2(x, y) {
    const n = Math.sin((x * 127.1 + y * 311.7 + state.seed * 91.13) * 0.0174533) * 43758.5453;
    return n - Math.floor(n);
  }

  function smooth(t) { return t * t * (3 - 2 * t); }

  function valueNoise(x, y, scale = 1) {
    const px = x / scale;
    const py = y / scale;
    const x0 = Math.floor(px);
    const y0 = Math.floor(py);
    const tx = smooth(px - x0);
    const ty = smooth(py - y0);
    const a = hash2(x0, y0);
    const b = hash2(x0 + 1, y0);
    const c = hash2(x0, y0 + 1);
    const d = hash2(x0 + 1, y0 + 1);
    return lerp(lerp(a, b, tx), lerp(c, d, tx), ty);
  }

  function fbm(x, y, octaves = 4) {
    let total = 0;
    let amplitude = 0.5;
    let frequency = 1;
    let normalization = 0;
    for (let i = 0; i < octaves; i += 1) {
      total += valueNoise(x * frequency, y * frequency, 1) * amplitude;
      normalization += amplitude;
      amplitude *= 0.52;
      frequency *= 2.02;
    }
    return total / normalization;
  }

  function createTerrainSource() {
    const size = state.size;
    const output = new Float32Array(size * size);
    let minimum = Infinity;
    let maximum = -Infinity;

    for (let y = 0; y < size; y += 1) {
      for (let x = 0; x < size; x += 1) {
        const nx = x / (size - 1) - 0.5;
        const ny = y / (size - 1) - 0.5;
        const broad = fbm(x * 0.034, y * 0.034, 5);
        const folds = fbm(x * 0.09 + 12, y * 0.09 - 8, 4);
        const detail = fbm(x * 0.22 - 4, y * 0.22 + 7, 3);
        const ridged = 1 - Math.abs(fbm(x * 0.06 - 6, y * 0.06 + 2, 4) * 2 - 1);
        const mountain = clamp(1 - Math.hypot(nx + 0.12, ny + 0.05) * 1.45, 0, 1);
        const canyonCenter = ny - (0.13 * Math.sin(nx * 9.2 + 0.4) + 0.045 * Math.sin(nx * 24 - 0.8) + 0.025 * Math.cos(ny * 22));
        const canyon = Math.exp(-canyonCenter * canyonCenter * 720) * (0.42 + (nx + 0.5) * 0.55);
        const basin = Math.exp(-((nx + 0.27) ** 2 * 8 + (ny - 0.2) ** 2 * 11));
        const value = 0.2 + broad * 0.25 + folds * 0.16 + detail * 0.12 + ridged * 0.2 + mountain * 0.16 - canyon * 0.2 - basin * 0.05;
        const cell = indexOf(x, y, size);
        output[cell] = value;
        minimum = Math.min(minimum, value);
        maximum = Math.max(maximum, value);
      }
    }

    for (let i = 0; i < output.length; i += 1) {
      output[i] = 0.08 + ((output[i] - minimum) / (maximum - minimum)) * 0.83;
    }
    return output;
  }

  function createField() {
    const length = state.size * state.size;
    state.terrain = state.base.slice();
    state.water = new Float32Array(length);
    state.sediment = new Float32Array(length);
    state.flowX = new Float32Array(length);
    state.flowY = new Float32Array(length);
    state.hardness = new Float32Array(length);
    for (let i = 0; i < length; i += 1) {
      state.hardness[i] = 0.38 + valueNoise((i % size) * 0.16 + 31, Math.floor(i / size) * 0.16 - 12, 1) * 0.5;
    }

    // Give the idle preview a shallow valley waterline and a downhill vector.
    const size = state.size;
    for (let y = 1; y < size - 1; y += 1) {
      for (let x = 1; x < size - 1; x += 1) {
        const i = indexOf(x, y, size);
        const h = state.terrain[i];
        state.water[i] = Math.max(0, 0.013 - (h - 0.08) * 0.009);
        const dx = state.terrain[indexOf(x + 1, y, size)] - state.terrain[indexOf(x - 1, y, size)];
        const dy = state.terrain[indexOf(x, y + 1, size)] - state.terrain[indexOf(x, y - 1, size)];
        state.flowX[i] = -dx * 0.8;
        state.flowY[i] = -dy * 0.8;
      }
    }
    state.step = 0;
    updateMetrics();
  }

  // ---------------------------------------------------------------------------
  // Erosion solver: shallow water + virtual pipes + sediment + thermal settle
  // ---------------------------------------------------------------------------
  function stepErosion() {
    const size = state.size;
    const count = size * size;
    const nextWater = new Float32Array(count);
    const nextSediment = new Float32Array(count);
    const rainfall = 0.00072 * (state.params.rain / 18);
    const pickUp = 0.017 * (state.params.erosion / 42);
    const evaporation = 0.988;
    const flowScale = 0.44;
    const neighbourOffsets = [[1, 0], [-1, 0], [0, 1], [0, -1]];

    // 1. Rain, pressure and outflow across four virtual pipes.
    for (let y = 1; y < size - 1; y += 1) {
      for (let x = 1; x < size - 1; x += 1) {
        const i = indexOf(x, y, size);
        const localRain = rainfall * (0.72 + valueNoise(x * 0.13 + state.step, y * 0.13, 1) * 0.56);
        state.water[i] += localRain;
        const surface = state.terrain[i] + state.water[i];
        const candidates = [];
        let totalPressure = 0;

        for (const [dx, dy] of neighbourOffsets) {
          const nx = x + dx;
          const ny = y + dy;
          const ni = indexOf(nx, ny, size);
          const pressure = Math.max(0, surface - (state.terrain[ni] + state.water[ni]));
          candidates.push({ dx, dy, ni, pressure });
          totalPressure += pressure;
        }

        const available = state.water[i] * flowScale;
        let totalOut = 0;
        let vx = 0;
        let vy = 0;
        const carried = state.sediment[i];

        for (const candidate of candidates) {
          if (totalPressure <= 0 || candidate.pressure <= 0) continue;
          const amount = Math.min(available * (candidate.pressure / totalPressure), candidate.pressure * 0.2);
          totalOut += amount;
          nextWater[candidate.ni] += amount;
          nextSediment[candidate.ni] += carried * (amount / Math.max(state.water[i], 0.0001));
          vx += candidate.dx * amount;
          vy += candidate.dy * amount;
        }
        nextWater[i] += Math.max(0, state.water[i] - totalOut);
        nextSediment[i] += carried * (1 - clamp(totalOut / Math.max(state.water[i], 0.0001), 0, 1));
        state.flowX[i] = state.flowX[i] * 0.54 + vx * 3.2;
        state.flowY[i] = state.flowY[i] * 0.54 + vy * 3.2;
      }
    }

    // 2. Pick up / deposit according to water speed, slope and carrying capacity.
    for (let y = 1; y < size - 1; y += 1) {
      for (let x = 1; x < size - 1; x += 1) {
        const i = indexOf(x, y, size);
        const slopeX = (state.terrain[indexOf(x + 1, y, size)] - state.terrain[indexOf(x - 1, y, size)]) * 0.5;
        const slopeY = (state.terrain[indexOf(x, y + 1, size)] - state.terrain[indexOf(x, y - 1, size)]) * 0.5;
        const slope = Math.hypot(slopeX, slopeY);
        const velocity = Math.hypot(state.flowX[i], state.flowY[i]) / Math.max(state.water[i], 0.02);
        const capacity = 0.002 + Math.min(0.14, state.water[i] * (0.15 + slope * 4.5) + velocity * 0.005);
        const difference = capacity - nextSediment[i];

        if (difference > 0) {
          const availableSoil = Math.max(0, state.terrain[i] - 0.065);
          const materialResponse = 1.2 - state.hardness[i];
          const eroded = Math.min(availableSoil, difference * pickUp * materialResponse * (0.7 + slope * 7));
          state.terrain[i] -= eroded;
          nextSediment[i] += eroded;
        } else {
          const deposited = Math.min(nextSediment[i], -difference * 0.1);
          state.terrain[i] += deposited;
          nextSediment[i] -= deposited;
        }
      }
    }

    // 3. Thermal / talus pass. Only one direction per pair to avoid double moving.
    const settled = state.terrain.slice();
    const talus = 0.012 + (state.params.talus / 34) * 0.019;
    for (let y = 1; y < size - 1; y += 1) {
      for (let x = 1; x < size - 1; x += 1) {
        const i = indexOf(x, y, size);
        for (const [dx, dy] of [[1, 0], [0, 1]]) {
          const nx = x + dx;
          const ny = y + dy;
          const ni = indexOf(nx, ny, size);
          const difference = state.terrain[i] - state.terrain[ni];
          if (difference > talus) {
            const amount = Math.min(0.014, (difference - talus) * 0.065);
            settled[i] -= amount;
            settled[ni] += amount;
          } else if (difference < -talus) {
            const amount = Math.min(0.014, (-difference - talus) * 0.065);
            settled[i] += amount;
            settled[ni] -= amount;
          }
        }
      }
    }
    state.terrain.set(settled);
    state.sediment.set(nextSediment);
    for (let i = 0; i < count; i += 1) state.water[i] = nextWater[i] * evaporation;
    state.step += 1;
  }

  function runSimulation() {
    if (state.running) return;
    state.running = true;
    state.step = 0;
    state.terrain = state.base.slice();
    state.water = new Float32Array(state.size * state.size);
    state.sediment = new Float32Array(state.size * state.size);
    state.flowX = new Float32Array(state.size * state.size);
    state.flowY = new Float32Array(state.size * state.size);
    const totalSteps = 72;
    const button = $('#runButton');
    const runLabel = $('#runLabel');
    const loading = $('#loadingOverlay');
    button.classList.add('running');
    runLabel.textContent = 'Simulating…';
    loading.classList.add('show');
    setSaveStatus('Solving flow field…', true);

    const tick = () => {
      for (let pass = 0; pass < 2 && state.step < totalSteps; pass += 1) stepErosion();
      const percent = Math.round((state.step / totalSteps) * 100);
      $('#loadingProgress').textContent = `${String(percent).padStart(2, '0')}%`;
      $('#frameReadout').textContent = `FRAME ${String(state.step).padStart(3, '0')}`;
      renderTerrain();
      updateMetrics();
      if (state.step < totalSteps) {
        simulationFrame = requestAnimationFrame(tick);
      } else {
        state.running = false;
        simulationFrame = null;
        button.classList.remove('running');
        runLabel.textContent = 'Run simulation';
        loading.classList.remove('show');
        setSaveStatus('All changes saved');
        showToast('Simulation complete · 72 steps');
      }
    };
    tick();
  }

  // ---------------------------------------------------------------------------
  // Canvas renderer
  // ---------------------------------------------------------------------------
  const elevationPalette = [
    [0.00, [25, 51, 46]], [0.20, [38, 74, 60]], [0.39, [79, 91, 66]],
    [0.58, [137, 119, 78]], [0.74, [190, 145, 93]], [0.88, [218, 166, 108]], [1.0, [159, 78, 64]]
  ];

  function paletteColor(value) {
    const t = clamp(value, 0, 1);
    for (let i = 1; i < elevationPalette.length; i += 1) {
      if (t <= elevationPalette[i][0]) {
        const [p0, c0] = elevationPalette[i - 1];
        const [p1, c1] = elevationPalette[i];
        return mix(c0, c1, (t - p0) / (p1 - p0));
      }
    }
    return elevationPalette[elevationPalette.length - 1][1];
  }

  function projectionMetrics() {
    const cellX = (canvasWidth * 0.44) / state.size;
    const cellY = (canvasHeight * 0.34) / state.size;
    return { cellX, cellY, originX: canvasWidth * 0.51, originY: canvasHeight * 0.14, zScale: canvasHeight * 0.32 };
  }

  function project(x, y, z, metrics) {
    return {
      x: metrics.originX + (x - y) * metrics.cellX,
      y: metrics.originY + (x + y) * metrics.cellY - z * metrics.zScale
    };
  }

  function drawBackground() {
    const background = ctx.createRadialGradient(canvasWidth * .51, canvasHeight * .27, 12, canvasWidth * .5, canvasHeight * .47, canvasWidth * .75);
    background.addColorStop(0, '#1d302d');
    background.addColorStop(.46, '#122321');
    background.addColorStop(1, '#0a1011');
    ctx.fillStyle = background;
    ctx.fillRect(0, 0, canvasWidth, canvasHeight);
    ctx.save();
    ctx.globalAlpha = .14;
    ctx.strokeStyle = '#8ab3a0';
    ctx.lineWidth = 1;
    for (let x = -canvasHeight; x < canvasWidth + canvasHeight; x += 38) {
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x + canvasHeight * .65, canvasHeight); ctx.stroke();
    }
    ctx.restore();
  }

  function surfaceAt(x, y) {
    const size = state.size;
    const ix = clamp(Math.round(x), 0, size - 1);
    const iy = clamp(Math.round(y), 0, size - 1);
    const i = indexOf(ix, iy, size);
    return state.terrain[i] + (state.showWater ? state.water[i] * 0.42 : 0);
  }

  function faceColor(x, y, h, slopeX, slopeY) {
    const shade = clamp(0.87 + slopeX * -2.2 + slopeY * 1.3, 0.55, 1.13);
    const i = indexOf(x, y, state.size);
    let color;
    if (state.renderMode === 'depth') {
      const depth = clamp(state.water[i] * 12, 0, 1);
      const base = paletteColor(h);
      color = mix(base, [47, 154, 157], depth * .84);
    } else if (state.renderMode === 'flow') {
      const magnitude = clamp(Math.hypot(state.flowX[i], state.flowY[i]) * 3.5, 0, 1);
      const base = paletteColor(h);
      color = mix(base, [74, 151, 151], magnitude * .34);
    } else {
      color = paletteColor(h);
      if (state.showWater) {
        const waterTint = clamp(state.water[i] * 10, 0, 1);
        color = mix(color, [55, 142, 138], waterTint * .5);
      }
    }
    return `rgb(${Math.round(color[0] * shade)},${Math.round(color[1] * shade)},${Math.round(color[2] * shade)})`;
  }

  function drawTerrain() {
    const size = state.size;
    const metrics = projectionMetrics();
    const maxWater = Math.max(.001, ...state.water);

    // A quiet cast shadow grounds the oblique surface.
    ctx.save();
    ctx.fillStyle = 'rgba(0,0,0,.43)';
    ctx.filter = 'blur(15px)';
    ctx.beginPath();
    ctx.ellipse(metrics.originX, metrics.originY + size * metrics.cellY * 1.03, canvasWidth * .32, canvasHeight * .055, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();

    // Painter's order: back edge first, then front edge.
    for (let sum = 0; sum < (size - 1) * 2; sum += 1) {
      const xStart = Math.max(0, sum - (size - 2));
      const xEnd = Math.min(size - 2, sum);
      for (let x = xStart; x <= xEnd; x += 1) {
        const y = sum - x;
        const i = indexOf(x, y, size);
        const p00 = project(x, y, surfaceAt(x, y), metrics);
        const p10 = project(x + 1, y, surfaceAt(x + 1, y), metrics);
        const p01 = project(x, y + 1, surfaceAt(x, y + 1), metrics);
        const p11 = project(x + 1, y + 1, surfaceAt(x + 1, y + 1), metrics);
        const average = (state.terrain[i] + state.terrain[indexOf(x + 1, y, size)] + state.terrain[indexOf(x, y + 1, size)] + state.terrain[indexOf(x + 1, y + 1, size)]) * .25;
        const slopeX = state.terrain[indexOf(x + 1, y, size)] - state.terrain[i];
        const slopeY = state.terrain[indexOf(x, y + 1, size)] - state.terrain[i];

        ctx.fillStyle = faceColor(x, y, average, slopeX, slopeY);
        ctx.beginPath(); ctx.moveTo(p00.x, p00.y); ctx.lineTo(p10.x, p10.y); ctx.lineTo(p11.x, p11.y); ctx.closePath(); ctx.fill();
        ctx.beginPath(); ctx.moveTo(p00.x, p00.y); ctx.lineTo(p11.x, p11.y); ctx.lineTo(p01.x, p01.y); ctx.closePath(); ctx.fill();

        if (state.showWater && state.water[i] > maxWater * .32) {
          const waterAlpha = clamp(state.water[i] / maxWater * .25, .035, .2);
          ctx.fillStyle = `rgba(125, 211, 199, ${waterAlpha})`;
          ctx.beginPath(); ctx.moveTo(p00.x, p00.y - 1); ctx.lineTo(p10.x, p10.y - 1); ctx.lineTo(p11.x, p11.y - 1); ctx.lineTo(p01.x, p01.y - 1); ctx.closePath(); ctx.fill();
        }
      }
    }

    // Fine relief lines make the rendered view read as a terrain surface rather than a flat color mesh.
    if (state.showContours || state.renderMode === 'flow') drawContours(metrics);
    if (state.showVectors || state.renderMode === 'flow') drawVectors(metrics);
  }

  function drawContours(metrics) {
    const size = state.size;
    ctx.save();
    ctx.strokeStyle = state.renderMode === 'depth' ? 'rgba(166,231,216,.19)' : 'rgba(239,205,151,.20)';
    ctx.lineWidth = .65;
    ctx.beginPath();
    for (let y = 0; y < size - 1; y += 1) {
      for (let x = 0; x < size - 1; x += 1) {
        const i = indexOf(x, y, size);
        const h = state.terrain[i];
        const right = state.terrain[indexOf(x + 1, y, size)];
        const down = state.terrain[indexOf(x, y + 1, size)];
        const level = Math.floor(h * 15);
        if (Math.floor(right * 15) !== level) {
          const p = project(x + .5, y, surfaceAt(x, y), metrics);
          const q = project(x + .5, y + .42, surfaceAt(x, y + 1), metrics);
          ctx.moveTo(p.x, p.y); ctx.lineTo(q.x, q.y);
        }
        if (Math.floor(down * 15) !== level) {
          const p = project(x, y + .5, surfaceAt(x, y), metrics);
          const q = project(x + .42, y + .5, surfaceAt(x + 1, y), metrics);
          ctx.moveTo(p.x, p.y); ctx.lineTo(q.x, q.y);
        }
      }
    }
    ctx.stroke();
    ctx.restore();
  }

  function drawVectors(metrics) {
    const size = state.size;
    ctx.save();
    ctx.strokeStyle = state.renderMode === 'flow' ? 'rgba(139,224,210,.72)' : 'rgba(229,139,97,.72)';
    ctx.fillStyle = ctx.strokeStyle;
    ctx.lineWidth = .8;
    for (let y = 5; y < size - 3; y += 7) {
      for (let x = 5; x < size - 3; x += 7) {
        const i = indexOf(x, y, size);
        const magnitude = Math.hypot(state.flowX[i], state.flowY[i]);
        if (magnitude < .002) continue;
        const p = project(x, y, surfaceAt(x, y) + .012, metrics);
        const scale = 8 + clamp(magnitude * 25, 0, 8);
        const q = project(x + state.flowX[i] * scale, y + state.flowY[i] * scale, surfaceAt(x, y) + .012, metrics);
        ctx.beginPath(); ctx.moveTo(p.x, p.y); ctx.lineTo(q.x, q.y); ctx.stroke();
        const angle = Math.atan2(q.y - p.y, q.x - p.x);
        ctx.beginPath(); ctx.moveTo(q.x, q.y); ctx.lineTo(q.x - Math.cos(angle - .55) * 3, q.y - Math.sin(angle - .55) * 3); ctx.lineTo(q.x - Math.cos(angle + .55) * 3, q.y - Math.sin(angle + .55) * 3); ctx.closePath(); ctx.fill();
      }
    }
    ctx.restore();
  }

  function renderTerrain() {
    if (!canvasWidth || !canvasHeight || !state.terrain) return;
    ctx.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0);
    ctx.clearRect(0, 0, canvasWidth, canvasHeight);
    drawBackground();
    drawTerrain();
  }

  function resizeCanvas() {
    const rect = canvas.getBoundingClientRect();
    canvasWidth = Math.max(1, rect.width);
    canvasHeight = Math.max(1, rect.height);
    devicePixelRatio = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width = Math.round(canvasWidth * devicePixelRatio);
    canvas.height = Math.round(canvasHeight * devicePixelRatio);
    canvas.style.width = `${canvasWidth}px`;
    canvas.style.height = `${canvasHeight}px`;
    renderTerrain();
  }

  function updateMetrics() {
    if (!state.terrain) return;
    let removed = 0;
    let sediment = 0;
    let flow = 0;
    for (let i = 0; i < state.terrain.length; i += 1) {
      removed += Math.max(0, state.base[i] - state.terrain[i]);
      sediment += state.sediment[i];
      flow += Math.hypot(state.flowX[i], state.flowY[i]);
    }
    const eroded = clamp((removed / state.terrain.length) * 850, 0, 99.9);
    $('#erodedMetric').textContent = `${eroded.toFixed(1)}%`;
    $('#sedimentMetric').textContent = `${(sediment / state.terrain.length * 3.4).toFixed(2)} m³`;
    $('#flowMetric').textContent = `${(2.2 + flow / state.terrain.length * 25).toFixed(1)} km`;
  }

  // ---------------------------------------------------------------------------
  // Node graph
  // ---------------------------------------------------------------------------
  const nodeDetails = {
    source: ['SELECTED NODE / SOURCE', 'Canyon seed', 'Heightfield input'],
    rain: ['SELECTED NODE / WATER', 'Rain source', 'XY emitter'],
    ridge: ['SELECTED NODE / SHAPE', 'Ridge fold', 'Macro relief'],
    hydraulic: ['SELECTED NODE / EROSION', 'Hydraulic pass', 'Depth-aware'],
    thermal: ['SELECTED NODE / WEATHER', 'Talus settle', 'Gravity pass'],
    layer: ['SELECTED NODE / MATERIAL', 'Layer merge', '3 channels'],
    output: ['SELECTED NODE / OUTPUT', 'Terrain render', 'Surface + depth']
  };

  function selectNode(name) {
    state.selectedNode = name;
    $$('.node').forEach(node => node.classList.toggle('selected', node.dataset.node === name));
    const detail = nodeDetails[name] || nodeDetails.hydraulic;
    $('#inspectorType').textContent = detail[0];
    $('#inspectorTitle').textContent = detail[1];
    $('#inspectorStatus').textContent = detail[2];
    $('#inspectorStatus').previousElementSibling.classList.toggle('aqua', name !== 'source');
  }

  const graphEdges = [
    ['source', 'ridge', 'out', 'in', false],
    ['ridge', 'hydraulic', 'out', 'in', false],
    ['rain', 'hydraulic', 'out', 'in', true],
    ['hydraulic', 'thermal', 'aux', 'in', true],
    ['hydraulic', 'layer', 'out', 'in', false],
    ['thermal', 'layer', 'out', 'aux', true],
    ['layer', 'output', 'out', 'in', false]
  ];

  function drawConnections() {
    const graph = $('#graphCanvas');
    const svg = $('#connectionLayer');
    const graphRect = graph.getBoundingClientRect();
    const zoom = state.graphZoom;
    if (!graphRect.width || !graphRect.height) return;
    svg.setAttribute('viewBox', `0 0 ${graphRect.width} ${graphRect.height}`);
    const paths = [];

    for (const [fromName, toName, fromPort, toPort, dashed] of graphEdges) {
      const from = $(`.node[data-node="${fromName}"] .port[data-port="${fromPort}"]`);
      const to = $(`.node[data-node="${toName}"] .port[data-port="${toPort}"]`);
      if (!from || !to) continue;
      const a = from.getBoundingClientRect();
      const b = to.getBoundingClientRect();
      const x1 = (a.left + a.width / 2 - graphRect.left) / zoom;
      const y1 = (a.top + a.height / 2 - graphRect.top) / zoom;
      const x2 = (b.left + b.width / 2 - graphRect.left) / zoom;
      const y2 = (b.top + b.height / 2 - graphRect.top) / zoom;
      const bend = Math.max(20, Math.abs(x2 - x1) * .42);
      const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
      path.setAttribute('class', `main-link${dashed ? ' dashed' : ''}`);
      path.setAttribute('d', `M ${x1} ${y1} C ${x1 + bend} ${y1}, ${x2 - bend} ${y2}, ${x2} ${y2}`);
      paths.push(path);
      const circle = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
      circle.setAttribute('cx', x2); circle.setAttribute('cy', y2); circle.setAttribute('r', '2.2');
      if (dashed) circle.setAttribute('class', 'secondary');
      paths.push(circle);
    }
    svg.replaceChildren(...paths);
  }

  function applyGraphZoom() {
    $('#graphLayer').style.transform = `scale(${state.graphZoom})`;
    $('#zoomReadout').textContent = `${Math.round(state.graphZoom * 100)}%`;
    requestAnimationFrame(drawConnections);
  }

  function resetGraph() {
    state.graphZoom = 1;
    Object.entries(state.defaultPositions).forEach(([name, position]) => {
      const node = $(`.node[data-node="${name}"]`);
      if (node) { node.style.left = `${position[0]}%`; node.style.top = `${position[1]}%`; }
    });
    applyGraphZoom();
    showToast('Graph layout reset');
  }

  function makeNodeDraggable(node) {
    node.addEventListener('click', event => {
      if (!event.target.closest('.node-menu')) selectNode(node.dataset.node);
    });
    node.addEventListener('pointerdown', event => {
      if (event.target.closest('button, .port')) return;
      const graph = $('#graphCanvas');
      const rect = graph.getBoundingClientRect();
      const zoom = state.graphZoom;
      const nodeRect = node.getBoundingClientRect();
      const startX = (event.clientX - rect.left) / zoom;
      const startY = (event.clientY - rect.top) / zoom;
      const offsetX = startX - (nodeRect.left - rect.left) / zoom;
      const offsetY = startY - (nodeRect.top - rect.top) / zoom;
      node.classList.add('dragging');
      node.setPointerCapture(event.pointerId);
      const move = moveEvent => {
        const nextX = clamp((moveEvent.clientX - rect.left) / zoom - offsetX, 3, rect.width / zoom - node.offsetWidth - 3);
        const nextY = clamp((moveEvent.clientY - rect.top) / zoom - offsetY, 3, rect.height / zoom - node.offsetHeight - 3);
        node.style.left = `${(nextX / (rect.width / zoom)) * 100}%`;
        node.style.top = `${(nextY / (rect.height / zoom)) * 100}%`;
        drawConnections();
      };
      const up = upEvent => {
        node.classList.remove('dragging');
        node.releasePointerCapture(upEvent.pointerId);
        node.removeEventListener('pointermove', move);
        node.removeEventListener('pointerup', up);
        setSaveStatus('Layout changed');
      };
      node.addEventListener('pointermove', move);
      node.addEventListener('pointerup', up);
    });
  }

  function addProbeNode() {
    if ($('.node[data-node="probe"]')) {
      selectNode('probe');
      return;
    }
    const probe = document.createElement('article');
    probe.className = 'node output-node';
    probe.dataset.node = 'probe';
    probe.style.left = '57%'; probe.style.top = '7%';
    probe.innerHTML = '<div class="node-topline"><span class="node-type output-type">DEBUG</span><button class="node-menu" aria-label="Probe node menu">•••</button></div><div class="node-title">Depth probe</div><div class="node-meta">WATER / SEDIMENT / XY</div><div class="layer-preview node-preview"><span></span><span></span><span></span></div><div class="port port-left" data-port="in"></div>';
    $('#graphLayer').appendChild(probe);
    nodeDetails.probe = ['SELECTED NODE / DEBUG', 'Depth probe', '3 channels'];
    makeNodeDraggable(probe);
    $('#nodeCount').textContent = '08';
    selectNode('probe');
    setSaveStatus('Added depth probe');
    requestAnimationFrame(drawConnections);
  }

  // ---------------------------------------------------------------------------
  // UI helpers and interactions
  // ---------------------------------------------------------------------------
  function setSaveStatus(text, busy = false) {
    $('#saveStatus').textContent = text;
    const dot = $('.status-chip .status-dot');
    dot.style.background = busy ? 'var(--aqua)' : 'var(--orange)';
    clearTimeout(saveTimer);
    if (busy) return;
    saveTimer = setTimeout(() => { $('#saveStatus').textContent = 'All changes saved'; }, 900);
  }

  function showToast(message) {
    $('#toastMessage').textContent = message;
    const toast = $('#toast');
    toast.classList.add('show');
    clearTimeout(toast._hideTimer);
    toast._hideTimer = setTimeout(() => toast.classList.remove('show'), 2400);
  }

  function updateControlLabels() {
    $('#rainValue').textContent = `${state.params.rain} mm`;
    $('#erosionValue').textContent = `${state.params.erosion}%`;
    $('#talusValue').textContent = `${state.params.talus}°`;
  }

  function markParameterChange() {
    updateControlLabels();
    setSaveStatus('Unsaved parameter change');
    if (!state.running) renderTerrain();
  }

  function setRenderMode(mode) {
    state.renderMode = mode;
    $$('.view-tabs button').forEach(button => button.classList.toggle('active', button.dataset.render === mode));
    renderTerrain();
  }

  function toggleControl(id, key) {
    const button = $(`#${id}`);
    state[key] = !state[key];
    button.classList.toggle('active', state[key]);
    renderTerrain();
  }

  function importMap(file) {
    if (!file) return;
    const image = new Image();
    image.onload = () => {
      const size = state.size;
      const buffer = document.createElement('canvas');
      buffer.width = size; buffer.height = size;
      const bufferContext = buffer.getContext('2d', { willReadFrequently: true });
      bufferContext.drawImage(image, 0, 0, size, size);
      const pixels = bufferContext.getImageData(0, 0, size, size).data;
      let min = 255; let max = 0;
      const luminance = new Float32Array(size * size);
      for (let i = 0; i < luminance.length; i += 1) {
        const p = i * 4;
        const value = pixels[p] * .2126 + pixels[p + 1] * .7152 + pixels[p + 2] * .0722;
        luminance[i] = value; min = Math.min(min, value); max = Math.max(max, value);
      }
      state.base = luminance.map(value => 0.08 + ((value - min) / Math.max(1, max - min)) * .83);
      state.sourceName = file.name.replace(/\.[^.]+$/, '').slice(0, 23) || 'Imported map';
      $('#projectName').textContent = state.sourceName;
      createField();
      renderTerrain();
      updateMetrics();
      setSaveStatus('Imported heightfield');
      showToast(`Imported ${state.sourceName}`);
      URL.revokeObjectURL(image.src);
    };
    image.src = URL.createObjectURL(file);
  }

  function regenerateSeed() {
    state.seed = Math.random() * 1000;
    state.sourceName = 'Canyon seed';
    state.base = createTerrainSource();
    $('#projectName').textContent = 'Canyon study 04';
    createField();
    renderTerrain();
    updateMetrics();
    setSaveStatus('New seed generated');
    showToast('New canyon seed generated');
  }

  function exportPreview() {
    canvas.toBlob(blob => {
      if (!blob) return;
      const link = document.createElement('a');
      link.href = URL.createObjectURL(blob);
      link.download = 'strata-canyon-preview.png';
      link.click();
      setTimeout(() => URL.revokeObjectURL(link.href), 1000);
      showToast('Preview exported as PNG');
    }, 'image/png');
  }

  function wireUi() {
    $$('.node').forEach(makeNodeDraggable);
    selectNode(state.selectedNode);

    $('#runButton').addEventListener('click', runSimulation);
    $('#screenshotButton').addEventListener('click', exportPreview);
    $('#importButton').addEventListener('click', () => $('#mapInput').click());
    $('#mapInput').addEventListener('change', event => importMap(event.target.files[0]));
    $('#methodButton').addEventListener('click', () => $('#methodDialog').showModal());
    $('#helpButton').addEventListener('click', () => $('#methodDialog').showModal());
    $('#closeMethod').addEventListener('click', () => $('#methodDialog').close());
    $('#closeMethodBottom').addEventListener('click', () => $('#methodDialog').close());
    $('#methodDialog').addEventListener('click', event => { if (event.target === $('#methodDialog')) $('#methodDialog').close(); });
    $('#renameProject').addEventListener('click', () => {
      const name = window.prompt('Name this terrain study', $('#projectName').textContent);
      if (name && name.trim()) { $('#projectName').textContent = name.trim().slice(0, 29); setSaveStatus('Project renamed'); }
    });

    $('#rainAmount').addEventListener('input', event => { state.params.rain = Number(event.target.value); markParameterChange(); });
    $('#erosionAmount').addEventListener('input', event => { state.params.erosion = Number(event.target.value); markParameterChange(); });
    $('#talusAmount').addEventListener('input', event => { state.params.talus = Number(event.target.value); markParameterChange(); });
    $$('.view-tabs button').forEach(button => button.addEventListener('click', () => setRenderMode(button.dataset.render)));
    $('#waterToggle').addEventListener('click', () => toggleControl('waterToggle', 'showWater'));
    $('#contourToggle').addEventListener('click', () => toggleControl('contourToggle', 'showContours'));
    $('#vectorToggle').addEventListener('click', () => toggleControl('vectorToggle', 'showVectors'));
    $('#zoomIn').addEventListener('click', () => { state.graphZoom = clamp(state.graphZoom + .1, .75, 1.35); applyGraphZoom(); });
    $('#zoomOut').addEventListener('click', () => { state.graphZoom = clamp(state.graphZoom - .1, .75, 1.35); applyGraphZoom(); });
    $('#resetGraph').addEventListener('click', resetGraph);
    $('#fitGraphButton').addEventListener('click', resetGraph);
    $('#addNodeButton').addEventListener('click', addProbeNode);

    // The source node's menu is a quick procedural re-seed affordance.
    $$('.node-menu').forEach(button => button.addEventListener('click', event => {
      event.stopPropagation();
      const nodeName = button.closest('.node').dataset.node;
      if (nodeName === 'source') regenerateSeed(); else selectNode(nodeName);
    }));

    renderStage.addEventListener('mousemove', event => {
      const rect = renderStage.getBoundingClientRect();
      const metrics = projectionMetrics();
      const relX = event.clientX - rect.left - metrics.originX;
      const relY = event.clientY - rect.top - metrics.originY;
      const cellX = clamp(Math.round((relX / metrics.cellX + relY / metrics.cellY) * .5), 0, state.size - 1);
      const cellY = clamp(Math.round((relY / metrics.cellY - relX / metrics.cellX) * .5), 0, state.size - 1);
      const i = indexOf(cellX, cellY, state.size);
      const tooltip = $('#terrainTooltip');
      tooltip.style.left = `${event.clientX - rect.left}px`;
      tooltip.style.top = `${event.clientY - rect.top}px`;
      tooltip.classList.add('show');
      $('#tooltipCoords').textContent = `X ${String(cellX).padStart(2, '0')} · Y ${String(cellY).padStart(2, '0')}`;
      $('#tooltipData').textContent = `DEPTH ${(state.water[i] || 0).toFixed(2)} / FLOW ${Math.hypot(state.flowX[i] || 0, state.flowY[i] || 0).toFixed(2)}`;
      $('#cursorReadout').textContent = `X ${cellX.toFixed(1)} / Y ${cellY.toFixed(1)}`;
      $('#cursorHeight').textContent = state.terrain[i].toFixed(2);
    });
    renderStage.addEventListener('mouseleave', () => $('#terrainTooltip').classList.remove('show'));

    window.addEventListener('resize', () => { resizeCanvas(); drawConnections(); });
  }

  function init() {
    state.base = createTerrainSource();
    createField();
    updateControlLabels();
    wireUi();
    resizeCanvas();
    requestAnimationFrame(drawConnections);
    // Let the initial state read as a composed pass rather than a blank heightfield.
    setTimeout(() => runSimulation(), 380);
  }

  init();
})();
