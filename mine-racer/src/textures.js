import * as THREE from 'three';

// ---------------------------------------------------------------------------
// All textures are generated procedurally on canvases — zero asset files.
// ---------------------------------------------------------------------------
function canvasTex(size, draw, { repeat = 1, srgb = true } = {}) {
  const c = document.createElement('canvas');
  c.width = c.height = size;
  draw(c.getContext('2d'), size);
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.repeat.set(repeat, repeat);
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 4;
  return t;
}

function speckle(g, s, n, colors, aMin, aMax, szMin, szMax, rng = Math.random) {
  for (let i = 0; i < n; i++) {
    const c = colors[(rng() * colors.length) | 0];
    g.globalAlpha = aMin + rng() * (aMax - aMin);
    g.fillStyle = c;
    const w = szMin + rng() * (szMax - szMin);
    g.fillRect(rng() * s, rng() * s, w, w * (0.6 + rng() * 0.8));
  }
  g.globalAlpha = 1;
}

export function makeTextures() {
  const T = {};

  // ---- rough shot-rock tunnel wall -------------------------------------
  T.rock = canvasTex(512, (g, s) => {
    g.fillStyle = '#4b3d31'; g.fillRect(0, 0, s, s);
    speckle(g, s, 2600, ['#5d4c3c', '#3a2f26', '#6a5847', '#2f2620', '#54453a'], 0.1, 0.4, 2, 10);
    // angular shattered-rock facets
    for (let i = 0; i < 60; i++) {
      const x = Math.random() * s, y = Math.random() * s, r = 14 + Math.random() * 46;
      g.globalAlpha = 0.10 + Math.random() * 0.16;
      g.fillStyle = Math.random() < 0.5 ? '#2c231c' : '#65523f';
      g.beginPath();
      g.moveTo(x, y);
      for (let k = 0; k < 5; k++) {
        const a = (k / 5) * Math.PI * 2 + Math.random();
        g.lineTo(x + Math.cos(a) * r * (0.5 + Math.random()), y + Math.sin(a) * r * (0.5 + Math.random()));
      }
      g.closePath(); g.fill();
    }
    g.globalAlpha = 1;
    // cracks
    g.strokeStyle = '#241c15'; g.lineWidth = 1.6; g.globalAlpha = 0.5;
    for (let i = 0; i < 26; i++) {
      let x = Math.random() * s, y = Math.random() * s;
      g.beginPath(); g.moveTo(x, y);
      for (let k = 0; k < 6; k++) { x += (Math.random() - 0.5) * 60; y += (Math.random() - 0.5) * 60; g.lineTo(x, y); }
      g.stroke();
    }
    g.globalAlpha = 1;
  });

  // ---- packed dirt floor ------------------------------------------------
  T.dirt = canvasTex(512, (g, s) => {
    g.fillStyle = '#3a2c1d'; g.fillRect(0, 0, s, s);
    speckle(g, s, 2400, ['#4a3a26', '#2c2115', '#54432c', '#241b11'], 0.12, 0.4, 2, 9);
    // small stones
    for (let i = 0; i < 240; i++) {
      g.globalAlpha = 0.25 + Math.random() * 0.4;
      g.fillStyle = ['#5e5142', '#6d5f4c', '#453a2c'][(Math.random() * 3) | 0];
      const r = 1 + Math.random() * 3;
      g.beginPath(); g.arc(Math.random() * s, Math.random() * s, r, 0, 7); g.fill();
    }
    g.globalAlpha = 1;
  });

  // ---- haulage road: dirt with twin wheel-rut grooves --------------------
  T.road = canvasTex(512, (g, s) => {
    g.fillStyle = '#3d2f1f'; g.fillRect(0, 0, s, s);
    speckle(g, s, 1800, ['#4a3a26', '#2c2115', '#54432c'], 0.1, 0.35, 2, 9);
    // two worn wheel grooves (drawn along X; geometry is rotated for N-S roads)
    for (const yc of [s * 0.32, s * 0.68]) {
      const grad = g.createLinearGradient(0, yc - 34, 0, yc + 34);
      grad.addColorStop(0, 'rgba(0,0,0,0)');
      grad.addColorStop(0.5, 'rgba(16,10,6,0.62)');
      grad.addColorStop(1, 'rgba(0,0,0,0)');
      g.fillStyle = grad;
      g.fillRect(0, yc - 34, s, 68);
      g.globalAlpha = 0.5; g.fillStyle = '#1c130c';
      g.fillRect(0, yc - 5, s, 10); g.globalAlpha = 1;
    }
    // scattered gravel pressed into the ruts
    speckle(g, s, 320, ['#5e5142', '#241a10'], 0.2, 0.5, 1, 4);
  });

  // ---- timber planks -----------------------------------------------------
  T.wood = canvasTex(512, (g, s) => {
    g.fillStyle = '#6b4e2e'; g.fillRect(0, 0, s, s);
    const planks = 5, pw = s / planks;
    for (let p = 0; p < planks; p++) {
      const shade = 0.82 + Math.random() * 0.36;
      g.fillStyle = `rgb(${(107 * shade) | 0},${(78 * shade) | 0},${(46 * shade) | 0})`;
      g.fillRect(p * pw, 0, pw - 2, s);
      // grain
      g.globalAlpha = 0.16; g.strokeStyle = '#2e1f10'; g.lineWidth = 1.2;
      for (let i = 0; i < 26; i++) {
        const x = p * pw + Math.random() * (pw - 4);
        g.beginPath(); g.moveTo(x, 0);
        g.bezierCurveTo(x + 6, s * 0.3, x - 6, s * 0.6, x + 3, s);
        g.stroke();
      }
      g.globalAlpha = 1;
      // knots
      if (Math.random() < 0.8) {
        const kx = p * pw + pw * (0.3 + Math.random() * 0.4), ky = Math.random() * s;
        g.fillStyle = '#33220f';
        g.beginPath(); g.ellipse(kx, ky, 5 + Math.random() * 4, 8 + Math.random() * 6, 0, 0, 7); g.fill();
      }
      g.fillStyle = '#241708';
      g.fillRect(p * pw + pw - 2, 0, 2, s);
    }
  });

  // ---- dark metal (rails, cart, hardware) --------------------------------
  T.metal = canvasTex(256, (g, s) => {
    g.fillStyle = '#53555c'; g.fillRect(0, 0, s, s);
    speckle(g, s, 700, ['#5f626b', '#45474e', '#6b4a30', '#3a3c42'], 0.12, 0.4, 2, 8);
    g.globalAlpha = 0.25; g.strokeStyle = '#2c2e33';
    for (let i = 0; i < 40; i++) {
      g.beginPath(); const y = Math.random() * s;
      g.moveTo(0, y); g.lineTo(s, y + (Math.random() - 0.5) * 10); g.stroke();
    }
    g.globalAlpha = 1;
  });

  // ---- checker flag --------------------------------------------------------
  T.checker = canvasTex(128, (g, s) => {
    const n = 6, q = s / n;
    for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) {
      g.fillStyle = (i + j) % 2 ? '#151310' : '#e8e2d4';
      g.fillRect(i * q, j * q, q, q);
    }
  });

  // ---- painted wooden mine sign --------------------------------------------
  T.sign = (text) => canvasTex(512, (g, s) => {
    g.fillStyle = '#7a5a36'; g.fillRect(0, 0, s, s / 2);
    g.strokeStyle = '#3a2812'; g.lineWidth = 10; g.strokeRect(8, 8, s - 16, s / 2 - 16);
    g.globalAlpha = 0.2;
    for (let i = 0; i < 30; i++) {
      g.strokeStyle = '#3f2c14'; g.lineWidth = 1.5;
      const y = Math.random() * s / 2;
      g.beginPath(); g.moveTo(0, y); g.lineTo(s, y + (Math.random() - 0.5) * 6); g.stroke();
    }
    g.globalAlpha = 1;
    g.fillStyle = '#f4e3b2';
    g.font = `bold ${s * 0.19}px "Courier New", monospace`;
    g.textAlign = 'center'; g.textBaseline = 'middle';
    g.shadowColor = 'rgba(0,0,0,0.6)'; g.shadowOffsetY = 5; g.shadowBlur = 4;
    g.fillText(text, s / 2, s / 4);
  }, { repeat: 1 });

  return T;
}
